#!/usr/bin/env python3
"""Prove the fifth u16 overlaps the next record in a pinned PC FFX save.

The fixture is read only. Mutations below occur in bytearray copies in memory.
This is structural RT0 evidence, not evidence of gameplay or a working mod.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


EXPECTED_SHA256 = "6e2a617b58cc058a72526f20a31bf00b4d79847f7784ce5845935538e77af0b3"
SAVE_SIZE = 0x6900
FILE_HEADER_SIZE = 0x40
EQUIPMENT_PAYLOAD_OFFSET = 0x449C
PLY_SAVE_PAYLOAD_OFFSET = 0x55CC
EQUIPMENT_COUNT = 200
EQUIPMENT_STRIDE = 22
ABILITY_OFFSET = 14
ABILITY_COUNT = 4


def probe(path: Path) -> dict[str, object]:
    source = path.read_bytes()
    source_hash = hashlib.sha256(source).hexdigest()
    if len(source) != SAVE_SIZE or source_hash != EXPECTED_SHA256:
        raise ValueError(f"Unexpected fixture: size={len(source)} sha256={source_hash}")

    first = FILE_HEADER_SIZE + EQUIPMENT_PAYLOAD_OFFSET
    next_record = first + EQUIPMENT_STRIDE
    last_record = first + (EQUIPMENT_COUNT - 1) * EQUIPMENT_STRIDE
    ply_save = FILE_HEADER_SIZE + PLY_SAVE_PAYLOAD_OFFSET
    assert first == 0x44DC
    assert ply_save == 0x560C
    assert first + EQUIPMENT_COUNT * EQUIPMENT_STRIDE == ply_save
    assert first + ABILITY_OFFSET + ABILITY_COUNT * 2 == next_record
    assert last_record + ABILITY_OFFSET + ABILITY_COUNT * 2 == ply_save

    abilities = struct.unpack_from("<4H", source, first + ABILITY_OFFSET)
    assert abilities == (0x8063, 0x8064, 0x802A, 0x8000)
    next_name_before = struct.unpack_from("<H", source, next_record)[0]
    ply_first_before = struct.unpack_from("<H", source, ply_save)[0]

    simulated = bytearray(source)
    struct.pack_into("<H", simulated, first + ABILITY_OFFSET + ABILITY_COUNT * 2, 0x800A)
    changed = [i for i, (old, new) in enumerate(zip(source, simulated)) if old != new]
    assert changed == [next_record, next_record + 1]
    assert struct.unpack_from("<H", simulated, next_record)[0] == 0x800A
    assert struct.unpack_from("<4H", simulated, first + ABILITY_OFFSET) == abilities

    simulated_last = bytearray(source)
    struct.pack_into("<H", simulated_last, last_record + ABILITY_OFFSET + ABILITY_COUNT * 2, 0x800A)
    assert struct.unpack_from("<H", simulated_last, ply_save)[0] == 0x800A
    assert hashlib.sha256(path.read_bytes()).hexdigest() == source_hash

    return {
        "verdict": "PASS_RT0_STRUCTURAL_OVERLAP",
        "fixture_sha256": source_hash,
        "file_header_bytes": FILE_HEADER_SIZE,
        "equipment_file_range": [first, ply_save],
        "equipment_records": EQUIPMENT_COUNT,
        "equipment_stride_bytes": EQUIPMENT_STRIDE,
        "first_four_abilities": [f"0x{value:04X}" for value in abilities],
        "first_fifth_offset": next_record,
        "next_name_before": f"0x{next_name_before:04X}",
        "next_name_after_simulation": "0x800A",
        "last_fifth_offset": ply_save,
        "ply_save_first_word_before": f"0x{ply_first_before:04X}",
        "ply_save_first_word_after_simulation": "0x800A",
        "source_fixture_unchanged": True,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path, help="Local genuine PC save, excluded from Git")
    args = parser.parse_args()
    print(json.dumps(probe(args.fixture), indent=2))
