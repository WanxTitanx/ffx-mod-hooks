#!/usr/bin/env python3
"""Build a local text-only compatibility pack for an existing mod overlay.

Jarvis-HOOK. Inputs are read-only. Reuse a translation only when its row/slot
still has exactly the original English script. Never replace gameplay data or
guess that an incompatible replacement container has the old text layout.

The overlay is resolved the way the File Loader sees it in the game: Win32 under
Wine/Proton, therefore case-insensitive, even though the Linux tree is not.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import struct
from asset_io import AssetError
from pack import decode, encode, rebuild_resource, validate_edits, edit_script, kernel_layout, field_layout
from extended_containers import scripts as macro_scripts
from text_layouts import describe_resource, MASTER

# Native kernel loader (Steam RVAs): FFX_Kernel_LoadFileToTable 0x381D40 copies each
# table through a 0x25800-byte buffer filled by 0x382DF0, which rounds the file size up
# to 16 bytes. command/ply_rom/a_ability/item keep that rounded size in a 16-bit global
# and allocate/copy only size & 0xFFFF bytes, so their payload must stay <= 0xFFF0.
# A 66,035-byte command.bin rounded to 66,048 and left only 512 bytes in memory;
# the Status ability list crashed on garbage text (2026-10-10).
NATIVE_U16_TABLES=frozenset({'command.bin','ply_rom.bin','a_ability.bin','item.bin'})
# Loader cases 0/2/3 owned by the optional 32-bit DLL adapter
# (language.text_extended_kernel_tables). ply_rom keeps its native 16-bit path.
EXTENDED_KERNEL_TABLES=frozenset({'command.bin','a_ability.bin','item.bin'})
NATIVE_BUFFER_TABLES=frozenset({'monmagic1.bin','monmagic2.bin'})
NATIVE_U16_LIMIT=0xFFF0
NATIVE_BUFFER_LIMIT=0x25800
NATIVE_LIMITS=Path(__file__).with_name('native_limits.json')

def fingerprint(data):
    return hashlib.sha256(data).hexdigest()

def native_limit(member,extended=False):
    """Largest payload the native kernel loader holds for this member, or None.

    `extended` builds for a runtime whose 32-bit kernel table adapter is armed;
    without it the DLL keeps the mod's own table for anything larger.
    """
    folder,_,leaf=member.lower().rpartition('/')
    if not folder.endswith('/battle/kernel'):return None
    if extended and leaf in EXTENDED_KERNEL_TABLES:return NATIVE_BUFFER_LIMIT
    if leaf in NATIVE_U16_TABLES:return NATIVE_U16_LIMIT
    return NATIVE_BUFFER_LIMIT if leaf in NATIVE_BUFFER_TABLES else None

def load_native_limits(path=NATIVE_LIMITS):
    """Reviewed slots that stay native when a table would exceed its native size."""
    data=json.loads(Path(path).read_text(encoding='utf-8'))
    return {request:{(int(row),int(slot)) for row,slot in entry['slots']} for request,entry in data['keep_native'].items()}

def native_member(request):
    canonical=request.replace('\\','/').lower()
    if not canonical.startswith('/ffx_data/'):
        raise AssetError('Invalid virtual resource root')
    member=canonical[len('/ffx_data/'):] if canonical.startswith('/ffx_data/ffx_ps2/') else canonical[1:]
    if any(part in ('','.','..') for part in member.split('/')):
        raise AssetError('Invalid native resource path')
    return member

def overlay_index(overlay):
    """Index the overlay by lower-case member, like the File Loader's Win32 lookup.

    A case-sensitive Path.is_file() misses e.g. FFX_Data/GameData/PS3Data/.../D3D11/
    while the game still reads that file. Case-only duplicates have no single
    meaning and redirected entries are never followed."""
    overlay=Path(overlay);files={};folders=set();redirected=set()
    for directory,subfolders,leaves in os.walk(overlay):
        base=Path(directory)
        for name in subfolders+leaves:
            path=base/name;key=path.relative_to(overlay).as_posix().lower()
            if path.is_symlink():redirected.add(key);continue
            table=folders if name in subfolders else files
            if key in folders or key in files:raise AssetError('Overlay path is ambiguous without case: '+key)
            if table is folders:folders.add(key)
            else:files[key]=path
    return {'files':files,'redirected':redirected}

def resolve_overlay(index,member):
    parts=member.lower().split('/')
    for count in range(1,len(parts)+1):
        if '/'.join(parts[:count]) in index['redirected']:raise AssetError('Redirected overlay entry: '+member)
    return index['files'].get('/'.join(parts))

def scripts(data,layout):
    if layout.container=='macro':
        return {key:value for key,value in macro_scripts(data).items() if value is not None}
    if layout.container=='field':
        identities=((row,slot) for row in range(field_layout(data)//8) for slot in range(2))
    else:
        low,high,_,_=kernel_layout(data,layout)
        identities=((row,slot) for row in range(low,high+1) for slot in range(layout.slots))
    result={}
    for row,slot in identities:
        try:result[row,slot]=edit_script(data,{'row':row,'slot':slot},layout)
        except AssetError:pass  # Inactive optional references remain untouched.
    return result

def rebase(source,translated,effective,layout,widths,keep_native=frozenset()):
    if layout.container=='field' and len(effective)>=36 and struct.unpack_from('<I',effective)[0]==8 and struct.unpack_from('<I',effective,32)[0]==len(effective):
        raise AssetError('Mod supplies a complete battle container instead of a standalone text bank')
    before,after,current=scripts(source,layout),scripts(translated,layout),scripts(effective,layout)
    edits=[];preserved=0
    for key,value in current.items():
        if key not in before or value!=before[key]:
            preserved+=1;continue
        if key not in after or after[key]==value or key in keep_native:continue
        text=decode(after[key],api=4)
        if encode(text,api=4)!=after[key]:raise AssetError('Translation is not byte-reversible')
        edits.append({'row':key[0],'slot':key[1],'text':text})
    result=rebuild_resource(effective,edits,layout,api=4)
    validate_edits(effective,result,edits,widths,layout=layout,api=4)
    # The existing authoring/validator contract preserves all non-text bytes.
    # Check it explicitly for indexed tables, including every new mod command.
    if layout.container=='indexed':
        low,_,stride,pool=kernel_layout(effective,layout)
        mutable={20+(e['row']-low)*stride+e['slot']*layout.offset_step+i for e in edits for i in (0,1)}
        if any(a!=b and i not in mutable for i,(a,b) in enumerate(zip(effective[:pool],result[:pool]))):
            raise AssetError('Rebase changed non-text metadata')
    return result,len(edits),preserved

def build(package:Path,reference:Path,overlay:Path,output:Path,keep_native=None,extended_kernel_tables=False):
    package=package.resolve(strict=True);reference=reference.resolve(strict=True);overlay=overlay.resolve(strict=True)
    output=output.resolve()
    if output.exists() or any(output==p or p in output.parents for p in (package,reference,overlay)):
        raise AssetError('Use a new output outside all input roots')
    original=json.loads((package/'manifest.json').read_text())
    if original['schema_version']!=4 or original['hook_api']!=4:raise AssetError('API 4 package required')
    manifest=copy.deepcopy(original);metrics=next(r for r in original['resources'] if r['family']=='font_metrics')
    widths=(package/metrics['path']).read_bytes()[64:272]
    planned=[];report=[];index=overlay_index(overlay)
    keep_native=load_native_limits() if keep_native is None else keep_native
    for item in manifest['resources']:
        source=(reference/item['path']).read_bytes();target=(package/item['path']).read_bytes()
        if fingerprint(source)!=item['source_sha256'] or fingerprint(target)!=item['sha256']:
            raise AssetError('Input package/reference identity changed')
        virtual=native_member(item['request'])
        if '..' in Path(virtual).parts or Path(virtual).is_absolute():raise AssetError('Invalid overlay path')
        path=resolve_overlay(index,virtual)
        effective=path.read_bytes() if path else source
        row={'id':item['id'],'request':item['request'],'path':item['path'],'sourceSha256':fingerprint(effective)}
        if effective!=source:
            if item['family']=='ui_texture':
                # The DLL admits only examined texture compilations; a mod texture
                # cannot be re-translated here. Leave the mod's texture in charge.
                report.append({**row,'status':'native-overlay-retained','reason':'Mod replaces an examined UI texture'})
                continue
            if not virtual.startswith(MASTER):raise AssetError('An examined font or graphic source was replaced')
            layout=describe_resource(virtual[len(MASTER):])
            try:
                target,count,preserved=rebase(source,target,effective,layout,widths)
            except (AssetError,ValueError,IndexError,struct.error) as error:
                report.append({**row,'status':'native-overlay-retained','reason':str(error)})
                continue
            item.update(source_size=len(effective),source_sha256=fingerprint(effective),size=len(target),sha256=fingerprint(target))
            row.update(status='rebased-text-only',translatedScripts=count,preservedModScripts=preserved)
        else:row['status']='unchanged'
        limit=native_limit(virtual,extended_kernel_tables)
        if limit is not None and len(target)>limit:
            # Never ship a table the native loader would truncate. Reviewed slots may
            # stay native to fit; otherwise the mod's own table stays in charge.
            kept=keep_native.get(item['request'],set())
            layout=describe_resource(virtual[len(MASTER):])
            if kept:
                target,count,preserved=rebase(source,target,effective,layout,widths,kept)
            if not kept or len(target)>limit:
                report.append({**row,'status':'native-overlay-retained','reason':'Translation exceeds the native kernel loader size (%d bytes)'%limit})
                continue
            item.update(source_size=len(effective),source_sha256=fingerprint(effective),size=len(target),sha256=fingerprint(target))
            row.update(status='rebased-text-only',translatedScripts=count,preservedModScripts=preserved,keptNative=len(kept),nativeLimit=limit)
        planned.append((item,effective,target));report.append(row)
    manifest['resources']=[item for item,_,_ in planned]
    manifest['display_name']='Português (Brasil) - local mod compatibility'
    (output/'pack').mkdir(parents=True);(output/'reference').mkdir()
    for item,source,target in planned:
        for directory,data in [('pack',target),('reference',source)]:
            path=output/directory/item['path'];path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    (output/'pack/manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    (output/'report.json').write_text(json.dumps({'author':'Jarvis-HOOK','upstreamManifestSha256':fingerprint((package/'manifest.json').read_bytes()),
        'overlay':str(overlay),'kernelTables':'32bit' if extended_kernel_tables else '16bit','resources':report},ensure_ascii=False,indent=2)+'\n')
    return {status:sum(r['status']==status for r in report) for status in sorted({r['status'] for r in report})}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('package','reference','overlay','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--native-limits',type=Path,default=NATIVE_LIMITS)
    parser.add_argument('--extended-kernel-tables',action='store_true',
                        help='Build for a DLL whose 32-bit kernel table adapter is enabled')
    args=parser.parse_args()
    print(json.dumps(build(args.package,args.reference,args.overlay,args.output,load_native_limits(args.native_limits),
                           args.extended_kernel_tables),indent=2))
