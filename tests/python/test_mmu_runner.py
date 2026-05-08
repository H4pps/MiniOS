"""Identity translation, data-abort context and process cleanup protocols."""

import functools
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_memory_runner

qemu = test_boot_runner.qemu
SYMBOLS = {'mini_os_fault_unmapped_site':0x40200300,'mini_os_fault_readonly_site':0x40200400,
           'mini_os_readonly_probe':0x40201000,'__stack_bottom':0x40210000,'__stack_top':0x40220000}
CONSOLE = 'timer_ticks=0\n' + test_memory_runner.CONSOLE.replace("if command == b'mem':",r'''
    if command == b'mmu':
        response += b'mmu: SCTLR_EL1=0x0000000000cd0839 TCR_EL1=0x0000000280993019 TTBR0_EL1=0x0000000040220000 MAIR_EL1=0x0000000000000044 tables=70\r\n'
        response += b'mmu: text=0x0000000040200000 text-write=denied rodata-write=denied dtb-write=denied stack=0x0000000040210000 guard=unmapped null=unmapped uart=0x0000000009000000\r\n'
    elif command == b'mmu x': response += b'usage: mmu\r\n'
    elif command == b'timer':
        timer_ticks+=1
        response += f'timer: frequency=62500000 target-hz=100 interval=625000 counter={timer_ticks*625000} ticks={timer_ticks} missed=0\r\n'.encode()
    elif command == b'timer x': response += b'usage: timer\r\n'
    elif command.startswith(b'fault '):
        readonly=command.split()[1]==b'readonly'
        esr=0x9600004f if readonly else 0x96000006
        dfsc=15 if readonly else 6
        target=0x40201000 if readonly else 0x1000
        elr=0x40200400 if readonly else 0x40200300
        reason='permission' if readonly else 'translation'
        report='mini-os: exception vector=current-spx-sync reason=data-abort\r\n'
        report+=f'esr=0x{esr:016x} ec=0x25 il=1 iss=0x{esr&0x1ffffff:07x} abort={reason} dfsc=0x{dfsc:02x} write={int(readonly)} far-valid=1\r\n'
        report+=f'elr=0x{elr:016x} spsr=0x0000000060000345\r\n'
        report+=f'sp=0x000000004021fff0 far(raw)=0x{target:016x}\r\n'
        for index in range(31): report+=f'x{index:02d}=0x{target if index==16 else 0x100+index:016x}\r\n'
        report+='mini-os: halted\r\n'
        for value in response+report.encode(): os.write(1,bytes([value]))
        time.sleep(30)
    elif command == b'mem':''')


class MmuRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=7):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=qemu.mmu_test,
            command_args=('-cpu','cortex-a53','-smp','1','-m','128M'))

    def test_fragmentation_both_models_ram_sizes_and_all_children_reaped(self):
        processes=[];popen=subprocess.Popen

        def launch(*args,**kwargs):
            process=popen(*args,**kwargs);processes.append(process);return process

        with patch.object(qemu.subprocess,'Popen',side_effect=launch):result=self.run_fake(CONSOLE)

        self.assertTrue(result.success,result.reason);self.assertEqual(len(processes),6)

        for process in processes:
            with self.assertRaises(ChildProcessError):os.waitpid(process.pid,os.WNOHANG)

            with self.assertRaises(ProcessLookupError):os.kill(process.pid,0)

    def test_incorrect_state_and_permissions(self):
        for old,new in (('00cd0839','00cd0838'),('text-write=denied','text-write=allowed')):
            result=self.run_fake(CONSOLE.replace(old,new));self.assertFalse(result.success)

    def test_incomplete_report_closed_input_timeout_and_diagnostics(self):
        for body in ('os.close(0)\nos.write(1,'+repr(qemu.READY)+')\ntime.sleep(30)',CONSOLE.replace("response += b'mini-os> '","time.sleep(30)")):
            result=self.run_fake(body,timeout=1);self.assertFalse(result.success)

        result=self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(30)',timeout=1)

        self.assertFalse(result.success);self.assertIn('Timed out',result.reason)

    def test_stderr_draining(self):
        result=self.run_fake(CONSOLE.replace("response += b'mini-os> '","os.write(2,b'diagnostic'*10000);response += b'mini-os> '"))

        self.assertTrue(result.success,result.reason);self.assertGreater(len(result.stderr),100000)
