#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "ArcanaFieldFixture.h"
#include "../hooks/ArcanaRuntime.h"
#include "../hooks/ArcanaCombat.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/NativeGameplayEvents.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static std::uintptr_t base=0;
static unsigned char actors[31][0xF90]{},command[96]{},pool[32]{};
static unsigned commandId=0x3000,targetMask=1u<<20,rng=7;
static int hit=100;static bool canDie=true;
static unsigned statusToApply=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
template<class T> static T& At(unsigned id,unsigned offset){return *reinterpret_cast<T*>(actors[id]+offset);}
static bool Patch(unsigned rva,void* target){
    unsigned char branch[5]={0xE9};const auto delta=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(target)-base-rva-5);
    std::memcpy(branch+1,&delta,4);return WorkshopFieldFixture::Write(base+rva,branch,5);
}
static int __cdecl Zero(){return 0;}
static int __cdecl One(){return 1;}
static int __cdecl Die(unsigned){return canDie?1:0;}
static unsigned __cdecl Rng(unsigned){return rng;}
static int __cdecl Wait(unsigned){return 100;}
static void* __cdecl Pool(unsigned){return pool;}
static unsigned __cdecl Mask(unsigned,unsigned,unsigned,unsigned){return targetMask;}
static const unsigned char* __cdecl Resolve(unsigned,unsigned,int,unsigned,unsigned* id){*id=commandId;return command;}
static int __cdecl Aftermath(unsigned,unsigned,unsigned target,int*,void*){
    reinterpret_cast<int(__cdecl*)(unsigned,void*,int,int,int,int,unsigned char)>(base+0x38E2F0)(target,actors[target],hit,0,1,0,0);
    At<unsigned short>(target,0x606)|=static_cast<unsigned short>(statusToApply);
    return 1;
}
static void Emit(FfxHooks::NativeGameplayEvents::Kind kind,unsigned actor=255,std::size_t sequence=0){
    auto ticket=FfxHooks::NativeGameplayEvents::Begin({kind,actor,actor<31?actors[actor]:nullptr,nullptr,sequence});
    FfxHooks::NativeGameplayEvents::End(ticket,true);
}
static void Equip(unsigned first,unsigned second=255){
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=0;
    FfxHooks::NativeSaveEvents::ResetCompleted();
    State state;std::uint64_t generation=0;
    Check(Runtime::Capture(state,generation)&&Runtime::EquipCard(generation,state.revision,0,0,static_cast<short>(first),false)==Error::None,"fixture equips a real acquired card");
    if(second<78){Runtime::Capture(state,generation);Check(Runtime::EquipCard(generation,state.revision,0,1,static_cast<short>(second),false)==Error::None,"fixture equips the second acquired card");}
    std::memset(actors,0,sizeof(actors));std::memset(command,0,sizeof(command));std::memset(pool,0,sizeof(pool));commandId=0x3000;command[40]=1;
    for(unsigned id=0;id<31;++id){At<unsigned short>(id,0xE)=static_cast<unsigned short>(id);At<unsigned>(id,0x594)=1000;At<unsigned>(id,0x598)=100;
        At<int>(id,0x5D0)=500;At<int>(id,0x5D4)=50;At<unsigned char>(id,0x5BD)=100;At<unsigned char>(id,0x5C1)=1;
        At<unsigned char>(id,0xDE9)=1;At<unsigned char>(id,0xDC8)=1;}
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=1;
    Emit(FfxHooks::NativeGameplayEvents::Kind::Battle);canDie=true;statusToApply=0;
}
static int Mp(){return reinterpret_cast<int(__cdecl*)(unsigned,const unsigned char*)>(base+0x38D030)(0,command);}
static int Critical(unsigned& flags){return reinterpret_cast<int(__cdecl*)(void*,void*,const void*,unsigned*,int)>(base+0x389750)(actors[0],actors[20],command,&flags,100);}
static void Action(unsigned owner=0){reinterpret_cast<int(__cdecl*)(unsigned char,unsigned,void*)>(base+0x38DA40)(static_cast<unsigned char>(owner),0,nullptr);}
static unsigned char frameBytes[512]{};
static void* gateway=nullptr;
static float xmmAfter=0;static unsigned ecxAfter=0,capAfter=0;
static const float xmmBefore=1.25f;
__declspec(naked) static void Gateway(void*,void*,void*){
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp,[esp+20]
        mov ebx,[esp+24]
        mov esi,[esp+28]
        mov edi,12345678h
        mov ecx,76543211h
        movss xmm0,xmmBefore
        call dword ptr [gateway]
        movss xmmAfter,xmm0
        mov ecxAfter,ecx
        mov capAfter,ebx
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static int PreCap(int amount){
    auto* frame=frameBytes+256;std::memset(frameBytes,0,sizeof(frameBytes));
    auto put=[&](int offset,unsigned value){std::memcpy(frame+offset,&value,4);};
    put(8,0);put(12,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors[0])));
    put(16,20);put(20,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors[20])));
    put(24,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(command)));put(28,commandId);
    put(-16,static_cast<unsigned>(amount));put(-0xA4,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors[0])));
    Gateway(frame,actors[0],command);int result=0;std::memcpy(&result,frame-16,4);return result;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=2)return 2;
    HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!image||!PrivatePeFixture::NormalizeRelocations(image))return 2;base=reinterpret_cast<std::uintptr_t>(image);
    Runtime::Settings settings;settings.enabled=true;settings.fullDeck=true;
    Check(Runtime::Prime(base,settings,false,nullptr)&&Runtime::Start()&&Combat::Start(base,true,false,nullptr),"exact PE admits runtime and combat hook set");
    ArcanaFieldFixture::FinalStores field;Check(field.Open(base,0,1000,100),"native field final stores supplied");
    *reinterpret_cast<unsigned*>(base+0xD334CC)=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors));
    // External scene, command/RNG and action-pool inputs are supplied; the
    // native MP, critical, CTB, HP, consume, action-loop and AP bodies execute.
    Check(Patch(0x38CF10,reinterpret_cast<void*>(Resolve))&&Patch(0x38D2D0,reinterpret_cast<void*>(Zero))&&Patch(0x398900,reinterpret_cast<void*>(Rng))&&
          Patch(0x390A10,reinterpret_cast<void*>(Wait))&&Patch(0x381780,reinterpret_cast<void*>(Zero))&&Patch(0x38D460,reinterpret_cast<void*>(Die))&&
          Patch(0x393660,reinterpret_cast<void*>(One))&&Patch(0x3B09C0,reinterpret_cast<void*>(Pool))&&Patch(0x394340,reinterpret_cast<void*>(Mask))&&
          Patch(0x38F0B0,reinterpret_cast<void*>(Aftermath))&&Patch(0x38EED0,reinterpret_cast<void*>(Zero)),"bounded external native dependencies supplied");
    // Supply a recovery caller with the exact native return address, and stop
    // the midpoint fixture after native cap selection/Trio logic (before UI).
    unsigned char recovery[]={0xFF,0x74,0x24,0x10,0xFF,0x74,0x24,0x10,0xFF,0x74,0x24,0x10,0xFF,0x74,0x24,0x10,0xE8,0,0,0,0,0x83,0xC4,0x10,0xC3};
    const int relative=0x38D290-0x3B21D5;std::memcpy(recovery+17,&relative,4);
    const unsigned char ret=0xC3;
    Check(WorkshopFieldFixture::Write(base+0x3B21C0,recovery,sizeof(recovery))&&WorkshopFieldFixture::Write(base+0x38ED8F,&ret,1),"native recovery return and pre-cap fixture endpoint supplied");
    gateway=reinterpret_cast<void*>(base+0x38ED1A);
    Equip(1,47);command[24]=1;command[37]=20;
    Check(Mp()==8,"black half-cost and 25 percent reduction compose once with ceiling");
    command[24]=2;Check(Mp()==15,"black-only reduction does not affect white commands");
    At<unsigned short>(0,0x6BC)=0x4000;command[24]=1;Check(Mp()==8,"native Half MP is not halved again");
    At<unsigned short>(0,0x6BC)=0x8000;Check(Mp()==1,"native One MP remains one");
    At<unsigned char>(0,0x640)=4;Check(Mp()==0,"native Spellspring remains free");
    Equip(63);command[32]=4;unsigned flags=0;
    Check(Critical(flags)==200&&(flags&0x100)&&command[39]==0,"card critical chance changes the native RNG boundary without mutating the command");
    rng=50;flags=0;Check(Critical(flags)==100&&flags==0,"native noncritical result survives outside the card chance");
    At<unsigned char>(0,0x640)=0x10;Check(Critical(flags)==200,"native guaranteed critical remains authoritative");
    Equip(15);Check(PreCap(8000)==11200&&capAfter==9999,"damage modifier precedes and preserves native normal cap selection");
    Check(xmmAfter==xmmBefore&&ecxAfter==0x76543211,"midpoint gateway preserves SSE and live ECX");
    At<unsigned short>(0,0x6BE)=0x800;Check(PreCap(8000)==11200&&capAfter==99999,"native Break Damage Limit cap selection remains intact");
    command[40]=6;Check(PreCap(1000)==1000,"fixed damage is not multiplied");command[40]=1;At<unsigned char>(0,0x640)=8;
    Check(PreCap(1000)==9999,"native Trio of 9999 runs after the card modifier");
    Equip(0);
    using Ctb=int(__cdecl*)(void*,int,int,int);const auto recoveryFn=reinterpret_cast<Ctb>(base+0x3B21C0);
    Check(reinterpret_cast<Ctb>(base+0x38D290)(actors[0],1,0,0)==100,"unrecognized CTB callers retain native cost");
    Check(recoveryFn(actors[0],1,0,0)==65&&recoveryFn(actors[0],1,0,0)==100,"Fool's stronger reduction applies only to the first committed recovery");
    Equip(12);Check(recoveryFn(actors[0],1,0,0)==125,"Hanged Man increases real CTB recovery");
    commandId=0x3021;reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x38E5F0)(0);
    Check(At<int>(0,0x5D4)==53,"Defend restores the reduced three percent MP through native consumption");
    reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x38E5F0)(0);Check(At<int>(0,0x5D4)==53,"repeated consumption cannot repeat Defend reward");
    Equip(6);command[24]=2;targetMask=1u<<1;hit=-600;At<int>(0,0x5D0)=200;
    Action();Check(At<int>(1,0x5D0)==1000&&At<int>(0,0x5D0)==300,"Lovers uses actual HP gained and caps sharing at ten percent caster HP");
    At<int>(1,0x5D0)=500;Action();Check(At<int>(0,0x5D0)==300,"multi-target or repeated results share one Lovers action cap");
    Equip(13);targetMask=1u<<20;hit=1000;At<int>(0,0x5D0)=100;Action();
    Check(At<int>(20,0x5D0)==0&&At<int>(0,0x5D0)==300,"Death kill reward follows a real positive native HP transition");
    At<int>(20,0x5D0)=500;Action();Check(At<int>(0,0x5D0)==300,"multi-hit kill rewards are deduplicated per action");
    Equip(20);targetMask=1;hit=1000;Action(20);
    Check(At<int>(0,0x5D0)==251,"Judgement survives lethal native damage and heals twenty-five percent");
    Action(20);Check(At<int>(0,0x5D0)==0,"Judgement is limited to once per battle");
    Equip(20);canDie=false;targetMask=1;hit=1000;Action(20);
    Check(At<int>(0,0x5D0)==500,"native cannot-die rule neither consumes nor triggers Judgement");
    Equip(65);auto ap=reinterpret_cast<int(__cdecl*)(unsigned,void*,int,int)>(base+0x398A10);
    auto& reward=*reinterpret_cast<unsigned*>(base+0x1F10F20);reward=0;ap(0,actors[0],100,1);
    Check(reward==135,"AP boost uses actual native earned AP");
    reward=0;At<unsigned short>(0,0x6BE)=0x20;ap(0,actors[0],100,1);Check(reward==405,"Arcana AP composes once with native Triple AP");
    reward=0;At<unsigned short>(0,0x6BE)=0x40;ap(0,actors[0],100,1);Check(reward==0,"native No AP remains zero");
    Equip(10);Check(Combat::PartyDropMultiplier()==2,"Wheel supplies a two-times drop floor");
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=0;FfxHooks::NativeSaveEvents::ResetCompleted();
    Check(Runtime::BattleGeneration()==0&&Combat::PartyDropMultiplier()==1,"new save session cannot inherit the prior battle's reward admission");
    Equip(9);Emit(FfxHooks::NativeGameplayEvents::Kind::Turn,0,1);
    Check(At<int>(0,0x5D4)==51,"Hermit restores the reduced one percent MP on an admitted turn edge");
    Emit(FfxHooks::NativeGameplayEvents::Kind::Turn,0,1);Check(At<int>(0,0x5D4)==51,"duplicate turn edge cannot restore MP twice");
    At<int>(0,0x5D0)=0;Emit(FfxHooks::NativeGameplayEvents::Kind::Turn,0,2);Check(At<int>(0,0x5D4)==51,"KO actors cannot receive turn restoration");
    Equip(72);Check(Patch(0x38D330,reinterpret_cast<void*>(Zero)),"aggregate fixture supplies max-resource input dependency");
    Emit(FfxHooks::NativeGameplayEvents::Kind::Aggregate,0);Emit(FfxHooks::NativeGameplayEvents::Kind::Aggregate,0);
    Check(At<unsigned char>(0,0x5BC)==15,"opening Overdrive applies once despite repeated equipment aggregation");
    Equip(16);targetMask=1u<<1;hit=0;statusToApply=1u<<6;Action();
    Emit(FfxHooks::NativeGameplayEvents::Kind::Turn,1,1);recoveryFn(actors[1],1,0,0);recoveryFn(actors[1],1,0,0);
    Check((At<unsigned short>(1,0x606)&0x40)!=0,"repeated CTB calculation consumes only one mod-owned status turn");
    Emit(FfxHooks::NativeGameplayEvents::Kind::Turn,1,2);recoveryFn(actors[1],1,0,0);
    Check((At<unsigned short>(1,0x606)&0x40)!=0,"Tower status remains through the second completed turn");
    Emit(FfxHooks::NativeGameplayEvents::Kind::Turn,1,3);recoveryFn(actors[1],1,0,0);
    Check((At<unsigned short>(1,0x606)&0x40)==0,"only the mod-owned Tower status expires after three completed turns");
    Equip(16);targetMask=1u<<1;statusToApply=0x40;hit=0;Action();command[46+6]=100;Action(20);
    for(unsigned turn=1;turn<=3;++turn){Emit(FfxHooks::NativeGameplayEvents::Kind::Turn,1,turn);recoveryFn(actors[1],1,0,0);}
    Check((At<unsigned short>(1,0x606)&0x40)!=0,"native monster reapplication takes ownership of a previously temporary status");
    Equip(21);command[24]=1;command[37]=99;
    Check(Mp()==99,"World retains the real native battle MP cost");
    At<unsigned short>(0,0x6BC)=0x8000;Check(Mp()==1,"World preserves the native equipment's One MP");
    command[24]=4;Check(PreCap(1000)==1500,"World boosts real native Overdrive HP damage by fifty percent");
    command[24]=1;Check(PreCap(1000)==1000,"World leaves ordinary damage unchanged");
    command[24]=4;command[40]=6;Check(PreCap(1000)==1000,"World does not multiply fixed Overdrive damage");command[40]=1;
    Check(recoveryFn(actors[0],1,0,0)==85&&recoveryFn(actors[0],1,0,0)==85,"World's recovery benefit persists after the first action");
    Equip(13);At<unsigned char>(20,0x641)=255;
    Check(PreCap(1000)==1200,"Death's new damage bonus consumes the target's real native Death immunity");
    At<unsigned char>(20,0x641)=254;Check(PreCap(1000)==1000,"high Death resistance is not misclassified as immunity");
    Equip(71);reward=0;ap(0,actors[0],100,1);Check(reward==175,"Eight of Pentacles grants its training bonus through the native AP consumer");
    Combat::Stop();command[24]=1;command[37]=20;
    Check(Mp()==20&&Combat::PartyDropMultiplier()==1,"logical stop leaves native MP and reward behavior");
    Runtime::Stop();std::printf("ArcanaCombatRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
