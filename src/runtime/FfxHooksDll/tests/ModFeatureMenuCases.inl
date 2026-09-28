// Jarvis-HOOK: independent F8 persisted gates, including failure and reload.
static void ModFeatureMenuCases(){
    using namespace FfxHooks;
    const char* const keys[]={"elemental.core","elemental.tactics","elemental.gravity","elemental.magic_bdl","spira.enabled","aeon_ascension.enabled"};
    EquipmentMenu::StopReady();Config::ResetForTests();
    Config::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-mod-flags.ini");
    f8AllowWrite=true;f8Writes=0;Config::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});
    for(unsigned selected=0;selected<6;++selected){
        const auto* flag=FindF8Flag(keys[selected]);
        Check(flag!=nullptr,"every mod mode has its own F8 identity");if(!flag)continue;
        Check(!flag->gate.defaultValue&&flag->activation==F8Activation::RestartRequired&&flag->applyMode==F8ApplyMode::None,
              "mod gates are default-OFF and never claim live hook installation");
        Check(SetF8FlagValue(*flag,true).code==F8EditCode::Saved&&ResolveF8Flag(*flag).value,"F8 saves the selected mod gate");
        for(unsigned i=0;i<6;++i){const auto* other=FindF8Flag(keys[i]);
            Check(other&&ResolveF8Flag(*other).value==(selected==i),"one feature never enables the other mod modes implicitly");}
        const auto saved=f8Saved;Config::LoadTextForTests(saved.c_str(),"C:\\private-mod-flags.ini");
        Check(ResolveF8Flag(*flag).value,"persisted independent selection survives reload");
        f8AllowWrite=false;
        Check(SetF8FlagValue(*flag,false).code==F8EditCode::PersistFailed&&ResolveF8Flag(*flag).value,"failed disk update preserves the requested feature");
        f8AllowWrite=true;
        Check(SetF8FlagValue(*flag,false).code==F8EditCode::Saved&&!ResolveF8Flag(*flag).value,"OFF persists without destroying another setting");
    }
    Config::ResetForTests();
}
