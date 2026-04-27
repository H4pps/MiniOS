"""Controlled translation/write faults, separately bounded from normal MMU tests."""
import functools
import unittest
import test_boot_runner
import test_mmu_runner as fixture
qemu=test_boot_runner.qemu
class MmuFaultRunnerTests(unittest.TestCase):
    def run_fake(self,body,timeout=4):
        return test_boot_runner.BootRunnerTests.run_fake(self,body,timeout,
            runner=functools.partial(qemu.fault_test,symbols=fixture.SYMBOLS,kinds=('unmapped','readonly')),
            command_args=('-cpu','cortex-a53'))
    def test_incorrect_fault_addresses_instruction_sites_and_syndromes(self):
        for old,new in (('target=0x40201000','target=0x40201008'),('elr=0x40200400','elr=0x40200404'),('dfsc=15','dfsc=14')):
            result=self.run_fake(fixture.CONSOLE.replace(old,new));self.assertFalse(result.success)
            self.assertIn('Incorrect exception report',result.reason)
    def test_incomplete_output_and_early_exit_preserve_diagnostics(self):
        result=self.run_fake(fixture.CONSOLE.replace("report+='mini-os: halted\\r\\n'","report+='partial'"),timeout=1)
        self.assertFalse(result.success);self.assertIn(b'mini-os: exception',result.stdout)
        result=self.run_fake(fixture.CONSOLE.replace("report+='mini-os: halted", "os.write(2,b'fault failed');sys.exit(7);report+='mini-os: halted"))
        self.assertFalse(result.success);self.assertIn(b'fault failed',result.stderr)
    def test_post_halt_prompt_and_premature_process_exit_are_rejected(self):
        result=self.run_fake(fixture.CONSOLE.replace("report+='mini-os: halted\\r\\n'","report+='mini-os: halted\\r\\nmini-os> '"))
        self.assertFalse(result.success)
        result=self.run_fake(fixture.CONSOLE.replace('time.sleep(30)','sys.exit(7)'))
        self.assertFalse(result.success)
