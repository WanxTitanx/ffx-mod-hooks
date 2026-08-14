#include "MusicHook.h"
#include "../shared/ffx_addresses.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#include <stdarg.h>
#include <stdio.h>
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

static PLH::x86Detour* g_detourPlay    = nullptr;
static PLH::x86Detour* g_detourSwitch  = nullptr;
static PLH::x86Detour* g_detourPrep    = nullptr;
static PLH::x86Detour* g_detourPreload = nullptr;
static uint64_t        g_trampolinePlay   = 0;
static uint64_t        g_trampolineSwitch = 0;
static uint64_t        g_trampolinePrep   = 0;
static uint64_t        g_trampolinePreload = 0;
static FFXHooksBlock*  g_block      = nullptr;
static MusicHookLogFn  g_log        = nullptr;
static MusicHookTarget g_target     = MusicHookTarget::PlayTrack;
static uintptr_t       g_base       = 0;
static bool            g_traceStack = false;
static bool            g_hookedPlay = false;
static bool            g_hookedSwitch = false;
static bool            g_hookedPrep = false;
static bool            g_hookedPreload = false;
static volatile LONG   g_callbackLogCount = 0;
static volatile LONG   g_minFadeFrames = 0;
static volatile LONG   g_arenaBattleMusicPending = -1;
static volatile LONG   g_arenaBattleMusicFadeFrames = 90;
static volatile LONG   g_arenaBattleMusicPendingExpireTick = 0;
static ArenaBattleMusicSoundCmdFn g_arenaSoundCmdFn = nullptr;

typedef int (__thiscall *FmodPlayTrack_t)(void* self, unsigned int trackIndex);
typedef int (__thiscall *FmodSwitchCrossfade_t)(
    void* self,
    unsigned int trackIndex,
    int fadeFrames,
    int context);
typedef int (__cdecl *MusicPrepBattleTrack_t)(int trackIndex);
typedef int (__cdecl *MusicPlayTrackWithPreload_t)(unsigned int trackIndex);

static void HookLog(MusicHookLogFn log, const char* fmt, ...) {
    if (!log) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    log(line);
}

static bool EnvFlagEnabled(const char* name) {
    char value[16] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
                       value[0] == 't' || value[0] == 'T');
}

static void LogMusicStackTrace(const char* label, LONG callNo, unsigned int trackIndex) {
    if (!g_traceStack) {
        return;
    }

    void* frames[16] = {};
    USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(sizeof(frames) / sizeof(frames[0])), frames, nullptr);
    char line[512] = {};
    size_t pos = static_cast<size_t>(_snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "[ffx-hooks] MusicHook stack target=%s callback=%ld track=%u frames=",
        label,
        callNo,
        trackIndex));

    for (USHORT i = 0; i < count && pos < sizeof(line); ++i) {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(frames[i]);
        const bool inFfx = g_base != 0 && addr >= g_base && addr < g_base + 0x03000000u;
        const unsigned value = static_cast<unsigned>(inFfx ? (addr - g_base) : addr);
        const int written = _snprintf_s(
            line + pos,
            sizeof(line) - pos,
            _TRUNCATE,
            "%s%s0x%08X",
            i == 0 ? "" : ",",
            inFfx ? "rva:" : "va:",
            value);
        if (written <= 0) {
            break;
        }
        pos += static_cast<size_t>(written);
    }

    HookLog(g_log, "%s", line);
}

const char* GetMusicHookTargetName(MusicHookTarget target) {
    switch (target) {
        case MusicHookTarget::SwitchCrossfade: return "SwitchCrossfade";
        case MusicHookTarget::PlayTrack:
        default: return "PlayTrack";
    }
}

static bool ConsumeOverride(const char* targetLabel, unsigned int* trackIndex) {
    if (!g_block || !trackIndex || g_block->musicOverrideTrackIndex < 0) {
        return false;
    }

    const LONG override = InterlockedExchange(
        reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), -1);
    if (override >= 0 && override <= 0xB5) {
        const unsigned int original = *trackIndex;
        *trackIndex = static_cast<unsigned int>(override);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
        HookLog(g_log, "[ffx-hooks] MusicHook override consumed target=%s original=%u override=%ld",
            targetLabel ? targetLabel : GetMusicHookTargetName(g_target), original, override);
        return true;
    }

    HookLog(g_log, "[ffx-hooks] MusicHook override ignored out-of-range value=%ld", override);
    return false;
}

void SetMusicHookMinFadeFrames(int fadeFrames) {
    if (fadeFrames < 0) fadeFrames = 0;
    if (fadeFrames > 600) fadeFrames = 600;
    InterlockedExchange(&g_minFadeFrames, fadeFrames);
}

static bool IsArenaBattleMusicPendingExpired() {
    const LONG pending = InterlockedCompareExchange(&g_arenaBattleMusicPending, -1, -1);
    if (pending < 0) return false;
    const uint32_t expire = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_arenaBattleMusicPendingExpireTick, 0, 0));
    return expire != 0 && static_cast<int32_t>(GetTickCount() - expire) > 0;
}

void SetArenaBattleMusicPending(int trackIndex, int fadeFrames) {
    if (trackIndex < 0 || trackIndex > 0xB5) {
        InterlockedExchange(&g_arenaBattleMusicPending, -1);
        InterlockedExchange(&g_arenaBattleMusicPendingExpireTick, 0);
        return;
    }
    if (fadeFrames < 0) fadeFrames = 0;
    if (fadeFrames > 600) fadeFrames = 600;
    InterlockedExchange(&g_arenaBattleMusicFadeFrames, fadeFrames);
    InterlockedExchange(&g_arenaBattleMusicPending, trackIndex);
    InterlockedExchange(&g_arenaBattleMusicPendingExpireTick, static_cast<LONG>(GetTickCount() + 45000u));
}

int GetArenaBattleMusicPending() {
    if (IsArenaBattleMusicPendingExpired()) {
        ClearArenaBattleMusicPending();
        return -1;
    }
    return static_cast<int>(InterlockedCompareExchange(&g_arenaBattleMusicPending, -1, -1));
}

void SetArenaBattleMusicSoundCmdFn(ArenaBattleMusicSoundCmdFn fn) {
    g_arenaSoundCmdFn = fn;
}

ArenaBattleMusicSoundCmdFn GetArenaBattleMusicSoundCmdFn() {
    return g_arenaSoundCmdFn;
}

void ClearArenaBattleMusicPending() {
    InterlockedExchange(&g_arenaBattleMusicPending, -1);
    InterlockedExchange(&g_arenaBattleMusicPendingExpireTick, 0);
}

static bool IsAmbientFieldMusicTrack(unsigned int trackIndex) {
    /* Leave overworld/field BGM alone; redirect battle prep + encounter tracks. */
    return trackIndex == 21u;
}

static bool TryPeekArenaBattleMusic(unsigned int requestedTrack, unsigned int* outTrack) {
    if (!outTrack || IsAmbientFieldMusicTrack(requestedTrack)) {
        return false;
    }
    if (IsArenaBattleMusicPendingExpired()) {
        ClearArenaBattleMusicPending();
        return false;
    }
    const LONG pending = InterlockedCompareExchange(&g_arenaBattleMusicPending, -1, -1);
    if (pending < 0) {
        return false;
    }
    *outTrack = static_cast<unsigned int>(pending);
    return true;
}

static void ConsumeArenaBattleMusicPending() {
    ClearArenaBattleMusicPending();
}

static bool ApplyArenaBattleTrackSwap(const char* label, unsigned int* trackIndex, bool consumePending) {
    if (!trackIndex) {
        return false;
    }
    unsigned int arenaTrack = *trackIndex;
    if (!TryPeekArenaBattleMusic(*trackIndex, &arenaTrack)) {
        return false;
    }
    const unsigned int original = *trackIndex;
    *trackIndex = arenaTrack;
    if (consumePending) {
        ConsumeArenaBattleMusicPending();
    }
    HookLog(g_log,
        "[ffx-hooks] MusicHook Arena+ %s(%u) -> track=%u%s",
        label ? label : "swap",
        original,
        arenaTrack,
        consumePending ? " (pending consumed)" : "");
    return true;
}

static bool TriggerArenaLabMusicRecipe(unsigned int overrideTrack) {
    if (overrideTrack > 0xB5u) {
        return false;
    }
    const unsigned int triggerTrack = (overrideTrack == 4u) ? 7u : 4u;
    if (g_block) {
        InterlockedExchange(
            reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex),
            static_cast<LONG>(overrideTrack));
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    }
    int32_t ret = 0;
    bool ok = false;
    if (g_arenaSoundCmdFn) {
        ok = g_arenaSoundCmdFn(triggerTrack, &ret);
    }
    HookLog(g_log,
        "[ffx-hooks] MusicHook Arena+ lab recipe override=%u trigger=%u soundcmd=%d ret=%d",
        overrideTrack,
        triggerTrack,
        ok ? 1 : 0,
        ret);
    return ok;
}

static int __cdecl MusicPrepBattleTrack_Shim(int trackIndex) {
    const LONG callNo = InterlockedIncrement(&g_callbackLogCount);
    int track = trackIndex;
    unsigned int arenaTrack = static_cast<unsigned int>(track);
    if (TryPeekArenaBattleMusic(static_cast<unsigned int>(track), &arenaTrack)) {
        track = static_cast<int>(arenaTrack);
        HookLog(g_log,
            "[ffx-hooks] MusicHook Arena+ PrepBattleTrack(%d) -> track=%d",
            trackIndex,
            track);
    }
    if (callNo <= 16) {
        HookLog(g_log, "[ffx-hooks] MusicHook PrepBattleTrack callback #%ld track=%d",
            callNo, track);
    }
    LogMusicStackTrace("PrepBattleTrack", callNo, static_cast<unsigned int>(track));
    return ((MusicPrepBattleTrack_t)g_trampolinePrep)(track);
}

static int __cdecl MusicPlayWithPreload_Shim(unsigned int trackIndex) {
    const LONG callNo = InterlockedIncrement(&g_callbackLogCount);
    if (callNo <= 32) {
        HookLog(g_log, "[ffx-hooks] MusicHook PlayTrackWithPreload callback #%ld track=%u",
            callNo, trackIndex);
    }
    LogMusicStackTrace("PlayTrackWithPreload", callNo, trackIndex);

    unsigned int arenaTrack = trackIndex;
    if (TryPeekArenaBattleMusic(trackIndex, &arenaTrack)) {
        if (g_arenaSoundCmdFn) {
            // Caminho lab (soundcmd disponivel): recipe + suprime a vanilla (a faixa sobe via soundcmd).
            HookLog(g_log,
                "[ffx-hooks] MusicHook Arena+ battle-entry PlayTrackWithPreload(%u) -> lab override=%u (suppress vanilla)",
                trackIndex,
                arenaTrack);
            TriggerArenaLabMusicRecipe(arenaTrack);
            ConsumeArenaBattleMusicPending();
            return 0;
        }
        // No soundcmd (probe heartbeat off): replaces track, plays vanilla — no silence
        // e sem vazar o override para a proxima PlayTrack (fix 2026-08-02).
        HookLog(g_log,
            "[ffx-hooks] MusicHook Arena+ battle-entry PlayTrackWithPreload(%u) -> track=%u (sem soundcmd, vanilla substituida)",
            trackIndex,
            arenaTrack);
        trackIndex = arenaTrack;
        ConsumeArenaBattleMusicPending();
    }

    ConsumeOverride("PlayTrackWithPreload", &trackIndex);
    return ((MusicPlayTrackWithPreload_t)g_trampolinePreload)(trackIndex);
}

static int __fastcall MusicHook_Shim(void* self, void* /*edx*/, unsigned int trackIndex) {
    const LONG callNo = InterlockedIncrement(&g_callbackLogCount);
    if (callNo <= 32) {
        HookLog(g_log, "[ffx-hooks] MusicHook PlayTrack callback #%ld this=0x%08X track=%u",
            callNo, static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)), trackIndex);
    }
    LogMusicStackTrace("PlayTrack", callNo, trackIndex);
    const unsigned int originalTrack = trackIndex;
    unsigned int arenaTrack = trackIndex;
    if (TryPeekArenaBattleMusic(trackIndex, &arenaTrack)) {
        trackIndex = arenaTrack;
        HookLog(g_log,
            "[ffx-hooks] MusicHook Arena+ PlayTrack(%u) -> track=%u",
            originalTrack,
            trackIndex);
        ConsumeArenaBattleMusicPending();
    } else {
        ConsumeOverride("PlayTrack", &trackIndex);
    }

    /* FIX 2026-08-05 (root cause): PlayTrackWithPreload apenas seta o event_index no
     * sub-struct engine (FmodMusic_SetTrackAndPlay) mas NAO popula tracks_array[track].event_ptr.
     * PlayTrackByIndex checa tracks_array[track].event_ptr != 0 e skipa se for 0 → silencio.
     *
     * Caminho correto (decifrado via IDA): SwitchCrossfade(self, track, fade, load_flag) faz:
     *   1. ReadEventByRuntimeId(self, track, load_flag) → MapRuntimeIdToFevIndex(track) →
     *      EventSystem->GetEvent(fev_index) → popula tracks_array[track].event_ptr
     *   2. PlayTrackByIndex(self, track) → toca via FMOD::Event::start
     *
     * Track 145 ("Challenge") mapeia pra FEV index 72 (dword_B4F1C8[145]=0x48), valido.
     * unk_CEC164=0 (load gate nao bloqueia). IsValidTrackIndex(145)=true (145<=181).
     *
     * Chamamos SwitchCrossfade com fade=0 (sem crossfade, so carrega+toc) e load_flag=1
     * (forca ReadEventByRuntimeId a carregar o event FMOD). */
    if (trackIndex != originalTrack) {
        if (g_trampolineSwitch) {
            HookLog(g_log,
                "[ffx-hooks] MusicHook SwitchCrossfade(%u) override fade=0 load_flag=1 (load+play)",
                trackIndex);
            int ret = ((FmodSwitchCrossfade_t)g_trampolineSwitch)(self, trackIndex, 0, 1);
            HookLog(g_log, "[ffx-hooks] MusicHook SwitchCrossfade(%u) returned %d", trackIndex, ret);
            return ret;
        }
        if (g_base) {
            FmodSwitchCrossfade_t switchFn = reinterpret_cast<FmodSwitchCrossfade_t>(
                g_base + RVA_FMOD_SWITCH_CROSSFADE);
            HookLog(g_log,
                "[ffx-hooks] MusicHook SwitchCrossfade(%u) override via base fade=0 load_flag=1",
                trackIndex);
            int ret = switchFn(self, trackIndex, 0, 1);
            HookLog(g_log, "[ffx-hooks] MusicHook SwitchCrossfade(%u) returned %d", trackIndex, ret);
            return ret;
        }
    }

    return ((FmodPlayTrack_t)g_trampolinePlay)(self, trackIndex);
}

static int __fastcall MusicSwitch_Shim(
    void* self,
    void* /*edx*/,
    unsigned int trackIndex,
    int fadeFrames,
    int context) {
    const LONG callNo = InterlockedIncrement(&g_callbackLogCount);
    if (callNo <= 16) {
        HookLog(g_log,
            "[ffx-hooks] MusicHook SwitchCrossfade callback #%ld this=0x%08X track=%u fade=%d context=%d",
            callNo, static_cast<unsigned>(reinterpret_cast<uintptr_t>(self)),
            trackIndex, fadeFrames, context);
    }
    LogMusicStackTrace("SwitchCrossfade", callNo, trackIndex);
    const unsigned int originalTrack = trackIndex;
    unsigned int arenaTrack = trackIndex;
    const bool arenaSwap = TryPeekArenaBattleMusic(trackIndex, &arenaTrack);
    if (arenaSwap) {
        trackIndex = arenaTrack;
        HookLog(g_log,
            "[ffx-hooks] MusicHook Arena+ SwitchCrossfade(%u) -> track=%u",
            originalTrack,
            trackIndex);
        ConsumeArenaBattleMusicPending();
    }
    const bool consumed = arenaSwap || ConsumeOverride("SwitchCrossfade", &trackIndex);
    if (consumed) {
        const LONG minFade = InterlockedCompareExchange(&g_minFadeFrames, 0, 0);
        if (minFade > 0 && fadeFrames < minFade) {
            fadeFrames = minFade;
        }
    }

    return ((FmodSwitchCrossfade_t)g_trampolineSwitch)(self, trackIndex, fadeFrames, context);
}

static MusicHookInstallResult InstallMusicHookAtRva(
    uintptr_t base,
    uint32_t targetRva,
    const char* label,
    uint64_t shimVa,
    PLH::x86Detour** detourOut,
    uint64_t* trampolineOut,
    bool* hookedOut) {
    MusicHookInstallResult result = { false, 0 };
    if (!detourOut || !trampolineOut || !hookedOut || *detourOut) {
        return result;
    }

    const uint64_t targetVa = static_cast<uint64_t>(base + targetRva);

    HookLog(g_log, "[ffx-hooks] MusicHook before new x86Detour target=%s VA=0x%08X",
        label ? label : "?", static_cast<unsigned>(targetVa));

    try {
        *detourOut = new PLH::x86Detour(targetVa, shimVa, trampolineOut);
        *hookedOut = (*detourOut)->hook();
        result.ok = *hookedOut;
        result.trampoline = *trampolineOut;
        HookLog(g_log, "[ffx-hooks] MusicHook hook() target=%s returned %d trampoline=0x%llX",
            label ? label : "?",
            *hookedOut ? 1 : 0,
            static_cast<unsigned long long>(*trampolineOut));
        if (!*hookedOut) {
            delete *detourOut;
            *detourOut = nullptr;
            *trampolineOut = 0;
        }
    } catch (const std::exception& ex) {
        HookLog(g_log, "[ffx-hooks] ERROR MusicHook exception during install(%s): %s",
            label ? label : "?", ex.what());
        delete *detourOut;
        *detourOut = nullptr;
        *trampolineOut = 0;
        *hookedOut = false;
    } catch (...) {
        HookLog(g_log, "[ffx-hooks] ERROR MusicHook unknown exception during install(%s)",
            label ? label : "?");
        delete *detourOut;
        *detourOut = nullptr;
        *trampolineOut = 0;
        *hookedOut = false;
    }

    return result;
}

static MusicHookInstallResult InstallMusicHookAt(
    uintptr_t base,
    MusicHookTarget target,
    uint64_t shimVa,
    PLH::x86Detour** detourOut,
    uint64_t* trampolineOut,
    bool* hookedOut) {
    const uintptr_t targetRva =
        (target == MusicHookTarget::SwitchCrossfade) ? RVA_FMOD_SWITCH_CROSSFADE : RVA_FMOD_PLAY_TRACK;
    return InstallMusicHookAtRva(
        base,
        static_cast<uint32_t>(targetRva),
        GetMusicHookTargetName(target),
        shimVa,
        detourOut,
        trampolineOut,
        hookedOut);
}

MusicHookInstallResult InstallMusicHookArenaBattle(
    uintptr_t base,
    FFXHooksBlock* block,
    MusicHookLogFn log) {
    MusicHookInstallResult result = { false, 0 };
    if (g_detourPlay || g_detourSwitch || g_detourPrep || g_detourPreload) {
        result.ok = g_hookedPreload && g_hookedSwitch;
        return result;
    }

    g_block = block;
    g_log = log;
    g_base = base;
    g_traceStack = EnvFlagEnabled("FFXHOOKS_TRACE_MUSIC_STACK");
    g_callbackLogCount = 0;

    const MusicHookInstallResult prepResult = InstallMusicHookAtRva(
        base,
        RVA_MUSIC_PREP_BATTLE_TRACK,
        "PrepBattleTrack",
        reinterpret_cast<uint64_t>(MusicPrepBattleTrack_Shim),
        &g_detourPrep,
        &g_trampolinePrep,
        &g_hookedPrep);
    const MusicHookInstallResult preloadResult = InstallMusicHookAtRva(
        base,
        RVA_MUSIC_PLAY_TRACK_WITH_PRELOAD,
        "PlayTrackWithPreload",
        reinterpret_cast<uint64_t>(MusicPlayWithPreload_Shim),
        &g_detourPreload,
        &g_trampolinePreload,
        &g_hookedPreload);
    const MusicHookInstallResult switchResult = InstallMusicHookAt(
        base,
        MusicHookTarget::SwitchCrossfade,
        reinterpret_cast<uint64_t>(MusicSwitch_Shim),
        &g_detourSwitch,
        &g_trampolineSwitch,
        &g_hookedSwitch);
    const MusicHookInstallResult playResult = InstallMusicHookAt(
        base,
        MusicHookTarget::PlayTrack,
        reinterpret_cast<uint64_t>(MusicHook_Shim),
        &g_detourPlay,
        &g_trampolinePlay,
        &g_hookedPlay);

    result.ok = preloadResult.ok && switchResult.ok;
    result.trampoline = g_trampolinePreload ? g_trampolinePreload : g_trampolineSwitch;
    if (!result.ok) {
        RemoveMusicHook(log);
    } else {
        HookLog(log,
            "[ffx-hooks] MusicHook Arena battle install ok PlayTrackWithPreload+SwitchCrossfade (Prep=%d PlayTrack fallback=%d)\n",
            prepResult.ok ? 1 : 0,
            playResult.ok ? 1 : 0);
    }
    return result;
}

MusicHookInstallResult InstallMusicHookDual(
    uintptr_t base,
    FFXHooksBlock* block,
    MusicHookLogFn log) {
    MusicHookInstallResult result = { false, 0 };
    if (g_detourPlay || g_detourSwitch || g_detourPrep || g_detourPreload) {
        result.ok = g_hookedPlay && g_hookedSwitch;
        return result;
    }

    g_block = block;
    g_log = log;
    g_base = base;
    g_traceStack = EnvFlagEnabled("FFXHOOKS_TRACE_MUSIC_STACK");
    g_callbackLogCount = 0;

    const MusicHookInstallResult playResult = InstallMusicHookAt(
        base,
        MusicHookTarget::PlayTrack,
        reinterpret_cast<uint64_t>(MusicHook_Shim),
        &g_detourPlay,
        &g_trampolinePlay,
        &g_hookedPlay);
    const MusicHookInstallResult switchResult = InstallMusicHookAt(
        base,
        MusicHookTarget::SwitchCrossfade,
        reinterpret_cast<uint64_t>(MusicSwitch_Shim),
        &g_detourSwitch,
        &g_trampolineSwitch,
        &g_hookedSwitch);

    result.ok = playResult.ok && switchResult.ok;
    result.trampoline = g_trampolinePlay;
    if (!result.ok) {
        RemoveMusicHook(log);
    } else {
        HookLog(log, "[ffx-hooks] MusicHook dual install ok PlayTrack+SwitchCrossfade\n");
    }
    return result;
}

MusicHookInstallResult InstallMusicHook(
    uintptr_t base,
    FFXHooksBlock* block,
    MusicHookTarget target,
    MusicHookLogFn log) {
    MusicHookInstallResult result = { false, 0 };
    if (g_detourPlay || g_detourSwitch || g_detourPrep || g_detourPreload) {
        const bool hooked = g_hookedPlay || g_hookedSwitch || g_hookedPrep || g_hookedPreload;
        HookLog(log, "[ffx-hooks] MusicHook install skipped: detour already exists (play=%d switch=%d prep=%d preload=%d)\n",
            g_hookedPlay ? 1 : 0, g_hookedSwitch ? 1 : 0, g_hookedPrep ? 1 : 0, g_hookedPreload ? 1 : 0);
        result.ok = hooked;
        result.trampoline = g_trampolinePlay ? g_trampolinePlay : g_trampolineSwitch;
        return result;
    }

    g_block = block;
    g_log = log;
    g_target = target;
    g_base = base;
    g_traceStack = EnvFlagEnabled("FFXHOOKS_TRACE_MUSIC_STACK");
    g_callbackLogCount = 0;
    if (g_traceStack) {
        HookLog(log, "[ffx-hooks] MusicHook stack trace enabled via FFXHOOKS_TRACE_MUSIC_STACK=1");
    }

    PLH::x86Detour** detourOut = (target == MusicHookTarget::SwitchCrossfade) ? &g_detourSwitch : &g_detourPlay;
    uint64_t* trampolineOut = (target == MusicHookTarget::SwitchCrossfade) ? &g_trampolineSwitch : &g_trampolinePlay;
    bool* hookedOut = (target == MusicHookTarget::SwitchCrossfade) ? &g_hookedSwitch : &g_hookedPlay;
    const uint64_t shimVa = (target == MusicHookTarget::SwitchCrossfade)
        ? reinterpret_cast<uint64_t>(MusicSwitch_Shim)
        : reinterpret_cast<uint64_t>(MusicHook_Shim);

    result = InstallMusicHookAt(base, target, shimVa, detourOut, trampolineOut, hookedOut);
    if (!result.ok) {
        g_block = nullptr;
        g_log = nullptr;
    }
    return result;
}

static bool UnhookDetour(PLH::x86Detour* detour, bool hooked, MusicHookLogFn log, const char* label) {
    if (!detour) return true;
    bool removed = true;
    if (hooked) {
        try {
            HookLog(log, "[ffx-hooks] MusicHook before unHook(%s)", label ? label : "?");
            removed = detour->unHook();
            HookLog(log, "[ffx-hooks] MusicHook unHook(%s) returned %d", label ? label : "?", removed ? 1 : 0);
        } catch (const std::exception& ex) {
            HookLog(log, "[ffx-hooks] ERROR MusicHook exception during unHook(%s): %s",
                label ? label : "?", ex.what());
            removed = false;
        } catch (...) {
            HookLog(log, "[ffx-hooks] ERROR MusicHook unknown exception during unHook(%s)", label ? label : "?");
            removed = false;
        }
    }
    delete detour;
    return removed;
}

bool RemoveMusicHook(MusicHookLogFn log) {
    const bool removedPlay = UnhookDetour(g_detourPlay, g_hookedPlay, log, "PlayTrack");
    const bool removedSwitch = UnhookDetour(g_detourSwitch, g_hookedSwitch, log, "SwitchCrossfade");
    const bool removedPrep = UnhookDetour(g_detourPrep, g_hookedPrep, log, "PrepBattleTrack");
    const bool removedPreload = UnhookDetour(g_detourPreload, g_hookedPreload, log, "PlayTrackWithPreload");
    g_detourPlay = nullptr;
    g_detourSwitch = nullptr;
    g_detourPrep = nullptr;
    g_detourPreload = nullptr;
    g_trampolinePlay = 0;
    g_trampolineSwitch = 0;
    g_trampolinePrep = 0;
    g_trampolinePreload = 0;
    g_block = nullptr;
    g_log = nullptr;
    g_base = 0;
    g_traceStack = false;
    g_hookedPlay = false;
    g_hookedSwitch = false;
    g_hookedPrep = false;
    g_hookedPreload = false;
    g_callbackLogCount = 0;
    InterlockedExchange(&g_minFadeFrames, 0);
    g_arenaSoundCmdFn = nullptr;
    ClearArenaBattleMusicPending();
    return removedPlay && removedSwitch && removedPrep && removedPreload;
}

bool IsMusicHookInstalled() {
    return g_hookedPlay || g_hookedSwitch || g_hookedPrep || g_hookedPreload;
}

uint64_t GetMusicHookTrampoline() {
    if (g_trampolinePreload) return g_trampolinePreload;
    if (g_trampolinePlay) return g_trampolinePlay;
    return g_trampolineSwitch;
}

#else

MusicHookInstallResult InstallMusicHook(uintptr_t, FFXHooksBlock*, MusicHookTarget, MusicHookLogFn) {
    return { false, 0 };
}

MusicHookInstallResult InstallMusicHookDual(uintptr_t, FFXHooksBlock*, MusicHookLogFn) {
    return { false, 0 };
}

MusicHookInstallResult InstallMusicHookArenaBattle(uintptr_t, FFXHooksBlock*, MusicHookLogFn) {
    return { false, 0 };
}

void SetMusicHookMinFadeFrames(int) {
}

void SetArenaBattleMusicPending(int, int) {
}

void SetArenaBattleMusicSoundCmdFn(ArenaBattleMusicSoundCmdFn) {
}

int GetArenaBattleMusicPending() {
    return -1;
}

void ClearArenaBattleMusicPending() {
}

bool RemoveMusicHook(MusicHookLogFn) {
    return true;
}

bool IsMusicHookInstalled() {
    return false;
}

uint64_t GetMusicHookTrampoline() {
    return 0;
}

const char* GetMusicHookTargetName(MusicHookTarget target) {
    switch (target) {
        case MusicHookTarget::SwitchCrossfade: return "SwitchCrossfade";
        case MusicHookTarget::PlayTrack:
        default: return "PlayTrack";
    }
}

#endif

} // namespace FfxHooks
