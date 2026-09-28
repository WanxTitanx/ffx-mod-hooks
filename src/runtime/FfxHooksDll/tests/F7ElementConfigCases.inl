void TestHookElementConfiguration(){
    using namespace FfxHooks;
    const char input[]=R"({"diff_enabled":true,"diff_elemWeak":255,"diff_elemExtra":[{"key":"mod.aether","affinity":1},{"key":"mod.void","affinity":3}],"diffByArea":true,"areas":[{"enabled":true,"fieldRow":7,"elemExtra":[{"key":"mod.void","affinity":2}]}]})";
    DifficultyConfig config{};auto parsed=ParseConfig(input,sizeof(input)-1,&config);
    Expect(parsed.code==ConfigCode::Ok,"hook-only affinity configuration parses beside the full native BYTE");
    Expect(config.global.elemWeak==255&&std::strcmp(config.global.elemExtra[0].key.data(),"mod.aether")==0&&config.global.elemExtra[1].affinity==F7Elements::Affinity::Absorb,
        "hook elements persist by stable key, separately from the eight native bits");
    Expect(config.areaCount==1&&config.areas[0].preset.elemExtra[0].affinity==F7Elements::Affinity::Resist,
        "area-specific hook affinities use the same configuration contract");
    std::array<char,kMaxJsonBytes> bytes{};size_t length=0;
    Expect(SerializeConfig(config,bytes.data(),bytes.size(),&length).code==ConfigCode::Ok,"extended affinity configuration serializes");
    DifficultyConfig restored{};Expect(ParseConfig(bytes.data(),length,&restored).code==ConfigCode::Ok&&
        std::strcmp(restored.global.elemExtra[1].key.data(),"mod.void")==0&&restored.global.elemExtra[1].affinity==F7Elements::Affinity::Absorb,
        "both hook affinities survive save and reload without truncating to BYTE");
    for(const char* bad:{
        R"({"diff_elemExtra":[{"key":"mod.a","affinity":1},{"key":"mod.a","affinity":2}]})",
        R"({"diff_elemExtra":[{"key":"bad key","affinity":1}]})",
        R"({"diff_elemExtra":[{"key":"mod.a","affinity":4}]})",
        R"({"diff_elemExtra":[{"key":"mod.a","key":"mod.b","affinity":1}]})",
        R"({"diff_elemExtra":[{"key":"mod.a","affinity":1},{"key":"mod.b","affinity":1},{"key":"mod.c","affinity":1}]})"})
        Expect(ParseConfig(bad,std::strlen(bad),&restored).code!=ConfigCode::Ok,"invalid, duplicate or excess external affinities are rejected");
    F7Elements::Selection selection{};selection.extra=config.global.elemExtra;selection.weak=0x20;selection.resist=0x40;
    std::int32_t value=10000;
    Expect(F7Elements::Base(selection,0,"mod.aether",value)&&value==15000,"external weak affinity resolves from its exact key");
    Expect(F7Elements::Base(selection,0,"mod.void",value)&&value==-10000,"external absorb retains its signed multiplier");
    Expect(!F7Elements::Base(selection,0,"mod.unknown",value),"unrelated packs do not inherit another key's setting");
    Expect(F7Elements::Base(selection,0x20,"mod.void",value)&&value==15000&&F7Elements::Base(selection,0x40,"mod.aether",value)&&value==5000,
        "both native Customs stay independent of external keys and each other");
    Expect(!F7Elements::Base(selection,0x100,"mod.aether",value),"a hook-only element is never written as a ninth native bit");
    const auto area=SelectElements(config,true,7),global=SelectElements(config,true,99),invalid=SelectElements(config,false,7);
    Expect(area.weak==0&&F7Elements::Base(area,0,"mod.void",value)&&value==5000,
        "area affinity replaces the global selection instead of adding another modifier");
    Expect(global.weak==255&&F7Elements::Base(global,0,"mod.void",value)&&value==-10000,
        "an unmatched area uses the same global fallback as native Difficulty");
    Expect(!F7Elements::Base(invalid,0,"mod.void",value)&&invalid.weak==0,"invalid configuration cannot publish external effects");
    config.global.enabled=false;const auto disabled=SelectElements(config,true,99);
    Expect(!F7Elements::Base(disabled,0,"mod.void",value)&&disabled.weak==0,"Difficulty OFF removes its external overlay without rewriting native data");
    F7Elements::AppliedSelection applied;auto pending=global;F7Elements::Selection observed{};
    applied.Begin(7,pending);applied.Admit(0,0x10000,0x1156);
    pending.extra[1].affinity=F7Elements::Affinity::Weak;
    Expect(applied.Read(7,18,0x10000,0x1156,observed)&&F7Elements::Base(observed,0,"mod.void",value)&&value==-10000,
        "a new requested or saved draft does not bypass the native Apply boundary");
    Expect(!applied.Read(8,18,0x10000,0x1156,observed)&&!applied.Read(7,18,0x10000,0x1157,observed)&&!applied.Read(7,18,0x20000,0x1156,observed),
        "generation changes and actor reuse invalidate the published external overlay");
    applied.Begin(7,pending);applied.Admit(0,0x10000,0x1156);
    Expect(applied.Read(7,18,0x10000,0x1156,observed)&&F7Elements::Base(observed,0,"mod.void",value)&&value==15000,
        "a successful subsequent Apply publishes the new selection");
    applied.Clear();Expect(!applied.Read(7,18,0x10000,0x1156,observed),"failed native transactions cannot expose a partially applied external selection");
}
