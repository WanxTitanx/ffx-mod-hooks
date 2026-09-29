#!/usr/bin/env python3
"""Actual runner preflight in Windows PowerShell; no build or process control."""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import subprocess
import tempfile
from unittest.mock import patch
import run_seymour_adapters as runner

class Captured(Exception):
    pass

def actual_script():
    scripts=[]
    def intercept(command,**_kwargs):
        if command[0]!='ssh' or '-EncodedCommand' not in command:
            raise AssertionError('inspection no longer precedes emission or remote writes')
        scripts.append(base64.b64decode(command[-1]).decode('utf-16le'))
        raise Captured()
    with tempfile.TemporaryDirectory(prefix='seymour-preflight-') as temporary:
        with patch.object(runner.subprocess,'run',side_effect=intercept):
            try:runner.run('unused-test-host',Path(temporary)/'capture',('session',))
            except Captured:pass
    if len(scripts)!=1:raise AssertionError('expected one initial inspection')
    return scripts[0]

def run(host,output):
    output.mkdir(parents=True,exist_ok=False)
    script=actual_script()
    (output/'actual-preflight.ps1').write_text(script,encoding='utf-8')
    cases=[('empty','',0),('unrelated','unrelated',0),('compiler','cl',75),
           ('linker','link',75),('msbuild','MSBuild',75),('query-failure',None,1),('native','<native>',(0,75))]
    results=[]
    for name,process,expected in cases:
        # Enumeration is isolated to this PowerShell process. Named queries
        # report missing matches exactly as the real Get-Process cmdlet does.
        endpoint="function Get-Process { [CmdletBinding()] param([string[]]$Name);"
        if process is None:endpoint+="throw 'Enumeration failed'"
        elif process:
            endpoint+=f"if(!$Name -or $Name -contains '{process}'){{[pscustomobject]@{{Id=12;ProcessName='{process}'}}}}else{{Write-Error 'No matching process'}}"
        else:endpoint+="if($Name){Write-Error 'No matching process'}"
        endpoint+='};'
        if process=='<native>':endpoint=''
        encoded=base64.b64encode((endpoint+script).encode('utf-16le')).decode()
        result=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',host,
            'powershell','-NoProfile','-NonInteractive','-EncodedCommand',encoded],capture_output=True,timeout=30)
        (output/(name+'.log')).write_bytes(result.stdout+result.stderr)
        ok=result.returncode in expected if isinstance(expected,tuple) else result.returncode==expected
        results.append({'case':name,'expected':expected,'exit_code':result.returncode,'ok':ok})
        print(name,result.returncode,'expected',expected,'PASS' if ok else 'FAIL',flush=True)
    report={'runner_sha256':hashlib.sha256(Path(runner.__file__).read_bytes()).hexdigest(),
            'script_sha256':hashlib.sha256(script.encode()).hexdigest(),'results':results,
            'scope':'actual runner script and Windows PowerShell; simulated enumeration; no build/game'}
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(not all(result['ok'] for result in results))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',default='windows11-dev-next')
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    raise SystemExit(run(args.host,args.output.resolve()))
