"""Periodic progress without serial output from IRQ handlers."""

import unittest
import test_boot_runner
import test_irq_runner

qemu = test_boot_runner.qemu
CONSOLE = 'timer_ticks = 0\n' + test_irq_runner.CONSOLE.replace("if command == b'irq':", r'''
    if command == b'timer':
        timer_ticks += 1
        response += f'timer: frequency=62500000 target-hz=100 interval=625000 counter={timer_ticks * 625000} ticks={timer_ticks} missed=0\r\n'.encode()
    elif command == b'timer x': response += b'usage: timer\r\n'
    elif command == b'irq':''')


class TimerRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=4):
        return test_boot_runner.BootRunnerTests.run_fake(self, body, timeout, runner=qemu.timer_test,
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_progress_and_cleanup(self):
        result = self.run_fake(CONSOLE); self.assertTrue(result.success, result.reason)

    def test_invalid_frequency_interval_and_frozen_ticks(self):
        for old, new in (('frequency=62500000', 'frequency=0'), ('interval=625000', 'interval=42'), ('ticks={timer_ticks}', 'ticks=0')):
            result = self.run_fake(CONSOLE.replace(old,new)); self.assertFalse(result.success)
            self.assertIn('timer', result.reason.lower())

    def test_early_exit_stderr_and_timeout(self):
        result = self.run_fake(CONSOLE.replace('timer_ticks += 1', "os.write(2, b'timer failure'); sys.exit(7)"))

        self.assertFalse(result.success); self.assertIn(b'timer failure', result.stderr)
        result = self.run_fake("signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(30)", timeout=1)

        self.assertFalse(result.success); self.assertIn('Timed out', result.reason)

    def test_stderr_draining_and_asynchronous_serial_rejection(self):
        result = self.run_fake(CONSOLE.replace('timer_ticks += 1', "os.write(2, b'diagnostic' * 10000); timer_ticks += 1"))

        self.assertTrue(result.success,result.reason); self.assertGreater(len(result.stderr),100000)
        result = self.run_fake(CONSOLE.replace("response += b'mini-os> '", "response += b'mini-os> noise'"))

        self.assertFalse(result.success)
