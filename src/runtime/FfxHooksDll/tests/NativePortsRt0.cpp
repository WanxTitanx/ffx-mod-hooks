#include "../hooks/NativePortsCore.h"
#include "../hooks/NativeBindingsCore.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks;
static int checks, failures;
static void Check(bool value, const char* label) {
    ++checks; if (!value) { ++failures; std::printf("FAIL: %s\n",label); }
}
int main() {
    using namespace NativePorts;
    Settings settings{};
    Check(!settings.blockWindows && !settings.backgroundInput && !settings.filterIme &&
          !settings.borderless && !settings.clipCursor && !settings.hideCursor && !settings.performance,
          "every new native behavior defaults OFF");
    settings.blockWindows = settings.backgroundInput = settings.filterIme = true;
    Check(BlockWindowsKey(settings,true,0x5B),"Windows key blocked only for focused game");
    Check(!BlockWindowsKey(settings,false,0x5B),"other applications retain Windows key");
    Check(!BlockWindowsKey(settings,true,0x41),"ordinary keys remain untouched");
    Check(FilterMessage(settings,false,false,0x0100)==MessageAction::Drop,"background key input suppressed");
    Check(FilterMessage(settings,false,false,0x00FF)==MessageAction::DefaultProcess,"raw input still receives native cleanup");
    Check(FilterMessage(settings,true,false,0x0100)==MessageAction::Forward,"focused game keyboard unchanged");
    Check(FilterMessage(settings,true,false,0x010F)==MessageAction::Drop,"IME composition filtered when requested");
    Check(FilterMessage(settings,true,true,0x010F)==MessageAction::Forward,"own text editor retains composition input");
    Check(FilterMessage(settings,false,false,0x0008)==MessageAction::Forward,"focus-loss notification remains native");
    Check(FilterMessage({},false,false,0x0100)==MessageAction::Forward,"OFF passes all normal input");
    Check(FreeCameraAllowed({true,false,true,false}),"native camera control is battle-scoped");
    Check(!FreeCameraAllowed({true,true,true,false}),"opening menus releases camera control");
    Check(!FreeCameraAllowed({false,false,true,false}),"losing focus releases camera control");
    Check(FreezeSceneAllowed({true,false,false,true}),"field time-stop requires a loaded playable field");
    Check(!FreezeSceneAllowed({true,false,true,true}),"field time-stop is not advertised as a battle freeze");
    Check(!FreezeSceneAllowed({true,false,false,false}),"time-stop does not affect title/loading contexts");

    IdleCursorState idle{};
    Check(!HideIdleCursor(idle,100,40,60,true),"idle timing starts when the cursor becomes eligible");
    Check(!HideIdleCursor(idle,2099,40,60,true),"cursor remains visible before two idle seconds");
    Check(HideIdleCursor(idle,2100,40,60,true),"periodic tick hides a stationary cursor without a new mouse message");
    Check(!HideIdleCursor(idle,2200,41,60,true),"a real position change restores the pointer immediately");
    Check(!HideIdleCursor(idle,5000,41,60,false),"disable or focus loss restores the pointer");
    Check(!HideIdleCursor(idle,5001,41,60,true),"focus return starts a fresh idle interval");
    IdleCursorState wrapped{};
    Check(!HideIdleCursor(wrapped,0xFFFFFF00u,0,0,true) &&
        HideIdleCursor(wrapped,0x000006D0u,0,0,true),"idle deadline survives GetTickCount wraparound");

    StyleOwner owner{};
    auto plan = PlanStyle(0x10CF0000u,true,owner);
    Check(plan.write && plan.value==0x90000000u,"borderless style removes frame and adds popup");
    CommitStyle(0x10CF0000u,plan.value,owner);
    Check(!PlanStyle(plan.value,true,owner).write,"owned borderless style is stable");
    auto restore=PlanStyle(plan.value,false,owner);
    Check(restore.write && restore.value==0x10CF0000u,"OFF restores exact original style");
    auto conflict=PlanStyle(plan.value^0x100u,false,owner);
    Check(!conflict.write && conflict.conflict,"foreign window edits are not overwritten");

    using namespace NativeBindings;
    Table bindings=Defaults();
    Check(bindings[static_cast<unsigned>(Action::Performance)].key==0,"new shortcuts begin unbound");
    Check(bindings[static_cast<unsigned>(Action::MenuF7)].key==0x76 && bindings[static_cast<unsigned>(Action::MenuF8)].key==0x77,
          "existing menu defaults remain reachable");
    Check(Validate(bindings,Action::Performance,{'K',Control|Shift})==BindResult::Protected,"SpeedHack shortcut is reserved unchanged");
    Check(Validate(bindings,Action::Performance,{0x77,0})==BindResult::Duplicate,"duplicate shortcut rejected");
    Check(Validate(bindings,Action::Performance,{0x73,Alt})==BindResult::Reserved,"OS close shortcut rejected");
    Check(Validate(bindings,Action::Performance,{0x1B,0})==BindResult::Reserved,"escape remains cancel");
    Check(Validate(bindings,Action::MenuF7,{0x0D,0})==BindResult::Reserved,"menu toggles cannot consume their own confirmation key");
    Check(Validate(bindings,Action::NativeTurbo,{0x70,Control})==BindResult::Reserved,"native F1-F5 keys cannot recursively trigger their aliases");
    Check(Validate(bindings,Action::Performance,{0x2D,0})==BindResult::Reserved,"existing Insert menu alias is reserved");
    Check(Validate(bindings,Action::Performance,{0x78,Control|Alt})==BindResult::Reserved,"existing developer chord is reserved");
    Check(Validate(bindings,Action::Performance,{'P',Control})==BindResult::Ok,"valid user binding accepted");
    bindings[static_cast<unsigned>(Action::Performance)]={'P',Control};
    Check(Resolve(bindings,'P',Control)==Action::Performance,"exact user chord resolves");
    Check(Resolve(bindings,'P',Control|Shift)==Action::None,"additional modifier does not trigger action");
    Check(Validate(bindings,Action::Performance,{})==BindResult::Ok,"unbind is always available");
    char label[40]{};
    Check(Format({'P',Control},label,sizeof(label)) && std::strcmp(label,"Ctrl+P")==0,"binding label is readable");
    Check(Format({},label,sizeof(label)) && std::strcmp(label,"Unassigned")==0,"unbound state is explicit");
    Binding roundtrip{};
    Check(Decode(Encode({'P',Control|Alt}),&roundtrip) && roundtrip.key=='P' && roundtrip.modifiers==(Control|Alt),"bounded numeric config roundtrip");
    Check(!Decode(-1,&roundtrip) && !Decode(0x7FFFFFFF,&roundtrip),"invalid persisted values rejected");
    std::printf("NativePortsRt0: %d/%d checks passed; failures=%d\n",checks-failures,checks,failures);
    return failures?1:0;
}
