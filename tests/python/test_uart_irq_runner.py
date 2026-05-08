"""Receive IRQ accounting, bursts, editing and bounded process cleanup."""

import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_uart_runner

qemu=test_boot_runner.qemu
CONSOLE='received = 0\n' + test_uart_runner.CONSOLE.replace('    byte = data[0]', '    received += 1\n    byte = data[0]').replace('            if command == b"echo":', r'''
            if command == b"uart" and arguments.strip() == b"x":
                response += b"usage: uart\r\n"
            elif command == b"uart":
                response += f'uart: mode=irq interrupt=33 interrupts={received} received={received} errors=0 dropped=0 queued=0 sleeps={received+100}\r\n'.encode()
            elif command == b"irq": response += b"irq: test OK\r\n"
            elif command == b"heap": response += b"heap: test OK\r\n"
            elif command == b"echo":''')


class UartIrqRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=6):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,runner=qemu.uart_irq_test,
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_editing_bursts_counters_and_cleanup(self):
        processes=[]; popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs); processes.append(process); return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch): result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason); self.assertEqual(len(processes),3)

        for process in processes:
            with self.assertRaises(ChildProcessError): os.waitpid(process.pid,os.WNOHANG)

    def test_wrong_delivery_errors_and_queue_accounting(self):
        for old,new in (('mode=irq','mode=polling'),('interrupt=33','interrupt=34'),('interrupts={received}','interrupts=0'),('received={received}','received=5'),('dropped=0','dropped=256'),('errors=0','errors=1'),('queued=0','queued=2'),('sleeps={received+100}','sleeps=0')):
            result=self.run_fake(CONSOLE.replace(old,new)); self.assertFalse(result.success)

    def test_stderr_early_exit_closed_input_and_timeout(self):
        result=self.run_fake(CONSOLE.replace('received += 1',"os.write(2,b'UART failed'); sys.exit(7)"))

        self.assertFalse(result.success); self.assertIn(b'UART failed',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success); self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3)

        self.assertFalse(result.success); self.assertIn('Timed out',result.reason)

    def test_stderr_draining_and_partial_output(self):
        result=self.run_fake(CONSOLE.replace('received += 1',"os.write(2,b'diagnostic'*1000); received += 1"))

        self.assertTrue(result.success,result.reason); self.assertGreater(len(result.stderr),100000)
        result=self.run_fake('os.write(1,'+repr(qemu.READY[:-1])+')\ntime.sleep(30)',timeout=.3)

        self.assertFalse(result.success); self.assertIn('Timed out',result.reason)
