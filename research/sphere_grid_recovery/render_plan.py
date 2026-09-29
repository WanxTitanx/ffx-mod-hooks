"""Pure-data Sphere Grid changes for isolated emulator experiments.

Every entry is a complete decoded instruction from the supported executable.
No file/process is opened or modified here. Render capacity is not logical
node capacity, save compatibility or live acceptance.
"""
from dataclasses import dataclass
import struct


@dataclass(frozen=True)
class Change:
    va: int
    before: bytes
    after: bytes
    family: str


def plan(capacity):
    # Four vertices per quad, with native uint16 triangle indices.
    if type(capacity) is not int or not 861 <= capacity <= 16384:
        raise ValueError('render capacity outside [861, 16384]')
    specs = [
        (0x6823fb, 'c7440704740d0000', 4, capacity*4, 'allocation'),
        (0x682409, 'c74407082e140000', 4, capacity*6, 'allocation'),
        (0x68241c, '6840d70000', 1, capacity*64, 'allocation'),
        (0x682430, '685c280000', 1, capacity*12, 'allocation'),
        (0x682444, '68a06b0000', 1, capacity*32, 'allocation'),
        (0x682466, '6870a10000', 1, capacity*48, 'allocation'),
        (0x6824a8, '6840d70000', 1, capacity*64, 'allocation'),
        (0x6824be, '685c280000', 1, capacity*12, 'allocation'),
        (0x6824d4, '68a06b0000', 1, capacity*32, 'allocation'),
        (0x6824ed, '6870a10000', 1, capacity*48, 'allocation'),
        (0x7f4c20, '81fba2fcffff', 2, -(capacity+1), 'protocol'),
        (0x7f5610, '81fb5d030000', 2, capacity, 'protocol'),
        (0x7f567a, '81eb5d030000', 2, capacity, 'protocol'),
        (0xa514ca, '055c030000', 1, capacity-1, 'producer'),
        (0xa514f8, '68a2fcffff', 1, -(capacity+1), 'producer'),
    ]
    result = []
    for va, text, offset, value, family in specs:
        before = bytes.fromhex(text)
        after = bytearray(before)
        struct.pack_into('<I', after, offset, value & 0xffffffff)
        result.append(Change(va, before, bytes(after), family))
    return tuple(result)
