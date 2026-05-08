"""Emergency reporting with a deliberately unmapped entry SP."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_fault_runner

qemu=test_boot_runner.qemu
SYMBOLS={**test_fault_runner.SYMBOLS,'__stack_guard':0x4020f000,'mini_os_fault_stack_site':0x40200300}
CONSOLE=test_fault_runner.CONSOLE.replace("reason = 'brk' if breakpoint else 'unknown'","reason = 'data-abort'").replace('esr = 0xf2000123 if breakpoint else 0x02000000','esr = 0x96000047').replace('elr = 0x40200100 if breakpoint else 0x40200200','elr = 0x40200300').replace('il=1 iss=0x{esr & 0x1ffffff:07x}', 'il=1 iss=0x{esr & 0x1ffffff:07x} abort=translation dfsc=0x07 write=1 far-valid=1').replace('sp=0x000000004021fff0 far(raw)=0xffffffffffffffff','sp=0x000000004020f800 far(raw)=0x000000004020f800').replace('0x100 + index:016x','(0x4020f800 if index==16 else 0x100+index):016x')


class StackFaultRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=4):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.fault_test,symbols=SYMBOLS,kinds=('stack',)),command_args=('-cpu','cortex-a53'))

    def test_fragmented_complete_report_and_cleanup(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),2)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_wrong_original_stack_far_site_syndrome_and_registers(self):
        for old,new in (('sp=0x000000004020f800','sp=0x000000004021fff0'),('far(raw)=0x000000004020f800','far(raw)=0x000000004020f808'),('elr = 0x40200300','elr = 0x40200304'),('dfsc=0x07','dfsc=0x06'),('0x100+index','0x200+index')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success);self.assertIn('Incorrect exception report',result.reason)

    def test_partial_report_exit_closed_input_and_stderr(self):
        result=self.run_fake(CONSOLE.replace('report.encode()','report[:100].encode()'),timeout=.5)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)
        result=self.run_fake(CONSOLE.replace('time.sleep(30)',"os.write(2,b'stack failure');sys.exit(7)"))

        self.assertFalse(result.success);self.assertIn(b'stack failure',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake(CONSOLE.replace('report = f',"os.write(2,b'diagnostic'*10000);report = f"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
