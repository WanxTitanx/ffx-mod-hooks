#!/usr/bin/env python3
"""Independent negative controls for native math-path admission, no game process.

These controls are separate from the unchanged 46-case acceptance denominator.
The math code must remain original, missing paths must still stop execution, and
admission must not silently include the gaps between the observed path spans.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from native_harness import (DrawFixture, NATIVE_COORDINATE_PATHS,
                            NATIVE_NUMERIC_PATHS, expect_fault, require)
from pe_image import read_exact


def missing_path(mapped, *, numeric: bool) -> dict:
    fixture=DrawFixture(mapped,861)
    removed=NATIVE_NUMERIC_PATHS if numeric else NATIVE_COORDINATE_PATHS
    fixture.m.allowed_code=[span for span in fixture.m.allowed_code if span not in removed]
    fault=expect_fault(lambda:fixture.draw(-862 if numeric else 861),'unmodelled_native_callee')
    require(fault['address']==hex(0x9497e0 if numeric else 0x641250),
            'missing-path control failed for an unrelated reason')
    return {'detected':fault,'mutation_in_private_machine_only':True}


def original_math(mapped) -> dict:
    fixture=DrawFixture(mapped,861);m=fixture.m;spans=[]
    for start,end in (*NATIVE_COORDINATE_PATHS,*NATIVE_NUMERIC_PATHS):
        actual=m.get(start,end-start)
        require(actual==mapped.data[start-mapped.base:end-mapped.base],
                'a native math instruction was replaced')
        require(not any(start<=address<end for address in m.external),
                'native math was replaced by a dependency stub')
        spans.append({'start':hex(start),'end':hex(end),
                      'sha256':hashlib.sha256(actual).hexdigest()})
    return {'original_instruction_spans':spans,'no_math_stubs':True}


def unobserved_neighbors(mapped) -> dict:
    fixture=DrawFixture(mapped,861);faults=[]
    # The sprite-table body is now required; test the new exact end instead.
    for address in (0x641279,0x684d82,0x9497e9):
        fault=expect_fault(lambda:fixture.m.call(address),'unmodelled_native_callee')
        require(fault['address']==hex(address),'unexpected branch escaped the admission guard')
        faults.append(fault)
    return {'unobserved_branches_rejected':faults}


def repeated_geometry(mapped) -> dict:
    fixture=DrawFixture(mapped,861);m=fixture.m
    for index in range(64):
        fixture.draw(861+index)
        fixture.draw(-862)
    for layer,buffers in enumerate(fixture.buffers):
        descriptor=fixture.layers+layer*0x6c
        require(m.get32(descriptor+4)==64*4 and m.get32(descriptor+8)==64*6,
                'repeated native math calls corrupted append counters')
        for name,stride in (('positions',48),('colors',64),('uv',32)):
            used=64*stride;handle=buffers[name]
            values=struct.unpack('<'+str(used//4)+'f',m.get(handle.address,used))
            require(all(math.isfinite(value) for value in values),
                    'native helper produced non-finite geometry')
            require(m.get(handle.address+used,handle.length-used)==bytes(handle.length-used),
                    'native helper changed unused neighboring quads')
    return {'draws':128,'finite_geometry':True,'unused_quads_unchanged':True}


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    args=parser.parse_args()
    mapped=read_exact(args.executable)
    checks=[('missing_numeric',lambda:missing_path(mapped,numeric=True)),
            ('missing_coordinate',lambda:missing_path(mapped,numeric=False)),
            ('original_math',lambda:original_math(mapped)),
            ('unobserved_neighbors',lambda:unobserved_neighbors(mapped)),
            ('repeated_geometry',lambda:repeated_geometry(mapped))]
    results=[]
    for name,action in checks:
        try:results.append({'case':name,'status':'passed','evidence':action()})
        except Exception as error:
            results.append({'case':name,'status':'failed',
                            'error':getattr(error,'evidence',str(error))})
    passed=sum(item['status']=='passed' for item in results)
    print(json.dumps({'executable_sha256':mapped.sha256,'passed':passed,
                      'total':len(results),'production_ready':False,'results':results},
                     indent=2,allow_nan=False))
    return 0 if passed==len(results) else 1


if __name__=='__main__':raise SystemExit(main())
