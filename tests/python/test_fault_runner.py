"""Fatal exception protocols and cleanup with independent disposable processes."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner

qemu = test_boot_runner.qemu
SYMBOLS = {"mini_os_fault_brk_site": 0x40200100, "mini_os_fault_undef_site": 0x40200200,
           "__stack_bottom": 0x40210000, "__stack_top": 0x40220000}
CONSOLE = 'os.write(1, ' + repr(qemu.READY) + ')\n' + r'''
line = bytearray()
while True:
    data = os.read(0, 1)
    if not data: break
    if data != b'\n':
        line += data
        os.write(1, data)
        continue
    kind = bytes(line).split()[1]
    breakpoint = kind == b'brk'
    reason = 'brk' if breakpoint else 'unknown'
    esr = 0xf2000123 if breakpoint else 0x02000000
    elr = 0x40200100 if breakpoint else 0x40200200
    report = f'\r\nmini-os: exception vector=current-spx-sync reason={reason}\r\n'
    report += f'esr=0x{esr:016x} ec=0x{esr >> 26:02x} il=1 iss=0x{esr & 0x1ffffff:07x}\r\n'
    report += f'elr=0x{elr:016x} spsr=0x0000000060000345\r\n'
    report += 'sp=0x000000004021fff0 far(raw)=0xffffffffffffffff\r\n'
    for index in range(31): report += f'x{index:02d}=0x{0x100 + index:016x}\r\n'
    report += 'mini-os: halted\r\n'
    for byte in report.encode(): os.write(1, bytes([byte]))
    time.sleep(30)
'''


class FaultRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=4, runner=None):
        if runner is None:
            runner = functools.partial(qemu.fault_test, symbols=SYMBOLS)

        return test_boot_runner.BootRunnerTests.run_fake(
            self, body, timeout, runner=runner, command_args=("-cpu", "cortex-a53"))

    def test_fragmented_reports_both_models_and_all_children_reaped(self):
        processes = []
        real_popen = subprocess.Popen

        def launch(*args, **kwargs):
            process = real_popen(*args, **kwargs)

            processes.append(process)

            return process

        with patch.object(qemu.subprocess, "Popen", side_effect=launch):
            result = self.run_fake(CONSOLE)

        self.assertTrue(result.success, result.reason)
        self.assertEqual(len(processes), 4)
        self.assertEqual(result.stdout.count(b"mini-os: halted"), 4)

        for process in processes:
            with self.assertRaises(ChildProcessError): os.waitpid(process.pid, os.WNOHANG)

            with self.assertRaises(ProcessLookupError): os.kill(process.pid, 0)

    def test_incorrect_context_rejected(self):
        fields = (("current-spx-sync", "lower-a64-sync"), ("reason={reason}", "reason=wrong"),
                  ("ec=0x{esr >> 26:02x}", "ec=0x00"), ("elr = 0x40200100", "elr = 0x40200104"),
                  ("60000345", "60000344"), ("60000345", "60000005"),
                  ("4021fff0", "4021fff1"), ("4021fff0", "40200000"),
                  ("0x100 + index", "0x200 + index"))

        for old, new in fields:
            with self.subTest(field=old):
                result = self.run_fake(CONSOLE.replace(old, new))

                self.assertFalse(result.success)
                self.assertIn("Incorrect exception report", result.reason)

    def test_incomplete_report_times_out(self):
        body = CONSOLE.replace("for byte in report.encode():", "for byte in report[:100].encode():")
        result = self.run_fake(body, timeout=1)

        self.assertFalse(result.success)
        self.assertIn("Timed out", result.reason)
        self.assertIn(b"mini-os: exception", result.stdout)

    def test_missing_report_times_out(self):
        result = self.run_fake("time.sleep(30)", timeout=1)

        self.assertFalse(result.success)
        self.assertIn("Timed out", result.reason)

    def test_premature_exit_preserves_diagnostics(self):
        result = self.run_fake(CONSOLE.replace("time.sleep(30)", "os.write(2, b'handler failed'); sys.exit(7)"))

        self.assertFalse(result.success)
        self.assertIn("exited prematurely", result.reason)
        self.assertEqual(result.stderr, b"handler failed")

    def test_closed_stdin(self):
        body = "os.close(0)\n" + 'os.write(1, ' + repr(qemu.READY) + ')\ntime.sleep(30)'
        result = self.run_fake(body)

        self.assertFalse(result.success)
        self.assertIn("closed serial stdin", result.reason)

    def test_stderr_continuously_drained(self):
        body = CONSOLE.replace("for byte in report.encode():", "os.write(2, b'diagnostic' * 20000)\n    for byte in report.encode():")
        result = self.run_fake(body)

        self.assertTrue(result.success, result.reason)
        self.assertEqual(result.stderr, b"diagnostic" * 80000)

    def test_prompt_after_halt_is_failure(self):
        result = self.run_fake(CONSOLE.replace("time.sleep(30)", "time.sleep(0.02); os.write(1, b'mini-os> '); time.sleep(30)"))

        self.assertFalse(result.success)
        self.assertIn("trailing UART output", result.reason)

    def test_overall_deadline_and_forced_cleanup(self):
        body = "if 'cortex-a57' in sys.argv: signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(30)\n" + CONSOLE
        result = self.run_fake(body, timeout=1)

        self.assertFalse(result.success)
        self.assertIn("cortex-a57", result.reason)
        self.assertIn("Timed out", result.reason)

    def test_ordinary_protocols_reject_unexpected_exceptions(self):
        body = 'os.write(1, ' + repr(qemu.READY + b'mini-os: exception vector=current-spx-sync\r\n') + ')\ntime.sleep(30)'

        for runner in (qemu.boot_test, qemu.uart_test, qemu.monitor_test):
            with self.subTest(runner=runner.__name__):
                # monitor_test also needs the existing CPU-count command option.
                result = test_boot_runner.BootRunnerTests.run_fake(
                    self, body, runner=runner, command_args=("-cpu", "cortex-a53", "-smp", "1"))

                self.assertFalse(result.success)
                self.assertIn("unexpected exception", result.reason)

    def test_missing_or_invalid_image_prevents_launch(self):
        with patch.object(qemu.subprocess, "Popen") as launch:
            with self.assertRaises(OSError):
                qemu.fault_test(["fake-qemu", "-kernel", "/does-not-exist/kernel.elf"])

            launch.assert_not_called()
