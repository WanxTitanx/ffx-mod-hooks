#include "VanguardMappingMenuCases.inl"
#include "VanguardCommandMenuCases.inl"
static void VanguardMenuCases(){
    VanguardMappingMenuCases();
    VanguardCommandMenuCases();
    using namespace FfxHooks;
    Check(std::strcmp(F8TabName(3),"Extras")==0,"former Arena+ tab is now Extras");
    const char* arenaKeys[]={"arena_plus.master","arena_plus.compose_f7","arena_plus.unlock_all",
        "arena_plus.victory_hook","arena_plus.resolver_log","arena_plus.music"};
    for(const auto* key:arenaKeys){const auto* flag=FindF8Flag(key);
        Check(flag&&std::strcmp(flag->tab,"Reforge")==0&&F8NativeNestedFlag(*flag),"all Arena+ gates belong to the Reforge subtree without flat duplicates");}
    const char* independent[]={"vanguard.quickcast_replace_doublecast","vanguard.dualcast_white_magic",
        "vanguard.magic_mp_scaling","vanguard.party_switch_costs_turn","vanguard.vampirism"};
    for(const auto* key:independent){const auto* flag=FindF8Flag(key);
        Check(flag&&std::strcmp(flag->tab,"Extras")==0&&!flag->gate.defaultValue&&F8NativeNestedFlag(*flag),
              "Vanguard has independent default-OFF feature gates");}
    Check(!F8NativeNestedFlag(*FindF8Flag("labs.scan_expanded"))&&
          !F8NativeNestedFlag(*FindF8Flag("window.borderless")),"nested ownership does not absorb unrelated Scan or System controls");
    auto findPage=[](const char* title){
        for(unsigned i=1;i<64;++i){const auto page=static_cast<NativeSettingsPage>(i);
            if(std::strcmp(F8NativeSettingsTitle(page),title)==0)return page;}
        return NativeSettingsPage::None;
    };
    const auto arenaRoot=findPage("Arena+"),arena=findPage("Arena+ settings"),vanguard=findPage("Vanguard Combat Engine");
    Check(arenaRoot!=NativeSettingsPage::None&&F8NativeSettingsCount(arenaRoot)==2,
          "Reforge opens Arena+ with an explicit Options child and Back");
    Check(arena!=NativeSettingsPage::None&&F8NativeSettingsCount(arena)==7,"Arena+ owns one submenu with all six preserved controls");
    Check(vanguard!=NativeSettingsPage::None&&F8NativeSettingsCount(vanguard)==9,"Vanguard has eight independent groups in its own submenu");
    if(arenaRoot==NativeSettingsPage::None||arena==NativeSettingsPage::None||vanguard==NativeSettingsPage::None)return;
    namespace C=FfxHooks::Config;
    C::ResetForTests();C::LoadTextForTests("[arena_plus]\nmaster=0\n","C:\\private-vanguard-menu.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});
    f8AllowWrite=true;f8Writes=0;F8NativeSettingsReset();
    const int obj=NativeMenu::Alloc();Check(obj!=0,"Vanguard navigation fixture allocates");if(!obj)return;
    NativeMenu::WrW(obj,NativeMenu::O_SELECTED,7);NativeMenu::WrW(obj,NativeMenu::O_TOP,2);
    F8NativeSettingsPush(obj,arenaRoot);char optionsLabel[80]{};
    F8NativeSettingsLabel(arenaRoot,0,optionsLabel,sizeof(optionsLabel));
    Check(std::strcmp(optionsLabel,"Options")==0,"Arena+ names its settings entry Options");
    F8NativeSettingsActivate(obj,0);
    Check(F8NativeSettingsPage()==arena,"Arena+ Options opens the six preserved controls");
    const unsigned before=f8Writes;
    F8NativeSettingsActivate(obj,0);
    Check(f8Writes==before+1&&C::ReadIntExact("arena_plus.master",0,1).value==1&&
          C::ReadIntExact("f8_authority.arena_plus_master",0,1).value==1,
          "nested Arena+ retains its real authoritative writer and existing keys");
    F8NativeSettingsActivate(obj,6);
    Check(F8NativeSettingsPage()==arenaRoot,"Options Back returns to Arena+ rather than skipping its parent");
    F8NativeSettingsActivate(obj,1);
    Check(!F8NativeSettingsActive()&&NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==7&&NativeMenu::RdW(obj,NativeMenu::O_TOP)==2,
          "Arena+ Back preserves the Reforge list position");
    F8NativeSettingsPush(obj,vanguard);const auto writes=f8Writes;
    for(int i=0;i<8;++i){
        NativeMenu::WrW(obj,NativeMenu::O_SELECTED,static_cast<short>(i));F8NativeSettingsActivate(obj,i);
        Check(F8NativeSettingsPage()!=vanguard&&F8NativeSettingsCount(F8NativeSettingsPage())>1,
              "every Vanguard category is reachable without enabling a feature");
        F8NativeSettingsActivate(obj,F8NativeSettingsCount(F8NativeSettingsPage())-1);
        Check(F8NativeSettingsPage()==vanguard&&NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==i,
              "Vanguard group Back returns to its selected category");
    }
    Check(f8Writes==writes,"browsing all groups never writes config or activates gameplay");
    F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}
