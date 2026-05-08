#!/usr/bin/env python3
"""Run LLVM checks only on project files, excluding downloaded dependencies."""

import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
EXTENSIONS = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp"}


def tool(name):
    executable = shutil.which(name)

    if executable is None:
        raise RuntimeError(f"Missing {name}; see README.md for LLVM prerequisites.")

    return executable


def main():
    if len(sys.argv) < 2:
        raise RuntimeError("Expected format, format-check, or lint.")

    command = sys.argv[1]

    if command in {"format", "format-check"}:
        files = sorted(
            path
            for folder in ("include", "src", "tests", "tools")
            for path in (ROOT / folder).rglob("*")
            if path.is_file() and path.suffix in EXTENSIONS
        )
        flags = ["-i"] if command == "format" else ["--dry-run", "--Werror"]

        for path in files:
            subprocess.run([tool("clang-format"), *flags, str(path)], check=True, cwd=ROOT)
    elif command == "lint" and len(sys.argv) == 3:
        build_dir = Path(sys.argv[2]).resolve()

        with (build_dir / "compile_commands.json").open() as stream:
            database = json.load(stream)

        sources = set()

        for entry in database:
            path = Path(entry["file"])

            if not path.is_absolute():
                path = Path(entry["directory"]) / path

            path = path.resolve()

            if path.suffix in EXTENSIONS and any(
                folder in path.parents for folder in (ROOT / "src", ROOT / "tests", ROOT / "tools")
            ):
                sources.add(path)

        if not sources:
            raise RuntimeError("No project translation units found in compilation database.")

        for source in sorted(sources):
            subprocess.run(
                [tool("clang-tidy"), "-p", str(build_dir), str(source)], check=True, cwd=ROOT
            )
    else:
        raise RuntimeError("Expected format, format-check, or lint BUILD_DIRECTORY.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
