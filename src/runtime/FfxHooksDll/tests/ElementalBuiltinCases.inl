#include "../hooks/ElementBuiltinPack.h"
#include <filesystem>
#include <fstream>

static bool PrepareBuiltinCases(std::uintptr_t base,const char* directory,bool all){
    namespace C=FfxHooks::Config;
    const auto root=std::filesystem::u8path(directory);std::filesystem::create_directories(root);
    const auto ini=(root/"hooks.ini").u8string();const auto manifest=root/"elemental-pack.json";
    const std::string gates=all?
        "[f8_authority]\nelemental_core=1\nelemental_tactics=1\nelemental_gravity=1\nelemental_magic_bdl=1\n[elemental]\ncore=1\ntactics=1\ngravity=1\nmagic_bdl=1\n":
        "[f8_authority]\nelemental_core=1\n[elemental]\ncore=1\n";
    C::LoadTextForTests((gates+"pack=missing-custom.json\n").c_str(),ini.c_str());
    Check(!E::Prepare(base,false,Log)&&E::RuntimeState().code==E::RuntimeCode::PackMissing,
          "an explicit missing package is not replaced with built-in definitions");
    {std::ofstream output(manifest,std::ios::binary);output<<"{broken";}
    C::LoadTextForTests(gates.c_str(),ini.c_str());
    Check(!E::Prepare(base,false,Log)&&E::RuntimeState().code==E::RuntimeCode::PackInvalid,
          "a malformed existing default package remains an error");
    std::filesystem::remove(manifest);
    Check(!E::Prepare(0,false,Log)&&E::RuntimeState().code==E::RuntimeCode::Unsupported,
          "built-in definitions do not bypass executable/profile admission");
    const bool prepared=E::Prepare(base,false,Log);
    const auto menu=FfxHooks::ElementMenu::Read();
    Check(prepared&&menu[8].available&&menu[9].available&&
          !std::strcmp(menu[8].key,"hook.custom03")&&!std::strcmp(menu[9].key,"hook.custom04"),
          "Core publishes both built-in custom identities without a pack file");
    Check(!std::filesystem::exists(manifest),"built-in preparation never invents an on-disk asset pack");
    return prepared;
}
static void BuiltinCases(std::uintptr_t base,std::vector<unsigned char>& actors,std::vector<unsigned char>& bank){
    namespace C=FfxHooks::Config;namespace F=FfxHooks::F7Elements;
    C::LoadTextForTests("[f8_authority]\nelemental_core=1\n[elemental]\ncore=1\n[element_scan]\nother_enabled=1\n", "C:\\private-built-in-view.ini");
    FfxHooks::ElementalScanView::Snapshot scan{};
    Check(E::DescriptorCount()==10&&FfxHooks::ElementalScanView::Capture(18,0,scan)&&scan.count==10,
          "the actual numerical provider exposes ten built-in definitions");
    Check(!std::strcmp(scan.rows[6].label,"Earth")&&!std::strcmp(scan.rows[8].label,"Poison")&&scan.rows[8].effectiveBp==10000,
          "built-in element names follow the approved roles and remain initially neutral");
    const auto originalActors=actors,originalBank=bank;
    std::snprintf(coreDifficulty.extra[0].key.data(),coreDifficulty.extra[0].key.size(),"hook.custom03");
    std::snprintf(coreDifficulty.extra[1].key.data(),coreDifficulty.extra[1].key.size(),"hook.custom04");
    coreDifficulty.extra[0].affinity=F::Affinity::Weak;coreDifficulty.extra[1].affinity=F::Affinity::Absorb;
    const F::Provider difficulty{CoreDifficultySelection};Check(F::Register(&difficulty),"F7 can supply applied built-in affinities");
    Check(FfxHooks::ElementalScanView::Capture(18,0,scan)&&scan.rows[8].baseBp==15000&&scan.rows[9].effectiveBp==-10000,
          "F7 custom weakness and absorption reach the real Scan resolver without an external package");
    C::LoadTextForTests("[f8_authority]\nelemental_core=1\n[elemental]\ncore=1\n[element_scan]\nother_enabled=1\n[element_names]\nnative_10=Radiance\nhook.custom03=Aether\nhook.custom04=Void\n", "C:\\private-built-in-view.ini");
    const auto menu=FfxHooks::ElementMenu::Read();
    Check(!std::strcmp(menu[4].label,"Radiance")&&!std::strcmp(menu[8].label,"Aether")&&!std::strcmp(menu[8].key,"hook.custom03"),
          "F7 receives aliases without changing its persisted element keys");
    Check(FfxHooks::ElementalScanView::Capture(18,0,scan)&&!std::strcmp(scan.rows[4].label,"Radiance")&&
          !std::strcmp(scan.rows[8].label,"Aether")&&!std::strcmp(scan.rows[9].label,"Void")&&scan.rows[8].effectiveBp==15000,
          "the native numerical Scan resolves the same aliases and preserves F7's affinity value");
    E::ElementalView canonical{};Check(E::ReadElement(18,4,canonical)&&!std::strcmp(canonical.label,"Holy"),
          "canonical package labels remain intact for non-UI data consumers");
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+0x38E680);
    std::array<unsigned char,44> info{};amount=450000;ceiling=99999;
    Check(producer(0,actors.data(),18,actors.data()+18*0xF90,bank.data()+20+96*64,0x3040,info.data(),0,0,0,0)==99999,
          "creating unused elements never assigns them or new caps to an unbound native command");
    Check(bank==originalBank&&actors==originalActors,"naming and external affinities preserve native actor, command and item bytes");
    F::Unregister(&difficulty);
    Check(FfxHooks::ElementalScanView::Capture(18,0,scan)&&scan.rows[8].effectiveBp==10000&&scan.rows[9].effectiveBp==10000,
          "retiring Difficulty returns the two unused elements to neutral");
    E::RequestStop();Check(!FfxHooks::ElementalScanView::Capture(18,0,scan)&&!FfxHooks::ElementMenu::Read()[8].available,
          "stop retires custom gameplay/presentation admission while keeping names as preferences");
}
