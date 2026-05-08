"""EL0 isolation, exact lower-EL context, process failures and child cleanup."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_task_runner

qemu=test_boot_runner.qemu
NAMES=('demo','brk','undef','unmapped','readonly','kernel','spin','stack')
SYMBOLS={'mini_os_user_code_begin':0x40210000,'__image_start':0x40200000}

for i,name in enumerate(NAMES):SYMBOLS['mini_os_user_'+name+'_site']=0x40210100+i*32

REPORTS=[]

for i,name in enumerate(NAMES):
    site=0x1000100+i*32
    outcome='exit' if i==0 else 'timeout' if i==6 else 'fault'
    ec=0x15 if i==0 else 0x3c if i==1 else 0x24 if 3<=i<=5 or i==7 else 0
    iss=0x123 if i==1 else 7 if i==3 else 0x4f if i==4 else 0xf if i==5 else 0x47 if i==7 else 0
    far=0x1003000 if i in (3,7) else 0x1000000 if i==4 else 0x40200000 if i==5 else 0
    spsr=0xa0000340 if i==6 else 0x340

    REPORTS.append((f'user: case={name} result={outcome} status={42 if i==0 else 0} el=0 vector={9 if i==6 else 8} '
        f'ec=0x{ec:02x} iss=0x{iss:07x} ELR=0x{site+4 if i==0 else site:016x} FAR(raw)=0x{far:016x} '
        f'SPSR=0x{spsr:016x} SP_EL0=0x{0x1003000 if i==7 else 0x1005000:016x} ticks={21 if i==6 else 0} '
        f'syscalls={7 if i==0 else 0} writes={1 if i==0 else 0}\r\n').encode())

PREFIX=b'user: hello from EL0\r\n'
RETURN=b'user: returned el=1 daif=0x0000000000000340 root-restored=yes pages-restored=yes\r\n'
DEMO=PREFIX+REPORTS[0]+RETURN+b'user: test OK cases=1\r\n'
SUITE=PREFIX+b''.join(REPORTS)+RETURN+b'user: test OK cases=8\r\n'
CONSOLE='user_demo='+repr(DEMO)+'\nuser_suite='+repr(SUITE)+'\n'+test_task_runner.CONSOLE.replace("    if command == b'tasks' or command == b'tasks test':",r'''
    if command == b'user':response+=user_demo
    elif command == b'user test':response+=user_suite
    elif command.startswith(b'user'):response+=b'usage: user [test]\r\n'
    elif command == b'mem':
        pages=memory*256;reserved=1024;allocated=131;free=pages-reserved-allocated
        response+=f'mem: base=0x0000000040000000 size=0x{memory*1024*1024:016x} pages={pages} reserved={reserved} allocated={allocated} free={free} metadata=0x0000000040320000\r\n'.encode()
    elif command == b'tasks' or command == b'tasks test':''')


class UserRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=6):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.user_test,symbols=SYMBOLS),
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_reports_models_sizes_repeated_return_and_reaping(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),4)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_incorrect_origin_syndrome_site_stack_return_and_accounting(self):
        for old,new in (('status=42','status=99'),('el=0','el=1'),('vector=8','vector=4'),
                        ('ec=0x15','ec=0x00'),('iss=0x0000123','iss=0x0000124'),
                        ('ELR=0x0000000001000104','ELR=0x0000000001000100'),
                        ('FAR(raw)=0x0000000001003000','FAR(raw)=0x0000000001003001'),
                        ('SPSR=0x0000000000000340','SPSR=0x0000000000000345'),
                        ('SP_EL0=0x0000000001005000','SP_EL0=0x0000000001005001'),
                        ('ticks=21','ticks=1'),('syscalls=7','syscalls=6'),('writes=1','writes=2'),
                        ('root-restored=yes','root-restored=no'),('pages-restored=yes','pages-restored=no'),
                        ('test OK cases=8','test OK cases=6')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success,old)

        leaked=CONSOLE.replace("elif command == b'user test':response+=user_suite","elif command == b'user test':batch+=1;response+=user_suite")
        result=self.run_fake(leaked.replace('allocated=131','allocated=131+batch'))

        self.assertFalse(result.success);self.assertIn('leaked',result.reason)

    def test_partial_exit_closed_input_and_deadline(self):
        result=self.run_fake(CONSOLE.replace('response+=user_suite',"os.write(2,b'user failed');sys.exit(7);response+=user_suite"))

        self.assertFalse(result.success);self.assertIn(b'user failed',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake(CONSOLE.replace('response+=user_suite',"os.write(1,user_suite[:80]);time.sleep(30);response+=user_suite"),timeout=.3)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)

    def test_stderr_draining_forced_shutdown_and_kernel_exception_rejection(self):
        result=self.run_fake(CONSOLE.replace('response+=user_suite',"os.write(2,b'diagnostic'*10000);response+=user_suite"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3)

        self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace('response+=user_suite',"os.write(1,b'mini-os: exception');response+=user_suite"))

        self.assertFalse(result.success)
