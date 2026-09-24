#include "InGameMenuDashboard.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <atomic>

namespace {

FfxHooks::DashLogFn g_logFn = nullptr;
bool g_installed = false;
volatile LONG g_pendingF8 = 0;
FfxHooks::DashHotkeyState g_hotkeyState{};
DWORD g_lastEdge = 0;
std::atomic<FfxHooks::DashShortcutReader> g_shortcutReader{nullptr};

void DashLog(const char* format, ...) {
    if (!g_logFn || !format) return;
    char line[384] = {};
    va_list args;
    va_start(args, format);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    g_logFn(line);
}

} // namespace

namespace FfxHooks {
void Dash_SetShortcutReader(DashShortcutReader reader){g_shortcutReader=reader;}

bool Dash_Install(DashLogFn log) {
    if (g_installed) return true;
    g_logFn = log;
    InterlockedExchange(&g_pendingF8, 0);
    g_hotkeyState = {};
    g_lastEdge = 0;
    g_installed = true;
    DashLog("[dash] installed (200 ms F8 edge adapter)\n");
    return true;
}

void Dash_Uninstall() {
    g_installed = false;
    g_logFn = nullptr;
    InterlockedExchange(&g_pendingF8, 0);
    g_hotkeyState = {};
    g_lastEdge = 0;
}

void Dash_Tick(bool foreground) {
    if (!g_installed) return;
    const auto reader=g_shortcutReader.load();const auto sample=reader?reader():DashShortcutSample{};
    const bool down = reader?sample.down:(GetAsyncKeyState(VK_F8) & 0x8000) != 0 ||
                      (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    const bool modifiersDown = reader?sample.mismatch:
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool edge = DashConsumeFocusedHotkeyEdge(
        &g_hotkeyState, foreground, down, modifiersDown);
    if (!foreground) {
        InterlockedExchange(&g_pendingF8, 0);
        return;
    }
    if (!edge) return;

    const DWORD now = GetTickCount();
    if (now - g_lastEdge < 200) return;
    g_lastEdge = now;
    InterlockedExchange(&g_pendingF8, 1);
    DashLog("[dash] F8 edge -> request FLAGS submenu\n");
}

bool Dash_F8Pressed() {
    return InterlockedExchange(&g_pendingF8, 0) != 0;
}

} // namespace FfxHooks
