#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "NativePortsCore.h"
#include "NativeBindingsCore.h"
#include "NativeGamepadCore.h"

namespace FfxHooks::NativePorts {
using LogFn=void(*)(const char*);
struct RuntimeStatus {
    bool supported=false, windowReady=false, keyboardReady=false, wine=false;
    bool borderlessApplied=false, cursorClipped=false, conflict=false;
};
bool Start(std::uintptr_t base,LogFn log,unsigned existingMenuKey=0x76u);
void RequestStop();
void Stop();
void BindWindow(HWND window);
void Tick(bool allowActions=true);
bool HandleWindowMessage(HWND,UINT,WPARAM,LPARAM,bool textEditor,LRESULT* result);
RuntimeStatus Status();
Settings CurrentSettings();
NativeBindings::Table Bindings();
NativeBindings::BindResult SaveBinding(NativeBindings::Action,NativeBindings::Binding);
bool BindingDown(NativeBindings::Action);
struct ShortcutSample {bool down=false,mismatch=false;};
ShortcutSample SampleShortcut(NativeBindings::Action);
const char* BindingText(NativeBindings::Action);
std::uint32_t GamepadBinding(NativeBindings::Action);
const char* GamepadBindingText(NativeBindings::Action);
bool SaveGamepadBinding(NativeBindings::Action,std::uint32_t);
bool BeginGamepadCapture(NativeBindings::Action);
bool ConsumeGamepadCapture(bool* saved,bool* cancelled);
bool GamepadCaptureActive();
bool MenuOpeningPadHeld();
bool BeginBindingCapture(NativeBindings::Action);
bool BindingCaptureActive();
void CancelBindingCapture();
bool BindingCaptureMessage(UINT,WPARAM);
bool ConsumeBindingCapture(NativeBindings::BindResult* result,bool* cancelled);
#ifdef FFXHOOKS_TESTING
void SetForegroundReaderForTests(HWND(*reader)());
void SetPointerReaderForTests(bool(*reader)(POINT*));
#endif
} // namespace FfxHooks::NativePorts
