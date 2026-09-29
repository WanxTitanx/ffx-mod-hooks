static bool elementNameReverse=false;
static bool ElementNameReorderedCatalog(FfxHooks::ElementMenu::Catalog& out) noexcept {
    out=FfxHooks::ElementMenu::Defaults();out[8].available=out[9].available=true;
    if(elementNameReverse)std::swap(out[8],out[9]);return true;
}
static void ElementNameMenuCases(){
    using namespace FfxHooks;namespace C=Config;namespace N=ElementNames;
    C::ResetForTests();C::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-element-names.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    const auto equipment=TestHost::state;
    Check(ElementMenu::SaveName(0x10,"native.holy","  Radiance ")==N::Result::Saved,"Holy can have a presentation-only alias");
    auto catalog=ElementMenu::Read();
    Check(!std::strcmp(catalog[4].label,"Radiance")&&catalog[4].nativeBit==0x10&&!std::strcmp(catalog[4].key,"native.holy"),"renaming preserves native identity and bit");
    Check(!std::strcmp(ElementMenu::Raw()[4].label,"Holy"),"canonical registry labels are not rewritten");
    Check(ElementMenu::SaveName(0,"hook.custom03","Aether")==N::Result::Saved,"an unused custom slot can be named before enabling gameplay");
    Check(!ElementMenu::Read()[8].available&&C::ReadIntExact("elemental.core",0,1).state==C::IntReadState::Missing,"renaming never arms Core");
    const auto before=f8Writes;
    Check(ElementMenu::SaveName(0,"hook.custom04","aEtHeR")==N::Result::Duplicate&&f8Writes==before,"case-insensitive duplicate names are rejected before persistence");
    Check(ElementMenu::SaveName(0x80,"native.darkness","")==N::Result::Empty,"empty Save is not an implicit reset");
    Check(ElementMenu::SaveName(0x80,"native.darkness","Dark;core=1")==N::Result::InvalidCharacter,"name input cannot add INI fields");
    const auto saved=f8Saved;C::LoadTextForTests(saved.c_str(),"C:\\private-element-names.ini");
    catalog=ElementMenu::Read();Check(!std::strcmp(catalog[4].label,"Radiance")&&!std::strcmp(catalog[8].label,"Aether"),"aliases survive reload through stable identities");
    Check(ElementMenu::SaveName(0,"hook.custom03","",true)==N::Result::Saved&&!std::strcmp(ElementMenu::Read()[8].label,"Poison"),"Reset inherits the default without renaming the identity");
    f8AllowWrite=false;
    Check(ElementMenu::SaveName(0x10,"native.holy","Light")==N::Result::WriteFailed&&!std::strcmp(ElementMenu::Read()[4].label,"Radiance"),"failed writes preserve the existing name");
    f8AllowWrite=true;
    const auto stable=f8Saved;
    C::LoadTextForTests("[element_names]\nnative_20=Custom 01\nnative_40=Gale\nhook.custom03=Custom 03\nhook.custom04=Void\n", "C:\\private-element-names.ini");
    catalog=ElementMenu::Read();
    Check(!std::strcmp(catalog[6].label,"Custom 01")&&!std::strcmp(catalog[7].label,"Gale")&&
          !std::strcmp(catalog[8].label,"Custom 03")&&!std::strcmp(catalog[9].label,"Void"),
          "new default roles preserve all saved aliases, including explicitly saved former default labels");
    C::LoadTextForTests("[element_names]\nnative_10=Poison\n", "C:\\private-element-names.ini");
    catalog=ElementMenu::Read();
    Check(!std::strcmp(catalog[4].label,"Poison")&&!std::strcmp(catalog[8].label,"Custom 03"),
          "a saved alias keeps priority when a formerly neutral slot receives the same new default");
    Check(ElementMenu::SaveName(0,"hook.custom03","",true)==N::Result::Saved&&
          !std::strcmp(ElementMenu::Read()[4].label,"Poison")&&!std::strcmp(ElementMenu::Read()[8].label,"Custom 03"),
          "resetting the promoted default preserves the other element's saved alias");
    C::LoadTextForTests("[element_names]\nnative_10=Darkness\nnative_80=Darkness\n", "C:\\private-element-names.ini");
    catalog=ElementMenu::Read();Check(!std::strcmp(catalog[4].label,"Holy")&&!std::strcmp(catalog[5].label,"Darkness")&&catalog[4].available,
          "manually conflicting aliases fall back without invalidating native elements");
    C::LoadTextForTests("[element_names]\nnative_10=Bad=Name\nnative_80=Night\n", "C:\\private-element-names.ini");
    catalog=ElementMenu::Read();Check(!std::strcmp(catalog[4].label,"Holy")&&!std::strcmp(catalog[5].label,"Night"),"one malformed alias cannot suppress another valid alias");
    C::LoadTextForTests("[element_names]\nnative_10=Darkness\nnative_80=Radiance\n", "C:\\private-element-names.ini");
    catalog=ElementMenu::Read();Check(!std::strcmp(catalog[4].label,"Darkness")&&!std::strcmp(catalog[5].label,"Radiance"),"valid simultaneous aliases are resolved before duplicate checks");
    C::LoadTextForTests(stable.c_str(),"C:\\private-element-names.ini");
    const ElementMenu::Provider reordered{ElementNameReorderedCatalog};Check(ElementMenu::Register(&reordered),"name test owns one temporary descriptor provider");
    Check(ElementMenu::SaveName(0,"hook.custom03","Aether")==N::Result::Saved,"external names attach to stable keys");
    elementNameReverse=true;catalog=ElementMenu::Read();
    Check(!std::strcmp(catalog[9].key,"hook.custom03")&&!std::strcmp(catalog[9].label,"Aether"),"reordering external descriptors moves the existing alias with its identity");
    Check(ElementMenu::SaveName(0,"hook.custom03","Ether")==N::Result::Saved&&!std::strcmp(ElementMenu::Read()[9].label,"Ether"),"a staged identity still saves to the correct element after reordering");
    ElementMenu::Unregister(&reordered);elementNameReverse=false;
    C::LoadTextForTests(stable.c_str(),"C:\\private-element-names.ini");
    F8NativeSettingsReset();const int obj=NativeMenu::Alloc();if(!obj){Check(false,"name menu allocates");return;}
    F8NativeSettingsPush(obj,NativeSettingsPage::ElementScan);const auto openedWrites=f8Writes;
    F8NativeSettingsActivate(obj,10);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementNames&&F8NativeSettingsCount(F8NativeSettingsPage())==7&&f8Writes==openedWrites,"Scan opens six names without changing configuration");
    F8NativeSettingsActivate(obj,0);Check(F8NativeSettingsPage()==NativeSettingsPage::ElementNameEdit,"Holy opens the staged name editor");
    F8NativeSettingsActivate(obj,0);Check(F8NativeSettingsPage()==NativeSettingsPage::ElementNameCapture&&ElementNameInput::Active(),"keyboard editing owns a separate capture page");
    ElementNameInput::Message(WM_CHAR,0xE3);F8NativeSettingsActivate(obj,1);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementNameCapture&&ElementNameInput::Active(),"unsupported input cannot silently save a partial name");
    ElementNameInput::Message(WM_CHAR,8);
    for(char ch:std::string("Light"))ElementNameInput::Message(WM_CHAR,static_cast<WPARAM>(ch));
    F8NativeSettingsActivate(obj,1);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementNameEdit&&!ElementNameInput::Active()&&!std::strcmp(g_nativeElementNameDraft,"Light")&&f8Writes==openedWrites,"accepting text stages it without saving");
    F8NativeSettingsActivate(obj,5);
    Check(F8NativeSettingsPage()==NativeSettingsPage::ElementNames&&!std::strcmp(ElementMenu::Read()[4].label,"Light"),"explicit Save publishes the alias to the shared F7/Scan catalog");
    char label[128]{};F8NativeSettingsLabel(NativeSettingsPage::ElementScan,2,label,sizeof(label));
    Check(std::strstr(label,"Light color")!=nullptr,"Scan color references automatically use the alias");
    F8NativeSettingsActivate(obj,1);F8NativeSettingsActivate(obj,4);
    g_nativeElementNameCharacter=1;F8NativeSettingsActivate(obj,2);
    Check(!std::strcmp(g_nativeElementNameDraft,"A"),"the on-screen picker supports controller-only naming");
    F8NativeSettingsActivate(obj,7);
    Check(!std::strcmp(ElementMenu::Read()[5].label,"Darkness"),"Back discards the picker draft");
    F8NativeSettingsActivate(obj,0);F8NativeSettingsActivate(obj,0);
    ElementNameInput::Message(WM_CHAR,'X');ElementNameInput::Message(WM_KILLFOCUS,0);
    F8NativeSettingsReset();Check(!ElementNameInput::Active()&&!std::strcmp(ElementMenu::Read()[4].label,"Light"),"focus loss/reset cancels capture without a save");
    Check(!std::memcmp(&equipment,&TestHost::state,sizeof(equipment)),"element aliases never rewrite native item/equipment attributes");
    NativeMenu::Reset(obj);C::ResetForTests();
}
