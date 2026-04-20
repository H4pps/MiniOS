#!/usr/bin/env python3
"""Shared QEMU configuration and bounded serial boot verification."""

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


def boot_test(command, timeout=10):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Boot deadline must be positive")
    output = {"stdout": bytearray(), "stderr": bytearray()}
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    success = False
    reason = "Timed out waiting for serial confirmation"
    deadline = time.monotonic() + timeout
    try:
        with selectors.DefaultSelector() as selector:
            for name in output:
                stream = getattr(process, name)
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, selectors.EVENT_READ, name)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                for key, _ in selector.select(min(remaining, 0.1)):
                    chunk = os.read(key.fd, 65536)
                    if chunk:
                        output[key.data].extend(chunk)
                    else:
                        selector.unregister(key.fileobj)
                serial = bytes(output["stdout"])
                if FAILURE in serial:
                    reason = "Kernel reported boot failure"
                    break
                # Require a complete exact line; a partial line is not confirmation.
                lines = serial.replace(b"\r\n", b"\n").split(b"\n")[:-1]
                if SUCCESS in lines:
                    # Give a process printing a final line before exiting time to
                    # exit; successful kernels stay in their halt loop.
                    try:
                        process.wait(timeout=0.05)
                        reason = f"Emulator exited prematurely ({process.returncode})"
                    except subprocess.TimeoutExpired:
                        success = True
                        reason = "Serial boot confirmation received"
                    break
                if process.poll() is not None and not selector.get_map():
                    reason = f"Emulator exited prematurely ({process.returncode})"
                    break
    finally:
        stop_process(process)
        # Drain diagnostics written during termination before closing the pipes.
        for name in output:
            stream = getattr(process, name)
            os.set_blocking(stream.fileno(), True)
            output[name].extend(stream.read())
            stream.close()
    return BootResult(success, reason, bytes(output["stdout"]), bytes(output["stderr"]), process.pid)


def qemu_command(image, executable):
    if not image.is_file():
        raise RuntimeError(f"Missing kernel image: {image}; run kernel-build first.")
    resolved = shutil.which(executable)
    if resolved is None:
        raise RuntimeError(f"Missing QEMU tool: {executable}; see README.md for prerequisites.")
    return [resolved, *QEMU_ARGS, "-kernel", str(image.resolve())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("run", "test"))
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
    result = boot_test(command, args.timeout)
    print(result.stdout.decode(errors="replace"), end="")
    if not result.success:
        print(result.stderr.decode(errors="replace"), end="", file=sys.stderr)
        print(f"Boot test failed: {result.reason}", file=sys.stderr)
        return 1
    print(result.reason)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
