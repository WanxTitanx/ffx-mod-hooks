#include "../hooks/VanguardUiBridge.h"
static bool vgUiAllow=true;
static bool VgUiRead(FfxHooks::Vanguard::MappingState& out) noexcept {
    namespace V=FfxHooks::Vanguard;out={};out.ids=V::DefaultMapping();out.codes.fill(V::MappingCode::Valid);out.stamp=123;
    for(unsigned i=0;i<V::AbilityCount;++i){char key[96]{};std::snprintf(key,sizeof(key),"vanguard_ids.%s",V::Abilities[i].key);
        const auto value=FfxHooks::Config::ReadIntExact(key,135,4095);if(value.state==FfxHooks::Config::IntReadState::Valid)out.ids[i]=static_cast<unsigned>(value.value);}
    return true;
}
static bool VgUiSave(unsigned effect,unsigned id) noexcept {
    namespace V=FfxHooks::Vanguard;if(!vgUiAllow||effect>=V::AbilityCount||id<135||id>4095)return false;
    V::MappingState state{};VgUiRead(state);for(unsigned i=0;i<V::AbilityCount;++i)if(i!=effect&&state.ids[i]==id)return false;
    char key[96]{};std::snprintf(key,sizeof(key),"vanguard_ids.%s",V::Abilities[effect].key);return FfxHooks::Config::SetInt(key,static_cast<int>(id));
}
static void VanguardMappingMenuCases(){
    namespace V=FfxHooks::Vanguard;namespace C=FfxHooks::Config;
    static const V::UiProvider provider{VgUiRead,VgUiSave};Check(V::RegisterUi(&provider),"mapping UI binds an isolated validator");
    C::ResetForTests();C::LoadTextForTests("[vanguard_ids]\n","C:\\private-vanguard-map.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;vgUiAllow=true;
    const int obj=NativeMenu::Alloc();Check(obj!=0,"mapping editor fixture allocates");
    if(!obj){V::UnregisterUi(&provider);C::ResetForTests();return;}
    F8NativeSettingsReset();F8NativeSettingsPush(obj,NativeSettingsPage::VanguardMapping);NativeMenu::WrW(obj,NativeMenu::O_SELECTED,0);F8NativeSettingsActivate(obj,0);
    const auto edit=F8NativeSettingsPage();Check(edit!=NativeSettingsPage::VanguardMapping&&F8NativeSettingsCount(edit)==4,"mapping selection opens a staged ID editor with save and cancel");
    if(edit==NativeSettingsPage::VanguardMapping){F8NativeSettingsReset();NativeMenu::Reset(obj);V::UnregisterUi(&provider);C::ResetForTests();return;}
    F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,3);
    Check(F8NativeSettingsPage()==NativeSettingsPage::VanguardMapping&&f8Writes==0&&C::ReadIntExact("vanguard_ids.hero_bravery",135,4095).state==C::IntReadState::Missing,"cancel discards the staged ID without a write");
    F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,2);
    Check(F8NativeSettingsPage()==edit&&f8Writes==0,"duplicate validation failure stays in the editor");
    for(unsigned i=0;i<19;++i)F8NativeSettingsActivate(obj,0);
    vgUiAllow=false;F8NativeSettingsActivate(obj,2);Check(F8NativeSettingsPage()==edit&&f8Writes==0,"UI cannot bypass unavailable native validation");
    vgUiAllow=true;F8NativeSettingsActivate(obj,2);
    Check(F8NativeSettingsPage()==NativeSettingsPage::VanguardMapping&&f8Writes==1&&C::ReadIntExact("vanguard_ids.hero_bravery",135,4095).value==155,"save writes exactly one validated changed ID");
    char label[100]{};F8NativeSettingsLabel(NativeSettingsPage::VanguardMapping,0,label,sizeof(label));Check(std::strstr(label,"155")&&std::strstr(label,"Valid"),"mapping page shows effective ID and validation");
    F8NativeSettingsReset();NativeMenu::Reset(obj);V::UnregisterUi(&provider);C::ResetForTests();
}
