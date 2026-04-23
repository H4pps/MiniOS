#!/usr/bin/env python3
"""Shared QEMU configuration and bounded serial boot and input verification."""

import argparse
import json
import math
from dataclasses import dataclass
import os
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time

SUCCESS = b"mini-os: boot OK"
FAILURE = b"mini-os: boot FAIL:"
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
        (b"first\n", b"first"),
        (b"first\n", b"first"),
        (b"ab\x08c\x7fd\r\n", b"ad"),
        (b"cr\r", b"cr"),
        (b"lf\n", b"lf"),
        (b"\x08\x7f\n", b""),
        (b"\r\n", b""),
        (b"\x00\x01\x1b\t\x80\xffok\n", b"ok"),
        (b"x" * 127 + b"\n", b"x" * 127),
        (b"x" * 128 + b"discard\x08\x7f\r\n", None),
        (b"recovered\n", b"recovered"),
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


def serial_test(command, timeout, exchanges=None, expected_failure=None, qmp_path=None):
    """One deadline and continuously drained pipes for boot and UART protocols."""
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Serial deadline must be positive")
    output = {"stdout": bytearray(), "stderr": bytearray()}
    interactive = exchanges is not None
    steps = iter(exchanges) if interactive else None
    step = next(steps, None) if interactive else None
    process = subprocess.Popen(command, stdin=subprocess.PIPE if interactive else None,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    success = False
    reason = "Timed out waiting for serial confirmation"
    deadline = time.monotonic() + timeout
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
                        fresh = serial[cursor:cursor + len(expected)]
                        if not expected.startswith(fresh):
                            reason = f"Incorrect UART response during {name}: expected {expected!r}, received {fresh!r}"
                            break
                        if sent == len(payload) and len(fresh) == len(expected):
                            cursor += len(expected)
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


def qemu_command(image, executable):
    if not image.is_file():
        raise RuntimeError(f"Missing kernel image: {image}; run kernel-build first.")
    resolved = shutil.which(executable)
    if resolved is None:
        raise RuntimeError(f"Missing QEMU tool: {executable}; see README.md for prerequisites.")
    return [resolved, *QEMU_ARGS, "-kernel", str(image.resolve())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("run", "test", "uart-test", "fdt-test"))
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
    runner = {"test": boot_test, "uart-test": uart_test, "fdt-test": fdt_test}[args.action]
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
