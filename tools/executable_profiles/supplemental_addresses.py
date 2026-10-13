#!/usr/bin/env python3
"""Merge reviewed supplemental RVA proofs into the existing correspondence table.

This preserves all existing correspondences and refuses conflicting evidence.
The private full-map generators can use merge(); --check verifies their output.
"""
from pathlib import Path
import argparse
import json
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TABLE = ROOT / 'src/runtime/FfxHooksDll/shared/Steam20261001Addresses.generated.inc'


def merge(pairs, proof_path=HERE / 'supplemental_addresses.json'):
    result = dict(pairs)
    for proof in json.loads(proof_path.read_text()):
        old, new = proof['legacy_rva'], proof['updated_rva']
        if type(old) is not int or type(new) is not int or not (0 < old < 0x237D000 and 0 < new < 0x237D000):
            raise ValueError('Invalid reviewed RVA pair')
        if old in result and result[old] != new:
            raise ValueError(f'Conflicting correspondence at {old:#x}')
        result[old] = new
    return result


def render(source, proof_path=HERE / 'supplemental_addresses.json'):
    marker = 'inline constexpr AddressPair SteamAddresses[] = {'
    start = source.index(marker)
    end = source.index('};', start) + 2
    body = source[start:end]
    rows = re.findall(r'\{0x([0-9A-Fa-f]+)u, 0x([0-9A-Fa-f]+)u\}', body)
    pairs = {int(a, 16): int(b, 16) for a, b in rows}
    if len(pairs) != len(rows):
        raise ValueError('Duplicate legacy correspondence')
    revised = marker + '\n' + ''.join(f'    {{0x{a:08X}u, 0x{b:08X}u}},\n' for a, b in sorted(merge(pairs, proof_path).items())) + '};'
    return source[:start] + revised + source[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--table', type=Path, default=TABLE)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path, help='Write regenerated output to a new file')
    args = parser.parse_args()
    source = args.table.read_text()
    revised = render(source)
    if args.output:
        with args.output.open('x') as stream:
            stream.write(revised)
    elif revised != source:
        parser.exit(1, 'Supplemental proofs are missing; regenerate with --output and review the result.\n')
    print('Supplemental address proofs: PASS')


if __name__ == '__main__':
    main()
