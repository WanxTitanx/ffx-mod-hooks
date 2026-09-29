"""Small bounded PE32 mapper for a read-only private executable fixture.

This does not invoke the Windows loader, imports, entrypoint, or relocations.
Native tests run only at the exact preferred base and use explicit dependency
fixtures. Optional expected values exist for synthetic mapper unit tests; the
native runner never exposes these identity overrides on its command line.
"""
from __future__ import annotations
from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct

EXPECTED_SHA256='78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced'
EXPECTED_IMAGE_SIZE=0x237d000
MAX_RAW_BYTES=64*1024*1024

class ImageError(ValueError):
    pass

@dataclass(frozen=True)
class Section:
    name: str
    rva: int
    length: int
    writable: bool
    executable: bool

@dataclass(frozen=True)
class MappedImage:
    base: int
    data: bytes
    sections: tuple[Section,...]
    sha256: str


def read_exact(path: Path) -> MappedImage:
    with path.open('rb') as stream:
        data=stream.read(MAX_RAW_BYTES+1)
    return map_verified(data)


def map_verified(raw: bytes, *, expected_sha256: str = EXPECTED_SHA256,
                 expected_image_size: int = EXPECTED_IMAGE_SIZE) -> MappedImage:
    if type(raw) is not bytes or not 64<=len(raw)<=MAX_RAW_BYTES:
        raise ImageError('invalid bounded PE input length')
    digest=hashlib.sha256(raw).hexdigest()
    if digest!=expected_sha256:
        raise ImageError('unsupported executable SHA-256; no native code executed')
    if raw[:2]!=b'MZ':raise ImageError('missing DOS signature')
    offset=struct.unpack_from('<I',raw,0x3c)[0]
    if offset<64 or offset>len(raw)-24 or raw[offset:offset+4]!=b'PE\0\0':
        raise ImageError('invalid PE signature offset')
    machine,count=struct.unpack_from('<HH',raw,offset+4)
    optional_size=struct.unpack_from('<H',raw,offset+20)[0]
    optional=offset+24
    if machine!=0x14c or not 1<=count<=96 or optional_size<96:
        raise ImageError('unsupported PE machine or section header')
    table=optional+optional_size
    if table+count*40>len(raw):raise ImageError('truncated PE section table')
    magic=struct.unpack_from('<H',raw,optional)[0]
    base=struct.unpack_from('<I',raw,optional+28)[0]
    size,header=struct.unpack_from('<II',raw,optional+56)
    if magic!=0x10b or base!=0x400000 or size!=expected_image_size or not 4096<=size<=MAX_RAW_BYTES:
        raise ImageError('unsupported PE32 image profile')
    if not table+count*40<=header<=min(len(raw),size):raise ImageError('invalid PE header size')
    entries=[];occupied=[(0,header)]
    for index in range(count):
        record=table+index*40
        virtual,rva,length,source=struct.unpack_from('<IIII',raw,record+8)
        flags=struct.unpack_from('<I',raw,record+36)[0]
        extent=max(virtual,length)
        if not extent or rva>size or extent>size-rva:
            raise ImageError('section exceeds image extent')
        if length and (source>len(raw) or length>len(raw)-source):
            raise ImageError('section raw bytes exceed executable')
        if any(rva<end and start<rva+extent for start,end in occupied):
            raise ImageError('overlapping image sections or headers')
        occupied.append((rva,rva+extent))
        name=raw[record:record+8].split(b'\0',1)[0].decode('ascii',errors='replace')
        section=Section(name,rva,extent,bool(flags&0x80000000),bool(flags&0x20000000))
        entries.append((section,length,source))
    image=bytearray(size);image[:header]=raw[:header]
    for section,length,source in entries:
        if length:image[section.rva:section.rva+length]=raw[source:source+length]
    return MappedImage(base,bytes(image),tuple(section for section,_,_ in entries),digest)
