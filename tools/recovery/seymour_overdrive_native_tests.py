#!/usr/bin/env python3
"""Original x86 Overdrive bodies and real relocation planner, private emulation.

No actor, command, counter or gauge formulas are mocked. Scope/installation/F8
are separate adapter tests; this fixture supplies synthetic native records.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import itertools
import json
import os
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT/'research/sphere_grid_recovery'))
from pe_image import read_exact
from native_machine import Machine


def run(executable,output):
    output.mkdir(parents=False,exist_ok=False)
    bridge=ROOT/'src/runtime/FfxHooksDll/tests/SeymourOverdriveNativeBridge.cpp'
    so=output/'plan.so'
    compiled=subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-O2','-Wall','-Wextra',
        '-Werror','-fPIC','-shared',str(bridge),'-o',str(so)],capture_output=True,timeout=60)
    (output/'build.log').write_bytes(compiled.stdout+compiled.stderr)
    if compiled.returncode:
        raise RuntimeError('Native test bridge did not compile')
    library=ctypes.CDLL(str(so.resolve()))
    library.SeymourOdSize.argtypes=(ctypes.c_uint,)
    library.SeymourOdRva.argtypes=(ctypes.c_uint,)
    library.SeymourOdSize.restype=library.SeymourOdRva.restype=ctypes.c_uint
    library.SeymourOdBuild.argtypes=(ctypes.c_uint,)*5+(ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint)
    library.SeymourOdBuild.restype=ctypes.c_int
    image=read_exact(executable)
    m=Machine(image)
    actors=m.alloc(31*0xf90,'Overdrive native actors')
    clone_base=0x28000000
    m.cpu.mem_map(clone_base,0x10000,m.u.UC_PROT_READ|m.u.UC_PROT_EXEC)
    sizes=[library.SeymourOdSize(i) for i in range(5)]
    rvas=[library.SeymourOdRva(i) for i in range(5)]
    clones=[]
    end=clone_base
    for length in sizes:
        clones.append(end)
        end+=(length+15)&~15
    m.allowed_code=[(image.base+rva,image.base+rva+size) for rva,size in zip(rvas,sizes)]
    m.allowed_code += [(p,p+n) for p,n in zip(clones,sizes)]
    m.allowed_code += [(0x7b15a0,0x7b1712),(0x794030,0x79406b),
                       (0x79af00,0x79af1a),(0x78bfc0,0x78c002),(0x79a0d0,0x79a0ea)]
    counts={}
    def check(group,condition,detail):
        count=counts.setdefault(group,{'passed':0,'total':0})
        count['total']+=1
        if not condition:
            raise AssertionError(f'{group}: {detail}')
        count['passed']+=1
    def put16(address,value):m.put(address,struct.pack('<H',value))
    def get16(address):return struct.unpack('<H',m.get(address,2))[0]
    def reset(selected=0):
        data=bytearray(actors.length)
        saved=bytearray(18*0x94)
        for actor in range(31):
            at=actor*0xf90
            struct.pack_into('<I',data,at+0x594,1000)
            struct.pack_into('<I',data,at+0x5d0,1000)
            struct.pack_into('<I',data,at+0x6f4,100)
            data[at+0x5bb]=selected
            data[at+0x5bd]=100
            data[at+0xdc8]=int(actor<8)
            data[at+0xdd6]=1
            if actor<18:
                struct.pack_into('<17H',saved,actor*0x94+0x60,*([2]*17))
        m.put(actors.address,bytes(data))
        m.put(0x113205c,bytes(saved))
        m.set32(0x11334cc,actors.address)
        for address in (0x112c9e5,0x112c02b,0x112a916):m.put(address,b'\0')
    def snapshot():return m.get(actors.address,actors.length),m.get(0x113205c,18*0x94)
    def call(address,args=()):
        with m.borrowed((actors,)):
            m.call(address,args,instructions=20000)
        return m.cpu.reg_read(m.r.UC_X86_REG_EAX)
    try:
        for function in range(5):
            source=ctypes.create_string_buffer(m.get(image.base+rvas[function],sizes[function]))
            dest=ctypes.create_string_buffer(sizes[function])
            ok=library.SeymourOdBuild(function,image.base,clones[function],clones[0],0x7b15a0,
                                     source,sizes[function],dest,sizes[function])
            check('relocation',ok==1,function)
            m.cpu.mem_write(clones[function],dest.raw)
        cases=0
        conditions=itertools.product(range(8),range(19),(0,1),(0,1),(0,1),(0,1),(0,1,2,65535))
        for actor,mode,forced,dead,stone,battle_type,remain in conditions:
            reset()
            at=actors.address+actor*0xf90
            counter=0x113205c+actor*0x94+0x60+2*mode
            def seed():
                m.put(at+0xdcc,bytes((dead,)))
                m.put(at+0xdce,bytes((stone,)))
                m.put(0x112c9e5,bytes((battle_type,)))
                put16(counter,remain)
            seed()
            before=snapshot()
            result=call(clones[0],(actor,mode,forced))
            eligible=mode<17 and battle_type==0 and ((not dead and not stone) or forced) and remain!=65535
            check('counter_semantics',get16(counter)==(remain-1 if eligible and remain else remain),conditions)
            check('counter_semantics',m.get32(at+0x6f0)==((1<<mode) if eligible else 0),(actor,mode))
            check('counter_semantics',result==int(eligible and remain<=1),(actor,mode))
            after=snapshot()
            reset();seed()
            native=call(0x7b10d0,(actor,mode,forced))
            check('original_seven_preserved' if actor<7 else 'vanilla_slot7_noop',
                  native==(result if actor<7 else 0) and snapshot()==(after if actor<7 else before),
                  (actor,mode,forced,dead,stone,battle_type,remain))
            cases+=1
        for mode in range(17):
            reset();m.set32(0x113205c+7*0x94+0x88,1<<mode)
            put16(0x113205c+7*0x94+0x60+2*mode,1)
            check('acquired_and_repeat',call(clones[0],(7,mode,0))==0,mode)
            check('acquired_and_repeat',m.get(0x112c02b,1)==b'\0',mode)
            before=snapshot()
            check('acquired_and_repeat',call(clones[0],(7,mode,1))==0 and snapshot()==before,mode)
        for actor in (0xffffffff,8,18,263):
            reset();before=snapshot()
            check('invalid_actor',call(clones[0],(actor,0,1))==0 and snapshot()==before,actor)
        events=[]
        for function,selected in ((2,1),(3,7),(4,11),(1,13)):
            def invoke(address):
                a=actors.address
                args=(20,a+20*0xf90,0,a,100,100,1) if function==2 else (20,a+20*0xf90,0,a) if function==3 else (7,a+7*0xf90) if function==1 else ()
                return call(address,args)
            reset(selected)
            native_result=invoke(image.base+rvas[function]);native=snapshot()
            reset(selected)
            expanded_result=invoke(clones[function]);expanded=snapshot()
            expected={2:3,3:30,4:20,1:3}[function]
            check('native_events',native[0][:7*0xf90]==expanded[0][:7*0xf90] and
                  native[1][:7*0x94]==expanded[1][:7*0x94],('original seven',function))
            check('native_events',native[0][8*0xf90:]==expanded[0][8*0xf90:] and
                  native[1][8*0x94:]==expanded[1][8*0x94:],('other actors',function))
            check('native_events',native[0][7*0xf90+0x5bc]==0 and expanded[0][7*0xf90+0x5bc]==expected,
                  ('Seymour gauge',function))
            check('native_events',expanded_result==native_result+1,('return',function))
            events.append({'function':function,'original_return':native_result,
                'extended_return':expanded_result,'seymour_gauge':expected})
        report={'status':'passed','counter_cases':cases,'groups':counts,'events':events,
            'exe_sha256':image.sha256,'scope':'original native math; real C++ relocation; guarded synthetic records',
            'mocked_callees':list(m.external),'live_game':False,'runtime_adapter_tested':False}
    except Exception as error:
        report={'status':'failed','groups':counts,'error':str(error),'live_game':False}
    paths=['hooks/SeymourOverdrivePlan.h','hooks/SeymourOverdriveEvidence.generated.h','tests/SeymourOverdriveNativeBridge.cpp']
    report['source_hashes']={p:hashlib.sha256((ROOT/'src/runtime/FfxHooksDll'/p).read_bytes()).hexdigest() for p in paths}
    (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return 0 if report['status']=='passed' else 1


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    raise SystemExit(run(a.executable,a.output))
