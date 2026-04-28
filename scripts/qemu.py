#!/usr/bin/env python3
"""Shared QEMU configuration and bounded serial boot, input, and fatal exception verification."""

import argparse
import json
import math
from dataclasses import dataclass
import os
import re
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from verify_elf import inspect
from fdt_edit import add_test_reservations

SUCCESS = b"mini-os: boot OK"
FAILURE = b"mini-os: boot FAIL:"
EXCEPTION = b"mini-os: exception"
QEMU_ARGS = [
    "-machine", "virt-8.2,gic-version=3,secure=off,virtualization=off",
    "-cpu", "cortex-a53", "-accel", "tcg", "-smp", "1", "-m", "128M",
    "-display", "none", "-monitor", "none", "-serial", "stdio", "-no-reboot",
]


@dataclass
class BootResult:
    success: bool
    reason: str
    stdout: bytes
    stderr: bytes
    pid: int


def stop_process(process):
    """Always reap the emulator, including one that ignores SIGTERM."""
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=1)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


PROMPT = b"mini-os> "
def discovery_line(memory_mib=128):
    return (f"mini-os: dtb OK uart=0x0000000009000000 clock=24000000 "
            f"ram=0x0000000040000000 size=0x{memory_mib * 1024 * 1024:016x}\r\n").encode()


READY = discovery_line() + SUCCESS + b"\r\nmini-os: uart ready\r\n" + PROMPT


def uart_exchanges(memory_mib=128):
    """Send small fresh exchanges, pacing printable input by its immediate echo."""
    yield "readiness", b"", discovery_line(memory_mib) + SUCCESS + b"\r\nmini-os: uart ready\r\n" + PROMPT
    cases = [
        (b"echo first\n", b"first"),
        (b"echo first\n", b"first"),
        (b"echo ab\x08c\x7fd\r\n", b"ad"),
        (b"echo cr\r", b"cr"),
        (b"echo lf\n", b"lf"),
        (b"\x08\x7f\n", b""),
        (b"\r\n", b""),
        (b"\x00\x01\x1b\t\x80\xffecho ok\n", b"ok"),
        (b"echo " + b"x" * 122 + b"\n", b"x" * 122),
        (b"echo " + b"x" * 123 + b"discard\x08\x7f\r\n", None),
        (b"echo recovered\n", b"recovered"),
    ]
    for index, (payload, line) in enumerate(cases):
        length = 0
        rejected = False
        suppress_lf = False
        for byte in payload:
            expected = b""
            if suppress_lf and byte == 10:
                suppress_lf = False
            elif byte in (10, 13):
                suppress_lf = byte == 13
                if rejected:
                    expected = b"\r\nmini-os: line too long\r\n" + PROMPT
                else:
                    expected = b"\r\n"
                    if line:
                        expected += b"echo: " + line + b"\r\n"
                    expected += PROMPT
            elif rejected:
                pass
            elif byte in (8, 127):
                if length:
                    length -= 1
                    expected = b"\x08 \x08"
            elif 32 <= byte <= 126:
                if length == 127:
                    rejected = True
                    expected = b"\a"
                else:
                    length += 1
                    expected = bytes([byte])
            yield f"case {index + 1}, byte {byte}", bytes([byte]), expected


def drain(selector, output, wait, qmp_output=None):
    for key, _ in selector.select(wait):
        chunk = os.read(key.fd, 65536)
        if chunk:
            destination = qmp_output if key.data == "qmp" else output[key.data]
            destination.extend(chunk)
        else:
            selector.unregister(key.fileobj)


def inject_dtb_failure(process, path, deadline, selector, output):
    """Re-register a cold-plugged data loader after ROM reset, then reset and run."""
    # A command-line loader resets before the DTB ROM and is overwritten by it.
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as control:
        while True:
            if time.monotonic() >= deadline:
                raise RuntimeError("Timed out connecting to QMP for DTB injection")
            if process.poll() is not None:
                raise RuntimeError(f"Emulator exited prematurely ({process.returncode})")
            try:
                control.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                drain(selector, output, min(0.01, max(0, deadline - time.monotonic())))
        control.setblocking(False)
        selector.register(control, selectors.EVENT_READ, "qmp")
        messages = bytearray()
        requests = [
            {"execute": "qmp_capabilities", "id": 1},
            {"execute": "qom-set", "id": 2,
             "arguments": {"path": "/machine/peripheral/dtb-corruption", "property": "realized", "value": False}},
            {"execute": "qom-set", "id": 3,
             "arguments": {"path": "/machine/peripheral/dtb-corruption", "property": "realized", "value": True}},
            {"execute": "system_reset", "id": 4},
            {"execute": "cont", "id": 5},
        ]
        stage = -1
        pending = b""
        try:
            while stage < len(requests):
                if time.monotonic() >= deadline:
                    raise RuntimeError("Timed out during QMP DTB injection")
                if process.poll() is not None:
                    raise RuntimeError(f"Emulator exited prematurely ({process.returncode})")
                if pending:
                    try:
                        pending = pending[control.send(pending):]
                    except BlockingIOError:
                        pass
                drain(selector, output, min(0.01, max(0, deadline - time.monotonic())), messages)
                if control not in selector.get_map():
                    raise RuntimeError("QMP closed during DTB injection")
                while b"\n" in messages:
                    line, _, rest = messages.partition(b"\n")
                    messages[:] = rest
                    response = json.loads(line)
                    if "error" in response:
                        raise RuntimeError(f"QMP DTB injection failed: {response['error']}")
                    if stage == -1 and "QMP" in response:
                        stage = 0
                    elif 0 <= stage < len(requests) and response.get("id") == requests[stage]["id"] and "return" in response:
                        stage += 1
                    else:
                        continue  # Asynchronous RESET/RESUME events are not replies.
                    if stage < len(requests):
                        pending = json.dumps(requests[stage]).encode() + b"\n"
        finally:
            if control in selector.get_map():
                selector.unregister(control)


def serial_test(command, timeout, exchanges=None, expected_failure=None, qmp_path=None, allow_exception=False):
    """One deadline and continuously drained pipes for boot and UART protocols."""
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Serial deadline must be positive")
    output = {"stdout": bytearray(), "stderr": bytearray()}
    interactive = exchanges is not None
    steps = iter(exchanges) if interactive else None
    step = next(steps, None) if interactive else None
    deadline = time.monotonic() + timeout
    process = subprocess.Popen(command, stdin=subprocess.PIPE if interactive else None,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    success = False
    reason = "Timed out waiting for serial confirmation"
    sent = 0
    cursor = 0
    confirmed_at = None
    try:
        with selectors.DefaultSelector() as selector:
            for name in output:
                stream = getattr(process, name)
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, selectors.EVENT_READ, name)
            if interactive:
                os.set_blocking(process.stdin.fileno(), False)
            if qmp_path is not None:
                inject_dtb_failure(process, qmp_path, deadline, selector, output)
            while time.monotonic() < deadline:
                if interactive and step is not None and sent < len(step[1]):
                    try:
                        sent += os.write(process.stdin.fileno(), step[1][sent:sent + 8])
                    except BlockingIOError:
                        pass
                    except BrokenPipeError:
                        reason = "Emulator closed serial stdin"
                        break
                drain(selector, output, min(0.01, max(0, deadline - time.monotonic())))
                serial = bytes(output["stdout"])
                if FAILURE in serial and expected_failure is None:
                    reason = "Kernel reported boot failure"
                    break
                if EXCEPTION in serial and not allow_exception:
                    reason = "Kernel reported unexpected exception"
                    break
                if process.poll() is not None:
                    reason = f"Emulator exited prematurely ({process.returncode})"
                    break
                if expected_failure is not None:
                    lines = serial.replace(b"\r\n", b"\n").split(b"\n")[:-1]
                    if SUCCESS in serial or any(FAILURE in line and line != expected_failure for line in lines):
                        reason = "Incorrect expected DTB failure response"
                        break
                    complete = expected_failure in lines
                elif interactive:
                    if step is not None:
                        name, payload, expected = step
                        if callable(expected):
                            matched = expected(serial[cursor:])
                        else:
                            fresh = serial[cursor:cursor + len(expected)]
                            if not expected.startswith(fresh):
                                reason = f"Incorrect UART response during {name}: expected {expected!r}, received {fresh!r}"
                                break
                            matched = len(expected) if len(fresh) == len(expected) else None
                        if sent == len(payload) and matched is not None:
                            cursor += matched
                            if len(serial) != cursor:
                                reason = f"Incorrect UART response during {name}: unsolicited output {serial[cursor:]!r}"
                                break
                            step = next(steps, None)
                            sent = 0
                    complete = step is None
                else:
                    lines = serial.replace(b"\r\n", b"\n").split(b"\n")[:-1]
                    resource = discovery_line().rstrip(b"\r\n")
                    if any(line.startswith(b"mini-os: dtb OK") and line != resource for line in lines):
                        reason = "Incorrect device tree resource confirmation"
                        break
                    complete = resource in lines and SUCCESS in lines and lines.index(resource) < lines.index(SUCCESS)
                if complete:
                    # A successful kernel stays alive. Drain any delayed diagnostics
                    # and reject extra UART output, including duplicate CRLF prompts.
                    if interactive and len(serial) != cursor:
                        reason = f"Unexpected trailing UART output: {serial[cursor:]!r}"
                        break
                    if confirmed_at is None:
                        confirmed_at = time.monotonic()
                    if time.monotonic() - confirmed_at >= 0.05:
                        success = True
                        reason = ("Expected DTB failure verified" if expected_failure is not None else
                                  "Serial UART exchanges verified" if interactive else "Serial boot confirmation received")
                        break
            else:
                if interactive and step is not None:
                    reason = f"Timed out waiting for UART response during {step[0]}"
    except (OSError, RuntimeError, ValueError) as error:
        reason = str(error)
    finally:
        stop_process(process)
        if process.stdin is not None:
            process.stdin.close()
        for name in output:
            stream = getattr(process, name)
            os.set_blocking(stream.fileno(), True)
            output[name].extend(stream.read())
            stream.close()
    return BootResult(success, reason, bytes(output["stdout"]), bytes(output["stderr"]), process.pid)


def boot_test(command, timeout=10):
    return serial_test(command, timeout)


def uart_test(command, timeout=10):
    return serial_test(command, timeout, uart_exchanges())


def fdt_test(command, timeout=10):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Serial deadline must be positive")
    deadline = time.monotonic() + timeout
    output, diagnostics = bytearray(), bytearray()
    last_pid = 0
    for name in ("128 MiB discovery", "256 MiB discovery and UART", "corrupt DTB rejection"):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return BootResult(False, f"Timed out during {name}", bytes(output), bytes(diagnostics), last_pid)
        scenario = list(command)
        if name.startswith("256"):
            scenario[scenario.index("-m") + 1] = "256M"
            result = serial_test(scenario, remaining, uart_exchanges(256))
        elif name.startswith("corrupt"):
            with tempfile.TemporaryDirectory(prefix="mini-os-fdt-", dir="/tmp") as directory:
                path = Path(directory) / "qmp.sock"
                # This scenario requires one deliberate reset after re-registering the loader.
                if "-no-reboot" in scenario:
                    scenario.remove("-no-reboot")
                scenario.extend(["-S", "-qmp", f"unix:{path},server=on,wait=off", "-device",
                                 "loader,id=dtb-corruption,addr=0x40000000,data=0,data-len=4"])
                result = serial_test(scenario, remaining,
                                     expected_failure=b"mini-os: boot FAIL: dtb bad magic", qmp_path=path)
        else:
            result = serial_test(scenario, remaining, [next(uart_exchanges())])
        output.extend(result.stdout)
        diagnostics.extend(result.stderr)
        last_pid = result.pid
        if not result.success:
            return BootResult(False, f"{name}: {result.reason}", bytes(output), bytes(diagnostics), last_pid)
    return BootResult(True, "Device tree discovery, RAM variation, UART, and rejection verified",
                      bytes(output), bytes(diagnostics), last_pid)


HELP = b'commands:\r\n  help         show commands\r\n  cpu          show CPU inventory and boot registers\r\n  echo [text]  echo text\r\n  fault brk|undef|unmapped|readonly  trigger a fatal exception\r\n  irq [test]   inspect or test interrupts\r\n  timer        inspect timer counters\r\n  mem [test|reclaim]  inspect, test or reclaim physical pages\r\n  mmu          inspect mappings and protection\r\n  heap [test]  inspect or test heap allocation\r\n'


def cpu_report_matcher(model, count):
    """Validate a complete report and cross-check decoded fields against raw registers."""
    def match(data):
        prefix = b"\r\ncpu: discovered="
        if len(data) < len(prefix) and prefix.startswith(data):
            return None
        if not data.startswith(prefix):
            raise ValueError("Incorrect CPU report prefix")
        end = data.find(PROMPT)
        if end < 0:
            return None
        lines = data[2:end].split(b"\r\n")
        if lines[-1] != b"" or len(lines) != count + 7:
            raise ValueError("Incorrect CPU report length")
        def require(condition, detail):
            if not condition:
                raise ValueError(f"Incorrect CPU report: {detail}")
        require(lines[0] == f"cpu: discovered={count} enabled={count} boot=0".encode(), "inventory counts")
        for index in range(count):
            expected = (f"cpu[{index}]: affinity=0x{index:016x} dt-status=enabled "
                        f"boot={'yes' if index == 0 else 'no'} compatible=arm,{model}").encode()
            require(lines[index + 1] == expected, "CPU identity or boot match")
        identity = re.fullmatch(rb"cpu: model=(Cortex-A(?:53|57)) implementer=0x([0-9a-f]{2}) part=0x([0-9a-f]{3}) variant=([0-9]+) revision=([0-9]+)", lines[count + 1])
        registers = re.fullmatch(rb"cpu: MIDR_EL1=0x([0-9a-f]{8}) MPIDR_EL1=0x([0-9a-f]{16})", lines[count + 2])
        masks = re.fullmatch(rb"cpu: DAIF=0x([0-9a-f]{16}) D=([01]) A=([01]) I=([01]) F=([01])", lines[count + 4])
        control = re.fullmatch(rb"cpu: SCTLR_EL1=0x([0-9a-f]{16}) MMU=(on|off) D-cache=(on|off) I-cache=(on|off)", lines[count + 5])
        # Five fixed report lines follow the inventory rows, plus the trailing empty line.
        require(identity is not None and registers is not None and masks is not None and control is not None, "register formatting")
        midr, mpidr = (int(value, 16) for value in registers.groups())
        part = 0xd03 if model == "cortex-a53" else 0xd07
        require(identity[1] == (b"Cortex-A53" if part == 0xd03 else b"Cortex-A57"), "model")
        require(int(identity[2], 16) == (midr >> 24) == 0x41 and
                int(identity[3], 16) == ((midr >> 4) & 0xfff) == part and
                int(identity[4]) == ((midr >> 20) & 15) and int(identity[5]) == (midr & 15), "MIDR decoding")
        require(mpidr & 0xff00ffffff == 0, "boot affinity")
        require(lines[count + 3] == b"cpu: EL=1 affinity=0:0:0:0", "execution level or affinity")
        daif = int(masks[1], 16)
        require(daif & 0x3c0 == 0x340 and all(int(masks[index + 2]) == ((daif >> (9 - index)) & 1) for index in range(4)), "interrupt masks")
        sctlr = int(control[1], 16)
        require(sctlr & 0x1005 == 1 and all(control[index + 2] == (b"on" if sctlr & (1 << bit) else b"off") for index, bit in enumerate((0, 2, 12))), "MMU or cache controls")
        return end + len(PROMPT)
    return match


def monitor_exchanges(model, count):
    yield next(uart_exchanges())
    cases = [
        (b"help", b"\r\n" + HELP + PROMPT),
        (b"cpu", cpu_report_matcher(model, count)),
        (b" cpu  ", cpu_report_matcher(model, count)),
        (b"help x", b"\r\nusage: help\r\n" + PROMPT),
        (b"cpu x", b"\r\nusage: cpu\r\n" + PROMPT),
        (b"fault", b"\r\nusage: fault brk|undef|unmapped|readonly\r\n" + PROMPT),
        (b"fault BRK", b"\r\nusage: fault brk|undef|unmapped|readonly\r\n" + PROMPT),
        (b"fault brk x", b"\r\nusage: fault brk|undef|unmapped|readonly\r\n" + PROMPT),
        (b"CPU", b"\r\nmini-os: unknown command: CPU\r\n" + PROMPT),
        (b"unknown", b"\r\nmini-os: unknown command: unknown\r\n" + PROMPT),
        (b"", b"\r\n" + PROMPT),
        (b"   ", b"\r\n" + PROMPT),
        (b"echo", b"\r\necho: \r\n" + PROMPT),
        (b" echo   hello  world  ", b"\r\necho: hello  world  \r\n" + PROMPT),
        (b"echo recovered", b"\r\necho: recovered\r\n" + PROMPT),
    ]
    for payload, response in cases:
        for byte in payload:
            yield "monitor character", bytes([byte]), bytes([byte])
        yield "monitor submission", b"\n", response


def monitor_test(command, timeout=10):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Serial deadline must be positive")
    deadline = time.monotonic() + timeout
    output, diagnostics = bytearray(), bytearray()
    last_pid = 0
    for model, count in (("cortex-a53", 1), ("cortex-a53", 4), ("cortex-a57", 1)):
        name = f"{model}, {count} CPU(s)"
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return BootResult(False, f"Timed out during {name}", bytes(output), bytes(diagnostics), last_pid)
        scenario = list(command)
        scenario[scenario.index("-cpu") + 1] = model
        scenario[scenario.index("-smp") + 1] = str(count)
        result = serial_test(scenario, remaining, monitor_exchanges(model, count))
        output.extend(result.stdout); diagnostics.extend(result.stderr); last_pid = result.pid
        if not result.success:
            return BootResult(False, f"{name}: {result.reason}", bytes(output), bytes(diagnostics), last_pid)
    return BootResult(True, "CPU inventory, register inspection, and monitor commands verified",
                      bytes(output), bytes(diagnostics), last_pid)



def command_exchange(name, payload, response):
    for byte in payload:
        yield name + " character", bytes([byte]), bytes([byte])
    yield name + " submission", b"\n", response


def scenario_test(command, timeout, scenarios, exchanges):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Serial deadline must be positive")
    deadline = time.monotonic() + timeout
    output, diagnostics = bytearray(), bytearray()
    last_pid = 0
    for model, count, memory in scenarios:
        name = f"{model}, {count} CPU(s), {memory} MiB"
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return BootResult(False, f"Timed out during {name}", bytes(output), bytes(diagnostics), last_pid)
        scenario = list(command)
        for flag, value in (("-cpu", model), ("-smp", str(count)), ("-m", f"{memory}M")):
            scenario[scenario.index(flag) + 1] = value
        result = serial_test(scenario, remaining, exchanges(memory))
        output.extend(result.stdout); diagnostics.extend(result.stderr); last_pid = result.pid
        if not result.success:
            return BootResult(False, f"{name}: {result.reason}", bytes(output), bytes(diagnostics), last_pid)
    return BootResult(True, "All serial scenarios verified", bytes(output), bytes(diagnostics), last_pid)


CPU_SCENARIOS = (("cortex-a53", 1, 128), ("cortex-a53", 4, 128), ("cortex-a57", 1, 128))


def irq_exchanges(memory=128):
    yield next(uart_exchanges(memory))
    previous = [-1, -1]
    def report(expected_sgi):
        def match(data):
            if PROMPT not in data:
                return None
            result = re.fullmatch(rb"\r\nirq: distributor=0x0000000008000000 redistributor=0x00000000080a0000 limit=([0-9]+) delivered=([0-9]+) self-sgi=([0-9]+)\r\nmini-os> ", data)
            if result is None:
                raise ValueError("Incorrect IRQ report formatting or resources")
            limit, delivered, sgi = (int(value) for value in result.groups())
            if not (32 <= limit <= 1020 and sgi == expected_sgi and delivered >= sgi and
                    delivered >= previous[0] and sgi >= previous[1]):
                raise ValueError("Incorrect IRQ counts")
            previous[:] = [delivered, sgi]
            return len(data)
        return match
    yield from command_exchange("IRQ status", b"irq", report(0))
    for number in range(1, 4):
        yield from command_exchange("IRQ integrity", b"irq test  ", b"\r\nirq: test OK\r\n" + PROMPT)
        yield from command_exchange("IRQ status", b"irq", report(number))
        yield from command_exchange("IRQ recovery", b"echo after IRQ", b"\r\necho: after IRQ\r\n" + PROMPT)
    yield from command_exchange("IRQ usage", b"irq test extra", b"\r\nusage: irq [test]\r\n" + PROMPT)


def irq_test(command, timeout=10):
    return scenario_test(command, timeout, CPU_SCENARIOS, irq_exchanges)


def serial_pause(seconds):
    until = [None]
    def match(data):
        if data:
            raise ValueError("Unexpected asynchronous serial output")
        if until[0] is None:
            until[0] = time.monotonic() + seconds
        return 0 if time.monotonic() >= until[0] else None
    return match


def timer_exchanges(memory=128):
    yield next(uart_exchanges(memory))
    observed = []
    def report(data):
        if PROMPT not in data:
            return None
        result = re.fullmatch(rb"\r\ntimer: frequency=([0-9]+) target-hz=100 interval=([0-9]+) counter=([0-9]+) ticks=([0-9]+) missed=([0-9]+)\r\nmini-os> ", data)
        if result is None:
            raise ValueError("Incorrect timer report formatting")
        frequency, interval, counter, ticks, missed = (int(value) for value in result.groups())
        if not (100 <= frequency <= 0xffffffff and interval == (frequency + 99) // 100 and
                all(0 <= value <= 0xffffffffffffffff for value in (counter, ticks, missed))):
            raise ValueError("Incorrect timer frequency or interval")
        if observed and (frequency != observed[0][0] or counter <= observed[-1][2] or ticks < observed[-1][3] or missed < observed[-1][4]):
            raise ValueError("Incorrect timer progress")
        observed.append((frequency, interval, counter, ticks, missed))
        return len(data)
    yield from command_exchange("timer baseline", b"timer", report)
    for _ in range(16):
        yield "timer wait", b"", serial_pause(0.05)
        yield from command_exchange("timer progress", b"timer", report)
        yield from command_exchange("timer console", b"echo ticking", b"\r\necho: ticking\r\n" + PROMPT)
        if observed[-1][3] >= observed[0][3] + 3:
            break
    else:
        raise ValueError("Timer failed to progress")
    yield from command_exchange("timer IRQ", b"irq test", b"\r\nirq: test OK\r\n" + PROMPT)
    yield from command_exchange("timer usage", b"timer x", b"\r\nusage: timer\r\n" + PROMPT)


def timer_test(command, timeout=10):
    return scenario_test(command, timeout, CPU_SCENARIOS, timer_exchanges)


def memory_exchanges(memory=128):
    yield next(uart_exchanges(memory))
    baseline = []
    def report(data):
        if PROMPT not in data: return None
        match = re.fullmatch(rb"\r\nmem: base=0x([0-9a-f]{16}) size=0x([0-9a-f]{16}) pages=([0-9]+) reserved=([0-9]+) allocated=([0-9]+) free=([0-9]+) metadata=0x([0-9a-f]{16})\r\nmini-os> ", data)
        if match is None: raise ValueError("Incorrect memory report formatting")
        base, size, pages, reserved, allocated, free, metadata = (int(value, 16 if i in (0,1,6) else 10) for i, value in enumerate(match.groups()))
        if not (base == 0x40000000 and size == memory*1024*1024 and pages == size//4096 and reserved >= 512 and pages == reserved+allocated+free and free >= 8 and base+0x200000 <= metadata < base+size and metadata%4096 == 0):
            raise ValueError("Incorrect memory accounting or metadata placement")
        values = (base,size,pages,reserved,allocated,free,metadata)
        if baseline and values != baseline[0]: raise ValueError("Memory test changed accounting")
        baseline[:] = [values]
        return len(data)
    yield from command_exchange("memory baseline", b"mem", report)
    for _ in range(3):
        yield from command_exchange("memory allocation", b"mem test  ", b"\r\nmem: test OK\r\n" + PROMPT)
        yield from command_exchange("memory restored", b"mem", report)
        yield from command_exchange("memory console", b"echo allocated", b"\r\necho: allocated\r\n" + PROMPT)
    yield from command_exchange("memory usage", b"mem test extra", b"\r\nusage: mem [test|reclaim]\r\n" + PROMPT)


def memory_test(command, timeout=10):
    return scenario_test(command, timeout, (("cortex-a53",1,128),("cortex-a53",1,256)), memory_exchanges)


def heap_exchanges(memory=128,reclaim=0):
    yield next(uart_exchanges(memory))
    baseline=[]
    def report(data):
        if PROMPT not in data: return None
        match=re.fullmatch(rb"\r\nheap: arena=0x([0-9a-f]{16}) bytes=([0-9]+) allocated=([0-9]+) free=([0-9]+) overhead=([0-9]+) blocks=([0-9]+) state=OK\r\nmini-os> ",data)
        if match is None: raise ValueError("Incorrect heap report formatting")
        values=tuple(int(v,16 if i==0 else 10) for i,v in enumerate(match.groups()))
        arena,size,allocated,free,overhead,blocks=values
        if not (size==262144 and 0x40200000<=arena<=0x40000000+memory*1024*1024-size and arena%4096==0 and allocated+free+overhead==size and free>20000 and blocks>0): raise ValueError("Incorrect heap accounting")
        if baseline and values!=baseline[0]: raise ValueError("Heap test changed accounting")
        baseline[:]=[values];return len(data)
    yield from command_exchange("heap baseline",b"heap",report)
    for _ in range(3):
        yield from command_exchange("heap allocation",b"heap test  ",b"\r\nheap: test OK\r\n"+PROMPT)
        yield from command_exchange("heap coalescing",b"heap",report)
    snapshots=[]
    def memory_report(data):
        if PROMPT not in data: return None
        match=re.fullmatch(rb"\r\nmem: base=0x0000000040000000 size=0x([0-9a-f]{16}) pages=([0-9]+) reserved=([0-9]+) allocated=([0-9]+) free=([0-9]+) metadata=0x([0-9a-f]{16})\r\nmini-os> ",data)
        if match is None: raise ValueError("Incorrect reclaim accounting format")
        size,pages,reserved,allocated,free,metadata=(int(v,16 if i in (0,5) else 10) for i,v in enumerate(match.groups()))
        if size!=memory*1024*1024 or pages!=size//4096 or reserved+allocated+free!=pages: raise ValueError("Incorrect reclaim accounting")
        if snapshots:
            old=snapshots[0]
            if (reserved!=old[0]-reclaim or allocated!=old[1] or free!=old[2]+reclaim or metadata!=old[3]): raise ValueError("Incorrect reclaimed pages")
        snapshots.append((reserved,allocated,free,metadata));return len(data)
    yield from command_exchange("reclaim baseline",b"mem",memory_report)
    yield from command_exchange("reclaim reusable",b"mem reclaim",f"\r\nmem: reclaimed pages={reclaim}\r\n".encode()+PROMPT)
    yield from command_exchange("reclaim accounting",b"mem",memory_report)
    yield from command_exchange("reclaim idempotence",b"mem reclaim",b"\r\nmem: reclaimed pages=0\r\n"+PROMPT)
    yield from command_exchange("heap after reclaim",b"heap",report)
    yield from command_exchange("heap usage",b"heap test extra",b"\r\nusage: heap [test]\r\n"+PROMPT)
    yield from command_exchange("heap console",b"echo heap alive",b"\r\necho: heap alive\r\n"+PROMPT)


def heap_test(command,timeout=10):
    deadline=time.monotonic()+timeout
    normal=scenario_test(command,timeout,CPU_SCENARIOS+(("cortex-a53",1,256),),heap_exchanges)
    if not normal.success: return normal
    with tempfile.TemporaryDirectory(prefix="mini-os-heap-") as directory:
        path=Path(directory)/"reserved.dtb"
        dump=list(command);index=dump.index("-machine")+1;dump[index]+=",dumpdtb="+str(path)
        process=None; output=b""; diagnostics=b""
        try:
            remaining=deadline-time.monotonic()
            if remaining<=0: raise TimeoutError("DTB dump deadline")
            process=subprocess.Popen(dump,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            output,diagnostics=process.communicate(timeout=remaining)
            if process.returncode!=0 or not path.is_file(): return BootResult(False,"DTB dump failed",normal.stdout+output,normal.stderr+diagnostics,process.pid)
            path.write_bytes(add_test_reservations(path.read_bytes()))
        except (OSError,ValueError,TimeoutError,subprocess.TimeoutExpired) as error:
            if process is not None:
                stop_process(process)
                output,diagnostics=process.communicate()
            return BootResult(False,f"Reserved DTB: {error}",normal.stdout+output,normal.stderr+diagnostics,process.pid if process else normal.pid)
        finally:
            if process is not None: stop_process(process)
        remaining=deadline-time.monotonic()
        if remaining<=0: return BootResult(False,"Timed out before reserved DTB boot",normal.stdout,normal.stderr,normal.pid)
        reserved=serial_test([*command,"-dtb",str(path)],remaining,heap_exchanges(128,64))
        return BootResult(reserved.success,reserved.reason,normal.stdout+reserved.stdout,normal.stderr+reserved.stderr,reserved.pid)


def fault_report_matcher(kind, symbols):
    """Match a terminal report, checking captured context against the ELF and sentinels."""
    def match(data):
        prefix = b"\r\nmini-os: exception vector=current-spx-sync reason="
        if len(data) < len(prefix) and prefix.startswith(data):
            return None
        if not data.startswith(prefix):
            raise ValueError("Incorrect exception report prefix")
        marker = b"mini-os: halted\r\n"
        end = data.find(marker)
        if end < 0:
            return None
        lines = data[2:end + len(marker)].split(b"\r\n")[:-1]
        def require(condition, detail):
            if not condition:
                raise ValueError(f"Incorrect exception report: {detail}")
        require(len(lines) == 36, "length")
        memory_fault = kind in ("unmapped","readonly")
        reason = "data-abort" if memory_fault else "brk" if kind == "brk" else "unknown"
        require(lines[0] == f"mini-os: exception vector=current-spx-sync reason={reason}".encode(), "vector or reason")
        if memory_fault:
            dfsc = 6 if kind=="unmapped" else 15
            esr = 0x96000000 | dfsc | (64 if kind=="readonly" else 0)
            description = "translation" if kind=="unmapped" else "permission"
            expected = f"esr=0x{esr:016x} ec=0x25 il=1 iss=0x{esr & 0x1ffffff:07x} abort={description} dfsc=0x{dfsc:02x} write={int(kind=='readonly')} far-valid=1"
        else:
            esr = 0xf2000123 if kind == "brk" else 0x02000000
            expected = f"esr=0x{esr:016x} ec=0x{esr >> 26:02x} il=1 iss=0x{esr & 0x1ffffff:07x}"
        require(lines[1] == expected.encode(), "syndrome")
        state = re.fullmatch(rb"elr=0x([0-9a-f]{16}) spsr=0x([0-9a-f]{16})", lines[2])
        require(state is not None, "state formatting")
        elr, spsr = (int(value, 16) for value in state.groups())
        require(elr == symbols[f"mini_os_fault_{kind}_site"], "faulting instruction address")
        require(spsr & 0x1f == 5 and spsr & 0x3c0 == 0x340, "saved execution state or masks")
        stack = re.fullmatch(rb"sp=0x([0-9a-f]{16}) far\(raw\)=0x([0-9a-f]{16})", lines[3])
        require(stack is not None, "SP or raw FAR formatting")
        sp = int(stack[1], 16)
        require(symbols["__stack_bottom"] + 304 <= sp <= symbols["__stack_top"] and sp % 16 == 0, "stack address")
        target = 0x1000 if kind=="unmapped" else symbols.get("mini_os_readonly_probe",0)
        if memory_fault: require(int(stack[2],16)==target, "fault address")
        for index in range(31):
            value = target if memory_fault and index==16 else 0x100+index
            require(lines[index + 4] == f"x{index:02d}=0x{value:016x}".encode(), f"saved x{index}")
        require(lines[-1] == b"mini-os: halted", "halt marker")
        return end + len(marker)
    return match


def fault_exchanges(kind, symbols):
    yield next(uart_exchanges())
    for byte in f"fault {kind}  ".encode():
        yield "fault character", bytes([byte]), bytes([byte])
    yield "fatal exception", b"\n", fault_report_matcher(kind, symbols)


def fault_test(command, timeout=10, symbols=None, kinds=("brk","undef")):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Serial deadline must be positive")
    deadline = time.monotonic() + timeout
    if symbols is None:
        symbols = inspect(Path(command[command.index("-kernel") + 1]), verbose=False)
    output, diagnostics = bytearray(), bytearray()
    last_pid = 0
    for model in ("cortex-a53", "cortex-a57"):
        for kind in kinds:
            name = f"{model}, fault {kind}"
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return BootResult(False, f"Timed out during {name}", bytes(output), bytes(diagnostics), last_pid)
            scenario = list(command)
            scenario[scenario.index("-cpu") + 1] = model
            result = serial_test(scenario, remaining, fault_exchanges(kind, symbols), allow_exception=True)
            output.extend(result.stdout); diagnostics.extend(result.stderr); last_pid = result.pid
            if not result.success:
                return BootResult(False, f"{name}: {result.reason}", bytes(output), bytes(diagnostics), last_pid)
    return BootResult(True, "Fatal vectors, syndromes, instruction addresses, and saved registers verified",
                      bytes(output), bytes(diagnostics), last_pid)


def mmu_exchanges(memory=128):
    yield next(uart_exchanges(memory))
    def report(data):
        if PROMPT not in data: return None
        lines = data.split(b"\r\n")
        if len(lines)!=4 or lines[0]!=b"" or lines[-1]!=PROMPT: raise ValueError("Incorrect MMU report framing")
        match = re.fullmatch(rb"mmu: SCTLR_EL1=0x([0-9a-f]{16}) TCR_EL1=0x([0-9a-f]{16}) TTBR0_EL1=0x([0-9a-f]{16}) MAIR_EL1=0x([0-9a-f]{16}) tables=([0-9]+)",lines[1])
        if match is None: raise ValueError("Incorrect MMU register formatting")
        sctlr,tcr,root,mair = (int(value,16) for value in match.groups()[:4]); tables=int(match[5])
        expected_tcr=(2<<32)|(2<<30)|(1<<23)|(25<<16)|(3<<12)|25
        if not (sctlr&0x1005==1 and tcr==expected_tcr and mair==0x44 and 0x40200000<=root<0x40000000+memory*1024*1024 and root%4096==0 and tables>=3): raise ValueError("Incorrect MMU execution state")
        probes = re.fullmatch(rb"mmu: text=0x0000000040200000 text-write=denied rodata-write=denied dtb-write=denied stack=0x([0-9a-f]{16}) guard=unmapped null=unmapped uart=0x0000000009000000",lines[2])
        if probes is None or not (0x40200000<=int(probes[1],16)<root and int(probes[1],16)%4096==0): raise ValueError("Incorrect MMU identity translations or permissions")
        return len(data)
    yield from command_exchange("MMU translation",b"mmu",report)
    yield from command_exchange("MMU SGI",b"irq test",b"\r\nirq: test OK\r\n"+PROMPT)
    yield from command_exchange("MMU allocation",b"mem test",b"\r\nmem: test OK\r\n"+PROMPT)
    yield from command_exchange("MMU usage",b"mmu x",b"\r\nusage: mmu\r\n"+PROMPT)
    # Reuse the recurring-timer exchange without repeating readiness.
    exchanges=timer_exchanges(memory);next(exchanges)
    yield from exchanges


def mmu_test(command,timeout=10,symbols=None):
    deadline=time.monotonic()+timeout
    scenarios=tuple((model,count,memory) for model,count,_ in CPU_SCENARIOS for memory in (128,256))
    normal=scenario_test(command,timeout,scenarios,mmu_exchanges)
    if not normal.success: return normal
    remaining=deadline-time.monotonic()
    if remaining<=0: return BootResult(False,"Timed out before MMU faults",normal.stdout,normal.stderr,normal.pid)
    faults=fault_test(command,remaining,symbols,("unmapped","readonly"))
    return BootResult(faults.success,faults.reason,normal.stdout+faults.stdout,normal.stderr+faults.stderr,faults.pid)


def qemu_command(image, executable):
    if not image.is_file():
        raise RuntimeError(f"Missing kernel image: {image}; run kernel-build first.")
    resolved = shutil.which(executable)
    if resolved is None:
        raise RuntimeError(f"Missing QEMU tool: {executable}; see README.md for prerequisites.")
    return [resolved, *QEMU_ARGS, "-kernel", str(image.resolve())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("run", "test", "uart-test", "fdt-test", "monitor-test", "fault-test", "irq-test", "timer-test", "memory-test", "mmu-test", "heap-test"))
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--timeout", type=float, default=10)
    args = parser.parse_args()
    command = qemu_command(args.image, args.qemu)
    if args.action == "run":
        process = subprocess.Popen(command)
        try:
            return process.wait()
        except KeyboardInterrupt:
            return 130
        finally:
            stop_process(process)
    runner = {"test": boot_test, "uart-test": uart_test, "fdt-test": fdt_test, "monitor-test": monitor_test, "fault-test": fault_test, "irq-test": irq_test, "timer-test": timer_test, "memory-test": memory_test, "mmu-test": mmu_test, "heap-test": heap_test}[args.action]
    result = runner(command, args.timeout)
    print(result.stdout.decode(errors="replace"), end="")
    if not result.success:
        print(result.stderr.decode(errors="replace"), end="", file=sys.stderr)
        print(f"Serial test failed: {result.reason}", file=sys.stderr)
        return 1
    print(result.reason)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
