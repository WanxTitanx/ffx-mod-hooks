"""Jarvis-HOOK: restore the native Thunder cast/impact contract in a private copy."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

ROW = 265
EXPECTED = (862, 863)
NATIVE = (86, 87)

def row_offset(data, index):
    if len(data) < 20:
        raise ValueError('Short command table')
    count = struct.unpack_from('<H', data)[0]
    if not 1 <= count <= 16 or len(data) < 8+12*count:
        raise ValueError('Invalid command segment count')
    matches = []
    for n in range(count):
        first, last, size, _, offset = struct.unpack_from('<HHHHI', data, 8+12*n)
        if first > last or last > 4095 or size != 92 or offset < 8+12*count:
            raise ValueError('Unsupported monster2 command layout')
        if first <= index <= last:
            start = offset+(index-first)*size
            if start+size > len(data):
                raise ValueError('Truncated command record')
            matches.append(start)
    if len(matches) != 1:
        raise ValueError('Command must have exactly one native record')
    return matches[0]

def repair(data):
    start = row_offset(data, ROW)
    pair = struct.unpack_from('<hh', data, start+16)
    if data[start+21] != 3:
        raise ValueError('Mana Storm requires the reviewed native caster animation 3')
    if pair == NATIVE:
        return bytes(data)
    if pair != EXPECTED:
        raise ValueError('Mana Storm animation identity changed; review before replacing it')
    result = bytearray(data)
    struct.pack_into('<hh', result, start+16, *NATIVE)
    assert result[:start+16] == data[:start+16] and result[start+20:] == data[start+20:]
    return bytes(result)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('Write a separate candidate; deployment owns any live replacement')
    before = args.source.read_bytes()
    after = repair(before)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(after)
    receipt = {'producer': 'Jarvis-HOOK', 'source': str(args.source.resolve()),
               'sourceSha256': hashlib.sha256(before).hexdigest(), 'output': str(args.output.resolve()),
               'outputSha256': hashlib.sha256(after).hexdigest(), 'row': ROW,
               'animationBefore': list(struct.unpack_from('<hh', before, row_offset(before, ROW)+16)),
               'animationAfter': list(NATIVE), 'allOtherBytesPreserved': True}
    args.output.with_suffix(args.output.suffix+'.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))

if __name__ == '__main__':
    main()
