// Native F8 binding editor: a current validator owns persistence, never the menu.
static void VanguardCommandMenuCases(){
    namespace V=FfxHooks::Vanguard;namespace C=FfxHooks::Config;
    static V::BindingState supplied{};static bool allow=true;
    static const V::UiProvider provider{VgUiRead,VgUiSave,nullptr,
        [](V::BindingState& out) noexcept {out=supplied;return true;},
        [](unsigned effect,unsigned packed,std::uint64_t stamp) noexcept {
            if(!allow||effect>=V::AbilityCount||stamp!=supplied.stamp)return false;
            char key[96]{};std::snprintf(key,sizeof(key),"vanguard_commands.%s",V::Abilities[effect].key);
            if(!C::SetInt(key,static_cast<int>(packed)))return false;
            auto& entry=supplied.entries[effect];entry.packed=packed;entry.command=packed&0xFFFF;entry.cost=(packed>>16)?(packed>>16)-1:256;
            entry.code=packed?V::BindingCode::Valid:V::BindingCode::Disabled;++supplied.stamp;return true;
        }};
    Check(V::RegisterUi(&provider),"command editor uses its registered native validator");
    supplied={};supplied.stamp=41;allow=true;
    C::ResetForTests();C::LoadTextForTests("[vanguard_commands]\n","C:\\private-command-editor.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    const int obj=NativeMenu::Alloc();Check(obj!=0,"command editor fixture owns one native menu object");
    if(!obj){V::UnregisterUi(&provider);return;}
    F8NativeSettingsReset();F8NativeSettingsPush(obj,NativeSettingsPage::Vanguard);
    F8NativeSettingsActivate(obj,6);
    const auto equipment=F8NativeSettingsPage();
    Check(F8NativeSettingsCount(equipment)==4,"equipment group exposes two independent gates plus command bindings and Back");
    if(F8NativeSettingsCount(equipment)!=4){F8NativeSettingsReset();NativeMenu::Reset(obj);V::UnregisterUi(&provider);return;}
    F8NativeSettingsActivate(obj,2);const auto list=F8NativeSettingsPage();
    Check(std::strcmp(F8NativeSettingsTitle(list),"Equipment command bindings")==0&&F8NativeSettingsCount(list)==14,
          "one binding row per stable ability appears without changing configuration");
    F8NativeSettingsActivate(obj,0);const auto edit=F8NativeSettingsPage();
    Check(std::strcmp(F8NativeSettingsTitle(edit),"Edit equipment command")==0&&F8NativeSettingsCount(edit)==8&&g_nativeSettingsDepth==4,
          "binding edit fits the existing four-frame navigation stack");
    F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,2);F8NativeSettingsActivate(obj,7);
    Check(F8NativeSettingsPage()==list&&f8Writes==0,"cancel discards command and OD changes together");
    F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,2);
    ++supplied.stamp;F8NativeSettingsActivate(obj,5);
    Check(F8NativeSettingsPage()==edit&&f8Writes==0,"stale kernel/configuration proof prevents a binding save");
    F8NativeSettingsActivate(obj,7);F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,0);
    for(unsigned i=0;i<13;++i)F8NativeSettingsActivate(obj,2);
    allow=false;F8NativeSettingsActivate(obj,5);Check(f8Writes==0&&F8NativeSettingsPage()==edit,"validator denial does not publish a partial command or price");
    allow=true;F8NativeSettingsActivate(obj,5);
    Check(f8Writes==1&&F8NativeSettingsPage()==list&&C::ReadIntExact("vanguard_commands.hero_bravery",0,0x100FFFF).value==static_cast<int>((13u<<16)|0x3001),
          "save persists command and twelve-point OD price in one validated atomic setting");
    char label[100]{};F8NativeSettingsLabel(list,0,label,sizeof(label));
    Check(std::strstr(label,"3001")&&std::strstr(label,"12"),"binding list shows the effective command and cost");
    F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,6);
    Check(f8Writes==2&&C::ReadIntExact("vanguard_commands.hero_bravery",0,0x100FFFF).value==0,"explicit Disable removes only this binding");
    F8NativeSettingsReset();NativeMenu::Reset(obj);V::UnregisterUi(&provider);C::ResetForTests();
}
