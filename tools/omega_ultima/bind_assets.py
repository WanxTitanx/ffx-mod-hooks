"""Jarvis-HOOK: read-only Omega formation/clone bindings, disabled by default."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def fingerprint(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & ((1 << 64) - 1)
    return f"{value:016x}"


def shape(data, field, index):
    if not 52 <= len(data) <= 65535 or struct.unpack_from('<I', data)[0] != 8 or struct.unpack_from('<I', data, 32)[0] != len(data):
        raise ValueError('Malformed formation extent')
    slots = 12 + struct.unpack_from('<I', data, 12)[0]
    if slots < 36 or slots + 16 > len(data) or not slots + 16 <= struct.unpack_from('<I', data, 16)[0] <= len(data):
        raise ValueError('Invalid formation slot span')
    roster = struct.unpack_from('<8H', data, slots)
    transforming = field == 1076 and 13 <= index <= 16
    count = 6 if transforming else 5
    if any(not 0x1000 < actor < 0x1400 or actor == 0x138E for actor in roster[:count]) or any(actor != 0xFFFF for actor in roster[count:]):
        raise ValueError('Unexpected initial/stored actor count or duplicate clone')
    mask = sum(1 << slot for slot in range(count) if roster[slot] != 0x10D3 and (not transforming or slot not in (1, 2)))
    if transforming and (roster[1] != 0x10D3 or roster[2] != (0x10FB, 0x10FC, 0x10FD, 0x1100)[index-13] or mask != 0x39):
        raise ValueError('Reserved Mimic/chest identity changed')
    if not mask:
        raise ValueError('No active combatant')
    return slots, mask, list(roster)


def bind(battle_root, clone):
    data = clone.read_bytes()
    if not 36 <= len(data) <= 1024*1024:
        raise ValueError('Invalid m910 extent')
    manifest = {'schema': 'ffx.spira.omega-ultima.v1', 'enabled': False,
                'clone': {'id': 910, 'size': len(data), 'hash': fingerprint(data)}, 'formations': []}
    sources = [{'path': str(clone.resolve()), 'sha256': hashlib.sha256(data).hexdigest()}]
    for field in (1076, 1077):
        for index in range(19 if field == 1076 else 9):
            if index == 10:
                continue
            name = f'omeg{field-1076:02}_{index:02}'
            path = battle_root / 'btl' / name / (name + '.bin')
            data = path.read_bytes()
            slots, mask, roster = shape(data, field, index)
            manifest['formations'].append({'field': field, 'index': index, 'size': len(data), 'hash': fingerprint(data)})
            sources.append({'path': str(path.resolve()), 'sha256': hashlib.sha256(data).hexdigest(),
                            'slots_offset': slots, 'eligible_mask': mask, 'native_roster': roster})
    return manifest, sources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--battle-root', type=Path, required=True)
    parser.add_argument('--clone', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest, sources = bind(args.battle_root, args.clone)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + '\n')
    args.output.with_suffix('.sources.json').write_text(json.dumps(sources, indent=2) + '\n')
    print('Bound m910 and 27 normal Omega formations; feature remains OFF.')


if __name__ == '__main__':
    main()
