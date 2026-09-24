#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "../hooks/FmvSpeedHook.h"
#include "../hooks/SpeedHackHook.h"
#include "MusicPeFixture.inc"
namespace F=FfxHooks::FmvSpeed;
static int checks,failures;
static unsigned frames=0;
static float frequency=48000;
static int soundIdentity=1;
static bool allowRate=true;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static int __stdcall GetFrequency(void*,float* out){*out=frequency;return 0;}
static int __stdcall SetFrequency(void*,float value){if(!allowRate)return 1;frequency=value;return 0;}
static int __stdcall GetSound(void*,void** out){*out=&soundIdentity;return 0;}
static bool __fastcall NativeFrameSink(void* self,void*,void* a,void* b,void* c){
    Check(a==reinterpret_cast<void*>(1)&&b==reinterpret_cast<void*>(2)&&c==reinterpret_cast<void*>(3),"native frame texture arguments remain exact");
    auto* raw=static_cast<unsigned char*>(self);auto& read=*reinterpret_cast<unsigned*>(raw+0x74);const auto written=*reinterpret_cast<unsigned*>(raw+0x78);
    if(read==written)return false;
    ++read;++*reinterpret_cast<unsigned*>(raw+0x100);++frames;return true;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* image=MapPe(Read(argv[1]));if(!image)return 2;
    std::array<unsigned char,0x750> parent{};auto* core=parent.data()+0x38;
    *reinterpret_cast<std::uintptr_t*>(image+0x8DED2C)=reinterpret_cast<std::uintptr_t>(parent.data());
    parent[0x6D0]=1;*reinterpret_cast<unsigned*>(core+0xE4)=1;core[0x58]=1;
    *reinterpret_cast<unsigned*>(core+0x78)=100;*reinterpret_cast<void**>(core+0x2A0)=&frequency;
    *reinterpret_cast<double*>(core+0x108)=33333333.333333333;
    *reinterpret_cast<std::uint32_t*>(image+0x8E82A4)=0;
    const auto original=reinterpret_cast<F::FrameFn>(&NativeFrameSink);
    Check(!F::PrepareForTests(0,original,{GetFrequency,SetFrequency,GetSound}),"unknown executable cannot acquire the movie path");
    Check(F::PrepareForTests(reinterpret_cast<std::uintptr_t>(image),original,{GetFrequency,SetFrequency,GetSound}),"exact movie consumer and call sites are admitted");
    auto fetch=[&](unsigned caller=0x2DAF6A){return F::FetchForTests(core,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),caller);};
    using Timestamp=std::uint64_t(__thiscall*)(void*);
    const auto timestamp=reinterpret_cast<Timestamp>(image+0x627BD0);
    const auto before=timestamp(core);
    F::SetDesired(4,true);Check(fetch()&&frames==4&&frequency==192000,"movie frame progress and its audio channel use the same factor");
    const auto elapsed=timestamp(core)-before;
    Check(elapsed>=133333330&&elapsed<=133333336,"actual native movie timestamp advances by four original frames");
    F::Neutralize();Check(frequency==48000,"focus/OFF restores the exact original audio frequency");
    const unsigned stale=F::PublicationEpoch();F::Neutralize();F::SetDesired(8,true,stale);
    Check(F::CurrentStatus().requested==1,"a stale Present frame cannot rearm movie speed after focus loss");
    *reinterpret_cast<unsigned*>(image+0x8E82A4)=1;F::SetDesired(8,true);unsigned prior=frames;
    Check(fetch()&&frames==prior+1&&frequency==48000,"a non-neutral legacy booster prevents double multiplication");
    *reinterpret_cast<unsigned*>(image+0x8E82A4)=0;
    F::SetDesired(2,true);prior=frames;Check(fetch(0x1234)&&frames==prior+1&&frequency==48000,"unrecognized callers remain native");
    allowRate=false;prior=frames;Check(fetch()&&frames==prior+1&&frequency==48000,"audio API rejection cannot accelerate only the picture");allowRate=true;
    F::SetDesired(8,true);const auto read=*reinterpret_cast<unsigned*>(core+0x74);*reinterpret_cast<unsigned*>(core+0x78)=read+3;prior=frames;
    Check(fetch()&&frames==prior+2&&frequency==96000,"decoder queue depth safely limits acceleration");
    frequency=72000;prior=frames;
    Check(fetch()&&frames==prior+1&&frequency==72000,"foreign frequency changes are preserved");
    *reinterpret_cast<unsigned*>(core+0x78)=*reinterpret_cast<unsigned*>(core+0x74)+16;prior=frames;
    Check(fetch()&&frames==prior+1&&frequency==72000,"a foreign owner cannot be silently reacquired on the next frame");
    parent[0x6D0]=0;F::Neutralize();frequency=48000;parent[0x6D0]=1;
    F::SetDesired(2,true);Check(fetch()&&frequency==96000,"a new movie can acquire its independent rate");
    F::Neutralize();F::RequestStop();prior=frames;Check(fetch()&&frames==prior+1&&frequency==48000,"teardown leaves future movie fetches native");
    // Movie ownership must release both previous legacy routes before it can run.
    FfxHooks::SpeedHackArbitrationInput input{};input.requestedFactor=8;input.nativeStateReady=true;input.nativeAvailabilityReady=true;
    input.globalTickHookReady=true;input.dialogBypassReady=true;input.globalTargetOwned=true;input.nativeAvailable=true;input.movieOwnsRate=true;
    input.nativeOwned=true;input.observedNativeState=1;input.lastWrittenNativeState=1;
    const auto decision=FfxHooks::ResolveSpeedHackArbitration(input);
    Check(decision.nativeAction==FfxHooks::SpeedHackNativeAction::RestoreZero && decision.route.backend==FfxHooks::SpeedHackBackend::None && decision.route.routedFactor==1,
        "movie handoff restores the native multiplier and publishes a neutral scene route");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("FmvSpeedNativeRt1: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
