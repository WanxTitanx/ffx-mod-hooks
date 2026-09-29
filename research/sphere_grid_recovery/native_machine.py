"""Guarded x86 execution in Unicorn memory, never in an FFX process.

All external callees are explicit fixtures. An unexpected callee, unreadable
address, ownership violation or instruction limit stops the test, not the game.
The native runner cannot treat one of those errors as an expected overrun unless
its exact fault classification matches the requested negative test.
"""
from __future__ import annotations
from dataclasses import asdict
from contextlib import contextmanager
from pathlib import Path
import struct
import sys
from typing import Callable
from memory_contract import Handle, Ledger, MemoryViolation
from pe_image import MappedImage

# Reuse the previously isolated dependency directory when running in the user's
# recovery worktree. Importing this module never installs any package.
ROOT=Path(__file__).resolve().parents[2]
LOCAL_DEPENDENCIES=ROOT/'.superpowers/recovery/python'
if LOCAL_DEPENDENCIES.is_dir():sys.path.insert(0,str(LOCAL_DEPENDENCIES))


class NativeFailure(AssertionError):
    def __init__(self, evidence: dict):
        self.evidence=evidence
        super().__init__(str(evidence))


class MissingEmulator(RuntimeError):
    pass


def page(value: int) -> int:
    return (value+0xfff)&~0xfff


class Machine:
    STACK=0x30000000
    STACK_SIZE=0x20000
    SP=STACK+0x10000
    STOP=0x3f000000
    HEAP_BEGIN=0x10000000
    HEAP_END=0x20000000

    def __init__(self, mapped: MappedImage, *, private_image: bytes | bytearray | None = None):
        try:
            import unicorn
            from unicorn import x86_const
        except ImportError as error:
            raise MissingEmulator('Unicorn 2.1.4 is required; no native fixture executed') from error
        self.u=unicorn;self.r=x86_const
        self.cpu=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
        self.mapped=mapped
        image=mapped.data if private_image is None else private_image
        if len(image)!=len(mapped.data):raise ValueError('private image length changed')
        self.cpu.mem_map(mapped.base,page(len(image)))
        self.cpu.mem_write(mapped.base,bytes(image))
        self.cpu.mem_protect(mapped.base,page(len(image)),unicorn.UC_PROT_READ)
        for section in mapped.sections:
            start=section.rva&~0xfff;extent=page(section.rva+section.length)-start
            permission=unicorn.UC_PROT_READ
            if section.writable:permission|=unicorn.UC_PROT_WRITE
            if section.executable:permission|=unicorn.UC_PROT_EXEC
            self.cpu.mem_protect(mapped.base+start,extent,permission)
        self.cpu.mem_map(self.STACK,self.STACK_SIZE,unicorn.UC_PROT_READ|unicorn.UC_PROT_WRITE)
        self.cpu.mem_map(self.STOP,0x1000,unicorn.UC_PROT_READ|unicorn.UC_PROT_EXEC)
        self.cpu.mem_write(self.STOP,b'\xcc')
        self.cpu.reg_write(self.r.UC_X86_REG_ESP,self.SP)
        self.ledger=Ledger(byte_budget=64*1024*1024,record_budget=4096)
        self.cursor=self.HEAP_BEGIN
        self.generation=1
        self.allocations: dict[int,Handle]={}
        self._heap_permissions:dict[int,int]={}
        self.failure: dict | None=None
        self.dependencies:set[str]=set()
        self.external:dict[int,Callable[[],None]]={}
        self.allowed_code:list[tuple[int,int]]=[]
        self.phase_stats:dict[int,dict]={}
        self.phase=False
        self.read_observer:Callable[[int,int,int],None] | None=None
        # Setup may access any live fixture allocation. An active scope, even
        # an empty one, may access only the exact resources captured by its caller.
        self._borrowed:dict[int,Handle] | None=None
        self.cpu.hook_add(unicorn.UC_HOOK_CODE,self._code)
        self.cpu.hook_add(unicorn.UC_HOOK_MEM_READ|unicorn.UC_HOOK_MEM_WRITE,self._memory)
        self.cpu.hook_add(unicorn.UC_HOOK_MEM_INVALID,self._invalid)

    def fail(self,kind: str,address: int,size: int=0,detail: str='') -> None:
        if self.failure is None:
            self.failure={'kind':kind,'address':hex(address),'size':size,
                          'eip':hex(self.cpu.reg_read(self.r.UC_X86_REG_EIP)),
                          'detail':detail}
        self.cpu.emu_stop()

    def _validate_access(self,address: int,size: int,write: bool) -> Handle | None:
        if type(size) is not int or size<=0 or address<0 or address+size>0x100000000:
            raise MemoryViolation('invalid or wrapping memory span')
        if self.HEAP_BEGIN<=address<self.HEAP_END or self.HEAP_BEGIN<address+size<=self.HEAP_END:
            handle=self.ledger.containing(address,size)
            if handle is None:raise MemoryViolation('allocation overrun or heap guard-page access')
            expected=None if self._borrowed is None else self._borrowed.get(handle.address)
            if self._borrowed is not None and expected is None:
                raise NativeFailure({'kind':'unowned_allocation','address':hex(address),
                                     'actual_owner':handle.owner})
            if expected is not None and expected.generation!=handle.generation:
                raise NativeFailure({'kind':'generation_mismatch','address':hex(address),
                                     'expected_generation':expected.generation,'actual_generation':handle.generation})
            if expected is not None and expected.generation==handle.generation and expected!=handle:
                raise NativeFailure({'kind':'allocation_serial_mismatch','address':hex(address),
                                     'expected_serial':expected.serial,'actual_serial':handle.serial})
            self.ledger.access_address(address,size,write=write,owner=handle.owner,generation=self.generation)
            if write and self.phase:
                stats=self.phase_stats.setdefault(handle.address,{'stores':0,'maximum_end_offset':0,'minimum_offset':address-handle.address})
                stats['minimum_offset']=min(stats['minimum_offset'],address-handle.address)
                stats['stores']+=1
                stats['maximum_end_offset']=max(stats['maximum_end_offset'],address+size-handle.address)
            return handle
        if self.STACK<=address and address+size<=self.STACK+self.STACK_SIZE:return None
        if not write and self.STOP<=address and address+size<=self.STOP+0x1000:return None
        rva=address-self.mapped.base
        for section in self.mapped.sections:
            if section.rva<=rva and rva+size<=section.rva+section.length:
                if write and not section.writable:raise MemoryViolation('write to read-only image section')
                return None
        if not write and 0<=rva and rva+size<=len(self.mapped.data):return None
        raise MemoryViolation('unmodelled memory span')

    @staticmethod
    def _kind(error: MemoryViolation) -> str:
        text=str(error)
        if 'released' in text:return 'use_after_free'
        if 'generation' in text:return 'generation_mismatch'
        if 'overrun' in text or 'underrun' in text or 'guard-page' in text:return 'allocation_bounds'
        return 'memory_contract'

    def _memory(self,cpu,access,address,size,value,context):
        try:
            self._validate_access(address,size,access==self.u.UC_MEM_WRITE)
            if access==self.u.UC_MEM_READ and self.read_observer:
                self.read_observer(cpu.reg_read(self.r.UC_X86_REG_EIP),address,size)
        except MemoryViolation as error:
            self.fail(self._kind(error),address,size,str(error))
        except NativeFailure as error:
            if self.failure is None:self.failure=error.evidence
            self.cpu.emu_stop()

    def _invalid(self,cpu,access,address,size,value,context):
        try:self._validate_access(address,size,access in (self.u.UC_MEM_WRITE_UNMAPPED,self.u.UC_MEM_WRITE_PROT))
        except MemoryViolation as error:self.fail(self._kind(error),address,size,str(error))
        except NativeFailure as error:
            if self.failure is None:self.failure=error.evidence
            self.cpu.emu_stop()
        else:self.fail('unmapped_or_protected_memory',address,size)
        return False

    def _code(self,cpu,address,size,context):
        try:
            if address in self.external:
                self.external[address]()
                return
            if address==self.STOP:return
            if not any(start<=address and address+size<=end for start,end in self.allowed_code):
                self.fail('unmodelled_native_callee',address,size)
        except (MemoryViolation,NativeFailure,AssertionError,ValueError) as error:
            if isinstance(error,NativeFailure):self.failure=error.evidence
            else:self.fail('dependency_fixture_failure',address,size,str(error))
            self.cpu.emu_stop()

    def alloc(self,length: int,label: str) -> Handle:
        address=self.cursor+0x1000
        extent=page(length)
        if address+extent+0x1000>self.HEAP_END:raise MemoryViolation('private heap address budget exhausted')
        handle=self.ledger.register(address,length,owner=label,generation=self.generation)
        self.cpu.mem_map(address,extent,self.u.UC_PROT_READ|self.u.UC_PROT_WRITE)
        self.allocations[address]=handle
        self._heap_permissions[address]=self.u.UC_PROT_READ|self.u.UC_PROT_WRITE
        if self._borrowed is not None:self._sync_heap_permissions()
        self.cursor=address+extent+0x1000
        return handle

    def release(self,handle: Handle) -> None:
        # This is a TEST allocator operation, not the game's native destructor.
        self.ledger.release(handle,owner=handle.owner,generation=self.generation)
        self.cpu.mem_protect(handle.address,page(handle.length),self.u.UC_PROT_NONE)
        self._heap_permissions[handle.address]=self.u.UC_PROT_NONE

    def recycle(self,old: Handle,*,generation: int) -> Handle:
        handle=self.ledger.register(old.address,old.length,owner=old.owner,generation=generation)
        self.cpu.mem_protect(handle.address,page(handle.length),self.u.UC_PROT_READ|self.u.UC_PROT_WRITE)
        self.cpu.mem_write(handle.address,b'\0'*handle.length)
        self.allocations[handle.address]=handle
        self._heap_permissions[handle.address]=self.u.UC_PROT_READ|self.u.UC_PROT_WRITE
        self._sync_heap_permissions()
        return handle

    def _sync_heap_permissions(self) -> None:
        # A UC_HOOK_MEM_WRITE callback can report a violation after Unicorn has
        # already committed the current store. Page protection prevents that
        # first offending store; exact byte bounds are still checked separately.
        for address,handle in self.allocations.items():
            permission=self.u.UC_PROT_NONE
            try:
                self.ledger.containing(address,handle.length)
                admitted=(self._borrowed is None or self._borrowed.get(address)==handle)
                if admitted and handle.generation==self.generation:
                    permission=self.u.UC_PROT_READ|self.u.UC_PROT_WRITE
            except MemoryViolation:
                pass
            if self._heap_permissions.get(address)!=permission:
                self.cpu.mem_protect(address,page(handle.length),permission)
                self._heap_permissions[address]=permission

    @contextmanager
    def borrowed(self,handles):
        """Bind captured resources, not whatever occupies their addresses now.

        Setup outside this call remains legal. Nested callbacks cannot silently
        replace an outer borrower; every access rechecks its captured identity.
        """
        previous=self._borrowed
        captured={} if previous is None else dict(previous)
        for handle in handles:
            if not isinstance(handle,Handle):raise ValueError('invalid borrowed resource')
            if previous is not None and handle.address not in previous:
                raise NativeFailure({'kind':'unowned_allocation','address':hex(handle.address),
                                     'detail':'nested callback cannot widen borrowed resources'})
            outer=captured.get(handle.address)
            if outer is not None and outer!=handle:
                raise NativeFailure({'kind':'allocation_serial_mismatch','address':hex(handle.address)})
            captured[handle.address]=handle
        self._borrowed=captured
        try:
            for handle in captured.values():
                self._validate_access(handle.address,handle.length,False)
            self._sync_heap_permissions()
            yield
        except MemoryViolation as error:
            raise NativeFailure({'kind':self._kind(error),'detail':str(error)}) from error
        finally:
            self._borrowed=previous
            self._sync_heap_permissions()

    def get(self,address: int,size: int) -> bytes:
        self._validate_access(address,size,False)
        return bytes(self.cpu.mem_read(address,size))

    def put(self,address: int,data: bytes) -> None:
        self._validate_access(address,len(data),True)
        self.cpu.mem_write(address,data)

    def get32(self,address: int) -> int:return struct.unpack('<I',self.get(address,4))[0]
    def set32(self,address: int,value: int) -> None:self.put(address,struct.pack('<I',value&0xffffffff))

    def arguments(self,count: int) -> tuple[int,...]:
        sp=self.cpu.reg_read(self.r.UC_X86_REG_ESP)
        return tuple(self.get32(sp+4+4*index) for index in range(count))

    def returning(self,value: int=0,pop: int=0) -> None:
        sp=self.cpu.reg_read(self.r.UC_X86_REG_ESP)
        target=self.get32(sp)
        self.cpu.reg_write(self.r.UC_X86_REG_EAX,value&0xffffffff)
        self.cpu.reg_write(self.r.UC_X86_REG_ESP,sp+4+pop)
        self.cpu.reg_write(self.r.UC_X86_REG_EIP,target)

    def memset(self) -> None:
        destination,value,length=self.arguments(3)
        if not 0<length<=2*1024*1024:raise MemoryViolation('memset dependency length exceeds fixture limit')
        self.put(destination,bytes([value&255])*length)
        self.dependencies.add('bounded C memset fixture')
        self.returning(destination)

    def memcpy(self) -> None:
        destination,source,length=self.arguments(3)
        if not 0<length<=2*1024*1024:raise MemoryViolation('memcpy dependency length exceeds fixture limit')
        self.put(destination,self.get(source,length))
        self.dependencies.add('bounded C memcpy fixture')
        self.returning(destination)

    def begin_phase(self) -> None:
        self.phase_stats.clear();self.phase=True

    def run(self,start: int,stop: int,*,instructions: int=200000) -> None:
        self.failure=None
        try:self.cpu.emu_start(start,stop,count=instructions)
        except self.u.UcError as error:
            if self.failure is None:
                self.failure={'kind':'emulator_error','eip':hex(self.cpu.reg_read(self.r.UC_X86_REG_EIP)),
                              'detail':str(error)}
        if self.failure:raise NativeFailure(self.failure)
        if self.cpu.reg_read(self.r.UC_X86_REG_EIP)!=stop:
            raise NativeFailure({'kind':'instruction_limit','start':hex(start),'expected_stop':hex(stop)})

    def call(self,start: int,args: tuple[int,...]=(),*,instructions: int=200000) -> None:
        self.put(self.SP,struct.pack('<I',self.STOP)+b''.join(struct.pack('<I',x&0xffffffff) for x in args))
        self.cpu.reg_write(self.r.UC_X86_REG_ESP,self.SP)
        self.run(start,self.STOP,instructions=instructions)
        if self.cpu.reg_read(self.r.UC_X86_REG_ESP)!=self.SP+4:
            raise NativeFailure({'kind':'cdecl_stack_mismatch','target':hex(start)})

    def measured(self,handle: Handle) -> dict:
        return {'capacity_bytes':handle.length,
                **self.phase_stats.get(handle.address,{'stores':0,'maximum_end_offset':0,'minimum_offset':None})}
