"""Compile reviewed UI PNGs into unchanged native Phyre texture containers.

Jarvis-HOOK. Only existing pixel payloads are written; headers, dimensions,
format and untouched compressed blocks remain native. No installation occurs.
"""
from __future__ import annotations
import argparse,hashlib,io,json,re
from functools import lru_cache
from pathlib import Path
import numpy as np
from PIL import Image
from asset_io import AssetError,VbfArchive,phyre_layout
from inspect_assets import texture_image

def validate_review_uids(entries):
    """Keep preview filenames portable and each group bound to one reviewed PNG."""
    groups={}
    reserved={'CON','PRN','AUX','NUL',*(f'COM{i}' for i in range(1,10)),*(f'LPT{i}' for i in range(1,10))}
    for entry in entries:
        uid=entry.get('uid') if isinstance(entry,dict) else None
        if not isinstance(uid,str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]{0,63}',uid) or uid.upper() in reserved:
            raise AssetError('Invalid preview UID: use a portable simple identifier')
        key=uid.casefold();binding=(uid,entry.get('candidate_png'),entry.get('candidate_png_sha256'))
        if key in groups and groups[key]!=binding:
            raise AssetError('Preview UID collision or inconsistent PNG association')
        groups[key]=binding

def _payload(image,fmt,encoder='pillow'):
    if fmt=='ARGB8':return image.tobytes('raw','BGRA')
    if encoder=='ispc':
        import ispc_texcomp
        if ispc_texcomp.__version__!='1.0.1':
            raise AssetError('Reviewed graphics require ispc_texcomp 1.0.1')
        surface=ispc_texcomp.RGBASurface(image.tobytes(),image.width,image.height)
        encode=ispc_texcomp.compress_blocks_bc1 if fmt=='DXT1' else ispc_texcomp.compress_blocks_bc3
        data=encode(surface)
        expected=image.width*image.height//(2 if fmt=='DXT1' else 1)
        # The 1.0.1 BC1 binding allocates twice its written extent. Never retain
        # or serialize the unwritten tail; native BC1 is exactly 8 bytes/block.
        if len(data)!=(expected*2 if fmt=='DXT1' else expected):
            raise AssetError('Unexpected ISPC output allocation')
        return data[:expected]
    if encoder!='pillow':raise AssetError('Unknown texture encoder')
    output=io.BytesIO();image.save(output,format='DDS',pixel_format=fmt)
    data=output.getvalue()
    if data[:4]!=b'DDS ' or data[84:88]!=fmt.encode():
        raise AssetError('Unexpected DDS encoder output')
    return data[128:]

@lru_cache(maxsize=32768)
def _bc3_alpha(values: tuple[int,...]) -> bytes:
    """Quantize BC3 alpha independently; preserve fully clear/opaque pixels.

    The DDS encoder can map 254-alpha text strokes to 203. Both native BC3
    interpolation modes are evaluated here against the actual sixteen samples.
    Fixed 0/255 entries in the six-alpha mode also retain antialiased strokes.
    """
    low,high=min(values),max(values)
    inner=[v for v in values if 0<v<255]
    il,ih=(min(inner),max(inner)) if inner else (low,high)
    pairs={(high,low),(255,low),(high,0),(255,0),(low,high),(il,ih),(0,255)}
    best=None
    for a0,a1 in sorted(pairs):
        palette=[a0,a1]
        if a0>a1:
            palette.extend(((7-i)*a0+i*a1)//7 for i in range(1,7))
        else:
            palette.extend(((5-i)*a0+i*a1)//5 for i in range(1,5))
            palette.extend((0,255))
        if (low==0 and 0 not in palette) or (high==255 and 255 not in palette):
            continue
        indices=[min(range(8),key=lambda j:abs(palette[j]-value)) for value in values]
        error=sum((palette[j]-v)**2 for j,v in zip(indices,values))
        bits=sum(j<<(3*i) for i,j in enumerate(indices))
        result=bytes((a0,a1))+bits.to_bytes(6,'little')
        if best is None or (error,result)<best:best=(error,result)
    return best[1]

def rebuild_texture(source: bytes,display_image: Image.Image, *, encoder='pillow'):
    layout=phyre_layout(source)
    if layout.format not in ('DXT1','DXT5','ARGB8'):
        raise AssetError('Unexamined UI texture format')
    if layout.pixels+layout.pixel_size!=len(source):
        raise AssetError('UI texture contains an unexamined mip or tail')
    if display_image.size!=(layout.width,layout.height):
        raise AssetError('UI texture dimensions cannot change')
    desired=display_image.convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    native=texture_image(source)
    before=np.asarray(native);after=np.asarray(desired)
    changed=np.any(before!=after,axis=2)
    audit=dict(format=layout.format,width=layout.width,height=layout.height,
               changed_pixels=int(changed.sum()),changed_blocks=0,outside_changed_blocks=0,
               source_sha256=hashlib.sha256(source).hexdigest())
    if not changed.any():
        audit['target_sha256']=audit['source_sha256'];return source,audit
    if layout.format!='ARGB8' and (layout.width%4 or layout.height%4):
        raise AssetError('UI texture requires aligned native blocks')
    encoded=_payload(desired,layout.format,encoder)
    if len(encoded)!=layout.pixel_size:raise AssetError('Encoded pixel extent changed')
    target=bytearray(source)
    if layout.format=='ARGB8':
        target[layout.pixels:]=encoded
        allowed=changed
    else:
        bh,bw=layout.height//4,layout.width//4
        blocks=changed.reshape(bh,4,bw,4).any(axis=(1,3))
        allowed=np.repeat(np.repeat(blocks,4,axis=0),4,axis=1)
        stride=8 if layout.format=='DXT1' else 16
        # All changed BC1 cells in this reviewed catalogue are opaque. Refuse
        # to flatten a future transparent block with the opaque BC1 encoder.
        if layout.format=='DXT1' and np.any(after[:,:,3][allowed]!=255):
            raise AssetError('Transparent BC1 edits require a separate examined encoder')
        for by,bx in np.argwhere(blocks):
            offset=(int(by)*bw+int(bx))*stride;at=layout.pixels+offset
            tile_before=before[by*4:by*4+4,bx*4:bx*4+4]
            tile_after=after[by*4:by*4+4,bx*4:bx*4+4]
            target[at:at+stride]=encoded[offset:offset+stride]
            if stride==16:
                if np.array_equal(tile_before[:,:,:3],tile_after[:,:,:3]):
                    target[at+8:at+16]=source[at+8:at+16]
                if np.array_equal(tile_before[:,:,3],tile_after[:,:,3]):
                    target[at:at+8]=source[at:at+8]
                else:
                    target[at:at+8]=_bc3_alpha(tuple(map(int,tile_after[:,:,3].flat)))
        audit['changed_blocks']=int(blocks.sum())
    target=bytes(target)
    decoded=np.asarray(texture_image(target))
    outside=np.any(decoded!=before,axis=2)&~allowed
    if outside.any():raise AssetError('Native pixels outside selected blocks changed')
    if target[:layout.pixels]!=source[:layout.pixels] or len(target)!=len(source):
        raise AssetError('Native container metadata or allocation size changed')
    # Compare premultiplied color so invisible RGB does not inflate visual error.
    a=after.astype(float);b=decoded.astype(float)
    a[:,:,:3]*=a[:,:,3:4]/255;b[:,:,:3]*=b[:,:,3:4]/255
    delta=(a-b)[allowed]
    audit.update(target_sha256=hashlib.sha256(target).hexdigest(),
                 edited_region_rmse=float(np.sqrt(np.mean(delta*delta))),
                 edited_region_max_error=float(np.abs(delta).max()),
                 container_bytes=len(target))
    return target,audit

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vbf',type=Path,required=True)
    parser.add_argument('--images',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--encoder',choices=['pillow','ispc'],default='pillow')
    args=parser.parse_args();vbf=args.vbf.resolve(strict=True);images=args.images.resolve(strict=True);out=args.output.resolve()
    if out.exists() or vbf.parent.parent==out or vbf.parent.parent in out.parents or images==out or images in out.parents:
        parser.error('Use a new isolated output directory outside the installation and PNG inputs')
    inputs=[json.loads(s) for s in (images/'resource-sources.jsonl').read_text().splitlines()]
    validate_review_uids(inputs)
    out.mkdir(parents=True);records=[];cache={};seen=set()
    with VbfArchive(vbf) as archive:
        for entry in inputs:
            name=entry['resource']
            if '/base_ftc/' in name:continue # Paired font profiles own these pages.
            if not name.startswith('ffx_data/gamedata/ps3data/') or '..' in Path(name).parts or name in seen:
                raise AssetError('Unexpected or duplicate native graphic route')
            seen.add(name);source=archive.read(name)
            if hashlib.sha256(source).hexdigest()!=entry['source_sha256']:
                raise AssetError('Native graphic source changed')
            png=(images/entry['candidate_png']).resolve(strict=True)
            if images not in png.parents or hashlib.sha256(png.read_bytes()).hexdigest()!=entry['candidate_png_sha256']:
                raise AssetError('Reviewed PNG changed or escaped its input root')
            key=(entry['source_sha256'],entry['candidate_png_sha256'])
            if key not in cache:
                cache[key]=rebuild_texture(source,Image.open(png),encoder=args.encoder)
            payload,audit=cache[key];relative='graphics/'+name
            path=out/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(payload)
            records.append(dict(uid=entry['uid'],request='/'+name,resource=name,path=relative,
                                source_size=len(source),size=len(payload),**audit))
        preview=out/'compiled-preview';preview.mkdir()
        for uid in sorted({r['uid'] for r in records}):
            row=next(r for r in records if r['uid']==uid)
            im=texture_image((out/row['path']).read_bytes()).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            matte=Image.new('RGBA',im.size,(0,0,0,255));matte.alpha_composite(im)
            matte.convert('RGB').save(preview/(uid+'.png'))
    report=dict(work_id='Jarvis-HOOK',encoder=args.encoder,resources=len(records),unique_compilations=len(cache),
                native_container_bytes=sum(r['size'] for r in records),headers_preserved=True,
                unchanged_blocks_preserved=True,records=records,installed=False,runtime_admitted=False)
    (out/'graphics-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='records'}))

if __name__=='__main__':main()
