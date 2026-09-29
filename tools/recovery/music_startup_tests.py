#!/usr/bin/env python3
"""Execute the actual Music startup block with simulated configuration and hooks.
No game, DLL, sound device, installed configuration or native memory is accessed.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

MOCK = r'''
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <limits>
#include "ffx_hooks_block.h"
using LONG=std::int32_t;
FFXHooksBlock* g_block=nullptr;
std::uintptr_t g_base=0x400000;
bool g_runtimeValidateOnly=false,g_musicHookArmed=false;
namespace Fake {
FFXHooksBlock block;
int requested=-1,custom=-1,lateChoice=-1,creates=0,installs=0,cas=0;
bool enabled=true,validate=false,createOk=true,hookOk=true,targetOk=true,dual=false,battleTargets=true,ordered=true;
std::string logs;
void Reset(){requested=custom=lateChoice=-1;creates=installs=cas=0;enabled=createOk=hookOk=targetOk=battleTargets=true;
 validate=dual=false;ordered=true;logs.clear();block.musicOverrideTrackIndex=-1;g_block=nullptr;g_musicHookArmed=false;}
}
LONG InterlockedCompareExchange(volatile LONG* p,LONG value,LONG expected){
 ++Fake::cas;Fake::ordered=Fake::ordered&&g_musicHookArmed&&g_block&&Fake::installs==1;
 if(Fake::lateChoice>=0){*p=Fake::lateChoice;Fake::lateChoice=-1;}
 const LONG old=*p;if(old==expected)*p=value;return old;
}
void Log(const char* format,...){char text[1024]{};va_list args;va_start(args,format);
 std::vsnprintf(text,sizeof(text),format,args);va_end(args);Fake::logs+=text;}
void LogLine(const char*){}
bool MusicHookEnabledFromConfig(){return Fake::enabled;}
bool EnvFlagEnabled(const char* name){return std::strcmp(name,"FFXHOOKS_VALIDATE_ONLY")==0?Fake::validate:true;}
int EnvInt(const char*,int){return Fake::requested;}
bool FpsScoutEnabledFromConfig(){return false;}
bool WaitForProbeHeartbeat(unsigned){return true;}
bool CreateBlock(){++Fake::creates;if(!Fake::createOk)return false;g_block=&Fake::block;return true;}
unsigned GetLastError(){return 0;}
int ArenaPlus_MusicFadeFrames(){return 20;}
bool ArenaPlusMusicFlagEnabledRaw(){return Fake::dual;}
void ArenaPlus_MusicHookProbeSoundCmd(){}
constexpr std::uintptr_t RVA_FMOD_SWITCH_CROSSFADE=1,RVA_FMOD_PLAY_TRACK=2;
std::uintptr_t rva(std::uintptr_t offset){return g_base+offset;}
struct MusicTargetValidation {bool switchCrossfadeOk,playTrackOk,playTrackWithPreloadOk,prepBattleTrackOk;};
MusicTargetValidation ValidateMusicTargets(){return {Fake::targetOk,Fake::targetOk,Fake::battleTargets,Fake::battleTargets};}
namespace FfxHooks {
enum class MusicHookTarget {SwitchCrossfade,PlayTrack};
struct MusicHookInstallResult {bool ok;std::uintptr_t trampoline;};
const char* GetMusicHookTargetName(MusicHookTarget){return "simulated target";}
void SetMusicHookMinFadeFrames(int){}
void SetArenaBattleMusicSoundCmdFn(void(*)()){}
MusicHookInstallResult Install(){++Fake::installs;
 if(Fake::custom>=0&&g_block)g_block->musicOverrideTrackIndex=Fake::custom;
 return {Fake::hookOk,0};}
MusicHookInstallResult InstallMusicHookDual(std::uintptr_t,FFXHooksBlock*,void(*)(const char*)){return Install();}
MusicHookInstallResult InstallMusicHookArenaBattle(std::uintptr_t,FFXHooksBlock*,void(*)(const char*)){return Install();}
MusicHookInstallResult InstallMusicHook(std::uintptr_t,FFXHooksBlock*,MusicHookTarget,void(*)(const char*)){return Install();}
}
FfxHooks::MusicHookTarget MusicHookTargetFromEnv(){return FfxHooks::MusicHookTarget::PlayTrack;}
'''
TEST = r'''
int total=0,failed=0;
void Check(bool condition,const char* message){++total;if(!condition){++failed;std::fprintf(stderr,"FAIL: %s\n",message);}}
int main(){
 Fake::Reset();Startup();Check(g_block&&g_block->musicOverrideTrackIndex==-1,"missing override leaves default");
 Check(Fake::cas==0,"missing override does not publish");
 for(int path=0;path<3;++path){Fake::Reset();Fake::requested=37;Fake::dual=path>0;Fake::battleTargets=path!=2;Startup();
  Check(g_block&&g_block->musicOverrideTrackIndex==37,"valid boot override applied after block creation");
  Check(Fake::logs.find("out-of-range")==std::string::npos,"valid boot value never logged out-of-range");
  Check(Fake::installs==1,"original install branch retained");
  Check(Fake::cas==1&&Fake::ordered,"exactly one publication after successful installation");}
 for(int invalid:{-2,182,(std::numeric_limits<int>::min)(),(std::numeric_limits<int>::max)()}){Fake::Reset();Fake::requested=invalid;Startup();
  Check(g_block&&g_block->musicOverrideTrackIndex==-1,"invalid override cannot publish");
  Check(Fake::logs.find("out-of-range")!=std::string::npos,"invalid override has range diagnostic");}
 for(int boundary:{0,181}){Fake::Reset();Fake::requested=boundary;Startup();
  Check(g_block&&g_block->musicOverrideTrackIndex==boundary,"both allowed range boundaries publish");}
 Fake::Reset();Fake::requested=37;Fake::enabled=false;Startup();
 Check(Fake::creates==0&&Fake::installs==0&&Fake::cas==0,"disabled music stays non-mutating");
 Check(Fake::logs.find("out-of-range")==std::string::npos,"disabled valid request is not a range error");
 Fake::Reset();Fake::requested=37;Fake::validate=true;Startup();
 Check(Fake::creates==0&&Fake::installs==0&&Fake::cas==0,"validate-only does not create or publish");
 Fake::Reset();Fake::requested=37;Fake::targetOk=false;Startup();
 Check(Fake::creates==0&&Fake::installs==0&&Fake::cas==0,"profile rejection does not create or publish");
 Fake::Reset();Fake::requested=37;Fake::createOk=false;Startup();
 Check(!g_block&&Fake::cas==0,"block creation failure does not publish");
 Check(Fake::logs.find("out-of-range")==std::string::npos,"missing block is not a range error");
 Fake::Reset();Fake::requested=37;Fake::hookOk=false;Startup();
 Check(g_block&&g_block->musicOverrideTrackIndex==-1&&Fake::cas==0,"failed hook is not armed by boot override");
 Fake::Reset();Fake::requested=37;Fake::custom=87;Startup();
 Check(g_block&&g_block->musicOverrideTrackIndex==87,"Custom Mix or manual choice during install wins");
 Fake::Reset();Fake::requested=37;Fake::lateChoice=109;Startup();
 Check(g_block&&g_block->musicOverrideTrackIndex==109,"choice arriving at publication wins atomically");
 Check(Fake::cas==1&&Fake::ordered,"racing choice does not retry or reassert boot value");
 static_assert(sizeof(FFXHooksBlock)==256,"use actual shared schema");
 std::printf("MusicStartupSimulatedRt1: %d/%d passed (actual startup block, simulated dependencies)\n",total-failed,total);
 return failed?1:0;
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-cpp',type=Path,help='Write a standalone harness into a NEW file for Windows verification')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    source=(root/'src/runtime/FfxHooksDll/dllmain.cpp').read_text(encoding='utf-8')
    start=source.index('    const bool enableMusic = MusicHookEnabledFromConfig();')
    end=source.index('    LogF8CatalogGate("labs.nova_super_damage", "Lab startup");',start)
    block=source[start:end]
    translation=MOCK+'\nvoid Startup(){\n'+block+'\n(void)enableFpsScout;\n}\n'+TEST
    header=root/'src/runtime/FfxHooksDll/shared/ffx_hooks_block.h'
    if args.emit_cpp:
        with args.emit_cpp.open('x',encoding='utf-8') as stream:
            stream.write(translation.replace('#include \"ffx_hooks_block.h\"',header.read_text()))
        return
    compiler=shutil.which(os.environ.get('CXX','g++'))
    if not compiler:raise SystemExit('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='ffx-music-startup-rt1-') as temp:
        path=Path(temp);cpp=path/'test.cpp';exe=path/'test'
        cpp.write_text(translation)
        shutil.copyfile(header,path/'ffx_hooks_block.h')
        flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror']
        if os.environ.get('FFX_MUSIC_TEST_SANITIZE'):
            flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
        subprocess.run([compiler,*flags,str(cpp),'-o',str(exe)],check=True,timeout=60)
        subprocess.run([str(exe)],check=True,timeout=10)
if __name__=='__main__':main()
