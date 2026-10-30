"""Native failure-path tests for the shared typed bank-library transport."""

from pathlib import Path
import subprocess
import tempfile
import unittest


class BankLibraryRuntime(unittest.TestCase):
    def test_transport_failure_paths_and_handle_token_exhaustion(self) -> None:
        source = Path(__file__).with_suffix(".c")
        with tempfile.TemporaryDirectory(prefix="banklib-runtime-") as directory:
            executable = Path(directory) / "runtime-test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(source), "-o", str(executable)],
                check=True, capture_output=True, text=True)
            result = subprocess.run(
                [str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS:", result.stdout)
