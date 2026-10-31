"""Compile and execute SDK initialization and memory boundaries on banked CP/M."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import bank1_system_boot_z80_test
import cpm_boot_z80_test


SOURCE = r"""
#include <stdio.h>
#include <string.h>
#include "BANKMEM.H"
#include "BIOSBANK.H"

#define CHECK(condition, message) do { \
    if (!(condition)) { puts("FAIL: " message); return 1; } \
} while (0)

static int check_context(void)
{
    static unsigned char host_fcb[36], host_dma[128], context[176];
    static unsigned char routine[] = {
        0xDD,0xE5, 0xDD,0x21,0,0, 0xDD,0x39,
        0xDD,0x5E,4, 0xDD,0x56,5, 0xD5, 0x21,48,0, 0x19,0xEB,
        0x0E,26, 0xCD,5,0, 0xD1, 0x0E,17, 0xCD,5,0,
        0x6F,0x26,0, 0xDD,0xE1,0xC9
    };
    static unsigned char setting[] = {0x1E,7,0x0E,32,0xCD,5,0,0x6F,0x26,0,0xC9};
    unsigned int code, stack, data, host_match, library_match, i;
    code = bank_memory_allocate(256);
    stack = bank_memory_allocate(256);
    data = bank_memory_allocate(256);
    CHECK(code && stack && data, "context test allocations");
    memset(host_fcb, 0, sizeof(host_fcb));
    memset(host_fcb + 1, '?', 8);
    memcpy(host_fcb + 1, "CTXH", 4);
    memcpy(host_fcb + 9, "DAT", 3);
    host_fcb[0] = 4;
    memcpy(context, host_fcb, sizeof(host_fcb));
    context[0] = 1;
    context[4] = 'L';
    CHECK(bank_memory_write(data, 0, context, sizeof(context)), "library search context");
    CHECK(bank_memory_write(code, 0, routine, sizeof(routine)), "search-first code");
    routine[27] = 18;
    CHECK(bank_memory_write(code, 48, routine, sizeof(routine)), "search-next code");
    bdos(26, (int)host_dma);
    host_match = bdos(17, (int)host_fcb);
    for (i = 0; i < 6; ++i)
    {
        CHECK(host_match < 4 &&
              !memcmp(host_dma + host_match * 32 + 1, "CTXH", 4) &&
              host_dma[host_match * 32 + 5] == '0' + i, "host search continuation");
        library_match = bank_memory_call(code, stack, i ? 48 : 0, data);
        CHECK(bank_bios_last_error() == BANK_BIOS_OK && library_match < 4,
              "library search continuation");
        CHECK(bank_memory_read(data, 48, context, 128), "library search DMA");
        CHECK(!memcmp(context + library_match * 32 + 1, "CTXL", 4) &&
              context[library_match * 32 + 5] == '0' + i, "library directory entry");
        host_match = bdos(18, (int)host_fcb);
    }
    CHECK(host_match == 255 && bank_memory_call(code, stack, 48, data) == 255,
          "independent search exhaustion");
    CHECK(bank_memory_write(code, 96, setting, sizeof(setting)), "user-setting code");
    bank_memory_call(code, stack, 96, 0);
    CHECK(bdos(32, 255) == 7, "library user change shared with host");
    bdos(32, 0);
    setting[1] = 255;
    CHECK(bank_memory_write(code, 96, setting, sizeof(setting)), "user-query code");
    CHECK(bank_memory_call(code, stack, 96, 0) == 0 &&
          bank_bios_last_error() == BANK_BIOS_OK, "host user change shared with library");
    setting[1] = 0;
    setting[3] = 14;
    CHECK(bank_memory_write(code, 96, setting, sizeof(setting)), "drive-setting code");
    bank_memory_call(code, stack, 96, 0);
    CHECK(bdos(25, 0) == 0, "library drive change shared with host");
    bdos(14, 3);
    setting[3] = 25;
    CHECK(bank_memory_write(code, 96, setting, sizeof(setting)), "drive-query code");
    CHECK(bank_memory_call(code, stack, 96, 0) == 3, "host drive change shared with library");
    CHECK(bank_memory_free(data) && bank_memory_free(stack) && bank_memory_free(code),
          "context test cleanup");
    return 0;
}

int main(void)
{
    unsigned char header[16], data[258], guard[16], revision, major;
    unsigned int first, second, available, i;
    CHECK(bank_bios_version() == BMBIOS_ABI, "BIOS version");
    major = *(unsigned char *)BMBIOS_VERSION;
    *(unsigned char *)BMBIOS_VERSION = BMBIOS_ABI_MAJOR + 1;
    CHECK(!bank_memory_initialize(), "reject other BIOS major version");
    *(unsigned char *)BMBIOS_VERSION = major;
    CHECK(bank_bios_read(0xE600, header, sizeof(header)), "read allocator descriptor");
    revision = header[8];
    header[8] = 4;
    CHECK(bank_bios_write(0xE600, header, sizeof(header)), "set old allocator revision");
    CHECK(!bank_memory_initialize(), "reject old allocator revision");
    header[8] = revision;
    CHECK(bank_bios_write(0xE600, header, sizeof(header)), "restore allocator descriptor");
    CHECK(bank_bios_read(0xE360, header, sizeof(header)), "read capabilities");
    header[2] = 0;
    CHECK(bank_bios_write(0xE360, header, sizeof(header)), "set unsupported capability revision");
    CHECK(!bank_memory_initialize() && bank_memory_last_error() == BANK_BIOS_UNAVAILABLE,
          "capability rejection has an error status");
    header[2] = 1;
    CHECK(bank_bios_write(0xE360, header, sizeof(header)), "restore capability revision");
    CHECK(bank_memory_initialize(), "initialize matching SDK");
    available = bank_memory_available();
    first = bank_memory_allocate(1);
    second = bank_memory_allocate(256);
    CHECK(first && second && bank_memory_size(first) == 256, "allocation and rounding");
    memset(guard, 0xCC, sizeof(guard));
    CHECK(bank_memory_write(second, 0, guard, sizeof(guard)), "set adjacent-block guard");
    for (i = 0; i < 255; ++i)
        data[i + 1] = (unsigned char)i;
    CHECK(bank_memory_write(first, 1, data + 1, 255), "unaligned write");
    memset(data, 0xA5, sizeof(data));
    CHECK(bank_memory_read(first, 1, data + 1, 255), "unaligned read");
    CHECK(data[0] == 0xA5 && data[256] == 0xA5, "host buffer guards");
    for (i = 0; i < 255; ++i)
        CHECK(data[i + 1] == (unsigned char)i, "chunk contents");
    CHECK(bank_memory_read(second, 0, data, sizeof(guard)), "read adjacent-block guard");
    CHECK(!memcmp(data, guard, sizeof(guard)), "adjacent block preserved");
    CHECK(!bank_memory_read(first, 256, data, 1), "object bounds");
    CHECK(bank_memory_last_error() == BANK_BIOS_ARGUMENT, "bounds status");
    CHECK(!bank_memory_write(first, 0xFFFF, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_ARGUMENT, "offset overflow");
    CHECK(!bank_memory_write(first, 1, data, 0xFFFF) &&
          bank_memory_last_error() == BANK_BIOS_ARGUMENT, "length overflow");
    CHECK(!bank_memory_write(first, 0, (void *)0xFEF1, 16) &&
          bank_memory_last_error() == BANK_BIOS_ARGUMENT, "aligned base overflow");
    CHECK(!bank_memory_read(first, 0, 0, 1), "null unaligned buffer");
    CHECK(!bank_memory_read(first, 0, 0, 16), "null aligned buffer");
    CHECK(bank_memory_read(first, 256, 0, 0), "zero-length read");
    CHECK(bank_memory_write(first, 256, 0, 0), "zero-length write");
    CHECK(!bank_memory_write(first, 257, 0, 0) &&
          bank_memory_last_error() == BANK_BIOS_ARGUMENT, "zero-length object bounds");
    CHECK(!bank_memory_size(first + 1), "interior handle");
    CHECK(bank_memory_last_error() == BANK_BIOS_HANDLE, "handle status");
    CHECK(!bank_memory_read(first + 1, 0, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_HANDLE, "interior copy handle");
    CHECK(!bank_memory_read(0, 0, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_HANDLE, "null copy handle");
    CHECK(!bank_memory_read(0xDD00, 0, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_HANDLE, "unallocated pool page");
    CHECK(!bank_memory_read(0xB000, 0, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_HANDLE, "page outside any extent");
    CHECK(!bank_memory_read(first, 0x2000, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_ARGUMENT, "offset reaches next extent");
    CHECK(bank_memory_read(0xC000, 0x07F0, data, 16) &&
          !bank_memory_read(0xC000, 0x07F1, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_ARGUMENT, "pinned stack extent bounds");
    CHECK(bank_memory_read(0x0100, 0xBEF0, data, 16) &&
          !bank_memory_read(0x0100, 0xBF00, data, 1), "pinned library extent bounds");
    CHECK(bank_memory_free(first), "free");
    CHECK(!bank_memory_read(first, 0, data, 1) &&
          !bank_memory_free(first), "stale handle and double free");
    CHECK(bank_memory_free(second) && bfree(0) &&
          bank_memory_available() == available, "full heap recovery and short symbol alias");
    CHECK(bank_memory_reset() && !bank_memory_write(first, 0, data, 16) &&
          bank_memory_last_error() == BANK_BIOS_HANDLE, "reset invalidates copy bounds");
    CHECK(bank_bios_version() == BMBIOS_ABI, "SDK returns to caller bank");
    CHECK(!check_context() && bank_memory_available() == available, "BDOS contexts and cleanup");
    puts("PASS: BANKSDK version checks, memory bounds and cleanup.");
    return 0;
}
"""


class BankSdkExecution(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[3]
        dcc = Path(os.environ.get("DCC_DIR", str(root.parent / "dcc")))
        if not (dcc / "dccmake").is_file():
            raise unittest.SkipTest("sibling DCC toolchain is unavailable")
        bank1_system_boot_z80_test.BankOneSystemBoot.setUpClass()
        temporary = tempfile.TemporaryDirectory(prefix="banksdk-check-")
        cls.addClassCleanup(temporary.cleanup)
        cls.folder = Path(temporary.name)
        sdk = root / "examples/BANKSDK"
        (cls.folder / "CHECK.C").write_text(SOURCE)
        (cls.folder / "dccmake.txt").write_text(
            f"dcc-input=CHECK.C,{sdk / 'BANKMEM.C'},{sdk / 'BIOSBANK.C'}\n"
            "dcc-output=SDKCHECK\ndcc-build-dir=build\ndcc-stack-bytes=512\n"
            f"dcc-include-directory={dcc},{sdk}\n"
            f"dcc-runtime={dcc / 'DCCRTL.MAC'}\n"
            f"dcc-tool={dcc / 'dcc'}\ndccpeep-tool={dcc / 'dccpeep'}\n"
            f"dccrtlstrip-tool={dcc / 'dccrtlstrip'}\n"
            f"m80c-tool={dcc / 'm80c'}\nl80c-tool={dcc / 'l80c'}\n")
        subprocess.run([str(dcc / "dccmake")], cwd=cls.folder, check=True,
                       capture_output=True, text=True)
        subprocess.run(
            [str(dcc / "dccmake"),
             f"dcc-input={root / 'examples/MEMPERF/MEMPERF.C'},{sdk / 'BANKMEM.C'},{sdk / 'BIOSBANK.C'}",
             "dcc-output=MEMPERF"],
            cwd=cls.folder, check=True, capture_output=True, text=True)

    def test_initialization_and_memory_boundaries(self):
        system = bank1_system_boot_z80_test.BankOneSystemBoot(
            "test_directory_file_read_and_warm_boot")
        system.setUp()
        disk = self.folder / "drive.img"
        disk.write_bytes(system.case.disks[3])
        definitions = self.folder / "diskdefs"
        definitions.write_text(cpm_boot_z80_test.build_images.cpm_diskdefs())
        subprocess.run(
            ["cpmcp", "-f", "z80sbc", str(disk),
             str(self.folder / "build/SDKCHECK.COM"), "0:SDKCHECK.COM"],
            cwd=self.folder, check=True, capture_output=True, text=True,
            env={**os.environ, "DISKDEFS": str(definitions)})
        system.case.disks[3][:] = disk.read_bytes()
        payload = self.folder / "ENTRY.DAT"
        payload.write_bytes(b"context regression\n")
        for drive, prefix in ((3, "CTXH"), (0, "CTXL")):
            disk.write_bytes(system.case.disks[drive])
            for index in range(6):
                subprocess.run(
                    ["cpmcp", "-f", "z80sbc", str(disk), str(payload),
                     f"0:{prefix}{index}.DAT"],
                    cwd=self.folder, check=True, capture_output=True, text=True,
                    env={**os.environ, "DISKDEFS": str(definitions)})
            system.case.disks[drive][:] = disk.read_bytes()
        system.case.until_prompt()
        system.case.command("D:")
        output = system.case.command("SDKCHECK")
        self.assertNotIn(b"FAIL:", output, output.decode(errors="replace"))
        self.assertIn(b"PASS: BANKSDK version checks, memory bounds and cleanup.", output)
        self.assertTrue(system.case.command("A:").endswith(b"A>"))

    def test_checked_copy_cycle_budget(self):
        from banked_sdk_benchmark import HighResidentAllocator, measure
        HighResidentAllocator.setUpClass()
        image = self.folder / "build/SDKCHECK.COM"
        for chunk, limits in ((4096, (94500, 94500)),
                              (256, (114000, 113500)),
                              (16, (440000, 435000)),
                              (1, (10800000, 14850000))):
            for writing in (False, True):
                for interrupts in (False, True):
                    with self.subTest(chunk=chunk, writing=writing, interrupts=interrupts):
                        result = measure(image, writing, chunk, interrupts)
                        self.assertLessEqual(result["tstates"], limits[int(writing)], result)
                        bursts = (chunk + 15) // 16 + int(writing and chunk % 16 != 0)
                        self.assertEqual(result["bank_switches"],
                                         2 * (4096 // chunk) * (1 + bursts))

    def test_memperf_repeated_workload_and_rate(self):
        system = bank1_system_boot_z80_test.BankOneSystemBoot()
        system.setUp()
        case = system.case
        case.until_prompt()
        machine = case.machine
        timer_bytes = []
        timing = False
        starts = reads = 0

        def output(port, value):
            nonlocal timing, starts, reads
            port &= 255
            if port == 0x17:
                if value == 0x40:
                    self.assertFalse(timing)
                    timing = True
                    starts += 1
                elif value == 0x60:
                    self.assertTrue(timing)
                    timing = False
                    reads += 1
                    timer_bytes.extend((100, 0, 0, 0))
                else:
                    self.assertEqual(value, 0)
            elif port == 7:
                system.banks[system.active][:] = machine.memory
                system.active = value & 1
                machine.memory[:] = system.banks[system.active]
            else:
                self.assertFalse(timing, "output or BDOS I/O inside timed copies")
                case.write_port(port, value)

        def input_byte(port):
            port &= 255
            if port == 0x17:
                return timer_bytes.pop(0) if timer_bytes else 0
            if port == 7:
                return 0xB4 | system.active
            return case.read_port(port)

        machine.set_output_callback(output)
        machine.set_input_callback(input_byte)
        image = (self.folder / "build/MEMPERF.COM").read_bytes()
        machine.memory[0x100:0x100 + len(image)] = image
        machine.pc = 0x100
        start = len(case.output)
        output_text = case.until_prompt(start)
        self.assertNotIn(b"FAIL:", output_text, output_text.decode(errors="replace"))
        self.assertEqual((starts, reads, timing, timer_bytes), (8, 8, False, []))
        self.assertEqual(output_text.count(b"100 ms; 1280.0 KiB/s"), 8)
        self.assertEqual(output_text.count(b"128 KiB per timed direction (32 repeats)"), 2)
        self.assertIn(b"PASS: MEMPERF complete; 5632 bytes free.", output_text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
