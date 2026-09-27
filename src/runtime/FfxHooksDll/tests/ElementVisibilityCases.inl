static void ElementVisibilityCases(){
    namespace C=FfxHooks::Config;namespace E=FfxHooks::ElementScan;
    for(unsigned selected=0;selected<8;++selected){
        char ini[200]{};_snprintf_s(ini,sizeof(ini),_TRUNCATE,
            "[element_scan]\nholy_enabled=%u\ndark_enabled=%u\nextra_enabled=%u\n",
            selected&1,(selected>>1)&1,(selected>>2)&1);
        C::ResetForTests();C::LoadTextForTests(ini,"C:\\private-elements.ini");
        E::Settings settings{};Check(E::ReadSettings(settings),"individual element switches load as strict booleans");
        for(unsigned affinity=0;affinity<256;++affinity){
            const auto orbs=E::Orbs(affinity,settings);unsigned count=0;
            for(const auto& orb:orbs)if(orb.bit){
                const unsigned index=orb.bit==0x10?0:orb.bit==0x80?1:2;
                Check((selected&(1u<<index))!=0,"disabled extra elements produce no visible orb");
                Check(orb.rgb==settings.rgb[index]&&orb.active==bool(affinity&orb.bit),"visibility preserves the real affinity and the correct palette entry");
                Check(std::fabs(orb.x-(379.f+63.f*count))<.001f,"selected extras are packed without blank disabled columns");
                ++count;
            }
            const unsigned expected=(selected&1)+((selected>>1)&1)+((selected>>2)&1);
            Check(count==expected,"zero one two or three extras follow independent choices");
        }
    }
    C::LoadTextForTests("[element_scan]\ndark_enabled=2\n","C:\\private-elements.ini");
    E::Settings settings{};Check(!E::ReadSettings(settings),"invalid individual element value fails closed");
    C::ResetForTests();C::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-elements.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    F8NativeSettingsReset();const int obj=NativeMenu::Alloc();if(!obj){Check(false,"individual elements menu allocates");return;}
    F8NativeSettingsPush(obj,NativeSettingsPage::ElementScan);
    Check(F8NativeSettingsCount(F8NativeSettingsPage())==6,"Scan settings include an individual-elements child page");
    F8NativeSettingsActivate(obj,4);
    const bool opened=F8NativeSettingsActive()&&std::strcmp(F8NativeSettingsTitle(F8NativeSettingsPage()),"Enabled Scan elements")==0;
    Check(opened,"individual switches open under the Scan settings page");
    if(opened){
        NativeMenu::rendered.clear();F8NativeSettingsDraw(obj,1);
        Check(WorkshopRendered("Holy")&&WorkshopRendered("Darkness")&&WorkshopRendered("Custom"),"all three extra elements have a separate visible toggle");
        F8NativeSettingsActivate(obj,1);
        Check(C::ReadIntExact("element_scan.dark_enabled",0,1).value==0&&f8Writes==1,"Darkness OFF is persisted exactly once");
        Check(C::ReadIntExact("element_scan.holy_enabled",0,1).state==C::IntReadState::Missing&&
              C::ReadIntExact("labs.element_scan_dark",0,1).state==C::IntReadState::Missing,"individual choices never toggle the master or other elements");
        f8AllowWrite=false;F8NativeSettingsActivate(obj,1);
        Check(C::ReadIntExact("element_scan.dark_enabled",0,1).value==0&&std::strstr(g_nativeSettingsNotice,"Unable"),"failed toggle write preserves the prior visible selection");
        f8AllowWrite=true;const auto saved=f8Saved;C::LoadTextForTests(saved.c_str(),"C:\\private-elements.ini");
        Check(E::ReadSettings(settings),"individual choices survive configuration reload");
        F8NativeSettingsActivate(obj,3);
        Check(F8NativeSettingsPage()==NativeSettingsPage::ElementScan,"individual elements Back restores its real parent");
    }
    F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}
