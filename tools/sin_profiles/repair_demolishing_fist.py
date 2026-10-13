"""Preserve the authored Demolishing Fist overlay and correct its caster mode."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from repair_mana_storm import row_offset

ROW = 318


def repair(data):
    start = row_offset(data, ROW)
    pair = struct.unpack_from('<hh', data, start + 16)
    if pair != (814, 814) or data[start + 21] not in (3, 10):
        raise ValueError('Demolishing Fist visual contract changed; review before replacing it')
    result = bytearray(data)
    # Native Blast Punch uses the same donor186 for both phases with caster10.
    # Keep the authored clone and every damage/status/target byte unchanged.
    result[start + 21] = 10
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Use a private output; installation requires separate backup/readback')
    before = args.input.read_bytes()
    after = repair(before)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(after)
    print(json.dumps({'row': ROW, 'changed_bytes': sum(a != b for a, b in zip(before, after)),
        'sha256': hashlib.sha256(after).hexdigest()}))


if __name__ == '__main__':
    main()
