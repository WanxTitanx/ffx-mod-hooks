#!/usr/bin/env python3
"""Read-only FFX recovery evidence. Never loads, executes or modifies the game.

Requires pefile and capstone. Preferred-image VAs are accepted on the command
line; all output also identifies RVAs. Raw call/pattern matches are candidates,
not an assertion that an instruction boundary or calling convention is proved.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_SHA256 = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"


def number(value: str) -> int:
    result = int(value, 0)
    if not 0 <= result <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("value must fit an unsigned DWORD")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--slice", action="append", default=[], metavar="VA:SIZE")
    parser.add_argument("--calls-to", action="append", type=number, default=[])
    parser.add_argument("--pattern", action="append", default=[], metavar="HEX")
    parser.add_argument("--disasm", action="store_true")
    args = parser.parse_args()
    try:
        import pefile
        import capstone
        raw = args.executable.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        if digest != EXPECTED_SHA256:
            raise ValueError(f"unsupported executable SHA-256: {digest}")
        pe = pefile.PE(data=raw, fast_load=True)
        base = pe.OPTIONAL_HEADER.ImageBase
        image_size = pe.OPTIONAL_HEADER.SizeOfImage
        if (pe.FILE_HEADER.Machine, pe.OPTIONAL_HEADER.Magic, base, image_size) != (
            0x14C, 0x10B, 0x400000, 0x237D000
        ):
            raise ValueError("unexpected executable profile")
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        result = {"sha256": digest, "preferred_base": hex(base),
                  "image_size": hex(image_size), "slices": [], "candidates": []}
        for spec in args.slice:
            va_text, size_text = spec.split(":", 1)
            va, size = number(va_text), number(size_text)
            if not 0 < size <= 0x20000 or not base <= va < base + image_size:
                raise ValueError(f"invalid bounded slice: {spec}")
            if size > base + image_size - va:
                raise ValueError(f"slice exceeds image: {spec}")
            data = pe.get_data(va - base, size)
            if len(data) != size:
                raise ValueError(f"slice is not entirely disk backed: {spec}")
            item = {"va": hex(va), "rva": hex(va-base), "size": size,
                    "sha256": hashlib.sha256(data).hexdigest()}
            if args.disasm:
                item["instructions"] = [
                    f"{i.address:08X} {i.bytes.hex():24s} {i.mnemonic} {i.op_str}"
                    for i in md.disasm(data, va)
                ]
            else:
                item["bytes"] = data.hex()
            result["slices"].append(item)
        patterns = [(p, bytes.fromhex(p)) for p in args.pattern]
        if any(not p for _, p in patterns):
            raise ValueError("empty search pattern")
        for section in pe.sections:
            if not section.Characteristics & 0x20000000:
                continue
            data = section.get_data()
            origin = base + section.VirtualAddress
            for target in args.calls_to:
                cursor = 0
                while True:
                    at = data.find(b"\xe8", cursor)
                    if at < 0 or at + 5 > len(data):
                        break
                    displacement = struct.unpack_from("<i", data, at + 1)[0]
                    if (origin + at + 5 + displacement) & 0xFFFFFFFF == target:
                        result["candidates"].append({"kind": "raw_rel32_call",
                            "va": hex(origin+at), "rva": hex(origin+at-base),
                            "target": hex(target), "instruction_boundary_proved": False})
                    cursor = at + 1
            for text, pattern in patterns:
                cursor = 0
                while True:
                    at = data.find(pattern, cursor)
                    if at < 0:
                        break
                    result["candidates"].append({"kind": "raw_pattern", "pattern": text,
                        "va": hex(origin+at), "rva": hex(origin+at-base),
                        "instruction_boundary_proved": False})
                    cursor = at + 1
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, ImportError) as exc:
        parser.exit(2, f"native_probe: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
