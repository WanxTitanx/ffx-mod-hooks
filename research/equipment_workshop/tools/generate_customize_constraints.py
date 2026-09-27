#!/usr/bin/env python3
"""Extract the native Customize group/level/variant facts from an input-only kernel."""
import argparse
import hashlib
from pathlib import Path
import struct

EXPECTED = 'd0610e7d37cde6e65298da116f6dc05a69236126d9ff157c2db65d17a97729aa'

def render(data: bytes) -> str:
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('Unreviewed ability kernel; verify its Customize semantics first')
    last, stride, length, offset = struct.unpack_from('<HHHI', data, 10)
    if last != 133 or stride != 108 or offset != 20 or length != 134 * 108:
        raise ValueError('Unexpected native ability table layout')
    lines = ['#pragma once', '// Native Customize reads these BYTE fields at +69/+6A/+6B.',
             '// Generated from the reviewed private kernel; no game payload is embedded.',
             'namespace workshop {', 'struct CustomizeRule { unsigned char group, level, variant; };',
             f'inline constexpr const char* CustomizeRuleSourceSha256="{EXPECTED}";',
             'inline constexpr CustomizeRule CustomizeRules[131]={']
    for index in range(131):
        group, level, variant = data[offset + index * stride + 105:offset + (index + 1) * stride]
        lines.append(f'    {{{group},{level},{variant}}}, // {index}')
    return '\n'.join(lines + ['};', '}', ''])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'include/customize_constraints.h')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    text = render(args.kernel.read_bytes())
    if args.check:
        if args.output.read_text() != text:
            raise SystemExit('Customize constraint facts differ from the reviewed kernel')
    else:
        args.output.write_text(text)
    print('CUSTOMIZE_CONSTRAINTS_OK 131 rules ' + EXPECTED)

if __name__ == '__main__':
    main()
