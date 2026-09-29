#!/usr/bin/env python3
"""Generate exact-profile Overdrive relocation descriptors; never edit the EXE."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess

SHA = '78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced'
SPECS = [('Counter',0x7b10d0,0xbb,0x18,6,7),('Turn',0x7b13d0,0x174,0xa,6,7),
         ('Damage',0x7b0d60,0x226,0x112,7,8),('Death',0x7b0f90,0x108,0x6f,7,8),
         ('Win',0x7b1550,0x4a,0x41,7,8)]


def generate(src):
    raw = src.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        raise ValueError('Unsupported executable')
    pe = struct.unpack_from('<I',raw,0x3c)[0]
    sections = struct.unpack_from('<H',raw,pe+6)[0]
    opt = pe+24
    sec = opt+struct.unpack_from('<H',raw,pe+20)[0]
    base = struct.unpack_from('<I',raw,opt+28)[0]
    image = bytearray(struct.unpack_from('<I',raw,opt+56)[0])
    size = struct.unpack_from('<I',raw,opt+60)[0]
    image[:size] = raw[:size]
    for i in range(sections):
        _,rva,rs,rp = struct.unpack_from('<IIII',raw,sec+40*i+8)
        image[rva:rva+rs] = raw[rp:rp+rs]
    reloc,size = struct.unpack_from('<II',raw,opt+96+5*8)
    end = reloc+size
    absolute = []
    while reloc < end:
        page,bs = struct.unpack_from('<II',image,reloc)
        if bs < 8 or bs % 2 or reloc+bs > end:
            raise ValueError('Invalid relocation block')
        for j in range(reloc+8,reloc+bs,2):
            word = struct.unpack_from('<H',image,j)[0]
            if word >> 12 == 3:
                absolute.append(page+(word & 4095))
            elif word >> 12 != 0:
                raise ValueError('Unexpected relocation type')
        reloc += bs
    out = ['#pragma once','#include <cstddef>','#include <cstdint>',
           '// Exact FFX.exe SHA256'+SHA+'.',
           '// Generated from original PE relocations and instruction-aligned call sites.',
           'namespace FfxHooks::SeymourOverdrive {',
           'inline constexpr std::uint32_t CounterRva=0x3b10d0, GaugeRva=0x3b15a0;',
           'enum Function : unsigned { Counter, Turn, Damage, Death, Win, FunctionCount };',
           'struct CallSite {std::uint16_t offset;std::uint32_t targetRva;};',
           'struct FunctionSpec {std::uint32_t rva;std::uint16_t size,boundOffset;std::uint8_t oldBound,extendedBound;const std::uint8_t* bytes;const std::uint16_t* absolutes;unsigned absoluteCount;const CallSite* calls;unsigned callCount;};']
    rows,evidence = [],{}
    for name,va,length,bound,old,new in SPECS:
        rva = va-base
        data = image[rva:rva+length]
        if data[bound] != old:
            raise ValueError('Native bound mismatch')
        txt = subprocess.check_output(['objdump','-D','-M','intel',f'--start-address={va}',
            f'--stop-address={va+length}',str(src)],text=True,timeout=20)
        calls = []
        for line in txt.splitlines():
            m = re.search(r'^\s*([a-f0-9]+):\s+((?:[a-f0-9]{2} )+)\s*([a-z]+)\s*(.*)',line)
            if not m:
                continue
            pc,ins,arg = int(m[1],16),m[3],m[4]
            if ins == 'call':
                target = int(arg,16)
                if data[pc-va] != 0xe8:
                    raise ValueError('Unmodelled indirect call')
                calls.append((pc-va,target-base))
            elif ins.startswith('j'):
                target = int(arg,16)
                if not va <= target < va+length:
                    raise ValueError('Unmodelled external branch')
        abses = [a-rva for a in absolute if rva <= a < rva+length]
        out.append('inline constexpr std::uint8_t '+name+'Bytes[]={')
        out.extend('    '+','.join(f'0x{x:02x}' for x in data[i:i+16])+',' for i in range(0,len(data),16))
        out.append('};')
        out.append('inline constexpr std::uint16_t '+name+'Absolute[]={'+','.join(hex(x) for x in abses or [0])+'};')
        out.append('inline constexpr CallSite '+name+'Calls[]={'+','.join('{'+hex(a)+','+hex(b)+'}' for a,b in calls)+'};')
        rows.append('{'+f'0x{rva:x},0x{length:x},0x{bound:x},{old},{new},{name}Bytes,{name}Absolute,{len(abses)},{name}Calls,{len(calls)}'+'}')
        evidence[name] = {'va':hex(va),'bytes':length,'sha256':hashlib.sha256(data).hexdigest(),
            'bound_instruction':hex(va+bound-2),'old':old,'new':new,'absolute_offsets':abses,'calls':calls}
    out.append('inline constexpr FunctionSpec Functions[]={'+',\n'.join(rows)+'};')
    out.append('} // namespace FfxHooks::SeymourOverdrive\n')
    return '\n'.join(out),evidence


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('executable',type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--evidence',type=Path)
    p.add_argument('--check',action='store_true')
    a = p.parse_args()
    text,evidence = generate(a.executable)
    if a.check:
        if a.output.read_text() != text:
            raise SystemExit('Generated evidence differs')
    else:
        with a.output.open('x') as stream:
            stream.write(text)
    if a.evidence:
        with a.evidence.open('x') as stream:
            json.dump(evidence,stream,indent=2)
            stream.write('\n')
    print('EXACT_SEYMOUR_OVERDRIVE_DESCRIPTORS',len(evidence))
