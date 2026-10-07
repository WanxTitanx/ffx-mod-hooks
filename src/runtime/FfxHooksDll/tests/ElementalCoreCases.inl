// Jarvis-HOOK: ten descriptors pass through the actual affinity and Nul entries.
#include "../hooks/ElementalScanView.h"
#include "../hooks/F7ElementAffinities.h"
#include "../hooks/ElementMenuCatalog.h"
#include "../shared/Config.h"
static FfxHooks::F7Elements::Selection coreDifficulty{};
static bool CoreDifficultySelection(unsigned,std::uintptr_t,FfxHooks::F7Elements::Selection& out) noexcept {out=coreDifficulty;return true;}
static std::uintptr_t coreImage=0;
static std::string CorePack(const std::vector<unsigned char>& bank){
    std::string json=R"({"schema":"ffx.mod007.elements.v1","package_id":"tests.tenfold","version":1,
      "exe_sha256":")" FFXHOOKS_FIXTURE_SHA256 R"(",
      "requires":["mod007.registry.v1","mod007.affinity.v1","mod007.context.v1"],
      "fallback":"native-unmodified","elements":[)";
    for(unsigned i=0;i<10;++i){if(i)json+=",";const auto key="tests.e"+std::to_string(i);
        json+="{\"key\":\""+key+"\",\"label_key\":\"label."+key+"\",\"label\":\"Element "+std::to_string(i)+
            "\",\"rgb\":16777215,\"native_bit\":"+std::to_string(i<8?1u<<i:0)+"}";}
    json+="],\"banks\":[{\"key\":\"table.command\",\"kind\":\"command\",\"locale\":\"us\",\"bytes\":"+
        std::to_string(bank.size())+",\"sha256\":\""+Hash(bank.data(),bank.size())+
        "\",\"sections\":[{\"first\":0,\"last\":319,\"width\":96,\"offset\":20}]}],\"commands\":[";
    for(unsigned i=80;i<=94;++i){if(i!=80)json+=",";
        const char* policy=i==91?"split_weighted":i==92?"lowest_exposure":"highest_exposure";
        json+="{\"key\":\"spell.test"+std::to_string(i)+"\",\"bank\":\"table.command\",\"index\":"+
            std::to_string(i)+",\"row_sha256\":\""+Hash(bank.data()+20+96*i,96)+"\",\"elements\":[";
        if(i<90)json+="{\"key\":\"tests.e"+std::to_string(i-80)+"\",\"weight\":1}";
        else if(i!=93)json+="{\"key\":\"tests.e0\",\"weight\":1},{\"key\":\"tests.e"+std::string(i==94?"8":"1")+"\",\"weight\":1}";
        json+="],\"policy\":\""+std::string(policy)+"\"}";
    }
    json+=R"(],"profiles":[{"key":"character.control","kind":"character","id":1,"affinities":[)";
    for(unsigned i=0;i<10;++i){if(i)json+=",";
        json+="{\"key\":\"tests.e"+std::to_string(i)+"\",\"base_bp\":"+std::to_string(i==1?-10000:15000)+"}";}
    return json+"]}],\"equipment\":[]}";
}
static unsigned __cdecl CoreEndpoint(unsigned,void*,unsigned target,void* actor,const void* command,
                                     unsigned,void* info,unsigned,unsigned,unsigned,unsigned){
    const auto* row=static_cast<const unsigned char*>(command);
    if(!(row[0x23]&1))return 0;
    using Affinity=int(__cdecl*)(const unsigned char*,const unsigned char*,unsigned,int);
    using Nullify=int(__cdecl*)(unsigned,unsigned,void*);
    const auto value=reinterpret_cast<Affinity>(coreImage+(::FfxHooks::ExecutableProfile::Rva<0x38A420>()))(static_cast<const unsigned char*>(actor),row,row[0x2D],amount);
    const int blocked=reinterpret_cast<Nullify>(coreImage+(::FfxHooks::ExecutableProfile::Rva<0x38C070>()))(target,row[0x2D],info);
    if(blocked==-1)static_cast<unsigned char*>(info)[1]=2;
    return static_cast<unsigned>(blocked==-1?0:value);
}
static void CoreCases(std::uintptr_t base,std::vector<unsigned char>& actors,std::vector<unsigned char>& bank){
    FfxHooks::Config::LoadTextForTests("[element_scan]\nother_enabled=1\n","C:\\private-core-visual.ini");
    coreImage=base;amount=1000;W::DamageProducerForTests(reinterpret_cast<void*>(&CoreEndpoint));
    std::array<unsigned char,44> info{};
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+(::FfxHooks::ExecutableProfile::Rva<0x38E680>()));
    const auto hit=[&](unsigned id,unsigned target){return static_cast<int>(producer(0,actors.data(),target,
        actors.data()+target*0xF90,bank.data()+20+96*id,0x3000+id,info.data(),0,0,0,0));};
    for(unsigned i=0;i<10;++i)Check(hit(80+i,1)==(i==1?-1000:1500),"all ten stable descriptor bindings reach native affinity resolution");
    Check(hit(90,1)==1500,"highest-exposure mixes weakness and absorption once");
    Check(hit(91,1)==250,"split-weighted combines signed portions before a single rounding");
    Check(hit(92,1)==-1000,"lowest-exposure chooses absorption");
    FfxHooks::ElementalScanView::Snapshot numerical{};
    Check(FfxHooks::ElementalScanView::Capture(1,0,numerical)&&numerical.count==10&&
          numerical.rows[0].baseBp==15000&&numerical.rows[1].effectiveBp==-10000,
          "the live Scan source exposes the same ten resolved values used by native damage");
    Check(hit(93,1)==1000,"an explicit empty list is not implicitly elemental");
    info[0xE]=2;
    Check(hit(80,1)==0&&info[0xE]==1,"one native Fire Nul charge nullifies one fully covered hit");
    Check(hit(94,1)==1500&&info[0xE]==1,"native-only Nul cannot nullify an uncovered ninth element or consume its charge");
    info={};actors[2*0xF90+0x5DD]=1;
    Check(hit(80,2)==1500,"actors without a custom profile use the actual native affinity baseline");
    Check(hit(88,2)==1000,"missing external affinity is explicitly neutral");
    const auto menu=FfxHooks::ElementMenu::Read();
    Check(menu[6].nativeBit==0x20&&menu[7].nativeBit==0x40&&std::strcmp(menu[8].key,"tests.e8")==0&&std::strcmp(menu[9].key,"tests.e9")==0,
          "F7 and Scan share eight native identities plus the two loaded external descriptors");
    std::snprintf(coreDifficulty.extra[0].key.data(),coreDifficulty.extra[0].key.size(),"tests.e8");
    coreDifficulty.extra[0].affinity=FfxHooks::F7Elements::Affinity::Weak;
    std::snprintf(coreDifficulty.extra[1].key.data(),coreDifficulty.extra[1].key.size(),"tests.e9");
    coreDifficulty.extra[1].affinity=FfxHooks::F7Elements::Affinity::Absorb;
    coreDifficulty.weak=0x20;coreDifficulty.resist=0x40;
    const FfxHooks::F7Elements::Provider difficultyProvider{CoreDifficultySelection};
    Check(FfxHooks::F7Elements::Register(&difficultyProvider),"the admitted Difficulty producer can publish one overlay");
    Check(hit(88,18)==1500&&hit(89,18)==-1000,"both external F7 affinities reach the actual native damage consumer");
    Check(hit(85,18)==1500&&hit(86,18)==500,"both native Custom affinities remain independent under the shared runtime");
    Check(hit(88,2)==1000,"Difficulty cannot grant enemy-only overrides to player actors");
    E::ElementalView enemy{};Check(E::ReadElement(18,9,enemy)&&enemy.baseBp==-10000&&enemy.effectiveBp==-10000,
          "the numerical Scan view reports the same enemy override used by damage");
    FfxHooks::F7Elements::Unregister(&difficultyProvider);
    Check(hit(88,18)==1000&&hit(89,18)==1000,"retiring Difficulty removes external overrides without touching monster data");
    FfxHooks::Config::LoadTextForTests("[element_scan]\nother_enabled=1\nhook.tests.e8.enabled=0\nhook.tests.e9.rgb=1193046\n","C:\\private-core-visual.ini");
    Check(FfxHooks::ElementalScanView::Capture(1,0,numerical)&&numerical.count==9&&numerical.rows[8].rgb==0x123456&&
          std::strcmp(numerical.rows[8].label,"Element 9")==0,"hook visibility and color apply to the real Scan source by stable key");
    Check(hit(88,1)==1500,"hiding a Scan element cannot disable its gameplay affinity");
    FfxHooks::Config::LoadTextForTests("[element_scan]\nother_enabled=1\n","C:\\private-core-visual.ini");
    W16(actors.data()+0xF90+0xE,2);
    Check(hit(88,1)==1000,"another canonical owner cannot borrow a stale character profile");W16(actors.data()+0xF90+0xE,1);
    amount=-1000;Check(hit(80,1)==-1500,"the elemental stage preserves signed arithmetic without manufacturing a second heal");
    amount=1000;const auto before=bank;bank[20+96*88+0x28]^=1;
    Check(hit(88,1)==1000,"changed command rows fall back without an external effect");bank=before;
    E::RequestStop();Check(hit(88,1)==1000,"stop restores the original native affinity route");
    Check(!FfxHooks::ElementalScanView::Capture(1,0,numerical),"stopped numerical presentation cannot retain stale runtime values");
    Check(info[0xE]==0&&B::currentDamage==nullptr,"transient context and native charges are left consistent");
}
