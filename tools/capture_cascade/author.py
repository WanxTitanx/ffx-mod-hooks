"""Jarvis-HOOK: author private Cascade assets from a read-only vanilla VBF."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from tools.text_languages.asset_io import VbfArchive, u16, u32

MASTER = 'ffx_ps2/ffx/master/jppc'
ROOT = Path(__file__).resolve().parents[2]


def container(source: bytes) -> tuple[int, ...]:
    if len(source) < 0x34 or u32(source, 0) != 8 or u32(source, 0x20) != len(source):
        raise ValueError('Unsupported eight-section game container')
    pointers = tuple(u32(source, offset) for offset in range(4, 0x24, 4))
    nonempty=tuple(p for p in pointers if p)
    if pointers[0] < 0x24 or any(a > b for a,b in zip(nonempty,nonempty[1:])):
        raise ValueError('Invalid section order')
    return pointers


def grant_policy(policy):
    if not isinstance(policy, dict) or set(policy) != {'remove', 'merge'}:
        raise ValueError('Explicit equipment grant policy required')
    remove = set(policy['remove']); merge = {int(a): b for a, b in policy['merge'].items()}
    if any(type(word) is not int or not 0x8000 <= word <= 0x8FFF for word in remove | set(merge) | set(merge.values())) or remove & set(merge) or any(a == b or b in remove or b in merge for a, b in merge.items()):
        raise ValueError('Invalid equipment grant policy')
    return remove, merge


def sanitize_grants(source: bytes, policy) -> bytes:
    if policy is None:
        return source
    remove, merge = grant_policy(policy)
    pointers = container(source)
    loot = pointers[4]; end = next((p for p in pointers[5:] if p), 0)
    # Monster_Loot.cs: 0x32 fixed header, seven character pairs of eight
    # weapon/eight armour words. GearChance and all other loot fields stay put.
    if not loot or loot + 0x112 > end or end > len(source):
        raise ValueError('Invalid monster equipment grant extent')
    after = bytearray(source)
    for group in range(14):
        at = loot + 0x32 + group * 16
        words = list(struct.unpack_from('<8H', source, at))
        present = set(words) - remove - set(merge)
        for i, word in enumerate(words):
            if word in remove:
                words[i] = 0
            elif word in merge:
                replacement = merge[word]
                words[i] = 0 if replacement in present else replacement
                present.add(replacement)
        struct.pack_into('<8H', after, at, *words)
    return bytes(after)


def verify_grants(data: bytes, policy):
    remove, merge = grant_policy(policy)
    if len(data) < 20 or u32(data, 0) != 1 or u16(data, 12) != 8 or u32(data, 16) != 20 or len(data) != 20 + u16(data, 14) or u16(data, 14) != 8 * (u16(data, 10) + 1):
        raise ValueError('Invalid effective native customization table')
    targets = {target for kind, target, _, _ in struct.iter_unpack('<4H', data[20:]) if kind & 3}
    if targets & (remove | set(merge)) or not set(merge.values()) <= targets:
        raise ValueError('Equipment grant policy disagrees with the effective customization targets')


def clone_monster(source: bytes, source_id: int, destination_id: int, hp_percent: int, grants=None) -> bytes:
    pointers = container(source)
    if not 334 <= source_id <= 343 or not 1 <= destination_id <= 999 or 334 <= destination_id <= 343:
        raise ValueError('Invalid Dark Aeon clone identity')
    if not 1 <= hp_percent <= 100:
        raise ValueError('HP percentage is outside the authoring range')
    stats, end = pointers[2], next(p for p in pointers[3:] if p)
    if not stats or stats + 0x80 > end or end > len(source) or u16(source, stats+0x72) != source_id:
        raise ValueError('Monster stat sheet identity/extent mismatch')
    if source[stats+0x78] != 255 or source[stats+0x79] != 0:
        raise ValueError('Expected a vanilla uncapturable Dark Aeon')
    hp = u32(source, stats+0x14)
    if not 0 < hp <= 0x7FFFFFFF:
        raise ValueError('Invalid native HP')
    after = bytearray(source)
    struct.pack_into('<I', after, stats+0x14, max(1,(hp*hp_percent+50)//100))
    struct.pack_into('<H', after, stats+0x72, destination_id)
    return sanitize_grants(bytes(after), grants)


def formation_slots(source: bytes) -> tuple[int, tuple[int, ...]]:
    pointers = container(source)
    formation, end = pointers[2], pointers[3]
    if formation + 28 > end or end > len(source):
        raise ValueError('Truncated/overlapping formation')
    return formation+12, struct.unpack_from('<8H',source,formation+12)


def encounter_rows(source: bytes):
    if len(source) < 16 or u32(source,0) != 2:
        raise ValueError('Unsupported encounter table')
    start, end, stop = (u32(source,x) for x in (4,8,12))
    if not 16 <= start <= end <= stop <= len(source):
        raise ValueError('Invalid encounter table extent')
    rows=[];owners={}
    for offset in range(start,end-13,14):
        field,data,formation=struct.unpack_from('<3H',source,offset)
        raw=source[offset+6:offset+12].split(b'\0')[0]
        name=raw.decode('ascii','strict')
        if len(name)!=6 or not name.isalnum():
            raise ValueError('Invalid encounter table name')
        owners.setdefault((data,formation),name)
        at=end+data
        if at+2>stop:raise ValueError('Missing encounter groups')
        p=at+2;groups=[]
        for group in range(source[at+1]):
            if p+5>stop:raise ValueError('Truncated encounter group')
            count,grace=source[p],source[p+3]
            if p+5+count*2>stop:raise ValueError('Truncated formation weights')
            for i in range(count):
                entry,weight=source[p+5+i*2:p+7+i*2]
                if grace and weight:groups.append((group,entry))
            p+=5+count*2
        rows.append((field,owners[(data,formation)],groups))
    return rows


def fnv(data):
    value=0xCBF29CE484222325
    for byte in data:value=((value^byte)*0x100000001B3)&0xFFFFFFFFFFFFFFFF
    return value


def sha(data):return hashlib.sha256(data).hexdigest()


def write_owned(path: Path, data: bytes):
    if path.exists():
        if path.is_symlink() or not path.is_file() or path.read_bytes()!=data:
            raise ValueError(f'Refusing to replace a different existing file: {path}')
        return
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('xb') as stream:stream.write(data)


def author(vbf: Path, mod: Path, report: Path, generated: Path):
    catalog=json.loads((Path(__file__).parent/'catalog.json').read_text())
    if catalog['enabledByDefault'] or {m['source'] for m in catalog['monsters']}!=set(range(334,344)):
        raise ValueError('Unexpected authoring contract')
    destinations=[m['destination'] for m in catalog['monsters']]
    if len(set(destinations))!=10:raise ValueError('Duplicate clone ID')
    outputs=[];monsters=[];formations=[];pairs=set()
    field_regions={f:r['family'] for r in catalog['regions'] for f in r['fields']}
    if len(field_regions)!=sum(len(r['fields']) for r in catalog['regions']):raise ValueError('Overlapping regions')
    with VbfArchive(vbf) as archive:
        customize_path = f'{MASTER}/battle/kernel/kaizou.bin'
        effective_customize = mod/'data/mods'/customize_path
        customize = effective_customize.read_bytes() if effective_customize.is_file() else archive.read(customize_path)
        verify_grants(customize, catalog['equipmentGrants'])
        native_capture={}
        def capturable(model):
            if model not in native_capture:
                path=f'{MASTER}/battle/mon/_m{model:03}/m{model:03}.bin'
                data=archive.read(path);pointers=container(data)
                if not pointers[2] or pointers[2]+0x80>next(p for p in pointers[3:] if p):raise ValueError('Invalid native monster identity')
                native_capture[model]=data[pointers[2]+0x78]<=103
            return native_capture[model]
        for entry in catalog['monsters']:
            source_id,destination=entry['source'],entry['destination']
            source_path=f'{MASTER}/battle/mon/_m{source_id:03}/m{source_id:03}.bin'
            destination_path=f'{MASTER}/battle/mon/_m{destination:03}/m{destination:03}.bin'
            if destination_path in archive.names:raise ValueError('Clone ID collides with a vanilla archive member')
            before=archive.read(source_path)
            after=clone_monster(before,source_id,destination,entry['hpPercent'],catalog['equipmentGrants'])
            stats=u32(before,12)
            outputs.append((mod/'data/mods'/destination_path,after))
            monsters.append(dict(entry,sourcePath=source_path,destinationPath=destination_path,
                sourceSha256=sha(before),sha256=sha(after),size=len(after),fnv64=f'{fnv(after):016x}',
                hpBefore=u32(before,stats+20),hpAfter=u32(after,stats+20),modelId=u16(before,stats+0x74),
                changedOffsets=[i for i,(a,b) in enumerate(zip(before,after)) if a!=b]))
        kernel_path=f'{MASTER}/battle/kernel/btl.bin'
        effective=mod/'data/mods'/kernel_path
        kernel=effective.read_bytes() if effective.is_file() else archive.read(kernel_path)
        for field,name,groups in encounter_rows(kernel):
            if field not in field_regions:continue
            for group,index in groups:
                battle=f'{name}_{index:02}'
                path=f'{MASTER}/battle/btl/{battle}/{battle}.bin'
                effective=mod/'data/mods'/path
                source=effective.read_bytes() if effective.is_file() else archive.read(path)
                offset,slots=formation_slots(source)
                if len(source)>65535:raise ValueError('Native formation exceeds the u16 resource owner')
                mask=0
                for slot,identity in enumerate(slots):
                    if 0x1000<identity<0x1400 and capturable(identity-0x1000):
                        mask|=1<<slot;pairs.add((field,identity-0x1000))
                formations.append({'field':field,'group':group,'formation':index,'name':battle,'size':len(source),
                    'slotsOffset':offset,'slots':list(slots),'replaceableMask':mask,'sha256':sha(source),
                    'fnv64':f'{fnv(source):016x}','source':str(effective) if effective.is_file() else path})
    if set(field_regions)-{f['field'] for f in formations}:raise ValueError('A region field has no admitted formation')
    # SHA-256 identifies the complete archive; its internal header MD5 alone is
    # not a vanilla identity. No source file is opened for writing.
    with vbf.open('rb') as stream:archive_sha=hashlib.file_digest(stream,'sha256').hexdigest()
    receipt={'author':'Jarvis-HOOK','schema':1,'vbf':str(vbf),'vbfBytes':vbf.stat().st_size,
        'vbfSha256':archive_sha,'kernelSha256':sha(kernel),'catalogSha256':sha((Path(__file__).parent/'catalog.json').read_bytes()),
        'monsters':monsters,'formations':formations,'naturalPairs':sorted(pairs),'runtimeVerified':False,
        'equipmentGrants':catalog['equipmentGrants'],'customizeSha256':sha(customize)}
    lines=['#pragma once','#include "CaptureCascadeCore.h"',
        '// Jarvis-HOOK. Vanilla bases with explicit current Spira grants and effective encounter files.',
        '// Metadata only: game assets remain in the private mod package.',
        'namespace FfxHooks::CaptureCascade::Catalog {',
        'struct Monster {unsigned source,destination,family;std::uint32_t size;std::uint64_t hash;};',
        'inline constexpr Monster monsters[]={']
    for m in monsters:lines.append(f'{{{m["source"]},{m["destination"]},{m["family"]},{m["size"]},0x{m["fnv64"]}ULL}},')
    lines+=['};','struct Region {std::uint16_t field;Aeon family;};','inline constexpr Region regions[]={']
    for field,family in sorted(field_regions.items()):lines.append(f'{{{field},static_cast<Aeon>({family})}},')
    lines+=['};','struct Formation {unsigned field,group,formation,size,slotsOffset,mask;std::uint64_t hash;const char* name;};','inline constexpr Formation formations[]={']
    for f in formations:lines.append(f'{{{f["field"]},{f["group"]},{f["formation"]},{f["size"]},{f["slotsOffset"]},{f["replaceableMask"]},0x{f["fnv64"]}ULL,"{f["name"]}"}},')
    lines+=['};','inline constexpr std::array<std::array<Candidate,2>,8> candidates{{']
    for r in catalog['regions']:
        entries=[]
        for family in r['candidates']:
            ids=[0x1000+m['destination'] for m in monsters if m['family']==family]
            padded=ids+[0]*(3-len(ids));entries.append('{static_cast<Aeon>('+str(family)+'),'+str(len(ids))+',{{'+','.join(map(str,padded))+'}}}')
        lines.append('{{'+','.join(entries)+'}},')
    lines+=['}};','inline constexpr unsigned macalaniaRooms[]={'+','.join(map(str,catalog['macalaniaRooms']))+'};',
        'inline Aeon RegionForField(unsigned field) noexcept {for(const auto& r:regions)if(r.field==field)return r.family;return Aeon::Count;}',
        'inline bool MacalaniaRoom(unsigned room) noexcept {for(auto r:macalaniaRooms)if(r==room)return true;return false;}',
        '}','']
    # Validate every destination before the first write to prevent partial output
    # when an unrelated file already occupies one of the allocated IDs.
    outputs += [(generated,'\n'.join(lines).encode()),(report,(json.dumps(receipt,indent=2)+'\n').encode())]
    for path,data in outputs:
        if path.exists() and (path.is_symlink() or not path.is_file() or path.read_bytes()!=data):
            raise ValueError(f'Output differs from existing owned artifact: {path}')
    for path,data in outputs:write_owned(path,data)
    return receipt


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vbf',required=True,type=Path)
    parser.add_argument('--mod',required=True,type=Path)
    parser.add_argument('--report',required=True,type=Path)
    parser.add_argument('--generated',type=Path,default=ROOT/'src/runtime/FfxHooksDll/hooks/CaptureCascadeCatalog.generated.h')
    args=parser.parse_args()
    receipt=author(args.vbf,args.mod,args.report,args.generated)
    print(json.dumps({'monsters':len(receipt['monsters']),'formations':len(receipt['formations']),
        'vbfSha256':receipt['vbfSha256'],'report':str(args.report),'generated':str(args.generated)}))


if __name__=='__main__':main()
