"""Bounded synthetic panel table for the original 7AB890 lookup routine.

The single-group header and row offsets follow the inspected native lookup and
A54860/A5AA30 consumers. This is test data, not an extracted panel.bin or a
replacement of those native functions. Unspecified rows intentionally do nothing;
the activation adapter separately requires a declared test row.
"""
from __future__ import annotations
import struct

def integer(value: int, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        raise ValueError('panel field is out of range')
    return value

def encode(rows: dict[int, tuple[int, int, int]], *, count: int=130) -> bytes:
    integer(count, 1, 130)
    if type(rows) is not dict:
        raise ValueError('explicit panel rows are required')
    # 8-byte table header, one 12-byte range descriptor, then 24-byte rows.
    data = bytearray(struct.pack('<4H4HI', 1, 0, 0, 0,
                                0, count-1, 24, count*24, 20))
    # Keep the text pointer returned by 7AB890 inside owned storage.
    data.extend(bytes(count*24+1))
    for index, fields in rows.items():
        integer(index, 0, count-1)
        if type(fields) is not tuple or len(fields) != 3:
            raise ValueError('panel row requires effect mask, command and amount')
        mask, command, amount = fields
        integer(mask, 0, 0x7ff); integer(command, 0, 0xffff)
        integer(amount, 0, 255)
        struct.pack_into('<HHB', data, 20+index*24+16, mask, command, amount)
    return bytes(data)

def lookup(data: bytes, index: int) -> tuple[int, int, int]:
    if type(data) is not bytes or len(data) < 45 or len(data) > 3141:
        raise ValueError('invalid synthetic panel table length')
    if struct.unpack_from('<4H', data) != (1, 0, 0, 0):
        raise ValueError('unsupported synthetic panel table header')
    first, last, stride, text_offset, offset = struct.unpack_from('<4HI', data, 8)
    integer(last, 0, 129); integer(index, 0, last)
    if first != 0 or stride != 24 or offset != 20 or text_offset != (last+1)*24:
        raise ValueError('invalid synthetic panel descriptor')
    if len(data) != offset+(last+1)*stride+1 or data[-1] != 0:
        raise ValueError('truncated or trailing synthetic panel data')
    return struct.unpack_from('<HHB', data, offset+index*stride+16)
