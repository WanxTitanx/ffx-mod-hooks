#!/usr/bin/env python3
"""Exclusive Sphere Grid machine-code tests using the exact private FFX.exe.

All code executes inside Unicorn. No process attachment, Windows DLL loading,
asset deployment or native save I/O occurs. The scope is constructor/writer and
state-pack/hydrate/load-copy unit fixtures, NOT a complete menu lifecycle.

Each case runs in a bounded child process; unexpected native dependencies and
missing emulation fail the case. The overall report always keeps production
acceptance false while the full menu/capture/effects/destructor paths are open.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import time
from emulator_patches import apply_private_image,render_plan
from memory_contract import MemoryViolation,RenderPlan
from native_machine import Machine,MissingEmulator,NativeFailure
from pe_image import EXPECTED_SHA256,ImageError,read_exact
import sprite_fixture

CAPACITIES=(860,861,862,1024,1025,2048,4096,16384)
# Exact instruction spans reached in the bounded diagnostic against EXPECTED_SHA256.
# Execute ORIGINAL native instructions, never substitute a return or Python math.
# Unobserved branches remain blocked. This is coverage of the fixture paths only.
NATIVE_NUMERIC_PATHS=((0x9497e0,0x9497e9),(0x949816,0x94988b))
# Include the bounded native table-selection body, not just its zero-return
# fallback. Table data stays in the verified image and no math is substituted.
NATIVE_COORDINATE_PATHS=((0x641250,0x641279),(0x684ca0,0x684d82))
UNVERIFIED=(
    'Native capture begin/reset and reserved overlay count',
    'Complete A48910 activation/effect/render interaction',
    'GPU submission and restoring field render state without changing locality',
    'Native capture destructor and deferred consumers during close/reopen',
    'Full menu navigation, new game and real save checksum/identity I/O',
    'Logical layouts above 1024, stat recomputation and ordering redesign',
    'In-game RT2 acceptance',
)


def require(condition: bool,message: str) -> None:
    if not condition:raise NativeFailure({'kind':'assertion','detail':message})


class DrawFixture:
    """Real constructor variant and real writer, explicit external dependencies."""
    def __init__(self,mapped,capacity: int,*,empty: bool=True):
        self.capacity=capacity
        self.plan=RenderPlan(capacity)
        self.m=Machine(mapped,private_image=apply_private_image(mapped.data,render_plan(capacity)))
        m=self.m
        self.root=m.alloc(0xd0,'render owner')
        m.cpu.reg_write(m.r.UC_X86_REG_ESI,self.root.address)
        m.allowed_code=[(0x6823c7,0x682571),(0x7f4900,0x7f6000),
                        *NATIVE_NUMERIC_PATHS,*NATIVE_COORDINATE_PATHS]
        m.dependencies.add('original native math instruction paths; unobserved branches remain blocked')
        ordinal=0
        def allocator():
            nonlocal ordinal
            (size,)=m.arguments(1);ordinal+=1
            allocated=m.alloc(size,f'native constructor allocation {ordinal}')
            m.dependencies.add('private guarded allocator, not native heap')
            m.returning(allocated.address)
        m.external[0x630670]=allocator
        m.external[0x94964c]=m.memset
        m.run(0x6823c7,0x682571,instructions=10000)
        require(m.get32(self.root.address+0x90)==2,'constructor did not create two layers')
        self.layers=m.get32(self.root.address+0x94)
        descriptors=m.allocations.get(self.layers)
        require(descriptors is not None and descriptors.length==0xd8,'descriptor array size is not 2*0x6c')
        self.buffers=[]
        for index in range(2):
            descriptor=self.layers+index*0x6c
            require(m.get32(descriptor+4)==capacity*4,'constructor vertex units differ')
            require(m.get32(descriptor+8)==capacity*6,'constructor index units differ')
            buffers={}
            for name,offset in [('positions',12),('colors',20),('uv',24),('indices',28)]:
                pointer=m.get32(descriptor+offset)
                handle=m.allocations.get(pointer)
                require(handle is not None and handle.length==self.plan.buffer_sizes[name],
                        f'{name}: constructor pointer/size is not the expected owned allocation')
                buffers[name]=handle
            self.buffers.append(buffers)
            if empty:
                m.set32(descriptor+4,0);m.set32(descriptor+8,0)
        self.sprite=m.alloc(0x200,'synthetic sprite resource')
        self.packet=m.alloc(0x80,'synthetic draw packet')
        sprite_fixture.install(m.put,m.set32,self.sprite.address,self.packet.address)
        matrix=m.alloc(0x40,'synthetic identity matrix')
        m.put(matrix.address,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1))
        m.set32(self.packet.address+0x10,matrix.address)
        def owner():
            m.dependencies.add('synthetic capture-owner resolver, not native capture lifecycle')
            m.returning(self.root.address)
        def viewport():
            for pointer in m.arguments(2):m.put(pointer,struct.pack('<4I',1920,1080,1920,1080))
            m.dependencies.add('synthetic viewport dimensions');m.returning()
        def cookie():
            m.dependencies.add('synthetic security-cookie check');m.returning()
        m.external.update({0x639180:owner,0x640f60:viewport,0x949240:cookie})
        self.draw_resources=(self.root,descriptors,self.sprite,self.packet,matrix,
                             *(h for layer in self.buffers for h in layer.values()))
        m.begin_phase()

    def check_geometry(self,layer: int,quad: int,local_indices: tuple[int,...]) -> None:
        buffers=self.buffers[layer];m=self.m
        positions=struct.unpack('<12f',m.get(buffers['positions'].address+quad*48,48))
        colors=struct.unpack('<16f',m.get(buffers['colors'].address+quad*64,64))
        uv=struct.unpack('<8f',m.get(buffers['uv'].address+quad*32,32))
        require(all(math.isfinite(v) for v in (*positions,*colors,*uv)), 'non-finite native quad')
        # result() has bounded these indices to this quad. Test the triangles
        # actually submitted, not a canonical triangulation of the same points.
        for first in (0,3):
            a,b,c=local_indices[first:first+3]
            area=(positions[3*b]-positions[3*a])*(positions[3*c+1]-positions[3*a+1])-(positions[3*c]-positions[3*a])*(positions[3*b+1]-positions[3*a+1])
            require(abs(area)>1e-6,'collapsed native triangle')
        require(all(0<=v<=1 for v in colors),'synthetic color outside normalized range')
        require(all(colors[i]>0 for i in (3,7,11,15)),'transparent synthetic quad')
        require(all(0<=v<=1 for v in uv),'native UV outside synthetic atlas')
        require(max(uv[::2])>min(uv[::2]) and max(uv[1::2])>min(uv[1::2]),'collapsed native UV rectangle')

    def draw(self,command: int) -> None:
        with self.m.borrowed(self.draw_resources):
            self.m.call(0x7f4900,(self.sprite.address,self.packet.address,command,0))

    def result(self,active: tuple[int,...],*,with_geometry: bool=True) -> dict:
        output=[]
        for index,buffers in enumerate(self.buffers):
            descriptor=self.layers+index*0x6c
            vertices=self.m.get32(descriptor+4);indices=self.m.get32(descriptor+8)
            if index in active:
                require(vertices==self.capacity*4 and indices==self.capacity*6,'append count mismatch')
                values=struct.unpack('<'+str(indices)+'H',self.m.get(buffers['indices'].address,indices*2))
                require(max(values)==self.capacity*4-1,'highest 16-bit vertex index is incorrect')
                for quad in range(self.capacity):
                    local_indices=tuple(value-quad*4 for value in values[quad*6:quad*6+6])
                    require(set(local_indices)=={0,1,2,3},
                            'triangle indices escape their quad')
                    # The index-only sentinel also produces two real triangle
                    # triplets; it must not bypass repeated-endpoint detection.
                    require(len(set(local_indices[:3]))==3 and len(set(local_indices[3:]))==3,
                            'repeated native triangle vertex')
                    if with_geometry:self.check_geometry(index,quad,local_indices)
                for name in ('positions','colors','uv','indices'):
                    measured=self.m.measured(buffers[name])
                    expected_end=buffers[name].length if with_geometry or name=='indices' else 0
                    require(measured['maximum_end_offset']==expected_end,f'{name}: maximum write extent differs')
            else:
                require(vertices==indices==0,'inactive layer append counters changed')
            output.append({'vertices':vertices,'index_count':indices,
                           'buffers':{name:self.m.measured(handle) for name,handle in buffers.items()}})
        return {'layers':output,'dependencies':sorted(self.m.dependencies),
                'synthetic_empty_counters':True,'native_capture_lifecycle_executed':False}


def expect_fault(action,kind: str) -> dict:
    try:action()
    except NativeFailure as error:
        require(error.evidence.get('kind')==kind,f'expected {kind}, got {error.evidence}')
        return error.evidence
    raise NativeFailure({'kind':'missing_negative_detection','expected':kind})


def writer_case(mapped,capacity: int,mode: str) -> dict:
    if mode not in ('positive','negative','both'):raise ValueError('unknown writer mode')
    fixture=DrawFixture(mapped,capacity)
    command=(lambda i:capacity+i) if mode=='positive' else (lambda i:-capacity-1) if mode=='negative' else (lambda i:0xffff)
    for index in range(capacity):fixture.draw(command(index))
    active=(0,) if mode=='positive' else (1,) if mode=='negative' else (0,1)
    report=fixture.result(active,with_geometry=mode!='both')
    report['next_append_detected']=expect_fault(lambda:fixture.draw(command(capacity)),'allocation_bounds')
    return report


def fixed_case(mapped,capacity: int) -> dict:
    fixture=DrawFixture(mapped,capacity)
    fixture.draw(capacity-1);fixture.draw(-capacity)
    for index,buffers in enumerate(fixture.buffers):
        descriptor=fixture.layers+index*0x6c
        require(fixture.m.get32(descriptor+4)==fixture.m.get32(descriptor+8)==0,'fixed write changed append counters')
        for name in ('positions','colors','uv'):
            require(fixture.m.measured(buffers[name])['maximum_end_offset']==buffers[name].length,
                    'fixed last quad did not stay within the matching layer')
    return {'fixed_last_quad_both_layers':True,'dependencies':sorted(fixture.m.dependencies),
            'native_capture_lifecycle_executed':False}


def lifecycle_negative_case(mapped,mode: str) -> dict:
    fixture=DrawFixture(mapped,861,empty=mode!='full_counters')
    if mode=='full_counters':
        return {'detected':expect_fault(lambda:fixture.draw(861),'allocation_bounds'),
                'reason':'constructor full count is not an empty append cursor'}
    handle=fixture.buffers[0]['colors']
    fixture.m.release(handle)
    if mode=='use_after_free':
        return {'detected':expect_fault(lambda:fixture.draw(0),'use_after_free'),
                'native_destructor_executed':False,'free_was_explicitly_injected':True}
    fixture.m.recycle(handle,generation=2)
    return {'detected':expect_fault(lambda:fixture.draw(0),'generation_mismatch'),
            'native_destructor_executed':False,'reuse_was_explicitly_injected':True}


MENU_SIZE=0x12fc0
NODE_BASE=0x808
LINK_BASE=0xa808
NODE_STRIDE=40
LINK_STRIDE=20
STATE_OFFSET=0x21ec
STATE_SIZE=0x1320
PAYLOAD_SIZE=0x68c0
FILE_SIZE=0x6900
CURSORS=tuple(0x11088+i*0x50+0x44 for i in range(7))


class StateFixture:
    def __init__(self,mapped,node_count: int,link_count: int):
        self.m=Machine(mapped);m=self.m
        self.nodes=node_count;self.links=link_count
        self.menu=m.alloc(MENU_SIZE,'menu state')
        self.save=m.alloc(PAYLOAD_SIZE,'private native-sized save payload')
        self.state=self.save.address+STATE_OFFSET
        m.put(self.save.address,b'\xa5'*PAYLOAD_SIZE)
        m.set32(0x2305834,self.menu.address)
        m.put(self.menu.address,struct.pack('<3H',1,node_count,link_count))
        for i in range(min(node_count,1024)):
            node=self.menu.address+NODE_BASE+i*NODE_STRIDE
            m.put(node+6,struct.pack('<H',1+i%100));m.put(node+0x21,bytes([i%128]))
        for i in range(min(link_count,1024)):
            m.put(self.menu.address+LINK_BASE+i*LINK_STRIDE+12,bytes([(i*3)%128]))
        for i,offset in enumerate(CURSORS):m.put(self.menu.address+offset,struct.pack('<H',max(0,node_count-1-i)))
        m.put(self.menu.address+71115,b'\x02\x03')
        m.allowed_code=[(0xa5bb70,0xa5bca0),(0xa49590,0xa497b0),(0x8b5450,0x8b5480),
                        *NATIVE_NUMERIC_PATHS]
        m.dependencies.add('original native numeric instruction paths; unobserved branches remain blocked')
        def getter():m.dependencies.add('private state-pointer dependency');m.returning(self.state)
        def after_load():m.dependencies.add('post-load cache/reset dependency not executed');m.returning()
        m.external.update({0x785000:getter,0x94925c:m.memcpy,0x787530:after_load})
        self.before_menu=m.get(self.menu.address,MENU_SIZE)
        self.before_save=m.get(self.save.address,PAYLOAD_SIZE)

    def pack(self):self.m.call(0xa5bb70)

    def check_pack(self):
        expected=bytearray(self.before_save)
        for i in range(self.nodes):expected[STATE_OFFSET+2*i:STATE_OFFSET+2*i+2]=bytes([1+i%100,i%128])
        for i in range(self.links):expected[STATE_OFFSET+0xa00+i]=(i*3)%128
        for i in range(7):struct.pack_into('<H',expected,STATE_OFFSET+0xf00+i*2,max(0,self.nodes-1-i))
        expected[STATE_OFFSET+0xf18:STATE_OFFSET+0xf1a]=b'\x02\x03'
        actual=self.m.get(self.save.address,PAYLOAD_SIZE)
        require(actual==expected,'native pack wrote unexpected payload bytes or truncated a cursor')
        return actual

    def load_roundtrip(self,payload: bytes):
        m=self.m
        file=m.alloc(FILE_SIZE,'synthetic 64-byte-header save file')
        # This is not native save serialization or CRC validation. It isolates
        # the already measured bulk load-copy boundary and the 64-byte skew.
        header=bytes(range(64));m.put(file.address,header+payload)
        m.put(self.save.address,b'\xcc'*PAYLOAD_SIZE)
        m.call(0x8b5450,(self.save.address,file.address))
        require(m.get(self.save.address,PAYLOAD_SIZE)==payload,'native load-copy did not preserve payload')
        require(m.get(file.address,64)==header,'native load-copy modified file header')

    def hydrate(self):
        m=self.m
        m.put(self.menu.address,self.before_menu)
        for i in range(self.nodes):
            node=self.menu.address+NODE_BASE+i*NODE_STRIDE
            m.put(node+6,struct.pack('<H',1));m.put(node+0x21,b'\0')
        for i in range(self.links):m.put(self.menu.address+LINK_BASE+i*LINK_STRIDE+12,b'\0')
        for offset in CURSORS:m.put(self.menu.address+offset,b'\0\0')
        m.put(self.menu.address+71115,b'\0\0')
        expected=bytearray(m.get(self.menu.address,MENU_SIZE))
        for i in range(self.nodes):
            struct.pack_into('<H',expected,NODE_BASE+i*NODE_STRIDE+6,1+i%100)
            expected[NODE_BASE+i*NODE_STRIDE+0x21]=i%128
        for i in range(self.links):expected[LINK_BASE+i*LINK_STRIDE+12]=(i*3)%128
        for i,offset in enumerate(CURSORS):struct.pack_into('<H',expected,offset,self.nodes-1-i)
        expected[71115:71117]=b'\x02\x03'
        struct.pack_into('<i',expected,70464,-7281)
        for offset in (70480,70484,70488):struct.pack_into('<f',expected,offset,0.125)
        struct.pack_into('<i',expected,71344,-2)
        m.call(0xa49590)
        require(m.get(self.menu.address,MENU_SIZE)==expected,
                'native hydrate changed an unowned menu field or failed its complete write contract')
        for i in range(self.nodes):
            node=self.menu.address+NODE_BASE+i*NODE_STRIDE
            require(m.get(node+6,2)==struct.pack('<H',1+i%100),'hydrated node content differs')
            require(m.get(node+0x21,1)==bytes([i%128]),'hydrated activation mask differs')
        for i in range(self.links):
            require(m.get(self.menu.address+LINK_BASE+i*LINK_STRIDE+12,1)==bytes([(i*3)%128]),'hydrated link differs')
        for i,offset in enumerate(CURSORS):
            require(m.get(self.menu.address+offset,2)==struct.pack('<H',self.nodes-1-i),'16-bit cursor was truncated')
        require(m.get(self.menu.address+71115,2)==b'\x02\x03','view state did not round-trip')


def state_case(mapped,count: int) -> dict:
    fixture=StateFixture(mapped,count,min(count,1024))
    fixture.pack();payload=fixture.check_pack()
    fixture.load_roundtrip(payload);fixture.hydrate()
    return {'nodes':count,'links':fixture.links,'payload_bytes':PAYLOAD_SIZE,'file_bytes':FILE_SIZE,
            'node_last_file_offset':64+STATE_OFFSET+2*(count-1),
            'payload_sha256':hashlib.sha256(payload).hexdigest(),
            'dependencies':sorted(fixture.m.dependencies),
            'full_activation_executed':False,'native_save_write_or_crc_executed':False,
            'actual_disk_save_touched':False}


def logical_array_negative(mapped,*,nodes: bool) -> dict:
    # Learn source instruction PCs from a real two-node / one-link pack fixture,
    # not from byte-pattern scans. Then hold their typed source regions fixed.
    training=StateFixture(mapped,2,1);pcs=set()
    expected={training.menu.address+NODE_BASE+6,training.menu.address+NODE_BASE+0x21} if nodes else {
        training.menu.address+LINK_BASE+12}
    def observe(pc,address,size):
        if address in expected:pcs.add(pc)
    training.m.read_observer=observe;training.pack()
    require(len(pcs)==(2 if nodes else 1),'unable to establish typed native source instruction PCs')
    fixture=StateFixture(mapped,1025 if nodes else 2,1 if nodes else 1025)
    start=fixture.menu.address+(NODE_BASE if nodes else LINK_BASE)
    end=start+1024*(NODE_STRIDE if nodes else LINK_STRIDE)
    def guard(pc,address,size):
        if pc in pcs and not start<=address<=end-size:
            raise NativeFailure({'kind':'logical_array_bounds','eip':hex(pc),'address':hex(address),
                                 'size':size,'typed_region':'nodes' if nodes else 'links'})
    fixture.m.read_observer=guard
    fault=expect_fault(fixture.pack,'logical_array_bounds')
    return {'detected':fault,'whole_menu_heap_allocation_would_not_detect_this':True,
            'read_pcs':sorted(hex(pc) for pc in pcs)}


class NodeProducerFixture(DrawFixture):
    """Real bulk and single-node producers; transform helpers are explicit fixtures.

    A single-node redraw is not a full A48910 activation: effect resources,
    animation scheduling and native menu exit are not exercised here.
    """
    def __init__(self,mapped,count: int):
        if not 1<=count<=1024:raise ValueError('node producer fixture respects embedded array capacity')
        super().__init__(mapped,count)
        m=self.m;self.count=count
        self.menu=m.alloc(MENU_SIZE,'node-producer menu')
        m.set32(0x2305834,self.menu.address)
        m.put(self.menu.address,struct.pack('<3H',1,count,0))
        for i in range(count):
            node=self.menu.address+NODE_BASE+i*NODE_STRIDE
            m.put(node,struct.pack('<hh',i%32,(i//32)%32))
            # Null entries exercise the both-layer index-only sentinel. The
            # last node stays filled so it can be redrawn after a mask change.
            content=0xffff if i%17==0 and i!=count-1 else 35
            m.put(node+6,struct.pack('<H',content))
        for offset in (0,4,8):m.set32(self.menu.address+0xf828+35*0x30+offset,self.sprite.address)
        m.put(self.menu.address+0x115bc,b'\0')
        m.allowed_code.append((0xa51340,0xa51700))
        def placement():
            destination,node=m.arguments(2)
            first=self.menu.address+NODE_BASE
            if node<first or node>=first+count*NODE_STRIDE or (node-first)%NODE_STRIDE:
                raise NativeFailure({'kind':'logical_node_index','node_pointer':hex(node),
                                     'first_node':hex(first),'declared_count':count,
                                     'boundary':'native producer argument to placement helper'})
            m.put(destination,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1))
            m.dependencies.add('synthetic placement transform; geometry shape is not verified')
            m.returning()
        def project():
            destination,basis,source=m.arguments(3)
            m.get(basis,64)
            m.put(destination,m.get(source,64))
            m.dependencies.add('synthetic projection transform; screen output is not verified')
            m.returning()
        m.external.update({0xa5ad30:placement,0x6ed700:project})
        m.begin_phase()

    def fill(self):
        self.m.call(0xa51340,instructions=3000000)
        for layer in range(2):
            descriptor=self.layers+layer*0x6c
            require(self.m.get32(descriptor+4)==self.count*4,'bulk node producer vertex count differs')
            require(self.m.get32(descriptor+8)==self.count*6,'bulk node producer index count differs')


def producer_redraw_case(mapped,count: int) -> dict:
    fixture=NodeProducerFixture(mapped,count);fixture.fill();m=fixture.m
    before=[{name:m.get(handle.address,handle.length) for name,handle in layer.items()}
            for layer in fixture.buffers]
    m.put(fixture.menu.address+NODE_BASE+(count-1)*NODE_STRIDE+0x21,b'\x7f')
    m.begin_phase();m.call(0xa51560,(count-1,))
    for layer,buffers in enumerate(fixture.buffers):
        descriptor=fixture.layers+layer*0x6c
        require(m.get32(descriptor+4)==count*4 and m.get32(descriptor+8)==count*6,
                'single-node redraw was incorrectly decoded as append')
        for name,handle in buffers.items():
            after=m.get(handle.address,handle.length)
            allowed_start=(count-1)*{'positions':48,'colors':64,'uv':32,'indices':12}[name]
            if layer==0 and name!='indices':
                require(after[:allowed_start]==before[layer][name][:allowed_start],
                        'redrawing the last node corrupted an earlier quad')
                measured=m.measured(handle)
                require(measured['stores']>0,'single-node redraw did not reach its buffer')
                require(measured['minimum_offset']==allowed_start and measured['maximum_end_offset']==handle.length,
                        'single-node redraw stores do not stay exclusively within the last quad')
            else:require(after==before[layer][name],'redraw modified indices or the other layer')
    return {'nodes':count,'bulk_producer_executed':True,'single_node_producer_executed':True,
            'neighbor_quads_preserved':True,'full_activation_executed':False,
            'native_capture_lifecycle_executed':False,'dependencies':sorted(m.dependencies)}


def invalid_node_index_case(mapped,negative: bool) -> dict:
    fixture=NodeProducerFixture(mapped,861)
    fault=expect_fault(lambda:fixture.m.call(0xa51560,(-1 if negative else fixture.count,)),
                       'logical_node_index')
    return {'detected':fault,'invalid_index':-1 if negative else fixture.count,
            'purpose':'detect native producer admitting an invalid logical node index',
            'guard_is_fixture_only_not_an_installed_hook':True}


def run_case(mapped,name: str) -> dict:
    parts=name.split(':')
    if parts[0]=='writer' and len(parts)==3:
        return writer_case(mapped,int(parts[1]),parts[2])
    if parts[0]=='fixed' and len(parts)==2:return fixed_case(mapped,int(parts[1]))
    if parts[0]=='state' and len(parts)==2:return state_case(mapped,int(parts[1]))
    if parts[0]=='producer' and len(parts)==2:return producer_redraw_case(mapped,int(parts[1]))
    if name in ('node_index_upper','node_index_negative'):return invalid_node_index_case(mapped,name=='node_index_negative')
    if name in ('full_counters','use_after_free','reused_generation'):return lifecycle_negative_case(mapped,name)
    if name in ('logical_nodes','logical_links'):return logical_array_negative(mapped,nodes=name=='logical_nodes')
    raise ValueError('unknown native case')


def case_names(capacities: tuple[int,...]) -> list[str]:
    return ([f'writer:{n}:{mode}' for n in capacities for mode in ('positive','negative','both')]+
            [f'fixed:{n}' for n in capacities]+['state:860','state:861','state:1024',
            'full_counters','use_after_free','reused_generation','logical_nodes','logical_links',
            'producer:860','producer:861','producer:862','producer:1024','node_index_upper','node_index_negative'])


def main(argv: list[str] | None=None) -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--capacities',default=','.join(map(str,CAPACITIES)))
    parser.add_argument('--timeout',type=int,default=180,help='Maximum seconds per private child case')
    parser.add_argument('--report',type=Path,help='New JSON report file; existing files are refused')
    parser.add_argument('--case',help=argparse.SUPPRESS)
    args=parser.parse_args(argv)
    try:
        capacities=tuple(int(x) for x in args.capacities.split(','))
        if not capacities or len(capacities)>16 or len(set(capacities))!=len(capacities):
            raise ValueError('capacities must be 1-16 distinct integer values')
        for count in capacities:RenderPlan(count)
        if not 1<=args.timeout<=600:raise ValueError('timeout must be in [1,600]')
        # Even the supervisor verifies identity. No CLI override is accepted.
        mapped=read_exact(args.executable)
        if args.case:
            if args.case not in case_names(capacities):raise ValueError('case is not in the selected matrix')
            started=time.monotonic()
            result=run_case(mapped,args.case)
            print(json.dumps({'case':args.case,'status':'passed','seconds':time.monotonic()-started,'evidence':result}))
            return 0
        results=[]
        for name in case_names(capacities):
            command=[sys.executable,str(Path(__file__).resolve()),str(args.executable),
                     '--capacities',args.capacities,'--case',name]
            try:
                completed=subprocess.run(command,capture_output=True,text=True,timeout=args.timeout)
                if completed.returncode==0:
                    result=json.loads(completed.stdout)
                    if result.get('status')!='passed' or result.get('case')!=name:
                        raise ValueError('child evidence does not match the requested case')
                else:
                    result={'case':name,'status':'failed','exit_code':completed.returncode,
                            'error':completed.stderr[-3000:],'output':completed.stdout[-3000:]}
            except subprocess.TimeoutExpired:
                result={'case':name,'status':'failed','error':'bounded private fixture timed out'}
            except (OSError,ValueError) as error:
                result={'case':name,'status':'failed','error':str(error)}
            results.append(result)
            print(f"{name}: {result['status']}",file=sys.stderr,flush=True)
        passed=sum(r['status']=='passed' for r in results)
        report={'schema':1,'executable_sha256':EXPECTED_SHA256,
                'evidence_level':'private native-instruction unit fixtures with explicit external dependencies',
                'unit_cases_passed':passed,'unit_cases_total':len(results),
                'unit_matrix_passed':passed==len(results),'production_ready':False,
                'full_menu_lifecycle_verified':False,'unverified_boundaries':list(UNVERIFIED),'results':results}
        text=json.dumps(report,indent=2,sort_keys=True)+'\n'
        if args.report:
            with args.report.open('x',encoding='utf-8') as stream:stream.write(text)
        print(text,end='')
        return 0 if passed==len(results) else 1
    except (OSError,ValueError,MemoryViolation,MissingEmulator,NativeFailure) as error:
        evidence=error.evidence if isinstance(error,NativeFailure) else {'detail':str(error)}
        print(json.dumps({'status':'not_validated','production_ready':False,'error':evidence}),file=sys.stderr)
        return 2


if __name__=='__main__':raise SystemExit(main())
