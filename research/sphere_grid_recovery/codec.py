"""Strict, lossless packed ABMAP pair compiler. No game or process access.

The native-layout profile describes fixed storage, not gameplay acceptance.
AUTHORING_LIMITS are for offline research only; they do not expand FFX's arrays.
The contents header has its own 16-bit count at +2. See PROVENANCE.md.
"""
from __future__ import annotations
from dataclasses import dataclass, replace
import struct
from typing import AbstractSet


class FormatError(ValueError):
    """Inconsistent, unsupported or unsafe-to-serialize packed input."""


@dataclass(frozen=True)
class Limits:
    nodes: int
    links: int
    clusters: int
    node_type_slots: int
    degree: int = 5


NATIVE_LAYOUT_LIMITS = Limits(1024, 1024, 128, 130)
AUTHORING_LIMITS = Limits(32767, 32767, 32767, 255)
MAX_INPUT_BYTES = 4 * 1024 * 1024


@dataclass(frozen=True)
class Node:
    x: int
    y: int
    opaque: int
    type_word: int
    cluster: int
    bucket: int
    content: int


@dataclass(frozen=True)
class Link:
    a: int
    b: int
    anchor: int
    opaque: int


@dataclass(frozen=True)
class Grid:
    layout_header: tuple[int, ...]
    contents_header: tuple[int, ...]
    clusters: tuple[bytes, ...]
    nodes: tuple[Node, ...]
    links: tuple[Link, ...]


def integer(value: int, minimum: int, maximum: int, field: str) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise FormatError(f'{field}: expected integer in [{minimum}, {maximum}]')
    return value


def cell(x: int, y: int) -> int:
    """Mirror x86 signed division: truncate toward zero, not Python floor.

    This computes the serialized bucket only. It does not establish the native
    spatial-acceleration structure's supported coordinate domain.
    """
    integer(x, -32768, 32767, 'x'); integer(y, -32768, 32767, 'y')
    def trunc256(value: int) -> int:
        return value // 256 if value >= 0 else -((-value) // 256)
    return (trunc256(x + 2560) + 20 * trunc256(y + 2336)) & 0xffff


def _limits(limits: Limits) -> None:
    if not isinstance(limits, Limits):
        raise FormatError('invalid limits profile')
    for field in ('nodes', 'links', 'clusters'):
        integer(getattr(limits, field), 1, 32767, f'{field} limit')
    integer(limits.node_type_slots, 1, 255, 'node type slot limit')
    # Allowing more than five here would silently bypass the fixed native node ABI.
    if type(limits.degree) is not int or limits.degree != 5:
        raise FormatError('the native adjacency contract has exactly five link slots')


def validate(grid: Grid, *, limits: Limits = NATIVE_LAYOUT_LIMITS,
             panel_ids: AbstractSet[int] | None = None) -> None:
    _limits(limits)
    if not isinstance(grid, Grid):
        raise FormatError('expected a Grid')
    for field in ('layout_header','contents_header','clusters','nodes','links'):
        if type(getattr(grid,field)) is not tuple:
            raise FormatError(f'{field}: immutable tuple required')
    if len(grid.layout_header) != 8 or len(grid.contents_header) != 4:
        raise FormatError('invalid preserved header width')
    for value in grid.layout_header + grid.contents_header:
        integer(value, 0, 65535, 'header word')
    if grid.layout_header[0] != 49 or grid.contents_header[0] != 49:
        raise FormatError('unsupported ABMAP magic')
    integer(len(grid.clusters), 1, limits.clusters, 'cluster capacity')
    integer(len(grid.nodes), 1, limits.nodes, 'node capacity')
    integer(len(grid.links), 0, limits.links, 'link capacity')
    for index, cluster in enumerate(grid.clusters):
        if type(cluster) is not bytes or len(cluster) != 16:
            raise FormatError(f'cluster {index}: expected 16 preserved bytes')
        radius_type=struct.unpack_from('<H',cluster,6)[0]
        if radius_type>7:
            raise FormatError(f'cluster {index}: unknown native radius type')
    for index, node in enumerate(grid.nodes):
        if not isinstance(node, Node):
            raise FormatError(f'node {index}: invalid record')
        expected_bucket = cell(node.x, node.y)
        integer(node.opaque, 0, 65535, f'node {index} opaque')
        integer(node.cluster, 0, len(grid.clusters)-1, f'node {index} cluster')
        integer(node.content, 0, 255, f'node {index} content')
        integer(node.type_word, 0, 65535, f'node {index} type word')
        integer(node.bucket, 0, 65535, f'node {index} bucket')
        if node.bucket != expected_bucket:
            raise FormatError(f'node {index}: stale coordinate bucket')
        expected_word = 0xffff if node.content == 255 else node.content
        if node.type_word != expected_word:
            raise FormatError(f'node {index}: layout/content type mismatch')
        if node.content != 255:
            if node.content >= limits.node_type_slots:
                raise FormatError(f'node {index}: node type exceeds UI slot capacity')
            if panel_ids is not None and node.content not in panel_ids:
                raise FormatError(f'node {index}: content row missing from supplied panel catalog')
    degree = [0] * len(grid.nodes)
    seen: set[tuple[int, int]] = set()
    for index, link in enumerate(grid.links):
        if not isinstance(link, Link):
            raise FormatError(f'link {index}: invalid record')
        integer(link.a, 0, len(grid.nodes)-1, f'link {index} node A')
        integer(link.b, 0, len(grid.nodes)-1, f'link {index} node B')
        integer(link.anchor, 0, 65535, f'link {index} anchor')
        integer(link.opaque, 0, 65535, f'link {index} opaque')
        if link.anchor != 0xffff and link.anchor >= len(grid.nodes):
            raise FormatError(f'link {index}: dangling anchor')
        if link.a == link.b:
            raise FormatError(f'link {index}: self link')
        edge = (min(link.a, link.b), max(link.a, link.b))
        if edge in seen:
            raise FormatError(f'link {index}: duplicate endpoints')
        seen.add(edge)
        degree[link.a] += 1; degree[link.b] += 1
        if degree[link.a] > 5 or degree[link.b] > 5:
            raise FormatError(f'link {index}: node degree exceeds five native link slots')


def parse(layout: bytes, contents: bytes, *, limits: Limits = NATIVE_LAYOUT_LIMITS,
          panel_ids: AbstractSet[int] | None = None) -> Grid:
    _limits(limits)
    if type(layout) is not bytes or type(contents) is not bytes:
        raise FormatError('immutable byte inputs are required')
    if not 16 <= len(layout) <= MAX_INPUT_BYTES:
        raise FormatError('invalid layout length')
    if not 8 <= len(contents) <= MAX_INPUT_BYTES:
        raise FormatError('invalid contents length')
    lh = struct.unpack_from('<8H', layout)
    ch = struct.unpack_from('<4H', contents)
    if lh[0] != 49 or ch[0] != 49:
        raise FormatError('unsupported ABMAP magic')
    clusters, nodes, links = lh[1:4]
    integer(clusters, 1, limits.clusters, 'cluster capacity')
    integer(nodes, 1, limits.nodes, 'node capacity')
    integer(links, 0, limits.links, 'link capacity')
    if ch[1] != nodes:
        raise FormatError(f'contents header count {ch[1]} differs from layout node count {nodes}')
    if len(layout) != 16 + clusters*16 + nodes*12 + links*8:
        raise FormatError('layout length differs from all three declared table sizes')
    if len(contents) != 8 + nodes:
        raise FormatError('contents length differs from declared node count')
    offset = 16
    cluster_records = tuple(layout[offset+i*16:offset+(i+1)*16] for i in range(clusters))
    offset += clusters*16
    node_records = tuple(Node(*struct.unpack_from('<hh4H', layout, offset+i*12), contents[8+i])
                         for i in range(nodes))
    offset += nodes*12
    link_records = tuple(Link(*struct.unpack_from('<4H', layout, offset+i*8)) for i in range(links))
    grid = Grid(lh, ch, cluster_records, node_records, link_records)
    validate(grid, limits=limits, panel_ids=panel_ids)
    return grid


def encode(grid: Grid, *, limits: Limits = NATIVE_LAYOUT_LIMITS,
           panel_ids: AbstractSet[int] | None = None) -> tuple[bytes, bytes]:
    validate(grid, limits=limits, panel_ids=panel_ids)
    lh = list(grid.layout_header)
    lh[1:4] = [len(grid.clusters), len(grid.nodes), len(grid.links)]
    ch = list(grid.contents_header)
    ch[1] = len(grid.nodes)  # Independent native contents-initializer loop bound.
    layout = bytearray(struct.pack('<8H', *lh))
    for cluster in grid.clusters:
        layout.extend(cluster)
    for node in grid.nodes:
        layout.extend(struct.pack('<hh4H', node.x, node.y, node.opaque,
                                  node.type_word, node.cluster, node.bucket))
    for link in grid.links:
        layout.extend(struct.pack('<4H', link.a, link.b, link.anchor, link.opaque))
    contents = struct.pack('<4H', *ch) + bytes(node.content for node in grid.nodes)
    return bytes(layout), contents


def append_node(grid: Grid, *, donor: int, connect_to: int, x: int, y: int,
                content: int, limits: Limits = NATIVE_LAYOUT_LIMITS,
                panel_ids: AbstractSet[int] | None = None) -> Grid:
    """Append one real packed node and one connection, preserving all old IDs.

    Returns new immutable data only. This never changes a game asset, save, hook,
    in-process count or graphics pointer. Runtime acceptance is a separate gate.
    """
    validate(grid, limits=limits, panel_ids=panel_ids)
    integer(donor, 0, len(grid.nodes)-1, 'donor index')
    integer(connect_to, 0, len(grid.nodes)-1, 'connection index')
    integer(content, 0, 255, 'content')
    bucket = cell(x, y)
    seed = grid.nodes[donor]
    node = Node(x, y, seed.opaque, 0xffff if content == 255 else content,
                seed.cluster, bucket, content)
    result = replace(grid, nodes=grid.nodes+(node,),
                     links=grid.links+(Link(connect_to, len(grid.nodes), 0xffff, 0),))
    validate(result, limits=limits, panel_ids=panel_ids)
    return result


def native_blockers(grid: Grid) -> tuple[str, ...]:
    """Known storage blockers only; an empty result is not runtime approval."""
    problems = []
    if len(grid.nodes) > 1024:
        problems.extend(['embedded menu node array exceeds 1024',
                         'native stat-recomputation loop still visits 1024 states',
                         '10-bit node ordering and native save layout require redesign'])
    if len(grid.links) > 1024:
        problems.append('embedded menu link array exceeds 1024')
    if len(grid.clusters) > 128:
        problems.append('embedded cluster array exceeds 128')
    if any(n.content != 255 and n.content >= 130 for n in grid.nodes):
        problems.append('node type exceeds the 130-record UI storage')
    if len(grid.nodes) > 861:
        problems.append('node writer exceeds the measured 861-quad layer capacity')
    return tuple(problems)
