// Jarvis-HOOK: isolated production observers with actual native HP, property,
// queue append and queue retirement. Only graphics-heavy result dispatch is a fixture.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/InsideSinRuntime.h"
#include "../hooks/SharedActionRuntime.h"
#include "../hooks/SharedActorRuntime.h"
#include "../hooks/SharedCombatRuntime.h"
#include "../hooks/CombatExtensionBus.h"
#include "../hooks/SharedBattleRuntime.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace S=FfxHooks::InsideSin;
namespace R=S::Runtime;
namespace A=FfxHooks::SharedAction;
namespace C=FfxHooks::SharedCombat;
namespace B=FfxHooks::CombatExtensions;
namespace P=FfxHooks::ExecutableProfile;
namespace FfxHooks::Config {bool GetBool(const char*,bool fallback){return fallback;}}
namespace FfxHooks::EquipmentWorkshop {bool CombatProducerReady(){return true;}}
static std::uintptr_t base;
static DWORD owner;
static unsigned checks=0,failures=0,originalCalls=0,propertyCalls=0;
static FfxHooks::SinAi::Context context{};
static std::array<unsigned char,31*0xF90> actors{};
static std::array<unsigned char,64> resource{};
static std::array<int,2> requested{};static unsigned hitCount=1;
static S::Bindings bindings{};
static S::CommandRow nativeDispel{};
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void Log(const char* text){std::fputs(text,stdout);}
static bool Context(bool,FfxHooks::SinAi::Context* out){if(GetCurrentThreadId()!=owner)return false;*out=context;return true;}
static unsigned char* Actor(unsigned id){return actors.data()+id*0xF90u;}
static void W16(void* p,unsigned n){const auto value=static_cast<std::uint16_t>(n);std::memcpy(p,&value,2);}
static void W32(void* p,unsigned n){std::memcpy(p,&n,4);}
static unsigned U32(const void* p){unsigned n=0;std::memcpy(&n,p,4);return n;}
static std::vector<unsigned char> Read(const char* path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static bool Patch(std::uintptr_t address,const void* source,unsigned count){DWORD old=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(address),count,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(address),source,count);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),count);
    return VirtualProtect(reinterpret_cast<void*>(address),count,old,&ignored)!=FALSE;}
static int __cdecl Result(unsigned,unsigned,unsigned target,int*,void*){
    ++originalCalls;
    for(unsigned i=0;i<hitCount;++i)C::Hp(target,Actor(target),requested[i],0,0,0,0);
    if(!U32(Actor(target)+0x5D0))W16(Actor(target)+0x606,1);
    return 2;
}
static int __cdecl Reinitialize(unsigned,unsigned target){
    // Model and resource identity deliberately stay the same. Native actor
    // initialization still denotes a new incarnation, not the previous latch.
    std::memset(Actor(target)+0x540,0,40);Actor(target)[0x540]=0x60;
    std::memset(Actor(target)+0x606,0,16);W32(Actor(target)+0x5D0,400);
    return 1;
}
static int __cdecl UnusedMp(unsigned,const unsigned char*){return 0;}
static int __cdecl UnusedCritical(const unsigned char*,const unsigned char*,const unsigned char*,unsigned*,int value){return value;}
static int __cdecl StopAfterNativeHp(unsigned target,unsigned char* actor,int amount,int display,int code,int flags,int other){
    const int result=C::OriginalHp()(target,actor,amount,display,code,flags,other);
    R::RequestStop();return result;
}
static unsigned char* Queue(unsigned actor,unsigned command=0x6000,unsigned reaction=0){
    auto* row=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());std::memset(row,0,72*2);
    *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=1;
    row[0]=static_cast<unsigned char>(actor);row[1]=static_cast<unsigned char>(reaction);row[3]=1;
    W16(row+8,command);W16(row+10,255);W32(row+16,1u<<(actor>=20?0:20));
    Actor(actor)[0xDE5]=0;Actor(actor)[0xDE7]=1;return row;
}
static int Finish(unsigned actor){auto* row=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());row[2]=row[3];return A::FinishShim(actor,0,0);}
static int Count(){return *reinterpret_cast<std::int8_t*>(base+P::Rva<0xD2BDE1>());}
static void Damage(unsigned source,unsigned target,int amount,int second=0){requested={amount,second};hitCount=second?2u:1u;int out=0;A::ResultShim(source,0,target,&out,nullptr);}
static void Battle(S::Preset preset,unsigned field=1073,unsigned model=220){
    ++context.generation;context.field=static_cast<std::uint16_t>(field);context.formation=0;context.enabled=true;
    context.distribution=FfxHooks::SinSpread::Distribution::All;context.seed=0;
    for(;context.seed<10000;++context.seed)if(S::ChooseForActor(context.seed,context.visit,context.generation,model,20)==preset)break;
    Check(context.seed<10000,"seed resolves the requested preset without changing its weight");
    std::memset(actors.data(),0,actors.size());
    for(unsigned id:{0u,1u,2u,20u}){auto* actor=Actor(id);actor[0xC]=static_cast<unsigned char>(id);W16(actor+0xE,id<18?id:0x1000+model);
        actor[0x540]=0x60;actor[0x541]=0;W32(actor+0x48,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(resource.data())));actor[0xDC8]=actor[0xDC9]=1;
        actor[0xDE5]=actor[0x592]=actor[0x593]=255;W32(actor+0x594,1000);W32(actor+0x5D0,1000);}
    R::ResetForTests();
}
static LONG CALLBACK Error(EXCEPTION_POINTERS* p){std::printf("INSIDE_EXCEPTION code=%08lX ip=%08lX rva=%08lX\n",
    p->ExceptionRecord->ExceptionCode,p->ContextRecord->Eip,static_cast<DWORD>(p->ContextRecord->Eip-base));return EXCEPTION_CONTINUE_SEARCH;}
static unsigned retirementSeen=0;static int retirementBefore=0;
static bool fillAfterRetirement=false;
static int __cdecl RejectRetirement(unsigned,unsigned,unsigned){return 0;}
static void* BeforeWitness(const A::FinishCall&) noexcept {retirementBefore=Count();return &retirementBefore;}
static void AfterWitness(void*,const A::FinishCall&,int result,bool complete) noexcept {if(complete&&result==1){
    ++retirementSeen;Check(Count()==retirementBefore-1,"all retirement observers see the native queue delta before SIN appends");
    if(fillAfterRetirement)*reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=62;
}}
static const A::Observer witness{nullptr,nullptr,BeforeWitness,AfterWitness};
static void NativeStatusCases(){
    using Status=int(__cdecl*)(unsigned,unsigned char*,unsigned,unsigned char*,const unsigned char*,void*,void*,unsigned char*,int,unsigned*,unsigned*);
    const auto native=reinterpret_cast<Status>(base+0x38AE00);
    auto evaluate=[&](const S::CommandRow& row,std::array<unsigned char,44>& result){
        std::array<unsigned,64> counters{},auxiliary{};unsigned flags=0,changed=0;
        (void)native(0,Actor(0),20,Actor(20),row.data(),counters.data(),auxiliary.data(),result.data(),0,&flags,&changed);
    };
    for(unsigned duration:{254u,255u}){
        std::array<unsigned char,44> result{};
        for(unsigned i=0;i<5;++i){Actor(20)[S::BuffOffsets[i]]=static_cast<unsigned char>(duration);result[7+S::DurationIndices[i]]=static_cast<unsigned char>(duration);}
        evaluate(nativeDispel,result);
        for(unsigned i=0;i<5;++i)Check(result[7+S::DurationIndices[i]]==(duration==254?0:255),"actual native Dispel removes all five temporary effects and preserves permanent control");
    }
    std::memset(Actor(20)+0x608,0,13);
    auto defense=bindings.GetBinding(S::Preset::Guardian)->row;
    Actor(20)[0x60B]=19;std::array<unsigned char,44> result{};result[10]=19;
    S::FilterExistingBuffs(defense,S::Shell);evaluate(defense,result);
    Check(result[10]==19&&result[11]==254&&result[17]==254,"native status calculation preserves old Shell duration while granting only Protect/Regen");
    const auto offense=bindings.GetBinding(S::Preset::Tormentor)->row;
    result={};Actor(20)[0x644]=255;Actor(20)[0x64E]=255;evaluate(offense,result);
    Check(!(result[20]&8)&&!result[8],"native Poison/Silence Proof resistance blocks the normal 75/50 percent attacks");
}
int main(int argc,char** argv){
    if(argc!=5)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);owner=GetCurrentThreadId();AddVectoredExceptionHandler(1,Error);
    const auto module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!module)return 2;
    base=reinterpret_cast<std::uintptr_t>(module);Check(PrivatePeFixture::NormalizeRelocations(module),"exact private Steam image relocates");
    auto kernel=Read(argv[2]);auto manifest=Read(argv[3]);std::string json(manifest.begin(),manifest.end());
    const auto flag=json.find("\"enabled\": false");if(flag!=std::string::npos)json.replace(flag,16,"\"enabled\": true");
    bool enabled=false;Check(S::ParseBindings(json,bindings,enabled)==S::BindingCode::Ready&&enabled,"all seven actual Editor commands satisfy the supplied contract");
    const auto dispel=Read(argv[4]);Check(dispel.size()==96,"actual native Dispel row is a character command");
    if(dispel.size()==96)std::memcpy(nativeDispel.data(),dispel.data(),nativeDispel.size());
    if(failures)return 1;
    W32(reinterpret_cast<void*>(base+P::Rva<0xD334CC>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors.data())));
    W32(reinterpret_cast<void*>(base+P::Rva<0xD2A934>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(kernel.data())));
    std::array<unsigned char,14> scenario{};unsigned char variant=0;
    W32(reinterpret_cast<void*>(base+P::Rva<0xD2A9C8>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(scenario.data())));
    W32(reinterpret_cast<void*>(base+P::Rva<0xD2A9FC>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&variant)));
    W16(reinterpret_cast<void*>(base+P::Rva<0xD2C256>()),0);
    // DONT_RESOLVE keeps imports inert. The native property setter's strcmp
    // import is a pure comparison against the private zeroed scene label.
    const auto compare=reinterpret_cast<std::uintptr_t>(&std::strcmp);
    Check(Patch(base+0x70C378,&compare,4),"private property import is resolved without launching the game");
    A::originals[0]=reinterpret_cast<void*>(&Result);
    A::originals[1]=reinterpret_cast<void*>(base+P::Rva<0x3B0870>());
    FfxHooks::SharedActor::original=reinterpret_cast<void*>(&Reinitialize);
    C::originals[2]=reinterpret_cast<void*>(base+P::Rva<0x38E2F0>());
    context={true,1073,1,1,FfxHooks::SinSpread::Distribution::All,1,0,0};
    Check(R::PrepareForTests(base,bindings,Context,Log),"production observers admit current native signatures");
    Check(A::Subscribe(A::Slot::Reserved,&witness),"independent retirement witness shares the producer");
    Battle(S::Preset::Devastator);NativeStatusCases();
    for(const auto& d:S::Definitions){
        Battle(d.preset);Queue(20);Damage(20,0,1);Check(Finish(20)==1,"real native queue retirement completes once");
        const auto* row=reinterpret_cast<const unsigned char*>(base+P::Rva<0xD2AC70>());
        Check(Count()==1&&row[0]==20&&row[1]==1&&row[3]==1&&FfxHooks::SinAi::U16(row+8)==bindings.GetBinding(d.preset)->id,"exactly one configured extra is appended as a nonrecursive reaction");
        Actor(20)[0xDE5]=0;Damage(20,0,0);Finish(20);Check(Count()==0,"SIN extra cannot spawn another SIN action");
    }
    Battle(S::Preset::Devastator);Queue(20,0x61B9);Damage(20,0,1);Finish(20);Check(Count()==0,"Oblivion Cannon cannot append SIN");
    for(unsigned od:{0x61BAu,0x61BBu,0x61DEu,0x61DFu,0x61E0u}){Queue(20,od);Damage(20,0,1);Finish(20);Check(Count()==0,"native OD command is excluded, including Omega species outside Omega");}
    // All four native command banks share the gameplay prefix. An ordinary
    // monmagic1/character/item command is eligible; an OD in any bank is not.
    std::array<unsigned char,20+96> otherBank{};
    W16(otherBank.data(),1);W32(otherBank.data()+16,20);
    struct Bank {unsigned family,root,width;};
    const Bank banks[]={{2,P::Rva<0xD2A940>(),96},{3,P::Rva<0xD2A92C>(),96},{4,P::Rva<0xD2A930>(),92}};
    for(const auto& bank:banks){
        W16(otherBank.data()+12,bank.width);W16(otherBank.data()+14,bank.width);
        W32(reinterpret_cast<void*>(base+bank.root),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(otherBank.data())));
        otherBank[20+0x26]=0;
        Queue(20,bank.family<<12);Damage(20,0,1);Finish(20);Check(Count()==1,"ordinary action from each native bank can earn its one extra");
        otherBank[20+0x26]=100;
        Queue(20,bank.family<<12);Damage(20,0,1);Finish(20);Check(Count()==0,"native OD metadata is excluded in every command bank");
        W32(reinterpret_cast<void*>(base+bank.root),0);
        Queue(20,bank.family<<12);Damage(20,0,1);Finish(20);Check(Count()==0,"an unreadable command bank cannot authorize an extra");
    }
    Queue(20,0x6000,1);Damage(20,0,1);Finish(20);Check(Count()==0,"a native counter or reaction cannot schedule an extra");
    auto* packet=Queue(20);packet[3]=2;W16(packet+24,0x6000);W16(packet+26,255);
    Damage(20,0,1);Finish(20);Check(Count()==1,"a multi-command native packet earns only one extra after retirement");
    Queue(20);Damage(20,0,1);fillAfterRetirement=true;Finish(20);fillAfterRetirement=false;
    Check(Count()==62,"another consumer filling the retired slot cannot make SIN exceed native queue capacity");
    Queue(20);Damage(20,0,1);const auto originalFinish=A::originals[1];A::originals[1]=reinterpret_cast<void*>(&RejectRetirement);
    Check(Finish(20)==0&&Count()==1,"failed native retirement preserves its record without appending an extra");A::originals[1]=originalFinish;
    for(unsigned outside:{583u,586u,515u,600u}){Battle(S::Preset::Devastator,outside);Queue(20);Damage(20,0,1);Finish(20);Check(Count()==0,"boss/story/other region preserves native actions");}
    Battle(S::Preset::Devastator,1075,47);Queue(20);Damage(20,0,1);Finish(20);Check(Count()==1,"final contract includes unchanged sins05 formations");
    Battle(S::Preset::Guardian);Actor(20)[0x60B]=12;Actor(20)[0x60C]=13;Actor(20)[0x612]=14;
    Queue(20);Damage(20,0,1);Finish(20);Check(Count()==0,"saturated defensive preset skips without rerolling an attack");
    Battle(S::Preset::Guardian);Actor(20)[0x60B]=12;
    auto row=bindings.GetBinding(S::Preset::Guardian)->row;B::DamageScope scope;
    B::DamageCall call{20,Actor(20),20,Actor(20),row.data(),bindings.GetBinding(S::Preset::Guardian)->id};
    Check(B::EnterDamage(scope,call),"native damage extension accepts defensive command");
    const auto* shadow=static_cast<const unsigned char*>(scope.forwardCommand);
    Check(shadow!=row.data()&&shadow[0x3D]==0&&shadow[0x3E]==255&&Actor(20)[0x60B]==12,"only absent defensive effects reach native status computation");
    B::LeaveDamage(scope,0,true);
    Battle(S::Preset::Ascendant);Queue(0);Damage(0,20,500);Check(!Actor(20)[0x613],"Ascendant does not fire in the middle of a received action");Finish(0);
    Check(Actor(20)[0x613]==254&&Actor(20)[0x60B]==254,"native property setter grants removable Haste/Shell at half HP");
    Actor(20)[0x613]=Actor(20)[0x60B]=0;Queue(0);Damage(0,20,1);Finish(0);
    Check(!Actor(20)[0x613]&&!Actor(20)[0x60B],"Dispel cannot rearm the consumed Ascendant condition");
    Check(FfxHooks::SharedActor::InitializeShim(0,20)==1,"shared actor initialization preserves its original result");
    Queue(0);Damage(0,20,0);Finish(0);
    Check(Actor(20)[0x613]==254&&Actor(20)[0x60B]==254,"same-address same-model native reincarnation receives an independent T7 latch");
    Battle(S::Preset::Ascendant);W32(Actor(20)+0x5D0,400);Queue(0);Damage(0,20,0);Finish(0);
    Check(Actor(20)[0x613]==254&&Actor(20)[0x60B]==254,"a received status-only action also checks Ascendant's current HP at completion");
    Battle(S::Preset::Sentinel);Queue(0);Damage(0,20,150,150);
    Check(U32(Actor(20)+0x5D0)==700&&!Actor(20)[0x613],"two small hits totaling thirty percent do not trigger Sentinel");
    Damage(0,20,250);Check(U32(Actor(20)+0x5D0)==650&&Actor(20)[0x613]==254,"one quarter hit invokes native twenty-percent recovery and Haste");
    Damage(0,20,250);Check(U32(Actor(20)+0x5D0)==400,"Sentinel recovery does not repeat");
    Battle(S::Preset::Sentinel);Queue(0);Damage(0,20,300,750);
    Check(U32(Actor(20)+0x5D0)==150,"Sentinel heals after the qualifying individual hit before a later hit can kill it");
    Battle(S::Preset::Sentinel);W16(Actor(20)+0x606,2);Queue(0);Damage(0,20,250);
    Check(U32(Actor(20)+0x5D0)==550,"Zombie converts native recovery to damage");
    Battle(S::Preset::Sentinel);W16(Actor(20)+0x606,2);W32(Actor(20)+0x5D0,400);Queue(0);Damage(0,20,250);
    Check(!U32(Actor(20)+0x5D0)&&(Actor(20)[0x606]&1)&&!Actor(20)[0x613],"lethal Zombie recovery follows native KO settlement without buffing a corpse");
    Battle(S::Preset::Sentinel);Queue(0);Damage(0,20,1000);Check(!U32(Actor(20)+0x5D0)&&!Actor(20)[0x613],"lethal original hit cannot recover a dead Sentinel");
    Battle(S::Preset::Devastator,1074,182);context.enabled=false;
    std::memcpy(Actor(21),Actor(20),0xF90);Actor(21)[0xC]=21;W16(Actor(21)+0xE,0x1000+184);
    std::memcpy(Actor(22),Actor(20),0xF90);Actor(22)[0xC]=22;W16(Actor(22)+0xE,0x1000+216);
    Queue(0);Damage(0,22,1000);Check(!Actor(20)[0x613]&&!Actor(21)[0x613],"another ally's death cannot trigger Gemini trait");
    Damage(0,21,1000);Check(Actor(20)[0x613]==254,"actual partner death grants Haste even with SIN disabled and other allies present");
    Actor(20)[0x613]=0;Damage(0,21,1);Check(!Actor(20)[0x613],"Gemini trait remains consumed after Dispel");
    Queue(20);Damage(20,0,1);Finish(20);Check(Count()==0,"disabled SIN cannot schedule extras while regional trait remains available");
    Battle(S::Preset::Devastator);Queue(20);Damage(20,0,1);
    std::thread foreign([]{Finish(20);});foreign.join();Check(Count()==0,"foreign thread cannot add a SIN extra");
    Battle(S::Preset::Sentinel);Queue(0);
    const C::Callbacks stopInFlight{UnusedMp,UnusedCritical,StopAfterNativeHp};
    Check(C::Register(C::vanguard,&stopInFlight),"the fixture closes admission between native HP application and the observer return");
    Damage(0,20,250);
    Check(U32(Actor(20)+0x5D0)==750&&!Actor(20)[0x613],"an in-flight HP callback cannot heal or buff after admission closes");
    C::Unregister(C::vanguard,&stopInFlight);R::Stop();
    Check(R::PrepareForTests(base,bindings,Context,Log),"the fixture prepares after testing interrupted teardown");
    Battle(S::Preset::Devastator);Queue(20);Damage(20,0,1);
    Check(Actor(20)[0x541]!=0,"an admitted infected instance receives its bounded preset label");
    const auto offset=U32(kernel.data()+16)+(0x61B3u&0xFFFu)*92u+0x2A;
    const auto originalPower=kernel[offset];kernel[offset]^=1;
    Finish(20);Check(Count()==0&&Actor(20)[0x540]==0x60&&!Actor(20)[0x541],"gameplay drift rejects the whole pool and restores its owned name");
    kernel[offset]=originalPower;Queue(20);Damage(20,0,1);Finish(20);
    Check(Count()==0,"restoring command bytes cannot silently rearm a partially consumed battle");
    R::Stop();Check(R::PrepareForTests(base,bindings,Context,Log),"a new explicitly admitted process fixture can restart");
    Battle(S::Preset::Devastator);Queue(20);Damage(20,0,1);Actor(20)[0x540]=0x61;
    std::array<unsigned char,40> foreignName{};std::memcpy(foreignName.data(),Actor(20)+0x540,foreignName.size());
    R::Stop();Check(!std::memcmp(foreignName.data(),Actor(20)+0x540,foreignName.size()),"teardown preserves another owner's changed name");
    Battle(S::Preset::Devastator);Queue(20);Damage(20,0,1);Finish(20);Check(Count()==0,"stopped descriptors are inert");
    A::Unsubscribe(A::Slot::Reserved,&witness);
    Check(retirementSeen>10&&originalCalls>20,"shared observers and originals executed throughout the fixture");
    std::printf("Inside Sin native: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
