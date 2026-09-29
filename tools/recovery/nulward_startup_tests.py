#!/usr/bin/env python3
"""Execute actual worker order and NulWard startup code with explicit fake owners.
This proves startup admission/order, not native MinHook or in-battle ABI.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

MOCK=r'''
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
using uintptr_t=std::uintptr_t;
uintptr_t g_base=0x400000;
namespace Fake {bool ward=true,apply=true,log=true,validate=false,workshop=true,nova=true,profile=true;
 bool damageOwned=false,installed=false,sixNuls=false,allElements=false;unsigned calls=0,novaCalls=0,workshopCalls=0;uintptr_t seenBase=0;
 void Reset(){ward=apply=log=workshop=nova=profile=true;validate=damageOwned=installed=sixNuls=allElements=false;calls=novaCalls=workshopCalls=0;seenBase=0;}}
bool NovaSuperDamageFlagEnabled(){return Fake::nova;}
bool NulWardFlagEnabled(){return Fake::ward;}
bool NulWardApplyEnabled(){return Fake::apply;}
bool NulWardLogFlagEnabled(){return Fake::log;}
bool NulWardNativeSlotsEnabled(){return false;}
bool NulWardP16Enabled(){return false;}
bool NulWardP16ApplyEnabled(){return false;}
bool EnvFlagEnabled(const char*){return Fake::validate;}
bool F8CatalogGateEnabled(const char*){return Fake::workshop;}
void* GetModuleHandleW(const wchar_t*){return reinterpret_cast<void*>(0x400000);}
void Log(const char*,...){}
void LogLine(const char*){}
void StartupTiming(const char*){}
void StartNovaPoolEarlyIfRequested(){++Fake::novaCalls;}
namespace FfxHooks {
namespace ModFeatures {enum class Feature {NulSpells};bool Enabled(Feature){return Fake::sixNuls;}}
enum class F8RuntimeAvailability {Available,ProducerUnavailable};
struct NulWardInstallOptions{bool nativeSlots=false,experimentP16=false,p16Apply=false,allElements=false;};
struct NulWardInstallResult{bool ok=false;uintptr_t stubWriteback=0,detourAftermath=0,detourHitLoop=0,detourPrecheck=0;};
NulWardInstallResult InstallNulWardHook(uintptr_t base,bool apply,bool log,void(*)(const char*),const NulWardInstallOptions* options){
 Fake::allElements=options&&options->allElements;
 ++Fake::calls;Fake::seenBase=base;Fake::installed=Fake::profile&&!Fake::damageOwned&&base==0x400000&&(apply||log);
 return {Fake::installed};
}
namespace EquipmentWorkshop {
void Start(uintptr_t,bool enabled,bool validate,void(*)(const char*)){++Fake::workshopCalls;Fake::damageOwned=enabled&&!validate;}
}
}
void PublishResolvedF8Status(const char*,FfxHooks::F8RuntimeAvailability,bool){}
'''
TEST=r'''
unsigned checks=0,failed=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",why);}}
int main(){
 for(unsigned workshop=0;workshop<2;++workshop)for(unsigned nova=0;nova<2;++nova){
  Fake::Reset();Fake::workshop=workshop;Fake::nova=nova;Startup();
  Check(Fake::installed&&Fake::calls==1,"NulWard validates before the shared damage entry is owned");
  Check(Fake::workshopCalls==1&&Fake::novaCalls==1,"original Nova and Workshop startup calls are preserved once");
  Check(Fake::damageOwned==bool(workshop),"Workshop still owns its requested entry");
  Check(Fake::seenBase==0x400000,"early adapter receives the real module base");
 }
 Fake::Reset();Fake::ward=Fake::apply=Fake::log=false;Startup();
 Check(Fake::calls==0&&Fake::damageOwned,"NulWard OFF does not affect Workshop startup");
 Fake::Reset();Fake::ward=Fake::apply=Fake::log=false;Fake::sixNuls=true;Startup();
 Check(Fake::calls==1&&Fake::installed&&Fake::allElements,"explicit six-Nul gate owns apply and expanded charges without legacy flags");
 Fake::Reset();Fake::validate=true;Fake::sixNuls=true;Startup();
 Check(Fake::calls==0&&!Fake::damageOwned,"validation-only installs neither behavior");
 Fake::Reset();Fake::profile=false;Startup();
 Check(Fake::calls==1&&!Fake::installed&&Fake::damageOwned,"failed ward profile does not suppress Workshop");
 Fake::Reset();Fake::apply=false;Startup();
 Check(Fake::calls==1&&Fake::installed,"observation-only request retains installation path");
 std::printf("NulWardStartupRt1: %u/%u passed (actual startup source, simulated owners)\n",checks-failed,checks);
 return failed?1:0;
}
'''
def main():
 root=Path(__file__).resolve().parents[2]
 source=(root/'src/runtime/FfxHooksDll/dllmain.cpp').read_text()
 worker=source.index('static DWORD WINAPI HooksWorkerThread(')
 a=source.index('    StartNovaPoolEarlyIfRequested();',worker)
 b=source.index('    FfxHooks::EquipmentWorkshop::NativeUi::Start(',a)
 order=source[a:b]
 if 'static void StartNulWardEarlyIfRequested() {' in source:
  a=source.index('static void StartNulWardEarlyIfRequested() {')
  b=source.index('\nstatic void StartFastloadEarlyIfRequested()',a)
  implementation=source[a:b];late=''
 else:
  a=source.index('    const bool enableNulWard = ');b=source.index('    if (GridTeachEnabled()) {',a)
  implementation='';late='\nconst bool enableNovaBypass=NovaSuperDamageFlagEnabled();\nconst bool validateOnly=EnvFlagEnabled("FFXHOOKS_VALIDATE_ONLY");\n'+source[a:b]
 compiler=shutil.which(os.environ.get('CXX','g++'))
 if not compiler:raise SystemExit('C++17 compiler required')
 with tempfile.TemporaryDirectory(prefix='ffx-ward-startup-') as temp:
  cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test';cpp.write_text(MOCK+implementation+'\nvoid Startup(){\n'+order+late+'\n}\n'+TEST)
  subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True,timeout=60)
  subprocess.run([str(exe)],check=True,timeout=15)
if __name__=='__main__':main()
