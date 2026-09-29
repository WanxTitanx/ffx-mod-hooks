#!/usr/bin/env python3
"""Run exclusive portable checks, optional read-only corpus and private x86 matrix.

No DLL deployment or game launch is provided. Output must be a NEW directory.
Portable-only success cannot change the report's production_ready=false.
"""
from __future__ import annotations
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import codec
from bundle import read_input
import native_harness

ROOT=Path(__file__).resolve().parent
STANDARD_LAYOUT='1303519cfc2dc4a6eb3975392ea2639f147272d816dcceea877e4157d77ba990'
STANDARD_CONTENTS='542ce37678fe4ec97d7c3ffd2403061de7ade1bc5a4b6b2de2a7cfdf90745206'


def audit_corpus(directory: Path | None) -> dict:
    if directory is None:
        return {'status':'not_run','pairs_passed':0,'production_ready':False,
                'reason':'No game-asset corpus was supplied; synthetic tests are not a golden corpus'}
    results=[]
    for name,layout,contents in [('original','dat01.dat','dat09.dat'),
                                ('standard','dat02.dat','dat10.dat'),('expert','dat03.dat','dat11.dat')]:
        item={'grid':name,'layout':layout,'contents':contents,'status':'failed'}
        try:
            a,b=read_input(directory/layout),read_input(directory/contents)
            data=codec.parse(a,b)
            if codec.encode(data)!=(a,b):raise codec.FormatError('no-edit round-trip changed source bytes')
            ah,bh=hashlib.sha256(a).hexdigest(),hashlib.sha256(b).hexdigest()
            item.update({'status':'passed','byte_identical_roundtrip':True,
                         'nodes':len(data.nodes),'links':len(data.links),'clusters':len(data.clusters),
                         'layout_sha256':ah,'contents_sha256':bh,
                         'matches_recorded_vanilla_standard':name=='standard' and ah==STANDARD_LAYOUT and bh==STANDARD_CONTENTS})
        except (OSError,ValueError) as error:item['error']=str(error)
        results.append(item)
    passed=sum(item['status']=='passed' for item in results)
    return {'status':'passed' if passed==3 else 'failed','pairs_passed':passed,'pairs_total':3,
            'production_ready':False,'scope':'read-only packed-pair round-trip, not native runtime acceptance',
            'pairs':results}


def make_output(path: Path) -> None:
    path.mkdir(parents=False,exist_ok=False)


def _write_json(path: Path,value) -> None:
    with path.open('x',encoding='utf-8') as stream:
        json.dump(value,stream,indent=2,sort_keys=True,allow_nan=False);stream.write('\n')


def _run(command: list[str],output: Path,*,timeout: int,env: dict | None=None) -> dict:
    environment=os.environ.copy();environment['PYTHONDONTWRITEBYTECODE']='1'
    if env is not None:environment.update(env)
    try:
        completed=subprocess.run(command,capture_output=True,text=True,timeout=timeout,env=environment)
        with output.open('x',encoding='utf-8') as stream:
            stream.write(completed.stdout);stream.write(completed.stderr)
        return {'exit_code':completed.returncode,'log':output.name,
                'stdout':completed.stdout,'stderr':completed.stderr}
    except (OSError,subprocess.TimeoutExpired) as error:
        with output.open('x',encoding='utf-8') as stream:stream.write(str(error)+'\n')
        return {'exit_code':2,'log':output.name,'stdout':'','stderr':str(error)}


def main(argv: list[str] | None=None) -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True,help='New directory under an existing parent')
    parser.add_argument('--executable',type=Path,help='Exact read-only FFX.exe for private native cases')
    parser.add_argument('--corpus',type=Path,help='Read-only ABMAP directory containing all six dat files')
    parser.add_argument('--native-timeout',type=int,default=180)
    args=parser.parse_args(argv)
    if not 1<=args.native_timeout<=600:parser.error('native timeout must be in [1,600]')
    try:
        make_output(args.output)
        report={'schema':1,'recorded_at_utc':datetime.now(timezone.utc).isoformat(),
                'environment':{'python':sys.version,'platform':platform.platform()},
                'production_ready':False,'full_menu_lifecycle_verified':False,
                'sources':{str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
                           for path in sorted(ROOT.rglob('*.py')) if '__pycache__' not in path.parts}}
        unit=_run([sys.executable,'-m','unittest','discover','-s',str(ROOT/'tests'),'-v'],
                  args.output/'portable.log',timeout=90)
        found=re.search(r'Ran (\d+) tests?',unit['stderr'])
        report['portable']={'status':'passed' if unit['exit_code']==0 and found else 'failed',
                            'tests_run':int(found.group(1)) if found else None,
                            'exit_code':unit['exit_code'],'log':unit['log']}
        mutation=_run([sys.executable,str(ROOT/'mutation_checks.py')],args.output/'mutations.log',timeout=90)
        try:mutation_result=json.loads(mutation['stdout'])
        except ValueError:mutation_result={'detected':0,'mutations':10,'error':mutation['stderr'][-2000:]}
        _write_json(args.output/'mutations.json',mutation_result)
        report['mutations']={'status':'passed' if mutation['exit_code']==0 and mutation_result.get('detected')==10 else 'failed',
                             'detected':mutation_result.get('detected',0),'total':10,'log':mutation['log']}
        corpus=audit_corpus(args.corpus);_write_json(args.output/'corpus.json',corpus)
        report['corpus']=corpus
        native={'status':'not_run','cases_prepared':len(native_harness.case_names(native_harness.CAPACITIES)),
                'reason':'Exact private executable not supplied','production_ready':False}
        if args.executable is not None and report['portable']['status']=='passed' and report['mutations']['status']=='passed':
            native_path=args.output/'native.json'
            command=[sys.executable,str(ROOT/'native_harness.py'),str(args.executable),
                     '--timeout',str(args.native_timeout),'--report',str(native_path)]
            execution=_run(command,args.output/'native.log',timeout=args.native_timeout*46+90)
            if native_path.is_file():
                native=json.loads(native_path.read_text())
                native['status']='passed_unit_scope_only' if execution['exit_code']==0 else 'failed'
            else:
                native={'status':'not_validated','production_ready':False,'exit_code':execution['exit_code'],
                        'reason':execution['stderr'][-3000:],'log':execution['log']}
        report['native']=native
        original={'status':'not_requested','tests_run':0,
                  'scope':'original native layout/activation/retirement and visual tests'}
        if args.executable is not None:
            original['status']='blocked'
            if args.corpus is None:
                original['reason']='Full native acceptance requires --corpus; activation must not be silently omitted'
            elif report['portable']['status']!='passed':
                original['reason']='Portable preflight failed'
            else:
                command=[sys.executable,'-m','unittest','discover','-s',str(ROOT),
                         '-p','test_*.py','-v']
                execution=_run(command,args.output/'original-native.log',timeout=600,
                    env={'FFX_SPHERE_EXE':str(args.executable.resolve()),
                         'FFX_SPHERE_ASSETS':str(args.corpus.resolve())})
                found=re.search(r'Ran (\d+) tests?',execution['stderr'])
                count=int(found.group(1)) if found else 0
                # At least the 27 original methods must run. Never hide missing
                # dependencies, an empty discovery, or a skipped mandatory test.
                accepted=(execution['exit_code']==0 and count>=27 and
                          'skipped=' not in execution['stderr'])
                original.update(status='passed' if accepted else 'failed',
                    tests_run=count,exit_code=execution['exit_code'],log=execution['log'])
        report['original_native']=original
        report['unverified_boundaries']=list(native_harness.UNVERIFIED)
        passed=report['portable']['status']=='passed' and report['mutations']['status']=='passed'
        if args.corpus is not None:passed=passed and corpus['status']=='passed'
        if args.executable is not None:
            passed=passed and native['status']=='passed_unit_scope_only' and original['status']=='passed'
        report['requested_checks_passed']=passed
        _write_json(args.output/'verification.json',report)
        print(json.dumps({'portable':report['portable'],'mutations':report['mutations'],
                          'corpus_status':corpus['status'],'native_status':native['status'],
                          'original_native':original,
                          'production_ready':False,'report':str(args.output/'verification.json')},indent=2))
        return 0 if passed else 1
    except (OSError,ValueError) as error:
        print(f'Verification refused: {error}',file=sys.stderr);return 2


if __name__=='__main__':raise SystemExit(main())
