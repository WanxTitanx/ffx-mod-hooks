#!/usr/bin/env python3
"""Create private Nul animation DLL/texture clones from a hash-pinned safe donor.

DXT5 complete-mip luminance tint follows Ps3MagicWindTextureWriter.cs in
WanxTitanx/ffx-editor-main (GPL-3.0), inspected 2026-09-29. Native .text,
texture headers, dimensions and indices are preserved. No donor is written.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path
from commands import SPELLS

COLORS=((1.4,1.15,.5,.8),(.65,.3,1.3,.8),(1.05,.7,.35,.8),(.35,1.2,1.05,.8),(.45,1.25,.45,.8),(.75,.45,1.2,.8))

def sha(data):return hashlib.sha256(data).hexdigest()

def tint(data,layout,color):
    if len(data)!=layout['bytes'] or sha(data)!=layout['sha256']:raise ValueError('Texture identity mismatch')
    width,height=layout['width'],layout['height'];sizes=[]
    if any(x<1 or x>8192 or x&(x-1) for x in (width,height)):raise ValueError('Unsupported texture dimensions')
    while True:
        sizes.append(max(1,width//4)*max(1,height//4)*16)
        if width==height==1:break
        width=max(1,width//2);height=max(1,height//2)
    start=layout['payload']
    if start+sum(sizes)!=len(data):raise ValueError('Not exactly a complete DXT5 mip chain')
    result=bytearray(data);red,green,blue,alpha=color
    if not all(0<x<=2 for x in color) or alpha>1:raise ValueError('Invalid tint')
    for block in range(start,len(result),16):
        a,b=data[block:block+2];result[block]=round(a*alpha);result[block+1]=round(b*alpha)
        if a>b and result[block]<=result[block+1]:result[block]=min(255,result[block+1]+1)
        for at in (block+8,block+10):
            rgb=struct.unpack_from('<H',data,at)[0]
            luminance=.2126*((rgb>>11)&31)/31+.7152*((rgb>>5)&63)/63+.0722*(rgb&31)/31
            color565=(round(min(1,luminance*red)*31)<<11)|(round(min(1,luminance*green)*63)<<5)|round(min(1,luminance*blue)*31)
            struct.pack_into('<H',result,at,color565)
        assert result[block+2:block+8]==data[block+2:block+8] and result[block+12:block+16]==data[block+12:block+16]
    assert result[:start]==data[:start] and len(result)==len(data)
    return bytes(result)

def clone_dll(data,recipe,new_id):
    if len(data)!=recipe['dll_bytes'] or sha(data)!=recipe['dll_sha256']:raise ValueError('Donor DLL identity mismatch')
    old=f'magic_{recipe["donor_magic"]:04}'.encode('utf-16le');new=f'magic_{new_id:04}'.encode('utf-16le')
    if len(new)!=len(old) or data.count(old)!=3:raise ValueError('Unreviewed magic identity strings')
    result=data.replace(old,new);section=recipe['text_section'];a=section['offset'];b=a+section['bytes']
    assert result[a:b]==data[a:b] and sha(result[a:b])==section['sha256']
    assert len(result)==len(data) and old not in result and result.count(new)==3
    return result

def stage(donor:Path,textures:Path,output:Path):
    recipe=json.loads(Path(__file__).with_name('holy_donor_recipe.json').read_text())
    data=donor.read_bytes();original={p:sha(p.read_bytes()) for p in [donor,*textures.rglob('*')] if p.is_file()}
    # Validate every source before creating any outputs.
    for layout in recipe['textures']:
        path=textures/layout['path'];blob=path.read_bytes()
        if sha(blob)!=layout['sha256']:raise ValueError('Source texture does not match the reviewed corpus')
    clone_dll(data,recipe,SPELLS[0].animation)
    output.mkdir(parents=True,exist_ok=False);files=[]
    for spell,color in zip(SPELLS,COLORS):
        dll=output/'magicFiles/FFX'/f'magic_{spell.animation:04}.dll';dll.parent.mkdir(parents=True,exist_ok=True)
        payload=clone_dll(data,recipe,spell.animation);dll.write_bytes(payload)
        files.append({'path':str(dll.relative_to(output)),'sha256':sha(payload),'bytes':len(payload)})
        root=output/'data/mods/FFX_Data/GameData/PS3Data/magic'/f'magic_{spell.animation:04}'
        for layout in recipe['textures']:
            target=root/layout['path'];target.parent.mkdir(parents=True,exist_ok=True)
            payload=tint((textures/layout['path']).read_bytes(),layout,color);target.write_bytes(payload)
            files.append({'path':str(target.relative_to(output)),'sha256':sha(payload),'bytes':len(payload)})
    assert all(sha(p.read_bytes())==value for p,value in original.items())
    for f in files:assert sha((output/f['path']).read_bytes())==f['sha256']
    result={'producer':'Jarvis-HOOK','donor_sha256':sha(data),'donors_unchanged':True,'native_text_unchanged':True,
            'textures_per_clone':len(recipe['textures']),'magic_ids':[s.animation for s in SPELLS],'files':files,
            'limits':'Private derived assets. Offline structural validation; live visual/gameplay acceptance remains RT2.'}
    (output/'animation-receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--donor',type=Path,required=True)
    p.add_argument('--textures',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    r=stage(a.donor,a.textures,a.output);print(f'PASS: {len(r["magic_ids"])} unique DLLs, {len(r["files"])} files, all donor bytes preserved')

if __name__=='__main__':main()
