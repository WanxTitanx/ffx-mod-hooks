#!/usr/bin/env python3
"""Original FFX instructions vs actual C++ core. No live process or save writes."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT/'research/sphere_grid_recovery'))
from pe_image import read_exact
from native_machine import Machine

def run(executable: Path,output: Path) -> int:
    output.mkdir(parents=False,exist_ok=False)
    bridge=ROOT/'src/runtime/FfxHooksDll/tests/SeymourCompatibilityNativeBridge.cpp'
    library=output/'core-test.so'
    built=subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared',str(bridge),'-o',str(library)],capture_output=True,timeout=60)
    (output/'build.log').write_bytes(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError('test bridge compilation failed')
    core=ctypes.CDLL(str(library.resolve()))
    core.SeymourTestFilter.argtypes=(ctypes.c_int,ctypes.c_uint,ctypes.c_int)
    core.SeymourTestFilter.restype=ctypes.c_int
    core.SeymourTestVisibility.argtypes=(ctypes.c_uint,ctypes.c_uint)
    core.SeymourTestVisibility.restype=ctypes.c_uint
    image=read_exact(executable);m=Machine(image)
    spans=((0x79a5c0,0x79a690),(0x794030,0x79406b),(0x7ad5f0,0x7ad64d),(0x7abbf0,0x7abc77))
    m.allowed_code=list(spans)
    actors=m.alloc(31*0xf90,'private native player records');m.set32(0x11334cc,actors.address)
    checks=0;counts={'native_queries':0,'restricted_seymour_queries':0,'native_visibility':0,'native_slot7_noop':0}
    def check(ok,reason):
        nonlocal checks
        checks+=1
        if not ok:raise AssertionError(reason)
    try:
        # 0x30FF invokes separate summon logic. It is not silently mocked here.
        denied={0x3017,0x3024,0x3025,0x3026,0x302a}
        for actor in range(18):
            for command in range(0x3000,0x30ff):
                at=actors.address+actor*0xf90+0x690+2*((command&0xfff)//16)
                for bit in (0,1):
                    data=struct.pack('<H',bit*(1<<(command&15)));m.put(at,data)
                    m.call(0x79a5c0,(actor,command));result=m.cpu.reg_read(m.r.UC_X86_REG_EAX)
                    check(result==bit,('native query',actor,command,bit,result))
                    check(m.get(at,2)==data,('query changed mask',actor,command))
                    after=core.SeymourTestFilter(actor,command,result)
                    expected=1 if actor==7 and command in denied else bit
                    check(after==expected,('filter/native mismatch',actor,command,bit,after))
                    counts['native_queries']+=1;counts['restricted_seymour_queries']+=int(after!=result)
        base=0x1130f2c;save=0x112ca90;length=0x68c0
        for actor in (0,6,7):
            m.put(0x113205c+actor*0x94+0x2d,bytes((12,13)))
            for flags in range(256):
                for enable in (0,1,2,3,128,255):
                    gear=bytearray([0x5a]*44)
                    for i in (0,1):gear[22*i+2:22*i+7]=bytes((1,flags,actor,i,actor))
                    m.put(base+12*22,bytes(gear));before=m.get(save,length)
                    m.call(0x7ad5f0,(actor,enable));expected=bytearray(before)
                    transformed=core.SeymourTestVisibility(flags,enable)
                    if actor!=7:
                        for offset in (3,25):expected[base+12*22+offset-save]=transformed
                    check(m.get(save,length)==bytes(expected),('visibility bytes',actor,flags,enable))
                    check((transformed&~2)==(flags&~2),('unrelated flag',flags,enable))
                    counts['native_visibility']+=1;counts['native_slot7_noop']+=int(actor==7)
        report={'status':'passed','checks':checks,'groups':counts,'exe_sha256':image.sha256,
                'original_instruction_hashes':{f'{lo:08x}-{hi:08x}':hashlib.sha256(m.get(lo,hi-lo)).hexdigest() for lo,hi in spans},
                'core_sha256':hashlib.sha256((ROOT/'src/runtime/FfxHooksDll/hooks/SeymourCompatibilityCore.h').read_bytes()).hexdigest(),
                'bridge_sha256':hashlib.sha256(bridge.read_bytes()).hexdigest(),
                'scope':'native disabled-mask query; native visibility exclusion and core bit equivalence',
                'native_math_substituted':False,'runtime_hook_integrated':False,'live_game_test':False}
        (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return 0
    except Exception as error:
        report={'status':'failed','checks_before_failure':checks,'groups':counts,'error':str(error),'runtime_hook_integrated':False}
        (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return 1
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();raise SystemExit(run(a.executable,a.output))
