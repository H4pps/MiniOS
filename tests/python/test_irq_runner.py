"""Returning IRQ protocols, exact counters, and disposable process cleanup."""
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
qemu = test_boot_runner.qemu
CONSOLE = 'os.write(1, ' + repr(qemu.READY) + ')\n' + r'''
sgi = 0
line = bytearray()
while True:
    byte = os.read(0, 1)
    if not byte: break
    if byte != b'\n': line += byte; os.write(1, byte); continue
    command = bytes(line).strip()
    response = b'\r\n'
    if command == b'irq':
        response += f'irq: distributor=0x0000000008000000 redistributor=0x00000000080a0000 limit=288 delivered={sgi} self-sgi={sgi}\r\n'.encode()
    elif command == b'irq test': sgi += 1; response += b'irq: test OK\r\n'
    elif command.startswith(b'irq'): response += b'usage: irq [test]\r\n'
    elif command.startswith(b'echo '): response += b'echo: ' + command[5:] + b'\r\n'
    response += b'mini-os> '
    for value in response: os.write(1, bytes([value]))
    line.clear()
'''
class IrqRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=4):
        return test_boot_runner.BootRunnerTests.run_fake(self, body, timeout, runner=qemu.irq_test,
            command_args=('-cpu', 'cortex-a53', '-smp', '1', '-m', '128M'))
    def test_fragmented_counts_integrity_and_cleanup(self):
        processes = []; popen = subprocess.Popen
        def launch(*args, **kwargs):
            process = popen(*args, **kwargs); processes.append(process); return process
        with patch.object(qemu.subprocess, 'Popen', side_effect=launch): result = self.run_fake(CONSOLE)
        self.assertTrue(result.success, result.reason); self.assertEqual(len(processes), 3)
        for process in processes:
            with self.assertRaises(ChildProcessError): os.waitpid(process.pid, os.WNOHANG)
    def test_incorrect_resources_counts_and_integrity(self):
        for old, new in (('0000000008000000', '0000000009000000'), ('self-sgi={sgi}', 'self-sgi=7'), ('test OK', 'test FAIL')):
            result = self.run_fake(CONSOLE.replace(old, new)); self.assertFalse(result.success)
    def test_exit_closed_input_and_partial_report(self):
        result = self.run_fake(CONSOLE.replace('sgi += 1;', "os.write(2, b'irq failed'); sys.exit(7);"))
        self.assertFalse(result.success); self.assertIn(b'irq failed', result.stderr)
        result = self.run_fake('os.close(0)\n' + 'os.write(1, ' + repr(qemu.READY) + ')\ntime.sleep(30)')
        self.assertFalse(result.success); self.assertIn('closed serial stdin', result.reason)
        result = self.run_fake(CONSOLE.replace("response += b'mini-os> '", "time.sleep(30)"), timeout=1)
        self.assertFalse(result.success); self.assertIn('Timed out', result.reason)
    def test_stderr_and_overall_deadline_forced_shutdown(self):
        result = self.run_fake(CONSOLE.replace("response += b'mini-os> '", "os.write(2, b'diagnostic' * 10000); response += b'mini-os> '"))
        self.assertTrue(result.success, result.reason); self.assertGreater(len(result.stderr), 100000)
        result = self.run_fake("if '4' in sys.argv: signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(30)\n" + CONSOLE, timeout=1)
        self.assertFalse(result.success); self.assertIn('Timed out', result.reason)
