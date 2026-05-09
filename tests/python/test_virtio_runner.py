"""VirtIO discovery/data/IRQ protocols, read-only fixtures, deadlines and child cleanup."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_elf_loader_runner

qemu=test_boot_runner.qemu
PREFIX=r'''
from pathlib import Path
import json
read_count=0
present='-blockdev' in sys.argv
sectors=0
if present:
    backend=json.loads(sys.argv[sys.argv.index('-blockdev')+1])
    disk=Path(backend['file']['filename'])
    sectors=disk.stat().st_size//512
    if '-global' not in sys.argv:
        os.write(1,b'mini-os: boot FAIL: virtio unsupported block transport\r\n')
        time.sleep(30)
def virtio_report():
    if not present:return b'virtio: transports=32 devices=0 block=no\r\n'
    return f'virtio: transports=32 devices=1 block=yes base=0x000000000a003e00 interrupt=79 version=2 sectors={sectors} readonly=yes queue=8 submitted={read_count} completed={read_count} interrupts={read_count} ready=yes error=none\r\n'.encode()
def sector_line(sector):
    with disk.open('rb') as stream:
        stream.seek(sector*512);data=stream.read(512)
    checksum=2166136261
    for byte in data:checksum=((checksum^byte)*16777619)&0xffffffff
    return f'virtio: read sector={sector} checksum=0x{checksum:08x} first=0x{data[:8].hex()} last=0x{data[-8:].hex()}\r\n'.encode()
'''
CONSOLE=PREFIX+test_elf_loader_runner.CONSOLE.replace("    elif command == b'elf':",r'''
    elif command == b'virtio':response+=virtio_report()
    elif command == b'virtio test':
        response+=virtio_report()
        if not present:response+=b'virtio: test unavailable\r\n'
        else:
            for sector in (0,sectors-1,0,sectors-1):response+=sector_line(sector)
            read_count+=4
            response+=b'virtio: test OK reads=4 interrupts=4\r\n'
    elif command.startswith(b'virtio'):response+=b'usage: virtio [test]\r\n'
    elif command==b'mem test':response+=b'mem: test OK\r\n'
    elif command == b'elf':''')


class VirtioRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=10):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.virtio_test,image=test_elf_loader_runner.IMAGE),
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmented_models_ram_disk_sizes_no_device_legacy_and_all_children_reaped(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),6)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

    def test_incorrect_discovery_features_data_counters_and_page_accounting(self):
        for old,new in (('transports=32','transports=31'),('block=yes','block=no'),
                        ('base=0x000000000a003e00','base=0x000000000b003e00'),('interrupt=79','interrupt=78'),
                        ('version=2','version=1'),('sectors={sectors}','sectors=1'),('readonly=yes','readonly=no'),
                        ('queue=8','queue=4'),('submitted={read_count}','submitted=0'),
                        ('completed={read_count}','completed=0'),('ready=yes','ready=no'),('error=none','error=io'),
                        ('checksum:08x','0:08x'),('data[:8].hex()','data[-8:].hex()'),
                        ('test OK reads=4','test FAIL reads=4'),('reads=4 interrupts=4','reads=4 interrupts=0')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success,old)

        leaked=CONSOLE.replace('allocated=131','allocated=131+read_count')
        result=self.run_fake(leaked);self.assertFalse(result.success);self.assertIn('leaked',result.reason)
        result=self.run_fake(CONSOLE.replace("os.write(1,b'mini-os: boot FAIL:","os.write(1,b'mini-os: boot OK\\r\\nmini-os: boot FAIL:"))

        self.assertFalse(result.success);self.assertIn('expected boot failure',result.reason)

    def test_incomplete_exit_closed_input_timeout_and_cleanup(self):
        result=self.run_fake(CONSOLE.replace('read_count+=4',"os.write(2,b'block failed');sys.exit(7);read_count+=4"))

        self.assertFalse(result.success);self.assertIn(b'block failed',result.stderr)
        result=self.run_fake('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)')

        self.assertFalse(result.success);self.assertIn('closed serial stdin',result.reason)
        result=self.run_fake(CONSOLE.replace('read_count+=4',"os.write(1,response[:80]);time.sleep(30);read_count+=4"),timeout=.3)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)

    def test_stderr_forced_shutdown_and_fatal_exception_rejection(self):
        result=self.run_fake(CONSOLE.replace('read_count+=4',"os.write(2,b'diagnostic'*10000);read_count+=4"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)',timeout=.3)

        self.assertFalse(result.success)
        result=self.run_fake(CONSOLE.replace('read_count+=4',"os.write(1,b'mini-os: exception');read_count+=4"))

        self.assertFalse(result.success)
