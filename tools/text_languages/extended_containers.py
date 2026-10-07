"""Bounded macro dictionary authoring. No codec, file IO or game mutations."""
from __future__ import annotations
import struct
from asset_io import AssetError, u16


def chunks(data: bytes) -> list[tuple[int, int, int]]:
    if len(data) < 64:
        raise AssetError('Truncated macro dictionary header')
    offsets = struct.unpack_from('<16I', data)
    active = sorted((offset, index) for index, offset in enumerate(offsets) if offset)
    if not active or active[0][0] != 64:
        raise AssetError('Missing macro chunks or unreferenced header gap')
    result = []
    for n, (start, index) in enumerate(active):
        end = active[n+1][0] if n+1 < len(active) else len(data)
        if not 64 <= start < end <= len(data) or end-start > 65536:
            raise AssetError('Invalid, aliased or oversized macro chunk')
        result.append((index, start, end))
    return result


def chunk_rows(data: bytes) -> dict[tuple[int, int], bytes | None]:
    header = u16(data, 0)
    if header < 4 or header % 4 or header >= len(data):
        raise AssetError('Invalid macro row table')
    result = {}
    for row in range(header // 4):
        for slot in range(2):
            offset = u16(data, row*4+slot*2)
            if not offset or offset >= len(data):
                result[row, slot] = None
                continue
            if offset < header:
                raise AssetError('Macro reference points into its header')
            end = data.find(b'\0', offset)
            if end < 0:
                raise AssetError('Unterminated macro string')
            result[row, slot] = data[offset:end]
    return result


def scripts(data: bytes) -> dict[tuple[int, int], bytes | None]:
    result = {}
    for index, start, end in chunks(data):
        for (row, slot), value in chunk_rows(data[start:end]).items():
            result[index*65536+row, slot] = value
    return result


def rebuild_macro(data: bytes, replacements: dict[tuple[int, int], bytes]) -> bytes:
    parts = chunks(data)
    original = scripts(data)
    for identity in replacements:
        if identity not in original or original[identity] is None:
            raise AssetError('Macro edit does not identify an existing text string')
    if all(original[key] == value for key, value in replacements.items()):
        return data
    output = bytearray(64)
    for index, start, end in parts:
        source = data[start:end]
        rows = chunk_rows(source)
        changed = any((index*65536+row, slot) in replacements and
                      replacements[index*65536+row, slot] != value
                      for (row, slot), value in rows.items())
        result = source
        if changed:
            header = u16(source, 0)
            target, known, inactive = bytearray(source[:header]), {}, []
            for (row, slot), old in rows.items():
                at = row*4+slot*2
                if old is None:
                    inactive.append(u16(source, at))
                    continue
                value = replacements.get((index*65536+row, slot), old)
                if value not in known:
                    if len(target)+len(value)+1 > 65536:
                        raise AssetError('Macro translation exceeds u16 chunk capacity')
                    known[value] = len(target)
                    target += value+b'\0'
                struct.pack_into('<H', target, at, known[value])
            if any(offset and offset < len(target) for offset in inactive):
                raise AssetError('Rebuilt macro activates an invalid native reference')
            result = bytes(target)
        struct.pack_into('<I', output, index*4, len(output))
        output += result
    return bytes(output)
