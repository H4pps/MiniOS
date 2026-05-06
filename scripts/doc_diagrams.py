#!/usr/bin/env python3
"""Render documented Mermaid diagrams, or check committed SVG freshness."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
EXPORTS = DOCS / "diagrams"
BLOCK = re.compile(r"^```mermaid\n(.*?)^```[ \t]*$", re.M | re.S)
NAME = re.compile(r"^%% diagram: ([a-z][a-z0-9-]+)$", re.M)


def diagrams():
    found = {}
    for document in sorted(DOCS.rglob("*.md")):
        for block in BLOCK.findall(document.read_text()):
            names = NAME.findall(block)
            if len(names) != 1 or names[0] in found:
                raise RuntimeError(f"Missing or duplicate diagram ID in {document.relative_to(ROOT)}")
            source = block.strip() + "\n"
            found[names[0]] = (source, hashlib.sha256(source.encode()).hexdigest())
    if not found:
        raise RuntimeError("No documented Mermaid diagrams found")
    return found


def check(found):
    failures = []
    for name, (_, digest) in found.items():
        output = EXPORTS / f"{name}.svg"
        marker = f"<!-- mini-os-diagram-source: {digest} -->"
        try:
            text = output.read_text()
            root = ET.fromstring(text)
            if marker not in text or root.tag != "{http://www.w3.org/2000/svg}svg":
                failures.append(name)
        except (OSError, ET.ParseError):
            failures.append(name)
    unexpected = {path.stem for path in EXPORTS.glob("*.svg")} - set(found)
    if failures or unexpected:
        raise RuntimeError("Missing/stale/invalid exports: " + ", ".join(failures) +
                           "; unexpected exports: " + ", ".join(sorted(unexpected)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check exports without Mermaid or Chrome")
    parser.add_argument("--mmdc", default="mmdc", help="Optional Mermaid CLI executable for rendering")
    parser.add_argument("--puppeteer-config", type=Path, help="Optional existing-Chrome configuration")
    parser.add_argument("--diagram", action="append", default=[], help="Render only these diagram IDs")
    args = parser.parse_args()
    found = diagrams()
    unknown = set(args.diagram) - set(found)
    if unknown:
        raise RuntimeError("Unknown diagram IDs: " + ", ".join(sorted(unknown)))
    if not args.check:
        executable = shutil.which(args.mmdc)
        if executable is None:
            raise RuntimeError("Missing mmdc; see docs/development.md for optional diagram tools")
        if args.puppeteer_config is not None and not args.puppeteer_config.is_file():
            raise RuntimeError("Missing Puppeteer configuration")
        EXPORTS.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="mini-os-diagrams-") as directory:
            temporary = Path(directory)
            for name, (source, digest) in found.items():
                if args.diagram and name not in args.diagram:
                    continue
                source_path = temporary / f"{name}.mmd"
                source_path.write_text(source)
                config = temporary / f"{name}.json"
                config.write_text(json.dumps({"deterministicIds": True, "deterministicIDSeed": name,
                                             "fontFamily": "Arial", "flowchart": {"htmlLabels": False}}))
                rendered = temporary / f"{name}.svg"
                command = [executable, "--input", str(source_path), "--output", str(rendered),
                           "--configFile", str(config), "--theme", "neutral",
                           "--backgroundColor", "white", "--width", "1400"]
                if args.puppeteer_config is not None:
                    command += ["--puppeteerConfigFile", str(args.puppeteer_config.resolve())]
                subprocess.run(command, check=True)
                text = rendered.read_text()
                marker = f"<!-- mini-os-diagram-source: {digest} -->"
                if text.startswith("<?xml"):
                    end = text.index("?>") + 2
                    text = text[:end] + "\n" + marker + text[end:]
                else:
                    text = marker + "\n" + text
                (EXPORTS / f"{name}.svg").write_text(text)
    check(found)
    print(f"Verified {len(found)} diagram exports")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
