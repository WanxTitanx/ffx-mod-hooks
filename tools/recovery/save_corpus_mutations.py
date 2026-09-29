#!/usr/bin/env python3
"""Negative controls for real-save tests, confined to temporary source copies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
TEST=Path('src/runtime/FfxHooksDll/tests/SphereGridSaveCorpusRt0.cpp')
SAVE=Path('src/runtime/FfxHooksDll/hooks/RonsoPoolSave.cpp')
CORE=Path('src/runtime/FfxHooksDll/hooks/NativeSaveCommitCore.h')
MUTATIONS=(('omit_exact_image',CORE,'actual==expected.image&&','true&&'),
           ('omit_file_identity',CORE,'expected.file==readbackFile&&','(static_cast<void>(readbackFile),true)&&'),
           ('omit_captured_path',CORE,'attempt.path==expected.path&&','true&&'),
           ('wrong_crc_table',SAVE,'i<255;','i<256;'))
def run(corpus,output):
    output.mkdir(parents=False,exist_ok=False);sources={}
    def collect(relative):
        p=(ROOT/relative).resolve();relative=p.relative_to(ROOT)
        if relative in sources:return
        data=p.read_bytes();sources[relative]=data
        for name in re.findall(r'^\s*#include\s*"([^"]+)"',data.decode(),re.M):collect((p.parent/name).relative_to(ROOT))
    collect(TEST);collect(SAVE);results=[]
    for name,file,before,after in MUTATIONS:
        work=output/name;work.mkdir()
        for path,data in sources.items():
            target=work/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        source=(work/file).read_text()
        if source.count(before)!=1:raise RuntimeError('mutation site is not unique: '+name)
        (work/file).write_text(source.replace(before,after));exe=work/'test'
        build=subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread',str(work/TEST),str(work/SAVE),'-o',str(exe)],capture_output=True,timeout=60)
        (work/'build.log').write_bytes(build.stdout+build.stderr)
        item={'name':name,'compiled':build.returncode==0,'detected':False}
        if build.returncode==0:
            result=subprocess.run([str(exe),str(corpus)],capture_output=True,timeout=180)
            (work/'result.json').write_bytes(result.stdout);(work/'stderr.log').write_bytes(result.stderr)
            try:report=json.loads(result.stdout)
            except ValueError:report={}
            preserved=report.get('groups',{}).get('source_preservation',{})
            item.update(exit_code=result.returncode,failures=report.get('failures'),detected=(result.returncode==1 and report.get('failures',0)>0 and preserved.get('passed',0)>0 and preserved.get('passed')==preserved.get('total')))
        results.append(item);print(json.dumps(item),flush=True)
    unchanged=all((ROOT/p).read_bytes()==data for p,data in sources.items())
    report={'source_hashes':{str(p):hashlib.sha256(data).hexdigest() for p,data in sources.items()},'detected':sum(r['detected'] for r in results),'total':len(results),'production_sources_unchanged':unchanged,'results':results}
    with (output/'summary.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    return 0 if unchanged and report['detected']==report['total'] else 1
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--corpus',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();raise SystemExit(run(a.corpus.resolve(),a.output.resolve()))
