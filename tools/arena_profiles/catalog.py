#!/usr/bin/env python3
"""Jarvis-HOOK: read owned VBF data into a private, exact-hash arena/catalog bundle.

The GPL-3.0 Editor reader is supplied explicitly; no archive payload enters Git.
Existing nine profiles are retained byte-for-byte. New terrain profiles use the
same normal main/camera program, never donor boss scripts or per-monster workers.
"""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
from pathlib import Path
import build

REGIONS={'bika':'Bikanel','bsil':'Besaid','dome':'Zanarkand Dome','hiku':'Airship',
         'nagi':'Calm Lands','mcyt':'Macalania Temple','mcfr':'Macalania Forest',
         'maca':'Lake Macalania','ssbt':'Baaj','bjyt':'Baaj Temple','sins':'Inside Sin',
         'kami':'Thunder Plains','kino':'Mushroom Rock','mihn':"Mi'ihen Highroad",
         'luca':'Luca','lchb':'Luca Harbor','djso':'Djose','djyt':'Djose Temple',
         'genk':'Moonflow','guad':'Guadosalam','stbv':'Bevelle','bvyt':'Bevelle Temple',
         'kilk':'Kilika','slik':'Kilika','klyt':'Kilika Temple','mtgz':'Mt. Gagazet',
         'omeg':'Omega Ruins','sfia':'Final Battle','znkd':'Zanarkand','zkrn':'Zanarkand Ruins',
         'cdsp':'Salvage Ship','test':'Test Arena','zzzz':'Monster Arena','syst':'System'}
def region(name):return REGIONS.get(name[:4],name[:6].upper())
def parts(raw):
    marker=build.u32(raw,0);assert marker in (5,7,8)
    offsets=[build.u32(raw,4+i*4) for i in range(marker)]
    result=[]
    for i,off in enumerate(offsets[:-1]):
        if off in (0,0xffffffff) or off>=len(raw):result.append(b'');continue
        end=next((v for v in offsets[i+1:] if off<v<=len(raw)),len(raw))
        result.append(raw[off:end])
    return result
def kernel_rows(raw):
    head,data,end=struct.unpack_from('<III',raw,4);assert end==len(raw) and (data-head)%14==0
    rows=[]
    for at in range(head,data,14):
        field,off,_=struct.unpack_from('<HHH',raw,at)
        key=raw[at+6:at+12].decode('ascii').strip('\0')
        cursor=data+off+2
        for group in range(raw[data+off+1]):
            count=raw[cursor];battlefield=build.u16(raw,cursor+1)
            for j in range(count):
                formation=raw[cursor+5+j*2]
                rows.append({'field':field,'group':group,'battlefield':battlefield,
                             'formation':formation,'name':f'{key}_{formation:02d}'})
            cursor+=5+count*2
    return rows
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--vbf',type=Path,required=True)
    parser.add_argument('--reader',type=Path,required=True);parser.add_argument('--previous',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--registry',type=Path,required=True)
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('owned_vbf_reader',args.reader);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    reader=module.VbfReader(str(args.vbf),quiet=True)
    prefix='ffx_ps2/ffx/master/jppc/battle/btl/'
    raw_by_name={}
    for path in reader.names:
        if path.startswith(prefix) and path.endswith('.bin'):
            key=path.split('/')[-2]
            if re.fullmatch('[a-z0-9_]{1,15}',key):raw_by_name[key]=reader.extract_bytes(reader.index_by_name(path))
    kernel=reader.extract_bytes(reader.index_by_name('ffx_ps2/ffx/master/jppc/battle/kernel/btl.bin'))
    rows=kernel_rows(kernel);reader.fs.close()
    previous=args.previous.read_bytes()
    assert previous[:8] in (b'ARPROG01',b'ARPROG02') and build.u32(previous,8)>=18
    carrier=previous[16:16+build.u32(previous,12)];at=16+len(carrier);profiles=[]
    for _ in range(18):
        n=build.u32(previous,at+8);profiles.append((previous[at:at+48],previous[at+48:at+48+n]));at+=48+n
    legacy=bytearray(b'ARPROG01'+struct.pack('<II',18,len(carrier))+carrier)
    for header,payload in profiles:legacy.extend(header);legacy.extend(payload)
    assert hashlib.sha256(legacy).hexdigest()=='6421be6420062828049b1d7bce1bd105b6f04530da70e5c73944a81fcdf45820'
    template=build.chunks(profiles[0][1]);template_geometry=struct.unpack_from('<4f',profiles[0][0],16)
    template_native=build.u32(profiles[0][0],12)
    source_by_bf={};geometry_by_bf={};geometry_by_region={}
    for row in rows:
        source_by_bf.setdefault(row['battlefield'],row)
        raw=raw_by_name.get(row['name'])
        if raw is None:continue
        try:
            ps=parts(raw);grown,native,basis=build.area8(ps[3],first_area=True)
            assert all(map(__import__('math').isfinite,basis))
            entry=(row,grown,native,basis,ps[2][3] if len(ps[2])>=4 else 0)
            geometry_by_bf.setdefault(row['battlefield'],entry)
            geometry_by_region.setdefault(row['name'][:4],entry)
        except (AssertionError,IndexError,struct.error,ValueError):pass
    # Preserve established scene indexes and JSON keys. Kernel selector zero is a
    # system sentinel, not a new selectable terrain.
    existing={1044:1,1046:2,1080:4,1049:7,1035:8}
    choices=[];mapping=dict(existing);manifest=[]
    for bf in sorted(source_by_bf):
        if bf==0 or bf in existing:continue
        row=source_by_bf[bf];choice=9+len(choices);mapping[bf]=choice
        source=row['name'];label=f'{region(source)} - {source[:6]} ({bf})'
        label=label[:55]
        geometry=geometry_by_bf.get(bf) or geometry_by_region.get(source[:4])
        if geometry:
            grow_source,grown,native,basis,water=geometry
            geometry_source=grow_source['name'];layout='area-zero' if grow_source['battlefield']==bf else 'region-layout'
        else:
            grown=template[3];native=template_native;basis=template_geometry;water=0
            geometry_source='kino00_00';layout='standard-layout'
        choices.append({'key':f'field_{bf}','label':label,'battlefield':bf,'source':source,
                        'geometry_source':geometry_source,'layout':layout})
        for camera in range(2):
            current=template[:];current[3]=grown
            formation=bytearray(current[2]);formation[3]=1 if water==1 else 0;current[2]=bytes(formation)
            current[0],edits=build.tactical(template[0],basis,
                elevation=-55.0 if camera else -18.0,distance_floor=340.0 if camera else 280.0)
            payload=build.container(current)
            header=struct.pack('<IIII4f16s',choice,camera,len(payload),native,*basis,source.encode().ljust(16,b'\0'))
            profiles.append((header,payload));manifest.append({'choice':choice,'camera':camera,'battlefield':bf,
                'source':source,'geometry_source':geometry_source,'layout':layout,'operand_edits':edits,
                'profile_sha256':build.sha(payload)})
    assert len(choices)+9<255 and len(profiles)==(len(choices)+9)*2
    registry='// Jarvis-HOOK: factual kernel IDs/names only; see arena browser evidence.\n'
    registry+='\n'.join('    {%s, %s, %du, %s},'%(json.dumps(e['key']),json.dumps(e['label']),e['battlefield'],json.dumps(e['source'])) for e in choices)+'\n'
    args.registry.write_text(registry)
    native_scenes={}
    for row in rows:
        if row['battlefield'] in mapping:native_scenes.setdefault(row['name'],mapping[row['battlefield']])
    encounters=[]
    for name,raw in sorted(raw_by_name.items()):
        ids=[];water=0
        try:
            formation=parts(raw)[2];water=formation[3] if len(formation)>=4 else 0
            for offset in range(12,min(len(formation),28),2):
                value=build.u16(formation,offset)
                if value!=0xffff:ids.append(value)
        except (AssertionError,IndexError,struct.error):pass
        encounters.append((name,native_scenes.get(name,0xffff),ids,water))
    assert len(encounters)<=1024
    bundle=bytearray(b'ARPROG02'+struct.pack('<II',len(profiles),len(carrier))+carrier)
    for header,payload in profiles:bundle.extend(header);bundle.extend(payload)
    bundle.extend(b'ARENC001'+struct.pack('<I',len(encounters)))
    for name,scene,ids,water in encounters:
        assert len(ids)<=8
        bundle.extend(struct.pack('<16sHBB8H',name.encode().ljust(16,b'\0'),scene,len(ids),water,*ids,*([0xffff]*(8-len(ids)))))
    assert len(bundle)<=8*1024*1024
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(bundle)
    result={'author':'Jarvis-HOOK','profiles':len(profiles),'scenery_choices':len(choices)+9,
            'native_battlefields':len(mapping),'encounters':len(encounters),'sha256':build.sha(bundle),
            'bytes':len(bundle),'original_profiles_unchanged':18,'new_scenery':choices,'new_profiles':manifest,
            'kernel_sha256':build.sha(kernel),'vbf_reader':str(args.reader),'vbf_reader_sha256':build.sha(args.reader.read_bytes()),
            'game_payload':'private; never commit or redistribute'}
    args.output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('new_scenery','new_profiles')},indent=2))
if __name__=='__main__':main()
