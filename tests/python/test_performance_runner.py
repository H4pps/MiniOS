"""Coherent snapshots, time conversion, bounded workload reports and cleanup."""
import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_irq_runner
qemu=test_boot_runner.qemu
SYMBOLS={'__exception_stacks_start':0x40230000}
CONSOLE="stamp=0\nrecovered=0\nmeasured=False\n"+test_irq_runner.CONSOLE.replace('os.write(1, '+repr(qemu.READY)+')',"memory=int(sys.argv[sys.argv.index('-m')+1][:-1]);os.write(1,"+repr(qemu.READY)+".replace(b'size=0x0000000008000000',f'size=0x{memory*1024*1024:016x}'.encode()))").replace("    if command == b'irq':",r'''
    if command==b'diag':
        stamp+=1
        response+=f'diag: el=1 mmu=on caches=off irq=on uptime-us={stamp*10000} timer-ticks={stamp} missed=1 recoveries={recovered} uart-dropped=0 pages-free=30000 heap-free=262112 exception-stack=0x0000000040235000\r\n'.encode()
    elif command==b'perf test':
        measured=True
        response+=b'perf: state=OK pages=8 bytes=4194304 counter-ticks=625000 microseconds=10000 timer-ticks=1\r\n'
    elif command==b'perf':
        response+=b'perf: state=OK pages=8 bytes=4194304 counter-ticks=625000 microseconds=10000 timer-ticks=1\r\n' if measured else b'perf: state=not-run\r\n'
    elif command.startswith(b'perf'):response+=b'usage: perf [test]\r\n'
    elif command.startswith(b'diag'):response+=b'usage: diag\r\n'
    elif command==b'recover brk':recovered+=1;response+=f'recover: brk OK count={recovered}\r\n'.encode()
    elif command==b'heap test':response+=b'heap: test OK\r\n'
    elif command == b'irq':''')
class PerformanceRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=6):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.performance_test,symbols=SYMBOLS),
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))
    def test_fragmented_snapshots_workloads_and_cleanup(self):
        processes=[];popen=subprocess.Popen
        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process
        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)
        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),6)
        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)
    def test_wrong_state_stack_counters_accounting_and_conversion(self):
        for old,new in (('el=1','el=0'),('mmu=on','mmu=off'),('irq=on','irq=off'),('caches=off','caches=on'),('40235000','40235010'),('pages-free=30000','pages-free={30000-stamp}'),('recoveries={recovered}','recoveries=7'),('microseconds=10000','microseconds=10001'),('pages=8','pages=7'),('state=OK','state=FAIL'),('uptime-us={stamp*10000}','uptime-us=10000'),('timer-ticks={stamp}','timer-ticks=0')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success)
    def test_exit_closed_input_stderr_and_timeout(self):
        result=self.run_fake(CONSOLE.replace('stamp+=1',"os.write(2,b'benchmark failed');sys.exit(7)"));self.assertFalse(result.success);self.assertIn(b'benchmark failed',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)');self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3);self.assertFalse(result.success)
    def test_stderr_draining_and_missing_report(self):
        result=self.run_fake(CONSOLE.replace('stamp+=1',"os.write(2,b'diagnostic'*10000);stamp+=1"));self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake(CONSOLE.replace('stamp+=1','time.sleep(30);stamp+=1'),timeout=.3);self.assertFalse(result.success);self.assertIn('Timed out',result.reason)
