"""Exclusive Sphere Grid native-instruction fixtures, entirely inside Unicorn.

The private executable is read-only. Heap, viewport, locale and CRT boundary
services are explicit test doubles. Constructors, resolver, counter reset,
writers and deferred resource retirement execute original x86 instructions.
There is no process attach, DLL injection, game launch or save-file mutation.
"""
from __future__ import annotations
from bisect import bisect_right
from collections import Counter
import hashlib
from pathlib import Path
import struct
import sys
import sprite_fixture

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/recovery'))
from native_machine_tests import Machine, aligned, EXPECTED_SHA256
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FPCW


def exact_image(path):
    if not __debug__:
        raise RuntimeError('Run the fixture without Python -O; assertions are mandatory')
    raw = Path(path).read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED_SHA256:
        raise ValueError('Unsupported executable SHA-256')
    return raw


class SphereMachine(Machine):
    def __init__(self, raw):
        self.starts = []
        self.freed = set()
        self.stats = {}
        self.services = Counter()
        self.native_calls = Counter()
        self.label = 'fixture'
        self.special = {}
        self.denied_ranges = []
        self.total_allocated = self.live_bytes = self.high_water = 0
        super().__init__(raw)
        self.cpu.reg_write(UC_X86_REG_FPCW, 0x37f)
        self.cpu.hook_add(UC_HOOK_CODE, self.external)
        self.cpu.hook_add(UC_HOOK_MEM_INVALID, self.invalid)

    def alloc(self, size, label):
        pointer = super().alloc(size, label)
        self.starts.append(pointer)
        self.stats[pointer] = dict(stores=0, maximum_end=0, bytes=size, label=label)
        self.total_allocated += size
        self.live_bytes += size
        self.high_water = max(self.high_water, self.live_bytes)
        return pointer

    def fault(self, kind, address, size):
        self.failure = dict(kind=kind, address=hex(address), size=size,
                            eip=hex(self.cpu.reg_read(UC_X86_REG_EIP)))
        self.cpu.emu_stop()

    def checked_range(self, address, size):
        for start, end, label in self.denied_ranges:
            if address < end and address + size > start:
                self.fault('subregion_overrun:' + label, address, size)
                return False
        index = bisect_right(self.starts, address) - 1
        if index >= 0:
            start, length, label = self.allocations[index]
            if start <= address < start + aligned(length):
                if start in self.freed:
                    self.fault('use_after_free:' + label, address, size)
                    return False
                if address + size > start + length:
                    self.fault('allocation_overrun:' + label, address, size)
                    return False
                return True
        return True

    def write(self, cpu, access, address, size, value, context):
        if not self.checked_range(address, size):
            return
        index = bisect_right(self.starts, address) - 1
        if index >= 0:
            start, length, _ = self.allocations[index]
            if start <= address < start + length:
                stat = self.stats[start]
                stat['stores'] += 1
                stat['maximum_end'] = max(stat['maximum_end'], address + size - start)

    def invalid(self, cpu, access, address, size, value, context):
        index = bisect_right(self.starts, address) - 1
        kind = 'unmapped_memory'
        if index >= 0 and self.starts[index] in self.freed:
            start, length, label = self.allocations[index]
            if start <= address < start + aligned(length):
                kind = 'use_after_free:' + label
        elif index >= 0:
            start, length, label = self.allocations[index]
            if start + length <= address < start + aligned(length) + 0x1000:
                kind = 'allocation_overrun:' + label
        self.fault(kind, address, size)
        return False

    def write_bytes(self, address, data):
        if not self.checked_range(address, len(data)):
            raise AssertionError(self.failure)
        self.cpu.mem_write(address, data)

    def release(self, pointer):
        if not pointer:
            return
        if pointer not in self.stats or pointer in self.freed:
            self.fault('invalid_or_double_free', pointer, 0)
            raise AssertionError(self.failure)
        self.freed.add(pointer)
        size = self.stats[pointer]['bytes']
        self.live_bytes -= size
        self.cpu.mem_unmap(pointer, aligned(size))

    def external(self, cpu, address, size, context):
        if address in self.special:
            self.special[address]()
            return
        sp = cpu.reg_read(UC_X86_REG_ESP)
        if address in (0x681db0, 0x639180, 0x684e70, 0x685370, 0x685950,
                       0x6859e0, 0x7f4900, 0xa51340, 0xa51560, 0xa4fe40,
                       0xa4ffd0, 0xa45570, 0xa5bb70, 0xa49590, 0xa48910, 0xa54860, 0x949240):
            self.native_calls[hex(address)] += 1
        if address in (0x630670, 0x6304c0):
            self.services['heap allocation'] += 1
            length = self.read32(sp + 4)
            pointer = self.alloc(length, self.label + ':allocation:' + str(len(self.allocations)))
            self.returning(pointer)
        elif address in (0x6306f0, 0x630540):
            self.services['heap release'] += 1
            self.release(self.read32(sp + 4))
            self.returning()
        elif address == 0x94964c:
            self.services['CRT memset'] += 1
            dest, value, length = (self.read32(sp + offset) for offset in (4, 8, 12))
            if length > 0x200000:
                raise AssertionError('unbounded native memset')
            self.write_bytes(dest, bytes([value & 255]) * length)
            self.returning(dest)
        elif address == 0x94925c:
            self.services['CRT memcpy'] += 1
            dest, source, length = (self.read32(sp + offset) for offset in (4, 8, 12))
            if length > 0x200000:
                raise AssertionError('unbounded native memcpy')
            self.write_bytes(dest, bytes(cpu.mem_read(source, length)))
            self.returning(dest)
        elif address == 0x6799d0:
            self.services['synthetic texture-path canonicalization'] += 1
            source, dest, capacity = (self.read32(sp + offset) for offset in (4, 8, 12))
            if not 1 <= capacity <= 4096:
                raise AssertionError('invalid path capacity')
            text = bytearray()
            for i in range(capacity - 1):
                value = cpu.mem_read(source+i, 1)[0]
                if not value:
                    break
                text.append(value)
            else:
                raise AssertionError('unterminated texture name')
            self.write_bytes(dest, bytes(text) + b'\0')
            self.returning(dest)
        elif address == 0x8ac2a0:
            self.services['locale fixture'] += 1
            self.returning(0)
        elif address == 0x640f60:
            self.services['viewport fixture'] += 1
            for offset in (4, 8):
                self.write_bytes(self.read32(sp + offset), struct.pack('<4I', 1920, 1080, 1920, 1080))
            self.returning()
        elif address == 0x94924a:
            self.fault('native_stack_cookie_mismatch', sp, 4)

    def invoke(self, address, args=(), this=None, limit=1000000):
        if this is not None:
            self.cpu.reg_write(UC_X86_REG_ECX, this)
        self.call(address, args, limit)
        return self.cpu.reg_read(UC_X86_REG_EAX)

    def manager(self, full=False):
        manager = self.alloc(0xe8, 'native manager fixture')
        self.set32(manager + 4, 1)
        self.set32(manager + 8, 1)
        slots = [(0x94, 3)]
        if full:
            slots = [(0x88,0),(0x8c,1),(0x90,2),(0x94,3),(0x98,4),(0xb0,4),
                     (0x9c,11),(0xb4,11),(0xa0,5),(0xb8,5),(0xa4,6),(0xbc,6),
                     (0xa8,12),(0xc0,12),(0xac,13),(0xc4,13),(0xc8,7),(0xcc,8),
                     (0xd0,9),(0xd4,9),(0xd8,9),(0xdc,9),(0xe0,10)]
        for offset, variant in slots:
            self.label = f'native-context-{variant}-slot-{offset:x}'
            pointer = self.invoke(0x681db0, (variant,), this=manager)
            self.set32(manager + offset, pointer)
        self.label = 'fixture'
        self.set32(0xccc838, manager)
        return manager

    def sphere_layers(self, manager):
        owner = self.read32(manager + 0x94)
        layers = self.read32(owner + 0x94)
        return owner, [layers, layers + 0x6c]

    def reset_sphere(self, manager):
        self.invoke(0x685370, (1,), this=manager)
        for layer in self.sphere_layers(manager)[1]:
            assert self.read32(layer+4) == self.read32(layer+8) == 0

    def sprite(self):
        pointer = self.alloc(0x200, 'synthetic sprite')
        sprite_fixture.install(self.write_bytes,self.set32,pointer)
        return pointer

    def packet(self):
        packet = self.alloc(0x80, 'synthetic draw packet')
        self.write_bytes(packet+4,sprite_fixture.MODULATION)
        matrix = self.alloc(64, 'identity matrix')
        self.write_bytes(matrix, struct.pack('<16f', 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1))
        self.set32(packet+0x10, matrix)
        return packet

    def draw(self, sprite, packet, command):
        return self.invoke(0x7f4900, (sprite, packet, command, 0))

    def summary(self):
        return dict(services=dict(self.services), native_calls=dict(self.native_calls),
                    allocated=self.total_allocated, live=self.live_bytes, peak=self.high_water,
                    frees=len(self.freed), failure=self.failure)
