#!/usr/bin/env python3
"""Run the shared CP/M container against this checkout."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import z80_host_fixtures

ROOT = Path(__file__).resolve().parents[1]
CONTAINER = ROOT / "external/z80-cpm-container"
SESSION = ROOT / "build/cpm-session"
IMAGE = "z80-cpm:local"
DISKS = (
    ("drive_a_cpm63k-z80.img", "drive_a_cpm63k.img"),
    ("drive_b_bdsc.img", "drive_b_bdsc.img"),
    ("drive_c_escape.img", "drive_c_escape.img"),
    ("drive_d_blank.img", "drive_d_blank.img"),
)


def command(*args: str) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def require_container() -> None:
    if not (CONTAINER / "docker/start.py").is_file():
        raise SystemExit("Initialize the container: git submodule update --init --recursive")
    if sys.platform != "darwin":
        raise SystemExit("Apple container workflows require macOS")
    if "," in str(ROOT):
        raise SystemExit("The workspace path cannot contain commas")


def prepare() -> None:
    require_container()
    command(sys.executable, str(CONTAINER / "docker/prepare.py"))
    SESSION.mkdir(parents=True, exist_ok=True)
    command(
        "container", "run", "--rm",
        "--mount", f"type=bind,source={ROOT},target=/workspace,readonly",
        "--mount", f"type=bind,source={SESSION},target=/output",
        "--entrypoint", "python3", IMAGE,
        "/workspace/src/cpm/build_images.py", "--output-dir", "/output",
    )
    shutil.copyfile(SESSION / DISKS[0][0], SESSION / DISKS[0][1])


def test_image_tag() -> str:
    digest = hashlib.sha256()

    def add_file(path: Path, name: str) -> None:
        digest.update(name.encode("utf-8") + b"\0")
        if path.is_symlink():
            digest.update(b"link\0" + os.readlink(path).encode("utf-8") + b"\0")
        else:
            stat = path.stat()
            digest.update(f"{stat.st_mode & 0o777}:{stat.st_size}\0".encode("ascii"))
            with path.open("rb") as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(block)
            digest.update(b"\0")

    add_file(ROOT / "scripts/z80-tests.Dockerfile", "test.Dockerfile")
    for directory, dirs, files in os.walk(CONTAINER, followlinks=False):
        relative = Path(directory).relative_to(CONTAINER)
        dirs[:] = sorted(name for name in dirs
                         if name not in {".git", "__pycache__"}
                         and not (relative == Path(".") and name == "build"))
        digest.update(f"directory:{relative}\0".encode("utf-8"))
        for name in sorted(files + [name for name in dirs if (Path(directory) / name).is_symlink()]):
            path = Path(directory) / name
            if name == ".DS_Store" or relative / name == Path("runtime/altair_env.txt"):
                continue
            add_file(path, str(relative / name))
    return f"z80-cpm-tests:{digest.hexdigest()}"


def ensure_test_image(rebuild: bool = False) -> str:
    image = test_image_tag()
    result = subprocess.run(["container", "image", "list", "--format", "json"],
                            cwd=ROOT, check=True, capture_output=True, text=True)
    images = json.loads(result.stdout)
    if not rebuild and any(item["configuration"]["name"] == f"docker.io/library/{image}"
                           for item in images):
        print(f"Using cached test image {image}", flush=True)
        return image
    command(sys.executable, str(CONTAINER / "docker/prepare.py"))
    command("container", "build", "-f", str(ROOT / "scripts/z80-tests.Dockerfile"),
            "-t", image, str(CONTAINER))
    return image


def assemble(arguments: list[str]) -> None:
    parser = argparse.ArgumentParser(description="Assemble a checkout source in the container")
    parser.add_argument("-I", dest="includes", type=Path, action="append", default=[])
    parser.add_argument("-o", dest="output", type=Path, required=True)
    parser.add_argument("source", type=Path)
    args = parser.parse_args(arguments)

    def workspace_path(path: Path, *, directory: bool = False) -> str:
        resolved = path.expanduser().resolve()
        if not (resolved.is_dir() if directory else resolved.is_file()):
            raise SystemExit(f"Assembly input not found: {resolved}")
        try:
            relative = resolved.relative_to(ROOT)
        except ValueError:
            raise SystemExit(f"Assembly input must be inside the checkout: {resolved}")
        return f"/workspace/{relative.as_posix()}"

    source = workspace_path(args.source)
    includes = [workspace_path(path, directory=True) for path in args.includes]
    output = args.output.expanduser().resolve()
    if "," in str(output.parent):
        raise SystemExit(f"Output path cannot contain commas: {output.parent}")
    output.parent.mkdir(parents=True, exist_ok=True)
    image = ensure_test_image()
    include_args = [item for path in includes for item in ("-I", path)]
    command("container", "run", "--rm",
            "--mount", f"type=bind,source={ROOT},target=/workspace,readonly",
            "--mount", f"type=bind,source={output.parent},target=/output",
            "--entrypoint", "z80asm", image, *include_args,
            "-o", f"/output/{output.name}", source)


def build_images(arguments: list[str]) -> None:
    parser = argparse.ArgumentParser(description="Build CP/M images in the container")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--firmware", type=Path)
    args = parser.parse_args(arguments)
    output = args.output_dir.expanduser().resolve()
    if "," in str(output):
        raise SystemExit(f"Output path cannot contain commas: {output}")
    firmware_mount = []
    firmware_args = []
    if args.firmware is not None:
        firmware = args.firmware.expanduser().resolve()
        if not firmware.is_file() or "," in str(firmware.parent):
            raise SystemExit(f"Firmware not found or path contains commas: {firmware}")
        firmware_mount = ["--mount", f"type=bind,source={firmware.parent},target=/firmware,readonly"]
        firmware_args = ["--firmware", f"/firmware/{firmware.name}"]
    output.mkdir(parents=True, exist_ok=True)
    image = ensure_test_image()
    command("container", "run", "--rm",
            "--mount", f"type=bind,source={ROOT},target=/workspace,readonly",
            "--mount", f"type=bind,source={output},target=/output",
            *firmware_mount, "--entrypoint", "python3", image,
            "/workspace/src/cpm/build_images.py", "--output-dir", "/output",
            *firmware_args)


def run_tests(arguments: list[str]) -> None:
    parser = argparse.ArgumentParser(description="Run container Z80 regressions")
    parser.add_argument("--rebuild", action="store_true")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--test")
    selection.add_argument("--benchmark", choices=("sdk", "transfer", "services"))
    options = parser.parse_args(arguments)
    selected = options.test or (f"benchmark:{options.benchmark}" if options.benchmark else None)
    needs_dcc = selected is None or selected in {
        "banked_heap_z80_test.py", "allocperf_z80_test.py", "memperf_z80_test.py",
        "bank_sdk_z80_test.py", "bank1_bdos_z80_test.py",
        "bank1_system_boot_z80_test.py", "benchmark:sdk",
    }
    ft_mount = []
    ft_argument = []
    if selected is None:
        ft = Path(os.environ.get(
            "Z80_FT_SOURCE",
            ROOT.parent / "esp32/esp32-altair-8800/Apps/disk-c-apps/FT/FT.ASM",
        )).expanduser().resolve()
        if not ft.is_file() or "," in str(ft.parent):
            raise SystemExit(f"FT source not found or path contains commas: {ft}; set Z80_FT_SOURCE")
        ft_mount = ["--mount", f"type=bind,source={ft.parent},target=/ft-source,readonly"]
        ft_argument = ["--ft-source", f"/ft-source/{ft.name}"]
    arguments = [argument for argument in arguments if argument != "--rebuild"]
    dcc = None
    if needs_dcc:
        executable = shutil.which("dccmake")
        dcc = Path(os.environ.get("DCC_DIR", Path(executable).resolve().parent
                                  if executable else ROOT.parent / "dcc")).expanduser().resolve()
        missing = [name for name in ("dccmake", "dcc", "m80c", "l80c", "dccpeep",
                                     "dccrtlstrip", "DCCRTL.MAC") if not (dcc / name).is_file()]
        if missing:
            raise SystemExit(f"Host DCC tools missing in {dcc}: {', '.join(missing)}; set DCC_DIR")
    image = ensure_test_image(options.rebuild)
    with tempfile.TemporaryDirectory(prefix="z80-regressions-") as directory:
        workspace = Path(directory) / "workspace"
        workspace.mkdir()
        for folder in ("src", "scripts", "examples"):
            shutil.copytree(ROOT / folder, workspace / folder,
                            ignore=shutil.ignore_patterns("build", "__pycache__", ".DS_Store"))
        if dcc is not None:
            z80_host_fixtures.build(workspace, dcc, selected)
        if "," in str(workspace):
            raise SystemExit("The temporary workspace path cannot contain commas")
        command(
            "container", "run", "--rm",
            "--mount", f"type=bind,source={workspace},target=/workspace",
            *ft_mount,
            "--entrypoint", "python3", image,
            "/workspace/scripts/z80_container_tests.py", *ft_argument,
            *arguments,
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "start", "test", "images",
                                           "assemble", "image"))
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    require_container()
    if args.action == "prepare":
        if args.arguments:
            parser.error("prepare takes no arguments")
        prepare()
    elif args.action == "start":
        prepare()
        command(sys.executable, str(CONTAINER / "docker/start.py"),
                "--skip-build", "--disk-dir", str(SESSION),
                "--sbc-root", str(ROOT), *args.arguments)
    elif args.action == "test":
        run_tests(args.arguments)
    elif args.action == "images":
        build_images(args.arguments)
    elif args.action == "assemble":
        assemble(args.arguments)
    else:
        if args.arguments:
            parser.error("image takes no arguments")
        ensure_test_image()


if __name__ == "__main__":
    main()
