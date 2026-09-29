#!/usr/bin/env python3
"""Jarvis-HOOK: bounded, idempotent custom-Nul command authoring; no live writes."""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path

@dataclass(frozen=True)
class Spell:
    id: int
    name: str
    element: str
    animation: int
    aliases: tuple[str, ...]

SPELLS = (
    Spell(320, 'NulHoly', 'Holy', 870, ('NulHoly', 'Radiant Ward')),
    Spell(321, 'NulShadow', 'Darkness', 871, ('NulShadow', 'NulDark', 'Umbral Ward')),
    Spell(370, 'NulEarth', 'Earth', 872, ('NulEarth',)),
    Spell(371, 'NulWind', 'Wind', 873, ('NulWind', 'NulAero')),
    Spell(372, 'NulPoison', 'Poison', 874, ('NulPoison', 'NulBio')),
    Spell(373, 'NulGravity', 'Gravity', 875, ('NulGravity',)),
)
WIDTH = 96
DECODE = {**{n: chr(n-15) for n in range(80,106)}, **{n: chr(n-15) for n in range(112,138)},
          **{n: chr(n) for n in range(48,58)}, 58:' ', 59:'!', 63:'%', 66:'(', 67:')', 70:',', 71:'-', 72:'.', 74:':', 75:';'}
ENCODE = {v:k for k,v in DECODE.items()}

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

class Bank:
    def __init__(self, data: bytes):
        if len(data)<20 or struct.unpack_from('<H',data)[0]!=1:
            raise ValueError('Only the reviewed single-section command bank is supported')
        first,last,width,size,offset = struct.unpack_from('<HHHHI',data,8)
        if first or width!=WIDTH or offset!=20 or size!=(last+1)*WIDTH or 20+size>=len(data):
            raise ValueError('Invalid command-bank layout or bounds')
        self.data=data;self.count=last+1
        self.rows=[data[20+n*WIDTH:20+(n+1)*WIDTH] for n in range(self.count)]
        self.pool=data[20+size:]
        for row in self.rows:
            for at in (0,4,8,12):self.script(struct.unpack_from('<H',row,at)[0])

    def script(self, offset: int) -> bytes:
        if offset>=len(self.pool):raise ValueError('Text pointer outside the pool')
        end=self.pool.find(b'\0',offset)
        if end<0:raise ValueError('Unterminated text script')
        return self.pool[offset:end]

    def name(self, index: int) -> str:
        return ''.join(DECODE.get(b,f'<{b:02X}>') for b in self.script(struct.unpack_from('<H',self.rows[index])[0]))

def inspect(data: bytes) -> dict:
    bank=Bank(data)
    found={s.name:[n for n in range(bank.count) if bank.name(n).casefold() in {a.casefold() for a in s.aliases}] for s in SPELLS}
    return {'sha256':digest(data),'bytes':len(data),'rows':bank.count,'existing':found}

def encode(text: str) -> bytes:
    try:return bytes(ENCODE[c] for c in text)+b'\0'
    except KeyError as error:raise ValueError('Recipe text requires a reviewed US glyph') from error

def build(data: bytes, caster: str='yuna') -> tuple[bytes,dict]:
    if caster not in ('yuna','all'):raise ValueError('Unknown caster scope')
    bank=Bank(data);audit=inspect(data)
    if bank.count not in (370,374):raise ValueError('Unexpected row count; inspect occupied IDs before authoring')
    for spell in SPELLS:
        found=audit['existing'][spell.name]
        if found and found!=[spell.id]:raise ValueError(f'Duplicate or displaced {spell.name}: {found}')
        if spell.id<bank.count and not found:raise ValueError(f'ID {spell.id} belongs to another command')
    owner,submenu=(1,4) if caster=='yuna' else (255,2)
    if bank.count==374:
        for spell in SPELLS:
            row=bank.rows[spell.id]
            if bank.name(spell.id)!=spell.name or struct.unpack_from('<hh',row,16)!=(spell.animation,0) or row[25]!=owner or row[24]!=submenu or row[23]!=submenu or row[26]!=5 or row[35] or row[37]!=2 or row[42] or row[43]!=1 or row[45] or any(row[46:92]):
                raise ValueError('Existing Nul differs from this recipe; refuse an implicit overwrite')
        return data,{**audit,'changed':False,'caster':caster}
    donor=bank.rows[320]
    if donor[35] or donor[42] or donor[43]!=1 or donor[37]!=2 or any(donor[46:92]):
        raise ValueError('Existing Ward is not the reviewed harmless status-free donor')
    rows=list(bank.rows);pool=bytearray(bank.pool)
    for i,spell in enumerate(SPELLS):
        row=bytearray(donor)
        description=f'Nullifies one {spell.element}-element attack on the party. Requires Elemental Nuls.'
        for at,value in ((0,spell.name),(8,description)):
            if len(pool)>65535:raise ValueError('Text offsets exceed native u16 storage')
            struct.pack_into('<H',row,at,len(pool));pool.extend(encode(value))
        struct.pack_into('<hh',row,16,spell.animation,0)
        row[23]=submenu;row[24]=submenu;row[25]=owner;row[26]=5;row[45]=0;row[92]=32+i
        if spell.id<len(rows):rows[spell.id]=bytes(row)
        elif spell.id==len(rows):rows.append(bytes(row))
        else:raise ValueError('Noncontiguous append would manufacture unnamed commands')
    header=bytearray(data[:20]);struct.pack_into('<H',header,10,len(rows)-1);struct.pack_into('<H',header,14,len(rows)*WIDTH)
    result=bytes(header)+b''.join(rows)+bytes(pool);reread=Bank(result)
    assert reread.count==374 and reread.pool.startswith(bank.pool)
    assert all(reread.rows[i]==row for i,row in enumerate(bank.rows) if i not in (320,321))
    assert all(reread.name(s.id)==s.name for s in SPELLS)
    return result,{**audit,'changed':True,'caster':caster,'output_sha256':digest(result),'output_rows':374,
                   'changed_existing_ids':[320,321],'appended_ids':[370,371,372,373],
                   'unchanged_other_rows':368,'text_pool_prefix_preserved':True,
                   'spells':[{'id':s.id,'encoded':hex(0x3000+s.id),'name':s.name,'element':s.element,'animation':s.animation} for s in SPELLS]}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path)
    parser.add_argument('--output',type=Path);parser.add_argument('--caster',choices=('yuna','all'),default='yuna')
    args=parser.parse_args();source=args.source.read_bytes()
    if not args.output:print(json.dumps(inspect(source),indent=2));return
    result,receipt=build(source,args.caster)
    args.output.mkdir(parents=True,exist_ok=False)
    (args.output/'command.bin').write_bytes(result);(args.output/'command-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__':main()
