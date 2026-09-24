#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdio>
#include <string>
#include "../hooks/NativePortsHook.h"
#include "../hooks/F8FlagCatalog.h"
#include "../shared/Config.h"
namespace P=FfxHooks::NativePorts;
namespace C=FfxHooks::Config;
static int checks,failures,keys,raw;
static std::string persisted;
static HWND foreground=nullptr;static int pointerOffset=0;
static HWND Foreground(){return foreground;}
static bool Pointer(POINT* point){RECT client{};if(!GetClientRect(foreground,&client))return false;*point={client.right/2+pointerOffset,client.bottom/2};return ClientToScreen(foreground,point)!=FALSE;}
static void Check(bool ok,const char* name) {++checks;if(!ok){++failures;std::printf("FAIL: %s\n",name);}}
static bool Persist(void*,const char*,const char* text) {persisted=text;return true;}
static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    LRESULT result=0;
    if(P::HandleWindowMessage(window,message,wParam,lParam,false,&result))return result;
    if(message==WM_KEYDOWN)++keys;
    if(message==WM_INPUT)++raw;
    return DefWindowProcW(window,message,wParam,lParam);
}
static void Pump(unsigned duration=160) {
    const DWORD stop=GetTickCount()+duration;MSG message{};
    do {
        P::Tick();
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(5);
    } while(static_cast<LONG>(GetTickCount()-stop)<0);
}
int main() {
    C::ResetForTests();C::SetProvidersForTests({nullptr,nullptr,nullptr,Persist});
    Check(C::LoadTextForTests("[input]\nblock_windows_key=0\nfix_background_input=0\nfilter_ime=0\n", "native-ports-test.ini"),"private config fixture loads");
    std::array<unsigned char,4096> image{};
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+0x80);pe->Signature=IMAGE_NT_SIGNATURE;
    pe->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;pe->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    pe->FileHeader.TimeDateStamp=0x55D2F3CCu;pe->OptionalHeader.SizeOfImage=0x0237D000u;
    Check(!P::Start(0,nullptr),"invalid profile cannot start native adapters");
    Check(P::Start(reinterpret_cast<uintptr_t>(image.data()),nullptr),"supported profile admits isolated window adapter");
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JarvisNativePortsRt1";
    Check(RegisterClassW(&wc)!=0,"isolated window class registers");
    HWND window=CreateWindowW(wc.lpszClassName,L"Jarvis-HOOK native ports RT1",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        70,70,520,350,nullptr,nullptr,wc.hInstance,nullptr);
    Check(window!=nullptr,"isolated owned window created");
    if(!window)return 2;
    P::BindWindow(window);SetForegroundWindow(window);Pump();
    Check(P::Status().windowReady,"native window consumer acknowledges readiness");
    const LONG_PTR original=GetWindowLongPtrW(window,GWL_STYLE);RECT originalRect{};GetWindowRect(window,&originalRect);
    Check(C::SetBool("window.borderless",true),"request borderless through same config writer");Pump();
    Check(P::Status().borderlessApplied,"window thread applies requested borderless style");
    Check((GetWindowLongPtrW(window,GWL_STYLE)&WS_OVERLAPPEDWINDOW)==0,"native frame removed");
    Check(C::SetBool("window.borderless",false),"request style restoration");Pump();
    Check(GetWindowLongPtrW(window,GWL_STYLE)==original,"exact original window style restored");
    RECT restored{};GetWindowRect(window,&restored);
    Check(EqualRect(&originalRect,&restored)!=FALSE,"original window placement restored");
    Check(C::SetBool("input.filter_ime",true),"request IME filter");Pump();
    LRESULT result=99;
    Check(P::HandleWindowMessage(window,WM_IME_COMPOSITION,0,0,false,&result) && result==0,"IME consumer filters composition");
    Check(!P::HandleWindowMessage(window,WM_IME_COMPOSITION,0,0,true,&result),"own text editor is exempt from IME filter");
    Check(C::SetBool("input.block_windows_key",true),"request owned low-level Windows-key hook");Pump(350);
    Check(P::Status().keyboardReady,"actual keyboard hook registration acknowledged");
    Check(C::SetBool("input.block_windows_key",false),"request keyboard hook removal");Pump(350);
    Check(!P::Status().keyboardReady,"OFF removes owned keyboard hook");
    Check(C::SetBool("window.borderless",true),"request second borderless transition");Pump();
    const LONG_PTR owned=GetWindowLongPtrW(window,GWL_STYLE);
    SetWindowLongPtrW(window,GWL_STYLE,owned|WS_BORDER);
    Check(C::SetBool("window.borderless",false),"request OFF after foreign edit");Pump();
    Check(GetWindowLongPtrW(window,GWL_STYLE)==(owned|WS_BORDER) && P::Status().conflict,"foreign window style survives restoration attempt");
    SetWindowLongPtrW(window,GWL_STYLE,owned);Pump();
    Check(GetWindowLongPtrW(window,GWL_STYLE)==original,"restoration succeeds after owner state returns");
    using namespace FfxHooks::NativeBindings;
    Check(P::SaveBinding(Action::Performance,{'P',Control})==BindResult::Ok,"user shortcut persists");
    Check(!persisted.empty() && C::GetInt("bindings.performance",-1)==Encode({'P',Control}),
        "binding uses existing atomic INI writer and publishes its saved value");
    Check(P::SaveBinding(Action::Borderless,{'P',Control})==BindResult::Duplicate,"duplicate binding never overwrites another action");
    Check(P::SaveBinding(Action::Performance,{'K',Control|Shift})==BindResult::Protected,"existing SpeedHack binding cannot be stolen");
    foreground=window;P::SetForegroundReaderForTests(Foreground);P::SetPointerReaderForTests(Pointer);
    HCURSOR arrow=LoadCursorA(nullptr,IDC_ARROW);SetCursor(arrow);
    Check(C::SetBool("window.hide_cursor",true),"request idle cursor hiding");Pump(2300);
    Check(GetCursor()==nullptr,"periodic window-thread updates hide the real cursor without a mouse message");
    ++pointerOffset;LRESULT ignored=0;P::HandleWindowMessage(window,WM_MOUSEMOVE,0,0,false,&ignored);
    Check(GetCursor()==arrow,"actual position change immediately restores the owned cursor");
    Pump(2200);HCURSOR foreignCursor=LoadCursorA(nullptr,IDC_CROSS);SetCursor(foreignCursor);
    C::SetBool("window.hide_cursor",false);Pump();
    Check(GetCursor()==foreignCursor,"OFF preserves a foreign cursor shape");
    SetCursor(arrow);P::SetPointerReaderForTests(nullptr);P::SetForegroundReaderForTests(nullptr);
    BindResult captureResult=BindResult::Invalid;bool cancelled=false;
    Check(P::BeginBindingCapture(Action::MenuF7),"user-requested menu shortcut editing is admitted");
    P::CancelBindingCapture();
    Check(P::BeginBindingCapture(Action::Borderless) && P::BindingCaptureActive(),"new action enters binding capture");
    Check(P::BindingCaptureMessage(WM_KEYDOWN,VK_SHIFT) &&
        !P::ConsumeBindingCapture(&captureResult,&cancelled),"a modifier alone keeps capture pending");
    Check(P::BindingCaptureMessage(WM_KEYDOWN,'B') && P::ConsumeBindingCapture(&captureResult,&cancelled) &&
        !cancelled && captureResult==BindResult::Ok && !P::BindingCaptureActive(),"captured ordinary key saves and exits capture");
    const auto beforeCancel=P::Bindings()[static_cast<unsigned>(Action::Borderless)];
    Check(P::BeginBindingCapture(Action::Borderless) && P::BindingCaptureMessage(WM_KEYDOWN,VK_ESCAPE) &&
        P::ConsumeBindingCapture(&captureResult,&cancelled) && cancelled &&
        P::Bindings()[static_cast<unsigned>(Action::Borderless)].key==beforeCancel.key,"Escape cancels without changing the saved shortcut");
    Check(P::BeginBindingCapture(Action::Borderless) && P::BindingCaptureMessage(WM_KEYDOWN,VK_DELETE) &&
        P::ConsumeBindingCapture(&captureResult,&cancelled) && !cancelled && captureResult==BindResult::Ok &&
        P::Bindings()[static_cast<unsigned>(Action::Borderless)].key==0,"Delete removes an existing shortcut");
    Check(P::BeginBindingCapture(Action::Borderless) && !P::BindingCaptureMessage(WM_KILLFOCUS,0) &&
        !P::BindingCaptureActive(),"focus loss cancels capture while forwarding its native notification");
    Check(P::BeginBindingCapture(Action::Borderless),"capture can reopen after focus cancellation");
    P::Stop();Pump(20);
    Check(!P::Status().keyboardReady && !P::CurrentSettings().blockWindows && !P::BindingCaptureActive(),"normal-context stop leaves input and capture inactive");
    DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);C::ResetForTests();
    std::printf("NativePortsWindowsRt1: %d/%d checks passed; failures=%d\n",checks-failures,checks,failures);
    return failures?1:0;
}
