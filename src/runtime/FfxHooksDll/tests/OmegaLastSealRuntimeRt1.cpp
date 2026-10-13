#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/OmegaLastSealRuntime.h"
#include "../hooks/OmegaLastSealDamage.h"
#include "../hooks/OmegaLastSealNative.h"
#include "../hooks/OmegaLastSealEvidence.h"
#include "../hooks/OmegaLastSealBridge.h"
#include "../hooks/F7DifficultyCore.h"
#include "../hooks/SharedQueueRuntime.h"
#include "../hooks/SharedActionRuntime.h"
#include "../hooks/CombatExtensionBus.h"
#include "../hooks/BattleEndHook.h"
#include "../hooks/ArcanaRuntime.h"
#include "../hooks/ArcanaFieldDialogCore.h"
#include "../hooks/TextLanguageHook.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <string>
namespace O=FfxHooks::OmegaLastSeal;namespace R=O::Runtime;namespace N=O::Native::Common;
namespace Q=FfxHooks::SharedQueue;namespace A=FfxHooks::SharedAction;namespace C=FfxHooks::SharedCombat;namespace B=FfxHooks::CombatExtensions;
static std::uintptr_t base=0;static unsigned checks=0,failures=0,grants=0;static const char* phase="start";
static R::Context context{1,false};static std::vector<unsigned char> actors(31*0xF90),parts(62*0x390),loot(31*0x118),dynamic(31*0x23C),models(5*0x1000);
static std::array<std::vector<unsigned char>,5> monsters{};static int damageAmount=1;static bool duplicateHp=false;
static unsigned char* Actor(unsigned id){return actors.data()+id*0xF90;}
static void Put(void* p,unsigned value,unsigned width=4){std::memcpy(p,&value,width);}
static unsigned Read(const void* p){unsigned value=0;std::memcpy(&value,p,4);return value;}
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
static auto File(const std::string& p){std::ifstream f(p,std::ios::binary);return std::vector<unsigned char>{std::istreambuf_iterator<char>(f),{}};}
static void Patch(unsigned rva,const void* data,unsigned size){DWORD old=0,unused=0;VirtualProtect(reinterpret_cast<void*>(base+rva),size,PAGE_EXECUTE_READWRITE,&old);std::memcpy(reinterpret_cast<void*>(base+rva),data,size);VirtualProtect(reinterpret_cast<void*>(base+rva),size,old,&unused);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+rva),size);}
static void Stub(unsigned rva,const void* target){unsigned char bytes[5]={0xE9};Put(bytes+1,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(target)-(base+rva+5)));Patch(rva,bytes,5);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* e){std::printf("LAST_SEAL_FAULT phase=%s code=%08lX rva=%08lX address=%08lX\n",phase,e->ExceptionRecord->ExceptionCode,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-base),e->ExceptionRecord->NumberParameters>1?static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]):0);return EXCEPTION_CONTINUE_SEARCH;}
namespace FfxHooks::Config {bool GetBool(const char*,bool value){return value;}const char* GetString(const char*,const char* value){return value;}}
namespace FfxHooks::EquipmentWorkshop {bool CombatProducerReady(){return true;}}
namespace FfxHooks::TextLanguage::Native {bool EncodeFieldLiteral(const char*,unsigned char*,std::size_t,std::size_t&) noexcept{return false;}Snapshot Inspect() noexcept{return {};}}
namespace FfxHooks::Arcana::FieldDialog {bool Encode(const char*,EncodedText&) noexcept{return false;}}
namespace FfxHooks::Arcana::Runtime {
std::uint64_t SaveSession() noexcept{return 7;}
std::uint64_t BattleGeneration() noexcept{return context.battle;}
Error GrantVictory(const VictoryReceipt& receipt) noexcept {if(VictoryCard(receipt)!=kOmegaWeaponCard)return Error::InvalidState;++grants;return Error::None;}
}
static bool Context(R::Context& out) noexcept {out=context;return true;}
static void Log(const char* text){std::fputs(text,stdout);}
static void __cdecl Render(void*,float,float,float){}
static int __cdecl Event(unsigned actor,unsigned event,unsigned,unsigned,unsigned,unsigned caller,unsigned* wait){Check(actor>=21&&actor<=24&&event==0x27&&caller==0,"rebirth dispatches the real native event for a reserved helper");if(wait)*wait=0;return 0;}
static unsigned __cdecl Targets(unsigned,unsigned,int,int){return 7;}
using Revive=int(__cdecl*)(unsigned,unsigned,int);static Revive realRevive=nullptr;
static int __cdecl HeadlessRevive(unsigned vm,unsigned mask,int pose){
    std::array<unsigned,5> prior{};for(unsigned i=0;i<5;++i){prior[i]=Read(Actor(20+i));Put(Actor(20+i),0);}
    const int result=realRevive(vm,mask,pose);for(unsigned i=0;i<5;++i)Put(Actor(20+i),prior[i]);return result;
}
static int __cdecl Result(unsigned,unsigned,unsigned target,int*,void*){
    C::Hp(target,Actor(target),damageAmount,0,0,0,0);if(duplicateHp)C::Hp(target,Actor(target),damageAmount,0,0,0,0);return 1;
}
static int __cdecl UnusedMp(unsigned,const unsigned char*){return 0;}
static int __cdecl UnusedCritical(const unsigned char*,const unsigned char*,const unsigned char*,unsigned*,int amount){return amount;}
static int __cdecl ArcanaTransform(unsigned target,unsigned char* actor,int amount,int display,int code,int flags,int other){return C::AfterArcanaHp(target,actor,amount>0?amount*2:amount,display,code,flags,other);}
static int __cdecl VanguardTransform(unsigned target,unsigned char* actor,int amount,int display,int code,int flags,int other){return C::OriginalHp()(target,actor,amount>0?amount*3:amount,display,code,flags,other);}
static unsigned char* Queue(unsigned source,unsigned command,unsigned reaction=0){
    const auto index=static_cast<unsigned>(*reinterpret_cast<std::int8_t*>(base+0xD2BDE1));std::array<unsigned char,72> row{};
    row[0]=static_cast<unsigned char>(source);row[1]=static_cast<unsigned char>(reaction);row[3]=1;Put(row.data()+8,command,2);Put(row.data()+10,255,2);Put(row.data()+16,1);
    Check(Q::Shim(source,row.data(),0,0,reaction)==-1,"native append succeeds exactly once");Actor(source)[0xDE5]=static_cast<unsigned char>(index);
    return reinterpret_cast<unsigned char*>(base+0xD2AC70+72*index);
}
static void Finish(unsigned source,bool complete=true,unsigned preserve=0){
    const unsigned index=Actor(source)[0xDE5];auto* row=reinterpret_cast<unsigned char*>(base+0xD2AC70+72*index);if(complete)row[2]=row[3];
    (void)A::FinishShim(source,index,preserve);
}
static void Hit(unsigned source,unsigned target,unsigned command,int amount,bool twice=false){
    Queue(source,command);damageAmount=amount;duplicateHp=twice;A::ResultShim(source,0,target,nullptr,nullptr);duplicateHp=false;Finish(source);
}
static void BreakCase(bool exposed){
    const auto before=N::ReadOr<std::uint16_t>(reinterpret_cast<std::uintptr_t>(Actor(20))+0x647);
    O::CommandRow row{};row[0x23]=4;row[0x34]=row[0x35]=255;
    std::array<unsigned char,44> info{};std::memcpy(info.data()+7,Actor(20)+0x608,13);std::memcpy(info.data()+20,Actor(20)+0x606,2);std::memcpy(info.data()+22,Actor(20)+0x616,2);
    std::array<unsigned,64> counters{},aux{};unsigned flags=0,changed=0;B::DamageScope scope;
    B::EnterDamage(scope,{0,Actor(0),20,Actor(20),row.data(),0x3000,info.data(),0,0,0,0});
    using Status=void(__cdecl*)(unsigned,unsigned char*,unsigned,unsigned char*,const unsigned char*,unsigned*,unsigned*,unsigned char*,unsigned,unsigned*,unsigned*);
    reinterpret_cast<Status>(base+0x38AE00)(0,Actor(0),20,Actor(20),static_cast<const unsigned char*>(scope.forwardCommand),counters.data(),aux.data(),info.data(),0,&flags,&changed);
    Check((N::Word(info.data()+20)&0xC0u)==(exposed?0xC0u:0u),exposed?"native Armor/Mental Break can apply during exposure":"guaranteed native breaks cannot bypass the closed exposure window");
    B::LeaveDamage(scope,0,true);
    Check(N::ReadOr<std::uint16_t>(reinterpret_cast<std::uintptr_t>(Actor(20))+0x647)==before&&row[0x34]==255&&row[0x35]==255,"status calculation restores the original resistance and command bytes");
}
static void Reset(){
    B::Reset(B::ResetReason::BattleStart);++context.battle;
    *reinterpret_cast<std::int8_t*>(base+0xD2BDE1)=0;std::memset(reinterpret_cast<void*>(base+0xD2AC70),0,72*62);Put(reinterpret_cast<void*>(base+0xD3338C),0);
    for(unsigned id=0;id<31;++id){Actor(id)[0xDE4]=0;Actor(id)[0xDE5]=255;}
    for(unsigned role=0;role<5;++role){const unsigned id=20+role;const auto hp=role==0?3000000u:role<3?150000u:75000u;
        N::Property(base,id,0,hp);N::Property(base,id,4,0);N::Property(base,id,0x15,1);}
    const FfxHooks::F7Difficulty::PopulateIo population{nullptr,[](void*){return 0;},[](void*){return true;},
        [](void*,std::uint8_t* scenePhase){*scenePhase=0x13;return true;},[](void*){O::Bridge::Populated();}};
    Check(FfxHooks::F7Difficulty::RunPopulatePostOriginal(population)==0&&R::SnapshotForTests().active&&!R::SnapshotForTests().gauge&&
          !Actor(23)[0xDC8]&&!Actor(24)[0xDC8]&&!*reinterpret_cast<std::int8_t*>(base+0xD2BDE1),
          "post-population admission hides both Echoes before any action, target selection or damage callback");
    Queue(20,O::Attack);Finish(20);
}
int main(int argc,char** argv){
    if(argc!=4)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);AddVectoredExceptionHandler(1,Fault);
    const auto pe=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!pe)return 2;base=reinterpret_cast<std::uintptr_t>(pe);
    Check(PrivatePeFixture::NormalizeRelocations(pe),"private exact Steam PE is relocated");
    Check(FfxHooks::NativeUiSupport::Profile(base,O::Evidence::spans),"all nine native witnesses match the relocated executable");
    const std::string root=argv[2];auto formation=File(root+"/omeg01_10.bin"),magic=File(root+"/monmagic1.bin"),magic2=File(root+"/monmagic2.bin"),commands=File(root+"/command.bin"),ctb=File(root+"/ctb_base.bin");
    std::array<unsigned char,64> scenario{};unsigned variant=0;
    for(unsigned id=0;id<31;++id){auto* p=Actor(id);p[0x13]=p[0x43F]=p[0xDE5]=255;for(unsigned n=0x16;n<=0x19;++n)p[n]=1;Put(p+0xC,id,2);}
    const auto address=[](const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));};
    for(const auto& entry:std::array<std::pair<unsigned,const void*>,10>{{{0xD334D4,actors.data()},{0xD34468,Actor(20)},{0xD3446C,parts.data()},{0xD36068,loot.data()},{0x1F10CD8,dynamic.data()},{0xD2A9C8,scenario.data()},{0xD2A9FC,&variant},{0xD2A94C,ctb.data()},{0xD2A92C,commands.data()},{0xD2A930,magic.data()}}})Put(reinterpret_cast<void*>(base+entry.first),address(entry.second));
    Put(reinterpret_cast<void*>(base+0xD2A934),address(magic2.data()));Put(reinterpret_cast<void*>(base+0xD2A9A8),address(formation.data()));Put(reinterpret_cast<void*>(base+0xD2A9A6),static_cast<unsigned>(formation.size()),2);
    Put(reinterpret_cast<void*>(base+0xD2A9C0),address(formation.data()+Read(formation.data()+12)));
    std::memcpy(reinterpret_cast<void*>(base+0xD2C25A),"omeg01_10",10);Put(reinterpret_cast<void*>(base+FfxHooks::ExecutableProfile::Rva<0xD2CA90>()),271,2);
    const auto compare=address(reinterpret_cast<const void*>(&std::strcmp));Patch(0x70C378,&compare,4);
    phase="native actor initialization";
    for(unsigned role=0;role<5;++role){auto* p=Actor(20+role);monsters[role]=File(root+"/m"+std::to_string(O::Native::Species(role))+".bin");auto& mon=monsters[role];
        Put(p+0xE,0x1000+O::Native::Species(role),2);Put(p+0x48,address(mon.data()));Put(p+0xF80,address(mon.data()+Read(mon.data()+12)));Put(p+0xF7C,address(mon.data()+Read(mon.data()+8)));p[0x50]=0x60;p[0xDC8]=p[0xDD2]=p[0xDD3]=1;
        reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(base+0x39B4F0)(0,20+role);
        Put(p,address(models.data()+role*0x1000));for(unsigned at:{0xBCu,0xC0u,0xC4u}){float one=1;std::memcpy(models.data()+role*0x1000+at,&one,4);}
    }
    for(unsigned id:{0u,1u,2u,14u}){auto* p=Actor(id);p[0xDC8]=p[0xDC9]=p[0xDD2]=p[0xDD3]=1;Put(p+0xE,id,2);Put(p+0x594,5000);Put(p+0x5D0,5000);}
    std::memset(reinterpret_cast<void*>(base+0xD2C895),255,7);for(unsigned i=0;i<3;++i)*reinterpret_cast<unsigned char*>(base+0xD2C895+i)=static_cast<unsigned char>(i);
    Stub(0x23D050,reinterpret_cast<void*>(&Render));Stub(0x3AB360,reinterpret_cast<void*>(&Event));Stub(0x3996D0,reinterpret_cast<void*>(&Targets));
    auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!trampoline)return 2;
    std::memcpy(trampoline,reinterpret_cast<void*>(base+0x3A89C0),6);trampoline[6]=0xE9;Put(trampoline+7,static_cast<unsigned>(base+0x3A89C6-reinterpret_cast<std::uintptr_t>(trampoline+11)));realRevive=reinterpret_cast<Revive>(trampoline);Stub(0x3A89C0,reinterpret_cast<void*>(&HeadlessRevive));
    Q::original=reinterpret_cast<void*>(base+0x3B0B90);C::originals[2]=reinterpret_cast<void*>(base+0x38E230);A::originals[0]=reinterpret_cast<void*>(&Result);A::originals[1]=reinterpret_cast<void*>(base+0x3B0860);
    auto raw=File(argv[3]);std::string manifest(raw.begin(),raw.end());const auto off=manifest.find("false");if(off==std::string::npos)return 2;manifest.replace(off,5,"true");
    phase="admission";Check(R::PrepareForTests(base,manifest.c_str(),Context,Log),"real production controller subscribes to private native owners");
#ifdef FFXHOOKS_HAVE_POLYHOOK
    Check(Q::Start(base),"the actual signature-gated native append detour installs");
#endif
    Reset();Check(R::SnapshotForTests().active,"exact v4 encounter admits combat");Check(R::SnapshotForTests().gauge==20,"one native body packet charges once");
    phase="native Zanmato damage calculation";
    {O::CommandRow zanmato{};Check(N::Command(base,0x30E2,zanmato)&&zanmato[0x2E]==255,"real player Zanmato has its native instant-death payload");
        std::array<unsigned char,44> info{};B::DamageScope scope;Actor(14)[0x5AA]=100;unsigned landed=0,missed=0;
        const auto landedAt=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&landed)),missedAt=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&missed));
        B::EnterDamage(scope,{14,Actor(14),20,Actor(20),zanmato.data(),0x30E2,info.data(),0,landedAt,missedAt,0});
        const auto* view=static_cast<const unsigned char*>(scope.forwardCommand);
        Check(view!=zanmato.data()&&view[0x2E]==0&&view[0x23]==1&&zanmato[0x2E]==255,"owned Zanmato borrows HP-only calculation without changing its native command");
        using Damage=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
        const auto result=reinterpret_cast<Damage>(base+0x38E630)(14,Actor(14),20,Actor(20),scope.forwardCommand,0x30E2,info.data(),0,landedAt,missedAt,0);
        Check((N::Word(info.data()+20)&1)==0,"actual native calculation cannot retain Zanmato instant death on the body");
        B::LeaveDamage(scope,result,true);
    }
    phase="sealed HP";Hit(0,20,0x3000,500000);Check(Read(Actor(20)+0x5D0)==3000000,"physical damage cannot pass the living Sacred seal");
    phase="closed break window";BreakCase(false);
    phase="Zanmato closed";Put(reinterpret_cast<void*>(base+0xD3338C),1);Hit(14,20,0x30E2,1,true);Check(Read(Actor(20)+0x5D0)==3000000,"Zanmato special subtraction and normal HP path both respect seals");Check(Read(reinterpret_cast<void*>(base+0xD3338C))==1,"native Zanmato mode restored after each HP scope");Put(reinterpret_cast<void*>(base+0xD3338C),0);
    phase="exposure and Defend";Put(Actor(21)+0x5D0,1);Put(Actor(22)+0x5D0,1);Hit(0,21,0x3000,1);Hit(0,22,0x3000,1);Check(R::SnapshotForTests().exposure==4,"last helper kill leaves four subsequent actions");
    phase="open break window";BreakCase(true);
    Queue(0,0x3021);Finish(0);Check(R::SnapshotForTests().exposure==3,"completed native Defend without ResultCall consumes one exposure action");
    phase="separate native input queue";
    std::memset(reinterpret_cast<void*>(base+0xD2AA80),0,8);*reinterpret_cast<unsigned char*>(base+0xD2BDE0)=1;Actor(0)[0xDE6]=1;Actor(0)[0xDE8]=3;
    reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned,unsigned)>(base+FfxHooks::ExecutableProfile::Rva<0x3B20F0>())(0,0,0,0);
    Check(!*reinterpret_cast<unsigned char*>(base+0xD2BDE0)&&R::SnapshotForTests().exposure==3,"native input-turn consumption is not an extra completed action or free-switch clock");
    Queue(0,0x3021);Finish(0,false);Check(R::SnapshotForTests().exposure==3,"an unfinished native packet cancellation does not consume exposure");
    phase="mixed Zanmato publications";Queue(14,0x30E2);Put(reinterpret_cast<void*>(base+0xD3338C),1);
    C::Hp(20,Actor(20),1,0,0,0,0);damageAmount=1;A::ResultShim(14,0,20,nullptr,nullptr);Finish(14);
    Check(Read(Actor(20)+0x5D0)==2900001,"direct native publication and ResultCall share one Zanmato budget");Put(Actor(20)+0x5D0,3000000);
    phase="Zanmato bounded";Put(reinterpret_cast<void*>(base+0xD3338C),1);Hit(14,20,0x30E2,1,true);Check(Read(Actor(20)+0x5D0)==2900001,"Zanmato is capped at 99999 across two HP publications in one action");
    const C::Callbacks arcanaTransform{UnusedMp,UnusedCritical,ArcanaTransform},vanguardTransform{UnusedMp,UnusedCritical,VanguardTransform};
    Check(C::Register(C::arcana,&arcanaTransform)&&C::Register(C::vanguard,&vanguardTransform),"composed HP transforms are installed before the final boss gate");
    Put(Actor(20)+0x5D0,800000);Hit(14,20,0x30E2,1,true);Check(Read(Actor(20)+0x5D0)==750000&&R::SnapshotForTests().finalPending,"Zanmato stops at the final-entry floor after both composed HP transforms");
    C::Unregister(C::arcana,&arcanaTransform);C::Unregister(C::vanguard,&vanguardTransform);
    Hit(14,20,0x30E2,1,true);Check(Read(Actor(20)+0x5D0)==750000,"later Zanmato actions cannot bypass pending final entry");Put(reinterpret_cast<void*>(base+0xD3338C),0);
    phase="final entry";Queue(20,O::Attack);Finish(20);Check(R::SnapshotForTests().finalEntered&&R::SnapshotForTests().helpers==15&&R::SnapshotForTests().gauge==50,"completed defensive marker activates all four reserved helpers and charge50");
    Check(Read(Actor(20)+0x5D0)==750000&&Read(Actor(23)+0x5D0)==75000,"final entry restores only helpers without healing the body");
    phase="pending helper cleanup";
    for(unsigned id=21;id<=24;++id)Queue(id,O::Attack);
    Queue(20,O::Attack);Queue(20,0x407E,1);
    for(unsigned id=21;id<=24;++id)Put(Actor(id)+0x5D0,0);Put(Actor(20)+0x5D0,1);Hit(0,20,0x3000,1);
    const auto* remaining=reinterpret_cast<unsigned char*>(base+0xD2AC70);
    Check(*reinterpret_cast<std::int8_t*>(base+0xD2BDE1)==1&&remaining[0]==20&&N::Word(remaining+8)==0x407E,"native retirement cancels queued helper/body offence while retaining original death command");
    for(unsigned id=21;id<=24;++id)Check(!Actor(id)[0xDE7]&&!Actor(id)[0xDC8]&&Actor(id)[0xDCD],"dead helper has no native queued action and is excluded from execution");
    Finish(20);Actor(20)[0xDCC]=1;Check(!R::SnapshotForTests().active,"original death command cannot restart the retired controller");
    const auto* terminal=FfxHooks::BattleTerminal::observers[static_cast<unsigned>(FfxHooks::BattleTerminal::Slot::OmegaLastSeal)].load();
    if(terminal){void* token=terminal->before(1);if(token)terminal->after(token,true);}Check(grants==0,"forced exact encounter never grants Weapon cards");
    phase="response and Defend";Reset();for(unsigned i=0;i<4;++i){Queue(20,O::Attack);Finish(20);}Check(R::SnapshotForTests().preparing&&R::SnapshotForTests().response==3,"five normal body turns announce a three-action preparation");Queue(0,0x3021);Finish(0);Check(R::SnapshotForTests().response==2,"non-damaging Defend advances preparation without a ResultCall");
    phase="guest player action";auto* guest=Actor(7);guest[0xDC8]=guest[0xDD2]=guest[0xDD3]=1;Put(guest+0xE,7,2);Put(guest+0x5D0,5000);Put(guest+0x594,5000);Queue(7,0x3021);Finish(7);Check(R::SnapshotForTests().response==1,"a real guest player action also advances the response clock");guest[0xDC8]=0;
    phase="native Shell";O::CommandRow nova{};Check(N::Command(base,O::Supernova,nova),"authored Supernova resolves in the full expanded native table");
    std::array<unsigned char,44> shellInfo{};shellInfo[0xA]=254;unsigned protectionFlags=0;int divisor=1;
    using Shell=int(__cdecl*)(const void*,unsigned*,int*,const void*,int);
    Check(reinterpret_cast<Shell>(base+0x38ADC0)(nova.data(),&protectionFlags,&divisor,shellInfo.data(),4000)==2000,"the actual native Shell leaf halves an authored Supernova pulse");
    phase="original counter and victory";context.original=true;Reset();Put(Actor(20)+0x5D0,1400000);Put(Actor(21)+0x5D0,0);Put(Actor(22)+0x5D0,0);
    for(unsigned i=0;i<3;++i)Hit(0,20,0x3000,1);
    auto* reaction=reinterpret_cast<unsigned char*>(base+0xD2AC70);
    Check(*reinterpret_cast<std::int8_t*>(base+0xD2BDE1)==1&&reaction[1]==1&&N::Word(reaction+8)==O::Reprimand&&Read(reaction+16)==1,"three completed same-category attacks queue one native reaction at the last attacker");
    const auto exposure=R::SnapshotForTests().exposure;Actor(20)[0xDE5]=0;damageAmount=1;A::ResultShim(20,0,0,nullptr,nullptr);Finish(20);
    Check(R::SnapshotForTests().exposure==exposure&&!R::SnapshotForTests().gauge&&!*reinterpret_cast<std::int8_t*>(base+0xD2BDE1),"Reprimand neither recurses nor charges the gauge or party clocks");
    Hit(0,20,0x3000,2000000);Queue(20,O::Attack);Finish(20);
    Put(reinterpret_cast<void*>(base+0xD3338C),1);for(unsigned id=21;id<=24;++id)Hit(14,id,0x30E2,1);Put(reinterpret_cast<void*>(base+0xD3338C),0);
    Check(!R::SnapshotForTests().helpers,"native Zanmato breaks every final-phase helper without a body bypass");
    Queue(20,0x407E,1);Hit(0,20,0x3000,1000000);Finish(20);Actor(20)[0xDCC]=1;
    Check(O::Native::RawOutcome(base)==4||O::Native::RawOutcome(base)==2,"native death settlement reports an actual party victory");
    if(terminal){void* token=terminal->before(1);if(token)terminal->after(token,true);}Check(grants==1,"confirmed original Omega terminal emits one paired Weapon receipt");
    phase="outside scope";R::Stop();Put(Actor(20)+0x5D0,3000000);Put(reinterpret_cast<void*>(base+0xD3338C),1);C::Hp(20,Actor(20),1,0,0,0,0);Check(Read(Actor(20)+0x5D0)==2900000,"outside admitted scope both native Zanmato paths remain untouched");
    std::printf("OmegaLastSealRuntimeRt1: %u checks, %u failures; rendering, target selection and result animation are controlled boundaries\n",checks,failures);return failures?1:0;
}
