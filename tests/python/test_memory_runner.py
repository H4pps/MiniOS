"""Allocation accounting, recovery, failures and subprocess cleanup."""

import unittest
import test_boot_runner
import test_irq_runner

qemu = test_boot_runner.qemu
CONSOLE = test_irq_runner.CONSOLE.replace('os.write(1, '+repr(qemu.READY)+')', "memory = 256 if '256M' in sys.argv else 128\nos.write(1, qemu_ready)")
# Each fake process determines its own resource confirmation from QEMU arguments.
CONSOLE = ('memory = 256 if \'256M\' in sys.argv else 128\nqemu_ready = '+repr(qemu.READY)+"\nif memory == 256: qemu_ready = "+repr(qemu.discovery_line(256)+qemu.READY[len(qemu.discovery_line()):])+"\n"+CONSOLE).replace("if command == b'irq':", r'''
    if command == b'mem':
        response += f'mem: base=0x0000000040000000 size=0x{memory*1024*1024:016x} pages={memory*256} reserved=600 allocated=0 free={memory*256-600} metadata=0x0000000040220000\r\n'.encode()
    elif command == b'mem test': response += b'mem: test OK\r\n'
    elif command.startswith(b'mem'): response += b'usage: mem [test|reclaim]\r\n'
    elif command == b'irq':''')


class MemoryRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=4):
        return test_boot_runner.BootRunnerTests.run_fake(self, body, timeout, runner=qemu.memory_test,
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_reports_both_ram_sizes_and_cleanup(self):
        result=self.run_fake(CONSOLE); self.assertTrue(result.success,result.reason)

    def test_bad_accounting_metadata_and_self_test(self):
        for old,new in (('reserved=600','reserved=1'),('0040220000','0040000000'),('test OK','test FAIL')):
            result=self.run_fake(CONSOLE.replace(old,new)); self.assertFalse(result.success)

    def test_early_exit_closed_stdin_timeout_and_cleanup(self):
        result=self.run_fake(CONSOLE.replace("response += b'mem: test OK", "os.write(2,b'memory error'); sys.exit(7); response += b'mem: test OK"))

        self.assertFalse(result.success); self.assertIn(b'memory error',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(30)',timeout=1)

        self.assertFalse(result.success); self.assertIn('Timed out',result.reason)

    def test_stderr_draining_and_partial_output(self):
        result=self.run_fake(CONSOLE.replace("response += b'mini-os> '","os.write(2,b'diagnostic'*10000); response += b'mini-os> '"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake(CONSOLE.replace("response += b'mini-os> '","time.sleep(30)"),timeout=1)

        self.assertFalse(result.success)
