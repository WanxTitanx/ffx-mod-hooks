static void ElementColorSettingsCases(){
    namespace C=FfxHooks::Config;namespace E=FfxHooks::ElementScan;
    C::ResetForTests();C::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-element-colors.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    F8NativeSettingsReset();const int obj=NativeMenu::Alloc();if(!obj){Check(false,"color page allocates a menu object");return;}
    F8NativeSettingsPush(obj,NativeSettingsPage::ElementScan);
    Check(F8NativeSettingsCount(F8NativeSettingsPage())==12,"Scan page preserves native rows, both Hook colors and name editing");
    char bitLabel[128]{};F8NativeSettingsLabel(NativeSettingsPage::ElementScan,5,bitLabel,sizeof(bitLabel));
    Check(std::strstr(bitLabel,"Earth")&&std::strstr(bitLabel,"Wind"),
          "custom order describes the current element names instead of stale hardcoded labels");
    E::Settings before{};Check(E::ReadSettings(before),"default palette loads independently of the enable gate");
    F8NativeSettingsActivate(obj,2);F8NativeElementAdjust(0,15);
    E::Settings current{};Check(E::ReadSettings(current)&&current.rgb[0]==before.rgb[0]&&f8Writes==0,"color preview changes no saved setting");
    F8NativeSettingsActivate(obj,4);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementScan&&f8Writes==0,"color Cancel restores parent without saving");
    F8NativeSettingsActivate(obj,2);F8NativeElementAdjust(0,15);
    f8AllowWrite=false;F8NativeSettingsActivate(obj,3);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementColor&&E::ReadSettings(current)&&current.rgb[0]==before.rgb[0]&&std::strstr(g_nativeSettingsNotice,"Unable"),
          "failed color persistence preserves saved RGB and keeps the draft");
    f8AllowWrite=true;F8NativeSettingsActivate(obj,3);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementScan&&E::ReadSettings(current)&&current.rgb[0]!=before.rgb[0]&&current.rgb[1]==before.rgb[1],
          "saving Holy color changes only that palette entry");
    const auto exact=current.rgb[0];F8NativeSettingsActivate(obj,2);F8NativeSettingsActivate(obj,3);
    Check(E::ReadSettings(current)&&current.rgb[0]==exact,"opening and saving without edits preserves the exact RGB24 value");
    F8NativeSettingsActivate(obj,5);
    for(int row=0;row<2;++row){F8NativeSettingsLabel(NativeSettingsPage::ElementBit,row,bitLabel,sizeof(bitLabel));
        Check(std::strstr(bitLabel,row==0?"Earth":"Wind")&&std::strstr(bitLabel,row==0?"0x20":"0x40"),
              "extra-bit choices use the approved names while preserving their native identities");}
    F8NativeSettingsActivate(obj,1);
    Check(E::ReadSettings(current)&&current.extraBit==64,"third-column custom bit persists without changing Holy or Darkness");
    Check(C::ReadIntExact("labs.element_scan_dark",0,1).state==C::IntReadState::Missing,"palette editing never implicitly enables the Scan hook");
    F8NativeSettingsActivate(obj,4);NativeMenu::rendered.clear();F8NativeSettingsDraw(obj,1);
    Check(WorkshopRendered("Hue")&&WorkshopRendered("Saturation")&&WorkshopRendered("Brightness"),"color editor explains all three HSV controls");
    NativeMenu::WrW(obj,NativeMenu::O_SELECTED,0);NativeMenu::padDirection=0x8000;NativeMenu::padEdge=0;g_nativeElementRepeat=0;
    const auto hue=g_nativeElementHsv.h;F8NativeSettingsInput(obj);NativeMenu::padDirection=0;
    Check(g_nativeElementHsv.h==(hue+359)%360,"left input moves one hue step with wraparound");
    F8NativeSettingsActivate(obj,4);
    Check(E::ReadSettings(current),"current colors load before editing the fourth column");
    const auto previousFourth=current.rgb[3];const auto previousThird=current.rgb[2];
    F8NativeSettingsActivate(obj,7);F8NativeElementAdjust(0,30);F8NativeSettingsActivate(obj,3);
    Check(E::ReadSettings(current)&&current.rgb[3]!=previousFourth&&current.rgb[2]==previousThird&&
          !current.enabled[3],"the other Custom color persists separately without enabling its column");
    F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}
