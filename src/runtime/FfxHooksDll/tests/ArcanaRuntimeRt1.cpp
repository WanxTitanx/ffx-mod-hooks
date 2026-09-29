#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "ArcanaFieldFixture.h"
#include "../hooks/ArcanaRuntime.h"
#include "../hooks/ArcanaElemental.h"
#include "../hooks/ArcanaCombat.h"
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/EquipmentWorkshopStore.h"
#include "../hooks/ArcanaCatalog.generated.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/RonsoPoolSave.h"
#include <cstdio>
#include <fstream>
#include <cstring>
#ifdef FFXHOOKS_SPIRA_COMPOSITION
#include "SpiraKernelFixture.h"
#include "../hooks/SpiraRuntime.h"
#include "../hooks/NovaSuperDamageHook.h"
#include "../hooks/VanguardRuntime.h"
#include "../shared/Config.h"
__declspec(naked) static std::size_t __cdecl RejectedNativeRead(void*,void*){
    __asm {
        push ebp
        mov ebp,esp
        push esi
        mov esi,[ebp+8]
        call dword ptr [ebp+0Ch]
        pop esi
        pop ebp
        ret
    }
}
#endif
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static void Log(const char* message){std::fputs(message,stdout);}
static int __cdecl Nothing(){return 0;}
static void* __cdecl Copy(void* out,const void* in,std::size_t size){return std::memcpy(out,in,size);}
static bool Patch(std::uintptr_t base,unsigned rva,void* target){
    auto* at=reinterpret_cast<unsigned char*>(base+rva);DWORD old=0,ignored=0;
    if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old))return false;
    at[0]=0xE9;const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(at)-5);std::memcpy(at+1,&delta,4);
    FlushInstructionCache(GetCurrentProcess(),at,5);return VirtualProtect(at,5,old,&ignored)!=FALSE;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=5
#ifdef FFXHOOKS_SPIRA_COMPOSITION
        &&argc!=6
#endif
        )return 2;
    HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!image||!PrivatePeFixture::NormalizeRelocations(image))return 2;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    FfxHooks::RonsoPool::SaveImage save{};std::ifstream input(argv[2],std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(save.data()),save.size()))return 2;
    const std::wstring root(argv[3],argv[3]+std::strlen(argv[3]));const auto path=root+L"\\ffx_008";
    const bool workshop=std::strcmp(argv[4],"workshop")==0
#ifdef FFXHOOKS_SPIRA_COMPOSITION
        ||argc==6
#endif
        ;
    const bool priorBalance=std::strcmp(argv[4],"v2")==0;
    const bool priorStatus=std::strcmp(argv[4],"v3")==0;
    const bool priorElements=std::strcmp(argv[4],"v4")==0;
    const bool legacy=priorBalance||priorStatus||priorElements||std::strcmp(argv[4],"legacy")==0;
    // Migration fixtures retain their saved loadout; the separate development
    // cases intentionally equip World/Empress and exercise field-stat changes.
    const bool development=!legacy&&(workshop||std::strcmp(argv[4],"development")==0);
    Runtime::Settings settings;
    Check(!Runtime::Prime(base,settings,false,Log)&&!FfxHooks::NativeSaveEvents::Requested(),"module OFF leaves native save infrastructure unrequested");
    settings.enabled=true;settings.fullDeck=development;
    Check(!Runtime::Prime(base,settings,true,Log)&&!FfxHooks::NativeSaveEvents::Requested(),"validate-only cannot register runtime writers");
#ifdef FFXHOOKS_SPIRA_COMPOSITION
    std::ifstream kernelInput(argv[5],std::ios::binary);
    std::vector<unsigned char> source((std::istreambuf_iterator<char>(kernelInput)),{});
    auto kernel=SpiraKernelFixture::Build(source);
    const auto kernelAddress=reinterpret_cast<std::uintptr_t>(kernel.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A944),&kernelAddress,4);
    const auto kernelSize=static_cast<unsigned short>(kernel.size());std::memcpy(reinterpret_cast<void*>(base+0xD2A970),&kernelSize,2);
    std::array<unsigned,8> language{};language[1]=1;const auto languageAddress=reinterpret_cast<std::uintptr_t>(language.data());
    std::memcpy(reinterpret_cast<void*>(base+0x8DED48),&languageAddress,4);
    const bool spiraFirst=std::strcmp(argv[4],"spira-first")==0;
    const bool sharedOnly=std::strcmp(argv[4],"shared-only")==0;
    if(spiraFirst)Check(FfxHooks::SpiraAbilities::Prepare(base,{true,true},false,Log),"Spira admits its producers before Arcana");
#endif
    const bool primed=Runtime::Prime(base,settings,false,Log);
    Check(primed&&FfxHooks::NativeSaveEvents::Requested(),"admitted Arcana requests the existing shared save producer");
    if(!primed){std::printf("ArcanaRuntimeRt1 %u/%u passed\n",checks-failures,checks);return 1;}
#ifdef FFXHOOKS_SPIRA_COMPOSITION
    const auto crtPath=std::filesystem::path(argv[1]).parent_path()/L"msvcr110.dll";
    const auto crt=LoadLibraryW(crtPath.c_str());Check(crt!=nullptr,"private native save CRT loads");
    if(!crt)return 1;
    for(const auto& entry:{std::pair<unsigned,const char*>{0x70C3F4,"fread"},{0x70C428,"fwrite"}}){
        const auto address=GetProcAddress(crt,entry.second);auto* slot=reinterpret_cast<void*>(base+entry.first);DWORD old=0,ignored=0;
        Check(address&&VirtualProtect(slot,4,PAGE_READWRITE,&old),"private CRT import slot is writable");
        std::memcpy(slot,&address,4);Check(VirtualProtect(slot,4,old,&ignored)!=FALSE,"private CRT import protection is restored");
    }
    Check(FfxHooks::InstallNovaSuperDamageHook(base,false,true,false,Log).ok,"finite clamp validates before any damage producer owns the entry");
#endif
    if(workshop)Check(FfxHooks::EquipmentWorkshop::StartForTests(base,
#ifdef FFXHOOKS_SPIRA_COMPOSITION
        !sharedOnly,
#else
        true,
#endif
        (root+L"\\workshop").c_str(),Log),"shared native producers install with the selected Workshop editing gate");
#ifdef FFXHOOKS_SPIRA_COMPOSITION
    FfxHooks::Config::LoadTextForTests("[vanguard]\nefficiency=1\nhero_caution=1\nvampirism=1\n[f8_authority]\nvanguard_efficiency=1\nvanguard_hero_caution=1\nvanguard_vampirism=1\n","C:\\private-composition.ini");
    if(spiraFirst)Check(FfxHooks::Vanguard::Start(base,false,Log),"Vanguard starts before Arcana combat");
#endif
    Check(Runtime::Start(),"native session/field provider installs after preflight");
    Elemental::Snapshot extra{};
    Check(Elemental::provider.load()!=nullptr&&!Elemental::Read(0,extra)&&!extra.battle,
          "real Arcana registers the elemental provider but exposes nothing before an admitted battle");
    Check(Combat::Start(base,true,false,Log)&&Combat::Active(),"profile-gated combat consumers install alongside the session provider");
#ifdef FFXHOOKS_SPIRA_COMPOSITION
    if(!spiraFirst)Check(FfxHooks::Vanguard::Start(base,false,Log),"Vanguard starts after Arcana combat");
    if(!spiraFirst&&!sharedOnly)Check(FfxHooks::SpiraAbilities::Prepare(base,{true,true},false,Log),"Spira admits already-shared clamp ownership after Arcana");
    if(!sharedOnly)Check(FfxHooks::InstallNovaSuperDamageHook(base,false,false,false,Log).ok&&FfxHooks::SpiraAbilities::Activate(),"one shared damage ceiling enables Spira without retiring Arcana");
    if(failures)return 1;
#endif
    State state;std::uint64_t generation=0;
    Check(!Runtime::Capture(state,generation),"runtime waits for an observed native load");
    // Only memcpy and the unrelated localized-name tail are substituted. The
    // real LoadDataFromBuffer body and installed producer boundary execute.
    const auto* copyCall=reinterpret_cast<const unsigned char*>(base+0x4B5462);
    std::int32_t copyDisplacement=0;std::memcpy(&copyDisplacement,copyCall+1,4);
    const unsigned copyTarget=static_cast<unsigned>(0x4B5467+static_cast<std::int64_t>(copyDisplacement));
    Check(copyCall[0]==0xE8&&copyTarget==0x54925C&&Patch(base,copyTarget,reinterpret_cast<void*>(Copy)),"private native memcpy dependency supplied at the actual call target");
    const auto* branch=reinterpret_cast<const unsigned char*>(base+0x4B546B);
    std::int32_t jump=0;std::memcpy(&jump,branch+1,4);
    const unsigned tail=static_cast<unsigned>(0x4B5470+static_cast<std::int64_t>(jump));
    Check(branch[0]==0xE9&&Patch(base,tail,reinterpret_cast<void*>(Nothing)),"unrelated post-load name synchronization isolated");
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=0;
    if(legacy){
        Record previous;previous.packHash={0x53,0xb3,0xc6,0x92,0xd7,0x8b,0x84,0x6e,0x0d,0x25,0x56,0xd2,0x27,0x17,0xc0,0x03,0x9f,0x01,0xf4,0xd5,0x51,0xfb,0xcb,0x3a,0x56,0x41,0x27,0x24,0xda,0x4e,0x16,0x3e};
        if(priorBalance)previous.packHash={0xb3,0x1a,0xd5,0x8a,0x8c,0x3e,0xe8,0xe8,0x98,0x9c,0x3d,0xea,0x34,0x87,0xb4,0x18,0xf1,0x1b,0x3a,0x30,0x06,0x97,0x4b,0xb9,0x02,0xa8,0xe3,0x13,0x4c,0x48,0x9e,0x75};
        if(priorStatus)previous.packHash={0xc9,0xa3,0xb6,0x34,0x9b,0x27,0x4d,0x21,0x37,0xe2,0xb3,0x17,0xee,0xd2,0x44,0x59,0x0e,0x19,0x20,0x75,0x28,0x49,0xfb,0xc7,0x2c,0xe1,0x6a,0x1b,0xac,0x51,0xce,0x37};
        if(priorElements)previous.packHash={0xc6,0xfe,0x69,0xe9,0xf9,0x6c,0xf7,0x76,0xa9,0x43,0xeb,0xeb,0xa7,0x25,0xed,0x65,0xdc,0x18,0x72,0xcc,0x6e,0x8e,0x0e,0x15,0x39,0x96,0x3c,0x16,0x4d,0xf2,0x45,0x3e};
        previous.state.mode=Mode::Constellation;AwardAll(previous.state,0);
        Equip(previous.state,previous.state.revision,0,0,21);Equip(previous.state,previous.state.revision,1,0,13);
        Equip(previous.state,previous.state.revision,2,0,18);Equip(previous.state,previous.state.revision,3,0,71);
        Equip(previous.state,previous.state.revision,4,0,0);
        Equip(previous.state,previous.state.revision,5,0,12);Equip(previous.state,previous.state.revision,6,0,9);
        Store previousStore;
        Check(FfxHooks::EquipmentWorkshop::Fingerprint(save.data(),save.size(),previous.nativeHash)&&previousStore.Prepare(path,previous)&&previousStore.Commit(path,previous.nativeHash),"fixture creates a save extension from the previously deployed balance pack");
    }
    FfxHooks::NativeSaveEvents::ReadCompleted(path.c_str(),save.data(),save.data(),save.size());
    reinterpret_cast<int(__cdecl*)(void*,const void*)>(base+0x4B5450)(reinterpret_cast<void*>(base+0xD2CA90),save.data());
#ifdef FFXHOOKS_SPIRA_COMPOSITION
    if(!sharedOnly){FfxHooks::SpiraAbilities::TickMainThread();Check(FfxHooks::SpiraAbilities::Ready(),"Spira kernel is admitted after the same native load");}
#endif
    Check(Runtime::Capture(state,generation)&&generation!=0,"observed read plus actual load binds one Arcana session");
    unsigned owned=0;for(auto card:state.acquired)owned+=card;
    Check(owned==((development||legacy)?78u:74u),"native fixture reconciles earned cards and retains the known legacy collection");
    if(legacy){
        Check(state.mode==Mode::Constellation&&state.slots[0][0]==21&&state.slots[1][0]==13&&state.slots[2][0]==18&&state.slots[3][0]==71,"real native load migrates the old collection without clearing equipped cards");
        Check(Runtime::ActorEffects(0)->Get(EffectKind::OverdriveDamage)==50&&Runtime::ActorEffects(1)->Get(EffectKind::DeathImmuneDamage)==20&&Runtime::ActorEffects(2)->Get(EffectKind::TouchConfuse)==50&&Runtime::ActorEffects(3)->Get(EffectKind::ApBonus)==75,"legacy ownership receives the current World, Death, Moon and Eight effects");
        Check(state.slots[4][0]==0&&Runtime::ActorEffects(4)->Get(EffectKind::FirstStrike)==1&&Runtime::ActorEffects(4)->Get(EffectKind::FirstCtbReduction)==35,"prior catalog versions preserve the equipped Fool and its opening benefits");
        Check(state.slots[5][0]==12&&state.slots[6][0]==9&&Runtime::ActorEffects(5)->Get(EffectKind::DefendMp)==3&&Runtime::ActorEffects(6)->Get(EffectKind::MpPerTurn)==1&&Runtime::ActorEffects(1)->Get(EffectKind::KillMp)==10,"migration keeps the loadout while applying lower repeatable MP recovery and preserving kill recovery");
    }
    if(priorElements){
        Check(Runtime::ActorEffects(1)->Get(EffectKind::StrikeBio)==1&&Runtime::ActorEffects(2)->Get(EffectKind::StrikeShadow)==1&&
              Runtime::ActorEffects(5)->Get(EffectKind::StrikeGravity)==1,"v4 migration preserves equipped cards and supplies their additional elemental effects");
    }
    Check(Runtime::EquipCard(generation-1,state.revision,0,0,0,false)==Error::Stale,"save-generation mismatch rejects old UI intent");
    if(development){
        ArcanaFieldFixture::FinalStores field;
        Check(field.Open(base,0,12345,200),"private fixture supplies bounded native field inputs");
        Check(Runtime::EquipCard(generation,state.revision,0,0,21,false)==Error::None,"World equips through the real field/clamp producer");
        unsigned maximum=0;std::memcpy(&maximum,reinterpret_cast<void*>(base+0xD3205C+0x24),4);
        Check(maximum==18517,"World enables BHP and its HP bonus before the original native maximum-HP clamp");
        Check(Runtime::Capture(state,generation),"field refresh preserves the owning session");
        Check(Runtime::EquipCard(generation,state.revision,0,1,3,false)==Error::None,"Empress joins the same native field calculation");
        std::memcpy(&maximum,reinterpret_cast<void*>(base+0xD3205C+0x24),4);
        Check(maximum==23455,"HP percentages apply exactly once on the native pre-cap value");
        reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x3861B0)(0);
        std::memcpy(&maximum,reinterpret_cast<void*>(base+0xD3205C+0x24),4);
        Check(maximum==23455,"repeated native recalculation does not compound card bonuses");
        std::memcpy(save.data()+64,reinterpret_cast<void*>(base+0xD2CA90),0x68C0);FfxHooks::RonsoPool::SealSave(save);
    }
    FfxHooks::NativeSaveEvents::WriteTransaction transaction;auto projected=save;
    Check(FfxHooks::NativeSaveEvents::ProjectWrite(path.c_str(),save.data(),projected.data(),save.size(),transaction),"bound Arcana session participates in copied-image serialization");
    FfxHooks::RonsoPool::SealSave(projected);
    Check(FfxHooks::NativeSaveEvents::PrepareWrite(path.c_str(),projected.data(),projected.size(),transaction),"sidecar prepare binds the exact projected native bytes");
    FfxHooks::NativeSaveEvents::FinishWrite(transaction,projected.data(),projected.size(),true);
    Check(std::filesystem::exists(Store::Extension(path,".arcana.v1")),"successful native completion persists the Arcana extension");
    if(legacy){
        Hash hash{},currentPack{};std::copy(std::begin(kPackHash),std::end(kPackHash),currentPack.begin());Record upgraded;Store reader;
        Check(FfxHooks::EquipmentWorkshop::Fingerprint(projected.data(),projected.size(),hash)&&reader.Read(path,hash,currentPack,upgraded)==StoreCode::Found&&upgraded.state.slots==state.slots,"the next real save commits the migrated collection with the new pack identity");
    }
#ifdef FFXHOOKS_SPIRA_COMPOSITION
    const auto rejectedPath=std::filesystem::path(root)/L"unidentified-save.bin";
    {std::ofstream rejected(rejectedPath,std::ios::binary);rejected.write(reinterpret_cast<const char*>(save.data()),save.size());}
    const auto open=reinterpret_cast<void*(__cdecl*)(const wchar_t*,const wchar_t*)>(GetProcAddress(crt,"_wfopen"));
    const auto close=reinterpret_cast<int(__cdecl*)(void*)>(GetProcAddress(crt,"fclose"));
    void* stream=open(rejectedPath.c_str(),L"rb");Check(stream!=nullptr,"private rejected-read fixture opens");
    auto rejectedImage=save;const auto output=reinterpret_cast<std::uintptr_t>(rejectedImage.data());
    const auto count=static_cast<std::uint32_t>(rejectedImage.size());
    const unsigned char returnAfterRead[]={0x83,0xC4,0x10,0xC3};
    Check(WorkshopFieldFixture::Write(base+0x8E72F4,&count,4)&&WorkshopFieldFixture::Write(base+0x8E72F8,&output,4)&&
          WorkshopFieldFixture::Write(base+0x2F0228,returnAfterRead,sizeof(returnAfterRead)),"private fixture retains the real native fread caller and importer");
    Check(stream&&RejectedNativeRead(stream,reinterpret_cast<void*>(base+0x2F0213))==0,
          "unknown save identity is rejected by the actual native I/O adapter");
    if(stream)close(stream);
    Check(!Runtime::Capture(state,generation),"a rejected native read cannot mint a fresh admitted Arcana session");
#endif
    Runtime::Stop();Check(!Runtime::Capture(state,generation),"logical stop closes new equipment mutations");
    Check(Elemental::provider.load()==nullptr&&!Elemental::Read(0,extra),"logical stop retires the external-element provider with the session");
    if(development){
        FfxHooks::NativeSaveEvents::WriteTransaction stopped;
        Check(FfxHooks::NativeSaveEvents::ProjectionRequired()&&FfxHooks::NativeSaveEvents::ProjectWrite(path.c_str(),save.data(),projected.data(),save.size(),stopped),"logical stop retains projection while temporary native fields remain");
        unsigned savedMaximum=0,liveMaximum=0;std::memcpy(&savedMaximum,projected.data()+0x5630,4);std::memcpy(&liveMaximum,save.data()+0x5630,4);
        Check(savedMaximum==9999&&liveMaximum==23455,"post-stop serialization removes the card maximum only from the copied native image");
        FfxHooks::RonsoPool::SealSave(projected);
        Check(FfxHooks::NativeSaveEvents::PrepareWrite(path.c_str(),projected.data(),projected.size(),stopped),"post-stop final metadata still prepares");
        FfxHooks::NativeSaveEvents::FinishWrite(stopped,nullptr,0,false);
        Check(!std::filesystem::exists(Store::Extension(path,".arcana.pending.v1"))&&std::filesystem::exists(Store::Extension(path,".arcana.v1")),"failed native completion aborts its journal and preserves the prior committed collection");
    }
    std::printf("ArcanaRuntimeRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
