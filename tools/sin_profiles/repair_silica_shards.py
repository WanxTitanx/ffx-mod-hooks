"""Remove the unintended donor Slow from the authored UNI-014 command."""
import argparse
import hashlib
import json
from pathlib import Path
from repair_mana_storm import row_offset

ROW = 303


def repair(data):
    start = row_offset(data, ROW)
    current = (data[start + 0x46], data[start + 0x53])
    if current == (0, 0):
        return bytes(data)
    if current != (100, 5):
        raise ValueError('Silica Shards Slow fields changed; review before replacing them')
    result = bytearray(data)
    result[start + 0x46] = result[start + 0x53] = 0
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
