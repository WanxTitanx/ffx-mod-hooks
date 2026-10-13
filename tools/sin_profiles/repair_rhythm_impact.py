"""Repair the animation contract of the authored Rhythm Impact row, not its AI."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
from repair_mana_storm import row_offset

ROW = 315
EXPECTED = (811, 811, 3)
CORRECTED = (811, 811, 1)
PREVIOUS_FALLBACK = (0, 0, 1)


def contract(data, start):
    return (*struct.unpack_from('<hh', data, start + 16), data[start + 21])


def repair(data):
    start = row_offset(data, ROW)
    current = contract(data, start)
    if current == CORRECTED:
        return bytes(data)
    if current not in (EXPECTED, PREVIOUS_FALLBACK):
        raise ValueError('Rhythm Impact animation identity changed; review before replacing it')
    # The user requested the authored 811 overlay back. Isolate the caster
    # change: retain physical cast 1 and restore only the previous visual pair.
    result = bytearray(data)
    struct.pack_into('<hh', result, start + 16, *CORRECTED[:2])
    result[start + 21] = CORRECTED[2]
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
    start = row_offset(after, ROW)
    print(json.dumps({'row': ROW, 'before': contract(before, start),
        'after': contract(after, start), 'changed_bytes': sum(a != b for a, b in zip(before, after)),
        'sha256': hashlib.sha256(after).hexdigest()}))


if __name__ == '__main__':
    main()
