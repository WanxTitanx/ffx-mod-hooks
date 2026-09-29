#!/usr/bin/env python3
"""Read-only save corpus through original load-copy, grid hydrate and pack.

The menu has 1024 reserved nodes/links. This proves state transport, NOT actual
layout selection, rendering, stat application, live persistence or a new runtime.
Save bytes are copied only into Unicorn memory; nothing rewrites corpus files.
"""
from __future__ import annotations
import argparse
import gc
import hashlib
import json
from pathlib import Path
import re
import struct
import time
from native_harness import StateFixture, CURSORS, NODE_BASE, NODE_STRIDE, LINK_BASE, LINK_STRIDE, STATE_OFFSET
from pe_image import read_exact, EXPECTED_SHA256


def check(condition, detail):
    if not condition:
        raise AssertionError(detail)


def corpus(manifest_path: Path):
    manifest=json.loads(manifest_path.read_text())
    entries=manifest['files']+manifest['archive_members']
    selected=sorted({e['sha256'] for e in entries if e['size']==26880})
    check(selected, 'no native-sized FFX saves in manifest')
    result=[]
    for digest in selected:
        check(re.fullmatch('[0-9a-f]{64}',digest) is not None,'invalid corpus digest')
        path=manifest_path.parent/'corpus'/digest
        check(not path.is_symlink(),'symlink corpus entry')
        data=path.read_bytes()
        check(len(data)==26880 and hashlib.sha256(data).hexdigest()==digest,'source size/hash mismatch')
        result.append((digest,data))
    return manifest,result


def selected_state(f, expected):
    m=f.m;state=expected[64+STATE_OFFSET:64+STATE_OFFSET+0x1320]
    # Check each field against the actual source, not a model seeded by the test.
    for i in range(1024):
        at=f.menu.address+NODE_BASE+i*NODE_STRIDE
        content=state[2*i]
        check(m.get(at+6,2)==struct.pack('<H',0xffff if content==255 else content),f'node content {i}')
        check(m.get(at+0x21,1)==state[2*i+1:2*i+2],f'activation mask {i}')
        check(m.get(f.menu.address+LINK_BASE+i*LINK_STRIDE+12,1)==state[0xa00+i:0xa01+i],f'link state {i}')
    for i,offset in enumerate(CURSORS):
        check(m.get(f.menu.address+offset,2)==state[0xf00+2*i:0xf02+2*i],f'16-bit cursor {i}')
    check(m.get(f.menu.address+71115,2)==state[0xf18:0xf1a],'view bytes')


def native_roundtrip(f, source_handle, data):
    m=f.m
    m.put(source_handle.address,data)
    m.put(f.save.address,b'\xa5'*f.save.length)
    m.call(0x8b5450,(f.save.address,source_handle.address))
    check(m.get(f.save.address,f.save.length)==data[64:],'native full payload copy')
    check(m.get(source_handle.address,len(data))==data,'native load changed source image')
    m.call(0xa49590)
    selected_state(f,data)
    m.call(0xa5bb70)
    check(m.get(f.save.address,f.save.length)==data[64:],'pack changed unrelated save bytes or truncated grid state')
    m.call(0xa49590)
    selected_state(f,data)
    return {'node_records':1024,'link_records':1024,'cursor_words':7,
            'payload_bytes_unchanged':f.save.length,'source_bytes_unchanged':len(data),
            'dependencies':sorted(m.dependencies)}


def run(executable: Path, manifest_path: Path, output: Path):
    check(not output.exists(),'output already exists')
    mapped=read_exact(executable)
    manifest,saves=corpus(manifest_path)
    results=[]
    for digest,data in saves:
        started=time.monotonic()
        try:
            f=StateFixture(mapped,1024,1024)
            source=f.m.alloc(26880,'read-only-source copy in emulator')
            result=native_roundtrip(f,source,data)
            results.append({'sha256':digest,'status':'passed','seconds':time.monotonic()-started,'evidence':result})
        except Exception as error:
            results.append({'sha256':digest,'status':'failed','error':str(error)})
        finally:
            if 'f' in locals():del f
            gc.collect()
        print(digest[:12],results[-1]['status'],flush=True)
    switches=[]
    f=StateFixture(mapped,1024,1024)
    source=f.m.alloc(26880,'reused native input buffer')
    # One machine and one input address: A..Z..A exposes retained earlier state.
    sequence=saves+list(reversed(saves))+saves[:1]
    for step,(digest,data) in enumerate(sequence):
        try:
            native_roundtrip(f,source,data)
            switches.append({'step':step,'sha256':digest,'status':'passed'})
        except Exception as error:
            switches.append({'step':step,'sha256':digest,'status':'failed','error':str(error)})
    preserved=all((manifest_path.parent/'corpus'/digest).read_bytes()==data for digest,data in saves)
    passed=sum(r['status']=='passed' for r in results)
    switching=sum(r['status']=='passed' for r in switches)
    report={'schema':1,'source_repository':manifest['repository'],'source_revision':manifest['revision'],
            'executable_sha256':EXPECTED_SHA256,'real_saves_passed':passed,'real_saves_total':len(results),
            'switches_passed':switching,'switches_total':len(switches),'sources_preserved':preserved,
            'scope':'original 8B5450/A49590/A5BB70 with synthetic reserved menu, guarded heap and explicit dependencies',
            'render_or_full_gameplay_test':False,'native_disk_io_executed':False,'production_ready':False,
            'results':results,'switches':switches}
    with output.open('x') as stream:json.dump(report,stream,indent=2);stream.write('\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('results','switches')},indent=2))
    return 0 if passed==len(results) and switching==len(switches) and preserved else 1


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    raise SystemExit(run(args.executable,args.manifest,args.output))
