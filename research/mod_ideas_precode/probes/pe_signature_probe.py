#!/usr/bin/env python3
"""Read-only PE/RVA identity gate for the FFX.exe copy used by this research."""

from __future__ import annotations

import argparse
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

EXPECTED_SHA256 = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"
EXPECTED_SIZE = 10_675_712
EXPECTED_IMAGEBASE = 0x400000
EXPECTED_MACHINE = 0x014C
EXPECTED_TIMESTAMP = 0x55D2F3CC

# PE RVAs. The IDA flat address for this image is RVA + 0x400000.
SIGNATURES: tuple[tuple[str, int, bytes], ...] = (
    ("damage_formula_dispatch", 0x389CB0, bytes.fromhex("55 8b ec 83 ec 0c 53")),
    ("element_affinity", 0x38A420, bytes.fromhex("55 8b ec 83 ec 20 57")),
    ("hit_accuracy", 0x38A950, bytes.fromhex("55 8b ec 8b 45 10 56")),
    ("od_command_gate", 0x38ABE0, bytes.fromhex("55 8b ec 51 53 8b 5d 08")),
    ("status_matrix", 0x38AEC0, bytes.fromhex("55 8b ec 83 ec 30 8b")),
    ("critical", 0x389750, bytes.fromhex("55 8b ec 57 8b 7d 10")),
    ("bdl_cap_immediate", 0x38ED41, bytes.fromhex("bb 9f 86 01 00")),
    ("damage_clamp", 0x38EDD5, bytes.fromhex("7e 02 8b c3 89")),
    ("od_menu_ready", 0x39AF70, bytes.fromhex("55 8b ec ff 75 08")),
    ("od_add_clamp", 0x3B15A0, bytes.fromhex("55 8b ec 80 3d")),
    ("gear_menu_owner", 0x4D7990, bytes.fromhex("55 8b ec 51 a1")),
)


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int


def parse_pe(data: bytes) -> tuple[int, int, int, list[Section]]:
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError("Missing DOS MZ header")
    pe_at = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_at + 24 > len(data) or data[pe_at : pe_at + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine, count, timestamp, _, _, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", data, pe_at + 4
    )
    optional_at = pe_at + 24
    if optional_at + optional_size > len(data):
        raise ValueError("Truncated optional header")
    magic = struct.unpack_from("<H", data, optional_at)[0]
    if magic != 0x10B:
        raise ValueError(f"Expected PE32, found optional magic 0x{magic:X}")
    imagebase = struct.unpack_from("<I", data, optional_at + 28)[0]
    section_at = optional_at + optional_size
    if count > 96 or section_at + count * 40 > len(data):
        raise ValueError("Invalid section table")
    sections = []
    for index in range(count):
        at = section_at + index * 40
        name = data[at : at + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, at + 8
        )
        sections.append(Section(name, virtual_address, virtual_size, raw_offset, raw_size))
    return machine, timestamp, imagebase, sections


def read_rva(data: bytes, sections: list[Section], rva: int, length: int) -> bytes:
    for section in sections:
        delta = rva - section.virtual_address
        if 0 <= delta and delta + length <= section.raw_size:
            at = section.raw_offset + delta
            if at + length <= len(data):
                return data[at : at + length]
    raise ValueError(f"RVA 0x{rva:X} is not backed by file bytes")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument(
        "--inspect-unknown",
        action="store_true",
        help="Print signatures for another binary, but never give it a PASS verdict",
    )
    args = parser.parse_args()
    if args.exe.stat().st_size > 128 * 1024 * 1024:
        raise ValueError("Input exceeds the bounded 128 MiB probe limit")
    data = args.exe.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    machine, timestamp, imagebase, sections = parse_pe(data)
    identity = (
        sha == EXPECTED_SHA256
        and len(data) == EXPECTED_SIZE
        and machine == EXPECTED_MACHINE
        and timestamp == EXPECTED_TIMESTAMP
        and imagebase == EXPECTED_IMAGEBASE
    )
    print(
        f"identity sha256={sha} size={len(data)} machine=0x{machine:X} "
        f"timestamp=0x{timestamp:X} imagebase=0x{imagebase:X}"
    )
    if not identity and not args.inspect_unknown:
        print("FAIL_TARGET_IDENTITY: signatures were not used to certify this binary")
        return 2
    failed = 0
    for name, rva, expected in SIGNATURES:
        actual = read_rva(data, sections, rva, len(expected))
        match = actual == expected
        failed += not match
        print(
            f"{name} rva=0x{rva:06X} ida_flat=0x{imagebase + rva:08X} "
            f"bytes={actual.hex()} {'MATCH' if match else 'MISMATCH'}"
        )
    if not identity:
        print("UNKNOWN_TARGET: inspection only, no passing identity verdict")
        return 2
    print(f"{'PASS' if failed == 0 else 'FAIL'} signatures={len(SIGNATURES) - failed}/{len(SIGNATURES)}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
