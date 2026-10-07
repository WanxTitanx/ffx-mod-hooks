#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../hooks/OriginalPs2RngRuntime.h"
#include "../hooks/OriginalPs2RngEvidence.generated.h"
#include "../hooks/F8FlagCatalog.h"
#include "PrivatePeFixture.h"

namespace R=FfxHooks::OriginalPs2Rng;
namespace V=R::Runtime;
namespace E=R::Evidence;
namespace FfxHooks {
bool PublishF8RuntimeStatus(const char*,F8RuntimeAvailability,bool,bool){return true;}
}
static unsigned checks=0,failures=0,nativeClockReads=0;
static void Check(bool value,const char* text){++checks;if(!value&&++failures<16)std::printf("FAIL %s\n",text);}
static unsigned(__cdecl* resetForInterleave)()=nullptr;
struct Inputs {std::uint64_t raw=0,frequency=1000;unsigned thread=7,clockReads=0;bool clockOk=true,performanceOk=true,stopAtCommit=false,reenterReset=false,stopAtPerformance=false;};
static bool Performance(void* p,std::uint64_t& raw,std::uint64_t& frequency) noexcept {
    auto& in=*static_cast<Inputs*>(p);
    if(in.stopAtPerformance){in.stopAtPerformance=false;V::RequestStop();}
    if(in.reenterReset){in.reenterReset=false;const auto previous=in.thread;in.thread=8;resetForInterleave();in.thread=previous;}
    raw=in.raw;frequency=in.frequency;SetLastError(7311);return in.performanceOk;
}
static bool Calendar(void* p,R::UtcCalendar& date) noexcept {
    auto& in=*static_cast<Inputs*>(p);++in.clockReads;date={2026,10,5,12,34,56};SetLastError(7312);return in.clockOk;
}
static unsigned Thread(void* p) noexcept{return static_cast<Inputs*>(p)->thread;}
static void Committed(void* p) noexcept {
    if(static_cast<Inputs*>(p)->stopAtCommit){V::RequestStop();Check(!V::Stop(),"cannot disable clock hook during committed initialization");}
}
static void Write(unsigned char* at,const void* data,size_t bytes){
    DWORD previous=0,ignored=0;
    if(!VirtualProtect(at,bytes,PAGE_EXECUTE_READWRITE,&previous))std::abort();
    std::memcpy(at,data,bytes);
    if(!VirtualProtect(at,bytes,previous,&ignored))std::abort();
}
static void Return(unsigned char* at,unsigned result){unsigned char code[6]={0xB8,0,0,0,0,0xC3};std::memcpy(code+1,&result,4);Write(at,code,sizeof(code));}
static void Jump(unsigned char* at,const void* function){
    unsigned char code[5]={0xE9,0,0,0,0};
    const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(function)-reinterpret_cast<std::uintptr_t>(at)-5);
    std::memcpy(code+1,&delta,4);Write(at,code,sizeof(code));
}
static unsigned __cdecl ResetFinal(){SetLastError(9921);return 1234;}
static unsigned __cdecl NativeTime(unsigned char* output){++nativeClockReads;std::memset(output,0,8);output[1]=0x22;SetLastError(9922);return 0;}
static unsigned __cdecl ForeignClock(){return 2;}
static unsigned InvokeSeed(unsigned char* image,unsigned argument){
    void* site=image+E::InitCallerRva-1;unsigned result;
    __asm {
        mov eax,argument
        call site
        mov result,eax
    }
    return result;
}
static void CheckState(unsigned char* image,const R::State& expected){
    Check(*reinterpret_cast<unsigned*>(image+E::NormalStateRva)==expected.normal,"native normal state matches the reference");
    Check(*reinterpret_cast<unsigned*>(image+E::AuxiliaryStateRva)==expected.auxiliary,"native auxiliary initialization is preserved");
    Check(std::memcmp(image+E::ChannelsRva,expected.channels.data(),sizeof(expected.channels))==0,"all 68 real native channels match");
}
int main(int argc,char** argv){
    if(argc!=3)return 2;const std::string mode=argv[2];
    const HMODULE module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module||!PrivatePeFixture::NormalizeRelocations(module))return 2;
    auto* image=reinterpret_cast<unsigned char*>(module);
    Check(image[E::InitCallerRva-1]==0x50,"native caller pushes the temporal argument");
    // Preserve the original E8 call sites; bypass only the unrelated remainder
    // of their parent functions in this private image.
    const unsigned char finish[]{0x83,0xC4,0x04,0xC3};
    Write(image+E::InitCallerRva+5,finish,sizeof(finish));
    const unsigned char ret=0xC3;Write(image+E::ResetCallerRva+5,&ret,1);
    // The real reset body writes the native counter. Its renderer/input helper
    // calls are private stubs; no renderer, loader, entrypoint or TLS is run.
    for(unsigned at : {57u,65u,70u,75u}){
        Check(image[E::ResetRva+at]==(at==75?0xE9:0xE8),"reviewed reset helper edge");
        std::int32_t delta=0;std::memcpy(&delta,image+E::ResetRva+at+1,4);
        const auto target=E::ResetRva+at+5+delta;
        if(target>=0x237D000-6)return 2;
        if(at==75)Jump(image+target,reinterpret_cast<const void*>(&ResetFinal));else Return(image+target,1234);
    }
    const unsigned service=FfxHooks::ExecutableProfile::Steam20261001?0x22F5B0:0x22F760;
    Jump(image+service,reinterpret_cast<const void*>(&NativeTime));
    FlushInstructionCache(GetCurrentProcess(),image,0x237D000);
    std::array<unsigned char,69> clockBefore{};
    std::memcpy(clockBefore.data(),image+E::ClockRva,clockBefore.size());
    Inputs input{};
    Check(V::SetInputSourceForTests({&input,Performance,Calendar,Thread,Committed}),"explicit private inputs accepted before startup");
    const auto reset=reinterpret_cast<unsigned(__cdecl*)()>(image+E::ResetCallerRva);
    resetForInterleave=reset;
    const auto clock=reinterpret_cast<unsigned(__cdecl*)()>(image+E::ClockRva);
    if(mode=="signature"){const unsigned char changed=image[E::InitializerRva+10]^1;Write(image+E::InitializerRva+10,&changed,1);}
    if(mode=="table")image[E::MultipliersRva]^=1;
    if(mode=="late")*reinterpret_cast<unsigned*>(image+E::ChannelsRva)=9;
    if(mode=="stop-before")V::RequestStop();
    input.stopAtPerformance=mode=="stop-starting";
    void* foreignOriginal=nullptr;
    if(mode=="hook-conflict"){
        Check(MH_Initialize()==MH_OK,"private foreign owner initializes its coordinator");
        Check(MH_CreateHook(image+E::ClockRva,reinterpret_cast<void*>(&ForeignClock),&foreignOriginal)==MH_OK,"foreign disabled clock lease exists before admission");
    }
    const bool enabled=mode!="off",validate=mode=="validate";
    const bool started=V::Start(reinterpret_cast<std::uintptr_t>(image),enabled,validate);
    if(mode=="off"||mode=="signature"||mode=="table"||mode=="late"||validate||mode=="stop-before"||mode=="stop-starting"||mode=="hook-conflict"){
        Check(!started,"unadmitted startup installs no active family");
        Check(input.clockReads==0,"startup does not consume a clock sample");
        Check(std::memcmp(image+E::ClockRva,clockBefore.data(),clockBefore.size())==0,"clock code stays unchanged when admission fails");
        if(mode=="signature"||mode=="table")Check(V::Status().reason==V::Reason::SignatureMismatch,"specific signature rejection reached");
        if(mode=="late")Check(V::Status().reason==V::Reason::CounterOriginMissing,"late attachment rejection reached");
        if(validate)Check(V::Status().reason==V::Reason::ValidationOnly,"validate-only path reached");
        if(mode=="hook-conflict"){
            Check(V::Status().reason==V::Reason::HookConflict,"foreign ownership is a visible conflict");
            Check(MH_RemoveHook(image+E::ClockRva)==MH_OK,"rollback preserves the foreign lease");
            Check(MH_CreateHook(image+E::InitializerRva,reinterpret_cast<void*>(&ForeignClock),&foreignOriginal)==MH_OK,"failed family creation releases only its unpublished initializer lease");
            Check(MH_RemoveHook(image+E::InitializerRva)==MH_OK,"private control lease is released");
        }
    }else{
        Check(started,"valid family installs on the exact private profile");
        Check(clock()==0x23&&nativeClockReads==1,"entropy calls outside admitted init retain native behavior");
        input.reenterReset=mode=="rebind-reset";
        if(mode!="no-reset")Check(reset()==1234,"original reset result survives exactly once");
        if(mode!="no-reset")Check(GetLastError()==9921,"reset preserves native last-error after observation");
        if(mode=="rebind-reset")Check(V::Status().resets==1&&V::Status().reason==V::Reason::WrongThread,"interleaved resets cannot rebind the owner");
        if(mode=="reset-phase"){
            input.raw=30000;Check(reset()==1234,"a later native reset preserves its return");
            Check(V::Status().resets==2,"both observed native resets establish epochs");
        }
        input.raw=50000;input.clockOk=mode!="clock-failure";input.performanceOk=mode!="counter-failure";
        input.thread=mode=="wrong-thread"?8:7;input.stopAtCommit=mode=="stop-committed";
        if(mode=="foreign-counter")*reinterpret_cast<unsigned*>(image+E::CounterRva)=1;
        const unsigned before=nativeClockReads;
        const bool apply=mode=="success"||mode=="stop-committed"||mode=="reset-phase"||mode=="argument-wrap";
        const unsigned originalArgument=mode=="argument-wrap"?0xFFFFFFFEu:17u;
        SetLastError(4311);
        const auto result=mode=="unknown-caller"?reinterpret_cast<unsigned(__cdecl*)(unsigned)>(image+E::InitializerRva)(originalArgument):InvokeSeed(image,originalArgument);
        Check(GetLastError()==(apply?4311u:9922u),"input acquisition preserves incoming error and native result error");
        R::ClockBytes bytes{};R::EncodeHealthyJapanClock({2026,10,5,12,34,56},bytes);
        const unsigned expectedArgument=apply?originalArgument+(mode=="reset-phase"?1199u:2997u):originalArgument;
        const auto expected=R::Initialize(apply?R::ClockXor(bytes):0x22,expectedArgument).state;
        CheckState(image,expected);
        Check(result==(expected.normal&0x7FFFFFFFu),"native initializer return value is unchanged");
        Check(nativeClockReads-before==(apply?0u:1u),"one original initialization and scoped-only entropy override");
        const auto status=V::Status();
        Check(status.initializations==(apply?1u:0u),"only completed admitted initializations become applied");
        Check(status.code==(apply?(mode=="stop-committed"?V::Code::StopPending:V::Code::Active):V::Code::Unavailable),"truthful activation or rejection state");
        if(apply){
            const auto preserved=expected;
            Check(V::Stop(),"normal-context stop drains and neutralizes the family");
            CheckState(image,preserved);
            Check(clock()==0x23,"stopped entropy hook forwards the native path");
        }
    }
    V::Service();V::Stop();
    std::printf("ORIGINAL_PS2_RNG_RUNTIME_RT1 %s %u/%u passed (private native image; input/renderer stubs)\n",mode.c_str(),checks-failures,checks);
    // Gateways are process-lifetime. The private image is reclaimed at exit.
    return failures?1:0;
}
