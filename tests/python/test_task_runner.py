"""Task progress, preemption/context reports, retained accounting and cleanup."""

import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_smp_runner

qemu=test_boot_runner.qemu
CONSOLE='batch=0\ntimer_stamp=0\n'+test_smp_runner.CONSOLE.replace("    if command == b'smp':",r'''
    if command == b'tasks' or command == b'tasks test':
        if command == b'tasks test':batch+=1
        response+=f'tasks: cpu=boot capacity=8 current=0 runnable=1 sleeping=0 exited=0 switches={batch*33} preemptions={batch*8} yields={batch*14} sleeps={batch*6} completed={batch*3}\r\n'.encode()
        response+=b'tasks: test OK workers=3 completed=3 preemptions=8 yields=14 sleeps=6 context=OK stack=OK\r\n' if batch else b'tasks: test=not-run\r\n'
    elif command.startswith(b'tasks'):response+=b'usage: tasks [test]\r\n'
    elif command == b'timer':
        timer_stamp+=1
        response+=f'timer: frequency=62500000 target-hz=100 interval=625000 counter={timer_stamp*625000} ticks={timer_stamp*4} missed=0\r\n'.encode()
    elif command.startswith(b'timer'):response+=b'usage: timer\r\n'
    elif command == b'recover brk':response+=b'recover: brk OK count=1\r\n'
    elif command == b'smp':''')


class TaskRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=6):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,runner=qemu.tasks_test,
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_workloads_retained_counts_progress_and_child_reaping(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),4)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_incorrect_state_counters_context_stack_and_progress(self):
        for old,new in (('current=0','current=1'),('runnable=1','runnable=4'),('sleeping=0','sleeping=1'),
                        ('exited=0','exited=3'),('workers=3','workers=2'),('context=OK','context=FAIL'),
                        ('stack=OK','stack=FAIL'),('switches={batch*33}','switches=0'),
                        ('preemptions={batch*8}','preemptions=0'),('yields={batch*14}','yields={batch*15}'),
                        ('sleeps={batch*6}','sleeps=6'),('completed={batch*3}','completed=3'),
                        ('ticks={timer_stamp*4}','ticks=1'),('test OK','test FAIL')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success,old)

    def test_partial_exit_closed_input_and_deadline(self):
        result=self.run_fake(CONSOLE.replace('batch+=1',"os.write(2,b'task failed');sys.exit(7);batch+=1"))

        self.assertFalse(result.success);self.assertIn(b'task failed',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake(CONSOLE.replace("response += b'mini-os> '",'time.sleep(30)'),timeout=.3)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)

    def test_stderr_draining_forced_shutdown_and_unexpected_exception(self):
        result=self.run_fake(CONSOLE.replace('batch+=1',"os.write(2,b'diagnostic'*10000);batch+=1"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3);self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace('batch+=1',"os.write(1,b'mini-os: exception');batch+=1"));self.assertFalse(result.success)
