#!/usr/bin/env python3
"""Audit or author an ABMAP pair in a NEW research directory; never deploy it.

A manifest is the last completion marker. An existing output path is always
refused. The generated filenames intentionally are not game override filenames.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import codec

UNVERIFIED = (
    'Native capture begin/reset and reserved overlay quad count',
    'GPU submission and graphics-state restoration after menu exit',
    'Full activation effect chain and old descriptor consumers',
    'Native release, asynchronous consumers and reopen lifetime',
    'Native spatial coordinate domain and every node-type resource',
    'Real save checksum, identity and load/new-game integration',
    'In-game open/navigate/activate/close/reopen/load sequence',
)


def read_input(path: Path) -> bytes:
    with path.open('rb') as stream:
        data = stream.read(codec.MAX_INPUT_BYTES+1)
    if len(data) > codec.MAX_INPUT_BYTES:
        raise codec.FormatError(f'{path.name}: input exceeds the bounded byte limit')
    return data


def report(grid: codec.Grid, layout: bytes, contents: bytes, *, panel_catalog: bool = False) -> dict:
    def digest(data: bytes) -> dict:
        return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    return {'schema':1,'kind':'sphere-grid-offline-research-pair',
            'structural_validation':True,'production_ready':False,'runtime_verified':False,
            'panel_catalog_verified':panel_catalog,
            'counts':{'clusters':len(grid.clusters),'nodes':len(grid.nodes),'links':len(grid.links)},
            'outputs':{'layout.dat':digest(layout),'contents.dat':digest(contents)},
            'native_blockers':list(codec.native_blockers(grid)),
            'unverified_boundaries':list(UNVERIFIED)}


def _write_exclusive(path: Path, data: bytes) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os,'O_NOFOLLOW',0)
    descriptor = os.open(path,flags,0o600)
    try:
        with os.fdopen(descriptor,'wb',closefd=False) as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        os.close(descriptor)


def export_new(output: Path, grid: codec.Grid, *, limits: codec.Limits = codec.NATIVE_LAYOUT_LIMITS,
               panel_ids: set[int] | None = None, source_hashes: dict | None = None) -> dict:
    layout,contents = codec.encode(grid,limits=limits,panel_ids=panel_ids)
    manifest = report(grid,layout,contents,panel_catalog=panel_ids is not None)
    if source_hashes is not None:
        manifest['inputs'] = source_hashes
    serialized = (json.dumps(manifest,indent=2,sort_keys=True,allow_nan=False)+'\n').encode('utf-8')
    output = Path(output)
    output.mkdir(parents=False,exist_ok=False)
    # A failed bundle remains in this new directory for inspection. Never
    # unlink files after an I/O error: another writer could have replaced the
    # pathname, and a pathname alone is not an ownership token.
    _write_exclusive(output/'layout.dat',layout)
    _write_exclusive(output/'contents.dat',contents)
    prepared=output/'.manifest.prepared'
    _write_exclusive(prepared,serialized)
    # A hard link publishes a fully flushed marker without replacing an existing
    # name. Unsupported filesystems fail closed. Keep the prepared name, too;
    # no delete/rename operation is needed, including failure cleanup.
    os.link(prepared,output/'manifest.json',follow_symlinks=False)
    return manifest


def _parse_integer(value: str) -> int:
    return int(value,0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['audit','append'])
    parser.add_argument('layout',type=Path)
    parser.add_argument('contents',type=Path)
    parser.add_argument('--authoring-only',action='store_true',help='Offline expanded counts; NOT native support')
    parser.add_argument('--panel-ids',type=Path,help='JSON array of actually available panel row IDs')
    parser.add_argument('--output',type=Path,help='New research directory for append')
    for field in ('donor','connect-to','x','y','content'):
        parser.add_argument('--'+field,type=_parse_integer)
    args = parser.parse_args(argv)
    if args.action=='append' and any(getattr(args,field) is None for field in
                                     ('output','donor','connect_to','x','y','content')):
        parser.error('append requires --output --donor --connect-to --x --y --content')
    if args.action=='audit' and args.output is not None:
        parser.error('audit is read-only and has no output directory')
    try:
        limits = codec.AUTHORING_LIMITS if args.authoring_only else codec.NATIVE_LAYOUT_LIMITS
        panel_ids = None
        if args.panel_ids:
            values = json.loads(read_input(args.panel_ids))
            if not isinstance(values,list) or any(type(x) is not int or not 0<=x<255 for x in values):
                raise codec.FormatError('panel IDs must be a JSON array of integer row IDs in [0,254]')
            if len(set(values))!=len(values): raise codec.FormatError('duplicate panel IDs')
            panel_ids = set(values)
        layout,contents = read_input(args.layout),read_input(args.contents)
        grid = codec.parse(layout,contents,limits=limits,panel_ids=panel_ids)
        if args.action=='audit':
            result = report(grid,layout,contents,panel_catalog=panel_ids is not None)
        else:
            result_grid = codec.append_node(grid,donor=args.donor,connect_to=args.connect_to,
                x=args.x,y=args.y,content=args.content,limits=limits,panel_ids=panel_ids)
            result = export_new(args.output,result_grid,limits=limits,panel_ids=panel_ids,
                                source_hashes={name:hashlib.sha256(data).hexdigest() for name,data in
                                               [('layout',layout),('contents',contents)]})
        print(json.dumps(result,indent=2,sort_keys=True))
        return 0
    except (OSError,ValueError) as error:
        print(f'Sphere Grid research operation refused: {error}',file=sys.stderr)
        return 3


if __name__=='__main__': raise SystemExit(main())
