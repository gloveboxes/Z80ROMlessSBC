"""Independent JLCDFM-style silkscreen-to-pad audit of exported Gerbers.

Usage: python audit-silk-pad.py <dir containing *_Mask and *_Silkscreen Gerbers>
Exits non-zero when any silk stroke is <0.18 mm from a same-side mask opening.
"""
import math
import re
import sys
from pathlib import Path

NUM = r"[-+]?\d*\.?\d+"


def parse(path):
    text = Path(path).read_text()
    macros = {}
    for name, body in re.findall(r"%AM(\w+)\*(.*?)%", text, re.S):
        macros[name] = re.sub(r"\s", "", body)
    apertures = {}
    for code, kind, params in re.findall(r"%ADD(\d+)([\w]+),?([^*]*)\*%", text):
        values = [float(v) for v in re.findall(NUM, params.replace("X", " "))]
        apertures[int(code)] = (kind, values)
    cmds = re.sub(r"%.*?%", "", text, flags=re.S).replace("\n", "")
    return macros, apertures, [c for c in cmds.split("*") if c]


def rot(points, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return [(x * c - y * s, x * s + y * c) for x, y in points]


def shape(macros, kind, v):
    """Return (core points, inflation radius) relative to the flash origin."""
    if kind == "C":
        return [(0, 0)], v[0] / 2
    if kind == "R":
        w, h = v[0] / 2, v[1] / 2
        return [(-w, -h), (w, -h), (w, h), (-w, h)], 0
    if kind == "O":
        w, h = v[0] / 2, v[1] / 2
        if w > h:
            return [(-(w - h), 0), (w - h, 0)], h
        return [(0, -(h - w)), (0, h - w)], w
    if kind == "RoundRect":
        r, pts, angle = v[0], v[1:9], v[9] if len(v) > 9 else 0
        return rot(list(zip(pts[0::2], pts[1::2])), angle), r
    if kind in macros:
        body = macros[kind].split("*")
        prim = [b for b in body if b.startswith("4,")][0]
        nums = [float(n) for n in re.findall(NUM, prim.replace("$1", ""))]
        count = int(nums[2])
        pts = list(zip(nums[3::2], nums[4::2]))[: count + 1]
        return rot(pts, v[0] if v else 0), 0
    raise SystemExit(f"unsupported aperture {kind}")


def seg_point(p, a, b):
    ax, ay = a
    dx, dy = b[0] - ax, b[1] - ay
    l2 = dx * dx + dy * dy
    t = 0 if l2 == 0 else max(0, min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / l2))
    return math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)


def cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def seg_seg(a, b, c, d):
    if (cross(a, b, c) * cross(a, b, d) < 0) and (cross(c, d, a) * cross(c, d, b) < 0):
        return 0.0
    return min(seg_point(a, c, d), seg_point(b, c, d),
               seg_point(c, a, b), seg_point(d, a, b))


def inside(p, poly):
    hit = False
    for i in range(len(poly)):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % len(poly)]
        if (y1 > p[1]) != (y2 > p[1]) and \
                p[0] < (x2 - x1) * (p[1] - y1) / (y2 - y1) + x1:
            hit = not hit
    return hit


def distance(a, b, poly):
    if len(poly) == 1:
        return seg_point(poly[0], a, b)
    if len(poly) >= 3 and (inside(a, poly) or inside(b, poly)):
        return 0.0
    edges = len(poly) if len(poly) >= 3 else 1
    return min(seg_seg(a, b, poly[i], poly[(i + 1) % len(poly)])
               for i in range(edges))


def walk(path):
    macros, apertures, cmds = parse(path)
    x = y = 0.0
    ap = None
    mode = "G01"
    out = []
    for c in cmds:
        for g in re.findall(r"G0[123]", c):
            mode = g
        m = re.match(r"D(\d+)$", c.replace("G54", ""))
        if m and int(m.group(1)) >= 10:
            ap = int(m.group(1))
            continue
        m = re.search(r"(?:X(-?\d+))?(?:Y(-?\d+))?(?:I(-?\d+))?(?:J(-?\d+))?D0([123])", c)
        if not m:
            continue
        nx = int(m.group(1)) / 1e6 if m.group(1) else x
        ny = int(m.group(2)) / 1e6 if m.group(2) else y
        op = m.group(5)
        if op == "1":
            if mode == "G01":
                out.append(("seg", (x, y), (nx, ny), ap))
            else:
                cx = x + int(m.group(3) or 0) / 1e6
                cy = y + int(m.group(4) or 0) / 1e6
                r = math.hypot(x - cx, y - cy)
                a0 = math.atan2(y - cy, x - cx)
                a1 = math.atan2(ny - cy, nx - cx)
                if mode == "G03" and a1 <= a0:
                    a1 += 2 * math.pi
                if mode == "G02" and a1 >= a0:
                    a1 -= 2 * math.pi
                n = max(8, int(abs(a1 - a0) * r / 0.02))
                pts = [(cx + r * math.cos(a0 + (a1 - a0) * i / n),
                        cy + r * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]
                out.extend(("seg", pts[i], pts[i + 1], ap) for i in range(n))
        elif op == "3":
            out.append(("flash", (nx, ny), None, ap))
        x, y = nx, ny
    return macros, apertures, out


def audit(mask_path, silk_path, label):
    mm, ma, mask_ops = walk(mask_path)
    _, sa, silk_ops = walk(silk_path)
    pads = []
    for kind, pos, _, ap in mask_ops:
        if kind != "flash":
            raise SystemExit("mask contains non-flash geometry")
        core, r = shape(mm, *ma[ap])
        pts = [(pos[0] + px, pos[1] + py) for px, py in core]
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        pads.append((pts, r, (min(xs) - r, min(ys) - r, max(xs) + r, max(ys) + r), pos))
    results = []
    for kind, a, b, ap in silk_ops:
        k, v = sa[ap]
        if k != "C":
            raise SystemExit(f"silk aperture {k} unsupported")
        half = v[0] / 2
        if kind == "flash":
            b = a
        best = None
        for pts, r, box, pos in pads:
            if max(a[0], b[0]) < box[0] - 1 or min(a[0], b[0]) > box[2] + 1 or \
                    max(a[1], b[1]) < box[1] - 1 or min(a[1], b[1]) > box[3] + 1:
                continue
            d = distance(a, b, pts) - r - half
            if best is None or d < best[0]:
                best = (d, pos)
        if best:
            results.append((best[0], a, b, best[1]))
    danger = [r for r in results if r[0] < 0.13]
    warn = [r for r in results if 0.13 <= r[0] < 0.18]
    low = min(results)[0] if results else float("inf")
    print(f"{label}: {len(silk_ops)} silk strokes, {len(pads)} mask openings, "
          f"min clearance {low:.4f} mm, danger={len(danger)} warning={len(warn)}")
    for r in sorted(danger + warn)[:15]:
        print(f"   {r[0]:.4f} silk {r[1]}->{r[2]} pad@{r[3]}")
    return not danger and not warn


if __name__ == "__main__":
    root = Path(sys.argv[1])
    ok = audit(next(root.glob("*F_Mask.gts")), next(root.glob("*F_Silkscreen.gto")), "front")
    ok &= audit(next(root.glob("*B_Mask.gbs")), next(root.glob("*B_Silkscreen.gbo")), "back")
    sys.exit(0 if ok else 1)
