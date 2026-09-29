// Jarvis-HOOK: actual loaded-bank readers and the native producer entry in a
// private PE. Its formula endpoint supplies precap amounts; the Nova suite
// separately executes the x86 clamp graph. This is not a live-game test.
#include <cstdio>
#if __has_include("../hooks/ElementalRuntime.h")
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/ElementalRuntime.h"
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/EquipmentWorkshopStore.h"
#include "../hooks/NovaSuperDamageHook.h"
#include "../hooks/SharedDamageRuntime.h"
#include "../hooks/CombatExtensionBus.h"
#include <array>
#include <cstring>
#include <string>
#include <vector>
namespace E=FfxHooks::ElementalDominion;
namespace W=FfxHooks::EquipmentWorkshop;
namespace B=FfxHooks::CombatExtensions;
static unsigned checks=0,failures=0,calls=0,component=3;
static int amount=450000,ceiling=99999;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
static void Log(const char* text){std::fputs(text,stdout);}
static void W16(unsigned char* p,unsigned v){p[0]=static_cast<unsigned char>(v);p[1]=static_cast<unsigned char>(v>>8);}
static void W32(unsigned char* p,unsigned v){std::memcpy(p,&v,4);}
static std::string Hash(const void* bytes,std::size_t size){
    W::Hash hash{};if(!W::Fingerprint(bytes,size,hash))return {};
    constexpr char hex[]="0123456789abcdef";std::string result;
    for(auto b:hash){result.push_back(hex[b>>4]);result.push_back(hex[b&15]);}return result;
}
static std::string Pack(const std::vector<unsigned char>& bank){
    std::string json=R"({"schema":"ffx.mod007.elements.v1","package_id":"tests.magic","version":1,
        "exe_sha256":"78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced",
        "requires":["mod007.registry.v1","mod007.context.v1","mod007.spell-cap.v1"],
        "fallback":"native-unmodified","elements":[)";
    for(unsigned i=0;i<8;++i){if(i)json+=",";const auto key="native.e"+std::to_string(i);
        json+="{\"key\":\""+key+"\",\"label_key\":\"label."+key+"\",\"label\":\"Element\",\"rgb\":16777215,\"native_bit\":"+std::to_string(1u<<i)+"}";}
    json+="],\"banks\":[{\"key\":\"table.command\",\"kind\":\"command\",\"locale\":\"us\",\"bytes\":"+
        std::to_string(bank.size())+",\"sha256\":\""+Hash(bank.data(),bank.size())+
        "\",\"sections\":[{\"first\":0,\"last\":319,\"width\":96,\"offset\":20}]}],\"commands\":[";
    for(unsigned i=64;i<68;++i){if(i!=64)json+=",";
        json+="{\"key\":\"spell.test"+std::to_string(i)+"\",\"bank\":\"table.command\",\"index\":"+
            std::to_string(i)+",\"row_sha256\":\""+Hash(bank.data()+20+96*i,96)+"\",\"elements\":[],\"spell\":\""+
            (i==65?"fury":i==66?"none":"native_magic")+"\"}";}
    return json+"],\"profiles\":[],\"equipment\":[]}";
}
static unsigned __cdecl Endpoint(unsigned user,void*,unsigned,void*,const void*,unsigned command,
                                 void*,unsigned,unsigned,unsigned,unsigned){
    ++calls;return static_cast<unsigned>(B::UpperDamage(amount,ceiling,user,command,component,false));
}
#include "ElementalCoreCases.inl"
#include "ElementalTacticsCases.inl"
#include "ElementalMonsterCases.inl"
#include "ElementalGravityCases.inl"
#include "ElementalEquipmentCases.inl"
#include "ElementalBuiltinCases.inl"
#include "ElementalArcanaCases.inl"
#ifdef FFXHOOKS_NUL_COMPOSITION
#include "../hooks/NulWardHook.h"
#include "NulWardCompositionCases.inl"
#endif
int main(int argc,char** argv){
#ifdef FFXHOOKS_NUL_COMPOSITION
    if(argc!=4)return 2;
    const bool nulFirst=std::strcmp(argv[3],"nul-first")==0;
    constexpr unsigned CommandCount=374;
#else
    constexpr unsigned CommandCount=320;
    if(argc!=4||(std::strcmp(argv[3],"off")&&std::strcmp(argv[3],"magic")&&std::strcmp(argv[3],"core")&&std::strcmp(argv[3],"tactics")&&std::strcmp(argv[3],"monster")&&std::strcmp(argv[3],"gravity")&&std::strcmp(argv[3],"gravity-override")&&std::strcmp(argv[3],"equipment")&&std::strcmp(argv[3],"builtin")&&std::strcmp(argv[3],"builtin-all")&&std::strcmp(argv[3],"arcana")&&std::strcmp(argv[3],"arcana-pack")&&std::strcmp(argv[3],"arcana-other")&&std::strcmp(argv[3],"arcana-native-exact")))return 2;
#endif
    const bool equipment=std::strcmp(argv[3],"equipment")==0;
    const bool arcanaNativeExact=std::strcmp(argv[3],"arcana-native-exact")==0;
    const bool arcanaPack=arcanaNativeExact||std::strcmp(argv[3],"arcana-pack")==0;
    const bool arcanaOther=std::strcmp(argv[3],"arcana-other")==0;
    const bool arcana=arcanaPack||arcanaOther||std::strcmp(argv[3],"arcana")==0;
    const bool builtinAll=std::strcmp(argv[3],"builtin-all")==0;
    const bool builtin=builtinAll||std::strcmp(argv[3],"builtin")==0||(arcana&&!arcanaPack&&!arcanaOther);
    const bool gravityOverride=std::strcmp(argv[3],"gravity-override")==0;
    const bool gravity=gravityOverride||std::strcmp(argv[3],"gravity")==0;
    const bool tactics=std::strcmp(argv[3],"tactics")==0
#ifdef FFXHOOKS_NUL_COMPOSITION
        ||std::strcmp(argv[3],"nul-first")==0||std::strcmp(argv[3],"elemental-first")==0
#endif
        ;
    const bool monster=std::strcmp(argv[3],"monster")==0;
    const bool core=equipment||monster||tactics||builtin||arcana||std::strcmp(argv[3],"core")==0;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(image),"private PE relocates without running its entry point");
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    std::vector<unsigned char> actors(31*0xF90),bank(20+CommandCount*96+1),language(28);
    const auto ap=reinterpret_cast<std::uintptr_t>(actors.data()),bp=reinterpret_cast<std::uintptr_t>(bank.data());
    const auto lp=reinterpret_cast<std::uintptr_t>(language.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD334CC),&ap,4);
    std::memcpy(reinterpret_cast<void*>(base+0xD2A92C),&bp,4);
    std::memcpy(reinterpret_cast<void*>(base+0x8DED48),&lp,4);W32(language.data()+4,1);
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=1;
    W16(bank.data(),1);W16(bank.data()+10,CommandCount-1);W16(bank.data()+12,96);W16(bank.data()+14,CommandCount*96);W32(bank.data()+16,20);
    for(unsigned i=0;i<31;++i){auto* a=actors.data()+i*0xF90;W16(a+0xC,i);W16(a+0xE,i<18?i:0x1000+i);a[0xDC8]=1;W32(a+0x5D0,1000);}
    for(unsigned i=64;i<68;++i){auto* r=bank.data()+20+96*i;r[0x19]=255;r[0x20]=2;r[0x23]=1;r[0x28]=4;r[0x2A]=16;}
    bank[20+96*65+0x20]=0;bank[20+96*66+0x20]=1;bank[20+96*67+0x20]=0x12;
    if(core)for(unsigned i=80;i<=(tactics?100u:94u);++i){auto* row=bank.data()+20+96*i;
        row[0x19]=255;row[0x20]=2;row[0x23]=i>=96?0:1;row[0x28]=4;row[0x2A]=16;
        row[0x2D]=static_cast<unsigned char>(i<88?1u<<(i-80):i==94?1:0);}
    std::vector<unsigned char> monsterFile(0x100);
    W32(monsterFile.data()+4,0x30);W32(monsterFile.data()+8,0x40);W32(monsterFile.data()+12,0x60);
    W32(monsterFile.data()+0x20,static_cast<unsigned>(monsterFile.size()));monsterFile[0x60]=7;
    if(monster||gravity){const auto file=reinterpret_cast<std::uintptr_t>(monsterFile.data());
        W16(actors.data()+18*0xF90+0xE,4438);std::memcpy(actors.data()+18*0xF90+0x48,&file,4);}
    if(gravity)for(unsigned id=110;id<114;++id){auto* row=bank.data()+20+96*id;
        row[0x19]=255;row[0x20]=id==111?0:2;row[0x23]=id==113?2:1;row[0x28]=5;row[0x2A]=4;}
    std::vector<unsigned char> abilityBank(20+201*108+1);
    W32(abilityBank.data(),1);W16(abilityBank.data()+10,200);W16(abilityBank.data()+12,108);
    W16(abilityBank.data()+14,201*108);W32(abilityBank.data()+16,20);
    if(equipment){const auto address=reinterpret_cast<std::uintptr_t>(abilityBank.data());
        std::memcpy(reinterpret_cast<void*>(base+0xD2A944),&address,4);
        W16(reinterpret_cast<unsigned char*>(base+0xD2A970),static_cast<unsigned>(abilityBank.size()));}
    if(arcana)CardRows(bank);
    auto json=arcanaPack?CardPack(bank,arcanaNativeExact):builtin?E::BuiltinPackText():equipment?EquipmentPack(bank,abilityBank):gravity?GravityPack(bank,monsterFile,gravityOverride):monster?MonsterPack(bank,monsterFile):tactics?TacticsPack(bank):core?CorePack(bank):Pack(bank);
#ifdef FFXHOOKS_NUL_COMPOSITION
    json=NulCompositionPack(bank);
#endif
    E::RuntimeOptions options{};options.magicBdl=!core&&!gravity;options.core=core;options.tactics=tactics;options.gravity=gravity;
    unsigned char original[16]{};std::memcpy(original,reinterpret_cast<void*>(base+0x38E680),16);
    Check(!E::PrepareText(base,{},json,false,Log)&&!B::Required(),"OFF publishes no shared producer request");
    Check(!E::PrepareText(base,options,json,true,Log)&&!B::Required(),"validate-only cannot arm the feature");
    Check(!std::memcmp(original,reinterpret_cast<void*>(base+0x38E680),16),"OFF and validate-only preserve native bytes");
#ifdef FFXHOOKS_NUL_COMPOSITION
    FfxHooks::NulWardInstallOptions nulOptions{};nulOptions.nativeSlots=true;
    nulOptions.allElements=true;
    if(nulFirst)Check(FfxHooks::InstallNulWardHook(base,true,false,Log,&nulOptions).ok,"NulWard can prepare before Elemental");
#endif
    if(!std::strcmp(argv[3],"off")){
        std::printf("ELEMENTAL_RUNTIME_OFF %u/%u passed\n",checks-failures,checks);return failures?1:0;
    }
    Check((builtin?PrepareBuiltinCases(base,argv[2],builtinAll):E::PrepareText(base,options,json,false,Log))&&B::Required(),"selected definitions prime shared native consumers");
    Check(FfxHooks::InstallNovaSuperDamageHook(base,false,false,false,Log).ok,"the existing clamp owner accepts the finite-policy request");
    const std::wstring directory(argv[2],argv[2]+std::strlen(argv[2]));
    Check(W::StartForTests(base,false,directory.c_str(),Log)&&W::CombatProducerReady()&&!W::Requested(),"shared producer works with Workshop gameplay OFF");
    W::DamageProducerForTests(reinterpret_cast<void*>(&Endpoint));
    Check(E::Activate(),"activation requires both actual shared native owners");E::TickMainThread();
    Check(E::RuntimeState().code==E::RuntimeCode::Ready,"loaded bank and native locale pass SHA-256 admission");
#ifdef FFXHOOKS_NUL_COMPOSITION
    if(!nulFirst)Check(FfxHooks::InstallNulWardHook(base,true,false,Log,&nulOptions).ok,"NulWard can prepare after Elemental");
    Check(FfxHooks::SharedAction::Start(base),"one shared result owner survives both initialization orders");
    if(failures)return 1;
    if(tactics){NulCompositionCases(base,actors,bank);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("NUL_WARD_COMPOSITION %s %u/%u passed\n",argv[3],checks-failures,checks);return failures?1:0;}
#endif
    if(failures)return 1;
    if(arcana){ArcanaCases(base,actors,bank,arcanaPack,arcanaOther,arcanaNativeExact);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("ELEMENTAL_RUNTIME_ARCANA %s %u/%u passed\n",argv[3],checks-failures,checks);return failures?1:0;}
    if(builtin){BuiltinCases(base,actors,bank);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("ELEMENTAL_RUNTIME_BUILTIN %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    if(equipment){EquipmentCases(base,actors,bank,abilityBank);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("ELEMENTAL_RUNTIME_EQUIPMENT %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    if(gravity){GravityCases(base,actors,bank,gravityOverride);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("ELEMENTAL_RUNTIME_GRAVITY %s %u/%u passed\n",argv[3],checks-failures,checks);return failures?1:0;}
    if(monster){MonsterCases(base,actors,bank,monsterFile);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("ELEMENTAL_RUNTIME_MONSTER %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    if(tactics){TacticsCases(base,actors,bank);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("ELEMENTAL_RUNTIME_TACTICS %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    if(core){CoreCases(base,actors,bank);W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
        std::printf("ELEMENTAL_RUNTIME_CORE %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    auto* source=actors.data();auto* target=actors.data()+18*0xF90;std::array<unsigned char,44> info{};
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+0x38E680);
    const auto hit=[&](unsigned id,const void* row){return static_cast<int>(producer(0,source,18,target,row,id,info.data(),0,0,0,0));};
    auto* magic=bank.data()+20+96*64;
    Check(hit(0x3040,magic)==450000&&calls==1,"producer preserves an eligible precap amount without multiplying it");
    amount=1200000;Check(hit(0x3040,magic)==999999,"eligible magic has a finite six-digit maximum");
    ceiling=9999;Check(hit(0x3040,magic)==9999,"missing or suppressed BDL is not granted");ceiling=99999;amount=450000;
    Check(hit(0x3041,bank.data()+20+96*65)==450000,"explicit Fury does not need the ordinary magic bit");
    Check(hit(0x3042,bank.data()+20+96*66)==99999,"physical commands do not inherit a spell classification");
    Check(hit(0x3043,bank.data()+20+96*67)==99999,"intentional healing is excluded");
    component=2;Check(hit(0x3040,magic)==99999,"MP keeps its native limit");
    component=1;Check(hit(0x3040,magic)==99999,"CTB keeps its native limit");component=3;
    amount=-500;Check(hit(0x3040,magic)==-500,"signed absorption is unchanged");amount=450000;
    Check(hit(0x3040,bank.data()+20+96*65)==99999,"an ID cannot borrow another command's address");
    magic[0x28]=6;Check(hit(0x3040,magic)==99999,"changed row bytes immediately disable their binding");magic[0x28]=4;
    W32(language.data()+4,0);Check(hit(0x3040,magic)==99999,"language changes invalidate admission before a new tick");
    E::TickMainThread();Check(E::RuntimeState().code==E::RuntimeCode::DataMismatch,"a different locale remains unavailable");
    W32(language.data()+4,1);E::TickMainThread();Check(hit(0x3040,magic)==450000,"matching restored data can be admitted again");
    auto replacement=bank;const auto rp=reinterpret_cast<std::uintptr_t>(replacement.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A92C),&rp,4);
    Check(hit(0x3040,magic)==99999,"old row pointers cannot borrow a newly loaded bank");
    E::TickMainThread();Check(hit(0x3040,replacement.data()+20+96*64)==450000,"fresh admission uses the replacement bank's real address");
    Check(B::currentDamage==nullptr,"every callback retires its TLS context");
    E::RequestStop();Check(hit(0x3040,replacement.data()+20+96*64)==99999&&!B::Required(),"stop restores native limits");
    W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(Log);
    std::printf("ELEMENTAL_RUNTIME_MAGIC %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production ElementalRuntime.h is missing");return 1;}
#endif
