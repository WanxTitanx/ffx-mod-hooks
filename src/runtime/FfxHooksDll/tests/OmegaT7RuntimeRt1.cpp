#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/OmegaT7Runtime.h"
#include "../hooks/OmegaT7Native.h"
#include "../hooks/OmegaT7Evidence.h"
#include "../hooks/OmegaT7Session.h"
#include "../hooks/OmegaT7Notice.h"
#include "../hooks/OmegaT7Bridge.h"
#include "../hooks/SinMetadataCore.h"
#include "../hooks/SharedActionRuntime.h"
#include "../hooks/SharedQueueRuntime.h"
#include "../hooks/SharedTurnRuntime.h"
#include "../hooks/SharedBattleRuntime.h"
#include "../hooks/CombatExtensionBus.h"
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/F7DifficultyCore.h"
#include "../hooks/SinNaturalCore.h"
#include "../hooks/SinNaturalFormation.h"
#include "OmegaStatsFixtures.h"
#include "../shared/Config.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
namespace O=FfxHooks::OmegaT7;namespace R=O::Runtime;namespace N=O::Native;namespace A=FfxHooks::SharedAction;namespace C=FfxHooks::SharedCombat;namespace B=FfxHooks::CombatExtensions;namespace T=FfxHooks::SharedTurn;namespace P=FfxHooks::ExecutableProfile;
namespace FfxHooks::Config {bool GetBool(const char* key,bool fallback){return std::strcmp(key,"spira.omega_t7")==0?true:fallback;}}
namespace FfxHooks::EquipmentWorkshop {bool CombatProducerReady(){return true;}}
namespace FfxHooks::OmegaT7::Session {bool Ready() noexcept{return false;}bool Prepared() noexcept{return true;}std::uint64_t BattleSeed(std::uint64_t) noexcept{return 0;}void Stop() noexcept{}void Prime(std::uintptr_t,const std::filesystem::path&,bool,bool,Log) noexcept{}bool Snapshot(Campaign&,std::uint64_t&) noexcept{return false;}}
namespace FfxHooks::OmegaT7::Notice {void Start() noexcept{}void Stop() noexcept{}void RequestReview() noexcept{}}
static unsigned checks=0,failures=0,amount=1,code=0,critical=0;static std::uintptr_t base=0;static unsigned profileNow=0;
static const char* phase="setup";
static std::vector<unsigned char> actors(31*0xF90),resource(4096),formation(28);static std::string manifest;
static O::Bindings bindings;static FfxHooks::SinAi::Context context{false,590,1,1,FfxHooks::SinSpread::Distribution::All,1};
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL profile=%u %s\n",profileNow,why);}}
static void Log(const char* s){std::fputs(s,stdout);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* e){std::printf("OMEGA_RUNTIME_EXCEPTION phase=%s profile=%u code=%08lX rva=%08lX address=%08lX eax=%08lX ecx=%08lX edx=%08lX\n",phase,profileNow,e->ExceptionRecord->ExceptionCode,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-base),e->ExceptionRecord->NumberParameters>1?static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]):0,e->ContextRecord->Eax,e->ContextRecord->Ecx,e->ContextRecord->Edx);return EXCEPTION_CONTINUE_SEARCH;}
static std::vector<unsigned char> ReadFileBytes(const char* p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static unsigned char* Actor(unsigned id){return actors.data()+id*0xF90;}
namespace D=FfxHooks::F7Difficulty;namespace S=FfxHooks::SinRam;
static D::Runtime statWriter;
static DWORD statThread=0;
static bool StatRead(void*,std::uintptr_t a,void* out,std::size_t n){return FfxHooks::NativeUiSupport::Copy(out,reinterpret_cast<void*>(a),n);}
static bool StatWrite(void*,std::uintptr_t a,const void* in,std::size_t n){return FfxHooks::NativeUiSupport::Copy(reinterpret_cast<void*>(a),in,n);}
static bool StatProof(unsigned id,std::uintptr_t address,std::uint32_t source,std::uint32_t maximum) noexcept {
    if(GetCurrentThreadId()!=statThread||id<20||id>=28||N::Actor(base,id)!=address||
       N::ReadOr<unsigned>(address+0x48)!=source||N::ReadOr<unsigned>(address+0x594)!=maximum)return false;
    return statWriter.OwnsOmegaMiniHp(context.generation,{address,static_cast<std::uint8_t>(id-20)},source,maximum);
}
static const O::Bridge::ScaleProvider statProvider{StatProof};
static void ScaleMini(bool begin){
    if(begin){statWriter.BeginGeneration(context.generation);statThread=GetCurrentThreadId();}
    S::RuntimeRequest request;request.omegaT7=true;request.config.enabled=O::Bridge::Available(context.field,context.formation);
    request.encounterToken=(unsigned(context.field)<<16)|context.formation;request.nativeFieldRow=88;
    request.origin=S::EncounterOrigin::Natural;request.transitionCallerRva=FfxHooks::SinNatural::kCallerRva;
    request.transitionRequestId=request.actorRequestId=1;request.transitionGeneration=context.generation;
    const D::ActorRef actor{reinterpret_cast<std::uintptr_t>(Actor(20)),0};
    statWriter.UpdateComposed({nullptr,StatRead,StatWrite},D::MakeNeutralConfig(),true,88,request,&actor,1);
}
static void Put(void* p,unsigned v,unsigned n=4){std::memcpy(p,&v,n);}
static bool Context(bool,FfxHooks::SinAi::Context* out){*out=context;return true;}
static int Count(){return *reinterpret_cast<std::int8_t*>(base+P::Rva<0xD2BDE1>());}
static unsigned char* Queue(unsigned source,unsigned command=0x6000,unsigned reaction=0){
    auto* q=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());std::memset(q,0,72*62);
    *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=0;
    for(unsigned id=0;id<31;++id){Actor(id)[0xDE5]=255;Actor(id)[0xDE7]=0;}
    std::array<unsigned char,72> row{};row[0]=static_cast<unsigned char>(source);row[1]=static_cast<unsigned char>(reaction);row[3]=1;
    Put(row.data()+8,command,2);Put(row.data()+10,255,2);Put(row.data()+16,1,4);
    if(!FfxHooks::SharedQueue::installed.load())FfxHooks::SharedQueue::original=reinterpret_cast<void*>(base+0x3B0B90);
    Check(FfxHooks::SharedQueue::Shim(source,row.data(),0,0,reaction)==-1,"production queue observer forwards one native append");
    Actor(source)[0xDE5]=0;return q;
}
static unsigned Sub(unsigned source){const auto index=Actor(source)[0xDE5];return index<Count()?*reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>()+72u*index+2):0;}
static void StartExtra(unsigned source){
    auto* q=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>()+72u*Actor(source)[0xDE5]);
    Check(q[3]>1,"the extra remains in its original native action packet");q[2]=static_cast<unsigned char>(q[3]-1);
}
static int __cdecl Result(unsigned source,unsigned sub,unsigned target,int*,void*){
    auto* p=Actor(target);auto* group=p+0x774;auto* info=group+24+44*group[0];
    info[1]=static_cast<unsigned char>(code);Put(info+0x18,1|(critical?0x100:0),2);Put(info+0x20,amount);
    if(!code&&amount)C::Hp(target,p,static_cast<int>(amount),0,0,0,0);
    group[2]=static_cast<unsigned char>(source);group[3]=static_cast<unsigned char>(sub);++group[0];return 2;
}
static void Damage(unsigned source,unsigned target,unsigned damage=1,unsigned outcome=0,bool crit=false){
    phase="result";
    amount=damage;code=outcome;critical=crit?1:0;
    const auto sub=Sub(source);auto* p=Actor(target);std::memset(p+0x774,0,728*2);p[0x775]=1;p[0x776]=static_cast<unsigned char>(source);p[0x777]=static_cast<unsigned char>(sub);
    int result=0;Check(A::ResultShim(source,sub,target,&result,nullptr)==2,"result continuation returns unchanged");
}
static int Finish(unsigned source){phase="finish/target";auto* q=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());const unsigned index=Actor(source)[0xDE5];q[index*72+2]=q[index*72+3];return A::FinishShim(source,index,0);}
static unsigned Outcome(){return reinterpret_cast<unsigned(__cdecl*)()>(base+0x3928D0)();}
static void LeaveParty(unsigned first=0,unsigned end=3){
    using Remove=void(__cdecl*)(unsigned,unsigned char*,int);
    for(unsigned i=first;i<end;++i)reinterpret_cast<Remove>(base+P::Rva<0x38DF00>())(i,Actor(i),1);
}
static int __cdecl HpThenLeave(unsigned target,void* actor,int hpDelta,int display,int resultCode,int hitFlags,int otherFlags){
    using Hp=int(__cdecl*)(unsigned,void*,int,int,int,int,int);
    const int result=reinterpret_cast<Hp>(base+P::Rva<0x38E2F0>())(target,actor,hpDelta,display,resultCode,hitFlags,otherFlags);
    LeaveParty();return result;
}
static std::uint64_t Battle(unsigned profile){
    profileNow=profile;std::fill(actors.begin(),actors.end(),static_cast<unsigned char>(0));std::fill(formation.begin(),formation.end(),static_cast<unsigned char>(0));
    const unsigned model=O::Catalog[profile].species==0?188:O::Catalog[profile].species==0xFFFF?251:O::Catalog[profile].species;
    O::Roster roster{model,49,239,0,0,0,0,0};for(unsigned i=0;i<8;++i)Put(formation.data()+12+2*i,roster[i]?0x1000+roster[i]:0xFFFF,2);
    for(unsigned id:{0u,1u,2u,20u,21u,22u}){auto* p=Actor(id);Put(p+0xC,id,2);Put(p+0xE,id<18?id:0x1000+roster[id-20],2);Put(p+0x48,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(resource.data())));
        p[0x540]=0x60;p[0xDC8]=p[0xDC9]=1;p[0xDD2]=p[0xDD3]=p[0xDD9]=1;p[0xDE5]=p[0x592]=p[0x593]=255;Put(p+0x594,1000);Put(p+0x5D0,1000);}
    context.field=590;context.formation=0;++context.generation;
    std::uint64_t seed=1;for(;seed<1000000;++seed)if(O::Assign(seed,590,0,roster)[0]==profile)break;
    Check(seed<1000000,"deterministic seed reaches every approved mark");
    Check(R::PrepareForTests(base,manifest.c_str(),Context,seed,Log),"complete real command bindings prepare");return seed;
}
#include "OmegaT7SupportCases.inl"
#include "OmegaT7PacketCases.inl"
#include "OmegaT7PreparedCases.inl"
int main(int argc,char** argv){
    if(argc!=6)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);AddVectoredExceptionHandler(1,Fault);
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;base=reinterpret_cast<std::uintptr_t>(image);
    Check(PrivatePeFixture::NormalizeRelocations(image),"exact private Steam image relocates");
    const auto missing=std::filesystem::temp_directory_path()/("omega-missing-"+std::to_string(GetCurrentProcessId()));
    R::Prime(base,missing,false,Log);
    Check(!R::Prepared()&&std::strstr(R::Detail(),"bindings")!=nullptr,"missing package produces a bindings diagnostic");
    Check(!R::Start(Context),"missing package cannot activate the runtime");
    R::Stop();
    Check(std::strstr(R::Detail(),"bindings")!=nullptr,"startup cleanup preserves the missing-package cause for logs and UI");
    auto kernels=ReadFileBytes(argv[2]),commands=ReadFileBytes(argv[3]),json=ReadFileBytes(argv[4]),monsterCommands=ReadFileBytes(argv[5]);manifest.assign(json.begin(),json.end());const auto at=manifest.find("false");if(at==std::string::npos)return 2;manifest.replace(at,5,"true");
    const auto ctbPath=(std::filesystem::path(argv[2]).parent_path()/"ctb_base.bin").string();
    auto ctb=ReadFileBytes(ctbPath.c_str());Check(!ctb.empty(),"native CTB table is supplied for donor Haste/Slow calculations");
    if(ctb.empty())return 1;
    Put(reinterpret_cast<void*>(base+0xD2A94C),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(ctb.data())));
    Check(O::ParseBindings(manifest,bindings)==O::BindingCode::Ready,"installed Omega bindings resolve");
    Check(!std::filesystem::exists(missing),"startup package fixture uses an unowned path only when absent");
    std::filesystem::create_directories(missing/"modules"/"config");
    {std::ofstream file(missing/"modules"/"config"/"_omega-t7-v1.json",std::ios::binary);file<<manifest;}
    R::Prime(base,missing,false,Log);
    Check(R::Prepared()&&!R::Active(),"enabled package primes through the production file loader before encounter startup");
    Check(!R::Start(nullptr)&&std::strstr(R::Detail(),"context")!=nullptr,"missing encounter context has a precise startup diagnostic");
    R::Stop();
    Check(std::strstr(R::Detail(),"context")!=nullptr,"dependency failure survives cleanup without retaining an active runtime");
    std::filesystem::remove_all(missing);
    Put(reinterpret_cast<void*>(base+P::Rva<0xD334CC>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors.data())));
    Put(reinterpret_cast<void*>(base+0xD34468),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(Actor(20))));
    Put(reinterpret_cast<void*>(base+P::Rva<0xD2A934>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(kernels.data())));
    Put(reinterpret_cast<void*>(base+P::Rva<0xD2A92C>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(commands.data())));
    Put(reinterpret_cast<void*>(base+P::Rva<0xD2A930>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(monsterCommands.data())));
    Put(reinterpret_cast<void*>(base+P::Rva<0xD2A9C0>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(formation.data())));
    std::array<unsigned char,64> scenario{};unsigned variant=0;
    Put(reinterpret_cast<void*>(base+P::Rva<0xD2A9C8>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(scenario.data())));
    Put(reinterpret_cast<void*>(base+P::Rva<0xD2A9FC>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&variant)));
    Put(reinterpret_cast<void*>(base+P::Rva<0xD2C256>()),0,2);
    std::memset(reinterpret_cast<void*>(base+P::Rva<0xD2C895>()),255,7);for(unsigned i=0;i<3;++i)*reinterpret_cast<unsigned char*>(base+P::Rva<0xD2C895>()+i)=static_cast<unsigned char>(i);
    *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2A8E0>())=1;
    DWORD old=0,ignored=0;const auto compare=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&std::strcmp));VirtualProtect(reinterpret_cast<void*>(base+0x70C378),4,PAGE_READWRITE,&old);Put(reinterpret_cast<void*>(base+0x70C378),compare);VirtualProtect(reinterpret_cast<void*>(base+0x70C378),4,old,&ignored);
    A::originals[0]=reinterpret_cast<void*>(&Result);A::originals[1]=reinterpret_cast<void*>(base+P::Rva<0x3B0870>());
    C::originals[2]=reinterpret_cast<void*>(base+P::Rva<0x38E2F0>());T::imageBase=base;T::original=reinterpret_cast<void*>(base+P::Rva<0x3B13D0>());
    AtomicPacketCases();
    PreparedPacketCases();
    for(unsigned profile=0;profile<48;++profile){Battle(profile);Queue(20);Damage(20,0);
        R::PreviewSnapshot preview;
        Check(R::Preview(preview)&&preview.field==590&&preview.count==3&&preview.monsters[0].profile==profile&&preview.monsters[0].name[0]==0x60,"preview exposes actual native assignment and preserves original monster identity");
        bool foreign=true;std::thread inspect([&]{R::PreviewSnapshot other;foreign=R::Preview(other);});inspect.join();
        Check(!foreign,"preview never reads actor ownership from another thread");
        std::array<unsigned char,40> originalName{},expectedName{};originalName[0]=0x60;
        Check(FfxHooks::SinMetadata::NamedView(originalName,O::Catalog[profile].name,7,&expectedName)&&
            !std::memcmp(Actor(20)+0x540,expectedName.data(),expectedName.size()),"every active Omega rule marks the native monster name with its curse and T7");
        const auto* q=reinterpret_cast<const unsigned char*>(base+P::Rva<0xD2AC70>());Check(Count()==1&&N::Word(q+24)==bindings.Get(O::Catalog[profile].skill).id&&q[1]==0&&q[3]==2,"each mark bundles one approved extra behind its original command");
        Check(Finish(20)==1&&Count()==0,"the complete native packet retires once without appending another reaction");}
    Battle(0);Queue(0,0x3000);Damage(0,20,500);Finish(0);Check(Actor(20)[0x60B]==254&&Actor(20)[0x660]==1,"Abyss crossing grants removable Shell and native Focus one");
    Actor(20)[0x60B]=0;Queue(0,0x3000);Damage(0,20,1);Finish(0);Check(!Actor(20)[0x60B]&&Actor(20)[0x660]==1,"Dispel never rearms a consumed crossing");
    Battle(1);Actor(21)[0x613]=7;Queue(0,0x3000);Damage(0,20,500);Finish(0);
    Check(Actor(20)[0x613]==254&&Actor(21)[0x613]==7&&Actor(22)[0x613]==254,"conditional Haste grants only missing effects to the entire living monster group");
    Battle(14);Queue(0,0x3000);Damage(0,20,500);Finish(0);Check(Actor(20)[0x612]==254&&!Actor(20)[0x613],"Profanation is only one self-Regen crossing reaction");
    Battle(18);Queue(0,0x3000);Damage(0,20,200);Check(N::ReadOr<unsigned>(reinterpret_cast<std::uintptr_t>(Actor(20))+0x5D0)==950,"Fortress heals immediately after the qualifying actual hit");Finish(0);
    Battle(28);Queue(0,bindings.Get(O::Skill::Firaja).id);Damage(0,20,1);Finish(0);Check((Actor(20)[0x5DA]&1)!=0,"Prism applies its first basic element absorption");
    Queue(20);T::Shim(20,Actor(20));Check((Actor(20)[0x5DA]&1)!=0,"Prism survives the first genuine monster turn");
    Queue(20);T::Shim(20,Actor(20));Check(!(Actor(20)[0x5DA]&1),"Prism restores its original affinity after two monster turns");
    Battle(2);Queue(0,0x3000);Damage(0,20,0,1);Finish(0);Queue(20);Damage(20,0);StartExtra(20);
    Actor(20)[0xDE5]=0;const auto skill=bindings.Get(O::Skill::Aeroja);B::DamageScope scope;
    B::EnterDamage(scope,{20,Actor(20),0,Actor(0),skill.row.data(),skill.id});
    Check(B::UpperDamage(100,9999,20,skill.id,3,false)==130,"charged Aeroja is increased thirty percent before the native cap");
    Check(B::UpperDamage(9000,9999,20,skill.id,3,false)==9999,"charged damage retains the native upper limit");B::LeaveDamage(scope,0,true);Damage(20,0,130);Finish(20);
    Battle(21);Actor(20)[0x660]=5;Queue(20);Damage(20,0);Finish(20);Check(Count()==0,"saturated Focus skips the defensive extra without offensive reroll");
    Battle(0);O::Bridge::Populated();Put(Actor(20)+0x5D0,500);O::Bridge::Populated();
    Check(Actor(20)[0x60B]==254&&Actor(20)[0x660]==1,"native poison or scripted HP crossing is observed without another damage action");
    Battle(21);O::Bridge::Populated();Actor(20)[0x660]=5;O::Bridge::Populated();
    Check(Actor(20)[0x60B]==254,"native Focus reaching five is observed before the next action");
    Battle(31);O::Bridge::Populated();Put(Actor(20)+0x606,0x40,2);O::Bridge::Populated();
    Check(Actor(20)[0x613]==254&&Actor(21)[0x613]==254,"scripted Armor Break starts Broken Shell group Haste once");
    Battle(8);O::Bridge::Populated();Put(Actor(21)+0x5D0,0);O::Bridge::Populated();
    Check(Actor(20)[0x613]==254&&!Actor(21)[0x613]&&Actor(22)[0x613]==254,"ally death buffs living monsters and excludes the dead ally");
    Battle(9);for(unsigned round=0;round<3;++round){Queue(20);Damage(20,0);StartExtra(20);Damage(20,0);Finish(20);
        Check(Actor(20)[0x660]==(round==2?1:0)&&Count()==0,"Cataclysm counts completed owned extras, including result-only dispatch, without chaining");}
    Battle(18);Queue(20);Damage(20,0);StartExtra(20);Actor(21)[0x60B]=7;
    const auto guard=bindings.Get(O::Skill::MightyGuard);B::DamageScope support;
    B::EnterDamage(support,{20,Actor(20),21,Actor(21),guard.row.data(),guard.id});
    const auto* forwarded=static_cast<const unsigned char*>(support.forwardCommand);
    Check(forwarded!=guard.row.data()&&forwarded[0x3D]==0&&forwarded[0x4A]==0&&forwarded[0x3E]==255&&guard.row[0x3D]==255,
        "group support masks an existing Shell duration without changing the shared command or missing Protect");B::LeaveDamage(support,0,true);
    NativeSupportCases();
    Battle(0);O::Bridge::Populated();Put(Actor(20)+0x5D0,500);
    std::thread foreign([]{O::Bridge::Populated();});foreign.join();
    Check(!Actor(20)[0x60B]&&!Actor(20)[0x660],"a foreign thread cannot observe or mutate a battle rule");O::Bridge::Populated();
    Check(Actor(20)[0x60B]==254&&Actor(20)[0x660]==1,"the original game-thread owner can still apply the pending condition");
    Battle(40);for(unsigned step=0;step<4;++step){Queue(20,0x4081);Damage(20,20,0);Finish(20);Check(Actor(20)[0x65E]==step+1&&Actor(20)[0x660]==step+1,"native Waddle actions advance Ancient Debt one step at a time");}
    Battle(41);for(unsigned step=0;step<4;++step){Queue(20,0x4081);Damage(20,20,0);Finish(20);Check(Actor(20)[0x613]==(step==3?254:0),"Last Step group Haste waits for the fourth native Waddle");}
    Battle(45);Queue(20,0x4022);Damage(20,0);Finish(20);Check(Actor(20)[0x660]==1,"m910 native Core Energy advances End Echo Focus");
    Battle(46);Queue(0,0x3016);Damage(0,20,0);Finish(0);Check(Actor(20)[0x613]==254&&Actor(21)[0x613]==254,"Steal attempt on a revealed Mimic grants group Haste even without damage");
    Battle(0);for(unsigned od:{0x61DEu,0x61DFu,0x61E0u,0x61B9u}){Queue(20,od);Damage(20,0);Finish(20);Check(Count()==0,"native OD does not receive a T7 chain");}
    Queue(20,0x6000,1);Damage(20,0);Finish(20);Check(Count()==0,"counter and reaction packets do not receive a T7 chain");
    for(unsigned profile=0;profile<48;++profile){
        Battle(profile);O::Bridge::Populated();Queue(20);
        Check(Outcome()==0,"native outcome admits a live ongoing battle");
        LeaveParty();Check(Outcome()==3,"actual native party removal produces the escape outcome");
        Finish(20);Check(Count()==0,"retirement after group escape cannot append any Omega extra");
    }
    Battle(0);O::Bridge::Populated();LeaveParty();Put(Actor(20)+0x5D0,500);O::Bridge::Populated();
    Check(!Actor(20)[0x60B]&&!Actor(20)[0x660],"passive mark reactions cannot run after native group escape");
    Battle(18);Queue(0,0x3000);C::originals[2]=reinterpret_cast<void*>(&HpThenLeave);
    Damage(0,20,200);C::originals[2]=reinterpret_cast<void*>(base+P::Rva<0x38E2F0>());
    Check(Outcome()==3&&N::ReadOr<unsigned>(reinterpret_cast<std::uintptr_t>(Actor(20))+0x5D0)==800,"post-HP callback rechecks native termination before a recovery reaction");
    Battle(19);O::Bridge::Populated();LeaveParty(0,1);Queue(20);
    Check(Outcome()==0,"one escaped character does not terminate a remaining party");
    Check(Count()==1&&*reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>()+3)==2,"individual Escape preserves the already combined Omega packet while allies remain");Finish(20);
    Battle(19);Queue(20);StartExtra(20);LeaveParty();
    Check(A::FinishShim(20,0,0)==1&&Count()==0,"native cancellation drains an already owned extra after group escape");
    for(unsigned terminal:{1u,2u,3u,4u,255u}){
        Battle(19);O::Bridge::Populated();Queue(20);
        *reinterpret_cast<unsigned char*>(base+0xD2C9F1)=static_cast<unsigned char>(terminal);
        Check(Outcome()==terminal,"native scripted terminal outcome remains authoritative");
        Finish(20);Check(Count()==0,"terminal or unknown outcomes never receive an Omega follow-up");
        *reinterpret_cast<unsigned char*>(base+0xD2C9F1)=0;
    }
    Check(O::Bridge::RegisterScale(&statProvider),"isolated real actor writer publishes a read-only mini proof");
    Battle(45);for(const auto& f:omegaStatsFixtures)if(f.model==910){
        Put(Actor(20)+D::kMaxHpOffset,f.hp);Put(Actor(20)+D::kCurrentHpOffset,f.hp);Put(Actor(20)+D::kMaxMpOffset,f.mp);Put(Actor(20)+D::kCurrentMpOffset,f.mp);
        Put(Actor(20)+D::kOverkillOffset,f.overkill);for(unsigned i=0;i<8;++i)Actor(20)[D::kStatOffsets[i]]=f.stats[i];
    }
    Check(O::Bridge::Available(590,0),"live source and ongoing native outcome admit the stat request");
    ScaleMini(true);O::Bridge::Populated();
    Check(R::Active()&&N::ReadOr<unsigned>(reinterpret_cast<std::uintptr_t>(Actor(20))+0x594)==255000,"original mini150000 scales to255000 through the sole writer and stays admitted");
    Check(Actor(20)[D::kStatOffsets[2]]==128,"native mini MAG90 becomes128 through the same T7 curve");
    ScaleMini(false);Check(N::ReadOr<unsigned>(reinterpret_cast<std::uintptr_t>(Actor(20))+0x594)==255000,"native repeated update never compounds mini HP");
    LeaveParty();Check(!O::Bridge::Available(590,0),"group Flee revokes new stat admission");ScaleMini(false);
    Check(N::ReadOr<unsigned>(reinterpret_cast<std::uintptr_t>(Actor(20))+0x594)==150000,"withdrawn source restores owned mini HP after native Flee");
    Battle(45);Put(Actor(20)+0x594,255000);Put(Actor(20)+0x5D0,255000);ScaleMini(true);O::Bridge::Populated();
    Check(!R::Active(),"forged255000 source cannot inherit the original mini writer witness");
    O::Bridge::UnregisterScale(&statProvider);
    Battle(45);Put(Actor(20)+0x594,255000);O::Bridge::Populated();Check(!R::Active(),"255000 without the single writer proof remains rejected");
    Battle(0);context.field=591;context.formation=10;Queue(20);Damage(20,0);Finish(20);Check(Count()==0,"authored Omega boss and other fields are unchanged");
    Battle(0);context.formation=10;Queue(20);Damage(20,0);Finish(20);Check(Count()==0,"normal Omega engine rejects the authored m099 boss formation");
    const auto encounterPath=(std::filesystem::path(argv[2]).parent_path()/"btl.bin").string();
    auto encounterTable=ReadFileBytes(encounterPath.c_str());Check(!encounterTable.empty(),"real encounter table supplied for slot-to-asset correlation");
    FfxHooks::SinNatural::Evidence selected{FfxHooks::SinNatural::kCallerRva,-1,590,0,88,0,10};unsigned char asset=255;
    const FfxHooks::SinNatural::FormationReader readFormation{nullptr,[](void*,std::uintptr_t at,void* out,std::size_t count){return FfxHooks::NativeUiSupport::Copy(out,reinterpret_cast<const void*>(at),count);}};
    Check(FfxHooks::SinNatural::ResolveFormation(readFormation,reinterpret_cast<std::uintptr_t>(encounterTable.data()),selected,asset)&&asset==11,"native selected slot ten resolves to ordinary omeg00_11");
    Battle(0);context.formation=asset;Queue(20);Damage(20,0);R::PreviewSnapshot resolvedPreview;
    Check(Count()==1&&O::Bridge::Available(590,11)&&R::Preview(resolvedPreview)&&resolvedPreview.formation==11&&resolvedPreview.count==3,
        "resolved ordinary asset receives active T7, one combined packet and the actual encounter preview");
    Finish(20);
    Battle(0);O::Bridge::Populated();R::PreviewSnapshot last;
    Check(R::Preview(last)&&last.count==3,"populated encounter publishes a complete preview");
    B::Reset(B::ResetReason::BattleExit);
    Check(R::Preview(last)&&last.count==3&&last.monsters[0].profile==0,"last encounter remains visible after native actors are retired");
    B::Reset(B::ResetReason::NativeLoad);
    Check(!R::Preview(last),"native save load clears the prior campaign preview");
    Battle(0);O::Bridge::Populated();const auto changed=bindings.Get(O::Skill::Darkja).id;
    auto* changedRow=kernels.data()+N::Dword(kernels.data()+16)+(changed&4095)*92;const auto power=changedRow[0x2A];changedRow[0x2A]^=1;
    Queue(20);Damage(20,0);Finish(20);Check(!R::Active()&&Count()==0,"changed live command bytes close admission instead of queuing an incompatible extra");changedRow[0x2A]=power;
    R::Stop();std::printf("OmegaT7RuntimeRt1: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
