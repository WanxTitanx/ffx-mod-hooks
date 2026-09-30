// Exercise the real settings callbacks. Only device and persistence endpoints
// are supplied by the existing bounded host; no audio device or game is loaded.
static void F8SoundFeedbackCases(){
    using namespace NativeMenu;using namespace FfxHooks;
    EquipmentMenu::StopReady();foreground=true;
    Config::ResetForTests();Config::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-f8-sounds.ini");
    Config::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});
    f8AllowWrite=true;
    const int obj=Alloc();Check(obj!=0,"sound fixture allocates native menu");if(!obj)return;
    const auto open=[&](NativeSettingsPage page){
        F8NativeSettingsReset();pointerSample={};pointerState={};padEdge=padDirection=0;
        F8NativeSettingsPush(obj,page);g_f7ConfirmTimer=0;g_nativeSettingsLastEdge=0;sounds.clear();
    };
    const auto activate=[&](int row){
        WrW(obj,O_SELECTED,static_cast<short>(row));g_f7ConfirmTimer=0;g_nativeSettingsLastEdge=0;
        pointerSample={};padDirection=0;padEdge=0x20;sounds.clear();
        F8NativeSettingsInput(obj);padEdge=0;
    };
    // All choice-page families share this production input path. Capture pages
    // reserve keyboard input and are tested through their own completion path.
    for(int raw=1;raw<=static_cast<int>(NativeSettingsPage::InterfaceLanguage);++raw){
        const auto page=static_cast<NativeSettingsPage>(raw);
        if(page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture||
           page==NativeSettingsPage::ElementNameCapture)continue;
        open(page);if(RdW(obj,O_COUNT)<2)continue;WrW(obj,O_SELECTED,0);WrW(obj,O_TOP,0);
        F8NativeSettingsInput(obj);Check(sounds.empty(),"each idle F8 settings page stays silent");
        padDirection=0x4000;F8NativeSettingsInput(obj);padDirection=0;
        Check(RdW(obj,O_SELECTED)==1&&sounds==std::vector<int>{1},"each F8 page emits one keyboard/controller row-change sound");
        sounds.clear();pointerSample.valid=true;pointerSample.x=.99f;pointerSample.y=.5f;F8NativeSettingsInput(obj);
        pointerSample.x=.3f;pointerSample.y=F8NativeSettingsTop()+.175f+.02f;
        F8NativeSettingsInput(obj);F8NativeSettingsInput(obj);
        Check(RdW(obj,O_SELECTED)==0&&sounds==std::vector<int>{1},"each F8 page emits hover once; stationary pointer is silent");
        pointerSample={};activate(RdW(obj,O_COUNT)-1);
        Check(!F8NativeSettingsActive()&&sounds==std::vector<int>{4},"each F8 Back row emits one native cancel sound");
    }
    for(unsigned locale=0;locale<UiLanguage::LocaleCount;++locale){
        open(NativeSettingsPage::InterfaceLanguage);activate(static_cast<int>(locale));
        Check(!F8NativeSettingsActive()&&sounds==std::vector<int>{1}&&
            static_cast<unsigned>(UiLanguage::Settings::Current())==locale,
            "every interface locale persists with one confirmation and no audio override");
    }
    open(NativeSettingsPage::InterfaceLanguage);f8AllowWrite=false;activate(0);
    Check(F8NativeSettingsActive()&&sounds==std::vector<int>{3}&&
        UiLanguage::Settings::Current()==UiLanguage::Locale::Chinese,
        "failed interface locale save preserves its previous setting and emits only error");
    f8AllowWrite=true;activate(0);
    Check(UiLanguage::Settings::Current()==UiLanguage::Locale::English,
        "interface selection recovers after a refused write");
    open(NativeSettingsPage::Languages);activate(0);
    Check(F8NativeSettingsPage()==NativeSettingsPage::LanguageChoice&&sounds==std::vector<int>{1},"opening an F8 child confirms once");
    for(int frame=0;frame<20;++frame){padEdge=0x20;F8NativeSettingsInput(obj);}
    Check(sounds==std::vector<int>{1},"held confirmation does not replay on the child page");padEdge=0;
    open(NativeSettingsPage::WorkshopRefinement);activate(1);
    Check(sounds==std::vector<int>{1},"successful F8 persistence emits confirmation, not cancel");
    open(NativeSettingsPage::WorkshopRefinement);f8AllowWrite=false;activate(0);
    Check(F8NativeSettingsActive()&&sounds==std::vector<int>{3},"failed F8 persistence emits only error and preserves its page");f8AllowWrite=true;
    open(NativeSettingsPage::ElementScan);activate(8);
    Check(sounds==std::vector<int>{3},"unavailable element reports an error rather than confirmation");
    open(NativeSettingsPage::PhotoMode);activate(0);
    Check(sounds==std::vector<int>{3},"rejected Photo Mode action is audible");
    open(NativeSettingsPage::Seymour);activate(0);
    Check(sounds==std::vector<int>{3},"failed Seymour action is audible");
    open(NativeSettingsPage::ElementColor);g_nativeElementHsv={0,50,50};padDirection=0x2000;F8NativeSettingsInput(obj);padDirection=0;
    Check(sounds==std::vector<int>{1},"fine color changes emit one adjustment sound");
    open(NativeSettingsPage::Languages);padEdge=0x40;F8NativeSettingsInput(obj);padEdge=0;
    Check(!F8NativeSettingsActive()&&sounds==std::vector<int>{4},"controller/keyboard cancel emits the native back sound");
    open(NativeSettingsPage::Languages);pointerSample.valid=true;pointerSample.x=.99f;pointerSample.y=.5f;F8NativeSettingsInput(obj);
    pointerSample.x=.3f;pointerSample.y=F8NativeSettingsTop()+.175f+.052f+.02f;pointerSample.buttonDown=true;
    F8NativeSettingsInput(obj);
    Check(F8NativeSettingsPage()==NativeSettingsPage::LanguageChoice&&sounds==std::vector<int>{1},"pointer row-change plus click emits only the action sound");
    for(auto capturePage:{NativeSettingsPage::KeyCapture,NativeSettingsPage::PadCapture})for(int outcome:{0,1,2}){
        open(capturePage);NativePorts::captureResultArmed=true;NativePorts::captureResultSaved=outcome==1;NativePorts::captureResultCancelled=outcome==2;
        F8NativeSettingsInput(obj);
        Check(!F8NativeSettingsActive()&&sounds==std::vector<int>{outcome==2?4:outcome==1?1:3},"both shortcut capture paths distinguish saved, canceled and rejected");
    }
    open(NativeSettingsPage::ElementNameCapture);activate(0);
    Check(sounds.empty(),"typing instruction is an information-only row");
    ElementNameInput::Begin("Test");ElementNameInput::Message(WM_CHAR,0x2603);sounds.clear();
    F8NativeSettingsInput(obj);F8NativeSettingsInput(obj);
    Check(sounds==std::vector<int>{3},"unsupported typed input gives one error without a per-frame beep");
    for(auto parent:{NativeSettingsPage::Languages,NativeSettingsPage::Controller,NativeSettingsPage::RewardMultipliers}){
        open(parent);while(g_nativeSettingsDepth<4)F8NativeSettingsPush(obj,parent);activate(parent==NativeSettingsPage::RewardMultipliers?1:0);
        Check(g_nativeSettingsDepth==4&&sounds==std::vector<int>{3},"a full submenu stack rejects opening without false confirmation");
    }
    open(NativeSettingsPage::VanguardCommandEdit);g_vanguardBindingCommand=0x313F;activate(0);
    Check(sounds.empty(),"command increment at its bound is silent");
    g_vanguardBindingCommand=0x3000;activate(1);Check(sounds.empty(),"command decrement at its bound is silent");
    g_vanguardBindingCost=255;activate(2);Check(sounds.empty(),"cost increment at its bound is silent");
    g_vanguardBindingCost=0;activate(3);Check(sounds.empty(),"cost decrement at its bound is silent");
    g_vanguardBindingCost=256;activate(4);Check(sounds.empty(),"unchanged default cost draft is silent");
    open(NativeSettingsPage::VanguardMappingEdit);g_vanguardMappingId=4095;activate(0);Check(sounds.empty(),"mapping increment at its bound is silent");
    g_vanguardMappingId=135;activate(1);Check(sounds.empty(),"mapping decrement at its bound is silent");
    open(NativeSettingsPage::ElementColor);g_nativeElementHsv={0,100,100};activate(1);Check(sounds.empty(),"clamped saturation confirmation is silent");
    activate(2);Check(sounds.empty(),"clamped brightness confirmation is silent");
    open(NativeSettingsPage::MonsterRewardId);g_rewardIdDraft=0;activate(1);Check(sounds.empty(),"minimum monster ID adjustment is silent");
    g_rewardIdDraft=MonsterRewards::SpeciesCount-1;activate(0);Check(sounds.empty(),"maximum monster ID adjustment is silent");
    open(NativeSettingsPage::RewardRate);g_rewardGlobal=true;g_rewardRateDraft=100;activate(1);Check(sounds.empty(),"maximum global reward adjustment is silent");
    activate(5);Check(sounds.empty(),"unchanged reward default draft is silent");
    g_rewardRateDraft=1;activate(2);Check(sounds.empty(),"minimum reward adjustment is silent");
    open(NativeSettingsPage::ElementNameEdit);g_nativeElementNameDraft[0]=0;activate(4);Check(sounds.empty(),"clearing an empty name draft is silent");
    open(NativeSettingsPage::Languages);
    {FfxHooks::MenuAudio::Scope parent;padDirection=0x4000;F8NativeSettingsInput(obj);padDirection=0;
        Check(sounds.empty(),"real nested F8 input waits for its parent feedback outcome");}
    Check(sounds==std::vector<int>{1},"real nested F8 input flushes one final navigation cue");
    open(NativeSettingsPage::MonsterRewardDetail);WrW(obj,O_SELECTED,1);padDirection=0x1000;padEdge=0x20;
    F8NativeSettingsInput(obj);padDirection=padEdge=0;
    Check(RdW(obj,O_SELECTED)==0&&sounds==std::vector<int>{1},"navigation into an informational row is still audible when Confirm has no action");
    F8NativeSettingsReset();Reset(obj);Config::ResetForTests();padEdge=padDirection=0;pointerSample={};pointerState={};
}
