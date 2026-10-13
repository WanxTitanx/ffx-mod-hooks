"""Jarvis-HOOK: compile read-only DATA v2 identity witnesses; never edit DATA."""
import argparse, hashlib, json, struct
from pathlib import Path
EXPECTED='63cd28a035b1ff189ab971b85f7486440b9cf0b8d73247b59d7520d5dbae6460'
IDS=(176,181,182,183,184,185,186)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('manifest',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    raw=a.manifest.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXPECTED:raise ValueError('Final DATA v2 manifest required')
    doc=json.loads(raw);names=set();count=0
    for item in doc['files']:
        if not item['relative'].endswith('a_ability.bin'):continue
        b=Path(item['candidate']).read_bytes()
        if hashlib.sha256(b).hexdigest()!=item['afterSha256']:raise ValueError('Candidate drift')
        u16=lambda off:struct.unpack_from('<H',b,off)[0]
        if (u16(0),u16(8),u16(10),u16(12),u16(14),struct.unpack_from('<I',b,16)[0])!=(1,0,186,108,187*108,20):raise ValueError('Unexpected table')
        pool=20+187*108
        for id in IDS:
            row=b[20+108*id:20+108*(id+1)];expected=bytearray(108);expected[0x66]=4;expected[0x68]=20;expected[0x69]=35
            if row[16:]!=expected[16:]:raise ValueError(f'Touch payload mismatch: {id}')
            at=pool+struct.unpack_from('<H',row)[0];end=b.index(0,at);name=b[at:end+1]
            if not 1<len(name)<=96:raise ValueError('Name bound')
            names.add((id,name))
        count+=1
    if count!=30:raise ValueError('All30 banks required')
    rows=['#pragma once','// Generated from the immutable DATA v2; native encoded names, not translated aliases.','#include <array>','namespace FfxHooks::SpiraElemental::Equipment {','struct NameProof {unsigned id,size;std::array<unsigned char,96> bytes;};','inline constexpr NameProof Names[]={']
    rows += ['    {%d,%d,{%s}},'%(id,len(name),','.join('0x%02X'%v for v in name)) for id,name in sorted(names)]
    rows += ['};','}'];a.output.write_text('\n'.join(rows)+'\n');print(f'{count} banks, {len(names)} distinct encoded identities')
if __name__=='__main__':main()
