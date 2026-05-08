"""Verified online/parked state, SGI progression, private stacks and cleanup."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_cpu_discovery_runner

qemu=test_boot_runner.qemu
SYMBOLS={'__stack_top':0x40232000,'__exception_stacks_start':0x40232000,'__secondary_stacks_start':0x4025a000}
CONSOLE='heartbeat=0\n'+test_cpu_discovery_runner.CONSOLE.replace(
    'os.write(1, '+repr(qemu.READY)+')',
    'memory=int(sys.argv[sys.argv.index("-m")+1][:-1]);os.write(1,'+repr(qemu.READY)+'.replace(b"size=0x0000000008000000",f"size=0x{memory*1024*1024:016x}".encode()))'
).replace("    if command == b'topology':",r'''
    if command == b'smp':
        response += f'smp: discovered={count} online={count} boot=0 psci=1.1\r\n'.encode()
        for i in range(count):
            stack=0x40232000 if i==0 else 0x4025a000+(i+1)*68*1024
            emergency=0x40232000+(i+1)*20*1024
            midr='410fd034' if model=='cortex-a53' else '410fd070'
            role='boot' if i==0 else 'parked';irq='on' if i==0 else 'off';beats=0 if i==0 else heartbeat
            response += f'smp[{i}]: affinity=0x{i:016x} dt-status=enabled online=yes role={role} heartbeat={beats} el=1 midr=0x{midr} mmu=on caches=off irq={irq} stack-top=0x{stack:016x} exception-stack=0x{emergency:016x}\r\n'.encode()
    elif command == b'smp test':heartbeat+=1;response+=b'smp: test OK\r\n'
    elif command.startswith(b'smp'):response+=b'usage: smp [test]\r\n'
    elif command == b'heap test':response+=b'heap: test OK\r\n'
    elif command == b'topology':''')


class SmpRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=6):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.smp_test,symbols=SYMBOLS),
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_model_capacity_memory_heartbeats_and_reaping(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),6)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_incorrect_online_identity_state_stacks_and_heartbeat(self):
        for old,new in (('online={count}','online=9'),('online=yes','online=no'),('el=1','el=0'),
                        ('410fd034','410fd070'),('mmu=on','mmu=off'),('caches=off','caches=on'),
                        ('0x4025a000+(i+1)','0x4025a000+i'),('0x40232000+(i+1)','0x40232000+i'),
                        ('beats=0 if i==0 else heartbeat','beats=0'),("irq='on' if i==0 else 'off'","irq='on'"),
                        ('psci=1.1','psci=0.1'),('test OK','test FAIL')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success,old)

    def test_partial_exit_closed_stdin_stderr_and_timeout(self):
        result=self.run_fake(CONSOLE.replace('heartbeat+=1',"os.write(2,b'secondary failed');sys.exit(7);heartbeat+=1"))

        self.assertFalse(result.success);self.assertIn(b'secondary failed',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake(CONSOLE.replace("response += b'mini-os> '",'time.sleep(30)'),timeout=.3)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)

    def test_stderr_draining_forced_shutdown_and_fatal_marker(self):
        result=self.run_fake(CONSOLE.replace('heartbeat+=1',"os.write(2,b'diagnostic'*10000);heartbeat+=1"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3)

        self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace('heartbeat+=1',"os.write(1,b'mini-os: exception');heartbeat+=1"))

        self.assertFalse(result.success)
