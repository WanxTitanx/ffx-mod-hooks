#!/usr/bin/env python3
"""Jarvis-HOOK: private native-font fixture from the user's unmodified FFX VBF."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/text_languages'))
from asset_io import VbfArchive
from inspect_assets import texture_image

FAMILIES=(('us','uspc','menu_us'),('jp','new_jppc','menu'),
          ('kr','new_krpc','menu_kr'),('ch','new_chpc','menu_ch'))


def members():
    for code,master,menu in FAMILIES:
        yield code,'metrics',f'ffx_ps2/ffx/master/{master}/menu/base.ftc'
        yield code,'mapping',f'ffx_ps2/ffx/master/jppc/ffx_encoding/ffxsjistbl_{code}.bin'
        for page in range(2):
            yield code,f'page{page}',f'ffx_data/gamedata/ps3data/{menu}/base_ftc/d3d11/font_0_{page}.dds.phyre'


def archive_bytes(values):
    names=b''.join(n.encode()+b'\0' for n in values)
    blocks=[];entries=[];data=bytearray()
    for name,value in values.items():
        first=len(blocks);offset=len(data)
        for at in range(0,len(value),65536):
            raw=value[at:at+65536];packed=zlib.compress(raw)
            if len(packed)>=len(raw):packed=raw
            blocks.append(len(packed)%65536);data+=packed
        entries.append((first,0,len(value),offset,0))
    size=16+len(entries)*48+4+len(names)+2*len(blocks)
    header=bytearray(struct.pack('<4sIQ',b'SRYK',size,len(entries)))
    header+=b''.join(hashlib.md5(n.encode()).digest() for n in values)
    header+=b''.join(struct.pack('<IIQQQ',a,b,c,d+size,e) for a,b,c,d,e in entries)
    header+=struct.pack('<I',len(names)+4)+names+struct.pack('<'+'H'*len(blocks),*blocks)
    assert len(header)==size
    return header+data+hashlib.md5(header).digest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vbf',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();source=args.vbf.resolve(strict=True)
    if out==source or source.parent in out.parents:raise ValueError('Use a private output outside the game directory')
    out.mkdir(parents=True,exist_ok=True)
    values={};evidence=[];by_family={}
    with VbfArchive(source) as archive:
        for family,kind,name in members():
            value=archive.read(name);values[name]=value;by_family.setdefault(family,{})[kind]=value
            evidence.append(dict(family=family,kind=kind,path=name,size=len(value),sha256=hashlib.sha256(value).hexdigest()))
    fixture=out/'native-fonts.vbf';fixture.write_bytes(archive_bytes(values))
    with VbfArchive(fixture) as archive:
        for name,value in values.items():assert archive.read(name)==value
    (out/'assets.json').write_text(json.dumps(evidence,indent=2)+'\n')
    header=['#pragma once','// Jarvis-HOOK: identities of original, user-owned font resources; no font pixels are shipped.',
            'namespace FfxHooks::UiNativeFont::Evidence {',
            'struct Asset {const char* path;const char* sha256;};', 'inline constexpr Asset Assets[]={']
    header += ['    {"'+v['path']+'","'+v['sha256']+'"},' for v in evidence]
    header += ['};','}']
    (out/'UiNativeFontEvidence.h').write_text('\n'.join(header)+'\n')
    from PIL import Image
    refs=[]
    for family,index in [('us',0),('us',32),('jp',500),('kr',500),('ch',1200)]:
        data=by_family[family];metrics=data['metrics'];cw,ch=struct.unpack_from('<HH',metrics,20);pw,ph=struct.unpack_from('<HH',metrics,40)
        image=texture_image(data[f'page{index&1}']).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        x=(index%18)//2*cw*image.width/pw;y=(index//18)*ch*image.height/ph
        box=(x,y,x+cw*image.width/pw,y+ch*image.height/ph)
        glyph=image.transform((56,72),Image.Transform.EXTENT,box,Image.Resampling.BILINEAR)
        name=f'{family}-{index}';glyph.save(out/(name+'.png'));(out/(name+'.rgba')).write_bytes(glyph.tobytes())
        refs.append(dict(family=family,index=index,file=name+'.rgba'))
    (out/'reference.json').write_text(json.dumps(refs,indent=2)+'\n')
    print('Verified',len(values),'original font resources; private fixture:',fixture)


if __name__=='__main__':main()
