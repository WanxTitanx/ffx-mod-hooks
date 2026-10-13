"""Native room IDs from the vanilla event group, independent of encounter IDs."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from tools.text_languages.asset_io import VbfArchive

PREFIXES={'bsvr':0,'bsil':0,'bsyt':0,'bika':1,'kami':2,'mcfr':3,'maca':3,'mcyt':3,'zkrn':4,'dome':4,'nagi05':5,'mtgz':6,'kino':7}
def generate(vbf: Path,mod: Path):
    with VbfArchive(vbf) as archive:
        name_path='ffx_ps2/ffx/proj/battle/jp/cddata/cdrom.fnd'
        group_path='ffx_ps2/ffx/proj/battle/jp/cddata/cdrom.fid'
        names=archive.read(name_path);groups=archive.read(group_path)
    boundaries=struct.unpack('<'+'h'*(len(groups)//2),groups);base=boundaries[12];end=next(x for x in boundaries[13:] if x>=0)
    if end-base!=402*18:raise ValueError('Unexpected event group extent')
    records=[];checks={19:'bsil0100',54:'maca0400',110:'mcfr0000',153:'mcyt0000',183:'mcfr1100',363:'zkrn0000'}
    for room in range(402):
        offset=struct.unpack_from('<I',names,4+(base+18*room)*4)[0]
        path=names[offset:names.index(0,offset)].decode('ascii');stem=path.split('/')[-2]
        if room in checks and stem!=checks[room]:raise ValueError('Known native room cross-reference mismatch')
        matches=[family for prefix,family in PREFIXES.items() if stem.startswith(prefix)]
        if matches and not path.endswith('/'+stem+'.ebp'):raise ValueError('Region room entry does not select its EBP')
        if len(matches)>1:raise ValueError('Ambiguous region')
        if matches:records.append(dict(room=room,family=matches[0],event=path))
    root=Path(__file__).resolve().parents[2]
    text=['// Jarvis-HOOK. Generated from vanilla cdrom group 12, 18 assets per native room.', '#pragma once','#include "CaptureCascadeCore.h"','namespace FfxHooks::CaptureCascade::Rooms {','struct Entry {unsigned room;Aeon family;};','inline constexpr Entry entries[]={']
    text += [f"    {{{r['room']},static_cast<Aeon>({r['family']})}}," for r in records]
    text += ['};','inline Aeon Region(unsigned room) noexcept {for(const auto& e:entries)if(e.room==room)return e.family;return Aeon::Count;}','}']
    (root/'src/runtime/FfxHooksDll/hooks/CaptureCascadeRooms.generated.h').write_text('\n'.join(text)+'\n')
    report=dict(author='Jarvis-HOOK',source=name_path,sha256=hashlib.sha256(names).hexdigest(),groupSource=group_path,groupSha256=hashlib.sha256(groups).hexdigest(),stride=18,prefixes=PREFIXES,rooms=records)
    (mod/'docs/CAPTURE_CASCADE_ROOMS_2026-10-09.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Native room region identities:',len(records))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--vbf',type=Path,required=True);p.add_argument('--mod',type=Path,required=True);a=p.parse_args();generate(a.vbf,a.mod)
