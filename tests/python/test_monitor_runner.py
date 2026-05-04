"""Monitor protocols with independent disposable processes and decoded-state checks."""
import os
import subprocess
import unittest
from unittest.mock import patch
import test_boot_runner

qemu = test_boot_runner.qemu

CONSOLE = 'os.write(1, ' + repr(qemu.READY) + ')\n' + r'''
model = sys.argv[sys.argv.index('-cpu') + 1]
count = int(sys.argv[sys.argv.index('-smp') + 1])
part = 'd03' if model == 'cortex-a53' else 'd07'
midr = '410fd034' if part == 'd03' else '411fd070'
variant, revision = ('0', '4') if part == 'd03' else ('1', '0')
report = f'cpu: discovered={count} enabled={count} boot=0\r\n'
for index in range(count):
    report += f'cpu[{index}]: affinity=0x{index:016x} dt-status=enabled boot={"yes" if index == 0 else "no"} compatible=arm,{model}\r\n'
report += f'cpu: model={"Cortex-A53" if part == "d03" else "Cortex-A57"} implementer=0x41 part=0x{part} variant={variant} revision={revision}\r\n'
report += f'cpu: MIDR_EL1=0x{midr} MPIDR_EL1=0x0000000080000000\r\n'
report += 'cpu: EL=1 affinity=0:0:0:0\r\n'
report += 'cpu: DAIF=0x0000000000000340 D=1 A=1 I=0 F=1\r\n'
report += 'cpu: SCTLR_EL1=0x0000000000cd0839 MMU=on D-cache=off I-cache=off\r\n'
line = bytearray()
while True:
    data = os.read(0, 1)
    if not data: break
    if data != b'\n':
        line += data
        os.write(1, data)
        continue
    command, _, arguments = bytes(line).lstrip(b' ').partition(b' ')
    arguments = arguments.lstrip(b' ')
    response = b'\r\n'
    if command in (b'cpu', b'help') and arguments:
        response += b'usage: ' + command + b'\r\n'
    elif command == b'cpu': response += report.encode()
    elif command == b'help': response += b'commands:\r\n  help         show commands\r\n  cpu          show CPU inventory and boot registers\r\n  echo [text]  echo text\r\n  fault brk|undef|unmapped|readonly|stack  trigger a fatal exception\r\n  irq [test]   inspect or test interrupts\r\n  timer        inspect timer counters\r\n  mem [test|reclaim]  inspect, test or reclaim physical pages\r\n  mmu          inspect mappings and protection\r\n  heap [test]  inspect or test heap allocation\r\n  uart         inspect receive interrupts and queue\r\n  recover brk|undef  test controlled exception recovery\r\n  diag         show coherent kernel diagnostics\r\n  perf [test]  measure a bounded memory workload\r\n  topology     show DT CPU hierarchy\r\n  features     show boot CPU capabilities\r\n  smp [test]   inspect online CPUs or test secondary heartbeats\r\n  tasks [test]  inspect scheduling or verify kernel tasks\r\n  user [test]  execute an isolated EL0 example or verify faults\r\n'
    elif command == b'fault': response += b'usage: fault brk|undef|unmapped|readonly|stack\r\n'
    elif command == b'echo': response += b'echo: ' + arguments + b'\r\n'
    elif command: response += b'mini-os: unknown command: ' + command + b'\r\n'
    response += b'mini-os> '
    for byte in response: os.write(1, bytes([byte]))
    line.clear()
'''


class MonitorRunnerTests(unittest.TestCase):
    def run_fake(self, body, timeout=6):
        return test_boot_runner.BootRunnerTests.run_fake(
            self, body, timeout, runner=qemu.monitor_test,
            command_args=("-cpu", "cortex-a53", "-smp", "1"))

    def test_fragmented_reports_all_models_and_process_cleanup(self):
        processes = []
        real_popen = subprocess.Popen
        def launch(*args, **kwargs):
            process = real_popen(*args, **kwargs)
            processes.append(process)
            return process
        with patch.object(qemu.subprocess, "Popen", side_effect=launch):
            result = self.run_fake(CONSOLE)
        self.assertTrue(result.success, result.reason)
        self.assertEqual(len(processes), 3)
        self.assertIn(b"discovered=4", result.stdout)
        self.assertIn(b"model=Cortex-A57", result.stdout)
        for process in processes:
            with self.assertRaises(ChildProcessError): os.waitpid(process.pid, os.WNOHANG)
            with self.assertRaises(ProcessLookupError): os.kill(process.pid, 0)

    def test_incorrect_counts_and_decoded_fields(self):
        for old, new in (("enabled={count}", "enabled=0"), ("revision={revision}", "revision=9"),
                         ("EL=1", "EL=2"), ("I=0 F=1", "I=1 F=1"),
                         ("MMU=on", "MMU=off"), ("boot=0", "boot=1")):
            with self.subTest(field=old):
                result = self.run_fake(CONSOLE.replace(old, new))
                self.assertFalse(result.success)
                self.assertIn("Incorrect CPU report", result.reason)

    def test_partial_report_times_out_and_preserves_output(self):
        body = CONSOLE.replace("response += report.encode()", "os.write(1, b'\\r\\ncpu: discovered='); time.sleep(30)")
        result = self.run_fake(body, timeout=1)
        self.assertFalse(result.success)
        self.assertIn("Timed out", result.reason)
        self.assertIn(b"cpu: discovered=", result.stdout)

    def test_premature_exit_preserves_diagnostics(self):
        body = CONSOLE.replace("response += report.encode()", "os.write(2, b'CPU failure\\n'); sys.exit(7)")
        result = self.run_fake(body)
        self.assertFalse(result.success)
        self.assertIn("exited prematurely", result.reason)
        self.assertIn(b"CPU failure", result.stderr)

    def test_stderr_is_drained_during_reports(self):
        result = self.run_fake(CONSOLE.replace("response += report.encode()", "os.write(2, b'diagnostic' * 20000); response += report.encode()"))
        self.assertTrue(result.success, result.reason)
        self.assertEqual(result.stderr, b"diagnostic" * 20000 * 6)

    def test_overall_deadline_and_forced_cleanup(self):
        body = "if '4' in sys.argv: signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(30)\n" + CONSOLE
        result = self.run_fake(body, timeout=1)
        self.assertFalse(result.success)
        self.assertIn("4 CPU(s)", result.reason)
        self.assertIn("Timed out", result.reason)
