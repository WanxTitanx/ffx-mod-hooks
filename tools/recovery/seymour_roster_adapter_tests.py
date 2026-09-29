#!/usr/bin/env python3
"""Actual permanent-roster adapter, simulated OS/native/session endpoints."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
from seymour_sort_adapter_harness import portable_finally

ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
CASES=('off invalid validate profile signature pin provider stop-before apply borrowed '
       'no-session wrong-thread battle reset-read stop-cleanup native-exception partial menu '
       'new-save-exception new-save-reentrant-provider provider-last-error').split()
PREFIX=r'''
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include "SeymourPersistentRosterHook.h"
#include "SeymourPersistentRosterCore.h"
#include "SeymourSessionRuntime.h"
#define __cdecl
using DWORD=std::uint32_t;
namespace Test {
namespace R=FfxHooks::SeymourPersistentRoster;
unsigned checks=0,failed=0,thread=7,owner=7,reads=0,adds=0,removes=0,at=0,writesAfter=0;
std::uint64_t revision=4;DWORD error=73;
bool profile=true,signature=true,pin=true,providerOk=true,session=true,persist=true,master=true;
bool injected=false,revive=false,reset=false;int config=1;std::uint8_t battle=0;
bool throwAssignment=false,probeProvider=false;
std::string scenario;constexpr std::uintptr_t Base=0x400000;
R::Image image;std::uint64_t(*provider)()=nullptr;
void Check(bool ok,const char* text){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",text);}}
template<class F> struct Exit {F f;~Exit(){f();}};
template<class F> Exit<F> OnExit(F f){return {f};}
int Assign(std::uint8_t,int);
}
static DWORD GetLastError(){return Test::error;}
static void SetLastError(DWORD error){Test::error=error;}
namespace FfxHooks {
namespace Config {
enum class IntReadState {Missing,Valid,Invalid};
struct IntReadResult {IntReadState state;int value;};
IntReadResult ReadIntExact(const char*,int,int){return {Test::config<0?IntReadState::Invalid:IntReadState::Valid,Test::config};}
bool SetInt(const char*,int value){if(!Test::persist)return false;Test::config=value;return true;}
}
struct F8FlagSpec{};struct FlagValue {bool value;};
const F8FlagSpec* FindF8Flag(const char*){static F8FlagSpec flag;return &flag;}
FlagValue ResolveF8Flag(const F8FlagSpec&){return {Test::master};}
namespace SeymourSession {
bool PublisherReady() noexcept{return Test::session;}
Token Capture(bool) noexcept {return Test::session&&Test::owner==Test::thread?Token{Test::revision,Test::owner}:Token{};}
bool Current(const Token& t,bool) noexcept{return Test::session&&Test::owner==Test::thread&&t==Token{Test::revision,Test::owner};}
}
namespace SeymourBattle {bool RegisterPermanentRosterProvider(std::uint64_t(*value)()){Test::provider=value;return Test::providerOk;}}
namespace RecoveryEvidence {struct Proof {std::uintptr_t rva;const std::uint8_t* bytes;std::size_t size;const std::uint16_t* relocations;std::size_t relocationCount;};}
namespace RecoveryNative {
bool Profile(std::uintptr_t base){return Test::profile&&base==Test::Base;}
bool Match(std::uintptr_t,const RecoveryEvidence::Proof&){return Test::signature;}
bool Pin(const void*){return Test::pin;}
bool Range(std::uintptr_t,std::size_t,std::uintptr_t=0){return true;}
bool Copy(void* output,const void* input,std::size_t size){
 const auto offset=reinterpret_cast<std::uintptr_t>(input)-Test::Base;const void* data=nullptr;
 if(offset==0xd32494&&size==1)data=&Test::image.party;
 if(offset==0xd307e8&&size==3)data=Test::image.front.data();
 if(offset==0xd307eb&&size==17)data=Test::image.reserve.data();
 if(offset==0xd2a8e0&&size==1)data=&Test::battle;
 if(!data)return false;
 ++Test::reads;
 if(Test::at==Test::reads){
  if(Test::reset)++Test::revision;
  else{Test::config=0;SeymourPersistentRoster::PresentTick();if(Test::revive){Test::config=1;SeymourPersistentRoster::PresentTick();}}
  Test::injected=true;
 }
 std::memcpy(output,data,size);SetLastError(177);return true;
}
}
}
'''
SUFFIX=r'''
namespace Test {
int Assign(std::uint8_t character,int on){
 Check(character==7,"only Seymour assigned");if(injected)++writesAfter;
 if(on){++adds;image.party=0x11;image.reserve[0]=7;}
 else{++removes;image.party=0x10;for(auto& id:image.front)if(id==7)id=255;for(auto& id:image.reserve)if(id==7)id=255;}
 if(on&&probeProvider)Check(provider&&provider()==0,"in-progress assignment cannot reuse an old applied status");
 SetLastError(901);if(scenario=="partial"&&on)image.reserve[1]=8;
 if((scenario=="native-exception"||throwAssignment)&&on)throw std::runtime_error("simulated native assignment exception");return 1;
}
}
int main(int argc,char** argv){
 using namespace Test;if(argc!=2)return 2;scenario=argv[1];
 if(scenario.rfind("revoke-",0)==0)at=static_cast<unsigned>(std::stoul(scenario.substr(7)));
 if(scenario.rfind("revive-",0)==0){revive=true;at=static_cast<unsigned>(std::stoul(scenario.substr(7)));}
 if(scenario.rfind("reset-",0)==0&&scenario!="reset-read"){reset=true;at=static_cast<unsigned>(std::stoul(scenario.substr(6)));}
 image.party=0x10;image.front={0,1,2};image.reserve.fill(255);
 if(scenario=="off")config=0;if(scenario=="invalid")config=-1;
 if(scenario=="profile")profile=false;if(scenario=="signature")signature=false;if(scenario=="pin")pin=false;if(scenario=="provider")providerOk=false;
 if(scenario=="stop-before")R::RequestStop();
 R::Start(Base,scenario=="validate",nullptr);
 if(scenario=="off"||scenario=="invalid"||scenario=="profile"||scenario=="signature"||scenario=="pin"||scenario=="provider"||scenario=="validate"||scenario=="stop-before"){
  R::PumpTick();Check(!adds&&!R::BattleReady(),"closed startup does not add roster");
 }else if(at){
  R::PumpTick();Check(injected,"requested read boundary reached");Check(!writesAfter,"revoked session cannot start native assignment");
 }else if(scenario=="no-session"||scenario=="wrong-thread"||scenario=="battle"){
  if(scenario=="no-session")session=false;if(scenario=="wrong-thread")thread=9;if(scenario=="battle")battle=1;
  R::PumpTick();Check(!adds,"field and owner-thread admission required");
 }else if(scenario=="borrowed"){
  image.party=0x11;image.reserve[0]=7;R::PumpTick();Check(!adds&&R::BattleRevision()==revision,"native roster borrowed");
  Check(R::Remove()&&!removes&&image.party==0x11,"borrowed roster not removed");
 }else if(scenario.rfind("new-save-",0)==0){
  R::PumpTick();Check(adds==1&&provider&&provider()==revision,"first save has a verified applied roster");
  ++revision;image.party=0x10;image.reserve.fill(255);
  throwAssignment=scenario=="new-save-exception";
  probeProvider=scenario=="new-save-reentrant-provider";
  bool caught=false;try{R::PumpTick();}catch(const std::runtime_error&){caught=true;}
  Check(adds==2&&!removes,"new save never restores the previous roster baseline");
  if(throwAssignment){
   Check(caught&&!R::BattleRevision(),"exception in a new save cannot inherit the old applied status");
   throwAssignment=false;R::PumpTick();
  }
  Check(provider&&provider()==revision,"completed readback admits only the new save revision");
  Check(R::Remove()&&removes==1&&image.party==0x10,"only the new owned addition is restored");
 }else if(scenario=="provider-last-error"){
  R::PumpTick();SetLastError(0xbaad);
  Check(R::BattleRevision()==revision,"verified revision query succeeds");
  Check(GetLastError()==0xbaad,"read-only roster provider preserves caller LastError");
  R::RequestStop();SetLastError(0xabba);
  Check(!R::BattleRevision()&&GetLastError()==0xabba,"rejected revision query preserves caller LastError");
  Check(R::Remove(),"owned cleanup still completes");
 }else if(scenario=="menu"){persist=false;Check(!R::MenuAction(),"persist failure retained");persist=true;Check(R::MenuAction()&&config==0,"menu OFF persisted");
 }else{
  bool caught=false;try{R::PumpTick();}catch(const std::runtime_error&){caught=true;}
  const auto admissionReads=reads;Check(adds==1,"one native addition");Check(GetLastError()==901,"native LastError preserved");
  if(scenario=="partial"){Check(!R::BattleRevision(),"partial roster is not advertised to battle");Check(!R::Remove()&&!removes,"unowned roster drift is not overwritten");}
  else{
   if(scenario=="native-exception"){Check(caught,"native exception propagated");Check(!R::BattleRevision(),"unverified native exception is not advertised to battle");R::PumpTick();}
   Check(provider&&provider()==revision,"same-save revision provided to battle");
   R::PumpTick();Check(adds==1,"idempotent owned roster");
   if(scenario=="reset-read"){++revision;image.party=0x10;image.reserve[0]=255;config=0;R::PresentTick();R::PumpTick();Check(!removes,"new save never receives old roster cleanup");}
   else{R::RequestStop();R::PumpTick();Check(removes==1&&image.party==0x10,"stop restores only own roster addition");}
   Check(R::Remove(),"normal teardown completes");
  }
  std::printf("ADMISSION_READS=%u\n",admissionReads);
 }
 std::printf("SeymourRosterAdapterRt1 %s: %u/%u passed (simulated endpoints)\n",scenario.c_str(),checks-failed,checks);return failed?1:0;
}
'''

def run(output):
    output.mkdir(parents=True,exist_ok=False)
    adapter=MOD/'hooks/SeymourPersistentRosterHook.cpp';source=adapter.read_text()
    for name in ('SeymourBattleCore.h','RecoveryNative.h','F8FlagCatalog.h','../shared/Config.h'):
        token='#include "'+name+'"'
        if source.count(token)!=1:raise ValueError('endpoint include changed: '+name)
        source=source.replace(token,'// Explicit simulated endpoints above.')
    old='reinterpret_cast<int(__cdecl*)(std::uint8_t,int)>(base+AssignRva)'
    if source.count(old)!=1:raise ValueError('native assignment endpoint changed')
    source=portable_finally(source.replace(old,'Test::Assign'))
    emitted=output/'roster.cpp';emitted.write_text(PREFIX+source+SUFFIX)
    files=[adapter,Path(__file__),MOD/'hooks/SeymourPersistentRosterCore.h',MOD/'hooks/SeymourSessionCore.h',MOD/'hooks/SeymourOverdriveControl.h']
    (output/'manifest.json').write_text(json.dumps({'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'generated_sha256':hashlib.sha256(emitted.read_bytes()).hexdigest(),'live_game_proven':False},indent=2)+'\n')
    binary=output/'roster';cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(MOD/'hooks'),str(emitted),'-o',str(binary)]
    build=subprocess.run(cmd,capture_output=True,timeout=60);(output/'build.log').write_bytes(build.stdout+build.stderr)
    if build.returncode:print(build.stderr.decode(errors='replace'));return build.returncode
    results=[];boundaries=0
    def case(name):
        result=subprocess.run([str(binary),name],capture_output=True,timeout=15);(output/(name+'.log')).write_bytes(result.stdout+result.stderr)
        text=result.stdout.decode(errors='replace');print(text.strip(),flush=True);results.append({'case':name,'exit_code':result.returncode});return text
    for name in CASES:
        text=case(name)
        if name=='apply':boundaries=int(re.search(r'ADMISSION_READS=(\d+)',text).group(1))
    if not 0<boundaries<128:raise ValueError('invalid boundary inventory')
    for mode in ('revoke','revive','reset'):
        for n in range(1,boundaries+1):case(f'{mode}-{n}')
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('ROSTER_ADAPTER_CASES',sum(r['exit_code']==0 for r in results),'/',len(results),flush=True)
    return int(any(r['exit_code'] for r in results))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    raise SystemExit(run(p.parse_args().output.resolve()))
