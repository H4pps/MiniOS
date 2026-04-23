"""Bidirectional protocol tests with real disposable subprocesses, no QEMU."""

import unittest
import test_boot_runner

qemu = test_boot_runner.qemu


# This fake console implements editing independently of the runner's cases.
CONSOLE = 'os.write(1, ' + repr(qemu.discovery_line()) + ')\n' + r'''
os.write(1, b"mini-os: boot OK\r\nmini-os: uart ready\r\nmini-os> ")
line = bytearray()
reject = False
cr = False
while True:
    data = os.read(0, 1)
    if not data:
        break
    byte = data[0]
    if cr and byte == 10:
        cr = False
        continue
    cr = False
    response = b""
    if byte in (10, 13):
        cr = byte == 13
        response = b"\r\n"
        if reject:
            response += b"mini-os: line too long\r\n"
        elif line:
            response += b"echo: " + bytes(line) + b"\r\n"
        response += b"mini-os> "
        line.clear()
        reject = False
    elif reject:
        continue
    elif byte in (8, 127):
        if line:
            line.pop()
            response = b"\x08 \x08"
    elif 32 <= byte <= 126:
        if len(line) == 127:
            reject = True
            response = b"\a"
        else:
            line.append(byte)
            response = bytes([byte])
    for part in response:
        os.write(1, bytes([part]))
'''


class UartRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=8):
        # Reuse the cleanup assertions without inheriting boot-only test behavior.
        return test_boot_runner.BootRunnerTests.run_fake(self, body, timeout, runner=qemu.uart_test)

    def test_bidirectional_fragmented_exchanges(self):
        result = self.run_fake(CONSOLE.replace(
            'os.write(1, b"mini-os: boot OK',
            'os.write(1, b"mini-os: "); time.sleep(0.02); os.write(1, b"boot OK',
        ))
        self.assertTrue(result.success, result.reason)
        self.assertIn(b"echo: ad\r\n", result.stdout)
        self.assertIn(b"mini-os: line too long", result.stdout)
        self.assertIn(b"echo: recovered\r\n", result.stdout)

    def test_boot_failure_during_uart_exchange(self):
        result = self.run_fake(
            "os.write(1, " + repr(qemu.READY) + ")\n"
            "os.read(0, 1)\nprint('mini-os: boot FAIL: receive', flush=True)\ntime.sleep(30)\n"
        )
        self.assertFalse(result.success)
        self.assertIn("Kernel reported boot failure", result.reason)

    def test_incorrect_response(self):
        result = self.run_fake(
            "os.write(1, " + repr(qemu.READY) + ")\n"
            "os.read(0, 1)\nos.write(1, b'wrong')\ntime.sleep(30)\n"
        )
        self.assertFalse(result.success)
        self.assertIn("Incorrect UART response", result.reason)

    def test_closed_stdin(self):
        result = self.run_fake(
            "os.close(0)\nos.write(1, " + repr(qemu.READY) + ")\ntime.sleep(30)\n"
        )
        self.assertFalse(result.success)
        self.assertIn("closed serial stdin", result.reason)

    def test_uart_early_exit(self):
        result = self.run_fake("os.write(1, " + repr(qemu.READY) + ")\nsys.exit(7)\n")
        self.assertFalse(result.success)
        self.assertIn("exited prematurely", result.reason)

    def test_uart_timeout_and_kill(self):
        result = self.run_fake(
            "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
            "os.write(1, " + repr(qemu.READY) + ")\nos.read(0, 1)\ntime.sleep(30)\n",
            timeout=0.3,
        )
        self.assertFalse(result.success)
        self.assertIn("Timed out", result.reason)

    def test_partial_readiness(self):
        result = self.run_fake("os.write(1, " + repr(qemu.READY[:-1]) + ")\ntime.sleep(30)\n", timeout=0.3)
        self.assertFalse(result.success)
        self.assertIn("readiness", result.reason)

    def test_stderr_is_drained_during_exchanges(self):
        result = self.run_fake("os.write(2, b'diagnostic' * 20000)\n" + CONSOLE)
        self.assertTrue(result.success, result.reason)
        self.assertEqual(result.stderr, b"diagnostic" * 20000)

    def test_stderr_readiness_cannot_start_protocol(self):
        result = self.run_fake("os.write(2, " + repr(qemu.READY) + ")\ntime.sleep(30)\n", timeout=0.3)
        self.assertFalse(result.success)
        self.assertEqual(result.stdout, b"")

    def test_preloaded_responses_cannot_fake_input_reception(self):
        result = self.run_fake(
            "os.write(1, " + repr(b"".join(expected for _, _, expected in qemu.uart_exchanges())) + ")\n"
            "time.sleep(30)\n", timeout=0.3
        )
        self.assertFalse(result.success)

    def test_duplicate_crlf_submission_is_rejected(self):
        result = self.run_fake(CONSOLE.replace("cr = byte == 13", "cr = False"))
        self.assertFalse(result.success)
        self.assertIn("Incorrect UART response", result.reason)
