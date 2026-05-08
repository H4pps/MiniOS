"""Heap fragmentation/reclamation serial protocols, diagnostics and cleanup."""

import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_memory_runner
import test_fdt_edit_runner

qemu=test_boot_runner.qemu
DUMP_BLOB=test_fdt_edit_runner.blob()
CONSOLE=('reclaimed=0\nreclaimable=64 if \'-dtb\' in sys.argv else 0\n'
    "if 'dumpdtb=' in ' '.join(sys.argv):\n"
    "    path=sys.argv[sys.argv.index('-machine')+1].split('dumpdtb=')[1].split(',')[0]\n"
    "    with open(path,'wb') as stream: stream.write("+repr(DUMP_BLOB)+")\n"
    "    sys.exit(0)\n"
    +test_memory_runner.CONSOLE.replace('reserved=600','reserved={600-reclaimed}').replace('free={memory*256-600}','free={memory*256-600+reclaimed}').replace("if command == b'mem':",r'''
    if command == b'heap':
        response+=b'heap: arena=0x00000000402a0000 bytes=262144 allocated=0 free=262112 overhead=32 blocks=1 state=OK\r\n'
    elif command==b'heap test':response+=b'heap: test OK\r\n'
    elif command.startswith(b'heap'):response+=b'usage: heap [test]\r\n'
    elif command==b'mem reclaim':
        response+=f'mem: reclaimed pages={reclaimable-reclaimed}\r\n'.encode();reclaimed=reclaimable
    elif command == b'mem':'''))


class HeapRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=5):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,runner=qemu.heap_test,
            command_args=('-machine','virt-8.2','-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_reports_custom_dtb_and_all_processes_reaped(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),6)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_wrong_heap_accounting_reclaim_and_test_status(self):
        for old,new in (('free=262112','free=1'),('heap: test OK','heap: test FAIL'),('reclaimed=reclaimable','reclaimed=0')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success)

    def test_dump_failure_timeout_and_serial_diagnostics(self):
        result=self.run_fake(CONSOLE.replace('sys.exit(0)','os.write(2,b"dump error");sys.exit(7)'))

        self.assertFalse(result.success);self.assertIn(b'dump error',result.stderr)
        result=self.run_fake(CONSOLE.replace("response+=b'heap: test OK", "os.write(2,b'heap error');sys.exit(7);response+=b'heap: test OK"))

        self.assertFalse(result.success);self.assertIn(b'heap error',result.stderr)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(30)',timeout=1)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)

    def test_stderr_draining_closed_input_and_partial_reports(self):
        result=self.run_fake(CONSOLE.replace("response += b'mini-os> '","os.write(2,b'diagnostic'*10000);response += b'mini-os> '"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)',timeout=1)

        self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace("response += b'mini-os> '","time.sleep(30)"),timeout=1)

        self.assertFalse(result.success)
