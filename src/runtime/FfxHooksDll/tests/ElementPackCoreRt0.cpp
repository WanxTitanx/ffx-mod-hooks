// Jarvis-HOOK: transactional manifest admission and stable native bindings.
#include <cstdio>
#include <string>
#include <utility>
#if __has_include("../hooks/ElementPackCore.h")
#include "../hooks/ElementPackCore.h"
namespace {
namespace E=FfxHooks::ElementalDominion;
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
std::string Replace(std::string text,const std::string& from,const std::string& to){
    const auto at=text.find(from);if(at==std::string::npos){++failures;return text;}
    text.replace(at,from.size(),to);return text;
}
std::string Fixture(){
    std::string text=R"({"schema":"ffx.mod007.elements.v1","package_id":"tests.tenfold","version":1,
      "exe_sha256":"78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced",
      "requires":["mod007.registry.v1","mod007.affinity.v1","mod007.context.v1","mod007.spell-cap.v1","mod007.tactics.v1","mod007.gravity.v1","mod007.equipment.v1"],
      "fallback":"native-unmodified","elements":[)";
    const char* keys[]={"ffx.fire","ffx.ice","ffx.thunder","ffx.water","ffx.holy","spira.earth","spira.wind","spira.dark","spira.poison","spira.gravity"};
    for(unsigned i=0;i<10;++i){if(i)text+=",";
        text+="{\"key\":\""+std::string(keys[i])+"\",\"label_key\":\"label."+keys[i]+"\",\"label\":\"Element "+std::to_string(i)+"\",\"rgb\":16777215,\"native_bit\":"+std::to_string(i<8?1u<<i:0)+"}";}
    text+=R"(],"banks":[{"key":"table.command","kind":"command","locale":"us","bytes":44558,
      "sha256":"db4c87f33f27a7df41bc8a520bc2246b664167319386fe99da9880ae06000429",
      "sections":[{"first":0,"last":319,"width":96,"offset":20}]}],
      "commands":[{"key":"spell.poison","bank":"table.command","index":66,
      "row_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "elements":[{"key":"spira.poison","weight":1}],"policy":"highest_exposure","spell":"native_magic"}],
      "profiles":[{"key":"enemy.demo","kind":"monster","id":342,"file_bytes":4096,
      "file_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      "imperil_limit":2,"imperil_resist_bp":5000,
      "affinities":[{"key":"spira.poison","base_bp":7500},{"key":"spira.gravity","base_bp":0,"locked":true}],
      "gravity":{"maximum_hp_divisor":16,"nonlethal":true,"override_native_immunity":false}}],
      "equipment":[]})";
    return text;
}
}
int main(){
    const auto base=Fixture();E::Pack pack{};E::PackProblem problem{};
    Check(E::LoadPack(base,E::KnownCapabilities,pack,problem),"complete ten-element manifest admits transactionally");
    Check(pack.registry.Size()==10&&pack.commands.size()==1&&pack.profiles.size()==1,"registry and binding counts are exact");
    const auto* command=pack.Command(0x3042);
    Check(command&&command->parts.size()==1&&pack.registry.At(command->parts[0].element)->key=="spira.poison","command id resolves a stable external key beyond byte masks");
    Check(pack.BoundRow(0x3042)==20+66*96&&pack.BoundRow(0x3FFF)==E::InvalidRow,"unknown native fallback indices are rejected");
    Check(pack.profiles[0].affinities[1].locked&&pack.profiles[0].imperilLimit==2,"affinity locks and boss stack caps remain separate");
    Check(pack.profiles[0].gravity.enabled&&!pack.profiles[0].gravity.overrideImmunity,"boss Gravity preserves native immunity by default");
    const std::pair<const char*,const char*> invalid[]={
      {"ffx.mod007.elements.v1","ffx.mod007.elements.v2"},
      {"tests.tenfold","not_namespaced"},{"\"version\":1","\"version\":0"},
      {"78ce3439","00000000"},{"native-unmodified","requires-installed-replacement"},
      {"mod007.tactics.v1","mod007.unknown.v9"},
      {"\"native_bit\":2}","\"native_bit\":1}"},
      {"\"native_bit\":128}","\"native_bit\":0}"},
      {"\"key\":\"spira.gravity\",\"label_key\"","\"key\":\"spira.poison\",\"label_key\""},
      {"\"rgb\":16777215","\"rgb\":16777216"},
      {"\"kind\":\"command\"","\"kind\":\"guess\""},
      {"\"width\":96","\"width\":92"},{"\"offset\":20","\"offset\":19"},
      {"\"last\":319","\"last\":4096"},{"\"bytes\":44558","\"bytes\":1000"},
      {"\"locale\":\"us\"","\"locale\":\"../us\""},
      {"db4c87f33f27a7df41bc8a520bc2246b664167319386fe99da9880ae06000429","NOT-A-HASH"},
      {"\"index\":66","\"index\":500"},{"\"bank\":\"table.command\"","\"bank\":\"table.missing\""},
      {"\"key\":\"spira.poison\",\"weight\"","\"key\":\"spira.missing\",\"weight\""},
      {"\"weight\":1","\"weight\":0"},{"\"weight\":1","\"weight\":1001"},
      {"\"policy\":\"highest_exposure\"","\"policy\":\"native_exact\""},
      {"\"spell\":\"native_magic\"","\"spell\":\"every_attack\""},
      {"\"base_bp\":7500","\"base_bp\":7400"},{"\"base_bp\":7500","\"base_bp\":27500"},
      {"\"imperil_limit\":2","\"imperil_limit\":5"},{"\"imperil_resist_bp\":5000","\"imperil_resist_bp\":10001"},
      {"\"maximum_hp_divisor\":16","\"maximum_hp_divisor\":8"},{"\"nonlethal\":true","\"nonlethal\":false"},
      {"\"kind\":\"monster\"","\"kind\":\"name_matches_boss\""},{"\"id\":342","\"id\":65535"},
      {"\"equipment\":[]","\"equipment\":[],\"unknown_field\":true"}
    };
    for(const auto& row:invalid){
      E::Pack unchanged=pack;const auto bad=Replace(base,row.first,row.second);
      Check(!E::LoadPack(bad,E::KnownCapabilities,unchanged,problem)&&problem.code!=E::PackError::None,"invalid schema reference range or rule rejects the entire pack");
      Check(unchanged.packageId==pack.packageId&&unchanged.registry.Size()==10&&unchanged.Command(0x3042),"failed admission preserves the previously resolved pack");
    }
    Check(!E::LoadPack(base,0,pack,problem)&&problem.code==E::PackError::Capability,"missing capabilities cannot silently degrade a bound spell");
    const auto effect=Replace(base,"\"spell\":\"native_magic\"",R"("spell":"native_magic","effects":[{"kind":"imperil","element":"spira.poison","stacks":1,"turns":3,"chance_bp":10000}])");
    Check(E::LoadPack(effect,E::KnownCapabilities,pack,problem)&&pack.commands[0].effects.size()==1,"bounded status binding admits without editing a native row");
    Check(!E::LoadPack(Replace(effect,"\"turns\":3","\"turns\":0"),E::KnownCapabilities,pack,problem),"zero duration cannot become a permanent accidental status");
    Check(!E::LoadPack(Replace(effect,"\"kind\":\"imperil\"","\"kind\":\"oil\""),E::KnownCapabilities,pack,problem),"unimplemented experimental status cannot be advertised");
    Check(!E::LoadPack(Replace(base,"\"requires\":[","\"requires\":[\"mod007.registry.v1\","),E::KnownCapabilities,pack,problem),"duplicate capability requirements are rejected");
    Check(E::LoadPack(base,E::KnownCapabilities,pack,problem)&&problem.code==E::PackError::None,"valid retry clears the prior diagnostic");
    const auto protectedAffinity=Replace(base,"\"base_bp\":7500",R"("base_bp":7500,"imperil_immune":true,"imperil_resist_bp":5000)");
    Check(E::LoadPack(protectedAffinity,E::KnownCapabilities,pack,problem),"a per-element status immunity and resistance are explicit independent metadata");
    Check(!E::LoadPack(Replace(protectedAffinity,"\"imperil_resist_bp\":5000","\"imperil_resist_bp\":10001"),E::KnownCapabilities,pack,problem),"per-element status resistance is bounded");
    Check(!E::LoadPack(Replace(protectedAffinity,"\"imperil_immune\":true","\"imperil_immune\":1"),E::KnownCapabilities,pack,problem),"status immunity is a strict boolean");
    const std::string equipmentBank=R"({"key":"table.autoability","kind":"autoability","locale":"us","bytes":20000,"sha256":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc","sections":[{"first":0,"last":174,"width":108,"offset":20}]},)";
    auto equipped=Replace(base,"\"banks\":[","\"banks\":["+equipmentBank);
    equipped=Replace(equipped,"\"equipment\":[]",R"("equipment":[{"key":"gear.external","bank":"table.autoability","index":160,"row_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","kind":"armor","owners":[1,8],"sos":true,"deltas":[{"key":"spira.poison","delta_bp":-2500}]}])");
    Check(E::LoadPack(equipped,E::KnownCapabilities,pack,problem)&&pack.equipment.size()==1,"external equipment metadata declares kind, canonical owners and a native SOS predicate");
    for(const auto& change:{std::pair<const char*,const char*>{"\"kind\":\"armor\"","\"kind\":\"cape\""},
        {"\"owners\":[1,8]","\"owners\":[]"},{"\"owners\":[1,8]","\"owners\":[1,1]"},
        {"\"owners\":[1,8]","\"owners\":[18]"},{"\"owners\":[1,8]","\"owners\":[-1]"}})
        Check(!E::LoadPack(Replace(equipped,change.first,change.second),E::KnownCapabilities,pack,problem),"unsupported equipment kind or malformed owner set rejects the pack");
    std::printf("ELEMENT_PACK_CORE_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production ElementPackCore.h is missing");return 1;}
#endif
