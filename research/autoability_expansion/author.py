#!/usr/bin/env python3
"""Jarvis-HOOK: append named Spira/Aeon records to hash-pinned local tables.

No equipment, recipe, config or executable is modified. Input bytes and backups
stay outside Git. The previous MOD-002 authoring tool supplies the format precedent.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import tempfile

HERE = Path(__file__).resolve().parent
DEFINITIONS = HERE / "definitions.json"
INVENTORY = HERE / "source_inventory.json"
HEADER, ABILITY_STRIDE, RATE_STRIDE = 20, 108, 4
ASIAN = {"jppc", "new_jppc", "new_chpc", "new_krpc"}
LATIN = {"inpc", "new_uspc", "new_depc", "new_frpc", "new_itpc", "new_sppc"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def header(data, stride, last):
    if len(data) < HEADER or struct.unpack_from("<I", data)[0] != 1:
        raise ValueError("Invalid table header")
    lo, hi, width, length = struct.unpack_from("<4H", data, 8)
    offset = struct.unpack_from("<I", data, 16)[0]
    if (lo, hi, width, length, offset) != (0, last, stride, (last+1)*stride, HEADER):
        raise ValueError("Unexpected range, stride or data bounds")
    if HEADER + length > len(data):
        raise ValueError("Truncated table")


def encode(text):
    special = {" ":58, "%":63, "’":65, "+":69, "-":71, ".":72, "/":73}
    result = bytearray()
    for c in text.replace("'", "’"):
        if "0" <= c <= "9": result.append(ord(c))
        elif "A" <= c <= "Z" or "a" <= c <= "z": result.append(ord(c)+15)
        elif c in special: result.append(special[c])
        else: raise ValueError(f"Unsupported Latin glyph {c!r}")
    return bytes(result)


def definitions():
    return validate_definitions(json.loads(DEFINITIONS.read_text()))


def validate_definitions(raw):
    rows = raw["definitions"]
    if raw["schema"] != 1 or [r["id"] for r in rows] != list(range(148, 175)):
        raise ValueError("Definition ID range must be 148..174")
    if len({r["key"] for r in rows}) != len(rows) or len({r["name"] for r in rows}) != len(rows):
        raise ValueError("Duplicate key/name")
    for row in rows:
        if row["equipment_word"] != f'0x{0x8000+row["id"]:04X}' or row["arms_rate"] != 0:
            raise ValueError("Unexpected equipment word/rate")
        if row["allowed_owners"] is not None and row["writes"]:
            raise ValueError("Exclusive abilities must remain native-neutral")
        if row["payload_mode"] == "hook_only" and row["writes"]:
            raise ValueError("Hook-only record cannot grant native effects")
        payload(row)
        encode(row["name"]); encode(row["description"])
    return rows


def payload(row):
    data = bytearray(ABILITY_STRIDE)
    fields = {"element_strike":(0x11,1), "element_absorb":(0x12,1),
              "stat_amount":(0x55,1), "stat_mask":(0x56,2), "flags64":(0x64,2)}
    for key, value in row["writes"].items():
        if key not in fields or type(value) is not int:
            raise ValueError("Unsupported payload field")
        at, width = fields[key]
        if not 0 <= value < 1 << (8*width): raise ValueError("Payload value out of range")
        data[at:at+width] = value.to_bytes(width,"little")
    return data


def build(original, kind, locale, rows):
    if locale not in ASIAN | LATIN: raise ValueError("Unreviewed locale")
    stride = ABILITY_STRIDE if kind == "a_ability" else RATE_STRIDE if kind == "arms_rate" else 0
    if not stride: raise ValueError("Unknown table kind")
    header(original, stride, 147)
    if [r["id"] for r in rows] != list(range(148,175)): raise ValueError("ID gap/collision")
    old_end = HEADER + 148*stride
    old_data, tail = original[HEADER:old_end], original[old_end:]
    new_rows = bytearray()
    expected_text = {}
    if kind == "a_ability":
        pool = bytearray(tail)
        empty = len(pool); pool.append(0)
        keys = tuple(old_data[147*stride+at:147*stride+at+2] for at in (2,6,10,14))
        def append(value):
            if not value: return empty
            offset = len(pool)
            if offset > 65535: raise ValueError("Text offset overflow")
            pool.extend(value); pool.append(0)
            return offset
        for row in rows:
            data = payload(row)
            name = str(row["id"]).encode("ascii") if locale in ASIAN else encode(row["name"])
            desc = b"" if locale in ASIAN else encode(row["description"])
            for at, value in ((0,append(name)),(4,empty),(8,append(desc)),(12,empty)):
                struct.pack_into("<H",data,at,value)
            for at, key in zip((2,6,10,14),keys): data[at:at+2]=key
            expected_text[row["id"]]=(name,desc)
            new_rows.extend(data)
        if len(pool)>65535: raise ValueError("Text pool overflow")
        new_tail=bytes(pool)
    else:
        new_rows.extend(b"\0"*(len(rows)*stride))
        new_tail=tail
    result=bytearray(original[:HEADER]+old_data+new_rows+new_tail)
    struct.pack_into("<H",result,10,174)
    struct.pack_into("<H",result,14,175*stride)
    header(result,stride,174)
    if result[HEADER:old_end]!=old_data: raise AssertionError("Existing records changed")
    new_end=HEADER+175*stride
    if result[new_end:new_end+len(tail)]!=tail: raise AssertionError("Existing tail/text changed")
    for row in rows:
        actual=result[HEADER+row["id"]*stride:HEADER+(row["id"]+1)*stride]
        if kind=="a_ability":
            if actual[16:]!=payload(row)[16:]: raise AssertionError("Wrong gameplay payload")
            for at,expected in ((0,expected_text[row["id"]][0]),(8,expected_text[row["id"]][1]),(4,b""),(12,b"")):
                offset=struct.unpack_from("<H",actual,at)[0]
                if offset>=len(new_tail) or new_tail[offset:].split(b"\0",1)[0]!=expected:
                    raise AssertionError("Text/auxiliary pointer mismatch")
        elif any(actual): raise AssertionError("New prices must be neutral")
    return bytes(result)


def targets():
    rows=json.loads(INVENTORY.read_text())["targets"]
    if len(rows)!=25 or sum(r["kind"]=="a_ability" for r in rows)!=13:
        raise ValueError("Wrong target inventory")
    if len({r["path"] for r in rows})!=25: raise ValueError("Duplicate target path")
    return rows


def plan():
    defs=definitions(); result=[]
    for entry in targets():
        p=Path(entry["path"])
        if not p.is_file() or p.is_symlink(): raise ValueError(f"Unsupported target {p}")
        data=p.read_bytes()
        if digest(data)!=entry["sha256"] or len(data)!=entry["size"]:
            raise ValueError(f"Input drift at {p}")
        result.append({**entry,"before":data,"after":build(data,entry["kind"],entry["locale"],defs)})
    return result


def atomic_write(path,data,mode):
    fd,tmp=tempfile.mkstemp(prefix=".aa-expansion-",dir=path.parent)
    try:
        with os.fdopen(fd,"wb") as stream:
            stream.write(data); stream.flush(); os.fsync(stream.fileno())
        os.chmod(tmp,mode)
        os.replace(tmp,path)
        directory=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
        try: os.fsync(directory)
        finally: os.close(directory)
    finally:
        if os.path.exists(tmp): os.unlink(tmp)


def require_apps_closed():
    wanted={"ffx.exe","ffxprojecteditor","ffxprojecteditor.dll"}
    for p in Path("/proc").iterdir():
        if not p.name.isdigit() or int(p.name)==os.getpid(): continue
        try: args=(p/"cmdline").read_bytes().split(b"\0")
        except (OSError,PermissionError): continue
        if any(Path(a.decode(errors="replace")).name.lower() in wanted for a in args if a):
            raise RuntimeError(f"Close game/editor process {p.name} before asset writing")


def apply():
    require_apps_closed(); rows=plan()
    stamp=datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup=Path('/home/wanderson/.codex/backups')/f'autoability-expansion-{stamp}'
    backup.mkdir(parents=True,exist_ok=False)
    manifest={"schema":1,"created_utc":stamp,"definitions_sha256":digest(DEFINITIONS.read_bytes()),
              "first_id":148,"last_id":174,"state":"prepared","records":[]}
    for r in rows:
        rel=Path(r["group"])/r["locale"]/f'{r["kind"]}.bin';dest=backup/rel
        dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(r["before"])
        if digest(dest.read_bytes())!=r["sha256"]: raise IOError("Backup mismatch")
        manifest['records'].append({k:r[k] for k in ('group','locale','kind','path','mode')}|{
            'backup':str(rel),'before_sha256':r['sha256'],'after_sha256':digest(r['after']),
            'before_size':len(r['before']),'after_size':len(r['after'])})
    mp=backup/'manifest.json';mp.write_text(json.dumps(manifest,indent=2)+'\n')
    changed=[]
    try:
        require_apps_closed()
        for r in rows:
            p=Path(r['path'])
            if digest(p.read_bytes())!=r['sha256']: raise IOError(f'Input drift at {p}')
        for r in rows:
            p=Path(r['path'])
            if digest(p.read_bytes())!=r['sha256']: raise IOError(f'Concurrent edit at {p}')
            # Track before replace: fsync can fail after the new file became visible.
            changed.append(r);atomic_write(p,r['after'],r['mode'])
            if p.read_bytes()!=r['after']: raise IOError(f'Readback mismatch at {p}')
        manifest['state']='applied';mp.write_text(json.dumps(manifest,indent=2)+'\n')
    except BaseException:
        conflicts=[]
        for r in reversed(changed):
            p=Path(r['path'])
            current=digest(p.read_bytes())
            if current==digest(r['after']): atomic_write(p,r['before'],r['mode'])
            elif current!=r['sha256']: conflicts.append(str(p))
        manifest['state']='rolled_back' if not conflicts else 'rollback_conflict'
        manifest['rollback_conflicts']=conflicts;mp.write_text(json.dumps(manifest,indent=2)+'\n')
        raise
    print(json.dumps(verify(mp),indent=2))


def read_manifest(path):
    m=json.loads(path.read_text());allowed={r['path'] for r in targets()}
    if m['schema']!=1 or len(m['records'])!=25 or {r['path'] for r in m['records']}!=allowed:
        raise ValueError('Manifest target mismatch')
    for r in m['records']:
        backup=(path.parent/r['backup']).resolve()
        if not backup.is_relative_to(path.parent.resolve()) or digest(backup.read_bytes())!=r['before_sha256']:
            raise ValueError('Backup path/hash mismatch')
    return m


def verify(path):
    m=read_manifest(path)
    if m['definitions_sha256']!=digest(DEFINITIONS.read_bytes()): raise ValueError('Definition drift')
    defs=definitions()
    for r in m['records']:
        before=(path.parent/r['backup']).read_bytes();after=Path(r['path']).read_bytes()
        if digest(after)!=r['after_sha256'] or after!=build(before,r['kind'],r['locale'],defs):
            raise AssertionError(f'Verification mismatch: {r["path"]}')
    return {'manifest':str(path),'files_verified':25,'ability_tables':13,'rate_tables':12,
            'new_ids':'148..174','existing_ids_preserved':'0..147','new_records_per_table':27,
            'gameplay_validation':'not performed; data only'}


def rollback(path):
    require_apps_closed();m=read_manifest(path)
    for r in m['records']:
        if digest(Path(r['path']).read_bytes()) not in (r['before_sha256'],r['after_sha256']):
            raise ValueError(f'Refusing to overwrite later edits: {r["path"]}')
    for r in m['records']:
        p=Path(r['path']);b=(path.parent/r['backup']).read_bytes()
        if digest(p.read_bytes())==r['after_sha256']:atomic_write(p,b,r['mode'])
        if p.read_bytes()!=b:raise IOError('Rollback readback failed')
    print('ROLLBACK verified 25 original files')


def main():
    ap=argparse.ArgumentParser(description=__doc__);mode=ap.add_mutually_exclusive_group()
    mode.add_argument('--apply',action='store_true');mode.add_argument('--verify',type=Path)
    mode.add_argument('--rollback',type=Path);mode.add_argument('--stage',type=Path)
    args=ap.parse_args()
    if args.apply:apply()
    elif args.verify:print(json.dumps(verify(args.verify),indent=2))
    elif args.rollback:rollback(args.rollback)
    else:
        rows=plan()
        if args.stage:
            args.stage.mkdir(parents=True,exist_ok=False)
            for r in rows:
                p=args.stage/r['group']/r['locale']/f'{r["kind"]}.bin'
                p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(r['after'])
        print(json.dumps({'mode':'staged' if args.stage else 'dry-run','files':len(rows),
                          'ids':'148..174','new_records':27,'existing_records_and_text':'preserved',
                          'stage':str(args.stage) if args.stage else None}))


if __name__=='__main__':main()
