// Jarvis-HOOK: production action adapters over a private mapped PE. Native HP
// application and queue removal run unchanged. The graphics-heavy aftermath
// body is replaced AFTER its evidenced entry prefix by a controlled endpoint.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/VanguardRuntime.h"
#include "../hooks/SharedDamageRuntime.h"
#include "../hooks/SharedBattleRuntime.h"
#include "../hooks/SharedActionRuntime.h"
#include "../shared/Config.h"
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
namespace V=FfxHooks::Vanguard;
namespace D=FfxHooks::SharedDamage;
namespace B=FfxHooks::SharedBattleRuntime;
static std::uintptr_t base;
static unsigned checks=0,failures=0,aftermathCalls=0,damageCalls=0;
static unsigned wakkaBegins=0,wakkaSettlements=0,wakkaAssists=0;
static LONG CALLBACK ExceptionTrace(EXCEPTION_POINTERS* value){
    if(value&&value->ExceptionRecord&&value->ContextRecord){
        std::printf("ACTION_EXCEPTION code=%08lX ip=%08lX imageRva=%08lX eax=%08lX ecx=%08lX edx=%08lX esp=%08lX\n",
            value->ExceptionRecord->ExceptionCode,value->ContextRecord->Eip,
            static_cast<DWORD>(value->ContextRecord->Eip-base),value->ContextRecord->Eax,
            value->ContextRecord->Ecx,value->ContextRecord->Edx,value->ContextRecord->Esp);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static std::array<unsigned char,31*0xF90> actors{};
static int requestedDamage[31]{};
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void Log(const char* text){
    std::fputs(text,stdout);
    if(std::strstr(text,"owner=4 ")){
        if(std::strstr(text,"event=begin "))++wakkaBegins;
        if(std::strstr(text,"event=settle "))++wakkaSettlements;
        if(std::strstr(text,"event=follow-enqueued "))++wakkaAssists;
    }
}
static void W16(unsigned char* p,unsigned n){p[0]=static_cast<unsigned char>(n);p[1]=static_cast<unsigned char>(n>>8);}
static unsigned Word(const unsigned char* p){return p[0]|(unsigned(p[1])<<8);}
static void W32(unsigned char* p,int n){std::memcpy(p,&n,4);}
static int R32(const unsigned char* p){int n=0;std::memcpy(&n,p,4);return n;}
static unsigned char* Actor(unsigned i){return actors.data()+i*0xF90;}
static int __cdecl Guard(const void*,unsigned*,int*,const void*,int value){return value;}
static unsigned __cdecl Damage(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned){++damageCalls;return 0;}
static int __cdecl Scene(void*){return 0;}
static int __cdecl Aftermath(unsigned source,unsigned sub,unsigned target,int*,void*){
    ++aftermathCalls;auto* actor=Actor(target);auto* group=actor+0x774;
    if(group[2]!=source||group[3]!=sub||group[0]>=group[1])return 0;
    using Apply=int(__cdecl*)(unsigned,unsigned char*,int,int,int,int,int);
    reinterpret_cast<Apply>(base+::FfxHooks::ExecutableProfile::Rva<0x38E2F0>())(target,actor,requestedDamage[target],0,0,0,0);
    actor[0x640]|=group[24+6];W16(actor+0x606,Word(actor+0x606)|Word(group+24+20));++group[0];return 2;
}
// The first 16 native bytes establish this exact frame and push sourceId once.
static __declspec(naked) void AftermathEndpoint(){
    __asm {
        add esp,4
        pop edi
        pop esi
        pop ebx
        mov esp,ebp
        pop ebp
        jmp Aftermath
    }
}
static bool PatchJump(std::uintptr_t address,void* destination){
    unsigned char jump[5]={0xE9};const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination)-address-5);
    std::memcpy(jump+1,&delta,4);DWORD old=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(address),5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(address),jump,5);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),5);
    return VirtualProtect(reinterpret_cast<void*>(address),5,old,&ignored)!=FALSE;
}
static std::vector<unsigned char> Kernel(){
    std::vector<unsigned char> out(20+148*108+1);W16(out.data(),1);W16(out.data()+10,147);W16(out.data()+12,108);W16(out.data()+14,148*108);W32(out.data()+16,20);
    for(const auto& row:V::Abilities){W16(out.data()+20+row.id*108,static_cast<unsigned>(out.size())-(20+148*108));
        for(const char* p=row.label;*p;++p)out.push_back(*p==' '?58:*p=='\''?65:*p=='-'?71:static_cast<unsigned char>(*p+15));out.push_back(0);}
    return out;
}
static unsigned char* NewAction(unsigned count=1){
    B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});
    auto* queue=reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2AC70>());std::memset(queue,0,72*2);
    *reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())=1;queue[0]=0;queue[3]=static_cast<unsigned char>(count);
    for(unsigned i=0;i<count;++i){W16(queue+8+16*i,0x3000+i);W16(queue+10+16*i,255);W32(queue+16+16*i,1<<18);}
    Actor(0)[0xDE5]=0;Actor(0)[0xDE7]=1;W32(Actor(0)+0x5D0,1000);W16(Actor(0)+0x606,0);return queue;
}
static void Hit(unsigned sub,unsigned target,int damage,unsigned grant=0,bool canCrit=true,unsigned mpCost=10,unsigned source=0,unsigned rootCommand=0x3000){
    auto* queue=reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2AC70>());queue[2]=static_cast<unsigned char>(sub);
    std::array<unsigned char,96> command{};command[0x20]=canCrit?4:0;command[0x23]=1;command[0x25]=static_cast<unsigned char>(mpCost);W16(command.data()+0x5A,grant);
    std::array<unsigned char,44> info{};
    const auto compute=reinterpret_cast<D::DamageFn>(base+::FfxHooks::ExecutableProfile::Rva<0x38E680>());
    compute(source,Actor(source),target,Actor(target),command.data(),rootCommand+sub,info.data(),0,0,0,0);
    auto* group=Actor(target)+0x774;std::memset(group,0,68);group[1]=1;group[2]=static_cast<unsigned char>(source);group[3]=static_cast<unsigned char>(sub);
    group[24+6]=static_cast<unsigned char>(grant);
    requestedDamage[target]=damage;
    using Apply=int(__cdecl*)(unsigned,unsigned,unsigned,int*,void*);int out=-1;
    const auto apply=reinterpret_cast<Apply>(base+::FfxHooks::ExecutableProfile::Rva<0x38F0B0>());
    apply(source,sub,target,&out,nullptr);const int after=R32(Actor(target)+0x5D0);
    apply(source,sub,target,&out,nullptr);
    Check(R32(Actor(target)+0x5D0)==after,"duplicate aftermath observation does not apply a second HP loss");
}
static int Complete(unsigned cursor){
    reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2AC70>())[2]=static_cast<unsigned char>(cursor);
    return reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned)>(base+::FfxHooks::ExecutableProfile::Rva<0x3B0870>())(0,0,0);
}
static void BuffCases(){
    NewAction(2);Actor(0)[0x640]=0x14;W32(Actor(18)+0x5D0,1000);W32(Actor(19)+0x5D0,1000);
    Hit(0,18,100);Check(Actor(0)[0x640]==0x14,"first subaction retains both temporary benefits for the remaining hits");
    Hit(1,19,100);Check(Actor(0)[0x640]==0x14,"last hit still benefits before the command queue removes its action");
    Complete(2);Check(Actor(0)[0x640]==0&&R32(Actor(0)+0x5D0)==1000,"used temporary effects expire once without implicitly enabling Vampirism");
    NewAction();Actor(0)[0x640]=0x14;Hit(0,18,0,0,false,10);Complete(1);
    Check(Actor(0)[0x640]==0x10,"a noncritical command consumes MP-0 but leaves unused Auto-Crit intact");
    NewAction();Actor(0)[0x640]=0x14;Hit(0,0,0,0x10,true,10);Complete(1);
    Check(Actor(0)[0x640]==0x10,"Auto-Crit reapplied by an actual consumed result survives the old action settlement");
    NewAction();Actor(0)[0x640]=0x14;Hit(0,0,0,4,true,10);Complete(1);
    Check(Actor(0)[0x640]==4,"a fresh MP-0 instance survives while the old used Auto-Crit is consumed");
    NewAction();Actor(0)[0x640]=0;*reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A90D>())=1;
    Hit(0,18,0);Complete(1);
    Check(*reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A90D>())==1,"consumption never clears the permanent F8 critical producer");
    *reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A90D>())=0;
}
#include "VanguardFollowUpCases.inl"
#include "VanguardThreatenCases.inl"
#include "VanguardEnergyCases.inl"
int main(int argc,char** argv){
    if(argc<2||argc>3)return 2;const bool buffs=argc==3&&std::strcmp(argv[2],"buffs")==0;
    const bool combined=argc==3&&std::strcmp(argv[2],"follow-vampirism")==0;
    const bool follow=combined||(argc==3&&std::strcmp(argv[2],"follow")==0);
    const bool threaten=argc==3&&std::strcmp(argv[2],"threaten")==0;
    const bool energy=argc==3&&std::strcmp(argv[2],"energy")==0;
    const bool shared=argc==3&&std::strcmp(argv[2],"shared-first")==0;
    if(argc==3&&!buffs&&!follow&&!threaten&&!energy&&!shared)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,ExceptionTrace);
    const HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    base=reinterpret_cast<std::uintptr_t>(image);Check(PrivatePeFixture::NormalizeRelocations(image),"private action image relocates");
    auto kernel=Kernel();const auto ap=reinterpret_cast<std::uintptr_t>(actors.data()),kp=reinterpret_cast<std::uintptr_t>(kernel.data());
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),&ap,4);std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A944>()),&kp,4);
    const auto size=static_cast<unsigned short>(kernel.size());std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A970>()),&size,2);
    *reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>())=1;
    // ApplyHpDamage asks the real scene-key reader for scenario/variant metadata.
    // Supply its bounded data, rather than bypassing the native special-boss rules.
    std::array<unsigned char,14> scenario{};unsigned char variant=0;
    const auto sp=reinterpret_cast<std::uintptr_t>(scenario.data()),vp=reinterpret_cast<std::uintptr_t>(&variant);
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A9C8>()),&sp,4);std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A9FC>()),&vp,4);
    W16(reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2C256>()),0);
    for(unsigned i=0;i<31;++i){auto* a=Actor(i);a[0xC]=static_cast<unsigned char>(i);W16(a+0xE,i<18?i:0x1000+i);a[0xDC8]=a[0xDC9]=1;
        a[0xDE5]=a[0x592]=a[0x593]=255;W32(a+0x594,10000);W32(a+0x5D0,1000);}
    auto* gear=reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD30F2C>());std::memset(gear,0,4400);gear[2]=1;gear[11]=4;
    for(unsigned i=0;i<4;++i)W16(gear+14+2*i,255);W16(gear+14,0x808B);Actor(0)[0x592]=0;
    FfxHooks::Config::ResetForTests();
    const char* settings=energy?"[vanguard]\nenergy_boost=1\nenergy_burst=1\nenergy_wall=1\nenergy_barrier=1\n[f8_authority]\nvanguard_energy_boost=1\nvanguard_energy_burst=1\nvanguard_energy_wall=1\nvanguard_energy_barrier=1\n":threaten?"[vanguard]\nthreaten_single_use=1\n[f8_authority]\nvanguard_threaten_single_use=1\n":follow?"[vanguard]\nfollow_up=1\n[f8_authority]\nvanguard_follow_up=1\n":buffs?"[vanguard]\nauto_crit_mp0_turn_end=1\n[f8_authority]\nvanguard_auto_crit_mp0_turn_end=1\n":
        "[vanguard]\nvampirism=1\n[f8_authority]\nvanguard_vampirism=1\n";
    if(combined)settings="[vanguard]\nfollow_up=1\nvampirism=1\n[f8_authority]\nvanguard_follow_up=1\nvanguard_vampirism=1\n";
    Check(FfxHooks::Config::LoadTextForTests(settings,"C:\\private-vampirism.ini"),"the selected action feature configuration is explicit");
    Check(PatchJump(base+::FfxHooks::ExecutableProfile::Rva<0x38F0C0>(),reinterpret_cast<void*>(&AftermathEndpoint)),"graphics-only aftermath endpoint is isolated after the verified native prefix");
    Check(D::Start(base),"shared damage dispatcher is installed once");
    static const D::WorkshopCallbacks host{Guard,Guard,Damage};Check(D::RegisterWorkshop(&host),"controlled formula endpoint is independent of action settlement");
    if(shared)Check(FfxHooks::SharedAction::Start(base),"an independent action owner is installed before Vanguard");
    Check(V::Start(base,false,Log),"Vanguard action adapters start from actual native signatures");
    V::MappingState mapping{};
    Check(V::ReadMapping(mapping)&&mapping.codes[4]==V::MappingCode::Valid,"real-format loaded ability header admits the expected effect");
    W16(kernel.data(),0);Check(!V::ReadMapping(mapping),"loaded-kernel mapping rejects missing native table sections");W16(kernel.data(),1);
    W32(kernel.data()+16,24);Check(!V::ReadMapping(mapping),"loaded-kernel mapping rejects a different native row base");W32(kernel.data()+16,20);
    if(failures)return 1;
    if(energy){EnergyCases(gear,kernel);V::RequestStop();D::UnregisterWorkshop(&host);
        std::printf("VANGUARD_ENERGY_RUNTIME %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    if(threaten){ThreatenCases();V::RequestStop();D::UnregisterWorkshop(&host);
        std::printf("VANGUARD_THREATEN_RUNTIME %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    if(follow){FollowUpCases(gear,kernel,combined);
        if(combined)Check(wakkaAssists==1&&wakkaBegins==3&&wakkaSettlements==2,
            "bounded evidence distinguishes Wakka's settled normal/reaction pair and the next ordinary begin");
        V::RequestStop();D::UnregisterWorkshop(&host);
        std::printf("VANGUARD_FOLLOW_RUNTIME %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    if(buffs){BuffCases();V::RequestStop();D::UnregisterWorkshop(&host);
        std::printf("VANGUARD_BUFF_RUNTIME %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    NewAction(2);Hit(0,18,300);Check(R32(Actor(0)+0x5D0)==1000,"first hit does not settle the whole action early");
    W32(Actor(19)+0x5D0,50);Hit(1,19,5000);Check(R32(Actor(19)+0x5D0)==0,"native HP application clamps overkill");
    Check(Complete(2)==1&&R32(Actor(0)+0x5D0)==1007,"one completed multi-target action restores two percent of actual total loss");
    Check(Complete(2)==0&&R32(Actor(0)+0x5D0)==1007,"repeated native removal cannot settle the same action twice");
    NewAction();W32(Actor(18)+0x5D0,1000);Hit(0,18,500);Complete(0);
    Check(R32(Actor(0)+0x5D0)==1000,"removing an incomplete command is cancellation, not a paid completion");
    NewAction();W32(Actor(1)+0x5D0,1000);Hit(0,1,500);Complete(1);
    Check(R32(Actor(0)+0x5D0)==1000,"damage to an ally cannot be converted into passive healing");
    NewAction();W16(Actor(0)+0x606,2);W32(Actor(18)+0x5D0,1000);Hit(0,18,500);Complete(1);
    Check(R32(Actor(0)+0x5D0)==1000,"Zombie suppresses passive Vampirism without being removed");
    NewAction();W32(Actor(18)+0x5D0,1000);Hit(0,18,500);
    B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});Complete(1);
    Check(R32(Actor(0)+0x5D0)==1000,"battle-generation reset discards an abandoned pending restoration");
    NewAction();W32(Actor(18)+0x5D0,1000);Hit(0,18,500);Actor(0)[0x592]=255;Complete(1);
    Check(R32(Actor(0)+0x5D0)==1000,"unequipping Vampirism before settlement cannot retain its passive effect");
    Actor(0)[0x592]=0;NewAction();W32(Actor(18)+0x5D0,1000);Hit(0,18,500);kernel[20+139*108+0x20]=1;Complete(1);
    Check(R32(Actor(0)+0x5D0)==1000,"a changed invalid kernel row cannot use stale equipped-effect proof at settlement");
    kernel[20+139*108+0x20]=0;
    V::RequestStop();D::UnregisterWorkshop(&host);
    std::printf("VANGUARD_ACTION_RUNTIME %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
