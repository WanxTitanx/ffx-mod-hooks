#!/usr/bin/env python3
"""Dense native stat-reader stress. Not final actor stats or an expanded menu."""
import argparse
import gc
import json
from pathlib import Path
import struct
import time
from pe_image import read_exact, EXPECTED_SHA256
from stat_fixture import StatFixture


def run(executable,output):
    if output.exists():raise FileExistsError(output)
    mapped=read_exact(executable);results=[]
    for count in (860,861,862,1024,1025,2048,4096,16384):
        started=time.monotonic();f=StatFixture(mapped,count)
        data=bytearray();expected=[[0]*10 for _ in range(7)]
        for i in range(count):
            content=5 if i%997==0 else 35
            mask=i%128
            if i%131==0:content,mask=255,0
            data.extend((content,mask))
            for character in range(7):
                if mask&(1<<character):expected[character][0 if content==35 else 2]+=6 if content==35 else 4
        try:
            for order in ('forward','reverse'):
                seeded=bytes(data) if order=='forward' else b''.join(data[i:i+2] for i in range(len(data)-2,-1,-2))
                f.m.put(f.state.address,seeded)
                # Dense active masks need a higher, explicit finite instruction
                # budget than the sparse constructor regression, not a code skip.
                with f.m.borrowed((f.state,f.summary,f.panel)):
                    f.m.call(0xa54860,instructions=30000000)
                actual=[list(struct.unpack('<II8B',f.m.get(f.summary.address+c*16,16))) for c in range(7)]
                if actual!=expected:raise AssertionError({'order':order,'expected':expected,'actual':actual})
                if f.state_bytes()!=seeded:raise AssertionError('reader changed source state')
            if f.downstream_apply_calls!=2:raise AssertionError('downstream boundary count')
            results.append({'nodes':count,'status':'passed','all_128_masks':True,
                            'forward_and_reverse_match':True,'source_preserved':True,'seconds':time.monotonic()-started})
        except Exception as error:
            results.append({'nodes':count,'status':'failed','error':str(error),'seconds':time.monotonic()-started})
        print(count,results[-1]['status'],flush=True)
        del f;gc.collect()
    passed=sum(x['status']=='passed' for x in results)
    report={'executable_sha256':EXPECTED_SHA256,'scope':'original A54860/7AB890 reader with synthetic panel, relocated state and count patch',
            'count_cases_passed':passed,'count_cases_total':len(results),'results':results,
            'final_actor_application_executed':False,'complete_logical_grid_expansion':False,'production_ready':False}
    with output.open('x') as stream:json.dump(report,stream,indent=2);stream.write('\n')
    return 0 if passed==len(results) else 1


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();raise SystemExit(run(a.executable,a.output))
