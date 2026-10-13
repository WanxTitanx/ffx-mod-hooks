// Real native Workshop configuration pages with the normal INI persistence
// adapter. No gameplay writer is called while editing preferences.
static bool nativeRequested[2]={true,true},nativeRunning[2]={true,true};
static bool WorkshopFeatureFixtureRead(unsigned i,bool running){return i<2&&(running?nativeRunning[i]:nativeRequested[i]);}
static EquipmentMenu::FeatureWriteResult WorkshopFeatureFixtureWrite(unsigned i,bool value){
    if(i>=2)return EquipmentMenu::FeatureWriteResult::Failed;
    const auto* flag=FfxHooks::FindF8Flag(i?"labs.equipment_workshop_native_ui":"labs.equipment_workshop");
    const bool saved=flag&&FfxHooks::SetF8FlagValue(*flag,value).code==FfxHooks::F8EditCode::Saved;
    if(saved)nativeRequested[i]=value;return saved?EquipmentMenu::FeatureWriteResult::Saved:EquipmentMenu::FeatureWriteResult::Failed;
}
static void WorkshopNativeSettingsCases(){
    namespace C=FfxHooks::Config;namespace S=FfxHooks::EquipmentWorkshop::Settings;
    using A=EquipmentMenu::Action;using P=EquipmentMenu::Page;
    EquipmentMenu::StopReady();C::ResetForTests();
    C::LoadTextForTests("[labs]\nequipment_workshop=1\nequipment_workshop_native_ui=1\n","C:\\private-native-workshop.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    TestHost::available=false;TestHost::customizeUnlocked=false;
    EquipmentMenu::featureReader=WorkshopFeatureFixtureRead;EquipmentMenu::featureWriter=WorkshopFeatureFixtureWrite;
    const auto gear=TestHost::state;const auto gil=TestHost::gil,commits=TestHost::commits;
    Check(EquipmentMenu::Open(true),"native settings are reachable without the gameplay backend or Customize");
    Check(WorkshopTestChoose(A::Settings)&&EquipmentMenu::page==P::Settings,"native information page exposes Workshop settings");
    Check(WorkshopTestChoose(A::Feature,0)&&!nativeRequested[0]&&S::NativeMenuRequested(),"disabling editing preserves the native configuration entry");
    Check(nativeRunning[0]&&std::strstr(EquipmentMenu::rows[0].label,"Restart required"),"startup-only editing reports pending restart rather than stopping live writers");
    Check(WorkshopTestChoose(A::Feature,0)&&nativeRequested[0],"editing can be re-enabled from the same native settings");
    const unsigned written=f8Writes;WorkshopTestChoose(A::Mode);WorkshopTestChoose(A::Back);
    Check(f8Writes==written&&EquipmentMenu::page==P::Settings,"native Cancel navigation never writes preferences");
    WorkshopTestChoose(A::Mode);WorkshopTestChoose(A::Value,1);workshop::Policy policy{};
    Check(S::Read(policy)&&policy.mode==1&&EquipmentMenu::page==P::Mode,"refinement A persists through the Workshop page");
    f8AllowWrite=false;WorkshopTestChoose(A::Value,2);
    Check(S::Read(policy)&&policy.mode==1&&std::strstr(EquipmentMenu::notice,"Unable"),"failed persistence preserves the previous refinement mode");
    f8AllowWrite=true;WorkshopTestChoose(A::Value,2);WorkshopTestChoose(A::Back);
    WorkshopTestChoose(A::ExpansionSettings);WorkshopTestChoose(A::Value,2);unsigned recipe=0;
    Check(S::ReadExpansion(recipe)&&recipe==2,"native expansion recipe persists");WorkshopTestChoose(A::Back);
    WorkshopTestChoose(A::Development);
    for(unsigned i=0;i<3;++i)WorkshopTestChoose(A::Value,i);
    Check(S::Read(policy)&&policy.devFreeMaterials&&policy.devFreeGil&&policy.devIgnoreProgression,"all three explicit development overrides moved into Workshop");
    Check(C::SetInt(S::DevelopmentKeys[0],2),"fixture injects one malformed development preference");
    EquipmentMenu::Build(P::Development);
    Check(std::strstr(EquipmentMenu::rows[0].label,"Invalid"),"invalid development value is disclosed instead of shown as OFF");
    WorkshopTestChoose(A::Value,0);Check(C::ReadIntExact(S::DevelopmentKeys[0],0,1).value==0,"native configuration repairs an invalid development flag without requiring a valid full policy");
    WorkshopTestChoose(A::Back);WorkshopTestChoose(A::Back);
    Check(EquipmentMenu::page==P::Info&&TestHost::commits==commits&&TestHost::gil==gil&&std::memcmp(&gear,&TestHost::state,sizeof(gear))==0,"configuration-only flow leaves every gear, material and Gil byte unchanged");
    for(const auto* key:{"labs.equipment_workshop","labs.equipment_workshop_native_ui"}){
        const auto* spec=FfxHooks::FindF8Flag(key);Check(spec&&F8NativeNestedFlag(*spec),"Workshop catalog gates no longer appear in F8");}
    const auto before=nativeRequested[0];const auto bulk=FfxHooks::SetF8TabValues("Reforge",false);
    bool touched=false;for(std::size_t i=0;i<bulk.rowCount;++i)touched|=std::strstr(bulk.rows[i].flag->gate.canonicalKey,"equipment_workshop")!=nullptr;
    Check(!touched&&before==nativeRequested[0],"F8 bulk actions do not change native-owned Workshop controls");
    EquipmentMenu::StopReady();C::ResetForTests();
    Check(WorkshopTestOpen(),"localized Workshop fixture opens");
    Check(C::SetString("language.ui_locale","pt"),"private fixture selects Portuguese");
    EquipmentMenu::Build(P::Inventory);
    Check(std::strstr(EquipmentMenu::rows[0].label,"Personagem:")&&WorkshopTestLists(A::Settings,0),"dynamic inventory templates are translated before formatting");
    WorkshopTestChoose(A::Piece,0);NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Arma")&&WorkshopRendered("Refinar")&&!WorkshopRendered("Choose A / B in F8"),"Portuguese labels and Workshop settings reference reach the renderer");
    for(const auto& call:NativeMenu::rendered)Check(FfxHooks::UiLanguage::ValidUtf8(call.text.c_str()),"rendered Portuguese labels never split a UTF-8 character");
    EquipmentMenu::StopReady();EquipmentMenu::featureReader=nullptr;EquipmentMenu::featureWriter=nullptr;C::ResetForTests();
}
