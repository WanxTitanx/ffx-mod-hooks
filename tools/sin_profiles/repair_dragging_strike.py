"""Restore both native Delay Attack phases through Dragging Strike-owned assets."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from repair_mana_storm import row_offset

ROW = 309
EXPECTED = (805, 805, 3)
CORRECTED = (805, 876, 23)


def repair(data):
    start = row_offset(data, ROW)
    current = (*struct.unpack_from('<hh', data, start + 16), data[start + 21])
    if current == CORRECTED:
        return bytes(data)
    if current != EXPECTED:
        raise ValueError('Dragging Strike visual contract changed; review before replacing it')
    result = bytearray(data)
    # Clone805 is donor157's cast phase, not donor158's impact phase. Clone876
    # supplies the separately owned impact; caster23 matches the complete donor.
    struct.pack_into('<hh', result, start + 16, *CORRECTED[:2])
    result[start + 21] = CORRECTED[2]
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Use a private output; install together with the owned876 impact assets')
    before = args.input.read_bytes()
    after = repair(before)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(after)
    print(json.dumps({'row': ROW, 'changed_bytes': sum(a != b for a, b in zip(before, after)),
        'sha256': hashlib.sha256(after).hexdigest()}))


if __name__ == '__main__':
    main()
