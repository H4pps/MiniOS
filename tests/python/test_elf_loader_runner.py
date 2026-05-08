"""Compiled ELF protocol, exact context, allocation accounting and subprocess cleanup."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_user_runner

qemu=test_boot_runner.qemu
IMAGE={'entry':0x1000000,'exit_site':0x1000008,
       'segments':[(0x1000000,4096,544,544,5),(0x1001000,8192,14,14,4),(0x1002000,12288,8,264,6)]}
LAYOUT=(b'elf: entry=0x0000000001000000 segments=3 pages=3\r\n'
        b'elf[0]: va=0x0000000001000000 file=544 memory=544 permissions=r-x\r\n'
        b'elf[1]: va=0x0000000001001000 file=14 memory=14 permissions=r--\r\n'
        b'elf[2]: va=0x0000000001002000 file=8 memory=264 permissions=rw-\r\n')
RUN=(b'elf: user OK\r\nuser: case=elf result=exit status=42 el=0 vector=8 ec=0x15 iss=0x0000000 '
     b'ELR=0x000000000100000c FAR(raw)=0x0000000000000000 SPSR=0x0000000060000340 '
     b'SP_EL0=0x0000000001005000 ticks=0 syscalls=2 writes=1\r\n')
RETURN=b'elf: returned el=1 daif=0x0000000000000340 root-restored=yes pages-restored=yes\r\n'
DEMO=LAYOUT+RUN+RETURN+b'elf: test OK runs=1\r\n'
SUITE=LAYOUT+RUN+RUN+b'elf: rejected bad-magic\r\n'+RETURN+b'elf: test OK runs=2\r\n'
CONSOLE='elf_demo='+repr(DEMO)+'\nelf_suite='+repr(SUITE)+'\n'+test_user_runner.CONSOLE.replace("    if command == b'user':",r'''
    if command == b'elf':response+=elf_demo
    elif command == b'elf test':response+=elf_suite
    elif command.startswith(b'elf'):response+=b'usage: elf [test]\r\n'
    elif command == b'user':''')


class ElfLoaderRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=6):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.elf_test,image=IMAGE),
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_image_reports_repeated_runs_and_all_children_reaped(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),4)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_wrong_layout_context_bss_rejection_and_return(self):
        for old,new in (('segments=3','segments=2'),('pages=3','pages=4'),
                        ('file=8 memory=264','file=8 memory=8'),('permissions=r-x','permissions=rw-'),
                        ('entry=0x0000000001000000','entry=0x0000000001000004'),
                        ('elf: user OK','elf: user FAIL'),('status=42','status=72'),
                        ('ELR=0x000000000100000c','ELR=0x0000000001000008'),
                        ('SPSR=0x0000000060000340','SPSR=0x00000000600003c0'),
                        ('SP_EL0=0x0000000001005000','SP_EL0=0x0000000001004ff0'),
                        ('syscalls=2','syscalls=1'),('writes=1','writes=0'),
                        ('rejected bad-magic','rejected none'),('root-restored=yes','root-restored=no'),
                        ('pages-restored=yes','pages-restored=no'),('test OK runs=2','test OK runs=1')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success,old)

        leaked=CONSOLE.replace("elif command == b'elf test':response+=elf_suite","elif command == b'elf test':batch+=1;response+=elf_suite")
        result=self.run_fake(leaked.replace('allocated=131','allocated=131+batch'))

        self.assertFalse(result.success);self.assertIn('leaked',result.reason)

    def test_partial_early_exit_closed_input_timeout_and_reaping(self):
        result=self.run_fake(CONSOLE.replace('response+=elf_suite',"os.write(2,b'ELF failed');sys.exit(7);response+=elf_suite"))

        self.assertFalse(result.success);self.assertIn(b'ELF failed',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake(CONSOLE.replace('response+=elf_suite',"os.write(1,elf_suite[:100]);time.sleep(30);response+=elf_suite"),timeout=.3)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)

    def test_stderr_drain_forced_shutdown_and_exception_rejection(self):
        result=self.run_fake(CONSOLE.replace('response+=elf_suite',"os.write(2,b'diagnostic'*10000);response+=elf_suite"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3)

        self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace('response+=elf_suite',"os.write(1,b'mini-os: exception');response+=elf_suite"))

        self.assertFalse(result.success)
