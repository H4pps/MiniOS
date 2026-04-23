"""Exercise serial matching and subprocess cleanup without an emulator."""

import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("qemu", Path(__file__).parents[2] / "scripts/qemu.py")
qemu = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = qemu
SPEC.loader.exec_module(qemu)
DISCOVERY = "os.write(1, " + repr(qemu.discovery_line()) + ")\n"


class BootRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=0.5, runner=qemu.boot_test, command_args=()):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "fake_qemu.py"
            script.write_text("import os, signal, sys, time\n" + body)
            result = runner([sys.executable, "-u", str(script), *command_args], timeout)
        # waitpid distinguishes a reaped child from a terminated zombie.
        with self.assertRaises(ChildProcessError):
            os.waitpid(result.pid, os.WNOHANG)
        with self.assertRaises(ProcessLookupError):
            os.kill(result.pid, 0)
        return result

    def test_success(self):
        result = self.run_fake(DISCOVERY + "print('mini-os: boot OK\\r', flush=True)\ntime.sleep(30)\n")
        self.assertTrue(result.success)
        self.assertEqual(result.stdout, qemu.discovery_line() + b"mini-os: boot OK\r\n")

    def test_fragmented_success(self):
        result = self.run_fake(
            DISCOVERY + "sys.stdout.write('mini-os: boot '); sys.stdout.flush()\n"
            "time.sleep(0.05)\nprint('OK', flush=True)\ntime.sleep(30)\n"
        )
        self.assertTrue(result.success)

    def test_failure_marker(self):
        result = self.run_fake("print('mini-os: boot FAIL: bss', flush=True)\ntime.sleep(30)\n")
        self.assertFalse(result.success)
        self.assertIn("Kernel reported", result.reason)
        self.assertIn(b"bss", result.stdout)

    def test_failure_overrides_success(self):
        result = self.run_fake(
            "sys.stdout.write('mini-os: boot OK\\nmini-os: boot FAIL: error\\n'); "
            "sys.stdout.flush()\ntime.sleep(30)\n"
        )
        self.assertFalse(result.success)

    def test_partial_output(self):
        result = self.run_fake("sys.stdout.write('mini-os: boot OK'); sys.stdout.flush()\ntime.sleep(30)\n")
        self.assertFalse(result.success)
        self.assertIn("Timed out", result.reason)
        self.assertEqual(result.stdout, b"mini-os: boot OK")

    def test_exact_line_required(self):
        result = self.run_fake("print('prefix mini-os: boot OK', flush=True)\ntime.sleep(30)\n")
        self.assertFalse(result.success)

    def test_stderr_is_not_serial(self):
        result = self.run_fake("print('mini-os: boot OK', file=sys.stderr, flush=True)\ntime.sleep(30)\n")
        self.assertFalse(result.success)
        self.assertIn(b"mini-os: boot OK", result.stderr)

    def test_early_exit_preserves_diagnostics(self):
        result = self.run_fake("print('startup', flush=True)\nprint('bad image', file=sys.stderr)\nsys.exit(7)\n")
        self.assertFalse(result.success)
        self.assertIn("exited prematurely (7)", result.reason)
        self.assertEqual(result.stdout, b"startup\n")
        self.assertEqual(result.stderr, b"bad image\n")

    def test_exit_after_success_line_is_failure(self):
        result = self.run_fake("print('mini-os: boot OK', flush=True)\nsys.exit(0)\n")
        self.assertFalse(result.success)
        self.assertIn("exited prematurely (0)", result.reason)

    def test_timeout_kills_uncooperative_process(self):
        result = self.run_fake("signal.signal(signal.SIGTERM, signal.SIG_IGN)\ntime.sleep(30)\n")
        self.assertFalse(result.success)
        self.assertIn("Timed out", result.reason)

    def test_missing_image(self):
        with self.assertRaisesRegex(RuntimeError, "Missing kernel image"):
            qemu.qemu_command(Path("/does-not-exist/kernel.elf"), sys.executable)

    def test_missing_tool(self):
        with tempfile.NamedTemporaryFile() as image:
            with self.assertRaisesRegex(RuntimeError, "Missing QEMU tool"):
                qemu.qemu_command(Path(image.name), "nonexistent-mini-os-qemu")

    def test_spawn_failure(self):
        with self.assertRaises(FileNotFoundError):
            qemu.boot_test(["/does-not-exist/qemu"])


if __name__ == "__main__":
    unittest.main()
