#!/usr/bin/env python3
"""Jarvis-HOOK: inspect exact native spans without loading or running the game."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import capstone
import pefile

EXE_SHA256 = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--span", action="append", required=True, help="RVA:size, both hexadecimal")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--calls-only", action="store_true")
    args = parser.parse_args()
    if args.exe.resolve() == args.output.resolve():
        parser.error("The report cannot replace the input executable")
    raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXE_SHA256:
        parser.error("Unsupported executable identity; no spans inspected")
    pe = pefile.PE(data=raw)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.ImageBase != 0x400000:
        parser.error("Unsupported native ABI")
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    spans = []
    for argument in args.span:
        try:
            rva, size = (int(value, 16) for value in argument.split(":"))
        except ValueError:
            parser.error("Every span must have exactly two hexadecimal integers")
        if not 0 <= rva < pe.OPTIONAL_HEADER.SizeOfImage or not 0 < size <= 0x10000:
            parser.error("Span is outside the bounded inspection range")
        code = pe.get_data(rva, size)
        if len(code) != size:
            parser.error("The requested span is not backed by complete file bytes")
        instructions = [{"rva": hex(i.address), "bytes": i.bytes.hex(),
                         "instruction": i.mnemonic + " " + i.op_str}
                        for i in decoder.disasm(code, rva)]
        spans.append({"rva": hex(rva), "size": size,
                      "sha256": hashlib.sha256(code).hexdigest(),
                      "decoded_bytes": sum(len(i["bytes"]) // 2 for i in instructions),
                      "instructions": instructions})
    report = {"producer": "Jarvis-HOOK", "level": "RT0-static-native-bytes",
              "exe_sha256": EXE_SHA256, "preferred_base": "0x400000",
              "width": "x86/32-bit", "spans": spans,
              "limitations": ["No instruction was executed", "A span is not a proved function boundary",
                              "Addresses use RVA form; absolute operands still use the preferred image base"]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for span in spans:
        print(span["rva"], "size", span["size"], "sha256", span["sha256"])
        for instruction in span["instructions"]:
            if not args.calls_only or instruction["instruction"].startswith("call "):
                print(instruction["rva"], instruction["bytes"], instruction["instruction"])


if __name__ == "__main__":
    main()
