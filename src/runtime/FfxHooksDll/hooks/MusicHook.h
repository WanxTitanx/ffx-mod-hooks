#pragma once
// MusicHook — thiscall interceptor for FFX_FmodMusic_PlayTrackByIndex.
// Status: Phase 1 — RVA confirmed, active when built with FFXHOOKS_HAVE_POLYHOOK.
//
// Design:
//   PolyHook2 x86Detour patches the target function.
//   __fastcall shim receives (this, edx_unused, trackIndex) and can
//   substitute trackIndex from FFXHooksBlock.musicOverrideTrackIndex.

#include <stdint.h>
#include "../shared/ffx_hooks_block.h"

namespace FfxHooks {
    typedef void (*MusicHookLogFn)(const char* message);

    enum class MusicHookTarget {
        PlayTrack,
        SwitchCrossfade
    };

    struct MusicHookInstallResult {
        bool ok;
        uint64_t trampoline;
    };

    MusicHookInstallResult InstallMusicHook(
        uintptr_t base,
        FFXHooksBlock* block,
        MusicHookTarget target,
        MusicHookLogFn log);
    /* Arena+: intercept battle-entry PlayTrack AND delayed SwitchCrossfade fallback. */
    MusicHookInstallResult InstallMusicHookDual(
        uintptr_t base,
        FFXHooksBlock* block,
        MusicHookLogFn log);
    /* Arena+ v5: hook FSM case-8 async path (Prep + PlayTrackWithPreload) + SwitchCrossfade fallback. */
    MusicHookInstallResult InstallMusicHookArenaBattle(
        uintptr_t base,
        FFXHooksBlock* block,
        MusicHookLogFn log);
    void SetMusicHookMinFadeFrames(int fadeFrames);
    /* Arena+: replace next battle-entry track 16 via lab recipe (override + soundcmd trigger mismatch). */
    typedef bool (*ArenaBattleMusicSoundCmdFn)(unsigned int triggerTrack, int32_t* retOut);
    void SetArenaBattleMusicPending(int trackIndex, int fadeFrames);
    void SetArenaBattleMusicSoundCmdFn(ArenaBattleMusicSoundCmdFn fn);
    ArenaBattleMusicSoundCmdFn GetArenaBattleMusicSoundCmdFn();
    int GetArenaBattleMusicPending();
    void ClearArenaBattleMusicPending();
    bool RemoveMusicHook(MusicHookLogFn log);
    bool IsMusicHookInstalled();
    uint64_t GetMusicHookTrampoline();
    const char* GetMusicHookTargetName(MusicHookTarget target);
}
