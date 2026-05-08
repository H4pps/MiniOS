"""Exact recoveries followed by an unarmed fatal fault and emergency reports."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_fault_runner
import test_stack_fault_runner

qemu=test_boot_runner.qemu
NORMAL=test_fault_runner.CONSOLE.replace('line = bytearray()', 'count=0\nline = bytearray()').replace("    kind = bytes(line).split()[1]",r'''
    command=bytes(line).strip();response=b'\r\n'
    if command==b'help':response+='''+repr(qemu.HELP)+r'''
    elif command in (b'recover brk',b'recover undef'):
        count+=1;kind=command.split()[1]
        response+=b'recover: '+kind+f' OK count={count}\r\n'.encode()
    elif command.startswith(b'recover'):response+=b'usage: recover brk|undef\r\n'
    elif command==b'irq test':response+=b'irq: test OK\r\n'
    elif command==b'heap test':response+=b'heap: test OK\r\n'
    elif command==b'echo resumed':response+=b'echo: resumed\r\n'
    else:response=b''
    if response:
        response+=b'mini-os> '
        for byte in response:os.write(1,bytes([byte]))
        line.clear();continue
    kind = bytes(line).split()[1]''')
CONSOLE=NORMAL.replace("    kind = bytes(line).split()[1]", "    kind = bytes(line).split()[1]\n" +
    "    if kind==b'stack':\n" + ''.join('        '+line+'\n' for line in test_stack_fault_runner.CONSOLE.splitlines()[test_stack_fault_runner.CONSOLE.splitlines().index("    breakpoint = kind == b'brk'"):]))


class RecoveryRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=5):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.recovery_test,symbols=test_stack_fault_runner.SYMBOLS),
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_reports_controlled_return_and_cleanup(self):
        # fault_test retains -smp, so distinguish its stack command in the console.
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),5)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_bad_return_accounting_and_premature_exit(self):
        result=self.run_fake(CONSOLE.replace(' OK count={count}',' FAIL count={count}'));self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace('count+=1','count+=2'));self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace('count+=1',"os.write(2,b'recovery failed');sys.exit(7)"));self.assertFalse(result.success);self.assertIn(b'recovery failed',result.stderr)

    def test_timeout_closed_input_and_stderr_draining(self):
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3);self.assertFalse(result.success)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)');self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake(CONSOLE.replace('count+=1',"os.write(2,b'diagnostic'*10000);count+=1"));self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
