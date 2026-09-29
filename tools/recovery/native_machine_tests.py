#!/usr/bin/env python3
"""Isolated x86 machine-code fixtures. Never starts, attaches to or patches FFX.

The original executable is read-only evidence. All writes occur in Unicorn's
private emulated address space. Synthetic external dependencies are identified
explicitly; a renderer result is not an in-game acceptance result.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'.superpowers/recovery/python'))
import pefile
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import *
from native_probe import EXPECTED_SHA256


def u32(value:int)->bytes:return struct.pack('<I',value&0xffffffff)
def aligned(value:int)->int:return (value+0xfff)&~0xfff

class Machine:
    def __init__(self,raw:bytes):
        self.pe=pefile.PE(data=raw)
        self.cpu=Uc(UC_ARCH_X86,UC_MODE_32)
        self.cpu.mem_map(0x400000,aligned(self.pe.OPTIONAL_HEADER.SizeOfImage))
        self.cpu.mem_write(0x400000,self.pe.get_memory_mapped_image())
        self.cpu.mem_map(0x30000000,0x20000)
        self.cpu.mem_map(0x3f000000,0x1000)
        self.cpu.mem_write(0x3f000000,b'\xcc')
        self.cpu.reg_write(UC_X86_REG_ESP,0x30010000)
        self.cursor=0x10000000
        self.allocations=[]
        self.writes=[]
        self.failure=None
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write)
    def alloc(self,size:int,label:str)->int:
        assert 0<size<=0x200000
        address=self.cursor
        self.cpu.mem_map(address,aligned(size))
        self.cursor+=aligned(size)+0x1000
        self.allocations.append((address,size,label))
        return address
    def read32(self,address:int)->int:return struct.unpack('<I',self.cpu.mem_read(address,4))[0]
    def set32(self,address:int,value:int):self.cpu.mem_write(address,u32(value))
    def write(self,cpu,access,address,size,value,context):
        for start,length,label in self.allocations:
            if start<=address<start+aligned(length):
                if address+size>start+length:
                    self.failure={'kind':'allocation_overrun','address':hex(address),'size':size,'allocation':label,
                                  'allocation_size':length,'eip':hex(cpu.reg_read(UC_X86_REG_EIP))}
                    cpu.emu_stop()
                self.writes.append((address,size,label))
                break
    def returning(self,value:int=0,pop:int=0):
        sp=self.cpu.reg_read(UC_X86_REG_ESP);ret=self.read32(sp)
        self.cpu.reg_write(UC_X86_REG_EAX,value&0xffffffff)
        self.cpu.reg_write(UC_X86_REG_ESP,sp+4+pop)
        self.cpu.reg_write(UC_X86_REG_EIP,ret)
    def call(self,address:int,args:tuple[int,...]=(),limit:int=200000):
        sp=0x30010000
        self.cpu.mem_write(sp,u32(0x3f000000)+b''.join(u32(x) for x in args))
        self.cpu.reg_write(UC_X86_REG_ESP,sp)
        self.failure=None
        try:self.cpu.emu_start(address,0x3f000000,count=limit)
        except UcError as exc:
            raise AssertionError(f'{exc}; eip={self.cpu.reg_read(UC_X86_REG_EIP):08x}') from exc
        if self.failure:raise AssertionError(self.failure)
        assert self.cpu.reg_read(UC_X86_REG_EIP)==0x3f000000,'native fixture did not return'


def constructor(raw:bytes)->tuple[Machine,int,list[dict]]:
    machine=Machine(raw);cpu=machine.cpu
    root=machine.alloc(0xA0,'render owner')
    cpu.reg_write(UC_X86_REG_ESI,root)
    ordinal=0
    def code(cpu,address,size,context):
        nonlocal ordinal
        sp=cpu.reg_read(UC_X86_REG_ESP)
        if address==0x630670:
            requested=machine.read32(sp+4)
            ordinal+=1
            pointer=machine.alloc(requested,f'native constructor allocation {ordinal}')
            machine.returning(pointer)
        elif address==0x94964c:
            dest,value,length=(machine.read32(sp+x) for x in (4,8,12))
            assert any(start<=dest and dest+length<=start+capacity for start,capacity,_ in machine.allocations),'native memset exceeds its allocation'
            cpu.mem_write(dest,bytes([value&255])*length);machine.returning(dest)
        elif address==0x682571:cpu.emu_stop()
    hook=cpu.hook_add(UC_HOOK_CODE,code)
    cpu.emu_start(0x6823c7,0x682571,count=10000)
    cpu.hook_del(hook)
    assert not machine.failure,machine.failure
    assert cpu.reg_read(UC_X86_REG_EIP)==0x682571
    layers=machine.read32(root+0x94);result=[]
    assert machine.read32(root+0x90)==2
    for layer in range(2):
        address=layers+layer*0x6c
        record={'vertices':machine.read32(address+4),'indices':machine.read32(address+8)}
        for label,offset,stride in [('positions',12,12),('colors',20,16),('uv',24,8),('indices',28,2)]:
            pointer=machine.read32(address+offset)
            allocation=next((length for start,length,_ in machine.allocations if start==pointer),None)
            assert allocation is not None,(label,hex(pointer))
            record[label+'_bytes']=allocation
            expected=record['indices' if label=='indices' else 'vertices']*stride
            assert allocation==expected,(label,allocation,expected)
        assert record['vertices']==861*4 and record['indices']==861*6
        result.append(record)
    return machine,root,result


def gateway()->dict:
    with tempfile.TemporaryDirectory(prefix='ffx-gateway-') as temporary:
        temporary=Path(temporary);source=temporary/'emit.cpp';binary=temporary/'emit'
        source.write_text('#include "src/runtime/FfxHooksDll/hooks/NulWardCore.h"\n#include <cstdio>\nint main(){auto code=FfxHooks::NulWard::BuildWritebackGateway(0x10000000,0x11000000,0x20000000);return std::fwrite(code.bytes.data(),1,code.size,stdout)==code.size?0:1;}\n')
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-I',str(ROOT),str(source),'-o',str(binary)],check=True)
        emitted=subprocess.check_output([str(binary)])
    cpu=Uc(UC_ARCH_X86,UC_MODE_32)
    for address,size in [(0x10000000,0x1000),(0x11000000,0x1000),(0x20000000,0x1000),(0x21000000,0x1000),(0x30000000,0x20000),(0x40000000,0x3000)]:cpu.mem_map(address,size)
    cpu.mem_write(0x10000000,emitted)
    # Deliberately clobber all volatile integer state, XMM0, direction/condition
    # flags and x87 control. A correct gateway restores everything except EAX.
    callback=bytes.fromhex('b9000000ee ba111111dd 0f57c0 dbe3 fd b85b000000 c3')
    cpu.mem_write(0x11000000,callback)
    cpu.mem_write(0x20000000,u32(0x21000000))
    cpu.mem_write(0x21000000,bytes.fromhex('8906018150060000'))
    frame=0x30018000;sp=0x30010000
    values={UC_X86_REG_EAX:123,UC_X86_REG_EBX:0x12345678,UC_X86_REG_ECX:0x400006e4,
            UC_X86_REG_EDX:0x23456789,UC_X86_REG_ESI:0x40002000,UC_X86_REG_EDI:0x3456789a,
            UC_X86_REG_EBP:frame,UC_X86_REG_ESP:sp}
    for reg,value in values.items():cpu.reg_write(reg,value)
    cpu.reg_write(UC_X86_REG_EFLAGS,0x246)
    cpu.reg_write(UC_X86_REG_FPCW,0x27f)
    xmm=0x112233445566778899aabbccddeeff00;cpu.reg_write(UC_X86_REG_XMM0,xmm)
    cpu.mem_write(frame-0x78,u32(3));cpu.mem_write(frame+0x1c,u32(0x3140));cpu.mem_write(frame+0x2c,u32(0x10))
    cpu.emu_start(0x10000000,0x21000000,count=1000)
    for reg,value in values.items():assert cpu.reg_read(reg)==(91 if reg==UC_X86_REG_EAX else value),(reg,hex(cpu.reg_read(reg)),hex(value))
    assert cpu.reg_read(UC_X86_REG_EFLAGS)==0x246
    assert cpu.reg_read(UC_X86_REG_XMM0)==xmm
    assert cpu.reg_read(UC_X86_REG_FPCW)==0x27f
    assert cpu.reg_read(UC_X86_REG_ESP)==sp
    cpu.mem_write(values[UC_X86_REG_ECX]+0x650,u32(7))
    cpu.ctl_remove_cache(0x21000000,0x21001000)
    cpu.emu_start(0x21000000,0x21000008,count=100)
    assert struct.unpack('<I',cpu.mem_read(0x40002000,4))[0]==91, {'out':cpu.mem_read(0x40002000,4).hex(), 'eip':hex(cpu.reg_read(UC_X86_REG_EIP)), 'eax':cpu.reg_read(UC_X86_REG_EAX), 'esi':hex(cpu.reg_read(UC_X86_REG_ESI))}
    assert struct.unpack('<I',cpu.mem_read(values[UC_X86_REG_ECX]+0x650,4))[0]==98
    return {'bytes':len(emitted),'registers_preserved':True,'flags_preserved':True,'x87_control_preserved':True,'xmm_preserved':True,'original_writeback_once':True}


def draw_fixture(raw:bytes,count:int)->dict:
    machine,root,_=constructor(raw);cpu=machine.cpu
    sprite=machine.alloc(0x200,'synthetic sprite resource')
    machine.set32(sprite+4,0x100);machine.set32(sprite+0x10,16128)
    packet=machine.alloc(0x80,'synthetic draw packet');matrix=machine.alloc(0x40,'synthetic identity matrix')
    cpu.mem_write(matrix,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1));machine.set32(packet+0x10,matrix)
    cpu.mem_write(sprite+0x20,b'\xff'*64)
    dependencies=set()
    def code(cpu,address,size,context):
        sp=cpu.reg_read(UC_X86_REG_ESP)
        if address==0x639180:dependencies.add('render owner fixture');machine.returning(root)
        elif address==0x640f60:
            dependencies.add('screen geometry fixture')
            for offset in (4,8):cpu.mem_write(machine.read32(sp+offset),struct.pack('<4I',1920,1080,1920,1080))
            machine.returning()
        elif address==0x94964c:
            dest,value,length=(machine.read32(sp+x) for x in (4,8,12))
            assert length<0x10000;cpu.mem_write(dest,bytes([value&255])*length);machine.returning(dest)
        elif address==0x949240:dependencies.add('security cookie fixture');machine.returning()
    cpu.hook_add(UC_HOOK_CODE,code)
    completed=0
    for index in range(count):
        machine.call(0x7f4900,(sprite,packet,861+index,0));completed+=1
    return {'draws_completed':completed,'dependencies':sorted(dependencies),'writes':len(machine.writes)}


def main()->None:
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('executable',type=Path)
    parser.add_argument('--draws',type=int,default=0);args=parser.parse_args()
    raw=args.executable.read_bytes();assert hashlib.sha256(raw).hexdigest()==EXPECTED_SHA256
    result={'executable_sha256':EXPECTED_SHA256,'nul_gateway':gateway()}
    _,_,layers=constructor(raw);result['sphere_native_layers']=layers
    if args.draws:
        assert 0<args.draws<=1024;result['draw']=draw_fixture(raw,args.draws)
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
