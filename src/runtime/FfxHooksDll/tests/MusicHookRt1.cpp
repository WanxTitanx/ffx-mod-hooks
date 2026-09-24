// Jarvis-HOOK: exercise the production callbacks without installing game hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#pragma warning(push)
#pragma warning(disable: 4505)
#include "../hooks/MusicHook.cpp"
#pragma warning(pop)
#include "../hooks/ArenaSoundtrack.h"

using namespace FfxHooks;
static FFXHooksBlock block;
static int checks, failures, loads, plays, preloads, preps, recipes;
static unsigned int played, loaded, prepared, preloadTrack;
static int loadFlag, loadFade;
static bool resident[182];
static int systemToken;
static DWORD producerThread, playbackThread;

static void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", message); }
}

static int __fastcall NativePlay(void* self, void*, unsigned int track) {
    Check(self == &systemToken, "native Play preserves the music-system this pointer");
    ++plays; played = track;
    playbackThread = GetCurrentThreadId();
    return track < 182 && resident[track] ? 1 : 0;
}

static int __fastcall NativeSwitch(void* self, void*, unsigned int track, int fade, int context) {
    ++loads; loaded = track; loadFlag = context; loadFade = fade;
    if (track < 182 && context == 1) resident[track] = true;
    // The real SwitchCrossfade calls the hooked PlayTrack entry again.
    return MusicHook_Shim(self, nullptr, track);
}

static int __fastcall NativeRead(void* self, void*, unsigned int track, int context) {
    Check(self == &systemToken, "native ReadEvent preserves music-system this pointer");
    ++loads; loaded = track; loadFlag = context; loadFade = 0;
    if (track < 182 && context == 1) resident[track] = true;
    return 0;
}

static int __cdecl NativePreload(unsigned int track) {
    ++preloads; preloadTrack = track;
    // Native opcode 39 is enqueued by 006FA3E0, not called on this stack.
    // Selecting an event alone does not populate its event pointer.
    return 37;
}

static DWORD WINAPI AudioConsumer(void*) {
    return static_cast<DWORD>(MusicHook_Shim(&systemToken, nullptr, preloadTrack));
}

static int BattleEntry(unsigned int track) {
    producerThread = GetCurrentThreadId();
    Check(MusicPlayWithPreload_Shim(track) == 37, "preload preserves native return before audio consumption");
    Check(plays == 0, "preload only queues playback");
    HANDLE thread = CreateThread(nullptr, 0, AudioConsumer, nullptr, 0, nullptr);
    Check(thread != nullptr, "isolated audio consumer thread created");
    if (!thread) return -1;
    WaitForSingleObject(thread, INFINITE);
    DWORD result = 0; GetExitCodeThread(thread, &result); CloseHandle(thread);
    Check(playbackThread != producerThread, "intent crosses from producer to audio consumer thread");
    return static_cast<int>(result);
}

static int __cdecl NativePrep(int track) { ++preps; prepared = static_cast<unsigned>(track); return 73; }
static bool SoundCommand(unsigned int track, int32_t* result) {
    ++recipes;
    *result = MusicSwitch_Shim(&systemToken, nullptr, track, 90, 1);
    return true;
}

static void Reset() {
    std::memset(&block, 0, sizeof(block)); block.musicOverrideTrackIndex = -1;
    std::memset(resident, 0, sizeof(resident));
    loads = plays = preloads = preps = recipes = 0;
    played = loaded = prepared = preloadTrack = ~0u;
    loadFlag = loadFade = -1;
    g_block = &block; g_base = 0; g_log = nullptr; g_traceStack = false;
    g_callbackLogCount = 0;
    g_trampolinePlay = reinterpret_cast<uint64_t>(NativePlay);
    g_trampolineSwitch = reinterpret_cast<uint64_t>(NativeSwitch);
    g_trampolinePreload = reinterpret_cast<uint64_t>(NativePreload);
    g_trampolinePrep = reinterpret_cast<uint64_t>(NativePrep);
    g_readEvent = reinterpret_cast<FmodReadEvent_t>(NativeRead);
    SetArenaBattleMusicSoundCmdFn(nullptr);
    ClearArenaBattleMusicPending(); SetMusicHookMinFadeFrames(0);
}

static void Arm(unsigned int track, bool shared = true) {
    SetArenaBattleMusicPending(static_cast<int>(track), 90);
    if (shared) block.musicOverrideTrackIndex = static_cast<int>(track);
}

static void VerifyLoaded(unsigned int track, int result) {
    Check(result == 1, "selected nonresident event is actually playable");
    Check(loads == 1 && loaded == track, "selected event is materialized exactly once");
    Check(plays == 1 && played == track, "selected event plays exactly once after loading");
    Check(loadFlag == 1 && loadFade == 0, "battle replacement uses synchronous load and play");
    Check(GetArenaBattleMusicPending() == -1, "pending intent consumed at playback");
    Check(block.musicOverrideTrackIndex == -1, "shared override cannot leak into the next track");
}

int main() {
    Reset(); Arm(145);
    VerifyLoaded(145, BattleEntry(16));
    Check(preloads == 1 && preloadTrack == 145, "native preload bookkeeping receives selected track");

    Reset(); Arm(145);
    Check(MusicPrepBattleTrack_Shim(16) == 73 && prepared == 145 && preps == 1, "prep preserves return and selects Challenge");
    Check(GetArenaBattleMusicPending() == 145, "prep retains intent until playback");
    VerifyLoaded(145, BattleEntry(145));

    Reset(); Arm(16); VerifyLoaded(16, BattleEntry(16));
    Reset(); Arm(145); VerifyLoaded(145, MusicHook_Shim(&systemToken, nullptr, 16));
    Reset(); Arm(145); VerifyLoaded(145, MusicHook_Shim(&systemToken, nullptr, 145));
    Reset(); block.musicOverrideTrackIndex = 145;
    VerifyLoaded(145, MusicHook_Shim(&systemToken, nullptr, 145));

    Reset(); resident[16] = true;
    Check(BattleEntry(16) == 1, "vanilla track still plays when no override is armed");
    Check(loads == 0 && preloads == 1 && plays == 1 && played == 16, "vanilla path does not force materialization");

    Reset(); Arm(145, false); resident[21] = true;
    Check(MusicPrepBattleTrack_Shim(21) == 73 && prepared == 21, "ambient prep remains unchanged");
    Check(BattleEntry(21) == 1 && loads == 0 && played == 21, "ambient music remains unchanged");
    Check(GetArenaBattleMusicPending() == 145, "ambient playback does not consume arena intent");
    plays = 0;
    VerifyLoaded(145, BattleEntry(16));

    Reset(); Arm(145, false); resident[16] = true;
    g_arenaBattleMusicPendingExpireTick = static_cast<LONG>(GetTickCount() - 1u);
    Check(BattleEntry(16) == 1 && loads == 0 && played == 16, "expired arena intent leaves vanilla playback alone");
    Check(GetArenaBattleMusicPending() == -1, "expired arena intent cleared");

    Reset(); Arm(145); SetArenaBattleMusicSoundCmdFn(SoundCommand);
    Check(MusicPlayWithPreload_Shim(16) == 0 && recipes == 1 && preloads == 0, "available soundcmd retains its recipe and suppresses vanilla");
    Check(loads == 1 && plays == 1 && played == 145 && block.musicOverrideTrackIndex == -1, "soundcmd loads selected track exactly once");

    // Different selections must not be mistaken for an already loaded previous battle.
    Reset(); Arm(145); VerifyLoaded(145, BattleEntry(16));
    loads = plays = 0; Arm(4); VerifyLoaded(4, BattleEntry(16));
    loads = plays = 0; resident[0] = true;
    Check(MusicHook_Shim(&systemToken, nullptr, 0) == 1 && loads == 0 && played == 0, "later native track is not overwritten");

    for (const auto& track : ArenaSoundtrack::kTracks) {
        Reset(); Arm(track.id);
        VerifyLoaded(track.id, BattleEntry(16));
    }

    Reset(); Arm(145); resident[0] = true;
    Check(MusicPlayWithPreload_Shim(16) == 37, "battle preload queues selected track");
    Check(MusicHook_Shim(&systemToken, nullptr, 0) == 1 && loads == 0 && played == 0,
        "older unrelated audio command does not consume the queued replacement");
    Check(GetArenaBattleMusicPending() == 145, "selected load intent survives unrelated audio command");
    plays = 0; VerifyLoaded(145, MusicHook_Shim(&systemToken, nullptr, preloadTrack));

    Reset(); Arm(145); MusicPlayWithPreload_Shim(16); ClearArenaBattleMusicPending();
    Check(MusicHook_Shim(&systemToken, nullptr, 145) == 0 && loads == 0,
        "cancel clears queued intent without touching native playback");
    Check(g_arenaBattleMusicPreloadQueued == 0, "cancel resets preload qualification");

    Reset(); Arm(145); MusicPlayWithPreload_Shim(16); SetArenaBattleMusicPending(-1, 90);
    Check(GetArenaBattleMusicPending() == -1 && g_arenaBattleMusicPreloadQueued == 0,
        "invalid pending selection clears queue qualification");

    // Repeated-install fast path must reject a partially installed music family.
    PLH::x86Detour existing(0, 0, &g_trampolinePreload);
    g_detourPreload = &existing; g_hookedPreload = true; g_hookedSwitch = true; g_hookedPlay = false;
    Check(!InstallMusicHookArenaBattle(0, &block, nullptr).ok, "missing playback hook is not a usable music family");
    g_hookedPlay = true;
    Check(InstallMusicHookArenaBattle(0, &block, nullptr).ok, "complete music family is reusable");
    g_readEvent = nullptr;
    Check(!InstallMusicHookArenaBattle(0, &block, nullptr).ok, "missing native loader rejects partial music family");
    g_detourPreload = nullptr; g_hookedPreload = g_hookedSwitch = g_hookedPlay = false;
    Check(RemoveMusicHook(nullptr) && !g_readEvent, "teardown clears the native reader with the callback family");

    std::printf("MusicHook RT1: %d/%d checks passed; failures=%d\n", checks - failures, checks, failures);
    return failures ? 1 : 0;
}
