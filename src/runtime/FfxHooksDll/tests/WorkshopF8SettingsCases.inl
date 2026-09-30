// Real F8 choice pages; unrelated device and recovery endpoints are substituted.
#include "RecoveryMenuBoundaries.inl"
#include "../hooks/NativePortsHook.h"
#include "../hooks/NativeGamepadHook.h"
#include "../hooks/NativeLanguageCore.h"
#include "../hooks/TextLanguageHook.h"
namespace FfxHooks::TextLanguage::Native {
Snapshot Inspect() noexcept {return {};}
const char* Detail() noexcept {return "Original text language";}
}

namespace FfxHooks::NativePorts {
void CancelBindingCapture(){}
const char* BindingText(NativeBindings::Action){return "Unassigned";}
const char* GamepadBindingText(NativeBindings::Action){return "Unassigned";}
bool BeginBindingCapture(NativeBindings::Action){return false;}
bool BeginGamepadCapture(NativeBindings::Action){return false;}
NativeBindings::BindResult SaveBinding(NativeBindings::Action,NativeBindings::Binding){return NativeBindings::BindResult::Ok;}
bool SaveGamepadBinding(NativeBindings::Action,std::uint32_t){return true;}
bool captureResultArmed=false,captureResultSaved=false,captureResultCancelled=false;
bool ConsumeBindingCapture(NativeBindings::BindResult* result,bool* cancel){
 if(!captureResultArmed)return false;captureResultArmed=false;*cancel=captureResultCancelled;
 *result=captureResultSaved?NativeBindings::BindResult::Ok:static_cast<NativeBindings::BindResult>(1);return true;
}
bool ConsumeGamepadCapture(bool* saved,bool* cancel){
 if(!captureResultArmed)return false;captureResultArmed=false;*saved=captureResultSaved;*cancel=captureResultCancelled;return true;
}
}
namespace FfxHooks::NativeGamepad {
Snapshot Poll(){return {};}
ButtonMap Mapping(){return IdentityMap();}
bool SaveMapping(const ButtonMap&){return true;}
void RefreshMapping(){}
}
static int g_f7RowCount=19;
#include "../hooks/NativeSettingsUi.inl"
static bool f8AllowWrite=true;
static unsigned f8Writes=0;
static std::string f8Saved;
static bool WorkshopF8Persist(void*,const char*,const char* content){++f8Writes;if(!f8AllowWrite)return false;f8Saved=content;return true;}
static void WorkshopF8SettingsCases(){
    namespace C=FfxHooks::Config;
    namespace S=FfxHooks::EquipmentWorkshop::Settings;
    EquipmentMenu::StopReady();pointerSample={};pointerState={};NativeMenu::padEdge=NativeMenu::padDirection=0;
    C::ResetForTests();C::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-f8-workshop.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});
    const int obj=NativeMenu::Alloc();Check(obj!=0,"private F8 pool allocation");if(!obj)return;
    NativeMenu::WrW(obj,NativeMenu::O_SELECTED,5);NativeMenu::WrW(obj,NativeMenu::O_TOP,2);
    const auto gearBefore=TestHost::state;
    F8NativeSettingsPush(obj,NativeSettingsPage::WorkshopRefinement);
    Check(F8NativeSettingsActive()&&NativeMenu::RdW(obj,NativeMenu::O_COUNT)==3&&NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==0,"F8 opens B-default A/B/back choice page");
    char label[128]{};F8NativeSettingsLabel(NativeSettingsPage::WorkshopRefinement,0,label,sizeof(label));
    Check(std::strstr(label,"default")&&std::strstr(label,"[Selected]"),"F8 marks the default without creating a config entry");
    NativeMenu::rendered.clear();F8NativeSettingsDraw(obj,1);
    Check(WorkshopRendered("Workshop refinement")&&WorkshopRendered("random ability")&&WorkshopRendered("whole equipment"),"actual F8 renderer shows both named choices");
    NativeMenu::WrW(obj,NativeMenu::O_SELECTED,1);g_f7ConfirmTimer=0;NativeMenu::padEdge=0x20;
    F8NativeSettingsInput(obj);NativeMenu::padEdge=0;workshop::Policy policy{};
    Check(!F8NativeSettingsActive()&&S::Read(policy)&&policy.mode==1&&f8Writes==1,"actual F8 selection persists A and closes its page");
    Check(NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==5&&NativeMenu::RdW(obj,NativeMenu::O_TOP)==2,"F8 mode choice preserves parent selection and scroll");
    F8NativeSettingsPush(obj,NativeSettingsPage::WorkshopRefinement);
    Check(NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==1,"reopening F8 highlights the saved A choice");
    F8NativeSettingsActivate(obj,2);
    Check(!F8NativeSettingsActive()&&f8Writes==1&&S::Read(policy)&&policy.mode==1,"Back never changes or persists the mode");
    F8NativeSettingsPush(obj,NativeSettingsPage::WorkshopRefinement);f8AllowWrite=false;
    F8NativeSettingsActivate(obj,0);
    Check(F8NativeSettingsActive()&&S::Read(policy)&&policy.mode==1&&std::strstr(g_nativeSettingsNotice,"Unable"),"F8 write failure preserves the active mode and reports failure");
    f8AllowWrite=true;F8NativeSettingsActivate(obj,0);
    Check(!F8NativeSettingsActive()&&S::Read(policy)&&policy.mode==2,"F8 returns to B through the real persistence adapter");
    Check(std::memcmp(&gearBefore,&TestHost::state,sizeof(gearBefore))==0,"F8 choices never mutate equipment ranks");
    const auto written=f8Saved;C::LoadTextForTests(written.c_str(),"C:\\private-f8-workshop.ini");
    Check(S::Read(policy)&&policy.mode==2,"saved F8 mode survives configuration reload");
    F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}
