#!/usr/bin/env python3
"""Build a private, bounded normal-battle profile pack from the user's assets.

No game-derived payload is checked into Git. Format facts and float-pool relocation
were checked against the GPLv3 Editor readers/writer; see the profile manifest.
"""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

SOURCES = ['kino00_00', 'mcfr00_00', 'mcyt00_00', 'mcyt00_21',
           'nagi05_24', 'nagi05_24', 'nagi05_24', 'bika02_01', 'kino00_00']
def u16(b,o): return struct.unpack_from('<H',b,o)[0]
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def set32(b,o,v): struct.pack_into('<I',b,o,v)
def sha(b): return hashlib.sha256(b).hexdigest()
def chunks(b):
    assert u32(b,0)==8 and len(b)<65536
    offsets=[u32(b,4+i*4) for i in range(8)]
    assert offsets[-1]==len(b)
    result=[]
    for i,o in enumerate(offsets[:-1]):
        if not o: result.append(b''); continue
        end=next(x for x in offsets[i+1:] if x>o)
        assert 0x24<=o<end<=len(b)
        result.append(bytes(b[o:end]))
    return result
def container(parts):
    b=bytearray(0x30);set32(b,0,8)
    for i,part in enumerate(parts):
        if not part:continue
        while len(b)%4:b.append(0)
        set32(b,4+i*4,len(b));b.extend(part)
    set32(b,32,len(b));assert len(b)<65536
    return b
def workers(script):
    n=u16(script,0x36);assert 0<n<=64
    out=[]
    for i in range(n):
        d=u32(script,0x38+4*i);assert d+0x28<=len(script)
        count=u16(script,d+8);tab=u32(script,d+0x20)
        assert tab+4*count<=len(script)
        out.append((d,count,tab))
    return out
def neutral_map(mapping,script):
    count,slots=mapping[:2];assert slots==138 and count>0
    records=(slots+3)&~1;ws=workers(script)
    out=bytearray([2,slots]+[255]*slots);out.extend(bytes(records-len(out)+8))
    for new,(slot,kind) in enumerate([(0,0),(63,4)]):
        old=mapping[2+slot];assert old<count
        o=records+old*4;wi,ty=mapping[o:o+2];assert ty==kind and wi<len(ws)
        src=u16(mapping,o+2);tags=u16(mapping,src)
        section=mapping[src:src+2+tags*2];assert len(section)==2+tags*2
        assert all(u16(section,2+j*2)==65535 or u16(section,2+j*2)<ws[wi][1] for j in range(tags))
        out[2+slot]=new
        struct.pack_into('<BBH',out,records+new*4,wi,ty,len(out));out.extend(section)
    assert len(out)<=2012 and all(out[2+i]==255 for i in range(41,49))
    return bytes(out)
def tactical(script, basis=(0.0, 0.0, 0.0, 1.0), elevation=-55.0, distance_floor=340.0, distance_ceiling=600.0):
    b=bytearray(script);ws=workers(b);pool=u32(b,ws[0][0]+0x1c)
    start=u32(b,0x30);length=u32(b,0);assert 0<pool<start and start+length<=len(b)
    assert all(u32(b,d+0x1c)==pool for d,_,_ in ws)
    ins=[];p=start
    while p<start+length:
        op=b[p];n=3 if op>=128 else 1;assert p+n<=start+length
        ins.append((p,op,u16(b,p+1) if n==3 else 0));p+=n
    changes=[];values=[]
    # Negative world Y is above the battlefield. Cover Cartesian opening shots
    # as well as polar shots without changing shared constants or instruction sizes.
    ox,oz,fx,fz=basis
    reference=(ox+fx*90.0,-20.0,oz+fz*90.0)
    reach=distance_floor*math.cos(math.radians(elevation))
    eye=(reference[0]-fx*reach,reference[1]+distance_floor*math.sin(math.radians(elevation)),reference[2]-fz*reach)
    for i,(o,op,arg) in enumerate(ins):
        if op not in (0xB5,0xD8) or arg not in (0x6002,0x6020,0x6004,0x6040,0x6044,0x604D) or i<3:continue
        args=ins[i-3:i]
        if [x[1] for x in args]!=[0xAF]*3:continue
        assert all(pool+4*x[2]+4<=start for x in args)
        if arg in (0x6002,0x6020):
            replacements=list(zip(args,eye if arg==0x6002 else reference))
        else:
            distance=struct.unpack_from('<f',b,pool+4*args[2][2])[0]
            if not math.isfinite(distance) or distance<=0:continue
            replacements=[(args[1],elevation),(args[2],max(distance_floor,min(distance_ceiling,distance)))]
        for push,value in replacements:
            if value not in values:values.append(value)
            changes.append((push[0]+1,(start-pool)//4+values.index(value)))
    assert changes, 'no proven literal polar arguments'
    for o,index in changes:assert index<65536;struct.pack_into('<H',b,o,index)
    add=4*len(values);out=b[:start]+b''.join(struct.pack('<f',v) for v in values)+b[start:]
    set32(out,0x10,u32(b,0x10)+add);set32(out,0x30,start+add)
    def reloc(o,strict=False):
        v=u32(out,o)
        if v>start if strict else v>=start:set32(out,o,v+add)
    for i in range(len(ws)):reloc(0x38+4*i)
    for old,_,_ in ws:
        d=old+add if old>=start else old
        for off in [0x14,0x18,0x1c,0x20,0x24]:reloc(d+off,off==0x14)
    return bytes(out),len(changes)
def area8(area, first_area=False):
    if first_area:
        area=bytearray(area)
        assert len(area)>=0x70 and area[0]==0 and area[1]>=1
        area[1]=1;area[4]=min(area[4],7);area[6]=min(area[6],8)
    assert len(area)>=0x70 and area[0]==0 and area[1]==1
    offsets=[u32(area,0x10+4*i) for i in range(8)]
    # The final camera block is independently bounded; some old modified donors
    # contain out-of-range pointers and are deliberately not accepted.
    assert (offsets[0]>=0x70 if first_area else offsets[0]==0x70) and all(0<=o<len(area) for o in offsets)
    live,stage,camera=offsets[4],offsets[5],offsets[7]
    assert 0<live<stage<camera and (stage-live)%16==0
    native=min(area[6],(stage-live)//16);assert 1<=native<=8
    party=area[4];assert 1<=party<=7
    def point(off):return struct.unpack_from('<4f',area,off)
    pp=[point(offsets[0]+16*i) for i in range(party)]
    mm=[point(live+16*i) for i in range(native)]
    ox=sum(x[0] for x in pp)/party;oz=sum(x[2] for x in pp)/party
    dx=sum(x[0] for x in mm)/native-ox;dz=sum(x[2] for x in mm)/native-oz
    length=math.hypot(dx,dz);assert length>1
    fx,fz=dx/length,dz/length
    out=bytearray(area[:live]);out.extend(area[live:live+native*16])
    for i in range(native,8):
        x=(i%4-1.5)*56;z=116 if i<4 else 172
        out.extend(struct.pack('<4f',ox+fz*x+fx*z,0,oz-fx*x+fz*z,0))
    newstage=len(out);available=(camera-stage)//16
    for i in range(8):
        if i<min(native,available):out.extend(area[stage+i*16:stage+(i+1)*16])
        else:out.extend(out[live+i*16:live+(i+1)*16])
    shift=len(out)-camera;out.extend(area[camera:]);out[6]=8
    set32(out,0x24,newstage)
    for j,o in enumerate(offsets):
        if o>=camera:set32(out,0x10+4*j,o+shift)
    assert len(out)<=(8192 if first_area else 2048)
    return bytes(out),native,(ox,oz,fx,fz)
def main():
    a=argparse.ArgumentParser();a.add_argument('--source-root',type=Path,required=True);a.add_argument('--carrier-path',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
    carrier=args.carrier_path.read_bytes();assert sha(carrier)=='ddf8d89343195d3d014630c296a9583ee556efa839918435802249fe148594d0'
    payload=bytearray(b'ARPROG01'+struct.pack('<II',18,len(carrier))+carrier);records=[]
    for choice,name in enumerate(SOURCES):
        f=args.source_root/name/(name+'.bin');raw=f.read_bytes();parts=chunks(raw)
        mapping=neutral_map(parts[1],parts[0]);grown,native,basis=area8(parts[3]);assert parts[2][3]==0
        for camera in range(2):
            current=parts[:];current[1]=mapping;current[3]=grown
            current[2]=parts[2][:12]+b'\xff'*16
            edits=0
            if camera:current[0],edits=tactical(current[0],basis)
            data=container(current)
            source=name.encode().ljust(16,b'\0');assert len(source)==16
            payload.extend(struct.pack('<IIII4f16s',choice,camera,len(data),native,*basis,source));payload.extend(data)
            records.append({'scenery':choice,'camera':camera,'source':str(f),'source_sha256':sha(raw),'profile_sha256':sha(data),'bytes':len(data),'native_positions':native,'basis':basis,'camera_operand_edits':edits,'worker_slots':[0,63],'monster_overrides':[]})
    assert len(payload)<=1024*1024
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(payload)
    manifest={'sha256':sha(payload),'bytes':len(payload),'records':records,'game_derived_payload':'private; do not commit'}
    args.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'sha256':manifest['sha256'],'bytes':len(payload),'profiles':len(records),'tactical_edits':[r['camera_operand_edits'] for r in records if r['camera']]},indent=2))
if __name__=='__main__':main()
