// Exercise the actual settings page and persistence boundary, not a second menu.
static bool scanMenuExternalOverride=false,scanMenuLegacyFlag=false;
static bool ScanMenuEnv(void*,const char* name,bool* out){
    if(scanMenuExternalOverride&&std::strcmp(name,"FFXHOOKS_SCAN_EXPANDED")==0){*out=true;return true;}
    return false;
}
static bool ScanMenuFlag(void*,const char* name,FfxHooks::Config::BoolSource* out){
    if(scanMenuLegacyFlag&&std::strcmp(name,"scan_expanded.flag")==0){*out=FfxHooks::Config::BoolSource::LegacyFlagModules;return true;}
    return false;
}
static int ScanSettingsRow(const char* prefix){
    for(int row=0;row<F8NativeSettingsCount(F8NativeSettingsPage());++row){
        char text[128]{};F8NativeSettingsLabel(F8NativeSettingsPage(),row,text,sizeof(text));
        if(std::strncmp(text,prefix,std::strlen(prefix))==0)return row;
    }
    return -1;
}
static void ScanSettingsMenuCases(){
    namespace C=FfxHooks::Config;
    C::ResetForTests();C::LoadTextForTests(
        "[labs]\nscan_expanded=0\nelement_scan_dark=0\n"
        "[element_scan]\nholy_rgb=1122867\ndark_rgb=4478310\nextra_rgb=7833753\nextra_bit=64\ndark_enabled=0\n",
        "C:\\private-scan-menu.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});
    f8AllowWrite=true;f8Writes=0;F8NativeSettingsReset();
    pointerSample={};pointerState={};NativeMenu::padEdge=NativeMenu::padDirection=0;
    const int obj=NativeMenu::Alloc();Check(obj!=0,"Scan settings menu allocates");if(!obj)return;
    NativeMenu::WrW(obj,NativeMenu::O_SELECTED,9);NativeMenu::WrW(obj,NativeMenu::O_TOP,3);
    F8NativeSettingsPush(obj,NativeSettingsPage::ElementScan);
    const int expanded=ScanSettingsRow("Scan Expanded:"),elements=ScanSettingsRow("Scan Extra Elements:");
    Check(expanded>=0&&elements>=0,"Scan controls are reachable inside the existing color submenu");
    Check(std::strcmp(F8NativeSettingsTitle(F8NativeSettingsPage()),"Scan settings")==0,
          "the shared submenu title describes both stats and elements");
    if(expanded<0||elements<0){F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();return;}
    Check(f8Writes==0&&ScanSettingsRow("Holy color:")>=0&&ScanSettingsRow("Enabled extra elements:")>=0,
          "opening the shared page preserves preferences and existing color/visibility access");
    unsigned owned=0;
    for(std::size_t i=0;i<FfxHooks::F8FlagCount();++i){const auto& flag=FfxHooks::F8FlagAt(i);
        const bool scan=std::strcmp(flag.gate.canonicalKey,"labs.scan_expanded")==0||
                        std::strcmp(flag.gate.canonicalKey,"labs.element_scan_dark")==0;
        Check(F8NativeScanOwnsFlag(flag)==scan,"only the two Scan masters move out of the flat Reforge list");
        if(F8NativeScanOwnsFlag(flag))++owned;
    }
    Check(owned==2,"both canonical Scan flags remain in the catalog for startup and bulk changes");
    NativeMenu::WrW(obj,NativeMenu::O_SELECTED,static_cast<short>(expanded));
    g_f7ConfirmTimer=0;g_nativeSettingsLastEdge=0;NativeMenu::padEdge=0x20;
    F8NativeSettingsInput(obj);NativeMenu::padEdge=0;
    Check(f8Writes==1&&C::ReadIntExact("labs.scan_expanded",0,1).value==1&&
          C::ReadIntExact("labs.element_scan_dark",0,1).value==0,
          "confirming Scan Expanded writes only its own preference once");
    Check(C::ReadIntExact("f8_authority.scan_expanded",0,1).value==1&&
          std::strstr(g_nativeSettingsNotice,"Restart")&&F8NativeSettingsPage()==NativeSettingsPage::ElementScan,
          "the submenu uses the authoritative gate writer and explains restart without closing");
    F8NativeSettingsActivate(obj,elements);F8NativeSettingsActivate(obj,expanded);
    Check(f8Writes==3&&C::ReadIntExact("labs.scan_expanded",0,1).value==0&&
          C::ReadIntExact("labs.element_scan_dark",0,1).value==1&&
          C::ReadIntExact("f8_authority.element_scan_dark",0,1).value==1,
          "disabling expanded stats leaves extra elements independently enabled");
    const auto saved=f8Saved;C::LoadTextForTests(saved.c_str(),"C:\\private-scan-menu.ini");
    Check(C::ReadIntExact("labs.scan_expanded",0,1).value==0&&C::ReadIntExact("labs.element_scan_dark",0,1).value==1,
          "submenu choices survive a real config reload");
    for(bool statsRunning:{false,true})for(bool elementsRunning:{false,true}){
        TestHost::scanExpandedActive=statsRunning;TestHost::scanActive=elementsRunning;
        char statsLabel[100]{},elementsLabel[100]{};
        F8NativeSettingsLabel(NativeSettingsPage::ElementScan,expanded,statsLabel,sizeof(statsLabel));
        F8NativeSettingsLabel(NativeSettingsPage::ElementScan,elements,elementsLabel,sizeof(elementsLabel));
        NativeMenu::rendered.clear();F8NativeSettingsDraw(obj,1);
        Check(std::strstr(statsLabel,"Scan Expanded: OFF")&&std::strstr(statsLabel,statsRunning?"Running: ON":"Running: OFF")&&
              std::strstr(elementsLabel,"Scan Extra Elements: ON")&&std::strstr(elementsLabel,elementsRunning?"Running: ON":"Running: OFF")&&
              WorkshopRendered(statsLabel)&&WorkshopRendered(elementsLabel),
              "the submenu shows the independent running states separately from next-boot choices");
    }
    TestHost::scanExpandedActive=TestHost::scanActive=false;
    f8AllowWrite=false;F8NativeSettingsActivate(obj,elements);
    Check(C::ReadIntExact("labs.element_scan_dark",0,1).value==1&&f8Saved==saved&&
          std::strstr(g_nativeSettingsNotice,"Unable"),"failed submenu persistence preserves the prior choice");
    f8AllowWrite=true;
    const unsigned writes=f8Writes;const int holy=ScanSettingsRow("Holy color:");
    NativeMenu::WrW(obj,NativeMenu::O_SELECTED,static_cast<short>(holy));
    F8NativeSettingsActivate(obj,holy);F8NativeElementAdjust(0,15);
    F8NativeSettingsActivate(obj,F8NativeSettingsCount(F8NativeSettingsPage())-1);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementScan&&
          NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==holy&&f8Writes==writes,
          "color Cancel returns to the same shared-menu row without saving the preview");
    Check(C::ReadIntExact("element_scan.holy_rgb",0,0xFFFFFF).value==1122867&&
          C::ReadIntExact("element_scan.dark_rgb",0,0xFFFFFF).value==4478310&&
          C::ReadIntExact("element_scan.extra_bit",32,64).value==64&&
          C::ReadIntExact("element_scan.dark_enabled",0,1).value==0,
          "moving and editing master controls preserves palette, masks and individual choices");
    C::SetProvidersForTests({nullptr,ScanMenuEnv,ScanMenuFlag,WorkshopF8Persist});
    scanMenuLegacyFlag=true;F8NativeSettingsActivate(obj,expanded);F8NativeSettingsActivate(obj,expanded);
    Check(!FfxHooks::ResolveF8Flag(*FfxHooks::FindF8Flag("labs.scan_expanded")).value&&
          C::ReadIntExact("f8_authority.scan_expanded",0,1).value==1,
          "submenu OFF remains authoritative over a pre-existing legacy marker");
    scanMenuExternalOverride=true;F8NativeSettingsActivate(obj,expanded);
    Check(C::ReadIntExact("labs.scan_expanded",0,1).value==0&&
          FfxHooks::ResolveF8Flag(*FfxHooks::FindF8Flag("labs.scan_expanded")).value&&
          std::strstr(g_nativeSettingsNotice,"External override"),
          "an environment override is reported instead of falsely promising a changed effective state");
    scanMenuExternalOverride=scanMenuLegacyFlag=false;
    const unsigned writesBeforeBack=f8Writes;
    F8NativeSettingsActivate(obj,F8NativeSettingsCount(F8NativeSettingsPage())-1);
    Check(!F8NativeSettingsActive()&&NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==9&&
          NativeMenu::RdW(obj,NativeMenu::O_TOP)==3&&f8Writes==writesBeforeBack,
          "Scan settings Back restores the Reforge selection and scroll without writing");
    F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}
