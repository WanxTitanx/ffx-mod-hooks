#!/usr/bin/env python3
"""Execute the exact native Sphere Grid writer against guarded allocations.

This is a writer unit fixture, NOT native capture-lifecycle or in-game proof.
The native constructor gives each descriptor full counts (3444 / 5166).
For append tests we explicitly seed synthetic EMPTY counters; no claim is made
that this models the native capture-begin path, which must be mapped separately.
The executable and game installation remain read-only. All stores are emulated.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path
from native_machine_tests import constructor, EXPECTED_SHA256
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP


class DrawFixture:
    def __init__(self, raw: bytes, *, empty: bool):
        self.machine, self.root, self.capacities = constructor(raw)
        self.cpu = self.machine.cpu
        m = self.machine
        self.layers = m.read32(self.root + 0x94)
        self.synthetic_empty_counts = empty
        if empty:
            for index in range(2):
                m.set32(self.layers + index * 0x6c + 4, 0)
                m.set32(self.layers + index * 0x6c + 8, 0)
        self.sprite = m.alloc(0x200, 'synthetic sprite resource')
        m.set32(self.sprite + 4, 0x100)
        m.set32(self.sprite + 0x10, 16128)
        self.packet = m.alloc(0x80, 'synthetic draw packet')
        matrix = m.alloc(0x40, 'synthetic identity matrix')
        self.cpu.mem_write(matrix, struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0,
                                              0, 0, 1, 0, 0, 0, 0, 1))
        m.set32(self.packet + 0x10, matrix)
        self.cpu.mem_write(self.sprite + 0x20, b'\xff' * 64)
        self.dependencies: set[str] = set()
        self.cpu.hook_add(UC_HOOK_CODE, self.external)
        self.machine.writes.clear()
        self.completed = 0

    def external(self, cpu, address, size, context):
        m = self.machine
        sp = cpu.reg_read(UC_X86_REG_ESP)
        if address == 0x639180:
            self.dependencies.add('synthetic capture owner, not native resolver')
            m.returning(self.root)
        elif address == 0x640f60:
            self.dependencies.add('synthetic viewport dimensions')
            for offset in (4, 8):
                cpu.mem_write(m.read32(sp + offset), struct.pack('<4I', 1920, 1080, 1920, 1080))
            m.returning()
        elif address == 0x94964c:
            destination, value, length = (m.read32(sp + x) for x in (4, 8, 12))
            assert 0x30000000 <= destination <= 0x30020000 - length and length < 0x10000
            cpu.mem_write(destination, bytes([value & 255]) * length)
            m.returning(destination)
        elif address == 0x949240:
            self.dependencies.add('synthetic security-cookie check')
            m.returning()

    def draw(self, command: int):
        self.machine.call(0x7f4900, (self.sprite, self.packet, command, 0))
        self.completed += 1

    def result(self) -> dict:
        m = self.machine
        layers = []
        for index in range(2):
            descriptor = self.layers + index * 0x6c
            # The numeric count and the index-buffer statistics need distinct keys.
            data = {'vertices': m.read32(descriptor + 4), 'index_count': m.read32(descriptor + 8)}
            for name, offset in [('positions', 12), ('colors', 20), ('uv', 24), ('indices', 28)]:
                start = m.read32(descriptor + offset)
                length = next(length for pointer, length, _ in m.allocations if pointer == start)
                stores = [(address, size) for address, size, _ in m.writes
                          if start <= address < start + length]
                data[name] = {'capacity_bytes': length, 'stores': len(stores),
                              'maximum_end_offset': max((address + size - start for address, size in stores), default=0)}
            layers.append(data)
        return {'completed': self.completed, 'synthetic_empty_counts': self.synthetic_empty_counts,
                'dependencies': sorted(self.dependencies), 'layers': layers}

    def expected_overrun(self, command: int) -> dict:
        try:
            self.draw(command)
        except AssertionError:
            failure = self.machine.failure
            if not failure or failure.get('kind') != 'allocation_overrun':
                raise  # An unmodelled native dependency is NOT an expected capacity failure.
            return failure
        raise AssertionError(f'expected an allocation-boundary failure for command {command}')


def run(raw: bytes) -> dict:
    result = {'evidence_level': 'native writer unit fixture; synthetic capture counters; no RT2',
              'executable_sha256': EXPECTED_SHA256}
    # Preserve the original fixture failure as a precondition regression: a
    # full constructor count cannot be used as an empty append cursor.
    full = DrawFixture(raw, empty=False)
    result['append_without_empty_counts'] = full.expected_overrun(861)
    for label, command in [('positive_append', lambda i: 861 + i),
                           ('negative_append_sentinel', lambda _: -862),
                           ('both_layers_index_sentinel', lambda _: 0xffff)]:
        fixture = DrawFixture(raw, empty=True)
        for index in range(861):
            fixture.draw(command(index))
        assert fixture.completed == 861
        data = fixture.result()
        active_layers = (0,) if label == 'positive_append' else (1,) if label == 'negative_append_sentinel' else (0, 1)
        for layer in active_layers:
            assert data['layers'][layer]['vertices'] == 3444
            assert data['layers'][layer]['index_count'] == 5166
        data['next_append_overrun'] = fixture.expected_overrun(command(861))
        result[label] = data
    for label, command in [('fixed_positive_last', 860), ('fixed_negative_last', -861)]:
        fixture = DrawFixture(raw, empty=True)
        fixture.draw(command)
        result[label] = fixture.result()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    raw = args.executable.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED_SHA256:
        raise SystemExit('Unsupported executable SHA-256; no fixture executed')
    print(json.dumps(run(raw), indent=2))


if __name__ == '__main__':
    main()
