// Jarvis-HOOK: execute mapped, relocated FFX music consumers without starting FFX.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#pragma warning(push)
#pragma warning(disable: 4505)
#include "../hooks/MusicHook.cpp"
#pragma warning(pop)
#include "MusicPeFixture.inc"
#include "../hooks/ArenaSoundtrack.h"

using namespace FfxHooks;
using NativeRead = int(__thiscall*)(void*, unsigned int, int);
static int checks, failures, eventReads, starts, callbacks;
static unsigned int fevIndex;
static int fakeEvent;
static std::array<uintptr_t, 16> eventSystemVtable;
static uintptr_t eventSystem;
static std::array<uint32_t, 16> musicSystem;
static std::array<std::array<uint32_t, 15>, 182> tracks;
static FFXHooksBlock block;
static int loadLogs, playLogs, validLoadLogs, activePlayLogs;

static void CaptureLog(const char* line) {
    if (std::strstr(line, "native load")) {
        ++loadLogs; if (std::strstr(line, "state=1/1")) ++validLoadLogs;
    }
    if (std::strstr(line, "native play")) {
        ++playLogs; if (std::strstr(line, "active=1")) ++activePlayLogs;
    }
}

static void Check(bool ok, const char* label) {
    ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", label); }
}
static int __stdcall GetEvent(void* self, unsigned int index, int mode, void** output) {
    Check(self == &eventSystem && mode == 0, "native event lookup preserves FMOD arguments");
    ++eventReads; fevIndex = index; *output = &fakeEvent; return 0;
}
static int __stdcall GetState(void* event, int* state) {
    Check(event == &fakeEvent, "native playback queries the loaded event"); *state = 1; return 0;
}
static int __stdcall SetCallback(void* event, void*, void*) {
    Check(event == &fakeEvent, "native playback registers the loaded event callback"); ++callbacks; return 0;
}
static int __stdcall StartEvent(void* event) {
    Check(event == &fakeEvent, "native playback starts the selected event"); ++starts; return 0;
}
static int __cdecl LogResult(int, const char*, int) { return 0; }

static void Jump(unsigned char* address, uintptr_t target) {
    address[0] = 0xE9;
    const int32_t relative = static_cast<int32_t>(target - reinterpret_cast<uintptr_t>(address + 5));
    std::memcpy(address + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), address, 5);
}

static void Reset(unsigned char* image, unsigned track, unsigned status) {
    // Explicitly model audio loading enabled without running game startup.
    *reinterpret_cast<uint32_t*>(image + 0x8EC164u) = 0;
    musicSystem.fill(0); for (auto& slot : tracks) slot.fill(0);
    eventSystemVtable.fill(0);
    eventSystemVtable[0x24 / 4] = reinterpret_cast<uintptr_t>(GetEvent);
    eventSystem = reinterpret_cast<uintptr_t>(eventSystemVtable.data());
    musicSystem[0x10 / 4] = reinterpret_cast<uint32_t>(tracks.data());
    musicSystem[0x18 / 4] = track;
    musicSystem[0] = musicSystem[1] = reinterpret_cast<uint32_t>(&eventSystem);
    tracks[track][1] = status;
    eventReads = starts = callbacks = loadLogs = playLogs = validLoadLogs = activePlayLogs = 0; fevIndex = ~0u;
    std::memset(&block, 0, sizeof(block)); block.musicOverrideTrackIndex = -1;
    g_base = reinterpret_cast<uintptr_t>(image); g_block = &block; g_log = CaptureLog; g_traceStack = false;
    g_trampolinePlay = reinterpret_cast<uint64_t>(image + RVA_FMOD_PLAY_TRACK);
    g_trampolineSwitch = reinterpret_cast<uint64_t>(image + RVA_FMOD_SWITCH_CROSSFADE);
    g_readEvent = ResolveMusicEventReader(g_base);
    ClearArenaBattleMusicPending(); SetArenaBattleMusicSoundCmdFn(nullptr);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto* image = MapPe(Read(argv[1])); if (!image) return 2;
    // Only FMOD boundary thunks and its result logger are replaced. Switch,
    // ReadEvent, runtime-to-FEV mapping and PlayTrack execute native PE bytes.
    Jump(image + 0x548CA6u, reinterpret_cast<uintptr_t>(GetState));
    Jump(image + 0x548CACu, reinterpret_cast<uintptr_t>(SetCallback));
    Jump(image + 0x548C9Au, reinterpret_cast<uintptr_t>(StartEvent));
    Jump(image + 0x307AC0u, reinterpret_cast<uintptr_t>(LogResult));
    auto nativeSwitch = reinterpret_cast<FmodSwitchCrossfade_t>(image + RVA_FMOD_SWITCH_CROSSFADE);
    auto nativeRead = reinterpret_cast<NativeRead>(image + 0x309170u);
    auto nativePlay = reinterpret_cast<FmodPlayTrack_t>(image + RVA_FMOD_PLAY_TRACK);
    Check(ResolveMusicEventReader(reinterpret_cast<uintptr_t>(image)) == nativeRead,
        "native event reader signature matches the exact mapped executable");
    image[RVA_FMOD_READ_EVENT_BY_RUNTIME_ID] ^= 1;
    Check(!ResolveMusicEventReader(reinterpret_cast<uintptr_t>(image)), "changed loader bytes fail closed");
    image[RVA_FMOD_READ_EVENT_BY_RUNTIME_ID] ^= 1;

    for (unsigned status : {0u, 1u}) {
        Reset(image, 145, status);
        nativeSwitch(musicSystem.data(), 145, 0, 1);
        Check(eventReads == 0 && starts == 0 && tracks[145][0] == 0,
            "real SwitchCrossfade returns early without loading a same-current or flagged-empty slot");

        Reset(image, 145, status);
        nativeRead(musicSystem.data(), 145, 1);
        nativePlay(musicSystem.data(), 145);
        Check(eventReads == 1 && fevIndex == 72 && starts == 1 && callbacks == 1,
            "real ReadEvent then PlayTrack resolves Challenge and starts its event");

        Reset(image, 145, status); SetArenaBattleMusicPending(145, 90);
        g_arenaBattleMusicPreloadQueued = 1;
        MusicHook_Shim(musicSystem.data(), nullptr, 145);
        Check(eventReads == 1 && fevIndex == 72 && starts == 1,
            "production replacement loads and starts Challenge through the native consumers");
        Check(GetArenaBattleMusicPending() == -1, "production native load consumes pending request");
        Check(loadLogs == 1 && playLogs == 1 && validLoadLogs == 1 && activePlayLogs == 1,
            "bounded native diagnostics observe valid bank, event and active playback");
    }
    for (const auto& entry : ArenaSoundtrack::kTracks) {
        Reset(image, entry.id, 1); musicSystem[0x2C / 4] = 1;
        SetArenaBattleMusicPending(entry.id, 90); g_arenaBattleMusicPreloadQueued = 1;
        MusicHook_Shim(musicSystem.data(), nullptr, entry.id);
        Check(eventReads == 1 && starts == 1 && callbacks == 1,
            "every catalog track reaches native load and start in the alternate bank mode");
    }
    Reset(image, 145, 1); *reinterpret_cast<uint32_t*>(image + RVA_FMOD_EVENT_LOAD_DISABLED) = 1;
    SetArenaBattleMusicPending(145, 90); g_arenaBattleMusicPreloadQueued = 1;
    MusicHook_Shim(musicSystem.data(), nullptr, 145);
    Check(eventReads == 0 && starts == 0 && tracks[145][0] == 0,
        "native global load-disable gate remains authoritative");
    Reset(image, 145, 1); musicSystem[0] = 0;
    SetArenaBattleMusicPending(145, 90); g_arenaBattleMusicPreloadQueued = 1;
    MusicHook_Shim(musicSystem.data(), nullptr, 145);
    Check(eventReads == 0 && starts == 0, "missing native music bank remains a safe no-op");
    g_readEvent = nullptr;
    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("MusicNativeRt1: %d/%d checks passed; failures=%d\n", checks-failures, checks, failures);
    return failures ? 1 : 0;
}
