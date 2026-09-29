"""Exact-byte experiments applied ONLY to a private emulator image copy.

This is not an installable hook. The sites cover one constructor, one writer and
one node producer. Other capture, submission, animation and menu consumers are
not thereby extended. No function accepts a process handle or writes a PE file.
"""
from __future__ import annotations
from dataclasses import dataclass
import struct


class PatchError(ValueError):
    """An entire emulated patch transaction was refused before publication."""


@dataclass(frozen=True)
class Patch:
    label: str
    rva: int
    expected: bytes
    immediate: int
    new_value: int

    def __post_init__(self):
        if not isinstance(self.label,str) or not self.label:
            raise PatchError('patch label is required')
        if type(self.rva) is not int or not 0 <= self.rva <= 0xffffffff:
            raise PatchError('invalid patch RVA')
        if type(self.expected) is not bytes or not 4 <= len(self.expected) <= 16:
            raise PatchError('invalid full instruction bytes')
        if type(self.immediate) is not int or not 0 <= self.immediate <= len(self.expected)-4:
            raise PatchError('immediate is outside instruction')
        if type(self.new_value) is not int or not -0x80000000 <= self.new_value <= 0xffffffff:
            raise PatchError('immediate does not fit its 32-bit representation')

    @property
    def replacement(self) -> bytes:
        code = bytearray(self.expected)
        struct.pack_into('<I',code,self.immediate,self.new_value & 0xffffffff)
        return bytes(code)


def render_plan(quads: int) -> tuple[Patch,...]:
    if type(quads) is not int or not 1 <= quads <= 16384:
        raise PatchError('quad capacity exceeds the 16-bit indexed-quad experiment')
    def site(label: str, va: int, code: str, offset: int, value: int) -> Patch:
        return Patch(label,va-0x400000,bytes.fromhex(code),offset,value)
    patches = [
        site('vertices',0x6823fb,'c7440704740d0000',4,quads*4),
        site('indices_count',0x682409,'c74407082e140000',4,quads*6),
    ]
    # Allocation and clearing lengths must change as a pair. Unrelated equal-
    # sized allocations elsewhere in the image are deliberately not searched.
    for name,stride,allocate,clear in [
        ('colors',64,0x68241c,0x6824a8),
        ('indices',12,0x682430,0x6824be),
        ('uv',32,0x682444,0x6824d4),
        ('positions',48,0x682466,0x6824ed),
    ]:
        code = b'\x68'+struct.pack('<I',861*stride)
        for operation,address in [('allocate',allocate),('clear',clear)]:
            patches.append(Patch(operation+'_'+name,address-0x400000,code,1,quads*stride))
    patches.extend([
        site('writer_positive_boundary',0x7f5610,'81fb5d030000',2,quads),
        site('writer_positive_decode',0x7f567a,'81eb5d030000',2,quads),
        site('writer_negative_append',0x7f4c20,'81fba2fcffff',2,-quads-1),
        site('producer_positive_bias',0xa514ca,'055c030000',1,quads-1),
        site('producer_negative_append',0xa514f8,'68a2fcffff',1,-quads-1),
    ])
    return tuple(patches)


def apply_private_image(image: bytes | bytearray, plan: tuple[Patch,...]) -> bytearray:
    """Preflight every complete instruction and return a modified COPY.

    The caller must have verified the exact source executable identity. Successful
    byte replacement is not a statement that all consumers have been covered.
    """
    if type(image) not in (bytes,bytearray) or not isinstance(plan,tuple) or not plan:
        raise PatchError('expected private image bytes and a nonempty immutable plan')
    if any(not isinstance(patch,Patch) for patch in plan):
        raise PatchError('invalid patch record')
    ordered = sorted(plan,key=lambda patch: patch.rva)
    end = -1
    labels = set()
    for patch in ordered:
        if not isinstance(patch,Patch):
            raise PatchError('invalid patch record')
        if patch.label in labels or patch.rva < end:
            raise PatchError('duplicate or overlapping patch')
        labels.add(patch.label)
        end = patch.rva+len(patch.expected)
        if end > len(image) or image[patch.rva:end] != patch.expected:
            raise PatchError(f'{patch.label}: exact source bytes did not match at RVA {patch.rva:#x}')
    output = bytearray(image)
    for patch in ordered:
        output[patch.rva:patch.rva+len(patch.expected)] = patch.replacement
    return output
