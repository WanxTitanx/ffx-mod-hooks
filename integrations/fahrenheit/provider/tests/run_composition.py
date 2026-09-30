"""Compile the actual provider-overlay load/UI methods with bounded game endpoints."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, secrets, subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
def body(text,signature):
    start=text.index(signature);at=text.index('{',start);depth=1;end=at+1
    while depth and end<len(text):
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    if depth:raise ValueError('Unbalanced provider method: '+signature)
    return text[start:end]

def run(reference:Path,output:Path):
    module_spec=importlib.util.spec_from_file_location('overlay',HERE.parent/'build_overlay.py')
    module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
    pinned=json.loads((HERE.parents[1]/'upstream-reference.json').read_text())
    name='src/runtime/save_impl.cs';raw=(reference/name).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=pinned['sha256'][name]:raise ValueError('Unexpected source identity')
    text=raw.decode('utf-8')
    for old,new in module.PATCHES[name]:text=module.replace_once(text,old,new)
    signatures=['internal void signal_exit_abort()', 'internal void signal_exit_success()', 'void IFhSaveSystemImpl.load(int slot)']
    if 'private void signal_exit_abort(bool' in text:signatures.insert(1,'private void signal_exit_abort(bool')
    methods='\n'.join(body(text,s) for s in signatures)
    output.mkdir(parents=True,exist_ok=False)
    (output/'Methods.cs').write_text('using System;\nusing System.IO;\nnamespace Fahrenheit { internal unsafe partial class ProviderLoadHost {\n'+methods+'\n} }\n')
    sources=[HERE/'ProviderIoTests.cs',HERE/'ProviderCompositionTests.cs',HERE.parent/'FhCooperativeServices.cs']
    import xml.sax.saxutils
    items=''.join('<Compile Include="'+xml.sax.saxutils.escape(str(p),{'"':'&quot;'})+'" />' for p in sources)
    project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><DefineConstants>HAS_PROVIDER</DefineConstants><StartupObject>Fahrenheit.ProviderCompositionTests</StartupObject></PropertyGroup><ItemGroup>'+items+'</ItemGroup></Project>'
    (output/'Composition.csproj').write_text(project)
    completed=subprocess.run(['dotnet','run','--project',str(output/'Composition.csproj'),'-c','Release'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'result.log').write_bytes(completed.stdout)
    print(completed.stdout.decode('utf-8','replace'));return completed.returncode

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,default=ROOT/'work/fahrenheit/upstream')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    target=args.output or ROOT/'work/fahrenheit-services'/('composition-'+secrets.token_hex(6))
    raise SystemExit(run(args.reference.resolve(),target.resolve()))
