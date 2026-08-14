#include "GridTeachHook.h"
#include "../shared/ffx_addresses.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include "KimahriLancetDualGrantHook.h"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <vector>
#include <intrin.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#endif

namespace FfxHooks {

namespace {

using GrantCommandFn = int(__cdecl*)(int charIdx, int cmdId, int on);
using ApplyLearnedMoveFn = void(__cdecl*)(int charIdx, int learnedMove);
using PrepareSaveCmdStateFn = int(__cdecl*)();
using BuildMenuFn = int(__cdecl*)(int charIdx, int actorRecord);
using IsCmdAvailFn = int(__cdecl*)(uint8_t charIdx, uint16_t cmdId);
using GetCmdEntryFn = uint8_t*(__cdecl*)(int16_t cmdIndex, int a2);

static bool              g_installed = false;
static bool              g_menuPatched = false;
static GridTeachLogFn    g_logFn = nullptr;
static uintptr_t         g_base = 0;
static uintptr_t         g_menuPatchVa = 0;
static const int         kMaxMenuPatchSites = 8;
static uintptr_t         g_menuPatchSites[kMaxMenuPatchSites] = {};
static uint8_t           g_menuPatchSaved[kMaxMenuPatchSites][4] = {};
static int               g_menuPatchCount = 0;

static uint16_t          g_gridLearnedBank[FFX_GRID_TEACH_SIDECAR_WORDS] = {};
static char              g_sidecarPath[MAX_PATH] = {};

#ifdef FFXHOOKS_HAVE_POLYHOOK
static PLH::x86Detour*   g_grantDetour = nullptr;
static uint64_t          g_grantTrampoline = 0;
static PLH::x86Detour*   g_applyLearnedDetour = nullptr;
static uint64_t          g_applyLearnedTrampoline = 0;
static PLH::x86Detour*   g_prepDetour = nullptr;
static uint64_t          g_prepTrampoline = 0;
static PLH::x86Detour*   g_buildDetour = nullptr;
static uint64_t          g_buildTrampoline = 0;
static PLH::x86Detour*   g_isCmdDetour = nullptr;
static uint64_t          g_isCmdTrampoline = 0;
static GetCmdEntryFn     g_getCmdEntry = nullptr;
static volatile LONG     g_gridGrantCount = 0;
static volatile LONG     g_applyLearnedCount = 0;
static volatile LONG     g_prepReapplyCount = 0;
static volatile LONG     g_prepSkipFieldCount = 0;
static volatile LONG     g_extMenuScrubCount = 0;
static volatile LONG     g_extMenuInjectCount = 0;
#endif

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static bool MemWrite(void* dest, const void* src, size_t len) {
    if (!dest || !src || len == 0) return false;
    DWORD old = 0;
    if (!VirtualProtect(dest, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dest, src, len);
    VirtualProtect(dest, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dest, len);
    return true;
}

static inline void PartyBankCoord(uint32_t encodedId, int& wordIdx, uint16_t& bitMask) {
    const uint32_t rel = (encodedId - 96u) & 0xFFFu;
    wordIdx = static_cast<int>(rel / 16u);
    bitMask = static_cast<uint16_t>(1u << (rel & 0xFu));
}

static inline volatile uint16_t* PartyBankWord(int wordIdx) {
    if (!g_base || wordIdx < 0 || wordIdx >= FFX_PARTY_WIDE_COMMAND_BANK_WORDS) return nullptr;
    return reinterpret_cast<volatile uint16_t*>(
        g_base + RVA_FFX_PARTY_WIDE_COMMAND_BANK + static_cast<uintptr_t>(wordIdx) * 2u);
}

static inline volatile uint16_t* KimahriRonsoUnlockWord() {
    if (!g_base) return nullptr;
    return reinterpret_cast<volatile uint16_t*>(g_base + RVA_FFX_KIMAHRI_RONSO_UNLOCK);
}

static int NormalizeCommandId(int cmdId) {
    if (cmdId == 0)
        return 0;
    if ((cmdId & 0xFFFFF000) == 0x3000)
        return cmdId;
    const int raw = cmdId & 0xFFF;
    /* LearnedMove=0 means stat node (IncreaseAmount path) — never OR 0x3000. */
    if (raw >= 1 && raw <= 0x3FF)
        return 0x3000 | raw;
    return cmdId;
}

static void ResolveSidecarPath() {
    if (g_sidecarPath[0]) return;
    HMODULE self = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&ResolveSidecarPath),
        &self);
    if (!self) {
        strcpy_s(g_sidecarPath, "grid_teach_learned.bin");
        return;
    }
    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(self, modulePath, MAX_PATH);
    char* slash = strrchr(modulePath, '\\');
    if (slash) *(slash + 1) = '\0';
    else modulePath[0] = '\0';
    snprintf(g_sidecarPath, MAX_PATH, "%sconfig\\grid_teach_learned.bin", modulePath);
}

static void LoadSidecar() {
    ResolveSidecarPath();
    memset(g_gridLearnedBank, 0, sizeof(g_gridLearnedBank));
    HANDLE h = CreateFileA(g_sidecarPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        HookLog("[ffx-hooks] GridTeach sidecar missing (ok on first run): %s", g_sidecarPath);
        return;
    }
    DWORD read = 0;
    ReadFile(h, g_gridLearnedBank, sizeof(g_gridLearnedBank), &read, nullptr);
    CloseHandle(h);
    HookLog("[ffx-hooks] GridTeach sidecar load %s (%lu bytes)", g_sidecarPath, static_cast<unsigned long>(read));
}

static void SaveSidecar() {
    ResolveSidecarPath();
    char dir[MAX_PATH] = {};
    strcpy_s(dir, g_sidecarPath);
    char* slash = strrchr(dir, '\\');
    if (slash) {
        *slash = '\0';
        CreateDirectoryA(dir, nullptr);
    }
    HANDLE h = CreateFileA(g_sidecarPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        HookLog("[ffx-hooks] GridTeach WARN sidecar save failed: %s", g_sidecarPath);
        return;
    }
    DWORD written = 0;
    WriteFile(h, g_gridLearnedBank, sizeof(g_gridLearnedBank), &written, nullptr);
    CloseHandle(h);
}

static void MarkGridLearnedPartyWide(uint32_t encodedId) {
    int w = 0; uint16_t m = 0;
    PartyBankCoord(encodedId, w, m);
    if (w < 0 || w >= FFX_GRID_TEACH_SIDECAR_WORDS) return;
    g_gridLearnedBank[w] = static_cast<uint16_t>(g_gridLearnedBank[w] | m);
    SaveSidecar();
}

static bool IsShadowPartyBankWord(int wordIdx) {
    return wordIdx >= FFX_PARTY_WIDE_COMMAND_BANK_WORDS;
}

static void SetRonsoUnlockBit(int cmdId, int on) {
    if (cmdId < FFX_CMD_RONSO_RAGE_ID_MIN || cmdId > FFX_CMD_RONSO_RAGE_ID_MAX) return;
    const int bit = cmdId - FFX_CMD_RONSO_RAGE_ID_MIN;
    volatile uint16_t* p = KimahriRonsoUnlockWord();
    if (!p) return;
    __try {
        const uint16_t pre = *p;
        if (on)
            *p = static_cast<uint16_t>(*p | static_cast<uint16_t>(1u << bit));
        else
            *p = static_cast<uint16_t>(*p & ~static_cast<uint16_t>(1u << bit));
        if (on && *p != pre) {
            HookLog("[ffx-hooks] GridTeach RonsoUnlock bit%d set (cmd %d) word=0x%04X",
                bit, cmdId, static_cast<unsigned>(*p));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool IsShadowSidecarCommandLearned(int cmdId) {
    if (cmdId < 96) return false;
    int w = 0; uint16_t m = 0;
    PartyBankCoord(static_cast<uint32_t>(0x3000 | (cmdId & 0xFFF)), w, m);
    if (!IsShadowPartyBankWord(w)) return false;
    return (g_gridLearnedBank[w] & m) != 0;
}

// Runtime row = command.bin + 0x10 header; byte[25] = CharacterUser (0xFF = any).
static bool CharacterUserAllowsMenu(uint8_t charIdx, int cmdId) {
    if (!g_getCmdEntry) return true;
    uint8_t* entry = nullptr;
    __try {
        entry = g_getCmdEntry(static_cast<int16_t>(cmdId & 0xFFF), 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
    if (!entry) return true;
    const uint8_t user = entry[25];
    return user == 0xFF || user == charIdx;
}

static bool CommandIsLearned(uint8_t charIdx, int cmdId) {
    const uint16_t encoded = static_cast<uint16_t>(0x3000 | (cmdId & 0xFFF));
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_isCmdTrampoline) {
        const int avail = reinterpret_cast<IsCmdAvailFn>(g_isCmdTrampoline)(charIdx, encoded);
        if ((avail & 1) != 0)
            return true;
    }
#endif
    if (cmdId >= 96 && IsShadowSidecarCommandLearned(cmdId))
        return true;
    if (cmdId >= FFX_CMD_RONSO_RAGE_ID_MIN && cmdId <= FFX_CMD_RONSO_RAGE_ID_MAX && charIdx == FFX_CHARACTER_KIMAHRI) {
        volatile uint16_t* ronso = KimahriRonsoUnlockWord();
        if (ronso) {
            const int bit = cmdId - FFX_CMD_RONSO_RAGE_ID_MIN;
            __try {
                const uint16_t word = *ronso;
                if ((word >> bit) & 1u)
                    return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    return false;
}

static bool RingSlotEmpty(uint16_t code) {
    return code == 0 || code == 0xFF || code == 0xFFFF;
}

static bool RingSlotMatchesId(uint16_t code, int cmdId) {
    return !RingSlotEmpty(code) && static_cast<int>(code & 0xFFF) == cmdId;
}

static bool ShouldScrubRingCommand(uint8_t charIdx, int cmdId) {
    if (cmdId <= 0)
        return false;
    if (cmdId >= FFX_CMD_RONSO_RAGE_ID_MIN && cmdId <= FFX_CMD_RONSO_RAGE_ID_MAX)
        return charIdx != FFX_CHARACTER_KIMAHRI;
    if (cmdId == static_cast<int>(FFX_CMD_BLUE_MAGIC_MENU_ID)
        || (cmdId >= static_cast<int>(FFX_CMD_BLUE_MAGIC_CHILD_MIN)
            && cmdId <= static_cast<int>(FFX_CMD_BLUE_MAGIC_CHILD_MAX)))
        return charIdx != FFX_CHARACTER_KIMAHRI;
    if (cmdId == static_cast<int>(FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID)
        || (cmdId >= static_cast<int>(FFX_CMD_YUNA_WM_PLUS_CHILD_MIN)
            && cmdId <= static_cast<int>(FFX_CMD_YUNA_WM_PLUS_CHILD_MAX)))
        return charIdx != FFX_CHARACTER_YUNA;
    if (cmdId >= 96)
        return !CharacterUserAllowsMenu(charIdx, cmdId);
    return false;
}

/* Kimahri ext pack: BuildMenu may land #322/#323–336 in wrong rings before inject.
 * Scrub duplicates so opener stays main (+0) and children stay special (+232), not OD (+296). */
static bool ShouldScrubKimahriMisplacedRing(uint8_t charIdx, int segmentOff, int cmdId) {
    if (charIdx != FFX_CHARACTER_KIMAHRI)
        return false;
    if (cmdId == static_cast<int>(FFX_CMD_BLUE_MAGIC_MENU_ID))
        return segmentOff != 0;
    if (cmdId >= static_cast<int>(FFX_CMD_BLUE_MAGIC_CHILD_MIN)
        && cmdId <= static_cast<int>(FFX_CMD_BLUE_MAGIC_CHILD_MAX))
        return segmentOff != static_cast<int>(FFX_BATTLE_COMMAND_RING_SPECIAL_OFFSET);
    if (cmdId >= FFX_CMD_RONSO_RAGE_ID_MIN && cmdId <= FFX_CMD_RONSO_RAGE_ID_MAX)
        return segmentOff != static_cast<int>(FFX_BATTLE_COMMAND_RING_OD_OFFSET);
    return false;
}

static void ScrubRingBuffer(uint16_t* buf, int count, uint8_t charIdx, int segmentOff, int* scrubbedOut) {
    if (!buf || count <= 0) return;
    int scrubbed = 0;
    for (int i = 0; i < count; ++i) {
        const uint16_t code = buf[i];
        if (RingSlotEmpty(code))
            continue;
        const int id = static_cast<int>(code & 0xFFF);
        if (ShouldScrubRingCommand(charIdx, id)
            || ShouldScrubKimahriMisplacedRing(charIdx, segmentOff, id)) {
            buf[i] = 0xFF;
            ++scrubbed;
        }
    }
    if (scrubbedOut)
        *scrubbedOut += scrubbed;
}

static void AppendRingCommand(uint16_t* buf, int count, uint16_t encoded, int* injectedOut) {
    if (!buf || count <= 0) return;
    const int id = static_cast<int>(encoded & 0xFFF);
    for (int i = 0; i < count; ++i) {
        if (RingSlotMatchesId(buf[i], id))
            return;
    }
    for (int i = 0; i < count; ++i) {
        if (RingSlotEmpty(buf[i])) {
            buf[i] = encoded;
            if (injectedOut)
                ++(*injectedOut);
            return;
        }
    }
}

static void InjectExtendedSpecialMenu(uint8_t charIdx, uintptr_t tree, int* injectedOut) {
    uint16_t* mainRing = reinterpret_cast<uint16_t*>(tree);
    uint16_t* case4Ring = reinterpret_cast<uint16_t*>(tree + FFX_BATTLE_COMMAND_RING_OD_OFFSET);
    uint16_t* specialRing = reinterpret_cast<uint16_t*>(tree + FFX_BATTLE_COMMAND_RING_SPECIAL_OFFSET);

    if (charIdx == FFX_CHARACTER_KIMAHRI) {
        if (CommandIsLearned(charIdx, FFX_CMD_BLUE_MAGIC_MENU_ID))
            AppendRingCommand(mainRing, FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT,
                FFX_CMD_BLUE_MAGIC_MENU_ENCODED, injectedOut);
        for (int id = static_cast<int>(FFX_CMD_BLUE_MAGIC_CHILD_MIN);
             id <= static_cast<int>(FFX_CMD_BLUE_MAGIC_CHILD_MAX); ++id) {
            if (CommandIsLearned(charIdx, id))
                AppendRingCommand(specialRing, FFX_BATTLE_COMMAND_RING_SPECIAL_SLOT_COUNT,
                    static_cast<uint16_t>(0x3000 | id), injectedOut);
        }
    } else if (charIdx == FFX_CHARACTER_YUNA) {
        if (CommandIsLearned(charIdx, FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID))
            AppendRingCommand(mainRing, FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT,
                FFX_CMD_WHITE_MAGIC_PLUS_MENU_ENCODED, injectedOut);
        for (int id = static_cast<int>(FFX_CMD_YUNA_WM_PLUS_CHILD_MIN);
             id <= static_cast<int>(FFX_CMD_YUNA_WM_PLUS_CHILD_MAX); ++id) {
            if (CommandIsLearned(charIdx, id))
                AppendRingCommand(case4Ring, FFX_BATTLE_COMMAND_RING_OD_SLOT_COUNT,
                    static_cast<uint16_t>(0x3000 | id), injectedOut);
        }
    }
}

static void ScrubAndInjectExtendedSpecialMenu(int charIdx) {
    if (!g_base || charIdx < 0 || charIdx > 6)
        return;

    uintptr_t ringBase = 0;
    __try {
        ringBase = *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (ringBase <= 0x10000u)
        return;

    const uintptr_t tree =
        ringBase + static_cast<uintptr_t>(FFX_BATTLE_COMMAND_RING_SLOT_STRIDE) * static_cast<uintptr_t>(charIdx);

    static const struct { int off; int cnt; } kSegments[] = {
        { 0, static_cast<int>(FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT) },
        { 40, 8 }, { 56, 8 }, { 72, 24 }, { 120, 24 }, { 168, 32 }, { 232, 32 },
        { static_cast<int>(FFX_BATTLE_COMMAND_RING_OD_OFFSET),
          static_cast<int>(FFX_BATTLE_COMMAND_RING_OD_SLOT_COUNT) },
    };
    const int kSegmentCount = static_cast<int>(sizeof(kSegments) / sizeof(kSegments[0]));

    int scrubbed = 0;
    int injected = 0;
    __try {
        for (int s = 0; s < kSegmentCount; ++s)
            ScrubRingBuffer(reinterpret_cast<uint16_t*>(tree + kSegments[s].off), kSegments[s].cnt,
                static_cast<uint8_t>(charIdx), kSegments[s].off, &scrubbed);
        InjectExtendedSpecialMenu(static_cast<uint8_t>(charIdx), tree, &injected);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (scrubbed > 0 || injected > 0) {
        const long n = InterlockedIncrement(&g_extMenuScrubCount);
        if (n <= 12) {
            HookLog("[ffx-hooks] GridTeach extMenu ch=%d scrub=%d inject=%d (#%ld)",
                charIdx, scrubbed, injected, n);
        }
        if (injected > 0)
            InterlockedExchangeAdd(&g_extMenuInjectCount, injected);
    }
}

static void ReapplyGridLearnedBankBits(const char* tag) {
    if (!g_base) return;
    int touched = 0;
    for (int w = 0; w < FFX_PARTY_WIDE_COMMAND_BANK_WORDS; ++w) {
        const uint16_t mask = g_gridLearnedBank[w];
        if (!mask) continue;
        volatile uint16_t* live = PartyBankWord(w);
        if (!live) continue;
        __try {
            const uint16_t pre = *live;
            *live = static_cast<uint16_t>(pre | mask);
            if (*live != pre) ++touched;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (tag && touched > 0) {
        const long n = InterlockedIncrement(&g_prepReapplyCount);
        if (n <= 8)
            HookLog("[ffx-hooks] GridTeach reapply %s touchedWords=%d", tag, touched);
    }
}

static void ApplyGridLearnSideEffects(int charIdx, int encodedCmdId) {
    const int id = encodedCmdId & 0xFFF;
    if (id >= FFX_CMD_RONSO_RAGE_ID_MIN && id <= FFX_CMD_RONSO_RAGE_ID_MAX)
        SetRonsoUnlockBit(id, 1);
    if (id >= 96) {
        MarkGridLearnedPartyWide(static_cast<uint32_t>(encodedCmdId));
        ReapplyGridLearnedBankBits(nullptr);
    }
    const long n = InterlockedIncrement(&g_applyLearnedCount);
    if (n <= 48 || (n % 10) == 0) {
        HookLog("[ffx-hooks] GridTeach ApplyLearned #%ld ch=%d cmd=0x%04X id=%d",
            n, charIdx, static_cast<unsigned>(encodedCmdId & 0xFFFF), id);
    }
}

static bool IsCallerSphereGridGrant(void* retAddr) {
    if (!g_base || !retAddr) return false;
    const uintptr_t r = reinterpret_cast<uintptr_t>(retAddr);
    const uintptr_t lo = g_base + RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_LO;
    const uintptr_t hi = g_base + RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_HI;
    return r >= lo && r < hi;
}

static bool IsCallerBattleKernelInit(void* retAddr) {
    if (!g_base || !retAddr) return false;
    const uintptr_t r = reinterpret_cast<uintptr_t>(retAddr);
    const uintptr_t lo = g_base + RVA_FFX_BTL_KERNEL_INIT_LO;
    const uintptr_t hi = g_base + RVA_FFX_BTL_KERNEL_INIT_HI;
    return r >= lo && r < hi;
}

static bool IsSphereGridOnStack() {
    if (!g_base) return false;
#ifdef _M_IX86
    uintptr_t frame = 0;
    __asm mov frame, ebp
    for (int i = 0; i < 32 && frame > 0x10000; ++i) {
        __try {
            const uintptr_t ret = *reinterpret_cast<uintptr_t*>(frame + 4);
            if (ret >= g_base + RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_LO
                && ret < g_base + RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_HI)
                return true;
            frame = *reinterpret_cast<uintptr_t*>(frame);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
    }
#endif
    return false;
}

static bool TryPatchMenuBound(uintptr_t base) {
    const uintptr_t fn = base + RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU;
    const size_t scanLen = 0x500;
    std::vector<uint8_t> bytes(scanLen);
    memcpy(bytes.data(), reinterpret_cast<const void*>(fn), scanLen);

    const uint32_t extended = FFX_BATTLE_MENU_COMMAND_ID_LIMIT_EXTENDED;

    g_menuPatchCount = 0;
    for (size_t i = 0; i + 6 <= scanLen && g_menuPatchCount < kMaxMenuPatchSites; ++i) {
        uintptr_t immVa = 0;
        if (bytes[i] == 0x3D && bytes[i + 1] == 0x40 && bytes[i + 2] == 0x01 && bytes[i + 3] == 0x00 && bytes[i + 4] == 0x00)
            immVa = fn + i + 1;
        else if (bytes[i] == 0x81 && (bytes[i + 1] & 0xF8) == 0xF8
            && bytes[i + 2] == 0x40 && bytes[i + 3] == 0x01 && bytes[i + 4] == 0x00 && bytes[i + 5] == 0x00)
            immVa = fn + i + 2;
        if (immVa) {
            const int idx = g_menuPatchCount;
            memcpy(g_menuPatchSaved[idx], reinterpret_cast<const void*>(immVa), 4);
            if (MemWrite(reinterpret_cast<void*>(immVa), &extended, sizeof(uint32_t))) {
                g_menuPatchSites[idx] = immVa;
                if (idx == 0) g_menuPatchVa = immVa;
                ++g_menuPatchCount;
                HookLog("[ffx-hooks] GridTeach menu bound site#%d patched @0x%08X 0x140->0x%X",
                    idx, static_cast<unsigned>(immVa), static_cast<unsigned>(extended));
            }
        }
    }
    return g_menuPatchCount > 0;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static void __cdecl ApplyLearnedMove_GridTeach_Shim(int charIdx, int learnedMove) {
    if (learnedMove == 0) {
        reinterpret_cast<ApplyLearnedMoveFn>(g_applyLearnedTrampoline)(charIdx, 0);
        return;
    }

    const int pre = learnedMove;
    const int normalized = NormalizeCommandId(learnedMove);
    const int id = normalized & 0xFFF;
    if (normalized != pre) {
        HookLog("[ffx-hooks] GridTeach FIX encoding 0x%04X -> 0x%04X (ch=%d)",
            static_cast<unsigned>(pre & 0xFFFF),
            static_cast<unsigned>(normalized & 0xFFFF),
            charIdx);
    }

    // sub_A54860 replays panel LearnedMove rows when entering/leaving the Sphere Grid UI.
    // The vanilla helper is fine for 0..255-era commands, but high grown ids (ex: 321)
    // are outside older byte-sized consumers seen on this path. Record the teach side
    // effects ourselves and skip the original replay to avoid corrupting UI/runtime state.
    if ((normalized & 0xFFFFF000) == 0x3000 && id > 255) {
        const long n = InterlockedIncrement(&g_applyLearnedCount);
        if (n <= 48 || (n % 10) == 0) {
            HookLog("[ffx-hooks] GridTeach HIGH-ID replay bypass #%ld ch=%d cmd=0x%04X id=%d",
                n, charIdx, static_cast<unsigned>(normalized & 0xFFFF), id);
        }
        ApplyGridLearnSideEffects(charIdx, normalized);
        return;
    }

    reinterpret_cast<ApplyLearnedMoveFn>(g_applyLearnedTrampoline)(charIdx, normalized);
    if ((normalized & 0xFFFFF000) == 0x3000 && id != 0
        && (id >= 96 || (id >= FFX_CMD_RONSO_RAGE_ID_MIN && id <= FFX_CMD_RONSO_RAGE_ID_MAX)))
        ApplyGridLearnSideEffects(charIdx, normalized);
}

static int __cdecl GrantCommandToCharacter_GridTeach_Shim(int charIdx, int cmdId, int on) {
    if (on && cmdId == 0)
        return reinterpret_cast<GrantCommandFn>(g_grantTrampoline)(charIdx, 0, on);

    const int normalized = on ? NormalizeCommandId(cmdId) : cmdId;
    const bool fromGrid = on && (IsCallerSphereGridGrant(_ReturnAddress()) || IsSphereGridOnStack());

    bool shadowOnly = false;
    if (on && (normalized & 0xFFFFF000) == 0x3000) {
        const int id = normalized & 0xFFF;
        if (id >= 96) {
            int w = 0; uint16_t m = 0;
            PartyBankCoord(static_cast<uint32_t>(normalized), w, m);
            (void)m;
            shadowOnly = IsShadowPartyBankWord(w);
        }
    }

    int rv = 0;
    if (!shadowOnly) {
        rv = reinterpret_cast<GrantCommandFn>(g_grantTrampoline)(charIdx, normalized, on);
    } else if (on) {
        rv = 1;
        const long n = InterlockedIncrement(&g_gridGrantCount);
        if (n <= 8) {
            HookLog("[ffx-hooks] GridTeach shadow grant ch=%d id=%d (skipped live bank word>=16)",
                charIdx, normalized & 0xFFF);
        }
    }

    if (!on || (normalized & 0xFFFFF000) != 0x3000)
        return rv;

    const int id = normalized & 0xFFF;
    if (on && rv && id >= 96) {
        if (fromGrid)
            ApplyGridLearnSideEffects(charIdx, normalized);
        else if (charIdx == static_cast<int>(FFX_CHARACTER_KIMAHRI)
            && (id == static_cast<int>(FFX_CMD_BLUE_MAGIC_MENU_ID)
                || (id >= static_cast<int>(FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST)
                    && id <= static_cast<int>(FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST)
                        + (FFX_CMD_RONSO_RAGE_ID_MAX - FFX_CMD_RONSO_RAGE_ID_MIN)))) {
            ApplyGridLearnSideEffects(charIdx, normalized);
        }
    }

    if (id >= FFX_CMD_RONSO_RAGE_ID_MIN && id <= FFX_CMD_RONSO_RAGE_ID_MAX) {
        if (fromGrid)
            SetRonsoUnlockBit(id, 1);
#ifdef FFXHOOKS_HAVE_POLYHOOK
        if (on && rv && !fromGrid && IsKimahriLancetDualGrantHookInstalled()) {
            KimahriLancetDualGrantOnRonsoLearn(
                charIdx,
                id,
                rv,
                _ReturnAddress(),
                &GrantCommandToCharacter_GridTeach_Shim);
        }
#endif
    }

    return rv;
}

static int __cdecl PrepareSaveCommandState_GridTeach_Shim() {
    // Same RVA serves battle init AND field Status/Equip. Only battle may reload
    // party_data + ply_save template; field path wipes derived stats (520 HP bug).
    if (!IsCallerBattleKernelInit(_ReturnAddress())) {
        const long n = InterlockedIncrement(&g_prepSkipFieldCount);
        if (n <= 4)
            HookLog("[ffx-hooks] GridTeach skip PrepareSave field path (Status/Equip stat guard)");
        return 0;
    }
    const int rv = reinterpret_cast<PrepareSaveCmdStateFn>(g_prepTrampoline)();
    ReapplyGridLearnedBankBits("PrepSave");
    return rv;
}

static int __cdecl BuildActorCommandMenu_GridTeach_Shim(int charIdx, int actorRecord) {
    ReapplyGridLearnedBankBits("BuildMenu.pre");
    const int rv = reinterpret_cast<BuildMenuFn>(g_buildTrampoline)(charIdx, actorRecord);
    ScrubAndInjectExtendedSpecialMenu(charIdx);
    return rv;
}

static int __cdecl HasCommandBit_GridTeach_Shim(uint8_t charIdx, uint16_t cmdId) {
    const int id = cmdId & 0xFFF;
    if (!CharacterUserAllowsMenu(charIdx, id))
        return 0;
    const int vanilla = reinterpret_cast<IsCmdAvailFn>(g_isCmdTrampoline)(charIdx, cmdId);
    if ((vanilla & 1) != 0)
        return vanilla;
    if (id >= 96 && IsShadowSidecarCommandLearned(id))
        return 1;
    return vanilla;
}
#endif

} // namespace

GridTeachInstallResult InstallGridTeachHook(uintptr_t base, GridTeachLogFn log) {
    GridTeachInstallResult result = { false, 0, false };
    if (g_installed) {
        result.ok = true;
        result.menuBoundPatchVa = g_menuPatchVa;
        result.menuBoundPatched = g_menuPatched;
        return result;
    }

    g_logFn = log;
    g_base = base;
    g_getCmdEntry = reinterpret_cast<GetCmdEntryFn>(base + RVA_FFX_KERNEL_GET_COMMAND_ENTRY_BY_ID);
    LoadSidecar();
    ReapplyGridLearnedBankBits("install");

    g_menuPatched = TryPatchMenuBound(base);
    if (!g_menuPatched) {
        HookLog("[ffx-hooks] WARN GridTeach menu bound patch not found @0x%08X — grown ids may not surface",
            static_cast<unsigned>(base + RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU));
    } else {
        HookLog("[ffx-hooks] GridTeach menu bound patched sites=%d firstVa=0x%08X limit=%u",
            g_menuPatchCount,
            static_cast<unsigned>(g_menuPatchVa),
            static_cast<unsigned>(FFX_BATTLE_MENU_COMMAND_ID_LIMIT_EXTENDED));
    }

    HookLog("[ffx-hooks] GridTeach v4.5 install base=0x%08X menuSites=%d sidecarWords=%u path=%s (PrepSave battle-only; extMenu scrub+inject post-BuildMenu)",
        static_cast<unsigned>(base),
        g_menuPatchCount,
        static_cast<unsigned>(FFX_GRID_TEACH_SIDECAR_WORDS),
        g_sidecarPath);

#ifdef FFXHOOKS_HAVE_POLYHOOK
    // Do NOT detour sub_798850 / ApplyLearnedMove in the first-safe build.
    // That function is a Sphere Grid UI/stat replay path and proved crash-prone
    // before any high-id callback logged. The real node activation path calls
    // GrantCommandToCharacter; capture grid teaches there instead.

    try {
        g_grantDetour = new PLH::x86Detour(
            static_cast<uint64_t>(base + RVA_FFX_GRANT_COMMAND_TO_CHARACTER),
            reinterpret_cast<uint64_t>(&GrantCommandToCharacter_GridTeach_Shim),
            &g_grantTrampoline);
        if (!g_grantDetour->hook()) {
            delete g_grantDetour; g_grantDetour = nullptr; g_grantTrampoline = 0;
        } else {
            HookLog("[ffx-hooks] GridTeach GrantCommand detour ok (encoding fix + Ronso backup)");
        }
    } catch (...) {
        delete g_grantDetour; g_grantDetour = nullptr; g_grantTrampoline = 0;
    }

    try {
        g_prepDetour = new PLH::x86Detour(
            static_cast<uint64_t>(base + RVA_FFX_BTL_PREPARE_SAVE_COMMAND_STATE),
            reinterpret_cast<uint64_t>(&PrepareSaveCommandState_GridTeach_Shim),
            &g_prepTrampoline);
        if (!g_prepDetour->hook()) {
            delete g_prepDetour; g_prepDetour = nullptr; g_prepTrampoline = 0;
        } else {
            HookLog("[ffx-hooks] GridTeach PrepareSave detour ok (battle-only; skip field Status/Equip)");
        }
    } catch (...) { delete g_prepDetour; g_prepDetour = nullptr; g_prepTrampoline = 0; }

    try {
        g_buildDetour = new PLH::x86Detour(
            static_cast<uint64_t>(base + RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU),
            reinterpret_cast<uint64_t>(&BuildActorCommandMenu_GridTeach_Shim),
            &g_buildTrampoline);
        if (!g_buildDetour->hook()) {
            delete g_buildDetour; g_buildDetour = nullptr; g_buildTrampoline = 0;
        } else {
            HookLog("[ffx-hooks] GridTeach BuildActorCommandMenu detour ok (bank reassert pre-call; Kimahri/Yuna extMenu post-call)");
        }
    } catch (...) { delete g_buildDetour; g_buildDetour = nullptr; g_buildTrampoline = 0; }

    try {
        g_isCmdDetour = new PLH::x86Detour(
            static_cast<uint64_t>(base + RVA_FFX_BATTLE_HAS_COMMAND_BIT),
            reinterpret_cast<uint64_t>(&HasCommandBit_GridTeach_Shim),
            &g_isCmdTrampoline);
        if (!g_isCmdDetour->hook()) {
            delete g_isCmdDetour; g_isCmdDetour = nullptr; g_isCmdTrampoline = 0;
        } else {
            HookLog("[ffx-hooks] GridTeach HasCommandBit detour ok (shadow 352+ + CharacterUser gate; no actor+0x690 write)");
        }
    } catch (...) { delete g_isCmdDetour; g_isCmdDetour = nullptr; g_isCmdTrampoline = 0; }
#else
    HookLog("[ffx-hooks] WARN GridTeach needs PolyHook");
#endif

    g_installed = true;
    result.ok = true;
    result.menuBoundPatchVa = g_menuPatchVa;
    result.menuBoundPatched = g_menuPatched;
    return result;
}

bool RemoveGridTeachHook(GridTeachLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_applyLearnedDetour) { g_applyLearnedDetour->unHook(); delete g_applyLearnedDetour; g_applyLearnedDetour = nullptr; }
    g_applyLearnedTrampoline = 0;
    if (g_grantDetour) { g_grantDetour->unHook(); delete g_grantDetour; g_grantDetour = nullptr; }
    g_grantTrampoline = 0;
    if (g_prepDetour) { g_prepDetour->unHook(); delete g_prepDetour; g_prepDetour = nullptr; }
    g_prepTrampoline = 0;
    if (g_buildDetour) { g_buildDetour->unHook(); delete g_buildDetour; g_buildDetour = nullptr; }
    g_buildTrampoline = 0;
    if (g_isCmdDetour) { g_isCmdDetour->unHook(); delete g_isCmdDetour; g_isCmdDetour = nullptr; }
    g_isCmdTrampoline = 0;
    g_gridGrantCount = 0;
    g_applyLearnedCount = 0;
    g_prepReapplyCount = 0;
    g_prepSkipFieldCount = 0;
    g_extMenuScrubCount = 0;
    g_extMenuInjectCount = 0;
#endif
    for (int i = 0; i < g_menuPatchCount; ++i) {
        if (g_menuPatchSites[i])
            MemWrite(reinterpret_cast<void*>(g_menuPatchSites[i]), g_menuPatchSaved[i], 4);
    }
    g_menuPatched = false;
    g_menuPatchVa = 0;
    g_menuPatchCount = 0;
    g_installed = false;
    g_base = 0;
    g_sidecarPath[0] = '\0';
    memset(g_gridLearnedBank, 0, sizeof(g_gridLearnedBank));
    g_logFn = nullptr;
    if (log) log("[ffx-hooks] GridTeach removed ok");
    return true;
}

bool IsGridTeachHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
