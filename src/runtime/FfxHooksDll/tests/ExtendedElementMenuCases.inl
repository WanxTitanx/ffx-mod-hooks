static bool SampleElementMenu(FfxHooks::ElementMenu::Catalog& out) noexcept {
    out=FfxHooks::ElementMenu::Defaults();
    for(unsigned i=8;i<10;++i){auto& item=out[i];item.available=true;item.rgb=i==8?0x123456:0xABCDEF;
        std::snprintf(item.key,sizeof(item.key),"%s",i==8?"mod.aether":"mod.void");
        std::snprintf(item.label,sizeof(item.label),"%s",i==8?"Aether":"Void");}
    return true;
}
static void ExtendedElementMenuCases(){
    using namespace FfxHooks;namespace C=Config;namespace E=ElementScan;
    C::ResetForTests();C::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-element-menus.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    F8NativeSettingsReset();const int obj=NativeMenu::Alloc();if(!obj){Check(false,"extended menu allocates");return;}
    const auto defaults=ElementMenu::Read();unsigned bits=0;
    for(unsigned i=0;i<8;++i){Check(defaults[i].available&&defaults[i].nativeBit&&!(bits&defaults[i].nativeBit),"eight native rows each own a distinct bit");bits|=defaults[i].nativeBit;}
    Check(bits==255&&!defaults[8].available&&!defaults[9].available,"Core OFF keeps external gameplay admission closed without inventing native bits");
    F8NativeSettingsPush(obj,NativeSettingsPage::ElementScan);char text[128]{};
    F8NativeSettingsLabel(F8NativeSettingsPage(),8,text,sizeof(text));Check(std::strstr(text,"Core")!=nullptr,"Scan explains that a disabled ninth descriptor needs Core");
    F8NativeSettingsActivate(obj,8);Check(F8NativeSettingsPage()==NativeSettingsPage::ElementScan&&f8Writes==0,"unavailable external entry cannot enable gameplay or write a made-up key");
    const ElementMenu::Provider provider{SampleElementMenu};Check(ElementMenu::Register(&provider),"test pack publishes one shared menu catalog");
    F8NativeSettingsLabel(F8NativeSettingsPage(),9,text,sizeof(text));Check(std::strstr(text,"Void")&&std::strstr(text,"ABCDEF"),"tenth descriptor supplies the real label and default color");
    F8NativeSettingsActivate(obj,8);Check(F8NativeSettingsPage()==NativeSettingsPage::ElementColor&&g_nativeElementRgb==0x123456,"ninth element opens its own color editor");
    F8NativeElementAdjust(0,20);F8NativeSettingsActivate(obj,3);
    const auto palette=ElementMenu::Read();std::uint32_t rgb=0;bool visible=false;
    Check(E::ReadHookPresentation(palette[8],rgb,visible)&&rgb!=0x123456&&visible,"external color persists without hiding the element");
    Check(E::ReadHookPresentation(palette[9],rgb,visible)&&rgb==0xABCDEF,"editing element nine leaves element ten unchanged");
    F8NativeSettingsActivate(obj,6);Check(F8NativeSettingsCount(F8NativeSettingsPage())==7,"visibility page includes four native extras and both external descriptors");
    F8NativeSettingsActivate(obj,4);Check(E::ReadHookPresentation(palette[8],rgb,visible)&&!visible,"ninth visibility switch persists under its stable key");
    f8AllowWrite=false;F8NativeSettingsActivate(obj,4);Check(E::ReadHookPresentation(palette[8],rgb,visible)&&!visible,"failed visibility persistence preserves the old choice");
    f8AllowWrite=true;const auto saved=f8Saved;C::LoadTextForTests(saved.c_str(),"C:\\private-element-menus.ini");
    Check(E::ReadHookPresentation(palette[8],rgb,visible)&&!visible&&rgb!=0x123456,"external palette and visibility survive reload");
    Check(C::ReadIntExact("elemental.core",0,1).state==C::IntReadState::Missing,"visual preferences never enable the gameplay module");
    F8NativeSettingsActivate(obj,6);Check(F8NativeSettingsPage()==NativeSettingsPage::ElementScan,"visibility Back restores Scan");
    ElementMenu::Unregister(&provider);F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}

static void NestedFeatureMenuCases(){
    using namespace FfxHooks;
    Config::ResetForTests();Config::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-nested-flags.ini");
    Config::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    F8NativeSettingsReset();const int obj=NativeMenu::Alloc();if(!obj){Check(false,"nested feature menu allocates");return;}
    for(const auto page:{NativeSettingsPage::AdditionalMods,NativeSettingsPage::FieldScout}){
        NativeMenu::WrW(obj,NativeMenu::O_SELECTED,3);NativeMenu::WrW(obj,NativeMenu::O_TOP,1);
        const auto before=f8Writes;F8NativeSettingsPush(obj,page);
        Check(f8Writes==before,"opening a group never changes any settings");
        const int leaves=page==NativeSettingsPage::AdditionalMods?8:4;
        Check(F8NativeSettingsCount(page)==leaves+1,"each group exposes its complete set of controls and Back");
        for(int row=0;row<leaves;++row){const auto* flag=F8NativeNestedSpec(page,row);
            Check(flag&&F8NativeNestedFlag(*flag),"nested leaf is hidden at the tab root but stays in the authority catalog");
            if(!flag)continue;const bool old=ResolveF8Flag(*flag).value;
            F8NativeSettingsActivate(obj,row);Check(ResolveF8Flag(*flag).value!=old,"nested control persists through the real settings handler");}
        const auto writes=f8Writes;F8NativeSettingsActivate(obj,leaves);
        Check(!F8NativeSettingsActive()&&f8Writes==writes&&NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==3&&NativeMenu::RdW(obj,NativeMenu::O_TOP)==1,
            "Back returns to the same parent cursor without changing configuration");
    }
    F8NativeSettingsReset();NativeMenu::Reset(obj);Config::ResetForTests();
}
