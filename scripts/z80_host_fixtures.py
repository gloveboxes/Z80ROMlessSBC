"""Build Z80 regression programs with the host DCC toolchain."""

from __future__ import annotations

import ast
import os
from pathlib import Path
import shutil
import subprocess


def build(workspace: Path, dcc: Path, selection: str | None) -> None:
    env = {**os.environ, "DCC_DIR": str(dcc),
           "PATH": f"{dcc}{os.pathsep}{os.environ['PATH']}"}

    def run(args: list[str], cwd: Path, *, check: bool = True):
        print("+", " ".join(args), flush=True)
        result = subprocess.run(args, cwd=cwd, env=env, capture_output=True, text=True)
        if check and result.returncode:
            raise RuntimeError(f"Command failed ({result.returncode}): {args}\n"
                               f"{result.stdout}\n{result.stderr}")
        return result

    def example(name: str, *flags: str) -> None:
        folder = workspace / "examples" / name
        (folder / "build").mkdir(exist_ok=True)
        run([str(dcc / "dccmake"), *flags], folder)

    full = selection is None
    if full or selection == "banked_heap_z80_test.py":
        example("BANKED", "dcc-define=BANKED_ONE_CYCLE", "dcc-build-dir=build/emulator")
        example("PRIMEBK1")
    if full or selection == "allocperf_z80_test.py":
        example("ALLOCPRF", "dcc-define=ITERATIONS=4", "dcc-build-dir=build/emulator")
    if full or selection in ("memperf_z80_test.py", "benchmark:sdk"):
        example("MEMPERF")

    need_lib = full or selection in ("bank_sdk_z80_test.py", "bank1_bdos_z80_test.py",
                                      "bank1_system_boot_z80_test.py")
    if need_lib:
        example("LIBDEMO")
    if full or selection == "bank1_system_boot_z80_test.py":
        lib = workspace / "examples/LIBDEMO"
        fixture = workspace / "build/z80-fixtures/libdemo-1024"
        run([str(dcc / "dccmake"), "dcc-stack-bytes=1024"], lib)
        fixture.mkdir(parents=True)
        shutil.copy2(lib / "lib/build/DATA.LIB", fixture / "DATA.LIB")
        run([str(dcc / "dccmake")], lib)

    if full or selection == "bank_sdk_z80_test.py":
        source = workspace / "src/common/tests/bank_sdk_z80_test.py"
        # Extract the fixture without importing the container-only emulator.
        module = ast.parse(source.read_text())
        definition = next(node for node in module.body if isinstance(node, ast.Assign)
                          and any(isinstance(target, ast.Name) and target.id == "SOURCE"
                                  for target in node.targets))
        sdk = workspace / "examples/BANKSDK"
        folder = workspace / "build/z80-fixtures/sdk"
        folder.mkdir(parents=True)
        (folder / "CHECK.C").write_text(ast.literal_eval(definition.value))
        (folder / "dccmake.txt").write_text(
            f"dcc-input=CHECK.C,{sdk / 'BANKMEM.C'},{sdk / 'BIOSBANK.C'}\n"
            "dcc-output=SDKCHECK\ndcc-build-dir=build\ndcc-stack-bytes=512\n"
            f"dcc-include-directory={dcc},{sdk}\n"
            f"dcc-runtime={dcc / 'DCCRTL.MAC'}\n"
            f"dcc-tool={dcc / 'dcc'}\ndccpeep-tool={dcc / 'dccpeep'}\n"
            f"dccrtlstrip-tool={dcc / 'dccrtlstrip'}\n"
            f"m80c-tool={dcc / 'm80c'}\nl80c-tool={dcc / 'l80c'}\n")
        run([str(dcc / "dccmake")], folder)
        run([str(dcc / "dccmake"),
             f"dcc-input={workspace / 'examples/MEMPERF/MEMPERF.C'},{sdk / 'BANKMEM.C'},{sdk / 'BIOSBANK.C'}",
             "dcc-output=MEMPERF"], folder)

    if full or selection == "bank1_bdos_z80_test.py":
        folder = workspace / "build/z80-fixtures/bdos"
        folder.mkdir(parents=True)
        (folder / "LIB.MAC").write_text(
            "\tcseg\n\tpublic _b1e00\n_b1e00:\n"
            "\tld hl,1\n\tret\n"
            "\tpublic _b1s00\n_b1s00:\n\tdb 'u16()',0\n\tend\n")
        run([str(dcc / "m80c"), "=LIB.MAC", "/X", "/O", "/Z", "/L", "/C"], folder)
        base = [str(dcc / "l80c"), "/P:100,LIB", "--lib",
                "--export", "VALUE=_b1e00", "-o", "TYPED"]
        signature = ["--signature", "VALUE=_b1s00"]
        run(base + signature, folder)
        shutil.copy2(folder / "TYPED.LIB", folder / "TYPED1.LIB")
        eight = base + signature
        for index in range(1, 8):
            eight += ["--export", f"V{index}=_b1e00", "--signature", f"V{index}=_b1s00"]
        run(eight, folder)
        shutil.copy2(folder / "TYPED.LIB", folder / "TYPED8.LIB")
        for arguments in ([], ["--signature", "OTHER=_b1s00"],
                          signature + signature, ["--signature", "VALUE=_none"],
                          ["--signature", "VALUE="]):
            result = run(base + arguments, folder, check=False)
            if result.returncode == 0 or "l80c:" not in result.stderr:
                raise RuntimeError(f"Expected linker rejection for {arguments}: {result.stderr}")
        (folder / "HEAP.C").write_text(
            '#include <stdlib.h>\nstatic unsigned char bss[2048];\n'
            'char hsig[] = "u16(u16)";\n'
            'unsigned int check(unsigned int *unused) {\n'
            ' unsigned char *p; (void)unused;\n'
            ' if (bss[0] || bss[2047]) return 0;\n'
            ' p = (unsigned char *)malloc(32768U); if (!p) return 0;\n'
            ' p[0] = 17; p[32767] = 29; bss[0] = p[0]; bss[2047] = p[32767];\n'
            ' free(p); return bss[0] == 17 && bss[2047] == 29 ? 0xBEEF : 0;\n}\n')
        run([str(dcc / "dcc"), "-stack", "512", "HEAP.C", "-o", "HEAP.MAC"], folder)
        run([str(dcc / "dccrtlstrip"), "-r",
             str(workspace / "examples/LIBDEMO/lib/build/DCCRTL.MAC"),
             "-o", "RTLMIN.MAC", "HEAP.MAC"], folder)
        runtime = folder / "RTLMIN.MAC"
        runtime.write_text(runtime.read_text().replace("\tend\tstart", "\tend"))
        run([str(dcc / "m80c"), "=HEAP.MAC", "/X", "/O", "/Z", "/L"], folder)
        run([str(dcc / "m80c"), "=RTLMIN.MAC", "/X", "/O", "/Z", "/L"], folder)
        run([str(dcc / "l80c"), "/P:100,RTLMIN,HEAP", "--lib",
             "--stack-bytes", "512", "--export", "_HEAP=_CHECK",
             "--signature", "_HEAP=_HSIG", "-o", "HEAP"], folder)
