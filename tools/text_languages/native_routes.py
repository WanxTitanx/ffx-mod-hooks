#!/usr/bin/env python3
"""Read-only, fingerprinted MOD-006 native file-route inspection. Jarvis-HOOK."""
import argparse
import hashlib
import struct
from pathlib import Path

from probe import EXE_SHA


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--rva', type=lambda value: int(value, 0))
    parser.add_argument('--length', type=lambda value: int(value, 0), default=256)
    parser.add_argument('--device-refs', action='store_true')
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXE_SHA:
        parser.error('Executable fingerprint mismatch')
    import pefile
    import capstone
    pe = pefile.PE(data=raw)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    ranges = []
    if args.rva is not None:
        if not 1 <= args.length <= 8192:
            parser.error('Length must be 1..8192 bytes')
        ranges.append((args.rva, args.length))
    if args.device_refs:
        seen = set()
        for target in range(0x2310C40, 0x2310C70, 4):
            pattern = struct.pack('<I', target)
            for section in pe.sections:
                if not section.Characteristics & 0x20:
                    continue
                code = section.get_data()
                at = 0
                while True:
                    hit = code.find(pattern, at)
                    if hit < 0:
                        break
                    at = hit + 4
                    begin = code.rfind(b'\x55\x8b\xec', max(0, hit-80), hit)
                    if begin < 0:
                        begin = max(0, hit-6)
                    rva = section.VirtualAddress + begin
                    if rva not in seen:
                        seen.add(rva)
                        ranges.append((rva, hit-begin+64))
    for rva, size in ranges:
        print(f'RVA 0x{rva:X}, preferred VA 0x{rva+0x400000:X}')
        for instruction in decoder.disasm(pe.get_data(rva, size), rva+0x400000):
            if instruction.mnemonic == 'int3':
                continue
            print(f'{instruction.address:08x} {instruction.bytes.hex():24s} '
                  f'{instruction.mnemonic} {instruction.op_str}')
            for operand in instruction.operands:
                if operand.type == capstone.x86.X86_OP_IMM and 0xB00000 <= operand.imm < 0xC60000:
                    value = pe.get_data(operand.imm-0x400000, 160).split(b'\0')[0]
                    if value and all(32 <= c < 127 for c in value):
                        print('  literal:', value.decode('ascii'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
