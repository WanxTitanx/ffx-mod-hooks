#!/usr/bin/env python3
"""Actual session teardown; simulated platform, no game or native save I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
CASES=('remove','stop-cleanup','other-thread','captured-callback','repeat')
PREFIX=r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "SeymourSessionRuntime.h"
#include "NativeSaveEvents.h"
#include "Config.h"
using DWORD=std::uint32_t;
namespace Test {
unsigned checks=0,failed=0,thread=7,primaryResets=0;
DWORD lastError=73;constexpr std::uintptr_t Base=0x400000;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",why);}}
DWORD ThreadId(){return thread;}
}
static DWORD GetLastError(){return Test::lastError;}
static void SetLastError(DWORD error){Test::lastError=error;}
namespace FfxHooks {
namespace Config {IntReadResult ReadIntExact(const char*,int,int){return {IntReadState::Valid,1};}}
namespace RonsoPool {bool IsSaveIoReady() noexcept{return true;}}
bool StartNativeSaveLoadEvents(std::uintptr_t base){return base==Test::Base;}
bool NativeSaveLoadEventsReady() noexcept{return true;}
namespace RecoveryNative {
bool Profile(std::uintptr_t base){return base==Test::Base;}
bool Pin(const void*){return true;}
bool Range(std::uintptr_t,std::size_t){return true;}
bool Copy(void* d,const void* s,std::size_t n){std::memcpy(d,s,n);return true;}
}
}
'''
SUFFIX=r'''
namespace Test {
namespace S=FfxHooks::SeymourSession;namespace E=FfxHooks::NativeSaveEvents;
void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept{}
void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept{}
void ResetPrimary() noexcept{++primaryResets;}
const E::Observer primary{Read,Write,ResetPrimary};
}
int main(int argc,char** argv){
 using namespace Test;if(argc!=2)return 2;const std::string scenario=argv[1];
 Check(E::Subscribe(&primary),"existing primary observer registered");
 S::PrimeSaveIo(Base,false);Check(S::PublisherReady(),"session publisher installed");
 E::ResetCompleted();const auto token=S::Capture();
 Check(token.Valid()&&S::Current(token),"real core admitted reset identity");
 if(scenario=="stop-cleanup"){
  S::RequestStop();Check(!S::Capture().Valid(),"stop rejects new writes");
  Check(S::Current(token,true),"owned cleanup remains available before removal");
 }
 if(scenario=="other-thread")thread=9;
 const auto captured=E::CaptureObservers();
 Check(S::Remove(),"normal removal completed");thread=7;
 Check(!S::PublisherReady(),"retired publisher unavailable");
 Check(!S::Capture(true).Valid(),"unsubscribed session cannot publish a cleanup token");
 Check(!S::Current(token,true),"previous cleanup token retired before unobserved loads");
 if(scenario=="captured-callback")for(const auto* value:captured)if(value&&value->reset)value->reset();
 E::ResetCompleted();Check(primaryResets>=2,"primary observer remains active");
 Check(!S::Capture(true).Valid(),"later reset cannot expose a stale cleanup identity");
 if(scenario=="repeat")Check(S::Remove()&&S::Remove(),"removal is idempotent");
 S::PrimeSaveIo(Base,false);Check(!S::PublisherReady()&&!S::Capture(true).Valid(),"retirement cannot restart");
 E::Unsubscribe(&primary);
 std::printf("SeymourSessionRetirementRt1 %s: %u/%u passed (simulated platform)\n",scenario.c_str(),checks-failed,checks);
 return failed?1:0;
}
'''

def run(output):
    output.mkdir(parents=True,exist_ok=False)
    adapter=MOD/'hooks/SeymourSessionRuntime.cpp';text=adapter.read_text()
    for name in ('GridTeachHook.h','RonsoPoolRuntime.h','RecoveryNative.h','../shared/Config.h'):
        marker='#include "'+name+'"'
        if text.count(marker)!=1:raise ValueError('endpoint changed: '+name)
        text=text.replace(marker,'// Explicit simulated platform endpoint above.')
    text=text.replace('GetCurrentThreadId()','Test::ThreadId()')
    source=output/'retirement.cpp';source.write_text(PREFIX+text+SUFFIX)
    files=[adapter,MOD/'hooks/SeymourSessionRuntime.h',MOD/'hooks/SeymourSessionCore.h',
           MOD/'hooks/SeymourActiveLoadCore.h',MOD/'hooks/NativeSaveEvents.h',MOD/'hooks/RonsoPoolSave.cpp',
           MOD/'hooks/RonsoPoolSave.h',MOD/'hooks/RonsoPoolCore.h',MOD/'shared/Config.h',Path(__file__)]
    (output/'manifest.json').write_text(json.dumps({
        'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        'generated_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'scope':'actual session state and registry; simulated platform; no native I/O',
        'live_game_proven':False},indent=2)+'\n')
    binary=output/'retirement'
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-I'+str(MOD/'hooks'),
         '-I'+str(MOD/'shared'),str(source),str(MOD/'hooks/RonsoPoolSave.cpp'),'-o',str(binary)]
    build=subprocess.run(cmd,capture_output=True,timeout=60)
    (output/'build.log').write_bytes(build.stdout+build.stderr)
    if build.returncode:print(build.stderr.decode(errors='replace'));return build.returncode
    results=[]
    for case in CASES:
        result=subprocess.run([str(binary),case],capture_output=True,timeout=15)
        (output/(case+'.log')).write_bytes(result.stdout+result.stderr)
        print(result.stdout.decode(errors='replace').strip(),flush=True)
        results.append({'case':case,'exit_code':result.returncode})
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    return int(any(r['exit_code'] for r in results))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    raise SystemExit(run(p.parse_args().output.resolve()))
