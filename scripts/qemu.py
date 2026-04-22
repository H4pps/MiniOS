#!/usr/bin/env python3
"""Shared QEMU configuration and bounded serial boot and input verification."""

import argparse
import math
from dataclasses import dataclass
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
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
READY = SUCCESS + b"\r\nmini-os: uart ready\r\n" + PROMPT


def uart_exchanges():
    """Send small fresh exchanges, pacing printable input by its immediate echo."""
    yield "readiness", b"", READY
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


def serial_test(command, timeout, exchanges=None):
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
            while time.monotonic() < deadline:
                if interactive and step is not None and sent < len(step[1]):
                    try:
                        sent += os.write(process.stdin.fileno(), step[1][sent:sent + 8])
                    except BlockingIOError:
                        pass
                    except BrokenPipeError:
                        reason = "Emulator closed serial stdin"
                        break
                for key, _ in selector.select(min(0.01, max(0, deadline - time.monotonic()))):
                    chunk = os.read(key.fd, 65536)
                    if chunk:
                        output[key.data].extend(chunk)
                    else:
                        selector.unregister(key.fileobj)
                serial = bytes(output["stdout"])
                if FAILURE in serial:
                    reason = "Kernel reported boot failure"
                    break
                if process.poll() is not None:
                    reason = f"Emulator exited prematurely ({process.returncode})"
                    break
                if interactive:
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
                    complete = SUCCESS in lines
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
                        reason = "Serial UART exchanges verified" if interactive else "Serial boot confirmation received"
                        break
            else:
                if interactive and step is not None:
                    reason = f"Timed out waiting for UART response during {step[0]}"
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


def qemu_command(image, executable):
    if not image.is_file():
        raise RuntimeError(f"Missing kernel image: {image}; run kernel-build first.")
    resolved = shutil.which(executable)
    if resolved is None:
        raise RuntimeError(f"Missing QEMU tool: {executable}; see README.md for prerequisites.")
    return [resolved, *QEMU_ARGS, "-kernel", str(image.resolve())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("run", "test", "uart-test"))
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
    result = (uart_test if args.action == "uart-test" else boot_test)(command, args.timeout)
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
