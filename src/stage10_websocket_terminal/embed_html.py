#!/usr/bin/env python3
"""Generate pico-ws-server's deterministic gzip HTML C header."""

from __future__ import annotations

import gzip
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: embed_html.py INPUT.html OUTPUT.h")
    source = Path(sys.argv[1]).read_bytes()
    compressed = gzip.compress(source, compresslevel=9, mtime=0)
    rows = []
    for offset in range(0, len(compressed), 12):
        chunk = compressed[offset : offset + 12]
        rows.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    header = (
        "#ifndef STATIC_HTML_HEX_H\n"
        "#define STATIC_HTML_HEX_H\n\n"
        "static const unsigned char static_html_gz[] = {\n"
        + "\n".join(rows)
        + "\n};\n"
        f"static const unsigned int static_html_gz_len = {len(compressed)}u;\n\n"
        "#endif\n"
    )
    Path(sys.argv[2]).write_text(header, encoding="ascii")


if __name__ == "__main__":
    main()