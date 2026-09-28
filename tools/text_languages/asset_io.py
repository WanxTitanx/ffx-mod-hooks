"""Bounded readers for MOD-006 authoring; they never modify source assets.

Jarvis-HOOK, 2026-09-27. Format references: FFX Editor (GPL-3.0),
research_tools/Ps2/vbf_reader.py and FfxLib/Ps3/Ps3MagicTextureWriter.cs.
The parsers below add closed bounds, archive-header verification and checked reads.
See docs/mods/MOD006_TEXT_LANGUAGES_PROVENANCE.md for the source ledger.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import io
from pathlib import Path
import struct
from typing import BinaryIO
import zlib

MAX_ASSET = 64 * 1024 * 1024
BLOCK = 65536


class AssetError(ValueError):
    """The input is not within the demonstrated asset contract."""


def u16(data: bytes | bytearray, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise AssetError('Truncated u16 field')
    return struct.unpack_from('<H', data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise AssetError('Truncated u32 field')
    return struct.unpack_from('<I', data, offset)[0]


def digest(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


class VbfArchive:
    """Read selected SRYK members without extracting the full game archive.

    The header MD5 is the archive's existing integrity check, not a signature of
    trust. Each exported member is additionally fingerprinted with SHA-256.
    """
    def __init__(self, source: Path | str | BinaryIO):
        self._owns = isinstance(source, (Path, str))
        self._file = open(source, 'rb') if self._owns else source
        try:
            self._file.seek(0, io.SEEK_END)
            self.size = self._file.tell()
            self._file.seek(0)
            lead = self._read_exact(16)
            magic, self.header_size, count = struct.unpack('<4sIQ', lead)
            if magic != b'SRYK' or not 1 <= count <= 1_000_000:
                raise AssetError('Unsupported VBF header')
            minimum = 16 + count * 48 + 4
            if not minimum <= self.header_size <= min(self.size - 16, 64 * 1024 * 1024):
                raise AssetError('VBF header size is out of range')
            self._file.seek(0)
            header = self._read_exact(self.header_size)
            self._file.seek(self.size - 16)
            if self._read_exact(16) != hashlib.md5(header).digest():
                raise AssetError('VBF header digest mismatch')
            entries_at = 16 + count * 16
            names_at = entries_at + count * 32
            names_size = u32(header, names_at)
            if names_size < 4 or names_at + names_size > len(header):
                raise AssetError('VBF name table exceeds the header')
            raw_names = header[names_at + 4:names_at + names_size]
            try:
                self.names = tuple(raw_names.rstrip(b'\0').decode('utf-8', 'strict').split('\0'))
            except UnicodeError as exc:
                raise AssetError('VBF names are not valid UTF-8') from exc
            if len(self.names) != count:
                raise AssetError('VBF member/name count mismatch')
            self._entries: dict[bytes, tuple[int, int, int]] = {}
            blocks_at = names_at + names_size
            self._blocks = header[blocks_at:]
            if len(self._blocks) % 2:
                raise AssetError('VBF block table is misaligned')
            for i in range(count):
                key = header[16+i*16:32+i*16]
                block, _, size, offset, _ = struct.unpack_from('<IIQQQ', header, entries_at+i*32)
                if key in self._entries:
                    raise AssetError('VBF has duplicate member fingerprints')
                self._entries[key] = (block, size, offset)
        except Exception:
            if self._owns:
                self._file.close()
            raise

    def _read_exact(self, count: int) -> bytes:
        data = self._file.read(count)
        if len(data) != count:
            raise AssetError('Truncated VBF input')
        return data

    def read(self, name: str, limit: int = MAX_ASSET) -> bytes:
        key = hashlib.md5(name.replace('\\', '/').lower().encode('utf-8')).digest()
        member = self._entries.get(key)
        if member is None:
            raise AssetError(f'VBF member is absent: {name}')
        first, size, offset = member
        if size > limit or limit > MAX_ASSET or limit < 0:
            raise AssetError('VBF member exceeds the resource limit')
        if size == 0:
            return b''  # Empty entries may carry intentionally invalid offsets.
        count = (size + BLOCK - 1) // BLOCK
        if first + count > len(self._blocks) // 2:
            raise AssetError('VBF member block range is invalid')
        lengths = [u16(self._blocks, 2*(first+i)) or BLOCK for i in range(count)]
        if offset < self.header_size or offset + sum(lengths) > self.size - 16:
            raise AssetError('VBF member storage range is invalid')
        self._file.seek(offset)
        output = bytearray()
        for i, length in enumerate(lengths):
            expected = min(BLOCK, size - i*BLOCK)
            stored = self._read_exact(length)
            if length == expected:
                decoded = stored
            elif length < expected:
                try:
                    decoder = zlib.decompressobj()
                    decoded = decoder.decompress(stored, expected + 1)
                    if len(decoded) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
                        raise AssetError('VBF compressed block has an invalid extent')
                except zlib.error as exc:
                    raise AssetError('VBF compressed block is corrupt') from exc
            else:
                raise AssetError('VBF stored block exceeds its logical extent')
            output += decoded
        if len(output) != size:
            raise AssetError('VBF member output length mismatch')
        return bytes(output)

    def close(self) -> None:
        if self._owns:
            self._file.close()

    def __enter__(self) -> VbfArchive:
        return self

    def __exit__(self, *_args) -> None:
        self.close()


@dataclass(frozen=True)
class FtcLayout:
    count: int
    metrics: int
    cell_width: int
    cell_height: int
    page_width: int
    page_height: int


def ftc_layout(data: bytes | bytearray) -> FtcLayout:
    # This lane uses the existing Western metrics-only slot; no static pool grows.
    if len(data) < 64 or data[:4] != b'FTCX' or u16(data, 4) != 200 or u16(data, 8) != 4:
        raise AssetError('Expected Western FTCX slot 4, version 200')
    count, metrics = u32(data, 16), u32(data, 48)
    cw, ch, pw, ph = u16(data, 20), u16(data, 22), u16(data, 40), u16(data, 42)
    if count != 230 or u32(data, 36) != 0 or metrics < 64 or metrics + count > len(data):
        raise AssetError('Western FTCX count, payload or metrics range is unsupported')
    if (cw, ch, pw, ph) != (14, 18, 128, 234):
        raise AssetError('Western FTCX geometry is unsupported')
    return FtcLayout(count, metrics, cw, ch, pw, ph)


@dataclass(frozen=True)
class PhyreLayout:
    width: int
    height: int
    format: str
    pixels: int
    pixel_size: int
    instance: int


def phyre_layout(data: bytes | bytearray) -> PhyreLayout:
    if len(data) < 96 or len(data) > MAX_ASSET or data[:5] != b'RYHPT':
        raise AssetError('Expected a bounded RYHPT texture')
    matches = []
    start = 0
    while True:
        hit = data.find(b'PTexture2D\0', start, min(len(data), 16384))
        if hit < 0:
            break
        start = hit + 1
        token = bytes(data[hit+11:hit+21]).split(b'\0', 1)[0]
        if token not in (b'ARGB8', b'DXT1', b'DXT3', b'DXT5', b'L8'):
            continue
        width, height = u32(data, hit-88), u32(data, hit-84)
        if not 1 <= width <= 8192 or not 1 <= height <= 8192:
            raise AssetError('Phyre texture dimensions are invalid')
        fmt = token.decode('ascii')
        pixels = hit + 11 + len(token) + 38
        size = width*height*(4 if fmt == 'ARGB8' else 1)
        if fmt.startswith('DXT'):
            size = ((width+3)//4)*((height+3)//4)*(8 if fmt == 'DXT1' else 16)
        if pixels + size > len(data):
            raise AssetError('Phyre pixel payload is truncated')
        matches.append(PhyreLayout(width, height, fmt, pixels, size, hit))
    if len(matches) != 1:
        raise AssetError('Expected exactly one supported Phyre texture instance')
    return matches[0]
