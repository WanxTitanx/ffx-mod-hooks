// Included by the existing native F8 shell after its row and pointer helpers.
// The same menu object owns these choice pages; no second overlay is created.
#include "EquipmentWorkshopSettings.h"
#include "ElementScanSettings.h"
#include "ElementHook.h"
enum class NativeSettingsPage { None, Keyboard, Gamepad, Controller, ControllerPort, Mapping, Destination, KeyCapture, PadCapture, Languages, LanguageChoice, WorkshopRefinement, Workshop, WorkshopExpansion, ElementScan, ElementColor, ElementBit, ElementVisibility };
struct NativeSettingsFrame {NativeSettingsPage page=NativeSettingsPage::None;int selected=0,top=0;};
static NativeSettingsFrame g_nativeSettingsFrames[4]{};
static int g_nativeSettingsDepth=0,g_nativeSettingsParentRow=0,g_nativeSettingsParentTop=0;
static int g_nativeSettingsAction=0,g_nativeSettingsMapFrom=0,g_nativeSettingsLastEdge=0;
static int g_nativeSettingsLanguage=0;
static const char* const g_nativeLanguageKeys[]={"language.voice","language.sfx","language.video"};
static char g_nativeSettingsNotice[128]{};
static unsigned g_nativeElementIndex=0;
static FfxHooks::ElementScan::Hsv g_nativeElementHsv{};
static std::uint32_t g_nativeElementRgb=0;
static bool g_nativeElementDirty=false;
static int g_nativeElementRepeat=0;
static void F8NativeElementAdjust(int row,int delta){
    if(row<0||row>2)return;
    if(row==0)g_nativeElementHsv.h=static_cast<unsigned>((static_cast<int>(g_nativeElementHsv.h)+360+delta)%360);
    else {auto& value=row==1?g_nativeElementHsv.s:g_nativeElementHsv.v;value=static_cast<unsigned>((std::max)(0,(std::min)(100,static_cast<int>(value)+delta)));}
    g_nativeElementDirty=true;
}
static bool F8NativeSettingsActive(){return g_nativeSettingsDepth>0;}
static NativeSettingsPage F8NativeSettingsPage(){return g_nativeSettingsDepth?g_nativeSettingsFrames[g_nativeSettingsDepth-1].page:NativeSettingsPage::None;}
static int F8NativeSettingsCount(NativeSettingsPage page){
    switch(page){
    case NativeSettingsPage::Keyboard:case NativeSettingsPage::Gamepad:return static_cast<int>(FfxHooks::NativeBindings::Action::Count)+1;
    case NativeSettingsPage::Controller:return 4;
    case NativeSettingsPage::ControllerPort:return 6;
    case NativeSettingsPage::Mapping:return 12;
    case NativeSettingsPage::Destination:return 11;
    case NativeSettingsPage::KeyCapture:case NativeSettingsPage::PadCapture:return 2;
    case NativeSettingsPage::Languages:case NativeSettingsPage::LanguageChoice:return 4;
    case NativeSettingsPage::WorkshopRefinement:return 3;
    case NativeSettingsPage::Workshop:return 6;
    case NativeSettingsPage::WorkshopExpansion:return 3;
    case NativeSettingsPage::ElementScan:return 6;
    case NativeSettingsPage::ElementColor:return 5;
    case NativeSettingsPage::ElementBit:return 3;
    case NativeSettingsPage::ElementVisibility:return 4;
    default:return 0;
    }
}
static float F8NativeSettingsHeight(){return 0.285f+0.052f*static_cast<float>((std::min)(9,F8NativeSettingsCount(F8NativeSettingsPage())));}
static float F8NativeSettingsTop(){return (1.0f-F8NativeSettingsHeight())*0.5f;}
static void F8NativeSettingsSetGeometry(int obj){
    using namespace NativeMenu;
    auto& frame=g_nativeSettingsFrames[g_nativeSettingsDepth-1];
    WrW(obj,O_COUNT,static_cast<int16_t>(F8NativeSettingsCount(frame.page)));
    WrW(obj,O_PAGE,9);WrW(obj,O_SELECTED,static_cast<int16_t>(frame.selected));WrW(obj,O_TOP,static_cast<int16_t>(frame.top));
    F7SeedPointerForDestination();InterlockedExchange(&g_f7MouseWheelDelta,0);
    g_nativeSettingsLastEdge=PadEdge();g_f7ConfirmTimer=12;
}
static void F8NativeSettingsPush(int obj,NativeSettingsPage page){
    if(g_nativeSettingsDepth>=4)return;
    if(g_nativeSettingsDepth){auto& frame=g_nativeSettingsFrames[g_nativeSettingsDepth-1];frame.selected=NativeMenu::RdW(obj,NativeMenu::O_SELECTED);frame.top=NativeMenu::RdW(obj,NativeMenu::O_TOP);}
    else {g_nativeSettingsParentRow=NativeMenu::RdW(obj,NativeMenu::O_SELECTED);g_nativeSettingsParentTop=NativeMenu::RdW(obj,NativeMenu::O_TOP);}
    g_nativeSettingsFrames[g_nativeSettingsDepth++]={page,0,0};
    if(page==NativeSettingsPage::WorkshopRefinement){workshop::Policy policy{};
        if(FfxHooks::EquipmentWorkshop::Settings::Read(policy))g_nativeSettingsFrames[g_nativeSettingsDepth-1].selected=policy.mode==1?1:0;
        else strncpy_s(g_nativeSettingsNotice,"Invalid Workshop setting. Select a valid mode; check other INI costs.",_TRUNCATE);
    }
    if(page==NativeSettingsPage::WorkshopExpansion){unsigned recipe=0;
        if(FfxHooks::EquipmentWorkshop::Settings::ReadExpansion(recipe))g_nativeSettingsFrames[g_nativeSettingsDepth-1].selected=static_cast<int>(recipe-1);
    }
    if(page==NativeSettingsPage::ElementColor){
        const FfxHooks::ElementScan::Settings defaults{};
        const auto read=FfxHooks::Config::ReadIntExact(FfxHooks::ElementScan::ColorKeys[g_nativeElementIndex],0,0xFFFFFF);
        g_nativeElementRgb=read.state==FfxHooks::Config::IntReadState::Valid?static_cast<std::uint32_t>(read.value):defaults.rgb[g_nativeElementIndex];
        g_nativeElementHsv=FfxHooks::ElementScan::ToHsv(g_nativeElementRgb);g_nativeElementDirty=false;g_nativeElementRepeat=0;
    }
    if(page==NativeSettingsPage::ElementBit){const auto read=FfxHooks::Config::ReadIntExact(FfxHooks::ElementScan::BitKey,32,64);
        g_nativeSettingsFrames[g_nativeSettingsDepth-1].selected=read.state==FfxHooks::Config::IntReadState::Valid&&read.value==64?1:0;}
    F8NativeSettingsSetGeometry(obj);
}
static void F8NativeSettingsReset(){g_nativeSettingsDepth=0;FfxHooks::NativePorts::CancelBindingCapture();g_nativeSettingsNotice[0]=0;}
static void F8NativeSettingsPop(int obj){
    if(!g_nativeSettingsDepth)return;
    FfxHooks::NativePorts::CancelBindingCapture();--g_nativeSettingsDepth;
    if(g_nativeSettingsDepth)F8NativeSettingsSetGeometry(obj);
    else {
        NativeMenu::WrW(obj,NativeMenu::O_COUNT,static_cast<int16_t>(g_f7RowCount));
        NativeMenu::WrW(obj,NativeMenu::O_PAGE,FfxHooks::F8Ui::Layout::VisibleRows);
        NativeMenu::WrW(obj,NativeMenu::O_SELECTED,static_cast<int16_t>(g_nativeSettingsParentRow));
        NativeMenu::WrW(obj,NativeMenu::O_TOP,static_cast<int16_t>(g_nativeSettingsParentTop));
        F7SeedPointerForDestination();g_f7ConfirmTimer=12;g_f7LastEdge=NativeMenu::PadEdge();
    }
}
static const char* F8NativeSettingsTitle(NativeSettingsPage page){
    switch(page){
    case NativeSettingsPage::Keyboard:return "Keyboard shortcuts";
    case NativeSettingsPage::Gamepad:return "Gamepad shortcuts";
    case NativeSettingsPage::Controller:return "Gamepad settings";
    case NativeSettingsPage::ControllerPort:return "Choose a controller";
    case NativeSettingsPage::Mapping:return "Remap gamepad buttons";
    case NativeSettingsPage::Destination:return "Choose the game action button";
    case NativeSettingsPage::KeyCapture:return "Press a keyboard shortcut";
    case NativeSettingsPage::PadCapture:return "Press a gamepad combination";
    case NativeSettingsPage::Languages:return "Audio languages";
    case NativeSettingsPage::LanguageChoice:return g_nativeSettingsLanguage==0?"Voice language":g_nativeSettingsLanguage==1?"Battle sound language":"Movie audio language";
    case NativeSettingsPage::WorkshopRefinement:return "Workshop refinement";
    case NativeSettingsPage::Workshop:return "Equipment Workshop";
    case NativeSettingsPage::WorkshopExpansion:return "Expansion recipe";
    case NativeSettingsPage::ElementScan:return "Scan element colors";
    case NativeSettingsPage::ElementColor:return g_nativeElementIndex==0?"Holy color":g_nativeElementIndex==1?"Darkness color":"Third element color";
    case NativeSettingsPage::ElementBit:return "Third extra element";
    case NativeSettingsPage::ElementVisibility:return "Enabled Scan elements";
    default:return "Settings";
    }
}
static void F8NativeSettingsLabel(NativeSettingsPage page,int row,char* out,size_t capacity){
    using namespace FfxHooks;
    const int count=F8NativeSettingsCount(page);
    if(row==count-1){strncpy_s(out,capacity,page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture||page==NativeSettingsPage::ElementColor?"Cancel":"Back",_TRUNCATE);return;}
    if(page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture){strncpy_s(out,capacity,"Clear this shortcut",_TRUNCATE);return;}
    if(page==NativeSettingsPage::ElementScan){
        if(row==4){ElementScan::Settings settings{};
            if(ElementScan::ReadSettings(settings))_snprintf_s(out,capacity,_TRUNCATE,"Enabled extra elements: %u / 3",ElementScan::VisibleCount(settings));
            else strncpy_s(out,capacity,"Enabled extra elements: INVALID settings",_TRUNCATE);return;}
        if(row<3){const char* names[]={"Holy","Darkness","Third element"};const ElementScan::Settings defaults{};
            const auto read=Config::ReadIntExact(ElementScan::ColorKeys[row],0,0xFFFFFF);
            if(read.state==Config::IntReadState::Invalid)_snprintf_s(out,capacity,_TRUNCATE,"%s color: INVALID",names[row]);
            else _snprintf_s(out,capacity,_TRUNCATE,"%s color: #%06X",names[row],read.state==Config::IntReadState::Valid?static_cast<unsigned>(read.value):defaults.rgb[row]);
        }else {const auto bit=Config::ReadIntExact(ElementScan::BitKey,32,64);const bool valid=bit.state==Config::IntReadState::Missing||(bit.state==Config::IntReadState::Valid&&(bit.value==32||bit.value==64));
            _snprintf_s(out,capacity,_TRUNCATE,"Third element: %s",!valid?"INVALID":bit.value==64?"Custom - bit 0x40":"Custom - bit 0x20");}return;
    }
    if(page==NativeSettingsPage::ElementColor){
        if(row==0)_snprintf_s(out,capacity,_TRUNCATE,"Hue: %u / 359",g_nativeElementHsv.h);
        else if(row==1)_snprintf_s(out,capacity,_TRUNCATE,"Saturation: %u%%",g_nativeElementHsv.s);
        else if(row==2)_snprintf_s(out,capacity,_TRUNCATE,"Brightness: %u%%",g_nativeElementHsv.v);
        else strncpy_s(out,capacity,"Save color",_TRUNCATE);return;
    }
    if(page==NativeSettingsPage::ElementBit){strncpy_s(out,capacity,row==0?"Custom element - bit 0x20":"Custom element - bit 0x40",_TRUNCATE);return;}
    if(page==NativeSettingsPage::ElementVisibility){
        if(row<0||row>=3)return;
        const char* names[]={"Holy","Darkness","Custom extra"};const auto read=Config::ReadIntExact(ElementScan::EnabledKeys[row],0,1);
        const char* value=read.state==Config::IntReadState::Invalid?"INVALID":read.state==Config::IntReadState::Valid&&!read.value?"OFF":"ON";
        _snprintf_s(out,capacity,_TRUNCATE,"%s: %s",names[row],value);return;
    }
    if(page==NativeSettingsPage::Workshop){
        if(row==0){strncpy_s(out,capacity,"Refinement method: A / B",_TRUNCATE);return;}
        if(row==4){unsigned recipe=0;const bool valid=EquipmentWorkshop::Settings::ReadExpansion(recipe);
            _snprintf_s(out,capacity,_TRUNCATE,"Expansion recipe: %s",valid?(recipe==1?"A - one per slot":"B - 1/2/3/4"):"INVALID");return;}
        const char* labels[]={"Free materials","Free Gil","Ignore Customize progression"};
        if(row<1||row>3){strncpy_s(out,capacity,"Invalid option",_TRUNCATE);return;}
        const auto value=Config::ReadIntExact(EquipmentWorkshop::Settings::DevelopmentKeys[row-1],0,1);
        const char* state=value.state==Config::IntReadState::Invalid?"INVALID":value.state==Config::IntReadState::Valid&&value.value?"ON":"OFF";
        _snprintf_s(out,capacity,_TRUNCATE,"DEV - %s: %s",labels[row-1],state);return;
    }
    if(page==NativeSettingsPage::WorkshopExpansion){
        unsigned recipe=0;const bool valid=EquipmentWorkshop::Settings::ReadExpansion(recipe);
        _snprintf_s(out,capacity,_TRUNCATE,"%s%s",row==0?"A - one Key Sphere per added slot":"B - 1/2/3/4 Key Spheres by slot",valid&&recipe==static_cast<unsigned>(row+1)?" [Selected]":"");return;
    }
    if(page==NativeSettingsPage::WorkshopRefinement){
        workshop::Policy policy{};const bool valid=EquipmentWorkshop::Settings::Read(policy);const unsigned mode=row==0?2u:1u;
        _snprintf_s(out,capacity,_TRUNCATE,"%s%s%s",EquipmentWorkshop::Settings::ModeName(mode),mode==2?" (default)":"",valid&&policy.mode==mode?" [Selected]":"");return;
    }
    if(page==NativeSettingsPage::Languages){
        const char* names[]={"Voices","Battle sounds","Movie audio"};
        const auto choice=static_cast<NativeLanguage::Choice>(Config::GetInt(g_nativeLanguageKeys[row],0));
        _snprintf_s(out,capacity,_TRUNCATE,"%s: %s",names[row],NativeLanguage::Valid(choice)?NativeLanguage::Name(choice):"Invalid setting");return;
    }
    if(page==NativeSettingsPage::LanguageChoice){strncpy_s(out,capacity,NativeLanguage::Name(static_cast<NativeLanguage::Choice>(row)),_TRUNCATE);return;}
    if(page==NativeSettingsPage::Keyboard || page==NativeSettingsPage::Gamepad){
        const auto action=static_cast<NativeBindings::Action>(row);
        const char* value=page==NativeSettingsPage::Keyboard?NativePorts::BindingText(action):NativePorts::GamepadBindingText(action);
        _snprintf_s(out,capacity,_TRUNCATE,"%s: %s",NativeBindings::Name(action),strlen(value)>28?"Configured combination":value);return;
    }
    if(page==NativeSettingsPage::Controller){
        if(row==0){const int port=Config::GetInt("gamepad.controller",-1);if(port<0)strncpy_s(out,capacity,"Controller: Automatic",_TRUNCATE);else _snprintf_s(out,capacity,_TRUNCATE,"Controller: Pad %d",port+1);}
        else strncpy_s(out,capacity,row==1?"Button mapping":"Restore default mapping",_TRUNCATE);
        return;
    }
    if(page==NativeSettingsPage::ControllerPort){if(row==0)strncpy_s(out,capacity,"Automatic - first connected controller",_TRUNCATE);else _snprintf_s(out,capacity,_TRUNCATE,"Pad %d",row);return;}
    if(page==NativeSettingsPage::Mapping){
        if(row==10){strncpy_s(out,capacity,"Restore default mapping",_TRUNCATE);return;}
        const auto map=NativeGamepad::Mapping();
        _snprintf_s(out,capacity,_TRUNCATE,"%s sends %s",NativeGamepad::kButtonNames[row],NativeGamepad::kButtonNames[map[row]]);return;
    }
    if(page==NativeSettingsPage::Destination)_snprintf_s(out,capacity,_TRUNCATE,"%s",NativeGamepad::kButtonNames[row]);
}
static void F8NativeSettingsActivate(int obj,int row){
    using namespace FfxHooks;
    const auto page=F8NativeSettingsPage();
    if(row<0||row>=F8NativeSettingsCount(page))return;
    if(row==F8NativeSettingsCount(page)-1){F8NativeSettingsPop(obj);return;}
    const auto action=static_cast<NativeBindings::Action>(g_nativeSettingsAction);
    if(page==NativeSettingsPage::ElementScan){
        if(row<3){g_nativeElementIndex=static_cast<unsigned>(row);F8NativeSettingsPush(obj,NativeSettingsPage::ElementColor);}
        else F8NativeSettingsPush(obj,row==3?NativeSettingsPage::ElementBit:NativeSettingsPage::ElementVisibility);return;
    }else if(page==NativeSettingsPage::ElementVisibility){
        const auto current=Config::ReadIntExact(ElementScan::EnabledKeys[row],0,1);
        // Missing means ON; invalid is repaired to OFF rather than enabling it.
        const bool next=current.state==Config::IntReadState::Valid&&current.value==0;
        const bool saved=ElementScan::SaveEnabled(static_cast<unsigned>(row),next);
        strncpy_s(g_nativeSettingsNotice,saved?"Element choice saved. Applies next Scan frame when the master is enabled.":"Unable to save element choice. Previous value preserved.",_TRUNCATE);
        return;
    }else if(page==NativeSettingsPage::ElementColor){
        if(row<3){F8NativeElementAdjust(row,row==0?15:10);return;}
        const auto rgb=g_nativeElementDirty?ElementScan::FromHsv(g_nativeElementHsv):g_nativeElementRgb;
        const bool saved=ElementScan::SaveColor(g_nativeElementIndex,rgb);
        strncpy_s(g_nativeSettingsNotice,saved?"Color saved. Visible on the next Scan frame when enabled.":"Unable to save color. Previous value preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);return;
    }else if(page==NativeSettingsPage::ElementBit){
        const bool saved=ElementScan::SaveBit(row==0?32u:64u);
        strncpy_s(g_nativeSettingsNotice,saved?"Third element selected. Gameplay masks are unchanged.":"Unable to save the third element. Previous value preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);return;
    }else if(page==NativeSettingsPage::Workshop){
        if(row==0){F8NativeSettingsPush(obj,NativeSettingsPage::WorkshopRefinement);return;}
        if(row==4){F8NativeSettingsPush(obj,NativeSettingsPage::WorkshopExpansion);return;}
        if(row<1||row>3)return;
        const auto current=Config::ReadIntExact(EquipmentWorkshop::Settings::DevelopmentKeys[row-1],0,1);
        // An invalid value is repaired to OFF, never silently enabled.
        const bool enabled=current.state==Config::IntReadState::Missing || (current.state==Config::IntReadState::Valid&&current.value==0);
        const bool saved=EquipmentWorkshop::Settings::SaveDevelopment(static_cast<unsigned>(row-1),enabled);
        strncpy_s(g_nativeSettingsNotice,saved?"Development setting saved. Prices stay visible; fusion still destroys its donor.":"Unable to save the development setting. Previous value preserved.",_TRUNCATE);
    }else if(page==NativeSettingsPage::WorkshopExpansion){
        const bool saved=EquipmentWorkshop::Settings::SaveExpansion(static_cast<unsigned>(row+1));
        strncpy_s(g_nativeSettingsNotice,saved?"Expansion recipe saved. Existing slots are unchanged.":"Unable to save expansion recipe. Previous choice preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);
    }else if(page==NativeSettingsPage::WorkshopRefinement){
        const bool saved=EquipmentWorkshop::Settings::SaveMode(row==0?2u:1u);
        strncpy_s(g_nativeSettingsNotice,saved?"Saved for future refinements. Existing ability ranks are unchanged.":"Unable to save the refinement mode. Previous setting preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);
    }else if(page==NativeSettingsPage::Languages){g_nativeSettingsLanguage=row;F8NativeSettingsPush(obj,NativeSettingsPage::LanguageChoice);}
    else if(page==NativeSettingsPage::LanguageChoice){
        const bool saved=Config::SetInt(g_nativeLanguageKeys[g_nativeSettingsLanguage],row);
        strncpy_s(g_nativeSettingsNotice,saved?"Saved. Restart FFX to apply the audio language.":"Unable to save the audio language.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);
    } else if(page==NativeSettingsPage::Keyboard||page==NativeSettingsPage::Gamepad){
        g_nativeSettingsAction=row;const auto selected=static_cast<NativeBindings::Action>(row);
        const bool opened=page==NativeSettingsPage::Keyboard?NativePorts::BeginBindingCapture(selected):NativePorts::BeginGamepadCapture(selected);
        if(opened){g_nativeSettingsNotice[0]=0;F8NativeSettingsPush(obj,page==NativeSettingsPage::Keyboard?NativeSettingsPage::KeyCapture:NativeSettingsPage::PadCapture);}
        else strncpy_s(g_nativeSettingsNotice,"No XInput controller detected. Connect it or enable Steam Input.",_TRUNCATE);
    } else if(page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture){
        const bool saved=page==NativeSettingsPage::KeyCapture?NativePorts::SaveBinding(action,{})==NativeBindings::BindResult::Ok:NativePorts::SaveGamepadBinding(action,0);
        strncpy_s(g_nativeSettingsNotice,saved?"Shortcut cleared.":"Unable to save the shortcut.",_TRUNCATE);F8NativeSettingsPop(obj);
    } else if(page==NativeSettingsPage::Controller){
        if(row<2)F8NativeSettingsPush(obj,row==0?NativeSettingsPage::ControllerPort:NativeSettingsPage::Mapping);
        else strncpy_s(g_nativeSettingsNotice,NativeGamepad::SaveMapping(NativeGamepad::IdentityMap())?"Default mapping restored.":"Unable to save mapping.",_TRUNCATE);
    } else if(page==NativeSettingsPage::ControllerPort){
        const bool saved=Config::SetInt("gamepad.controller",row-1);strncpy_s(g_nativeSettingsNotice,saved?"Controller selection saved.":"Unable to save controller selection.",_TRUNCATE);if(saved){NativeGamepad::RefreshMapping();F8NativeSettingsPop(obj);}
    } else if(page==NativeSettingsPage::Mapping){
        if(row==10)strncpy_s(g_nativeSettingsNotice,NativeGamepad::SaveMapping(NativeGamepad::IdentityMap())?"Default mapping restored.":"Unable to save mapping.",_TRUNCATE);
        else {g_nativeSettingsMapFrom=row;F8NativeSettingsPush(obj,NativeSettingsPage::Destination);}
    } else if(page==NativeSettingsPage::Destination){
        auto map=NativeGamepad::Mapping();NativeGamepad::SwapDestination(map,static_cast<unsigned>(g_nativeSettingsMapFrom),static_cast<unsigned>(row));
        const bool saved=NativeGamepad::SaveMapping(map);strncpy_s(g_nativeSettingsNotice,saved?"Mapping saved. Shortcuts still use physical buttons.":"Unable to save mapping.",_TRUNCATE);if(saved)F8NativeSettingsPop(obj);
    }
}
static void F8NativeSettingsInput(int obj){
    using namespace NativeMenu;using namespace FfxHooks;
    const auto page=F8NativeSettingsPage();const bool capturing=page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture;
    if(g_f7ConfirmTimer>0)--g_f7ConfirmTimer;
    if(capturing){
        bool done=false,saved=false,cancelled=false;
        if(page==NativeSettingsPage::KeyCapture){NativeBindings::BindResult result{};done=NativePorts::ConsumeBindingCapture(&result,&cancelled);saved=done&&!cancelled&&result==NativeBindings::BindResult::Ok;}
        else done=NativePorts::ConsumeGamepadCapture(&saved,&cancelled);
        if(done){strncpy_s(g_nativeSettingsNotice,cancelled?"Shortcut edit cancelled.":saved?"Shortcut saved.":"Shortcut not saved. Avoid reserved or duplicate combinations.",_TRUNCATE);F8NativeSettingsPop(obj);PlaySfx(saved?4:3);return;}
    }
    const auto mouse=F7ListMouseTick(obj,NX(0.250f),NY(F8NativeSettingsTop()+0.175f),NW(0.500f),NH(0.052f),NH(0.045f),RdW(obj,O_COUNT),9);
    const int edge=PadEdge(),dir=capturing?0:F7Ui::ResolveDirectionalInput(PadDir(),mouse.ownsDirectionalFrame);
    int selected=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP),count=RdW(obj,O_COUNT);
    if(g_nativeElementRepeat>0)--g_nativeElementRepeat;
    if(page==NativeSettingsPage::ElementColor&&selected<3&&(dir&0xA000)&&!g_nativeElementRepeat){
        F8NativeElementAdjust(selected,(dir&0x8000)?-1:1);g_nativeElementRepeat=6;
    }
    if(dir&0x1000){if(selected>0)--selected;}
    else if(dir&0x4000){if(selected+1<count)++selected;}
    if(selected<top)top=selected;else if(selected>=top+9)top=selected-8;
    WrW(obj,O_SELECTED,static_cast<int16_t>(selected));WrW(obj,O_TOP,static_cast<int16_t>(top));
    const bool confirm=mouse.confirm||(!capturing&&(edge&0x20)&&!(g_nativeSettingsLastEdge&0x20));
    const bool cancel=!capturing&&(edge&0x40)&&!(g_nativeSettingsLastEdge&0x40);
    g_nativeSettingsLastEdge=edge;
    if(g_f7ConfirmTimer==0 && (confirm||cancel)){
        if(cancel)F8NativeSettingsPop(obj);else F8NativeSettingsActivate(obj,selected);
        g_f7ConfirmTimer=12;PlaySfx(1);
    }
}
static void F8NativeSettingsDraw(int obj,int frame){
    using namespace NativeMenu;using namespace FfxHooks;
    const auto page=F8NativeSettingsPage();const int selected=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP),count=RdW(obj,O_COUNT);
    const float panelTop=F8NativeSettingsTop(),panelHeight=F8NativeSettingsHeight();
    auto text=[](const char* label,float x,float y,bool title=false){unsigned char value[128]{};EncodeLabel(label,value,sizeof(value));if(title)DrawString(value,x,y);else DrawStringSub(value,x,y);};
    DrawMenuBackdrop();DrawMenuNeonFrame(frame);
    DrawMenuGlassPanel(NX(0.205f),NY(panelTop),NW(0.590f),NH(panelHeight),frame,1);
    text(F8NativeSettingsTitle(page),NX(0.250f),NY(panelTop+0.037f),true);
    const auto pad=NativeGamepad::Poll();
    const char* help="Select an option. Back returns to F8.";
    if(page==NativeSettingsPage::Keyboard)help="Choose an action, then press its keyboard shortcut.";
    else if(page==NativeSettingsPage::Gamepad)help="Use two or more buttons. Release the combo to save.";
    else if(page==NativeSettingsPage::Controller)help=pad.connected?"XInput / Steam Input controller connected.":"Connect an XInput controller or enable Steam Input.";
    else if(page==NativeSettingsPage::KeyCapture)help="Press keys. Esc cancels; Backspace/Delete clears.";
    else if(page==NativeSettingsPage::PadCapture)help="Press a combo, then release. Mouse: clear or cancel.";
    else if(page==NativeSettingsPage::Destination)help="Used targets swap places. Every button stays mapped.";
    else if(page==NativeSettingsPage::Languages)help="Voices, battle sounds and movie audio. Restart required.";
    else if(page==NativeSettingsPage::LanguageChoice)help="Game default, English or Japanese. Restart required.";
    else if(page==NativeSettingsPage::Workshop)help="Development only. Free materials still require one each.";
    else if(page==NativeSettingsPage::WorkshopRefinement)help="B rolls one ability. A improves all. Existing ranks stay.";
    else if(page==NativeSettingsPage::WorkshopExpansion)help="Each added slot uses its matching Key Sphere level.";
    else if(page==NativeSettingsPage::ElementScan)help=IsElementHookInstalled()?"Extra elements: ON. Color and element choices apply next frame.":"Extra elements: OFF. Enable Scan Extra Elements; restart.";
    else if(page==NativeSettingsPage::ElementColor)help="Left/Right: fine change. Enter: larger step. Save or Cancel.";
    else if(page==NativeSettingsPage::ElementBit)help="Only changes the displayed bit; adds no gameplay effects.";
    else if(page==NativeSettingsPage::ElementVisibility)help="Independent choices. Scan Extra Elements must be ON.";
    text(help,NX(0.250f),NY(panelTop+0.106f));
    for(int row=top;row<count&&row<top+9;++row){
        const float y=NY(panelTop+0.175f+(row-top)*0.052f);
        DrawSolidRect(NX(0.250f),y,NW(0.500f),NH(0.045f),0x50314558u,0x30303A48u);
        if(row==selected){DrawSolidRect(NX(0.250f),y,NW(0.500f),NH(0.045f),0x60345263u,0x40273849u);DrawSolidRect(NX(0.250f),y+NH(0.043f),NW(0.500f),NH(0.002f),kMenuNeonGreenLine,kMenuNeonGreenLineLo);DrawCursor(NX(0.223f),y);}
        char label[100]{};F8NativeSettingsLabel(page,row,label,sizeof(label));text(label,NX(0.265f),y+NH(0.012f));
    }
    if(page==NativeSettingsPage::ElementColor){
        const auto rgb=g_nativeElementDirty?ElementScan::FromHsv(g_nativeElementHsv):g_nativeElementRgb;
        DrawSolidRect(NX(.718f),NY(panelTop+.033f),NW(.028f),NH(.040f),0x80000000u|rgb,0x80000000u|rgb);
        for(unsigned i=0;i<36;++i){const auto color=0x80000000u|ElementScan::FromHsv({i*10,100,100});
            DrawSolidRect(NX(.25f+static_cast<float>(i)*(.5f/36)),NY(panelTop+panelHeight-.111f),NW(.5f/36),NH(.012f),color,color);}
        DrawSolidRect(NX(.25f+static_cast<float>(g_nativeElementHsv.h)/360*.5f),NY(panelTop+panelHeight-.114f),NW(.002f),NH(.018f),0x80FFFFFFu,0x80FFFFFFu);
    }
    char first[57]{},second[73]{};strncpy_s(first,g_nativeSettingsNotice,56);
    if(strlen(g_nativeSettingsNotice)>56)strncpy_s(second,g_nativeSettingsNotice+56,_TRUNCATE);
    text(first,NX(0.250f),NY(panelTop+panelHeight-0.09f));text(second,NX(0.250f),NY(panelTop+panelHeight-0.06f));
    text("Mouse/Scroll  Arrows: move  Enter: select  Back: return",NX(0.250f),NY(panelTop+panelHeight-0.03f));
}
