"""Bounded instrumentation for private emulation; never patches native memory.

An address alone cannot identify a stale callback after an allocator reuses it.
Use Handle or pass the expected owner/generation at every callback boundary.
The ledger does not claim to infer provenance from arbitrary integer bit patterns.
"""
from __future__ import annotations
from bisect import bisect_left, bisect_right
from dataclasses import dataclass, replace


class MemoryViolation(RuntimeError):
    """A specific allocation, lifetime or protocol invariant failed."""


def _integer(value: int, low: int, high: int, label: str) -> int:
    if type(value) is not int or not low <= value <= high:
        raise MemoryViolation(f'{label}: expected integer in [{low}, {high}]')
    return value


@dataclass(frozen=True)
class Handle:
    address: int
    length: int
    owner: str
    generation: int
    serial: int


@dataclass
class Statistics:
    reads: int = 0
    writes: int = 0
    maximum_end: int = 0


@dataclass
class _Record:
    handle: Handle
    alive: bool
    statistics: Statistics


class Ledger:
    """Exact allocation extents and bounded counters, independent of page size.

    The emulator owns allocation placement. Recycled slots may reuse their exact
    base, but splitting old slots is intentionally unsupported. This preserves
    identifiable tombstones without an unbounded history of every allocation.
    """
    def __init__(self, *, byte_budget: int = 64*1024*1024, record_budget: int = 4096):
        self.byte_budget = _integer(byte_budget, 1, 0xffffffff, 'byte budget')
        self.record_budget = _integer(record_budget, 1, 262144, 'record budget')
        self._records: dict[int, _Record] = {}
        self._starts: list[int] = []
        self._serial = 0
        self.live_bytes = 0
        self.peak_bytes = 0

    @property
    def record_count(self) -> int:
        return len(self._records)

    def register(self, address: int, length: int, *, owner: str, generation: int) -> Handle:
        _integer(address, 1, 0xffffffff, 'allocation address')
        _integer(length, 1, 0xffffffff, 'allocation size')
        _integer(generation, 1, 0xffffffffffffffff, 'generation')
        if type(owner) is not str or not owner or len(owner) > 128:
            raise MemoryViolation('allocation owner must be a bounded nonempty label')
        end = address+length
        if end > 0x100000000:
            raise MemoryViolation('allocation address wraps 32-bit space')
        old = self._records.get(address)
        if old is not None and old.alive:
            raise MemoryViolation('allocation overlap with a live owner')
        if old is None and self.record_count >= self.record_budget:
            raise MemoryViolation('allocation record budget exhausted')
        if self.live_bytes+length > self.byte_budget:
            raise MemoryViolation('live allocation byte budget exhausted')
        index = bisect_left(self._starts, address)
        previous = self._records[self._starts[index-1]] if index else None
        following_index = index+1 if old is not None else index
        following = self._records[self._starts[following_index]] if following_index < len(self._starts) else None
        if previous and previous.handle.address+previous.handle.length > address:
            raise MemoryViolation('allocation overlap with an existing slot')
        if following and end > following.handle.address:
            raise MemoryViolation('allocation overlap with an existing slot')
        self._serial += 1
        handle = Handle(address, length, owner, generation, self._serial)
        self._records[address] = _Record(handle, True, Statistics())
        if old is None:
            self._starts.insert(index, address)
        self.live_bytes += length
        self.peak_bytes = max(self.peak_bytes, self.live_bytes)
        return handle

    def _record(self, handle: Handle) -> _Record:
        if not isinstance(handle, Handle):
            raise MemoryViolation('invalid allocation handle')
        record = self._records.get(handle.address)
        if record is None or record.handle != handle:
            raise MemoryViolation('stale allocation handle or reused address')
        if not record.alive:
            raise MemoryViolation('allocation has been released')
        return record

    def access(self, handle: Handle, offset: int, size: int, *, write: bool) -> None:
        record = self._record(handle)
        if type(offset) is not int or offset < 0:
            raise MemoryViolation('allocation underrun')
        _integer(size, 1, 0xffffffff, 'access size')
        if offset > handle.length or size > handle.length-offset:
            raise MemoryViolation('allocation overrun')
        stats = record.statistics
        if write:
            stats.writes += 1
            stats.maximum_end = max(stats.maximum_end, offset+size)
        else:
            stats.reads += 1

    def containing(self, address: int, size: int) -> Handle | None:
        _integer(address, 0, 0xffffffff, 'access address')
        _integer(size, 1, 0xffffffff, 'access size')
        end = address+size
        if end > 0x100000000:
            raise MemoryViolation('access address wraps 32-bit space')
        index = bisect_right(self._starts, address)-1
        if index >= 0:
            record = self._records[self._starts[index]]
            handle = record.handle
            if address < handle.address+handle.length:
                self._record(handle)
                if end > handle.address+handle.length:
                    raise MemoryViolation('allocation overrun across exact byte boundary')
                return handle
        following = index+1
        if following < len(self._starts) and end > self._starts[following]:
            raise MemoryViolation('allocation underrun across exact byte boundary')
        return None

    def access_address(self, address: int, size: int, *, write: bool,
                       owner: str, generation: int) -> None:
        handle = self.containing(address, size)
        if handle is None:
            raise MemoryViolation('unknown allocation address')
        if handle.owner != owner:
            raise MemoryViolation('callback owner mismatch')
        if handle.generation != generation:
            raise MemoryViolation('callback generation mismatch')
        self.access(handle, address-handle.address, size, write=write)

    def release(self, handle: Handle, *, owner: str, generation: int) -> None:
        record = self._record(handle)
        if owner != handle.owner:
            raise MemoryViolation('release owner mismatch; borrowed pointer')
        if generation != handle.generation:
            raise MemoryViolation('release generation mismatch')
        record.alive = False
        self.live_bytes -= handle.length

    def stats(self, handle: Handle) -> Statistics:
        return replace(self._record(handle).statistics)


@dataclass(frozen=True)
class RenderPlan:
    """One indexed-quad layer, NOT the whole logical Sphere Grid.

    16-bit stores permit 65536 addressed vertices, hence 16384 four-vertex quads.
    Raising this plan does not relocate menu arrays or enlarge native save data.
    """
    quads: int

    def __post_init__(self):
        if type(self.quads) is not int or self.quads < 1:
            raise MemoryViolation('quad capacity must be a positive integer')
        if self.quads > 16384:
            raise MemoryViolation('quad capacity exceeds 16-bit triangle index representation')

    @classmethod
    def for_nodes(cls, nodes: int, *, extra_quads: int) -> RenderPlan:
        _integer(nodes, 1, 16384, 'logical node budget')
        _integer(extra_quads, 0, 16384, 'explicit overlay quad budget')
        return cls(nodes+extra_quads)

    @property
    def proves_native_layout(self) -> bool:
        return False

    @property
    def buffer_sizes(self) -> dict[str, int]:
        return {name: self.quads*stride for name,stride in
                (('positions',48),('colors',64),('uv',32),('indices',12))}

    @property
    def maximum_vertex_index(self) -> int:
        return self.quads*4-1

    @property
    def append_negative(self) -> int:
        return -self.quads-1

    def positive(self, slot: int) -> int:
        return self.quads+_integer(slot, 0, self.quads-1, 'quad slot')

    def negative(self, slot: int) -> int:
        return -1-_integer(slot, 0, self.quads-1, 'quad slot')

    def decode(self, command: int) -> tuple[str, int | None]:
        _integer(command, -0x80000000, 0x7fffffff, 'draw command')
        if command == 0xffff:
            return 'append_both_indices', None
        if command == self.append_negative:
            return 'append_second', None
        if command < 0:
            slot = -1-command
            _integer(slot, 0, self.quads-1, 'negative fixed slot')
            return 'fixed_second', slot
        if command < self.quads:
            return 'fixed_first', command
        slot = command-self.quads
        _integer(slot, 0, self.quads-1, 'positive append slot')
        return 'append_first', slot

    def check_append(self, *, vertices: int, indices: int) -> int:
        _integer(vertices, 0, 0xffffffff, 'vertex count')
        _integer(indices, 0, 0xffffffff, 'index count')
        if vertices % 4 or indices % 6 or vertices//4 != indices//6:
            raise MemoryViolation('append counters have inconsistent vertex/index units')
        slot = vertices//4
        if slot >= self.quads:
            raise MemoryViolation('append buffer is full; no store is admitted')
        return slot
