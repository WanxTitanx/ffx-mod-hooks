"""Lossless, strict offline ABMAP layout/content pairing. No game/process I/O.

Packed layouts were checked against the user's editor and original assets;
native A45570 derives cells and A47210 reads the contents count at offset 2.
Structural validation is not permission to deploy or proof of live rendering.
"""
from __future__ import annotations
from dataclasses import dataclass, replace
import struct

MAX_NODES, MAX_LINKS, MAX_CLUSTERS, MAX_DEGREE = 1024, 1024, 128, 5
NODE, LINK = struct.Struct('<hh4H'), struct.Struct('<4H')


def integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{name} outside [{low}, {high}]')
    return value


def bucket(x, y):
    integer(x, -32768, 32767, 'x')
    integer(y, -32768, 32767, 'y')
    def div(v):
        return v // 256 if v >= 0 else -((-v) // 256)
    return (div(x + 2560) + 20 * div(y + 2336)) & 0xffff


@dataclass(frozen=True)
class Node:
    x: int
    y: int
    unknown: int
    content_word: int
    cluster: int
    cell: int

    def encode(self):
        return NODE.pack(self.x, self.y, self.unknown, self.content_word, self.cluster, self.cell)


@dataclass(frozen=True)
class Link:
    first: int
    second: int
    anchor: int
    unknown: int

    def encode(self):
        return LINK.pack(self.first, self.second, self.anchor, self.unknown)


@dataclass(frozen=True)
class Grid:
    layout_header: bytes
    contents_header: bytes
    clusters: tuple
    nodes: tuple
    links: tuple
    contents: bytes

    @property
    def node_count(self):
        return len(self.nodes)

    def validate(self):
        nc, lc, cc = len(self.nodes), len(self.links), len(self.clusters)
        integer(nc, 1, MAX_NODES, 'node capacity')
        integer(lc, 0, MAX_LINKS, 'link capacity')
        integer(cc, 1, MAX_CLUSTERS, 'cluster capacity')
        if len(self.layout_header) != 16 or len(self.contents_header) != 8:
            raise ValueError('invalid header sizes')
        if struct.unpack_from('<H', self.layout_header)[0] != 0x31 or struct.unpack_from('<H', self.contents_header)[0] != 0x31:
            raise ValueError('invalid ABMAP signature')
        if struct.unpack_from('<3H', self.layout_header, 2) != (cc, nc, lc):
            raise ValueError('layout count mismatch')
        if struct.unpack_from('<H', self.contents_header, 2)[0] != nc:
            raise ValueError('contents count mismatch')
        if len(self.contents) != nc or any(len(c) != 16 for c in self.clusters):
            raise ValueError('invalid record sizes')
        for index, node in enumerate(self.nodes):
            if not 0 <= node.cluster < cc:
                raise ValueError(f'node {index}: invalid cluster')
            expected = 0xffff if self.contents[index] == 0xff else self.contents[index]
            if node.content_word != expected:
                raise ValueError(f'node {index}: redundant content mismatch')
            if node.cell != bucket(node.x, node.y):
                raise ValueError(f'node {index}: spatial bucket mismatch')
        degrees, edges = [0] * nc, set()
        for index, link in enumerate(self.links):
            if not 0 <= link.first < nc or not 0 <= link.second < nc:
                raise ValueError(f'link {index}: dangling endpoint')
            if link.anchor != 0xffff and not 0 <= link.anchor < nc:
                raise ValueError(f'link {index}: dangling anchor')
            if link.first == link.second:
                raise ValueError(f'link {index}: self link')
            edge = tuple(sorted((link.first, link.second)))
            if edge in edges:
                raise ValueError(f'link {index}: duplicate edge')
            edges.add(edge)
            degrees[link.first] += 1
            degrees[link.second] += 1
            if max(degrees[link.first], degrees[link.second]) > MAX_DEGREE:
                raise ValueError(f'link {index}: native node degree exceeds {MAX_DEGREE}')

    def encode(self):
        self.validate()
        return (self.layout_header + b''.join(self.clusters) + b''.join(n.encode() for n in self.nodes) + b''.join(l.encode() for l in self.links), self.contents_header + self.contents)

    def append_node(self, *, template, x, y, content, connect_to):
        self.validate()
        integer(template, 0, self.node_count - 1, 'template')
        integer(connect_to, 0, self.node_count - 1, 'connect_to')
        integer(content, 0, 255, 'content')
        cell = bucket(x, y)
        if self.node_count == MAX_NODES:
            raise ValueError('node capacity requires a separately verified runtime')
        node = replace(self.nodes[template], x=x, y=y, content_word=0xffff if content == 0xff else content, cell=cell)
        nodes = self.nodes + (node,)
        links = self.links + (Link(connect_to, self.node_count, 0xffff, 0),)
        header, content_header = bytearray(self.layout_header), bytearray(self.contents_header)
        struct.pack_into('<3H', header, 2, len(self.clusters), len(nodes), len(links))
        struct.pack_into('<H', content_header, 2, len(nodes))
        result = Grid(bytes(header), bytes(content_header), self.clusters, nodes, links, self.contents + bytes([content]))
        result.validate()
        return result

    def capacity_report(self):
        return dict(nodes=len(self.nodes), links=len(self.links), clusters=len(self.clusters), logical_node_limit=MAX_NODES, logical_link_limit=MAX_LINKS, maximum_degree=MAX_DEGREE, unmodified_render_quads=861, requires_render_capacity_change=len(self.nodes) > 861, live_accepted=False)


def parse_pair(layout, contents):
    if len(layout) < 16 or len(contents) < 8:
        raise ValueError('truncated ABMAP header')
    cc, nc, lc = struct.unpack_from('<3H', layout, 2)
    integer(nc, 1, MAX_NODES, 'node capacity')
    integer(lc, 0, MAX_LINKS, 'link capacity')
    integer(cc, 1, MAX_CLUSTERS, 'cluster capacity')
    if struct.unpack_from('<H', contents, 2)[0] != nc:
        raise ValueError('contents count does not match layout count')
    if len(layout) != 16 + cc * 16 + nc * 12 + lc * 8 or len(contents) != 8 + nc:
        raise ValueError('truncated or trailing ABMAP data')
    no = 16 + cc * 16
    lo = no + nc * 12
    result = Grid(bytes(layout[:16]), bytes(contents[:8]), tuple(bytes(layout[16+i*16:32+i*16]) for i in range(cc)), tuple(Node(*NODE.unpack_from(layout, no+i*12)) for i in range(nc)), tuple(Link(*LINK.unpack_from(layout, lo+i*8)) for i in range(lc)), bytes(contents[8:]))
    result.validate()
    return result
