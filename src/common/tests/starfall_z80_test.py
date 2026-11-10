"""Run the built CP/M graphics game on the production BIOS and C emulator."""

from collections import deque
from pathlib import Path
import unittest

import bank1_system_boot_z80_test as boot
import cpm_boot_z80_test


class StarfallExecution(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        boot.BankOneSystemBoot.setUpClass()
        cls.program = (Path(__file__).resolve().parents[3] /
                       "examples/STARFALL/STARFALL.COM").read_bytes()

    def play(self, mode="quit", reply=b"Z80G1\r", timer_error=False):
        fixture = boot.BankOneSystemBoot()
        fixture.setUp()
        case = fixture.case
        case.disks[0][:] = cpm_boot_z80_test.install_program(
            bytes(case.disks[0]), "STARFALL.COM", self.program)
        case.until_prompt()
        commands = []
        timer = deque()
        milliseconds = 0
        paddle = 136
        frame_count = 0
        self.paddle_times = []
        self.star_times = []
        start = len(case.output)
        original_read = case.read_port
        original_write = case.write_port

        def read(port):
            if port & 255 == 7:
                return 0xB4 | case.machine.active_bank
            if port & 255 == 0x17:
                return timer.popleft() if timer else 0
            return original_read(port)

        def write(port, value):
            nonlocal milliseconds, paddle, frame_count
            if port & 255 == 7:
                return
            if port & 255 == 0x17:
                if value == 0x40:
                    milliseconds = 0
                elif value == 0x60:
                    milliseconds += 1 if mode == "pacing" else 10
                    timer.extend((0xFFFFFFFF if timer_error else milliseconds).to_bytes(4, "little"))
                return
            original_write(port, value)
            if port & 255 != 0 or not case.output.endswith(b"\x1b\\"):
                return
            command = bytes(case.output).rsplit(b"\x1b]777;", 1)[-1][:-2].decode("ascii")
            commands.append(command)
            fields = command.split(";")
            if fields[2] == "open" and reply:
                case.input.extend(reply)
            elif fields[2] == "move":
                if fields[3] == "0":
                    paddle = int(fields[4])
                    self.paddle_times.append(milliseconds)
                else:
                    self.star_times.append(milliseconds)
                    if mode == "win":
                        target = max(0, min(272, int(fields[4]) - 20))
                        distance = target - paddle
                        case.input.extend(([4] if distance > 0 else [19]) * (abs(distance) // 12))
            elif fields[2] == "present":
                frame_count += 1
                if mode == "quit":
                    case.input.extend(b"q")
                elif mode == "pacing" and len(self.star_times) == 3:
                    case.input.extend(b"q")
                elif mode == "clamp":
                    if frame_count == 1:
                        case.input.extend(b"a" * 30)
                    elif frame_count == 8:
                        case.input.extend(b"d" * 30)
                    elif frame_count == 16:
                        case.input.extend(b"q")

        case.machine.set_input_callback(read)
        case.machine.set_output_callback(write)
        command_start = len(case.output)
        case.input.extend(b"STARFALL\r")
        for _ in range(2000):
            case.machine.ticks_to_stop = 100000
            case.machine.run()
            self.assertFalse(case.machine.halted)
            if case.output[command_start:].endswith(b">"):
                break
        else:
            self.fail(f"Game exceeded 200M-cycle budget; frames={frame_count}, pc={case.machine.pc:04x}")
        output = bytes(case.output[command_start:])
        self.assertIn(b"A>", output)
        self.assertFalse(case.input, "Game must consume its reply and controls")
        for command in commands:
            self.assertLessEqual(len(command), 128)
            self.assertRegex(command, r"^Z80G;1;[a-z]+(?:;[0-9]+)*$")
        return output, commands, bytes(case.output[start:])

    def test_quit_closes_canvas_and_returns_to_cpm(self):
        output, commands, _ = self.play()
        self.assertIn(b"Stopped.", output)
        self.assertIn("Z80G;1;sprite;0;48;8;11", commands)
        self.assertIn("Z80G;1;present", commands)
        self.assertEqual(commands[-1], "Z80G;1;close")

    def test_paddle_and_stars_update_every_50_ms(self):
        self.play(mode="pacing")
        self.assertEqual(len(self.star_times), 3)
        self.assertEqual(
            [later - earlier for earlier, later in zip(self.star_times, self.star_times[1:])],
            [50, 50],
        )
        self.assertEqual(len(self.paddle_times), 3)
        self.assertEqual(
            [later - earlier for earlier, later in zip(self.paddle_times, self.paddle_times[1:])],
            [50, 50],
        )

    def test_missing_and_rejected_graphics_terminal_fail_cleanly(self):
        for reply in (b"", b"Z80G0\r"):
            with self.subTest(reply=reply):
                output, commands, _ = self.play(reply=reply)
                self.assertIn(b"Graphics terminal unavailable", output)
                self.assertEqual([cmd.split(";")[2] for cmd in commands], ["open", "close"])

    def test_timer_failure_is_explicit(self):
        output, commands, _ = self.play(timer_error=True)
        self.assertIn(b"timer failed", output)
        self.assertEqual(commands, [])

    def test_controls_clamp_to_canvas_edges(self):
        output, commands, _ = self.play(mode="clamp")
        self.assertIn(b"Stopped.", output)
        positions = [int(cmd.split(";")[4]) for cmd in commands if cmd.startswith("Z80G;1;move;0;")]
        self.assertIn(0, positions)
        self.assertIn(272, positions)
        self.assertTrue(all(0 <= x <= 272 for x in positions))

    def test_catching_ten_stars_wins(self):
        output, commands, _ = self.play(mode="win")
        self.assertIn(b"Caught 10/10; missed 0/3.", output)
        self.assertIn(b"You win!", output)
        self.assertEqual(commands[-1], "Z80G;1;close")

    def test_three_misses_end_game(self):
        output, commands, _ = self.play(mode="lose")
        self.assertIn(b"missed 3/3.", output)
        self.assertIn(b"Game over.", output)
        self.assertEqual(commands[-1], "Z80G;1;close")


if __name__ == "__main__":
    unittest.main()
