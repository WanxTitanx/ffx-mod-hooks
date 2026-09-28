// Jarvis-HOOK: one action owner with process-lived observers and exactly one
// native call. Only the graphics-heavy result suffix is isolated in this test.
#include <cstdio>
#if __has_include("../hooks/SharedActionRuntime.h")
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/SharedActionRuntime.h"
#include <array>
#include <cstring>
namespace A=FfxHooks::SharedAction;
static unsigned checks=0,failures=0,beginResults=0,endResults=0,beginFinishes=0,endFinishes=0,nativeResults=0;
static bool fault=false;
static int marker=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static int __cdecl Result(unsigned source,unsigned sub,unsigned target,int* result,void* data){
    ++nativeResults;
    Check(source==2&&sub==1&&target==18&&data==&marker,"shared result producer preserves all arguments");
    if(fault)RaiseException(0xE0077701,0,0,nullptr);
    if(result)*result=12345;return 6789;
}
static __declspec(naked) void ResultSuffix(){
    __asm {add esp,4}
    __asm {pop edi}
    __asm {pop esi}
    __asm {pop ebx}
    __asm {mov esp,ebp}
    __asm {pop ebp}
    __asm {jmp Result}
}
static bool Patch(std::uintptr_t at,void* to){
    unsigned char bytes[5]={0xE9};const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(to)-at-5);
    std::memcpy(bytes+1,&delta,4);DWORD old=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(at),5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(at),bytes,5);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),5);
    return VirtualProtect(reinterpret_cast<void*>(at),5,old,&ignored)!=FALSE;
}
static void* BeforeResult(const A::ResultCall& call) noexcept {
    ++beginResults;Check(call.source==2&&call.sub==1&&call.target==18,"observer receives the actual result identity");return &marker;
}
static void AfterResult(void* token,const A::ResultCall&,int value,bool complete) noexcept {
    ++endResults;Check(token==&marker&&complete==!fault&&(!complete||value==6789),"result retirement retains exact return/exception semantics");
}
static void* BeforeFinish(const A::FinishCall& call) noexcept {
    ++beginFinishes;Check(call.owner==2&&call.index==0&&!call.preserve,"observer receives the actual queue removal arguments");return &marker;
}
static void AfterFinish(void* token,const A::FinishCall&,int value,bool complete) noexcept {
    ++endFinishes;Check(token==&marker&&complete&&value==1,"observer sees the actual native queue removal result");
}
static const A::Observer observer{BeforeResult,AfterResult,BeforeFinish,AfterFinish};
static int __cdecl LegacyResult(unsigned source,unsigned sub,unsigned target,int* result,void* data){
    return A::OriginalResult()(source,sub,target,result,data);
}
static int __cdecl LegacyFinish(unsigned owner,unsigned index,unsigned preserve){return A::OriginalFinish()(owner,index,preserve);}
static const A::Legacy legacy{LegacyResult,LegacyFinish};
static bool InvokeResult(A::ResultFunction call){
    __try {int output=0;return call(2,1,18,&output,&marker)==6789&&output==12345;}
    __except(GetExceptionCode()==0xE0077701?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;const bool first=std::strcmp(argv[2],"legacy-first")==0;
    if(!first&&std::strcmp(argv[2],"observer-first"))return 2;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    Check(PrivatePeFixture::NormalizeRelocations(image),"private mapped image relocates");
    Check(Patch(base+0x38F0C0,reinterpret_cast<void*>(&ResultSuffix)),"only the graphical result suffix is isolated");
    if(first)Check(A::RegisterLegacy(&legacy),"legacy registers first");
    Check(A::Subscribe(A::Slot::Elemental,&observer),"independent action observer registers");
    Check(A::Start(base),"one profile-gated action owner starts");
    if(!first)Check(A::RegisterLegacy(&legacy),"legacy joins an existing action owner");
    Check(A::Start(base),"same installed owner is reused without a duplicate detour");
    const auto result=reinterpret_cast<A::ResultFunction>(base+0x38F0B0);
    Check(InvokeResult(result)&&nativeResults==1&&beginResults==1&&endResults==1,"actual result executes once between observer boundaries");
    fault=true;Check(!InvokeResult(result)&&nativeResults==2&&beginResults==2&&endResults==2,"native exception retires the observer before propagating");fault=false;
    std::array<unsigned char,31*0xF90> actors{};const auto pointer=reinterpret_cast<std::uintptr_t>(actors.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD334CC),&pointer,4);
    auto* actor=actors.data()+2*0xF90;actor[0xC]=2;actor[0xDE5]=0;actor[0xDE7]=1;
    auto* queue=reinterpret_cast<unsigned char*>(base+0xD2AC70);std::memset(queue,0,72);
    queue[0]=2;queue[2]=queue[3]=1;queue[8]=0;queue[9]=0x30;queue[10]=255;
    *reinterpret_cast<unsigned char*>(base+0xD2BDE1)=1;
    const auto finish=reinterpret_cast<A::FinishFunction>(base+0x3B0870);
    Check(finish(2,0,0)==1&&beginFinishes==1&&endFinishes==1&&*reinterpret_cast<unsigned char*>(base+0xD2BDE1)==0,
          "the actual native queue removal runs once with observers attached");
    Check(A::UnregisterLegacy(&legacy),"legacy stops without disabling the observer");
    Check(InvokeResult(result)&&beginResults==3&&endResults==3,"observer-only startup remains operational");
    Check(A::Unsubscribe(A::Slot::Elemental,&observer),"observer closes its own admission");
    Check(InvokeResult(result)&&beginResults==3&&endResults==3,"no observers means exact native result behavior");
    std::printf("SHARED_ACTION_RUNTIME %s %u/%u passed\n",argv[2],checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production SharedActionRuntime.h is missing");return 1;}
#endif
