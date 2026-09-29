#!/usr/bin/env python3
"""Compile the actual FieldProbe adapter against fake PLH and OS dependencies.
Each scenario gets a fresh process. No game image or installed data is accessed.
These tests establish lifecycle/control flow, not target signatures or Windows ABI.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

MOCK=r'''
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#define __cdecl
#define __try try
#define __except(x) catch(...)
#define EXCEPTION_EXECUTE_HANDLER 1
using LONG=long;using DWORD=unsigned long;using BOOL=int;using HMODULE=void*;using LPCSTR=const char*;
constexpr DWORD GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS=4,GET_MODULE_HANDLE_EX_FLAG_PIN=1;
constexpr BOOL FALSE=0;
struct SRWLOCK{bool held=false;};
#define SRWLOCK_INIT {}
inline BOOL TryAcquireSRWLockExclusive(SRWLOCK* p){if(p->held)return 0;p->held=true;return 1;}
inline void ReleaseSRWLockExclusive(SRWLOCK* p){p->held=false;}
namespace Fake {
inline unsigned creates=0,hooks=0,unhooks=0,deletes=0,failHook=0,failUnhook=0,logs=0;
inline unsigned originalCalls=0;
inline bool validate=false,pinAllowed=true,profileAllowed=true,stopAtAdmissionLog=false;
inline unsigned profileCalls=0,signatureCalls=0,failSignature=0;
inline DWORD error=0;
inline std::vector<bool> active;
inline std::vector<std::uint64_t> replacements;
inline int Encounter(int selector,int group,float walk){++originalCalls;error=53;return selector+group+static_cast<int>(walk);}
inline int Decode(int value){++originalCalls;error=53;return value+13;}
inline void Zones(){++originalCalls;error=53;}
inline int Texture(std::int64_t a,int b,int c,std::uint32_t*,char*,int d){++originalCalls;error=53;return static_cast<int>(a)+b+c+d;}
inline void Log(const char* text){++logs;error=91;
 if(stopAtAdmissionLog&&std::strstr(text,"FieldProbe admitted"))FfxHooks::RequestFieldProbeStop();}
inline bool Inert(){for(bool value:active)if(value)return false;return true;}
}
inline LONG InterlockedIncrement(volatile LONG* p){return ++*p;}
inline DWORD GetLastError(){return Fake::error;}
inline void SetLastError(DWORD value){Fake::error=value;}
inline DWORD GetCurrentThreadId(){return 9;}
inline BOOL GetModuleHandleExA(DWORD,LPCSTR,HMODULE* out){*out=reinterpret_cast<void*>(1);return Fake::pinAllowed;}
inline DWORD GetEnvironmentVariableA(const char*,char* out,DWORD capacity){
 if(!Fake::validate)return 0;
 if(capacity>1){out[0]='1';out[1]=0;}
 return 1;
}
namespace PLH {
class x86Detour {
 unsigned index;std::uint64_t target,replacement,*out;
public:
 x86Detour(std::uint64_t address,std::uint64_t fn,std::uint64_t* original):
  index(Fake::creates++),target(address),replacement(fn),out(original){Fake::active.push_back(false);Fake::replacements.push_back(fn);}
 bool hook(){
  ++Fake::hooks;if(Fake::hooks==Fake::failHook)return false;
  const auto rva=static_cast<std::uint32_t>(target-0x400000);
  if(rva==0x380de0)*out=reinterpret_cast<std::uint64_t>(&Fake::Encounter);
  else if(rva==0x43e980)*out=reinterpret_cast<std::uint64_t>(&Fake::Decode);
  else if(rva==0x475ac0)*out=reinterpret_cast<std::uint64_t>(&Fake::Zones);
  else *out=reinterpret_cast<std::uint64_t>(&Fake::Texture);
  Fake::active[index]=true;return true;
 }
 // Match PolyHook: successful unHook frees forwarding storage and clears it.
 bool unHook(){++Fake::unhooks;if(Fake::unhooks==Fake::failUnhook)return false;Fake::active[index]=false;*out=0;return true;}
 ~x86Detour(){++Fake::deletes;}
};
}
'''
TEST=r'''
#include "src/runtime/FfxHooksDll/hooks/FieldProbeHook.cpp"
#include <cstdio>
#include <cstdlib>
using namespace FfxHooks;
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",why);}}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 const std::string scenario=argv[1];
 if(scenario=="off"){
  Check(!InstallFieldProbeHook(0x400000,false,false,Fake::Log).ok,"OFF cannot report installed");
  Check(Fake::creates==0&&Fake::hooks==0,"OFF has no hook side effects");
 }else if(scenario=="profile"||scenario.rfind("signature",0)==0){
  if(scenario=="profile")Fake::profileAllowed=false;
  else Fake::failSignature=static_cast<unsigned>(std::atoi(argv[1]+9));
  const auto result=InstallFieldProbeHook(0x400000,true,true,Fake::Log);
  Check(!result.ok&&!IsFieldProbeHookInstalled(),"profile or requested signature failure closes admission");
  Check(Fake::creates==0&&Fake::hooks==0,"every requested target validated before any detour creation");
  Check(Fake::profileCalls==1,"independent loaded-image profile checked");
 }else if(scenario=="pin"){
  Fake::pinAllowed=false;Check(!InstallFieldProbeHook(0x400000,true,true,Fake::Log).ok,"unpinned callback module cannot publish");
  Check(Fake::hooks==0,"failed pin performs no hook publication");
 }else if(scenario=="validate"){
  Fake::validate=true;Check(!InstallFieldProbeHook(0x400000,true,true,Fake::Log).ok,"validate-only blocks inside the adapter");
  Check(Fake::creates==0&&Fake::hooks==0,"validation must not allocate/publish detours");
 }else if(scenario.rfind("partial",0)==0){
  Fake::failHook=static_cast<unsigned>(std::atoi(argv[1]+7));
  const auto result=InstallFieldProbeHook(0x400000,true,true,Fake::Log);
  Check(!result.ok&&!IsFieldProbeHookInstalled(),"missing required target never publishes success");
  Check(!IsFieldProbeHookInstalled()&&Fake::unhooks==0&&Fake::deletes==0,
        "failed installation closes diagnostics without freeing reachable gateways");
  const auto originalCalls=Fake::originalCalls,logCalls=Fake::logs;
  if(Fake::failHook>1){
   const auto callback=reinterpret_cast<int(*)(int,int,float)>(Fake::replacements[0]);
   Check(callback(2,3,0.0f)==5&&Fake::originalCalls==originalCalls+1&&Fake::error==53,
         "retained encounter entry forwards after partial installation");
  }
  Check(Fake::logs==logCalls,"retained callbacks do not resume diagnostics");
  Check(!RemoveFieldProbeHook(Fake::Log),"partial publication never claims physical retirement");
  Check(Fake::unhooks==0&&Fake::deletes==0,"partial cleanup preserves process-lifetime code");
  const auto before=Fake::hooks;
  Check(!InstallFieldProbeHook(0x400000,true,true,Fake::Log).ok&&Fake::hooks==before,"failed lane cannot silently retry or overwrite context");
 }else if(scenario.rfind("retire",0)==0){
  const auto result=InstallFieldProbeHook(0x400000,true,true,Fake::Log);Check(result.ok&&result.hookedCount==4,"four requested hooks installed");
  Fake::failUnhook=static_cast<unsigned>(std::atoi(argv[1]+6));
  Check(!RemoveFieldProbeHook(Fake::Log),"failed unhook must be visible");
  Check(Fake::deletes==0,"published detour storage must survive a failed unhook");
  Check(!IsFieldProbeHookInstalled(),"removal closes diagnostic admission even if code is retained");
 }else if(scenario=="repeat"){
  Check(InstallFieldProbeHook(0x400000,true,true,Fake::Log).ok,"initial install succeeds");
  const auto before=Fake::hooks;
  Check(InstallFieldProbeHook(0x400000,true,true,Fake::Log).ok,"same request is idempotent");
  Check(Fake::hooks==before&&Fake::unhooks==0&&Fake::deletes==0,"same request never recreates published hooks");
  Check(!InstallFieldProbeHook(0x400000,false,true,Fake::Log).ok,"different request requires explicit process restart");
  Check(IsFieldProbeHookInstalled()&&Fake::hooks==before,"reconfiguration rejection preserves original live request");
 }else if(scenario=="forward"){
  Check(InstallFieldProbeHook(0x400000,false,true,Fake::Log).ok,"texture-only request installs");
  const auto callback=reinterpret_cast<int(*)(std::int64_t,int,int,std::uint32_t*,char*,int)>(Fake::replacements.back());
  char source[]="/map/demo/tex/example.dds";std::uint32_t marker=9;
  Check(callback(3,5,7,&marker,source,11)==26&&Fake::originalCalls==1,"texture arguments and original result preserved once");
  Check(Fake::error==53,"original last-error remains visible");
  Check(!RemoveFieldProbeHook(Fake::Log),"stop reports restart-required, not physical removal");
  Check(Fake::deletes==0,"normal stop also retains possible prologue entrants");
  const auto logs=Fake::logs;
  Check(callback(3,5,7,&marker,source,11)==26&&Fake::originalCalls==2&&Fake::error==53,
        "late texture entry forwards exactly once with original arguments and error");
  Check(Fake::logs==logs&&!IsFieldProbeHookInstalled(),"late entry cannot reactivate diagnostics");
  Check(!RemoveFieldProbeHook(Fake::Log)&&Fake::unhooks==0&&Fake::deletes==0,
        "repeated stop retains the same gateways without attempting freeing unHook");
 }else if(scenario=="stop-logger"){
  Fake::stopAtAdmissionLog=true;
  const auto result=InstallFieldProbeHook(0x400000,true,true,Fake::Log);
  Check(!result.ok&&result.hookedCount==0&&!IsFieldProbeHookInstalled(),
        "stop during admission logging cannot return an installed result");
  Check(Fake::hooks==4&&Fake::unhooks==0&&Fake::deletes==0,
        "publication stop keeps possibly reachable forwarding code");
 }else if(scenario=="full"){
  const auto result=InstallFieldProbeHook(0x400000,true,true,Fake::Log);
  Check(result.ok&&result.hookedCount==4&&IsFieldProbeHookInstalled(),"all requested hooks available");
 }else return 2;
 std::printf("FieldProbeLifecycle %s: %u/%u passed (real adapter, simulated PLH/OS)\n",argv[1],checks-failures,checks);
 return failures?1:0;
}
'''
def main():
    root=Path(__file__).resolve().parents[2]
    compiler=shutil.which(os.environ.get('CXX','g++'))
    if not compiler:raise SystemExit('C++17 compiler required')
    scenarios=['off','pin','validate','full','repeat','forward','profile','stop-logger']+[f'signature{i}' for i in range(1,5)]+[f'partial{i}' for i in range(1,5)]+[f'retire{i}' for i in range(1,5)]
    with tempfile.TemporaryDirectory(prefix='ffx-field-probe-lifecycle-') as temp:
        work=Path(temp);hooks=work/'src/runtime/FfxHooksDll/hooks';hooks.mkdir(parents=True)
        for name in ['FieldProbeHook.h','FieldProbeHook.cpp']:shutil.copyfile(root/'src/runtime/FfxHooksDll/hooks'/name,hooks/name)
        for name in ['FieldProbeEvidence.h','RecoveryEvidence.generated.h']:
            shutil.copyfile(root/'src/runtime/FfxHooksDll/hooks'/name,hooks/name)
        (hooks/'RecoveryNative.h').write_text('''#pragma once
#include <windows.h>
#include "RecoveryEvidence.generated.h"
namespace FfxHooks::RecoveryNative {
inline bool Profile(std::uintptr_t base) noexcept {++Fake::profileCalls;return base==0x400000&&Fake::profileAllowed;}
inline bool Match(std::uintptr_t,const RecoveryEvidence::Proof&) noexcept {return ++Fake::signatureCalls!=Fake::failSignature;}
}
''')
        shared=hooks.parent/'shared';shared.mkdir();shutil.copyfile(root/'src/runtime/FfxHooksDll/shared/ffx_addresses.h',shared/'ffx_addresses.h')
        (work/'windows.h').write_text(MOCK)
        plh=work/'polyhook2/Detour';plh.mkdir(parents=True);(plh/'x86Detour.hpp').write_text('#pragma once\n#include <windows.h>\n')
        cpp=work/'test.cpp';exe=work/'test';cpp.write_text(TEST)
        subprocess.run([compiler,'-std=c++17','-O1','-Wall','-Wextra','-Werror','-DFFXHOOKS_HAVE_POLYHOOK','-I',str(work),str(cpp),'-o',str(exe)],check=True,timeout=60)
        failed=[]
        for name in scenarios:
            run=subprocess.run([str(exe),name],capture_output=True,text=True,timeout=10)
            print(run.stdout,end='');print(run.stderr,end='')
            if run.returncode:failed.append({'scenario':name,'exit_code':run.returncode})
        print('FAILED_SCENARIOS',failed)
        if failed:raise SystemExit(1)
if __name__=='__main__':main()
