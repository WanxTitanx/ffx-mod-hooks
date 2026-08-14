#include "InGameMenuDashboard.h"
#include "../shared/Config.h"
#include "../shared/ffx_addresses.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* Log() is static in dllmain.cpp; dashboard logs go through OutputDebugStringA. */
static void DashLog(const char* fmt, ...) {
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(line);
}

namespace {

static const int kMaxItems = 64;
static const int kLabelCap = 128;
static const int kStatusCap = 256;
static const int kTabCap = 20;

struct DashItem {
    const char* key;
    const char* label;
    bool* runtimePtr;
    bool defaultValue;
};

struct DashTab {
    const char* name;
    const char* keyPrefix;
    DashItem items[kMaxItems];
    int itemCount;
};

static DashTab g_tabs[8] = {};
static int g_tabCount = 0;
static int g_selTab = 0;
static int g_selItem = 0;
static int g_scroll = 0;
static bool g_open = false;
static char g_status[kStatusCap] = "F8 show/hide; \x18\x19 tabs; \x1B\x1A items; ENTER toggle";
static DWORD g_lastToggleTick = 0;
static HWND g_hwnd = nullptr;
static bool g_initialized = false;
static bool g_runtimeBooleans[256] = {};
static int g_runtimeIdx = 0;

/* Game pause: set g_FFX_MenuSubsystemActive to freeze field logic */
static bool g_gamePaused = false;

static void SetGamePaused(bool pause) {
    if (pause == g_gamePaused) return;
    g_gamePaused = pause;
    static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    volatile DWORD* flag = reinterpret_cast<volatile DWORD*>(
        base + RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG);
    *flag = pause ? 1 : 0;
    if (pause) SetCapture(g_hwnd);
    else ReleaseCapture();
}

static bool* RegisterRuntimeBool(bool initialValue) {
    if (g_runtimeIdx < 256) {
        g_runtimeBooleans[g_runtimeIdx] = initialValue;
        return &g_runtimeBooleans[g_runtimeIdx++];
    }
    return nullptr;
}

static DashItem MakeItem(const char* key, const char* label, bool defaultVal) {
    DashItem item;
    item.key = key;
    item.label = label;
    item.runtimePtr = RegisterRuntimeBool(defaultVal);
    item.defaultValue = defaultVal;
    return item;
}

static DashTab MakeTab(const char* name, const char* keyPrefix) {
    DashTab tab;
    tab.name = name;
    tab.keyPrefix = keyPrefix;
    tab.itemCount = 0;
    return tab;
}

static void TabAdd(DashTab* tab, const char* subKey, const char* label, bool defaultVal) {
    if (tab->itemCount >= kMaxItems) return;
    tab->items[tab->itemCount++] = MakeItem(subKey, label, defaultVal);
}

static void BuildTabs() {
    g_tabCount = 0;

    { DashTab tab = MakeTab("Plugins", "plugins");
        TabAdd(&tab, "plugins.dinput8",    "dinput8.dll (hook proxy)",          true);
        TabAdd(&tab, "plugins.dxgi",       "dxgi.dll (Special K)",              false);
        TabAdd(&tab, "plugins.unx",        "unx.dll (UnX loader)",             false);
        TabAdd(&tab, "plugins.ffx_probe",  "ffx-probe.dll (probe tool)",        false);
        g_tabs[g_tabCount++] = tab; }

    { DashTab tab = MakeTab("Boosters", "boosters");
        TabAdd(&tab, "boosters.entire_party_earns_ap", "Entire Party Earns AP", false);
        TabAdd(&tab, "boosters.permanent_sensor",       "Permanent Sensor",      false);
        TabAdd(&tab, "boosters.playable_seymour",      "Playable Seymour",       false);
        TabAdd(&tab, "boosters.speed_hack",             "Speed Hack (F2 hold)",  false);
        g_tabs[g_tabCount++] = tab; }

    { DashTab tab = MakeTab("Cheats", "cheats");
        TabAdd(&tab, "cheats.always_overdrive", "Always Overdrive", false);
        TabAdd(&tab, "cheats.always_critical",  "Always Critical",  false);
        TabAdd(&tab, "cheats.damage_value",     "Damage 99999",     false);
        TabAdd(&tab, "cheats.always_rare_drop", "Always Rare Drop", false);
        TabAdd(&tab, "cheats.ap_100x",           "100x AP",          false);
        TabAdd(&tab, "cheats.gil_100x",          "100x Gil",         false);
        g_tabs[g_tabCount++] = tab; }

    { DashTab tab = MakeTab("Field", "field_scout");
        TabAdd(&tab, "field_scout.master",    "FieldScout Master",  false);
        TabAdd(&tab, "field_scout.heavy",     "Heavy Mode",         false);
        TabAdd(&tab, "field_scout.max",       "Max Mode",           false);
        TabAdd(&tab, "field_scout.ultra",     "Ultra Mode",         false);
        TabAdd(&tab, "field_scout.materials", "Ultra: Materials",   false);
        TabAdd(&tab, "field_scout.sound",     "Ultra: Sound",       false);
        TabAdd(&tab, "field_scout.encounters","Ultra: Encounters",  false);
        g_tabs[g_tabCount++] = tab; }

    { DashTab tab = MakeTab("Arena+", "arena_plus");
        TabAdd(&tab, "arena_plus.master",       "Arena+ Master",          false);
        TabAdd(&tab, "arena_plus.compose_f7",   "Compose F7 (boss rush)", false);
        TabAdd(&tab, "arena_plus.victory_hook", "Victory Hook",           false);
        TabAdd(&tab, "arena_plus.resolver_log", "Resolver Log",           false);
        TabAdd(&tab, "arena_plus.music",         "Arena+ Music",           false);
        g_tabs[g_tabCount++] = tab; }

    { DashTab tab = MakeTab("Input", "input");
        TabAdd(&tab, "input.block_windows_key",   "Block Windows Key",    true);
        TabAdd(&tab, "input.fix_background_input","Fix Background Input", true);
        TabAdd(&tab, "input.filter_ime",          "Filter IME (crashes)", true);
        TabAdd(&tab, "input.dialog_skip",         "Dialog Skip (voices)", false);
        g_tabs[g_tabCount++] = tab; }
}

static int DashItemCount() {
    if (g_selTab < 0 || g_selTab >= g_tabCount) return 0;
    return g_tabs[g_selTab].itemCount;
}

static void ClampSelection() {
    if (g_selTab < 0) g_selTab = 0;
    if (g_selTab >= g_tabCount) g_selTab = g_tabCount - 1;
    if (g_selItem < 0) g_selItem = 0;
    int ic = DashItemCount();
    if (g_selItem >= ic) g_selItem = ic - 1;
    if (g_scroll > g_selItem) g_scroll = g_selItem;
    if (g_scroll < g_selItem - 10) g_scroll = g_selItem - 10;
    if (g_scroll < 0) g_scroll = 0;
}

static void DashDraw() {
    if (!g_open) return;
    HDC hdc = GetDC(g_hwnd);
    if (!hdc) return;
    RECT rect; GetClientRect(g_hwnd, &rect);
    int w = rect.right, h = rect.bottom;

    HBRUSH bg = CreateSolidBrush(RGB(16, 16, 28));
    FillRect(hdc, &rect, bg); DeleteObject(bg);

    HFONT font = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, "Consolas");
    SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    int y = 8, lh = 18;

    SetTextColor(hdc, RGB(100, 180, 255));
    TextOutA(hdc, 10, y, "FFX-Hooks Dashboard", 19);
    TextOutA(hdc, w - 130, y, " F8 close", 9);
    y += lh + 3;

    for (int i = 0; i < g_tabCount; i++) {
        char lb[32] = {};
        _snprintf_s(lb, sizeof(lb), _TRUNCATE,
            i == g_selTab ? " [%s] " : "  %s  ", g_tabs[i].name);
        SetTextColor(hdc, i == g_selTab ? RGB(255, 255, 100) : RGB(120, 120, 140));
        TextOutA(hdc, 8 + i * 105, y, lb, (int)strlen(lb));
    }
    y += lh + 4;

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 80));
    SelectObject(hdc, pen);
    MoveToEx(hdc, 8, y, 0); LineTo(hdc, w - 8, y);
    DeleteObject(pen);
    y += 4;

    int ic = DashItemCount();
    for (int i = g_scroll; i < ic && i < g_scroll + 12; i++) {
        bool sel = (i == g_selItem);
        auto& item = g_tabs[g_selTab].items[i];
        bool val = item.runtimePtr ? *item.runtimePtr : item.defaultValue;
        char buf[128] = {};
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s %s  %s",
            sel ? ">" : " ", val ? "[X]" : "[ ]", item.label);
        SetTextColor(hdc, sel ? RGB(255,255,255) : RGB(200,200,200));
        TextOutA(hdc, 24, y, buf, (int)strlen(buf));
        y += lh;
    }

    pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 80));
    SelectObject(hdc, pen);
    MoveToEx(hdc, 8, h - 24, 0); LineTo(hdc, w - 8, h - 24);
    DeleteObject(pen);
    SetTextColor(hdc, RGB(120, 200, 120));
    TextOutA(hdc, 8, h - 22, g_status, (int)strlen(g_status));

    DeleteObject(font);
    ReleaseDC(g_hwnd, hdc);
}

/* Edge detection */
static bool g_ek[256] = {};
static DWORD g_kt[256] = {};

static bool EKey(int vk) {
    bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool e = d && !g_ek[vk];
    g_ek[vk] = d;
    if (!e) return false;
    DWORD n = GetTickCount();
    if (n - g_kt[vk] < 120) return false;
    g_kt[vk] = n;
    return true;
}

static void TItem() {
    int idx = g_selItem;
    if (idx < 0 || idx >= DashItemCount()) return;
    auto& item = g_tabs[g_selTab].items[idx];
    bool nv = !(item.runtimePtr ? *item.runtimePtr : item.defaultValue);
    if (item.runtimePtr) *item.runtimePtr = nv;
    if (item.key) FfxHooks::Config::SetBool(item.key, nv);
    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "%s: %s",
        item.label, nv ? "On" : "Off");
}

static void CALLBACK TProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_open) { ClampSelection(); DashDraw(); }
}

static WNDPROC g_owp = nullptr;
static LRESULT CALLBACK DWP(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) {
        int vk = (int)w;
        if (vk == VK_F8 || vk == VK_INSERT) {
            if (EKey(vk)) {
                g_open = !g_open;
                if (g_open) {
                    for (int t = 0; t < g_tabCount; t++)
                        for (int i = 0; i < g_tabs[t].itemCount; i++) {
                            auto& item = g_tabs[t].items[i];
                            if (item.runtimePtr && item.key)
                                *item.runtimePtr = FfxHooks::Config::GetBool(item.key, item.defaultValue);
                        }
                }
                _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, g_open ? "Open" : "Closed");
                SetGamePaused(g_open);
                InvalidateRect(h, 0, TRUE); return 0;
            }
        }
        if (g_open) {
            if (EKey(VK_ESCAPE) || EKey(VK_BACK)) { g_open = 0; SetGamePaused(false); InvalidateRect(h,0,1); return 0; }
            if (EKey(VK_LEFT) || EKey('A')) { g_selTab=(g_selTab-1+g_tabCount)%g_tabCount; g_selItem=0; g_scroll=0; InvalidateRect(h,0,1); return 0; }
            if (EKey(VK_RIGHT) || EKey('D')) { g_selTab=(g_selTab+1)%g_tabCount; g_selItem=0; g_scroll=0; InvalidateRect(h,0,1); return 0; }
            if (EKey(VK_UP) || EKey('W')) { g_selItem--; ClampSelection(); InvalidateRect(h,0,1); return 0; }
            if (EKey(VK_DOWN) || EKey('S')) { g_selItem++; ClampSelection(); InvalidateRect(h,0,1); return 0; }
            if (EKey(VK_HOME)) { g_selItem=0; g_scroll=0; InvalidateRect(h,0,1); return 0; }
            if (EKey(VK_END)) { g_selItem=DashItemCount()-1; ClampSelection(); InvalidateRect(h,0,1); return 0; }
            if (EKey(VK_RETURN) || EKey(VK_SPACE) || EKey('E') || EKey('Z')) { TItem(); InvalidateRect(h,0,1); return 0; }
            /* Block ALL keyboard input to game when dashboard is open — like F7 pause */
            return 0;
        }
    }
    /* Also block mouse when dashboard is open */
    if (g_open && (m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN || m == WM_LBUTTONUP ||
                   m == WM_RBUTTONDOWN || m == WM_RBUTTONUP || m == WM_MBUTTONDOWN ||
                   m == WM_MBUTTONUP || m == WM_MOUSEWHEEL || m == WM_INPUT)) {
        return 0;
    }
    return CallWindowProc(g_owp, h, m, w, l);
}

static HWND FindGameWindow() {
    /* Try multiple approaches — the game window might have various titles */
    HWND found = nullptr;

    /* 1. Try by class name FFXGAME (most reliable for FFX) */
    found = FindWindowA("FFXGAME", nullptr);
    if (found) return found;

    /* 2. Try by title containing FINAL FANTASY */
    found = FindWindowA(nullptr, "FINAL FANTASY X");
    if (found) return found;
    found = FindWindowA(nullptr, "FINAL FANTASY X/X-2 HD Remaster");
    if (found) return found;

    /* 3. EnumWindows fallback */
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        char title[128] = {};
        if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
            if (strstr(title, "FINAL FANTASY") != nullptr) {
                *reinterpret_cast<HWND*>(lp) = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

} // ns

bool FfxHooks::StartInGameMenuDashboard() {
    if (g_initialized) return true;
    BuildTabs();
    DashLog("[ffx-hooks] Dashboard: searching for game window...\n");
    g_hwnd = FindGameWindow();
    if (!g_hwnd) {
        DashLog("[ffx-hooks] Dashboard: EnumWindows failed, trying class FFXGAME\n");
        g_hwnd = FindWindowA("FFXGAME", nullptr);
    }
    if (!g_hwnd) {
        DashLog("[ffx-hooks] Dashboard: FFXGAME class not found, trying all visible windows\n");
        /* Last resort: find any visible window in the process */
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            if (IsWindowVisible(hwnd)) {
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == GetCurrentProcessId()) {
                    *reinterpret_cast<HWND*>(lp) = hwnd;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&g_hwnd));
    }
    if (!g_hwnd) {
        DashLog("[ffx-hooks] Dashboard: FAILED to find any window\n");
        return false;
    }
    char buf[128];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[ffx-hooks] Dashboard: found hwnd=%p\n", g_hwnd);
    OutputDebugStringA(buf);
    SetLastError(0);
    g_owp = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)DWP);
    if (!g_owp && GetLastError() != 0) {
        DashLog("[ffx-hooks] Dashboard: SetWindowLongPtr FAILED\n");
        return false;
    }
    SetTimer(g_hwnd, 0xDEAD, 50, TProc);
    g_initialized = true;
    DashLog("[ffx-hooks] Dashboard: started successfully\n");
    return true;
}
