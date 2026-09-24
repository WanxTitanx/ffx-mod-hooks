// Jarvis-HOOK. UnX input behavior reference: Kaldaien/UnX, faae4359, input.cpp
// and window.cpp (GPL-3.0-or-later file headers; MIT repository license).
// Independent adapters use the existing Hooks window boundary, not a proxy.
#include "NativePortsHook.h"
#include "NativeGamepadHook.h"
#include "F8FlagCatalog.h"
#include "F8RuntimeCore.h"
#include "../shared/Config.h"
#include "../shared/ffx_addresses.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace FfxHooks::NativePorts {
namespace {
constexpr UINT kUpdateMessage=WM_APP+0x2D7B;
constexpr WPARAM kUpdateToken=0x484F4F4Bu;
std::atomic<unsigned> g_settings{0};
std::atomic<bool> g_stop{true},g_supported{false},g_keyboardReady{false};
std::atomic<bool> g_borderlessApplied{false},g_cursorClipped{false},g_conflict{false};
std::atomic<HWND> g_window{nullptr};
std::atomic<DWORD> g_keyboardThreadId{0};
HANDLE g_keyboardThread=nullptr;
LogFn g_log=nullptr;
bool g_wine=false;
DWORD g_lastTick=0;
uintptr_t g_base=0;
bool g_cameraProfile=false;
SRWLOCK g_cameraLock=SRWLOCK_INIT;
F8Runtime::OwnedByte g_cameraOwner{},g_freezeOwner{};
constexpr uint32_t kFreeCameraRva=RVA_FFX_DEBUG_FLAGS+4u;
constexpr uint32_t kFreezeSceneRva=0x00EFBB63u;
SRWLOCK g_bindingLock=SRWLOCK_INIT;
NativeBindings::Table g_bindings=NativeBindings::Defaults();
std::atomic<int> g_captureAction{-1},g_captureValue{-1};
std::atomic<unsigned> g_releaseBeforeShortcut{0};
std::array<NativeGamepad::EdgeState,static_cast<unsigned>(NativeBindings::Action::Count)> g_padEdges{};
std::atomic<unsigned> g_padActionEdges{0};
std::atomic<unsigned> g_menuOpeningPad{0};
std::atomic<bool> g_refreshWindow{false};
std::atomic<int> g_padCaptureAction{-1},g_padCaptureControl{-1};
SRWLOCK g_padCaptureLock=SRWLOCK_INIT;
NativeGamepad::Capture g_padCapture{};
// These snapshots belong only to the game window's message thread.
StyleOwner g_style{};
WINDOWPLACEMENT g_originalPlacement{sizeof(WINDOWPLACEMENT)};
RECT g_appliedRect{},g_priorClip{},g_appliedClip{};
bool g_clipOwned=false,g_clipConflict=false,g_priorClipUnbounded=false;
IdleCursorState g_idleCursor{};
HCURSOR g_savedCursor=nullptr;
bool g_cursorHidden=false;
#ifdef FFXHOOKS_TESTING
HWND(*g_foregroundReaderForTests)()=nullptr;
bool(*g_pointerReaderForTests)(POINT*)=nullptr;
#endif

void Log(const char* text) {if(g_log)g_log(text);}
Settings Unpack(unsigned bits) {
    return {(bits&1)!=0,(bits&2)!=0,(bits&4)!=0,(bits&8)!=0,(bits&16)!=0,(bits&32)!=0,(bits&64)!=0};
}
bool Focused(HWND window) {
    HWND foreground=GetForegroundWindow();
#ifdef FFXHOOKS_TESTING
    // The OpenSSH window station is not an interactive/visible desktop. Its
    // isolated fixture supplies only an existing owned HWND as the focus witness.
    if(g_foregroundReaderForTests)return window && IsWindow(window) &&
        GetAncestor(g_foregroundReaderForTests(),GA_ROOT)==GetAncestor(window,GA_ROOT);
#endif
    return window && IsWindowVisible(window) && !IsIconic(window) &&
        GetAncestor(foreground,GA_ROOT)==GetAncestor(window,GA_ROOT);
}
bool EqualRect(const RECT& a,const RECT& b) {return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;}
void RestoreClip() {
    RECT current{};
    if(g_clipOwned && GetClipCursor(&current) && EqualRect(current,g_appliedClip)) ClipCursor(g_priorClipUnbounded?nullptr:&g_priorClip);
    g_clipOwned=false;g_cursorClipped=false;
}
void RestoreIdleCursor() {
    if(g_cursorHidden && !GetCursor() && g_savedCursor)SetCursor(g_savedCursor);
    g_cursorHidden=false;g_savedCursor=nullptr;g_idleCursor.sampled=false;
}
void UpdateIdleCursor(HWND window,bool textEditor) {
    POINT point{},origin{};RECT client{};
    bool sampled=GetCursorPos(&point)!=FALSE;
#ifdef FFXHOOKS_TESTING
    if(g_pointerReaderForTests)sampled=g_pointerReaderForTests(&point);
#endif
    const bool inside=sampled && GetClientRect(window,&client) &&
        ClientToScreen(window,&origin) &&
        point.x>=origin.x && point.y>=origin.y &&
        point.x<origin.x+client.right && point.y<origin.y+client.bottom;
    const bool eligible=!g_stop && (g_settings.load()&32u)!=0 && !textEditor && Focused(window) && inside;
    const bool hide=HideIdleCursor(g_idleCursor,GetTickCount(),point.x,point.y,eligible);
    if(hide) {
        // WM_SETCURSOR may never arrive while idle. The existing periodic message
        // invokes this on the window thread without touching ShowCursor's counter.
        HCURSOR previous=SetCursor(nullptr);
        if(previous){g_savedCursor=previous;g_cursorHidden=true;}
    } else if(g_cursorHidden) {
        if(!GetCursor() && g_savedCursor)SetCursor(g_savedCursor);
        g_cursorHidden=false;g_savedCursor=nullptr;
    }
}
void ApplyWindow(HWND window,bool textEditor) {
    const auto config=Unpack(g_stop?0:g_settings.load());
    UpdateIdleCursor(window,textEditor);
    if(IsIconic(window)){RestoreClip();return;}
    if(g_refreshWindow.exchange(false) && !g_stop && Focused(window)){
        // The game's existing WM_SIZE path reinitializes presentation resources.
        // Restore the exact placement instead of changing any desktop settings.
        WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};RECT bounds{};
        if(GetWindowPlacement(window,&placement) && GetWindowRect(window,&bounds)){
            if(SetWindowPos(window,nullptr,bounds.left,bounds.top,640,480,SWP_NOZORDER|SWP_NOACTIVATE)){
                SetWindowPlacement(window,&placement);
                SetWindowPos(window,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
            }
        }
    }
    const auto observed=static_cast<std::uint32_t>(GetWindowLongPtrW(window,GWL_STYLE));
    const auto plan=PlanStyle(observed,config.borderless,g_style);
    g_conflict=plan.conflict;
    if(plan.write && !plan.conflict) {
        if(!g_style.owned) {g_originalPlacement.length=sizeof(g_originalPlacement);GetWindowPlacement(window,&g_originalPlacement);}
        SetLastError(0);
        const LONG_PTR previous=SetWindowLongPtrW(window,GWL_STYLE,static_cast<LONG_PTR>(plan.value));
        if((previous!=0 || GetLastError()==0) && static_cast<std::uint32_t>(GetWindowLongPtrW(window,GWL_STYLE))==plan.value) {
            if(config.borderless) {
                CommitStyle(observed,plan.value,g_style);
                MONITORINFO monitor{sizeof(MONITORINFO)};
                if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
                    SetWindowPos(window,nullptr,monitor.rcMonitor.left,monitor.rcMonitor.top,
                        monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,
                        SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
                    GetWindowRect(window,&g_appliedRect);
                }
            } else {
                RECT current{};
                if(GetWindowRect(window,&current) && EqualRect(current,g_appliedRect)) SetWindowPlacement(window,&g_originalPlacement);
                SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
                g_style={};
            }
        } else g_conflict=true;
    }
    g_borderlessApplied=g_style.owned && !g_conflict;
    if(config.clipCursor && g_clipConflict){g_conflict=true;return;}
    if(config.clipCursor && Focused(window)) {
        RECT client{};POINT origin{};
        if(GetClientRect(window,&client) && ClientToScreen(window,&origin)) {
            OffsetRect(&client,origin.x,origin.y);
            RECT observedClip{};
            if(GetClipCursor(&observedClip)) {
                if(!g_clipOwned){
                    g_priorClip=observedClip;
                    RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};
                    desktop.right=desktop.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);desktop.bottom=desktop.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);
                    g_priorClipUnbounded=EqualRect(desktop,g_priorClip);
                }
                if(!g_clipOwned || EqualRect(observedClip,g_appliedClip)) {
                    if(ClipCursor(&client)){g_appliedClip=client;g_clipOwned=true;g_cursorClipped=true;}
                } else {g_clipOwned=false;g_cursorClipped=false;g_clipConflict=true;g_conflict=true;}
            }
        }
    } else {RestoreClip();if(!config.clipCursor)g_clipConflict=false;}
}

LRESULT CALLBACK Keyboard(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION && !g_stop && data &&
        (message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP)) {
        const auto* key=reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if(BlockWindowsKey(Unpack(g_settings),Focused(g_window),key->vkCode))return 1;
    }
    return CallNextHookEx(nullptr,code,message,data);
}
DWORD WINAPI KeyboardThread(void*) {
    MSG message{};PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    g_keyboardThreadId=GetCurrentThreadId();
    HHOOK hook=nullptr;
    while(!g_stop) {
        const bool requested=(g_settings.load()&1u)!=0 && g_window.load()!=nullptr;
        if(requested && !hook) {
            HMODULE ownModule=nullptr;
            // A low-level callback can be in flight after unhook. Match the
            // existing native-menu process lifetime; never unload its code early.
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&Keyboard),&ownModule);
            hook=SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,ownModule,0);
            g_keyboardReady=hook!=nullptr;
        } else if(!requested && hook) {UnhookWindowsHookEx(hook);hook=nullptr;g_keyboardReady=false;}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,100,QS_ALLINPUT);
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT)g_stop=true;
            TranslateMessage(&message);DispatchMessageW(&message);
        }
    }
    if(hook)UnhookWindowsHookEx(hook);
    g_keyboardReady=false;g_keyboardThreadId=0;return 0;
}
bool Supported(std::uintptr_t base) {
    __try {
        if(!base)return false;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>0x1000)return false;
        const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
        return pe->Signature==IMAGE_NT_SIGNATURE && F8Runtime::IsSupportedExecutable({pe->FileHeader.Machine,
            pe->OptionalHeader.Magic,pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage});
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool CameraProfile(uintptr_t base) {
    __try {
        const auto* camera=reinterpret_cast<const unsigned char*>(base+0x00390C78u);
        const auto* freeze=reinterpret_cast<const unsigned char*>(base+0x0042114Eu);
        return camera[0]==0x0F && camera[1]==0xBE && camera[2]==0x05 &&
            *reinterpret_cast<const uint32_t*>(camera+3)==base+kFreeCameraRva &&
            freeze[0]==0x80 && freeze[1]==0x3D && freeze[6]==0 &&
            *reinterpret_cast<const uint32_t*>(freeze+2)==base+kFreezeSceneRva;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
struct ByteAccess {uintptr_t address=0;unsigned char observed=0;bool read=false;};
bool ReadByte(void* raw,uintptr_t address,uint8_t* value) {
    auto& access=*static_cast<ByteAccess*>(raw);
    __try {*value=*reinterpret_cast<const volatile uint8_t*>(address);access={address,*value,true};return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {access.read=false;return false;}
}
F8Runtime::WriteResult WriteByte(void* raw,uintptr_t address,uint8_t value) {
    auto& access=*static_cast<ByteAccess*>(raw);
    if(!access.read || access.address!=address)return {F8Runtime::WriteEffect::NotTouched};
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory)) ||
        memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
        !(memory.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
        return {F8Runtime::WriteEffect::NotTouched};
    __try {
        const auto before=static_cast<uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile CHAR*>(address),
            static_cast<CHAR>(value),static_cast<CHAR>(access.observed)));
        return {before==access.observed?F8Runtime::WriteEffect::Verified:F8Runtime::WriteEffect::NotTouched};
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {F8Runtime::WriteEffect::MayHaveChanged};}
}
void ApplyCamera(bool allowGameplay) {
    if(!g_cameraProfile)return;
    AcquireSRWLockExclusive(&g_cameraLock);
    CameraScope scope{};
    __try {
        scope.focused=!g_stop && Focused(g_window);
        scope.menu=!allowGameplay || *reinterpret_cast<const uint32_t*>(g_base+RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG)!=0;
        scope.battle=*reinterpret_cast<const uint8_t*>(g_base+RVA_FFX_BATTLE_ACTIVE_FLAG)!=0;
        const auto player=*reinterpret_cast<const uintptr_t*>(g_base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR);
        scope.fieldReady=player>=0x10000u && player<0x7FFF0000u;
    } __except(EXCEPTION_EXECUTE_HANDLER) {scope={};}
    const bool cameraRequested=!g_stop && Config::GetBool("camera.free_look",false);
    const bool freezeRequested=!g_stop && Config::GetBool("camera.freeze_scene",false);
    const bool foreign=GetModuleHandleW(L"unx.dll")!=nullptr;
    const bool wants[]={cameraRequested && FreeCameraAllowed(scope) && !foreign,
        freezeRequested && FreezeSceneAllowed(scope) && !foreign};
    F8Runtime::OwnedByte* owners[]={&g_cameraOwner,&g_freezeOwner};
    const uint32_t rvas[]={kFreeCameraRva,kFreezeSceneRva};
    const char* keys[]={"camera.free_look","camera.freeze_scene"};
    for(unsigned i=0;i<2;++i) {
        ByteAccess access{};F8Runtime::ByteIo io{&access,ReadByte,WriteByte};
        uint8_t observed=0;
        if(owners[i]->state==F8Runtime::OwnershipState::Unowned &&
            (!ReadByte(&access,g_base+rvas[i],&observed) || observed>1)) {
            PublishF8RuntimeStatus(keys[i],F8RuntimeAvailability::Conflict,true,false);
            continue;
        }
        F8Runtime::UpdateOwnedByte(io,g_base+rvas[i],1,wants[i],owners[i]);
        const bool conflict=owners[i]->state==F8Runtime::OwnershipState::Conflict || foreign;
        const bool pending=owners[i]->state==F8Runtime::OwnershipState::RestorePending;
        PublishF8RuntimeStatus(keys[i],conflict?F8RuntimeAvailability::Conflict:
            (pending?F8RuntimeAvailability::RestorePending:F8RuntimeAvailability::Available),true,
            wants[i] && owners[i]->state==F8Runtime::OwnershipState::Owned);
    }
    ReleaseSRWLockExclusive(&g_cameraLock);
}
void Publish(const char* key,bool requested,bool applied,bool ready,bool conflict=false) {
    PublishF8RuntimeStatus(key,!g_supported?F8RuntimeAvailability::UnsupportedBuild:
        (conflict?F8RuntimeAvailability::Conflict:(ready?F8RuntimeAvailability::Available:F8RuntimeAvailability::ProducerUnavailable)),
        true,requested && applied);
}
void LoadBindings(unsigned existingMenuKey) {
    auto table=NativeBindings::Defaults();
    if(existingMenuKey>0 && existingMenuKey<=255)table[0]={existingMenuKey,0};
    for(unsigned i=0;i<table.size();++i) {
        const auto action=static_cast<NativeBindings::Action>(i);
        NativeBindings::Binding value{};
        if(NativeBindings::Decode(Config::GetInt(NativeBindings::Key(action),NativeBindings::Encode(table[i])),&value) &&
            NativeBindings::Validate(table,action,value)==NativeBindings::BindResult::Ok)table[i]=value;
    }
    AcquireSRWLockExclusive(&g_bindingLock);g_bindings=table;ReleaseSRWLockExclusive(&g_bindingLock);
}
} // namespace

bool Start(std::uintptr_t base,LogFn log,unsigned existingMenuKey) {
    if(!g_stop)return g_supported;
    g_log=log;g_supported=Supported(base);g_base=base;
    if(!g_supported){Log("[ffx-hooks] NativePorts: unsupported executable profile");return false;}
    g_wine=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")!=nullptr;
    g_cameraProfile=CameraProfile(base);
    if(!g_cameraProfile) {
        PublishF8RuntimeStatus("camera.free_look",F8RuntimeAvailability::SignatureMismatch,true,false);
        PublishF8RuntimeStatus("camera.freeze_scene",F8RuntimeAvailability::SignatureMismatch,true,false);
    }
    LoadBindings(existingMenuKey);g_stop=false;g_lastTick=0;
    NativeGamepad::Start(base);
    g_keyboardThread=CreateThread(nullptr,0,KeyboardThread,nullptr,0,nullptr);
    if(!g_keyboardThread)Log("[ffx-hooks] NativePorts: keyboard worker unavailable; other window controls remain independent");
    Log("[ffx-hooks] NativePorts: native input/window consumers ready; new options default OFF");
    return true;
}
void RequestStop() {
    g_settings=0;g_stop=true;g_captureAction=-1;g_captureValue=-1;g_padCaptureAction=-1;g_padActionEdges=0;
    NativeGamepad::RequestStop();
}
void Stop() {
    RequestStop();
    ApplyCamera(false);
    NativeGamepad::Stop();
    const DWORD thread=g_keyboardThreadId;
    if(thread)PostThreadMessageW(thread,WM_QUIT,0,0);
    const HWND window=g_window;
    if(window && IsWindow(window)) {
        DWORD_PTR result=0;
        SendMessageTimeoutW(window,kUpdateMessage,kUpdateToken,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,200,&result);
    }
    if(g_keyboardThread) {
        if(WaitForSingleObject(g_keyboardThread,500)==WAIT_OBJECT_0){CloseHandle(g_keyboardThread);g_keyboardThread=nullptr;}
        else Log("[ffx-hooks] NativePorts: keyboard drain pending; keep module resident");
    }
}
void BindWindow(HWND window) {
    if(!g_supported || g_stop || !window || g_window==window)return;
    // Native window replacement is accepted only after the old HWND is gone.
    const HWND prior=g_window;
    if(prior && IsWindow(prior))return;
    g_window=window;g_style={};g_clipOwned=false;g_clipConflict=false;g_conflict=false;g_idleCursor={};g_cursorHidden=false;g_savedCursor=nullptr;
}
void Tick(bool allowActions) {
    if(g_stop || !g_supported)return;
    ApplyCamera(allowActions);
    const auto pad=NativeGamepad::Poll();unsigned padEdges=0;
    for(unsigned i=0;i<g_padEdges.size();++i) {
        const auto action=static_cast<NativeBindings::Action>(i);
        const auto binding=GamepadBinding(action);
        if(NativeGamepad::Pressed(g_padEdges[i],pad.buttons,binding,
            pad.connected && Focused(g_window) && !BindingCaptureActive())){
            padEdges|=1u<<i;
            if(i<2)g_menuOpeningPad=binding;
        }
    }
    g_padActionEdges=padEdges;
    static bool wasDown[static_cast<unsigned>(NativeBindings::Action::Count)]{};
    for(unsigned i=2;i<static_cast<unsigned>(NativeBindings::Action::Count);++i) {
        const bool down=BindingDown(static_cast<NativeBindings::Action>(i));
        if(down && !wasDown[i] && allowActions && Focused(g_window)) {
            const char* key=nullptr;
            unsigned nativeKey=0;
            switch(static_cast<NativeBindings::Action>(i)) {
            case NativeBindings::Action::Performance:key="diagnostics.performance";break;
            case NativeBindings::Action::Borderless:key="window.borderless";break;
            case NativeBindings::Action::FreeLook:key="camera.free_look";break;
            case NativeBindings::Action::TimeStop:key="camera.freeze_scene";break;
            case NativeBindings::Action::Sensor:key="boosters.permanent_sensor";break;
            case NativeBindings::Action::PartyAP:key="boosters.entire_party_earns_ap";break;
            case NativeBindings::Action::NativeTurbo:nativeKey=VK_F1;break;
            case NativeBindings::Action::Supercharge:nativeKey=VK_F2;break;
            case NativeBindings::Action::EncounterRate:nativeKey=VK_F3;break;
            case NativeBindings::Action::AutoBattle:nativeKey=VK_F4;break;
            case NativeBindings::Action::HideHud:nativeKey=VK_F5;break;
            case NativeBindings::Action::GameMenu:nativeKey=VK_ESCAPE;break;
            case NativeBindings::Action::RefreshWindow:g_refreshWindow=true;break;
            default:break; // Menu and SpeedHack owners consume their own samples.
            }
            if(key)if(const auto* flag=FindF8Flag(key))SetF8FlagValue(*flag,!ResolveF8Flag(*flag).value);
            if(nativeKey && (GetAsyncKeyState(static_cast<int>(nativeKey))&0x8000)==0 && Focused(g_window)){
                // Native F1-F5/Esc behavior remains owned by FFX. This is only an
                // optional shortcut alias, not a second cheat implementation.
                INPUT input[2]{};input[0].type=INPUT_KEYBOARD;input[0].ki.wScan=static_cast<WORD>(MapVirtualKeyW(nativeKey,MAPVK_VK_TO_VSC));
                input[0].ki.dwFlags=KEYEVENTF_SCANCODE;input[1]=input[0];input[1].ki.dwFlags|=KEYEVENTF_KEYUP;
                SendInput(2,input,sizeof(INPUT));
            }
        }
        wasDown[i]=down;
    }
    const DWORD now=GetTickCount();if(g_lastTick && now-g_lastTick<100)return;g_lastTick=now;
    NativeGamepad::RefreshMapping();
    Settings value{Config::GetBool("input.block_windows_key",false),Config::GetBool("input.fix_background_input",false),
        Config::GetBool("input.filter_ime",false),Config::GetBool("window.borderless",false),
        Config::GetBool("window.clip_cursor",false),Config::GetBool("window.hide_cursor",false),
        Config::GetBool("diagnostics.performance",false)};
    g_settings=(value.blockWindows?1u:0u)|(value.backgroundInput?2u:0u)|(value.filterIme?4u:0u)|
        (value.borderless?8u:0u)|(value.clipCursor?16u:0u)|(value.hideCursor?32u:0u)|(value.performance?64u:0u);
    const HWND window=g_window;const bool ready=window && IsWindow(window);
    if(ready)PostMessageW(window,kUpdateMessage,kUpdateToken,0);
    if(g_wine && value.blockWindows)
        PublishF8RuntimeStatus("input.block_windows_key",F8RuntimeAvailability::PlatformLimited,true,false);
    else Publish("input.block_windows_key",value.blockWindows,g_keyboardReady,ready && (!value.blockWindows || g_keyboardReady));
    Publish("input.fix_background_input",value.backgroundInput,ready,ready);
    Publish("input.filter_ime",value.filterIme,ready,ready);
    Publish("window.borderless",value.borderless,g_borderlessApplied,ready,g_conflict);
    Publish("window.clip_cursor",value.clipCursor,g_cursorClipped,ready,g_conflict);
    Publish("window.hide_cursor",value.hideCursor,ready,ready);
    Publish("diagnostics.performance",value.performance,ready,ready);
}
bool HandleWindowMessage(HWND window,UINT message,WPARAM wParam,LPARAM lParam,bool textEditor,LRESULT* result) {
    if(!g_supported || window!=g_window || !result)return false;
    if(message==kUpdateMessage && wParam==kUpdateToken){ApplyWindow(window,textEditor);*result=0;return true;}
    if(message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !wParam)){RestoreClip();RestoreIdleCursor();}
    if(message==WM_DESTROY){RestoreClip();RestoreIdleCursor();g_window=nullptr;}
    if(message==WM_MOUSEMOVE || message==WM_SETCURSOR)UpdateIdleCursor(window,textEditor);
    const auto config=Unpack(g_stop?0:g_settings.load());
    if(message==WM_SETCURSOR && LOWORD(lParam)==HTCLIENT && config.hideCursor &&
        !textEditor && Focused(window) && g_cursorHidden) {
        SetCursor(nullptr);*result=TRUE;return true;
    }
    const auto action=FilterMessage(config,Focused(window),textEditor,message);
    if(action==MessageAction::Forward)return false;
    *result=action==MessageAction::DefaultProcess?DefWindowProcW(window,message,wParam,lParam):0;
    return true;
}
RuntimeStatus Status() {return {g_supported,g_window.load()!=nullptr,g_keyboardReady,g_wine,g_borderlessApplied,g_cursorClipped,g_conflict};}
#ifdef FFXHOOKS_TESTING
void SetForegroundReaderForTests(HWND(*reader)()) {g_foregroundReaderForTests=reader;}
void SetPointerReaderForTests(bool(*reader)(POINT*)) {g_pointerReaderForTests=reader;}
#endif
Settings CurrentSettings() {return Unpack(g_settings);}
NativeBindings::Table Bindings() {
    AcquireSRWLockShared(&g_bindingLock);const auto result=g_bindings;ReleaseSRWLockShared(&g_bindingLock);return result;
}
NativeBindings::BindResult SaveBinding(NativeBindings::Action action,NativeBindings::Binding value) {
    auto table=Bindings();const auto valid=NativeBindings::Validate(table,action,value);
    if(valid!=NativeBindings::BindResult::Ok)return valid;
    if(!Config::SetInt(NativeBindings::Key(action),NativeBindings::Encode(value)))return NativeBindings::BindResult::Invalid;
    table[static_cast<unsigned>(action)]=value;
    AcquireSRWLockExclusive(&g_bindingLock);g_bindings=table;ReleaseSRWLockExclusive(&g_bindingLock);
    return NativeBindings::BindResult::Ok;
}
ShortcutSample SampleShortcut(NativeBindings::Action action) {
    if(BindingCaptureActive())return {};
    const auto table=Bindings();const auto index=static_cast<unsigned>(action);
    if(index>=table.size())return {};
    const bool pad=(g_padActionEdges.load()&(1u<<index))!=0;
    const unsigned held=g_releaseBeforeShortcut;
    if(held && (GetAsyncKeyState(static_cast<int>(held))&0x8000)==0)g_releaseBeforeShortcut=0;
    if(held && held==table[index].key)return {pad,false};
    const unsigned mods=((GetAsyncKeyState(VK_CONTROL)&0x8000)?NativeBindings::Control:0u)|
        ((GetAsyncKeyState(VK_SHIFT)&0x8000)?NativeBindings::Shift:0u)|((GetAsyncKeyState(VK_MENU)&0x8000)?NativeBindings::Alt:0u);
    const bool key=table[index].key && (GetAsyncKeyState(static_cast<int>(table[index].key))&0x8000)!=0;
    if(action==NativeBindings::Action::MenuF8 && (GetAsyncKeyState(VK_INSERT)&0x8000)!=0)
        return {true,mods!=0 && !pad && !(key && mods==table[index].modifiers)};
    return {key||pad,key && mods!=table[index].modifiers && !pad};
}
bool BindingDown(NativeBindings::Action action){const auto sample=SampleShortcut(action);return sample.down && !sample.mismatch;}
const char* BindingText(NativeBindings::Action action) {
    static thread_local char label[40];const auto table=Bindings();const auto index=static_cast<unsigned>(action);
    if(index>=table.size())return "Unassigned";
    NativeBindings::Format(table[index],label,sizeof(label));return label;
}
bool BeginBindingCapture(NativeBindings::Action action) {
    if(action>=NativeBindings::Action::Count)return false;
    g_padCaptureAction=-1;
    g_captureValue=-1;g_captureAction=static_cast<int>(action);return true;
}
bool BindingCaptureActive() {return g_captureAction.load()>=0 || g_padCaptureAction.load()>=0;}
void CancelBindingCapture() {g_captureAction=-1;g_captureValue=-1;g_padCaptureAction=-1;g_padCaptureControl=-1;}
bool BindingCaptureMessage(UINT message,WPARAM key) {
    if(!BindingCaptureActive())return false;
    if(message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !key)){CancelBindingCapture();return false;}
    if(g_padCaptureAction.load()>=0){
        if(message==WM_KEYDOWN || message==WM_SYSKEYDOWN){
            if(key==VK_ESCAPE)g_padCaptureControl=-2;
            if(key==VK_BACK || key==VK_DELETE)g_padCaptureControl=0;
            return true;
        }
        return message==WM_KEYUP || message==WM_SYSKEYUP;
    }
    if(message!=WM_KEYDOWN && message!=WM_SYSKEYDOWN && message!=WM_KEYUP && message!=WM_SYSKEYUP)return false;
    if(message==WM_KEYUP || message==WM_SYSKEYUP)return true;
    if(key==VK_SHIFT || key==VK_CONTROL || key==VK_MENU || (key>=VK_LSHIFT && key<=VK_RMENU))return true;
    const unsigned modifiers=((GetAsyncKeyState(VK_CONTROL)&0x8000)?NativeBindings::Control:0u)|
        ((GetAsyncKeyState(VK_SHIFT)&0x8000)?NativeBindings::Shift:0u)|((GetAsyncKeyState(VK_MENU)&0x8000)?NativeBindings::Alt:0u);
    const int value=key==VK_ESCAPE?-2:((key==VK_BACK || key==VK_DELETE)?0:
        NativeBindings::Encode({static_cast<unsigned>(key),modifiers}));
    int pending=-1;
    if(g_captureValue.compare_exchange_strong(pending,value))g_releaseBeforeShortcut=static_cast<unsigned>(key);
    return true;
}
bool ConsumeBindingCapture(NativeBindings::BindResult* result,bool* cancelled) {
    const int action=g_captureAction,value=g_captureValue;
    if(action<0 || value==-1 || !result || !cancelled)return false;
    CancelBindingCapture();*cancelled=value==-2;
    *result=NativeBindings::BindResult::Ok;
    if(!*cancelled) {
        NativeBindings::Binding binding{};
        *result=NativeBindings::Decode(value,&binding)?SaveBinding(static_cast<NativeBindings::Action>(action),binding):NativeBindings::BindResult::Invalid;
    }
    return true;
}

static bool GamepadKey(NativeBindings::Action action,char* key,std::size_t capacity){
    const char* source=NativeBindings::Key(action);
    if(!source)return false;
    const int n=std::snprintf(key,capacity,"gamepad.%s",source+9);
    return n>=0 && static_cast<std::size_t>(n)<capacity;
}
std::uint32_t GamepadBinding(NativeBindings::Action action){
    char key[64]{};if(!GamepadKey(action,key,sizeof(key)))return 0;
    const int value=Config::GetInt(key,0);
    return value>=0 && NativeGamepad::ValidCombo(static_cast<std::uint32_t>(value))?static_cast<std::uint32_t>(value):0;
}
const char* GamepadBindingText(NativeBindings::Action action){
    static thread_local char text[128];NativeGamepad::Format(GamepadBinding(action),text,sizeof(text));return text;
}
bool SaveGamepadBinding(NativeBindings::Action action,std::uint32_t value){
    if(action>=NativeBindings::Action::Count || !NativeGamepad::ValidCombo(value))return false;
    for(unsigned i=0;value && i<static_cast<unsigned>(NativeBindings::Action::Count);++i)
        if(i!=static_cast<unsigned>(action) && GamepadBinding(static_cast<NativeBindings::Action>(i))==value)return false;
    char key[64]{};
    if(!GamepadKey(action,key,sizeof(key)) || !Config::SetInt(key,static_cast<int>(value)))return false;
    NativeGamepad::RefreshMapping();return true;
}
bool BeginGamepadCapture(NativeBindings::Action action){
    if(action>=NativeBindings::Action::Count)return false;
    const auto pad=NativeGamepad::Poll();if(!pad.connected)return false;
    CancelBindingCapture();AcquireSRWLockExclusive(&g_padCaptureLock);
    g_padCapture.Begin(pad.buttons);g_padCaptureAction=static_cast<int>(action);
    ReleaseSRWLockExclusive(&g_padCaptureLock);return true;
}
bool GamepadCaptureActive(){return g_padCaptureAction.load()>=0;}
bool MenuOpeningPadHeld(){
    const auto mask=g_menuOpeningPad.load();if(!mask)return false;
    const auto pad=NativeGamepad::Poll();
    if(pad.connected && Focused(g_window) && (pad.buttons&mask)!=0)return true;
    g_menuOpeningPad=0;return false;
}
bool ConsumeGamepadCapture(bool* saved,bool* cancelled){
    const int action=g_padCaptureAction.load();
    if(action<0 || !saved || !cancelled)return false;
    const int control=g_padCaptureControl.exchange(-1);
    const auto pad=NativeGamepad::Poll();
    AcquireSRWLockExclusive(&g_padCaptureLock);
    auto result=g_padCapture.Sample(pad.buttons);
    if(control!=-1 || !pad.connected)g_padCapture.Cancel();
    ReleaseSRWLockExclusive(&g_padCaptureLock);
    if(control==-1 && pad.connected && !result.done)return false;
    *cancelled=control==-2 || !pad.connected;*saved=false;
    g_padCaptureAction=-1;
    if(!*cancelled && (control==0 || result.valid))*saved=SaveGamepadBinding(static_cast<NativeBindings::Action>(action),control==0?0:result.mask);
    return true;
}
} // namespace FfxHooks::NativePorts
