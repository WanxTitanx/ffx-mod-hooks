"""PT-BR glyph authoring in unused Western font cells. Jarvis-HOOK."""
from __future__ import annotations
import struct
from asset_io import AssetError, digest, ftc_layout, phyre_layout
from inspect_assets import ATLAS_NAMES, texture_image

SOURCE_HASHES = {
    'base.ftc': '309ba121c26b693a8bea16f10891c997a0363a72307bc7af80f25d11cf21f8c2',
    'font_0_0.dds.phyre': 'd4ccdc03c461238b664e960a2efca12888eddc210ec67479151d7957e336d926',
    'font_0_1.dds.phyre': '15ad660bda441c061832e542e69b57d31e3a529f2f4ce7d177988d7b225246b6',
    'shadow_0_0.dds.phyre': '5688e69d337bf0ed7b96730b92748002bd8e22590e46ab1b85fa4da8dd6e6e59',
    'shadow_0_1.dds.phyre': '2f1e8667334d2f554c8dbab90e6f76d146c559a6dfca8f220c2cd786e0cdf47e',
}
# F0/F1 contain native ink despite having no assigned Unicode character.
CUSTOM_GLYPHS = ((227,242,112,199),(245,243,126,199),(195,244,80,176),(213,245,94,176))
ORDINAL_GLYPHS = ((186,246,126),(170,247,112))


def _rgb565(rgb):
    r,g,b=rgb
    return ((r*31+127)//255<<11)|((g*63+127)//255<<5)|((b*31+127)//255)


def _expand565(value):
    r,g,b=value>>11,(value>>5)&63,value&31
    return ((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))


def encode_dxt5(pixels):
    """One BC3 block; zero-alpha pixels remain exactly transparent."""
    pixels=tuple(pixels)
    if len(pixels)!=16:
        raise AssetError('DXT5 requires sixteen pixels')
    a0=max(p[3] for p in pixels)
    alphas=[a0,0]+[((7-i)*a0)//7 for i in range(1,7)] if a0 else [0]*8
    ab=sum(min(range(8),key=lambda j:abs(alphas[j]-p[3]))<<(3*i)
           for i,p in enumerate(pixels))
    visible=[p[:3] for p in pixels if p[3]]
    high=tuple(max(p[c] for p in visible) for c in range(3)) if visible else (0,0,0)
    low=tuple(min(p[c] for p in visible) for c in range(3)) if visible else (0,0,0)
    c0,c1=_rgb565(high),_rgb565(low)
    if c0==c1:
        if c0<65535:c0+=1
        else:c1-=1
    if c0<c1:c0,c1=c1,c0
    p0,p1=_expand565(c0),_expand565(c1)
    colors=[p0,p1,tuple((2*a+b)//3 for a,b in zip(p0,p1)),
            tuple((a+2*b)//3 for a,b in zip(p0,p1))]
    cb=sum(min(range(4),key=lambda j:sum((colors[j][c]-p[c])**2 for c in range(3)))<<(2*i)
           for i,p in enumerate(pixels))
    return bytes((a0,0))+ab.to_bytes(6,'little')+struct.pack('<HHI',c0,c1,cb)


def cell_box(code,width,height):
    if not 48<=code<=255:
        raise AssetError('Invalid Western glyph byte')
    g=code-48
    x=14*((g%18)//2)
    y=18*(g//18)
    return (round(x*width/128),round(y*height/234),
            round((x+14)*width/128),round((y+18)*height/234))


def _crop(pages,role,code):
    image=pages[f'{role}_0_{(code-48)&1}.dds.phyre']
    return image.crop(cell_box(code,*image.size))


def _accent_boundary(image):
    alpha=image.getchannel('A')
    rows=[max(alpha.crop((0,y,alpha.width,y+1)).tobytes())>12
          for y in range(alpha.height)]
    first=next((i for i,value in enumerate(rows) if value),None)
    if first is None:
        raise AssetError('Native tilde donor is blank')
    for start in range(first+1,min(alpha.height//2,first+24)):
        if not any(rows[start:start+2]) and any(rows[start+2:]):
            return start+1
    raise AssetError('Native tilde and letter body cannot be separated')


def _patch_empty_blocks(original,desired,targets):
    from PIL import Image
    layout=phyre_layout(original)
    if (layout.format,layout.width,layout.height)!=('DXT5',512,1024):
        raise AssetError('Unsupported Western atlas')
    before=texture_image(original)
    after=desired.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    blocks=set()
    for x0,y0,x1,y1 in targets:
        y0,y1=layout.height-y1,layout.height-y0
        blocks.update((bx,by) for by in range(y0//4,(y1+3)//4)
                      for bx in range(x0//4,(x1+3)//4))
    output=bytearray(original)
    changed=[]
    for bx,by in sorted(blocks):
        box=(bx*4,by*4,bx*4+4,by*4+4)
        if before.crop(box).getchannel('A').getbbox() is not None:
            raise AssetError('Added glyph would overwrite native ink or a shared edge block')
        raw=after.crop(box).tobytes()
        pixels=tuple(tuple(raw[i:i+4]) for i in range(0,len(raw),4))
        if not any(p[3] for p in pixels):continue
        offset=layout.pixels+(by*(layout.width//4)+bx)*16
        output[offset:offset+16]=encode_dxt5(pixels)
        changed.append(offset)
    if not changed:
        raise AssetError('No visible glyphs were generated')
    return bytes(output),changed


def build_font(originals, *, ordinals=False):
    from PIL import Image
    if set(originals)!=set(SOURCE_HASHES):
        raise AssetError('All font, shadow and metric inputs are required')
    for name,data in originals.items():
        if digest(data)!=SOURCE_HASHES[name]:
            raise AssetError(f'Unrecognized native font input: {name}')
    ftc=ftc_layout(originals['base.ftc'])
    metrics=bytearray(originals['base.ftc'])
    pages={n:texture_image(originals[n]).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
           for n in ATLAS_NAMES}
    edited={n:p.copy() for n,p in pages.items()}
    targets={n:[] for n in ATLAS_NAMES}
    glyphs=[]
    for unicode,code,base,donor in CUSTOM_GLYPHS:
        index=code-48
        width=metrics[ftc.metrics+base-48]
        if not 1<=width<=56 or index>=ftc.count:
            raise AssetError('Glyph exceeds existing Western metrics')
        boundary=_accent_boundary(_crop(pages,'font',donor))
        for role in ('font','shadow'):
            key=f'{role}_0_{index&1}.dds.phyre'
            box=cell_box(code,*pages[key].size)
            old=pages[key].crop(box)
            if old.getchannel('A').getbbox() is not None:
                raise AssetError(f'Unassigned glyph {code} has native ink')
            letter=_crop(pages,role,base).resize(old.size,Image.Resampling.LANCZOS)
            accent=_crop(pages,role,donor)
            band=Image.new('RGBA',accent.size)
            band.paste(accent.crop((0,0,accent.width,boundary)),(0,0))
            band=band.resize(old.size,Image.Resampling.LANCZOS)
            shifted=Image.new('RGBA',old.size)
            shifted.paste(band,(round((width-metrics[ftc.metrics+donor-48])/2),0))
            edited[key].paste(Image.alpha_composite(letter,shifted),box)
            targets[key].append(box)
        metrics[ftc.metrics+index]=width
        glyphs.append({'unicode':unicode,'code':code,'width':width})
    if ordinals:
        for unicode,code,base in ORDINAL_GLYPHS:
            index=code-48;scale=.65
            width=round(metrics[ftc.metrics+base-48]*scale)
            for role in ('font','shadow'):
                key=f'{role}_0_{index&1}.dds.phyre';box=cell_box(code,*pages[key].size)
                old=pages[key].crop(box)
                if old.getchannel('A').getbbox() is not None:
                    raise AssetError('Ordinal would overwrite native glyph ink')
                original=_crop(pages,role,base)
                letter=original.resize((round(original.width*scale),round(original.height*scale)),Image.Resampling.LANCZOS)
                tile=Image.new('RGBA',old.size);tile.alpha_composite(letter,(0,1))
                edited[key].paste(tile,box);targets[key].append(box)
            metrics[ftc.metrics+index]=width
            glyphs.append({'unicode':unicode,'code':code,'width':width})
    output={'base.ftc':bytes(metrics)}
    audit={'native_count_unchanged':ftc.count,'glyphs':glyphs,'changed_blocks':{}}
    for name in ATLAS_NAMES:
        output[name],changed=_patch_empty_blocks(originals[name],edited[name],targets[name])
        audit['changed_blocks'][name]=changed
        if len(output[name])!=len(originals[name]):
            raise AssetError('Font allocation size changed')
    return output,glyphs,audit


def main():
    import argparse,json
    from pathlib import Path
    from PIL import Image,ImageDraw
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    source=args.source.resolve(strict=True)
    output=args.output.resolve()
    if output.exists() or source==output or source in output.parents:
        parser.error('Use a new isolated output directory')
    try:
        payloads,glyphs,audit=build_font({n:(source/n).read_bytes() for n in SOURCE_HASHES})
        output.mkdir(parents=True)
        for name,data in payloads.items():
            (output/name).write_bytes(data)
        pages={n:texture_image(payloads[n]).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
               for n in ATLAS_NAMES}
        gallery=Image.new('RGBA',(480,220),(30,30,30,255))
        draw=ImageDraw.Draw(gallery)
        for i,g in enumerate(glyphs):
            gallery.alpha_composite(_crop(pages,'font',g['code']),(i*112+16,34))
            gallery.alpha_composite(_crop(pages,'shadow',g['code']),(i*112+16,124))
            draw.text((i*112+16,8),f"U+{g['unicode']:04X}",fill=(240,240,240,255))
        gallery.save(output/'glyph-preview.png')
        (output/'audit.json').write_text(json.dumps(audit,indent=2),encoding='utf-8')
        print(json.dumps({'output':str(output),'glyphs':glyphs,
                          'changed_blocks':{k:len(v) for k,v in audit['changed_blocks'].items()}},indent=2))
    except (AssetError,OSError) as exc:
        parser.exit(1,f'Font build failed: {exc}\n')


if __name__=='__main__':
    main()
