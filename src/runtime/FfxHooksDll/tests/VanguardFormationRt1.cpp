// Jarvis-HOOK: real formation, input-queue and CTB writers over an isolated PE.
// Model/UI/global status-rebuild endpoints are inert; no game session is started.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/VanguardRuntime.h"
#include "../hooks/SharedBattleRuntime.h"
#include "../shared/Config.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
namespace V=FfxHooks::Vanguard;
namespace B=FfxHooks::SharedBattleRuntime;
static std::uintptr_t base,switchCall;
static std::array<unsigned char,31*0xF90> actors{};
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void Log(const char* text){std::fputs(text,stdout);}
static void Put16(void* p,unsigned value){const auto word=static_cast<std::uint16_t>(value);std::memcpy(p,&word,2);}
static void Put32(void* p,int value){std::memcpy(p,&value,4);}
static unsigned char* Actor(unsigned owner){return actors.data()+owner*0xF90;}
static unsigned char* At(unsigned rva){return reinterpret_cast<unsigned char*>(base+rva);}
static int __cdecl NoPresentation(){return 0;}
static unsigned __cdecl WaitData(unsigned){return 4;}
static int __cdecl Scene(void*){return 0;}
static bool Patch(unsigned rva,const void* data,std::size_t size){
    DWORD prior=0,ignored=0;auto* dst=At(rva);
    if(!VirtualProtect(dst,size,PAGE_EXECUTE_READWRITE,&prior))return false;
    std::memcpy(dst,data,size);FlushInstructionCache(GetCurrentProcess(),dst,size);
    return VirtualProtect(dst,size,prior,&ignored)!=FALSE;
}
static bool Jump(unsigned rva,const void* target){
    unsigned char bytes[5]={0xE9};const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-(base+rva+5));
    std::memcpy(bytes+1,&delta,4);return Patch(rva,bytes,5);
}
// Enter the actual Switch command CALL instruction, preserving its native
// return address. Only the following rendering suffix is replaced in the fixture.
static __declspec(naked) int __cdecl NativeSwitch(unsigned,unsigned){
    __asm {
        push ebp
        mov ebp,esp
        push offset resumed
        push 0
        push 0
        push dword ptr [ebp+0Ch]
        push dword ptr [ebp+8]
        jmp dword ptr [switchCall]
    resumed:
        mov esp,ebp
        pop ebp
        ret
    }
}
static void Reset(){
    actors.fill(0);
    for(unsigned i=0;i<31;++i){auto* a=Actor(i);Put16(a+0xC,i);Put16(a+0xE,i<18?i:0x1000+i);
        a[0x10]=i<7?1:0;a[0xDD4]=a[0xDD6]=1;a[0xDE4]=a[0xDE5]=255;
        a[0x65C]=a[0x65D]=12;a[0xDE8]=3;a[0x5BD]=100;
        Put32(a+0x594,1000);Put32(a+0x598,100);Put32(a+0x5D0,1000);Put32(a+0x5D4,100);
        if(i<3)a[0xDC8]=1;
    }
    std::memset(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>())),255,7);At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]=0;At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[1]=1;At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[2]=2;
    std::memset(At((::FfxHooks::ExecutableProfile::Rva<0xD2C8A3>())),255,17);for(unsigned i=0;i<4;++i)At((::FfxHooks::ExecutableProfile::Rva<0xD2C8A3>()))[i]=static_cast<unsigned char>(i+3);
    std::memset(At((::FfxHooks::ExecutableProfile::Rva<0xD2AA80>())),0,62*8);std::memset(At((::FfxHooks::ExecutableProfile::Rva<0xD2AC70>())),0,62*72);
    *At((::FfxHooks::ExecutableProfile::Rva<0xD2BDE0>()))=*At((::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>()))=0;*At((::FfxHooks::ExecutableProfile::Rva<0xD2C9E4>()))=255;*At((::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>()))=1;
    B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});
}
static void InsertInput(unsigned owner){
    using Insert=int(__cdecl*)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
    Check(reinterpret_cast<Insert>(base+::FfxHooks::ExecutableProfile::Rva<0x3B2310>())(owner,0,1,0,255,0)==-1,"native input queue creates the outgoing turn");
    Actor(owner)[0xDE4]=0;
}
static void Removed(unsigned owner,bool shattered=false){
    auto* actor=Actor(owner);Put16(actor+0x616,0x100);
    if(shattered){Put16(actor+0x606,5);Put32(actor+0x5D0,0);}
    reinterpret_cast<void(__cdecl*)(unsigned,void*,int)>(base+::FfxHooks::ExecutableProfile::Rva<0x38DF00>())(owner,actor,1);
}
static void Scheduler(){(void)reinterpret_cast<int(__cdecl*)()>(base+::FfxHooks::ExecutableProfile::Rva<0x391000>())();}
static void SwitchCases(bool enabled){
    Reset();InsertInput(0);const unsigned beforeMp=Actor(3)[0x5D4];
    Check(NativeSwitch(0,3)==3&&At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==3&&At((::FfxHooks::ExecutableProfile::Rva<0xD2C8A3>()))[0]==0,
          "real native Swap updates active and reserve identities without manual array synthesis");
    Check(Actor(0)[0xDC8]==0&&Actor(3)[0xDC8]==1,"native swap changes only the intended active membership");
    std::printf("FORMATION_QUEUE enabled=%u wait=%u pending=%u total=%u sourceIndex=%u incomingIndex=%u\n",
                enabled?1u:0u,Actor(3)[0x65C],Actor(3)[0xDE6],*At((::FfxHooks::ExecutableProfile::Rva<0xD2BDE0>())),Actor(0)[0xDE4],Actor(3)[0xDE4]);
    // Swap preserves the outgoing current entry for its caller to finish. Only
    // the incoming entry belongs to the new cost; never remove both entries.
    Check(Actor(3)[0x65C]==(enabled?12:0)&&Actor(3)[0xDE6]==(enabled?0:1)&&*At((::FfxHooks::ExecutableProfile::Rva<0xD2BDE0>()))==(enabled?1:2),
          "paid switch consumes the incoming native queued turn once; OFF preserves its immediate turn");
    Check(Actor(3)[0x5D4]==beforeMp&&!Actor(3)[0x6CC]&&!Actor(3)[0x6CD],
          "switch payment neither regenerates MP nor stages unrelated resource costs");
    Reset();InsertInput(0);
    using Swap=int(__cdecl*)(unsigned,unsigned,int,unsigned);
    Check(reinterpret_cast<Swap>(base+::FfxHooks::ExecutableProfile::Rva<0x3ADAF0>())(0,3,0,0)==3&&Actor(3)[0x65C]==0&&Actor(3)[0xDE6]==1,
          "an unrelated native swap caller is not silently reclassified as a voluntary paid switch");
}
static void ReplacementCases(bool enabled){
    Reset();Actor(3)[0x65C]=20;Actor(4)[0x65C]=2;Removed(0);Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==(enabled?3:0),"replacement follows native reserve order, not lowest CTB");
    if(!enabled)return;
    Check(Actor(3)[0xDC8]&&Actor(3)[0x65C]>0&&Actor(3)[0x65C]<=20&&!Actor(3)[0xDE6]&&*At((::FfxHooks::ExecutableProfile::Rva<0xD2BDE0>()))==0,
          "automatic replacement retains the reserve wait and creates no free immediate input turn");
    Check(Actor(0)[0xDCD]&&!Actor(0)[0xDC8]&&Actor(3)[0x5D4]==100,
          "replacement does not revive the ejected actor or trigger a regeneration event");
    const auto formation=std::array<unsigned char,3>{At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0],At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[1],At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[2]};
    Removed(0);Scheduler();Check(std::memcmp(formation.data(),At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>())),3)==0,"duplicate removal cannot fill a second active slot");
    Reset();Put32(Actor(3)+0x5D0,0);Removed(0,true);Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==4&&Actor(0)[0xDCD],"Shatter chooses the first living native-eligible reserve and keeps the source removed");
    Reset();Actor(3)[0xDD4]=0;Removed(0);Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==4,"native Switch availability can reject an earlier listed reserve");
    Reset();Actor(3)[0x10]=Actor(5)[0x10]=0;Actor(4)[0xDD4]=0;Removed(0);Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==6,"battle-specific unloaded or unavailable party members are never fabricated as reserves");
    Reset();for(unsigned i=3;i<7;++i)Actor(i)[0x10]=0;Removed(0);Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==0&&!Actor(0)[0xDC8],"no eligible reserve leaves the authentic vacancy instead of inventing a party member");
    Reset();Removed(0);B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==0,"battle-generation reset discards a pending replacement from an abandoned battle");
    Reset();Put16(Actor(0)+0x606,1);Put32(Actor(0)+0x5D0,0);
    reinterpret_cast<void(__cdecl*)(unsigned,void*,int)>(base+::FfxHooks::ExecutableProfile::Rva<0x38DF00>())(0,Actor(0),1);Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==0,"ordinary KO is not changed into an Eject/Shatter substitution");
    Reset();Removed(0);Removed(1);Removed(2);Scheduler();
    Check(At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[0]==3&&At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[1]==4&&At((::FfxHooks::ExecutableProfile::Rva<0xD2C895>()))[2]==5,
          "multiple removals allocate distinct reserves in formation order");
}
int main(int argc,char** argv){
    if(argc!=3)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    const std::string mode=argv[2];const bool cost=mode=="switch"||mode=="both",reinforce=mode=="reinforce"||mode=="both";
    if(!cost&&!reinforce&&mode!="off")return 2;
    HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    base=reinterpret_cast<std::uintptr_t>(image);switchCall=base+::FfxHooks::ExecutableProfile::Rva<0x3AEB7F>();
    Check(PrivatePeFixture::NormalizeRelocations(image),"formation fixture normalizes the exact private PE");
    const auto table=reinterpret_cast<std::uintptr_t>(actors.data());std::memcpy(At((::FfxHooks::ExecutableProfile::Rva<0xD334CC>())),&table,4);
    for(unsigned rva:{(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x38D5A0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x38E460u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3AB380u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x388BF0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3A0420u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x396F30u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x396950u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x39F010u>())>())>())>()),
                      (::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3AD400u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3AD480u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3AF810u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3AF4C0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3AFB70u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3ACEC0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3B06C0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x3B0CE0u>())>())>())>())})
        Check(Jump(rva,reinterpret_cast<void*>(&NoPresentation)),"model/UI and unrelated global-rebuild boundaries are isolated");
    Check(Jump((::FfxHooks::ExecutableProfile::Rva<0x390A10>()),reinterpret_cast<void*>(&WaitData)),"native CTB formula reads a bounded constant table fixture");
    const unsigned char endSwitch[]={0x83,0xC4,0x10,0xC3};Check(Patch((::FfxHooks::ExecutableProfile::Rva<0x3AEB84>()),endSwitch,sizeof(endSwitch)),"native Switch call keeps its original callsite and isolates only its rendering suffix");
    FfxHooks::Config::ResetForTests();std::string ini="[vanguard]\n";
    if(cost)ini+="party_switch_costs_turn=1\n";if(reinforce)ini+="eject_shatter_auto_replace=1\n";
    ini+="[f8_authority]\nvanguard_party_switch_costs_turn=1\nvanguard_eject_shatter_auto_replace=1\n";
    Check(FfxHooks::Config::LoadTextForTests(ini.c_str(),"C:\\private-vanguard-formation.ini"),"formation options are configured independently");
    const bool started=V::Start(base,false,Log);Check(started==(cost||reinforce),"only requested formation behavior starts");
    if(failures)return 1;
    SwitchCases(cost);ReplacementCases(reinforce);V::RequestStop();
    std::printf("VANGUARD_FORMATION %s %u/%u passed\n",mode.c_str(),checks-failures,checks);return failures?1:0;
}
