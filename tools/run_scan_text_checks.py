#!/usr/bin/env python3
"""Jarvis-HOOK: execute the production Scan text/outline boundary without FFX."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / "src/runtime/FfxHooksDll"


def function(source, name):
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError("Missing production function: " + name)
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "work/scan-text-rt0")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    source = (DLL / "dllmain.cpp").read_text()
    adapter = function(source, "NativeTextOutline_MenuGuard")
    adapter += "\n" + function((DLL / "hooks/ElementalScanDraw.inl").read_text(), "NumericalText")
    adapter += "\n" + function(source, "ScanPresentationTextReady")
    (args.out / "ScanTextAdapter.inc").write_text(adapter + "\n")
    binary = args.out / "ScanTextRt0"
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", *flags,
                    "-I" + str(args.out), "-I" + str(DLL / "hooks"),
                    str(DLL / "tests/ScanTextRt0.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
