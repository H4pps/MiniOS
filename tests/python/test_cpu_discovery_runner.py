"""CPU hierarchy/ID protocol, malformed reports, deadlines and child cleanup."""
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner
import test_irq_runner
qemu = test_boot_runner.qemu
FEATURES = b'''features: ID_AA64PFR0_EL1=0x0000000001000022 ID_AA64ISAR0_EL1=0x0000000000011120
features: ID_AA64ISAR1_EL1=0x0000000000000000 ID_AA64MMFR0_EL1=0x0000000000001122
features: ID_AA64MMFR1_EL1=0x0000000000000000 ID_AA64DFR0_EL1=0x0000000010305106
features: el0=a64+a32(0x2) el1=a64+a32(0x2) el2=none(0x0) el3=none(0x0)
features: fp=present(0x0) simd=present(0x0) gic=v3(0x1) aes=aes+pmull(0x2)
features: sha1=present(0x1) sha2=sha256(0x1) crc32=present(0x1) atomics=none(0x0)
features: pa-bits=40(0x2) asid-bits=16(0x2) vmid-bits=8(0x0) granule4k=present(0x0)
features: granule16k=none(0x0) granule64k=present(0x0) pan=none(0x0) hafdbs=none(0x0)
features: sb=none(0x0) debug=implemented(0x6) breakpoints=6(0x5) watchpoints=4(0x3)
'''.replace(b'\n', b'\r\n')
CONSOLE = ('features=' + repr(FEATURES) + '\nhelp_text=' + repr(qemu.HELP) + '\n' +
           "model=sys.argv[sys.argv.index('-cpu')+1];count=int(sys.argv[sys.argv.index('-smp')+1])\n" +
           test_irq_runner.CONSOLE.replace("    if command == b'irq':", r'''
    if command == b'topology':
        response += f'topology: described=yes cpus={count} sockets=1 clusters=1 cores={count} threads=0\r\n'.encode()
        for i in range(count):
            response += f'topology[{i}]: affinity=0x{i:016x} socket=0 cluster=0 core={i} thread=- dt-status=enabled\r\n'.encode()
    elif command == b'features':
        report = features
        if model == 'cortex-a57': report = report.replace(b'0000000000001122', b'0000000000001124').replace(b'pa-bits=40(0x2)', b'pa-bits=44(0x4)')
        response += report
    elif command.startswith(b'topology'): response += b'usage: topology\r\n'
    elif command.startswith(b'features'): response += b'usage: features\r\n'
    elif command == b'help': response += help_text
    elif command == b'irq':'''))


class CpuDiscoveryRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=5):
        return test_boot_runner.BootRunnerTests.run_fake(
            self, body, timeout, runner=qemu.cpu_discovery_test,
            command_args=('-cpu', 'cortex-a53', '-smp', '1', '-m', '128M'))

    def test_fragmented_reports_across_models_and_capacity_reap_children(self):
        processes = []; popen = subprocess.Popen
        def launch(*args, **kwargs):
            process = popen(*args, **kwargs); processes.append(process); return process
        with patch.object(qemu.subprocess, 'Popen', side_effect=launch):
            result = self.run_fake(CONSOLE)
        self.assertTrue(result.success, result.reason)
        self.assertEqual(len(processes), 5)
        for process in processes:
            with self.assertRaises(ChildProcessError): os.waitpid(process.pid, os.WNOHANG)

    def test_incorrect_counts_hierarchy_affinity_raw_and_decoded_fields(self):
        for old, new in (('cores={count}', 'cores=9'), ('socket=0 cluster=0', 'socket=1 cluster=0'),
                         ('affinity=0x{i:016x}', 'affinity=0x0000000000000007'),
                         ('described=yes', 'described=no'), ('dt-status=enabled', 'dt-status=disabled'),
                         ('0000000000011120', '0000000000011130'), ('sha256', 'unknown'),
                         ('breakpoints=6', 'breakpoints=8'), ('pa-bits=44(0x4)', 'pa-bits=40(0x2)')):
            result = self.run_fake(CONSOLE.replace(old, new))
            self.assertFalse(result.success, old)

    def test_partial_exit_closed_input_and_timeout(self):
        result = self.run_fake(CONSOLE.replace('report = features', "os.write(2,b'feature failed');sys.exit(7);report = features"))
        self.assertFalse(result.success); self.assertIn(b'feature failed', result.stderr)
        result = self.run_fake('os.close(0)\nos.write(1,' + repr(qemu.READY) + ')\ntime.sleep(30)')
        self.assertFalse(result.success); self.assertIn('closed serial stdin', result.reason)
        result = self.run_fake(CONSOLE.replace("response += b'mini-os> '", 'time.sleep(30)'), timeout=.3)
        self.assertFalse(result.success); self.assertIn('Timed out', result.reason)

    def test_stderr_draining_forced_shutdown_and_unexpected_exception(self):
        result = self.run_fake(CONSOLE.replace('report = features', "os.write(2,b'diagnostic'*10000);report = features"))
        self.assertTrue(result.success, result.reason); self.assertGreater(len(result.stderr), 100000)
        result = self.run_fake('signal.signal(signal.SIGTERM,signal.SIG_IGN)\ntime.sleep(30)', timeout=.3)
        self.assertFalse(result.success)
        result = self.run_fake(CONSOLE.replace('report = features', "os.write(1,b'mini-os: exception');report = features"))
        self.assertFalse(result.success)
