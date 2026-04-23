"""Resource matching, expected rejection, and one deadline across QEMU launches."""

import os
import subprocess
import time
import unittest
from unittest.mock import patch
import test_boot_runner
from test_uart_runner import CONSOLE

qemu = test_boot_runner.qemu
FAILURE = b"mini-os: boot FAIL: dtb bad magic"


class FdtRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=3, runner=qemu.fdt_test):
        return test_boot_runner.BootRunnerTests.run_fake(
            self, body, timeout, runner=runner, command_args=("-m", "128M")
        )

    def test_three_scenarios_and_all_processes_are_reaped(self):
        # Substitute only the RAM size in a separately implemented fake console.
        console = CONSOLE.replace(repr(qemu.discovery_line()), "resource_line")
        body = (
            "if '-device' in sys.argv:\n"
            "    import socket, json\n"
            "    path = sys.argv[sys.argv.index('-qmp') + 1].split(',')[0][5:]\n"
            "    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
            "    server.bind(path); server.listen(1)\n"
            "    connection, _ = server.accept()\n"
            "    stream = connection.makefile('rwb', buffering=0)\n"
            "    stream.write(b'{\"QMP\": {}}\\n')\n"
            "    for line in stream:\n"
            "        request = json.loads(line)\n"
            "        stream.write((json.dumps({'return': {}, 'id': request['id']}) + '\\n').encode())\n"
            "        if request['execute'] == 'cont': break\n"
            "    print('mini-os: boot FAIL: dtb bad magic', flush=True)\n"
            "    time.sleep(30)\n"
            "resource_line = " + repr(qemu.discovery_line()) + "\n"
            "if '256M' in sys.argv: resource_line = " + repr(qemu.discovery_line(256)) + "\n"
        ) + console
        processes = []
        real_popen = subprocess.Popen

        def launch(*args, **kwargs):
            process = real_popen(*args, **kwargs)
            processes.append(process)
            return process

        with patch.object(qemu.subprocess, "Popen", side_effect=launch):
            result = self.run_fake(body)
        self.assertTrue(result.success, result.reason)
        self.assertEqual(len(processes), 3)
        self.assertIn(qemu.discovery_line(256), result.stdout)
        self.assertIn(FAILURE, result.stdout)
        for process in processes:
            with self.assertRaises(ChildProcessError):
                os.waitpid(process.pid, os.WNOHANG)
            with self.assertRaises(ProcessLookupError):
                os.kill(process.pid, 0)

    def test_boot_requires_resource_confirmation_before_success(self):
        for output in (b"mini-os: boot OK\n", qemu.discovery_line(256) + b"mini-os: boot OK\n",
                       b"mini-os: boot OK\n" + qemu.discovery_line()):
            result = self.run_fake("os.write(1, " + repr(output) + ")\ntime.sleep(30)\n",
                                   timeout=0.5, runner=qemu.boot_test)
            self.assertFalse(result.success)

    def test_qmp_disconnect_deadline_and_errors_reap_process(self):
        import tempfile
        from pathlib import Path
        for response in (b"", b'{"QMP": {}}\n', b'{"error": {"desc": "rejected"}}\n'):
            with tempfile.TemporaryDirectory(dir="/tmp") as directory:
                path = Path(directory) / "qmp.sock"
                body = (
                    "import socket\n"
                    "server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
                    "server.bind(" + repr(str(path)) + "); server.listen(1)\n"
                    "connection, _ = server.accept()\n"
                    "os.write(2, b'injection diagnostic\\n')\n"
                    "connection.sendall(" + repr(response) + ")\n"
                    + ("connection.close()\n" if not response else "") + "time.sleep(30)\n"
                )
                result = self.run_fake(body, timeout=0.5,
                    runner=lambda command, timeout: qemu.serial_test(
                        command, timeout, expected_failure=FAILURE, qmp_path=path))
                self.assertFalse(result.success)
                self.assertIn("QMP", result.reason)
                self.assertIn(b"injection diagnostic", result.stderr)

    def test_incorrect_discovery_values(self):
        body = "os.write(1, " + repr(qemu.READY.replace(b"clock=24000000", b"clock=48000000")) + ")\ntime.sleep(30)\n"
        result = self.run_fake(body, runner=qemu.uart_test)
        self.assertFalse(result.success)
        self.assertIn("Incorrect UART response", result.reason)

    def test_expected_failure_is_exact_and_cannot_mask_boot_success(self):
        for output in (b"mini-os: boot FAIL: dtb other\n", FAILURE + b"\nmini-os: boot OK\n"):
            result = self.run_fake(
                "os.write(1, " + repr(output) + ")\ntime.sleep(30)\n",
                runner=lambda command, timeout: qemu.serial_test(command, timeout, expected_failure=FAILURE),
            )
            self.assertFalse(result.success)
            self.assertIn("Incorrect expected", result.reason)

    def test_overall_deadline_includes_previous_scenarios(self):
        body = (
            "if '256M' in sys.argv: time.sleep(30)\n"
            "os.write(1, " + repr(qemu.READY) + ")\ntime.sleep(30)\n"
        )
        started = time.monotonic()
        result = self.run_fake(body, timeout=0.8)
        self.assertFalse(result.success)
        self.assertIn("256 MiB", result.reason)
        self.assertLess(time.monotonic() - started, 1.5)

    def test_expected_failure_requires_a_complete_line(self):
        result = self.run_fake(
            "os.write(1, " + repr(FAILURE) + ")\ntime.sleep(30)\n", timeout=0.2,
            runner=lambda command, timeout: qemu.serial_test(command, timeout, expected_failure=FAILURE),
        )
        self.assertFalse(result.success)
        self.assertIn("Timed out", result.reason)
