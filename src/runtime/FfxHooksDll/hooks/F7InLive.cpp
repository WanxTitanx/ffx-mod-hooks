// F7InLive.cpp - "FFX Editor - In-Live" (F7): Difficulty (RAM), Force Last Battle, Music.
// Lane Jarvis-HOOK. Gate: f7_inlive.flag / FFXHOOKS_ENABLE_F7=1.
//
// Difficulty detours the uniquely evidenced actor-table initializer at VA 0x79C130 on the
// supported executable. The original runs first; the portable transaction then snapshots and
// applies validated fields on the same game thread before the caller mirrors vanilla slots.
// Difficulty never opens battle data, executable files, or saves for writing.
//  - Force Last Battle: read-only hook on FFX_Field_ResolveEncounterToken (VA 0x7828B0) captures
//    the field row + group of the last NATURAL encounter; "Force" = CALL MsBattleEncountExe (VA 0x780DE0,
//    int __cdecl(int,int,float)) on the main thread — a path already proven by Arena+ (ArenaPlus_ForceBattleDirect).
//  - Music: reuses the FFXHooksBlock contract (musicOverrideTrackIndex + musicSeq) for LOCK and the
//    battle-entry pending of MusicHook (SetArenaBattleMusicPending) for BATTLE. Randomizer picks
//    from the playlist at battle start. Requires MusicHook installed (music.flag / FFXHOOKS_ENABLE_MUSIC).
#include "F7InLive.h"
#include "CustomMixRuntime.h"
#include "ArenaBattleProgram.h"
#include "F8FlagCatalog.h"
#include "F7DifficultyCore.h"
#include "MinHookBatchCoordinator.h"
#include "MusicHook.h"   // SetArenaBattleMusicPending / SetMusicHookMinFadeFrames (battle-entry pending)
#include "SharedBattleRuntime.h"
#include "SinAiHook.h"
#include "SinRamConfigCore.h"
#include "../shared/ffx_addresses.h"
#include "SinTransitionPublication.h"
#include "../shared/Config.h"   // Config::CheckEnabled (gate f7.inlive)


#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <share.h>
#include <MinHook.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

static const uint32_t RVA_MS_BATTLE_ENCOUNT   = 0x00380DE0u; // MsBattleEncountExe (field, group, walkedDelta)

static const char* const F7_STATUS_NAMES[F7_STATUS_COUNT] = {
    "Death", "Zombie", "Petrify", "Poison", "PowerBreak", "MagicBreak", "ArmorBreak", "MentalBreak",
    "Confuse", "Berserk", "Provoke", "Threaten", "Sleep", "Silence", "Darkness", "Shell",
    "Protect", "Reflect", "NulTide", "NulBlaze", "NulShock", "NulFrost", "Regen", "Haste", "Slow"
};

// Global runtime state.
static uintptr_t          g_base       = 0;
static FFXHooksBlock*     g_block      = nullptr;
static void (*g_log)(const char*)      = nullptr;
static volatile LONG      g_enabled    = 0;
static volatile LONG      g_inBattle   = 0;
static volatile LONG      g_appliedCount = 0;

static void* g_trampResolve = nullptr;
static void* g_trampScene   = nullptr;

using namespace F7Difficulty;

static Runtime g_difficultyRuntime{};
static RuntimeResult g_difficultyLast{};
static AdapterGateCode g_difficultyGate = AdapterGateCode::InvalidArgument;
static DifficultyDetourOwner g_difficultyDetours{};
static void* g_trampActorPopulate = nullptr;
static void* g_trampPositionRead = nullptr;
static bool g_positionHookCreated = false;
static volatile LONG g_positionHookAccepting = 0;
static volatile LONG g_positionCallbacks = 0;
static bool InstallPositionReadHook();
static volatile LONG g_difficultyAccepting = 0;
static volatile LONG g_difficultyCallbacks = 0;
static volatile LONG g_difficultyInShim = 0;
static HANDLE g_difficultyDrainedEvent = nullptr;
static SharedBattleRuntime::ComposerSlot g_initSceneComposer{};
static SRWLOCK g_difficultyRuntimeLock = SRWLOCK_INIT;
static uint64_t g_difficultyGeneration = 0;
static BattleFieldPublication g_difficultyPendingBattleField{};
static SinRam::SinTransitionPublication g_sinTransitionPublication{};
// Explicit launches suppress only ResolveEncounter calls made synchronously on that same thread.
static thread_local EncounterCaptureSuppression g_explicitCaptureSuppression{};
static BattleFieldTicket g_difficultyCurrentBattleField{};
static SinRam::RuntimeRequest g_sinCurrentRequest{};
static uint64_t g_sinLastAcceptedGeneration = 0;
static uint32_t g_sinAreaVisit = 1;
static uint16_t g_sinSceneId = 0xFFFFu,g_sinAreaField = 0;
static size_t g_difficultyPointersRejected = 0;

static void F7_ApplyMusicBattle();   // fwd: armada no battle-start (auto-apply)

// ── Helpers ───────────────────────────────────────────────────────────────
void F7_Log(const char* fmt, ...) {
    if (!g_log) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(line);
}

bool F7_IsEnabled() {
    return InterlockedCompareExchange(&g_enabled, 0, 0) != 0;
}

static void ResolveConfigPath(char* out, size_t cap) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *slash = '\0';
    _snprintf_s(out, cap, _TRUNCATE, "%s\\modules\\config\\f7_inlive.json", path);
}

static bool ReadConfigDocument(
    void*, const char* path, char* out, size_t cap, size_t* lengthOut) {
    if (lengthOut) *lengthOut = 0;
    if (!path || !out || cap == 0 || !lengthOut) return false;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > (LONGLONG)cap) {
        CloseHandle(h);
        return false;
    }
    DWORD read = 0;
    BOOL ok = ReadFile(h, out, (DWORD)size.QuadPart, &read, nullptr);
    CloseHandle(h);
    if (!ok || read != static_cast<DWORD>(size.QuadPart)) return false;
    *lengthOut = static_cast<size_t>(read);
    return true;
}

static bool WriteConfigDocumentAtomic(
    void*, const char* path, const char* data, size_t length) {
    if (!path || (!data && length != 0) || length > kMaxJsonBytes) return false;
    char temporary[MAX_PATH] = {};
    if (_snprintf_s(temporary, sizeof(temporary), _TRUNCATE, "%s.tmp", path) < 0) return false;
    HANDLE file = CreateFileA(
        temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL wrote = WriteFile(
        file, data, static_cast<DWORD>(length), &written, nullptr);
    const BOOL flushed = wrote ? FlushFileBuffers(file) : FALSE;
    const BOOL closed = CloseHandle(file);
    if (!wrote || written != length || !flushed || !closed) {
        DeleteFileA(temporary);
        return false;
    }
    if (!MoveFileExA(
            temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary);
        return false;
    }
    return true;
}

static int JsonInt(const char* json, const char* key, int def) {
    if (!json || !key) return def;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':');
    if (!p) return def;
    return atoi(p + 1);
}

static bool JsonBool(const char* json, const char* key, bool def) {
    if (!json || !key) return def;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':');
    if (!p) return def;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (strncmp(p, "true", 4) == 0 &&
        (p[4] == ',' || p[4] == '}' || p[4] == ' ' || p[4] == '\t' ||
         p[4] == '\r' || p[4] == '\n')) return true;
    if (strncmp(p, "false", 5) == 0 &&
        (p[5] == ',' || p[5] == '}' || p[5] == ' ' || p[5] == '\t' ||
         p[5] == '\r' || p[5] == '\n')) return false;
    return def;
}

static void JsonIntArray(const char* json, const char* key, int* out, int maxCount, int* outCount) {
    *outCount = 0;
    if (!json || !key || !out) return;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    ++p;
    while (*p && *p != ']' && *outCount < maxCount) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
        if (*p == ']' || !*p) break;
        out[*outCount] = atoi(p);
        ++*outCount;
        while (*p && *p != ',' && *p != ']') ++p;
    }
}



enum class ConfigEditKind : uint8_t {
    DifficultyGlobal,
    MusicReset,
    MusicLock,
    MusicBattle,
    MusicRandomizer,
    MusicFade,
    ForceRepeat,
    ForceRoute,
    NaturalCapture,
};

struct ConfigEdit {
    ConfigEditKind kind = ConfigEditKind::MusicReset;
    F7DifficultyPreset preset{};
    int first = 0;
    int second = 0;
    int third = 0;
    bool flag = false;
};

static F7Config MakeDefaultLegacyConfig() {
    F7Config config{};
    config.music.lockTrack = -1;
    config.music.battleTrack = -1;
    config.force.lastField = -1;
    config.force.lastGroup = -1;
    config.force.repeatCount = 1;
    return config;
}

static void ApplyConfigEdit(
    F7Config* config, DifficultyConfig* difficulty, bool* difficultyValid,
    void* context) {
    if (!config || !difficulty || !difficultyValid || !context) return;
    const ConfigEdit& edit = *static_cast<const ConfigEdit*>(context);
    switch (edit.kind) {
        case ConfigEditKind::DifficultyGlobal:
            difficulty->global = edit.preset;
            *difficultyValid = true;
            break;
        case ConfigEditKind::MusicReset:
            config->music.lockTrack = -1;
            config->music.battleTrack = -1;
            config->music.randomizer = false;
            config->music.fadeFrames = 0;
            config->music.playlistCount = 0;
            break;
        case ConfigEditKind::MusicLock:
            config->music.lockTrack = edit.first;
            break;
        case ConfigEditKind::MusicBattle:
            config->music.battleTrack = edit.first;
            break;
        case ConfigEditKind::MusicRandomizer:
            config->music.randomizer = edit.flag;
            break;
        case ConfigEditKind::MusicFade:
            config->music.fadeFrames = edit.first;
            break;
        case ConfigEditKind::ForceRepeat:
            config->force.repeatCount = edit.first;
            break;
        case ConfigEditKind::ForceRoute:
            config->force.hasLast = true;
            config->force.lastField = edit.first;
            config->force.lastGroup = edit.second;
            break;
        case ConfigEditKind::NaturalCapture:
            config->force.hasLast = true;
            config->force.lastField = edit.first;
            config->force.lastGroup = edit.second;
            config->force.lastFormation = edit.third;
            break;
    }
}

static bool ValidateDifficultySnapshot(const DifficultyConfig& config) {
    std::array<char, kMaxJsonBytes + 1> bounded{};
    size_t used = 0;
    return SerializeConfig(config, bounded.data(), bounded.size(), &used).code == ConfigCode::Ok;
}

bool F7_LoadConfig() {
    F7Config loaded = MakeDefaultLegacyConfig();
    DifficultyConfig difficulty = MakeNeutralConfig();
    bool difficultyValid = true;
    SinRam::Config sinRam{};
    bool sinRamValid = true;
    char path[MAX_PATH] = {};
    ResolveConfigPath(path, sizeof(path));
    char json[kMaxJsonBytes + 1] = {};
    size_t length = 0;
    const PersistenceIo persistence{nullptr, &ReadConfigDocument, &WriteConfigDocumentAtomic};
    if (ReadDocument(persistence, path, json, sizeof(json), &length) != PersistenceCode::Ok) {
        // WHY: publish only after filesystem work finishes, so concurrent readers see
        // either the prior immutable generation or one complete neutral replacement.
        F7_ReplaceConfigSnapshot(
            loaded, difficulty, difficultyValid, sinRam, sinRamValid);
        F7_Log("[ffx-hooks] F7: configuration absent or unreadable (%s); neutral defaults remain OFF\n", path);
        return false;
    }
    const ConfigResult parsedResult = ParseConfig(json, length, &difficulty);
    difficultyValid = parsedResult.code == ConfigCode::Ok;
    if (!difficultyValid) {
        difficulty = MakeNeutralConfig();
        F7_Log("[ffx-hooks] F7: Difficulty configuration rejected code=%u offset=%zu; writer remains OFF\n",
               static_cast<unsigned>(parsedResult.code), parsedResult.offset);
    }
    const SinRamConfig::ParseResult sinRamResult =
        SinRamConfig::ParseDocument(json, length, &sinRam);
    sinRamValid = sinRamResult.code == SinRamConfig::Code::Ok;
    if (sinRamValid) sinRam.seeded = true; // migrate player config to automatic area threats
    if (!sinRamValid) {
        sinRam = {};
        F7_Log(
            "[ffx-hooks] F7: S.I.N. configuration rejected code=%u offset=%zu; "
            "only S.I.N. remains OFF/T0\n",
            static_cast<unsigned>(sinRamResult.code), sinRamResult.offset);
    }
    loaded.music.lockTrack = JsonInt(json, "music_lock", -1);
    loaded.music.battleTrack = JsonInt(json, "music_battle", -1);
    loaded.music.randomizer = JsonBool(json, "music_randomizer", false);
    loaded.music.fadeFrames = JsonInt(json, "music_fade", 0);
    JsonIntArray(
        json, "music_playlist", loaded.music.playlist, F7_PLAYLIST_MAX,
        &loaded.music.playlistCount);
    loaded.force.lastField = JsonInt(json, "force_lastField", -1);
    loaded.force.lastGroup = JsonInt(json, "force_lastGroup", -1);
    loaded.force.lastFormation = JsonInt(json, "force_lastFormation", 0);
    loaded.force.hasLast = JsonBool(json, "force_hasLast", false);
    loaded.force.repeatCount = JsonInt(json, "force_repeat", 1);
    if (loaded.force.repeatCount < 1) loaded.force.repeatCount = 1;
    if (loaded.force.repeatCount > 9) loaded.force.repeatCount = 9;
    F7_ReplaceConfigSnapshot(
        loaded, difficulty, difficultyValid, sinRam, sinRamValid);
    const F7ConfigStateSnapshot published = F7_GetConfigSnapshot();
    F7_Log("[ffx-hooks] F7: configuration loaded difficulty=%s hpMul=%d sinRam=%s/T%d valid=%d musicLock=%d battle=%d areas=%d forceLast=%s(%d/%d)\n",
        published.config.diffGlobal.enabled ? "ON" : "OFF",
        published.config.diffGlobal.hpMul, published.sinRam.enabled ? "ON" : "OFF",
        published.sinRam.threatLevel, published.sinRamValid ? 1 : 0,
        published.config.music.lockTrack,
        published.config.music.battleTrack, published.config.areaCount,
        published.config.force.hasLast ? "yes" : "no",
        published.config.force.lastField, published.config.force.lastGroup);
    return true;
}
// ── Config save (atomic: .tmp + MoveFileEx) ──────────────────────────────
static bool AppendConfigJson(char* buffer, size_t capacity, size_t* used, const char* format, ...) {
    if (!buffer || !used || !format || *used >= capacity) return false;
    va_list args;
    va_start(args, format);
    const int written = _vsnprintf_s(
        buffer + *used, capacity - *used, _TRUNCATE, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity - *used) return false;
    *used += static_cast<size_t>(written);
    return true;
}


bool F7_SaveConfig() {
    char path[MAX_PATH] = {};
    ResolveConfigPath(path, sizeof(path));
    char buffer[kMaxJsonBytes + 1] = {};
    size_t used = 0;
    const F7ConfigStateSnapshot snapshot = F7_GetConfigSnapshot();
    if (!snapshot.difficultyValid) {
        F7_Log("[ffx-hooks] F7: invalid Difficulty snapshot cannot be saved\n");
        return false;
    }
    const ConfigResult serialized = SerializeConfig(
        snapshot.difficulty, buffer, sizeof(buffer), &used);
    if (serialized.code != ConfigCode::Ok || used < 2) {
        F7_Log("[ffx-hooks] F7: configuration serialization rejected code=%u\n",
               static_cast<unsigned>(serialized.code));
        return false;
    }

    size_t close = used;
    while (close > 0 && (buffer[close - 1] == '\n' || buffer[close - 1] == '\r' ||
                         buffer[close - 1] == ' ' || buffer[close - 1] == '\t')) --close;
    if (close == 0 || buffer[close - 1] != '}') return false;
    --close;
    if (close > 0 && buffer[close - 1] == '\n') --close;
    used = close;

    char sinRamValue[128] = {};
    const SinRamConfig::SerializeResult sinRamSerialized =
        SinRamConfig::SerializeValue(
            snapshot.sinRam, sinRamValue, sizeof(sinRamValue));
    if (sinRamSerialized.code != SinRamConfig::Code::Ok) {
        F7_Log("[ffx-hooks] F7: S.I.N. configuration serialization rejected code=%u\n",
               static_cast<unsigned>(sinRamSerialized.code));
        return false;
    }

    const F7Config& config = snapshot.config;
    const int playlistCount = config.music.playlistCount < 0
        ? 0 : (config.music.playlistCount > F7_PLAYLIST_MAX
            ? F7_PLAYLIST_MAX : config.music.playlistCount);
    bool complete = AppendConfigJson(buffer, sizeof(buffer), &used,
        ",\n  \"sinRam\":%s,\n  \"music_lock\": %d,\n  \"music_battle\": %d,\n"
        "  \"music_randomizer\": %s,\n  \"music_fade\": %d,\n",
        sinRamValue, config.music.lockTrack, config.music.battleTrack,
        config.music.randomizer ? "true" : "false", config.music.fadeFrames);
    complete = complete && AppendConfigJson(
        buffer, sizeof(buffer), &used, "  \"music_playlist\": [");
    for (int i = 0; complete && i < playlistCount; ++i) {
        complete = AppendConfigJson(
            buffer, sizeof(buffer), &used, "%s%d", i ? "," : "", config.music.playlist[i]);
    }
    complete = complete && AppendConfigJson(buffer, sizeof(buffer), &used, "],\n");
    complete = complete && AppendConfigJson(buffer, sizeof(buffer), &used,
        "  \"force_lastField\": %d,\n  \"force_lastGroup\": %d,\n"
        "  \"force_lastFormation\": %d,\n  \"force_hasLast\": %s,\n"
        "  \"force_repeat\": %d\n}\n",
        config.force.lastField, config.force.lastGroup, config.force.lastFormation,
        config.force.hasLast ? "true" : "false", config.force.repeatCount);
    if (!complete || used > kMaxJsonBytes) {
        F7_Log("[ffx-hooks] F7: configuration exceeds the bounded JSON buffer\n");
        return false;
    }

    const PersistenceIo persistence{nullptr, &ReadConfigDocument, &WriteConfigDocumentAtomic};
    if (WriteDocument(persistence, path, buffer, used) != PersistenceCode::Ok) {
        F7_Log("[ffx-hooks] F7: atomic configuration write failed (error=%lu)\n", GetLastError());
        return false;
    }
    F7_Log("[ffx-hooks] F7: configuration saved (%zu bytes)\n", used);
    return true;
}

void F7_SetDifficultyGlobal(const F7DifficultyPreset& preset) {
    DifficultyConfig validation = F7_GetConfigSnapshot().difficulty;
    validation.global = preset;
    if (!ValidateDifficultySnapshot(validation)) {
        F7_Log("[ffx-hooks] F7: invalid Difficulty UI edit rejected; writer remains OFF\n");
        return;
    }
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::DifficultyGlobal;
    edit.preset = preset;
    F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
}


// Difficulty actor access is obtained only through the validated cdecl accessor; all
// transformations and ownership decisions remain in the portable core.
using ActorAccessorFn = int (__cdecl*)(uint8_t);
using ActorPopulateFn = int (__cdecl*)();

static bool IsWritableActorProtection(DWORD protection) {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    switch (protection & 0xFFu) {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
    }
}

static bool ValidateCommittedWritableSpan(uintptr_t address, size_t length) {
    if (address == 0 || length == 0 || address > UINTPTR_MAX - length) return false;
    MEMORY_BASIC_INFORMATION memory = {};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) !=
        sizeof(memory)) return false;
    if (memory.State != MEM_COMMIT || !IsWritableActorProtection(memory.Protect)) return false;
    const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
    if (address < regionBase) return false;
    const size_t offset = static_cast<size_t>(address - regionBase);
    return offset <= memory.RegionSize && length <= memory.RegionSize - offset;
}

static bool DifficultyReadMemory(
    void*, uintptr_t address, void* output, size_t length) {
    if (!output || length == 0) return false;
    __try {
        memcpy(output, reinterpret_cast<const void*>(address), length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool DifficultyWriteMemory(
    void*, uintptr_t address, const void* input, size_t length) {
    if (!input || !ValidateCommittedWritableSpan(address, length)) return false;
    __try {
        memcpy(reinterpret_cast<void*>(address), input, length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uintptr_t DifficultyGetActorBySlot(void*, uint8_t slot) {
    if (!g_base || !g_trampActorPopulate) return 0;
    const ActorAccessorFn accessor = reinterpret_cast<ActorAccessorFn>(
        g_base + kActorAccessorRva);
    return static_cast<uintptr_t>(static_cast<uint32_t>(accessor(slot)));
}

// WHY (R7-D1): a single collapsed validator counter hid why an entire battle wrote
// nothing (RT2 2026-09-17: actors=4 written=0 rejected=4). The diagnostic validator
// preserves the exact pass/fail contract — writable committed span, readable formation
// field, formation != 0xFFFF — while tallying each rejection cause per generation; the
// generation log then says WHICH check rejected the pointers.
static LONG g_difficultyRejectSpan = 0;
static LONG g_difficultyRejectRead = 0;
static LONG g_difficultyRejectFormation = 0;
static uintptr_t g_lastActorTableBase = 0;

static void DifficultyResetRejectDiagnostics() {
    InterlockedExchange(&g_difficultyRejectSpan, 0);
    InterlockedExchange(&g_difficultyRejectRead, 0);
    InterlockedExchange(&g_difficultyRejectFormation, 0);
}

static bool DifficultyValidateActorDiag(void* context, uintptr_t address, size_t span) {
    if (span != kActorSpan || !ValidateCommittedWritableSpan(address, span)) {
        InterlockedIncrement(&g_difficultyRejectSpan);
        return false;
    }
    uint16_t formation = 0xFFFFu;
    if (!DifficultyReadMemory(
            nullptr, address + kFormationIdOffset, &formation, sizeof(formation))) {
        InterlockedIncrement(&g_difficultyRejectRead);
        return false;
    }
    if (formation == 0xFFFFu) {
        InterlockedIncrement(&g_difficultyRejectFormation);
        return false;
    }
    (void)context;
    return true;
}

static int DifficultyCallPopulateOriginal(void*) {
    const auto original=reinterpret_cast<ActorPopulateFn>(g_trampActorPopulate);
    return original ? original() : 0;
}

static volatile LONG g_difficultyOwnerThread = 0;

static bool DifficultyAutoStatusSignaturesMatch() {
    if (!g_base) return false;
    __try {
        return ValidateAutoStatusEvidence(
            reinterpret_cast<const uint8_t*>(g_base + kAutoStatusRemoveRva), kAutoStatusRemoveBody.size(),
            reinterpret_cast<const uint8_t*>(g_base + kAutoStatusApplyRva), kAutoStatusApplyBody.size());
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool DifficultyReadSceneInitState(uint8_t* stateOut);

static bool DifficultyAutoStatusThreadAdmitted() {
    if (!(InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) != 0 &&
        CanServiceDifficultyRetry(
            static_cast<uint32_t>(InterlockedCompareExchange(&g_difficultyOwnerThread, 0, 0)),
            GetCurrentThreadId(), InterlockedCompareExchange(&g_difficultyInShim, 0, 0) != 0)))
        return false;
    uint8_t phase = 0;
    return DifficultyReadSceneInitState(&phase) && IsPostPopulateBattlePhase(phase);
}

static bool DifficultyRefreshAutoStatus(void*, uintptr_t actor) {
    if (!DifficultyAutoStatusThreadAdmitted() ||
        !DifficultyAutoStatusSignaturesMatch() || !DifficultyValidateActorDiag(nullptr, actor, kActorSpan))
        return false;
    using RefreshFn = void(__cdecl*)(uintptr_t);
    __try {
        // Vanilla uses this same pair to preserve temporary effects while
        // rebuilding permanent/SOS effects; do not recreate its status writer.
        reinterpret_cast<RefreshFn>(g_base + kAutoStatusRemoveRva)(actor);
        reinterpret_cast<RefreshFn>(g_base + kAutoStatusApplyRva)(actor);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static MemoryIo DifficultyMemoryIo() {
    const bool autoReady = DifficultyAutoStatusThreadAdmitted() && DifficultyAutoStatusSignaturesMatch();
    return {nullptr, &DifficultyReadMemory, &DifficultyWriteMemory,
            autoReady ? &DifficultyRefreshAutoStatus : nullptr};
}

static void DifficultyPublishResult(const RuntimeResult& result, size_t pointersRejected) {
    g_difficultyLast = result;
    g_difficultyPointersRejected = pointersRejected;
    InterlockedExchange(&g_appliedCount, static_cast<LONG>(result.actorsSeen));
}

static RuntimeResult DifficultyUpdateCurrentActorsLocked(
    const F7ConfigStateSnapshot& configSnapshot, size_t* pointersRejectedOut) {
    if (!DifficultyAutoStatusThreadAdmitted()) {
        if (pointersRejectedOut) *pointersRejectedOut = 0;
        RuntimeResult unavailable{};
        unavailable.code = ResultCode::NoActors;
        return unavailable;
    }
    std::array<ActorRef, kActorSlots> actors{};
    size_t actorCount = 0;
    size_t rejected = 0;
    for (size_t slot = 0; slot < kActorSlots; ++slot) {
        const uintptr_t address = DifficultyGetActorBySlot(
            nullptr, static_cast<uint8_t>(slot));
        if (address == 0) continue;
        if (!DifficultyValidateActorDiag(nullptr, address, kActorSpan)) {
            ++rejected;
            continue;
        }
        actors[actorCount++] = {address, static_cast<uint8_t>(slot)};
    }
    if (pointersRejectedOut) *pointersRejectedOut = rejected;
    SinRam::RuntimeRequest sinRequest{};
    if (g_sinCurrentRequest.origin == SinRam::EncounterOrigin::Natural &&
        g_sinCurrentRequest.transitionCallerRva == SinNatural::kCallerRva &&
        g_sinCurrentRequest.transitionRequestId != 0u &&
        g_sinCurrentRequest.transitionRequestId == g_sinCurrentRequest.actorRequestId &&
        g_sinCurrentRequest.transitionRequestId == g_difficultyCurrentBattleField.request &&
        g_sinCurrentRequest.transitionGeneration == g_difficultyGeneration) {
        // WHY: Apply Now may replace only the configuration half of evidence already consumed
        // for the current natural battle. It cannot manufacture a token, request, or generation.
        sinRequest = g_sinCurrentRequest;
        // Seeded encounters keep their assignment until the next generation;
        // editing the seed cannot reroll an already engaged monster or reward.
        if (!sinRequest.config.seeded) {
            sinRequest.config = configSnapshot.sinRamValid
                ? configSnapshot.sinRam : SinRam::Config{};
        }
    }
    const auto result=g_difficultyRuntime.UpdateComposed(
        DifficultyMemoryIo(), configSnapshot.difficulty, configSnapshot.difficultyValid,
        g_difficultyCurrentBattleField.fieldRow, sinRequest,
        actorCount == 0 ? nullptr : actors.data(), actorCount);
    SinAi::RefreshLabels(g_difficultyGeneration);
    return result;
}

static void DifficultyCallbackLeave() {
    if (InterlockedDecrement(&g_difficultyCallbacks) == 0 &&
        g_difficultyDrainedEvent) SetEvent(g_difficultyDrainedEvent);
}

static bool DifficultyDrainCallbacks(DWORD timeoutMs) {
    if (!g_difficultyDrainedEvent) {
        return InterlockedCompareExchange(&g_difficultyCallbacks, 0, 0) == 0;
    }
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        // Reset before the authoritative count read. A leave racing this reset sets the event
        // again, while a stale signaled state can no longer satisfy the following wait.
        ResetEvent(g_difficultyDrainedEvent);
        MemoryBarrier();
        if (InterlockedCompareExchange(&g_difficultyCallbacks, 0, 0) == 0) return true;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;
        const DWORD remaining = static_cast<DWORD>(deadline - now);
        if (WaitForSingleObject(g_difficultyDrainedEvent, remaining) != WAIT_OBJECT_0) return false;
    }
}

int F7_DifficultyAppliedCount() {
    return static_cast<int>(InterlockedCompareExchange(&g_appliedCount, 0, 0));
}

bool F7_DifficultyInBattle() {
    return InterlockedCompareExchange(&g_inBattle, 0, 0) != 0;
}

// WHY: Difficulty behavior is armed by its own validated preset, not by the broad F7
// master. Shared battle infrastructure can exist solely for Seymour while the master is
// OFF, and Difficulty must still be able to run — and restore — on that infrastructure.
static bool DifficultyConfigEnablesBehavior(
    const DifficultyConfig& difficulty, bool difficultyValid) {
    if (!difficultyValid) return false;
    if (difficulty.global.enabled) return true;
    if (!difficulty.byArea) return false;
    for (size_t i = 0; i < difficulty.areaCount; ++i) {
        if (difficulty.areas[i].enabled && difficulty.areas[i].preset.enabled) return true;
    }
    return false;
}

bool F7_DifficultyRequestedFromDisk() {
    char path[MAX_PATH] = {};
    ResolveConfigPath(path, sizeof(path));
    char json[kMaxJsonBytes + 1] = {};
    size_t length = 0;
    const PersistenceIo persistence{nullptr, &ReadConfigDocument, &WriteConfigDocumentAtomic};
    if (ReadDocument(persistence, path, json, sizeof(json), &length) != PersistenceCode::Ok)
        return false;
    DifficultyConfig difficulty = MakeNeutralConfig();
    const ConfigResult parsed = ParseConfig(json, length, &difficulty);
    return DifficultyConfigEnablesBehavior(difficulty, parsed.code == ConfigCode::Ok);
}

bool F7_SinRequestedFromDisk() {
    char path[MAX_PATH] = {};
    ResolveConfigPath(path,sizeof(path));
    char json[kMaxJsonBytes+1] = {};size_t length=0;
    const PersistenceIo persistence{nullptr,&ReadConfigDocument,&WriteConfigDocumentAtomic};
    if(ReadDocument(persistence,path,json,sizeof(json),&length)!=PersistenceCode::Ok)return false;
    SinRam::Config config{};
    return SinRamConfig::ParseDocument(json,length,&config).code==SinRamConfig::Code::Ok && config.enabled;
}

// The first capture runs on native population completion. User traces disprove
// both the late menu pump and foreign Present thread as its primary producer.
static volatile LONG g_difficultyRetryArmed = 0;
static ULONGLONG   g_difficultyRetryDeadline = 0;
static uint64_t    g_difficultyRetryGeneration = 0;
static volatile LONG g_difficultyRetryWaitingLogged = 0;
static volatile LONG g_difficultyRetryThreadLogged = 0;
// Protected by g_difficultyRuntimeLock together with the retry generation.
struct DifficultyRetryDiagnostics {
    RuntimeResult result{};
    size_t rejected = 0;
    LONG spanRejected = 0;
    LONG formationRejected = 0;
    LONG readRejected = 0;
    uintptr_t tableBase = 0;
};
static DifficultyRetryDiagnostics g_difficultyRetryLast{};
static constexpr ULONGLONG kDifficultyRetryWindowMs = 8000;

static void DifficultyArmRetry() {
    g_difficultyRetryLast = {};
    g_difficultyRetryLast.result.code = ResultCode::NoActors;
    g_difficultyRetryGeneration = g_difficultyGeneration;
    g_difficultyRetryDeadline = GetTickCount64() + kDifficultyRetryWindowMs;
    InterlockedExchange(&g_difficultyRetryWaitingLogged, 0);
    InterlockedExchange(&g_difficultyRetryThreadLogged, 0);
    InterlockedExchange(&g_difficultyRetryArmed, 1);
    if(InterlockedCompareExchange(&g_difficultyAccepting,0,0)==0)
        InterlockedExchange(&g_difficultyRetryArmed,0);
}

// Keep SEH in trivial leaf helpers. MSVC rejects __try in a function that contains an
// object with a non-trivial destructor; DifficultyRetryTick owns callback accounting via
// RAII, so the unsafe reads must be isolated here without changing their fail-closed values.
static bool DifficultyReadSceneInitState(uint8_t* state) {
    __try {
        *state = *reinterpret_cast<volatile uint8_t*>(g_base + 0x00D2A8E0u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uintptr_t DifficultyReadActorTableBase() {
    uintptr_t tableBase = 0;
    __try {
        tableBase = *reinterpret_cast<volatile uintptr_t*>(g_base + 0x00D34460u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        tableBase = 0;
    }
    return tableBase;
}

static void DifficultyRetryTick() {
    struct RetryCallbackScope {
        RetryCallbackScope() {
            InterlockedIncrement(&g_difficultyCallbacks);
        }
        ~RetryCallbackScope() {
            DifficultyCallbackLeave();
        }
    } callbackScope;
    if (InterlockedCompareExchange(&g_difficultyRetryArmed, 0, 0) == 0) return;
    const uint32_t ownerThread = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_difficultyOwnerThread, 0, 0));
    const uint32_t currentThread = GetCurrentThreadId();
    if (!CanServiceDifficultyRetry(ownerThread, currentThread,
            InterlockedCompareExchange(&g_difficultyInShim, 0, 0) != 0)) {
        if (ownerThread != 0 && ownerThread != currentThread &&
            InterlockedCompareExchange(&g_difficultyRetryThreadLogged, 1, 0) == 0) {
            F7_Log("[ffx-hooks] F7: Difficulty retry waiting for owner thread owner=%u frame=%u\n",
                   ownerThread, currentThread);
        }
        return;
    }
    const F7ConfigStateSnapshot configSnapshot = F7_GetConfigSnapshot();
    // Pair generation/deadline reads with the initializer's publication lock. A stale
    // tick must not disarm a newer battle or capture bytes while its shim is running.
    struct RetryRuntimeScope {
        RetryRuntimeScope() { AcquireSRWLockExclusive(&g_difficultyRuntimeLock); }
        ~RetryRuntimeScope() { ReleaseSRWLockExclusive(&g_difficultyRuntimeLock); }
    } runtimeScope;
    if (InterlockedCompareExchange(&g_difficultyRetryArmed, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) == 0 ||
        !CanServiceDifficultyRetry(static_cast<uint32_t>(
            InterlockedCompareExchange(&g_difficultyOwnerThread, 0, 0)), currentThread,
            InterlockedCompareExchange(&g_difficultyInShim, 0, 0) != 0)) return;
    const ULONGLONG now = GetTickCount64();
    const uint64_t armedGeneration = g_difficultyRetryGeneration;
    const uint64_t currentGeneration = g_difficultyGeneration;
    if (armedGeneration != currentGeneration) {
        // A newer battle superseded this retry; its own shim re-arms if needed.
        InterlockedExchange(&g_difficultyRetryArmed, 0);
        return;
    }
    // 0 is also written by cleanup. Generation plus a proven post-populate phase
    // and matching execution thread must all hold before the first actor capture.
    uint8_t sceneState = 0xFF;
    const bool sceneStateReadable = DifficultyReadSceneInitState(&sceneState);
    const bool populateDone = sceneStateReadable &&
        IsPostPopulateBattlePhase(sceneState);
    if (now >= g_difficultyRetryDeadline) {
        InterlockedExchange(&g_difficultyRetryArmed, 0);
        DifficultyPublishResult(g_difficultyRetryLast.result, g_difficultyRetryLast.rejected);
        F7_Log("[ffx-hooks] F7: Difficulty retry generation=%llu expired result=%s actors=%zu rejected=%zu state=0x%02X readable=%d spanRej=%ld formRej=%ld readRej=%ld table=0x%08X\n",
               static_cast<unsigned long long>(armedGeneration),
               F7_DifficultyResultName(g_difficultyRetryLast.result.code),
               g_difficultyRetryLast.result.actorsSeen, g_difficultyRetryLast.rejected,
               static_cast<unsigned>(sceneState), sceneStateReadable ? 1 : 0,
               g_difficultyRetryLast.spanRejected, g_difficultyRetryLast.formationRejected,
               g_difficultyRetryLast.readRejected,
               static_cast<unsigned>(g_difficultyRetryLast.tableBase));
        return;
    }
    if (!populateDone) return;
    if (InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) == 0 ||
        !g_difficultyDetours.active ||
        InterlockedCompareExchange(&g_difficultyInShim, 0, 0) != 0) return;
    DifficultyResetRejectDiagnostics();
    g_lastActorTableBase = DifficultyReadActorTableBase();
    size_t rejected = 0;
    const RuntimeResult result =
        DifficultyUpdateCurrentActorsLocked(configSnapshot, &rejected);
    const bool terminal = result.actorsSeen != 0 &&
        (result.code == ResultCode::Applied || result.code == ResultCode::Restored);
    const LONG spanRejected = InterlockedCompareExchange(&g_difficultyRejectSpan, 0, 0);
    const LONG formationRejected =
        InterlockedCompareExchange(&g_difficultyRejectFormation, 0, 0);
    const LONG readRejected = InterlockedCompareExchange(&g_difficultyRejectRead, 0, 0);
    const uintptr_t tableBase = g_lastActorTableBase;
    g_difficultyRetryLast.result = result;
    g_difficultyRetryLast.rejected = rejected;
    g_difficultyRetryLast.spanRejected = spanRejected;
    g_difficultyRetryLast.formationRejected = formationRejected;
    g_difficultyRetryLast.readRejected = readRejected;
    g_difficultyRetryLast.tableBase = tableBase;
    if (!terminal) {
        if (result.actorsSeen == 0 &&
            InterlockedCompareExchange(&g_difficultyRetryWaitingLogged, 1, 0) == 0) {
            F7_Log("[ffx-hooks] F7: Difficulty retry generation=%llu waiting for valid actors state=0x%02X rejected=%zu\n",
                   static_cast<unsigned long long>(armedGeneration),
                   static_cast<unsigned>(sceneState), rejected);
        }
        return;
    }
    DifficultyPublishResult(result, rejected);
    InterlockedExchange(&g_difficultyRetryArmed, 0);
    F7_Log("[ffx-hooks] F7: Difficulty retry generation=%llu result=%s actors=%zu written=%zu rejected=%zu state=0x%02X spanRej=%ld formRej=%ld readRej=%ld table=0x%08X auto=%zu (scene-init done)\n",
           static_cast<unsigned long long>(armedGeneration),
           F7_DifficultyResultName(result.code), result.actorsSeen,
           result.fieldsWritten, rejected, static_cast<unsigned>(sceneState),
           spanRejected, formationRejected, readRejected,
           static_cast<unsigned>(tableBase), result.autoStatusRefreshed);
}

void F7_DifficultyApplyNow() {
    InterlockedIncrement(&g_difficultyCallbacks);
    if (InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) == 0 ||
        !g_difficultyDetours.active) {
        AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
        g_difficultyLast = {};
        g_difficultyLast.code = ResultCode::Unavailable;
        ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
        DifficultyCallbackLeave();
        return;
    }
    if (InterlockedCompareExchange(&g_difficultyRetryArmed, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_difficultyInShim,0,0)!=0) {
        F7_Log("[ffx-hooks] F7: Difficulty Apply Now deferred until post-populate (retry armed)\n");
        DifficultyCallbackLeave();
        return;
    }
    const F7ConfigStateSnapshot configSnapshot = F7_GetConfigSnapshot();
    AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
    if (InterlockedCompareExchange(&g_difficultyRetryArmed, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_difficultyInShim,0,0)!=0 ||
        InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) == 0) {
        ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
        DifficultyCallbackLeave();
        return;
    }
    size_t rejected = 0;
    const RuntimeResult result =
        DifficultyUpdateCurrentActorsLocked(configSnapshot, &rejected);
    DifficultyPublishResult(result, rejected);
    ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
    F7_Log("[ffx-hooks] F7: Difficulty Apply Now result=%s actors=%zu written=%zu restored=%zu lost=%zu faults=%zu rejected=%zu auto=%zu\n",
           F7_DifficultyResultName(result.code), result.actorsSeen, result.fieldsWritten,
           result.fieldsRestored, result.ownershipLost, result.faults, rejected, result.autoStatusRefreshed);
    DifficultyCallbackLeave();
}

F7DifficultyRuntimeStatus F7_DifficultyStatus() {
    F7DifficultyRuntimeStatus status{};
    const F7ConfigStateSnapshot configSnapshot = F7_GetConfigSnapshot();
    AcquireSRWLockShared(&g_difficultyRuntimeLock);
    status.configured = configSnapshot.difficulty.global.enabled;
    for (size_t i = 0; !status.configured && configSnapshot.difficulty.byArea &&
                       i < configSnapshot.difficulty.areaCount; ++i) {
        status.configured = configSnapshot.difficulty.areas[i].enabled &&
            configSnapshot.difficulty.areas[i].preset.enabled;
    }
    // WHY: four independent truths. Infrastructure may be installed solely for another
    // consumer; admission is a lifecycle flag; behavior follows only validated Difficulty
    // config. None of them may be conflated with the broad F7 master.
    status.difficultyValid = configSnapshot.difficultyValid;
    status.difficultyBehaviorEnabled = DifficultyConfigEnablesBehavior(
        configSnapshot.difficulty, configSnapshot.difficultyValid);
    status.infrastructureInstalled = g_difficultyDetours.active;
    status.callbackAdmissionOpen =
        InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) != 0;
    status.ownedFieldsPresent = g_difficultyRuntime.HasOwnedOrIndeterminateFields();
    status.infrastructureGate = g_difficultyGate;
    status.last = g_difficultyLast;
    status.generation = g_difficultyGeneration;
    status.currentBattleField = g_difficultyCurrentBattleField.fieldRow;
    status.currentBattleFieldSource = g_difficultyCurrentBattleField.source;
    status.pointersRejected = g_difficultyPointersRejected;
    ReleaseSRWLockShared(&g_difficultyRuntimeLock);
    return status;
}

void F7_SinObserveLocation() {
    if (!g_base || InterlockedCompareExchange(&g_difficultyAccepting,0,0)==0) return;
    uint16_t scene=0xFFFFu;bool field=false;
    __try {
        field=*reinterpret_cast<const uint8_t*>(g_base+RVA_FFX_BATTLE_ACTIVE_FLAG)==0 &&
            *reinterpret_cast<const uint32_t*>(g_base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)!=0;
        scene=*reinterpret_cast<const uint16_t*>(g_base+RVA_FFX_SCENE_STATE_OBJECT+FFX_SCENE_STATE_SCENE_ID_OFFSET);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return;}
    if(!field || scene==0xFFFFu)return;
    AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
    if(InterlockedExchange(&g_inBattle,0)!=0)SinAi::EndEncounter(g_difficultyGeneration);
    if(scene!=g_sinSceneId) {
        if(g_sinSceneId!=0xFFFFu){++g_sinAreaVisit;if(!g_sinAreaVisit)g_sinAreaVisit=1;}
        g_sinSceneId=scene;g_sinAreaField=0;
    }
    ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
}

bool F7_SinAiContext(bool commandsReady,SinAi::Context* out) {
    (void)commandsReady;
    if(!out || InterlockedCompareExchange(&g_difficultyAccepting,0,0)==0 ||
       static_cast<DWORD>(InterlockedCompareExchange(&g_difficultyOwnerThread,0,0))!=GetCurrentThreadId())return false;
    std::uint8_t battleState=0;
    if(!DifficultyReadMemory(nullptr,g_base+RVA_FFX_BATTLE_ACTIVE_FLAG,&battleState,sizeof(battleState)) || !battleState)return false;
    AcquireSRWLockShared(&g_difficultyRuntimeLock);
    const auto request=g_sinCurrentRequest;
    const bool admitted=request.origin==SinRam::EncounterOrigin::Natural && request.transitionCallerRva==SinNatural::kCallerRva &&
        request.transitionRequestId!=0 && request.transitionRequestId==request.actorRequestId &&
        request.transitionRequestId==g_difficultyCurrentBattleField.request && request.transitionGeneration==g_difficultyGeneration &&
        request.config.enabled && request.config.seeded && InterlockedCompareExchange(&g_inBattle,0,0)!=0;
    if(admitted)*out={true,static_cast<std::uint16_t>(request.encounterToken>>16),request.config.seed,request.areaVisit,
        static_cast<SinSpread::Distribution>(request.config.distribution),request.transitionGeneration};
    ReleaseSRWLockShared(&g_difficultyRuntimeLock);return admitted;
}
void F7_SinAiRegistered(unsigned slot,std::uint64_t generation) {
    if(slot>=8 || static_cast<DWORD>(InterlockedCompareExchange(&g_difficultyOwnerThread,0,0))!=GetCurrentThreadId())return;
    AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
    if(generation!=0 && generation==g_difficultyGeneration && g_sinCurrentRequest.transitionGeneration==generation && g_sinCurrentRequest.scriptManaged){
        g_sinCurrentRequest.scriptActorMask=static_cast<std::uint8_t>(g_sinCurrentRequest.scriptActorMask|(1u<<slot));
        DifficultyArmRetry();
    }
    ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
    F7_Log("[ffx-hooks] S.I.N. script registered slot=%u generation=%llu\n",slot,static_cast<unsigned long long>(generation));
}
F7SinRamRuntimeStatus F7_SinRamStatus() {
    F7SinRamRuntimeStatus status{};
    const F7ConfigStateSnapshot configSnapshot = F7_GetConfigSnapshot();
    status.config = configSnapshot.sinRam;
    status.configValid = configSnapshot.sinRamValid;

    AcquireSRWLockShared(&g_difficultyRuntimeLock);
    status.generation = g_difficultyGeneration;
    status.areaVisit = g_sinAreaVisit;
    status.areaField = g_sinAreaField;
    const bool currentNatural =
        g_sinCurrentRequest.origin == SinRam::EncounterOrigin::Natural &&
        g_sinCurrentRequest.transitionCallerRva == SinNatural::kCallerRva &&
        g_sinCurrentRequest.transitionRequestId != 0u &&
        g_sinCurrentRequest.transitionRequestId == g_sinCurrentRequest.actorRequestId &&
        g_sinCurrentRequest.transitionRequestId == g_difficultyCurrentBattleField.request &&
        g_sinCurrentRequest.transitionGeneration == g_difficultyGeneration;
    if (currentNatural) {
        status.request = static_cast<uint32_t>(g_sinCurrentRequest.transitionRequestId);
        status.currentAssignment = InterlockedCompareExchange(&g_inBattle,0,0)!=0;
        status.battleConfig = g_sinCurrentRequest.config;
        if(status.currentAssignment){status.areaField=static_cast<uint16_t>(g_sinCurrentRequest.encounterToken>>16);status.areaVisit=g_sinCurrentRequest.areaVisit;}
    }
    const bool available = g_difficultyDetours.active &&
        InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) != 0;
    ReleaseSRWLockShared(&g_difficultyRuntimeLock);

    if (!configSnapshot.sinRamValid) {
        status.state = F7SinRamState::Invalid;
    } else if (!configSnapshot.sinRam.enabled) {
        status.state = F7SinRamState::Off;
    } else if (!available) {
        status.state = F7SinRamState::Unavailable;
    } else if (currentNatural) {
        status.state = F7SinRamState::CurrentNatural;
    } else {
        status.state = F7SinRamState::WaitNatural;
    }
    return status;
}

const char* F7_SinRamStateName(F7SinRamState state) {
    switch (state) {
        case F7SinRamState::Invalid: return "INVALID";
        case F7SinRamState::Off: return "OFF";
        case F7SinRamState::Unavailable: return "UNAVAILABLE";
        case F7SinRamState::WaitNatural: return "WAIT NATURAL";
        case F7SinRamState::CurrentNatural: return "CURRENT NATURAL";
        default: return "UNAVAILABLE";
    }
}

const char* F7_DifficultyResultName(ResultCode code) {
    switch (code) {
        case ResultCode::Unavailable: return "Unavailable";
        case ResultCode::NoActors: return "NoActors";
        case ResultCode::Applied: return "Applied";
        case ResultCode::Restored: return "Restored";
        case ResultCode::OwnershipLost: return "OwnershipLost";
        case ResultCode::InvalidConfig: return "InvalidConfig";
        case ResultCode::Fault: return "Fault";
        default: return "Unavailable";
    }
}

const char* F7_DifficultyGateName(AdapterGateCode code) {
    switch (code) {
        case AdapterGateCode::Supported: return "Supported";
        case AdapterGateCode::WrongMachine: return "WrongMachine";
        case AdapterGateCode::WrongTimestamp: return "WrongTimestamp";
        case AdapterGateCode::WrongImageSize: return "WrongImageSize";
        case AdapterGateCode::WrongImageBase: return "WrongImageBase";
        case AdapterGateCode::WrongSha256: return "WrongSha256";
        case AdapterGateCode::ResolverSignatureMismatch: return "ResolverSignatureMismatch";
        case AdapterGateCode::InitSceneSignatureMismatch: return "InitSceneSignatureMismatch";
        case AdapterGateCode::InitializerSignatureMismatch: return "InitializerSignatureMismatch";
        case AdapterGateCode::PopulateSignatureMismatch: return "PopulateSignatureMismatch";
        case AdapterGateCode::AutoStatusSignatureMismatch: return "AutoStatusSignatureMismatch";
        case AdapterGateCode::AccessorSignatureMismatch: return "AccessorSignatureMismatch";
        case AdapterGateCode::HookCreateFailed: return "HookCreateFailed";
        case AdapterGateCode::HookEnableFailed: return "HookEnableFailed";
        case AdapterGateCode::HookRollbackFailed: return "HookRollbackFailed";
        case AdapterGateCode::CoordinatorNotReady: return "CoordinatorNotReady";
        case AdapterGateCode::CoordinatorBusy: return "CoordinatorBusy";
        case AdapterGateCode::CoordinatorPoisoned: return "CoordinatorPoisoned";
        case AdapterGateCode::RetainedInert: return "RetainedInert";
        case AdapterGateCode::Installed: return "Installed";
        default: return "InvalidArgument";
    }
}

// ── Music (reuses FFXHooksBlock override + MusicHook battle pending) ────
void F7_MusicApplyLock() {
    if (!g_block) return;
    const F7MusicConfig music = F7_GetConfigSnapshot().config.music;
    if (music.lockTrack >= 0 && music.lockTrack <= 0xB5) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex),
                            music.lockTrack);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
        F7_Log("[ffx-hooks] F7: music lock -> %d\n", music.lockTrack);
    }
}

void F7_MusicClearOverride() {
    if (!g_block) return;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), -1);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    F7_Log("[ffx-hooks] F7: music override cleared\n");
}

const char* F7_StatusName(int i) {
    if (i < 0 || i >= F7_STATUS_COUNT) return "?";
    return F7_STATUS_NAMES[i];
}

void F7_MusicPreview(int track) {
    if (!g_block || track < 0 || track > 0xB5) {
        // FIX 2026-08-02 (RT2): user confirmed the Preview with lock none (-1) and nothing happened.
        F7_Log("[ffx-hooks] F7: music preview invalid track=%d (adjust the track with L/R before Preview)\n", track);
        return;
    }
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), track);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    const unsigned int trigger = (track == 4) ? 7u : 4u;   // chain Lab-proved: override + soundcmd 23 trigger != desired
    int32_t ret = 0;
    bool ok = false;
    ArenaBattleMusicSoundCmdFn fn = GetArenaBattleMusicSoundCmdFn();
    if (fn) {
        // FIX 2026-08-02 (RT2): soundcmd via probe crashed (probe OFF — MMF with 0xCD freed data).
        // SEH: if probe is not alive, preview does NOT crash the game — uses override only.
        __try { ok = fn(trigger, &ret); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
            F7_Log("[ffx-hooks] F7: music preview soundcmd exception; override retained\n");
        }
    }
    F7_Log("[ffx-hooks] F7: music preview track=%d trigger=%u soundcmd=%d ret=%d\n", track, trigger, ok ? 1 : 0, ret);
}

bool F7_ResetMusic() {
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::MusicReset;
    F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
    if (g_block) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), -1);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    }
    SetMusicHookMinFadeFrames(0);
    const bool saved = F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music reset to default (lock=-1 battle=-1 randomizer=off fade=0)\n");
    return saved;
}

void F7_SetMusicLock(int track) {
    // FIX 2026-08-02 (RT2): track 0 = none (0 silences the initial menu / crashes in battle).
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::MusicLock;
    edit.first = (track <= 0) ? -1 : (track > 0xB5 ? 0xB5 : track);
    F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
    F7_MusicApplyLock();
    F7_SaveConfig();
}

void F7_SetMusicBattleTrack(int track) {
    // FIX 2026-08-02 (RT2): track 0 = none (0 crashed on battle entry — auto-apply).
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::MusicBattle;
    edit.first = (track <= 0) ? -1 : (track > 0xB5 ? 0xB5 : track);
    const F7ConfigStateSnapshot snapshot =
        F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music battle -> %d\n", snapshot.config.music.battleTrack);
}

void F7_SetMusicRandomizer(bool on) {
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::MusicRandomizer;
    edit.flag = on;
    F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music randomizer -> %s\n", on ? "ON" : "OFF");
}

void F7_SetMusicFade(int frames) {
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::MusicFade;
    edit.first = (frames < 0) ? 0 : (frames > 600 ? 600 : frames);
    const F7ConfigStateSnapshot snapshot =
        F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
    if (snapshot.config.music.fadeFrames > 0)
        FfxHooks::SetMusicHookMinFadeFrames(snapshot.config.music.fadeFrames);
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music fade -> %d\n", snapshot.config.music.fadeFrames);
}

// Battle start: arms the entry music (MusicHook pending — one-shot consumption,
// expires in 45s, does not leak into the field). Requires the battle-entry hook installed.
static void F7_ApplyMusicBattle() {
    const F7MusicConfig music = F7_GetConfigSnapshot().config.music;
    int target = -1;
    int fade = (music.fadeFrames > 0) ? music.fadeFrames : 90;
    if (music.randomizer && music.playlistCount > 0) {
        const int idx = (int)(GetTickCount() % (unsigned)music.playlistCount);
        target = music.playlist[idx];
        F7_Log("[ffx-hooks] F7: randomizer sorteou faixa %d (playlist[%d])\n", target, idx);
    } else if (music.battleTrack >= 0) {
        target = music.battleTrack;
    } else {
        return;  // no battle track configured — keep vanilla music
    }
    if (target < 0) return;
    // Use MusicLock mechanism (g_block->musicOverrideTrackIndex) — works with ALL interceptors
    if (g_block) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), target);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
        F7_Log("[ffx-hooks] F7: battle music lock -> %d\n", target);
    }
    FfxHooks::SetMusicHookMinFadeFrames(fade);
}


// ── Force Last Battle ─────────────────────────────────────────────────────
int F7_LastEncounterField()  { return F7_GetConfigSnapshot().config.force.lastField; }
int F7_LastEncounterGroup()  { return F7_GetConfigSnapshot().config.force.lastGroup; }
bool F7_HasLastEncounter()   { return F7_GetConfigSnapshot().config.force.hasLast; }

void F7_SetRepeatCount(int n) {
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::ForceRepeat;
    edit.first = (n < 1) ? 1 : (n > 9 ? 9 : n);
    F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
    F7_SaveConfig();
}

// ── Force: scheduled by tick (no Sleep on main thread — fix 2026-08-02) ──
static volatile LONG g_forceRepeatLeft = 0;    // pending forces
static volatile LONG g_forceRepeatDelay = 0;   // frames between forces

void F7_PublishPendingBattleField(int32_t fieldRow, BattleFieldSource source) {
    g_difficultyPendingBattleField.Publish(fieldRow, source);
}

BattleFieldRequest F7_BeginPendingBattleFieldRequest() {
    return g_difficultyPendingBattleField.BeginRequest();
}

bool F7_CommitPendingBattleFieldRequest(
    BattleFieldRequest request, int32_t fieldRow, BattleFieldSource source) {
    return g_difficultyPendingBattleField.Commit(request, fieldRow, source);
}

void F7_CancelPendingBattleFieldRequest(BattleFieldRequest request) {
    g_difficultyPendingBattleField.Cancel(request);
}

void F7_BeginExplicitLaunchCapture() {
    g_explicitCaptureSuppression.Begin();
}

void F7_EndExplicitLaunchCapture() {
    g_explicitCaptureSuppression.End();
}

void F7_ForceLastBattle() {
    const F7ForceConfig force = F7_GetConfigSnapshot().config.force;
    if (!g_base || !force.hasLast) {
        F7_Log("[ffx-hooks] F7: force last - no captured last battle\n");
        return;
    }
    InterlockedExchange(&g_forceRepeatLeft, force.repeatCount);
    InterlockedExchange(&g_forceRepeatDelay, 0);
    F7_Log("[ffx-hooks] F7: force last scheduled field=%d group=%d reps=%d (tick-based)\n",
        force.lastField, force.lastGroup, force.repeatCount);
}

// KEYSTONE B (2026-08-02): force with ARBITRARY field/group — o lever do F7 (o stepper do field).
void F7_ForceFieldBattle(int field, int group) {
    if (!g_base) return;
    ConfigEdit edit{};
    edit.kind = ConfigEditKind::ForceRoute;
    edit.first = field;
    edit.second = group;
    const F7ConfigStateSnapshot snapshot =
        F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
    const int repeat = snapshot.config.force.repeatCount > 0
        ? snapshot.config.force.repeatCount : 1;
    InterlockedExchange(&g_forceRepeatLeft, repeat);
    InterlockedExchange(&g_forceRepeatDelay, 0);
    F7_Log("[ffx-hooks] F7 lever: force field=%d group=%d (tick-based)\n", field, group);
}

// KEYSTONE B (2026-08-02): this F7 lever publishes one validated Difficulty preset and saves it.
// Explicit permille mapping: 0=x1.00 1=x1.15 2=x1.30 3=x1.50 4=x1.75 5=x2.00.
void F7_SetDifficultyLevel(int level) {
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    static const int kHpMulByLevel[6] = { 1000, 1150, 1300, 1500, 1750, 2000 };
    F7DifficultyPreset preset = F7_GetConfigSnapshot().difficulty.global;
    preset.enabled = true;
    preset.hpMul = kHpMulByLevel[level];
    F7_SetDifficultyGlobal(preset);
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7 lever: difficulty level=%d hpMul=%d (applies on next battle start)\n",
        level, preset.hpMul);
}

// called from F7_TickMainThread (1x/frame, main thread): 1 force per tick, ~10 frames between them
static void F7_ForceTick() {
    if (InterlockedCompareExchange(&g_forceRepeatLeft, 0, 0) <= 0) return;
    if (InterlockedCompareExchange(&g_forceRepeatDelay, 0, 0) > 0) {
        InterlockedDecrement(&g_forceRepeatDelay);
        return;
    }
    const LONG left = InterlockedDecrement(&g_forceRepeatLeft);
    if (left < 0) { InterlockedExchange(&g_forceRepeatLeft, 0); return; }
    const F7ForceConfig force = F7_GetConfigSnapshot().config.force;
    typedef int (__cdecl* FnMsBattleEncountExe)(int, int, float);
    FnMsBattleEncountExe fn = (FnMsBattleEncountExe)(g_base + RVA_MS_BATTLE_ENCOUNT);
    const BattleFieldRequest fieldRequest = F7_BeginPendingBattleFieldRequest();
    int ret = 0;
    {
        F7ExplicitLaunchCaptureScope captureScope;
        ret = fn(force.lastField, force.lastGroup, 0.0f);
    }
    if (ret == -1) {
        F7_CommitPendingBattleFieldRequest(
            fieldRequest, force.lastField, BattleFieldSource::Force);
    } else {
        F7_CancelPendingBattleFieldRequest(fieldRequest);
    }
    F7_Log("[ffx-hooks] F7: force tick (%d remaining) field=%d group=%d -> ret=%d\n",
        (int)(left > 0 ? left : 0), force.lastField, force.lastGroup, ret);
    if (left > 0) InterlockedExchange(&g_forceRepeatDelay, 10);   // ~166ms at 60fps between forces
}

static bool ComputeExecutableSha256(std::array<uint8_t, 32>* output) {
    if (!output) return false;
    output->fill(0);
    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) return false;
    HANDLE file = CreateFileA(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    PUCHAR hashObject = nullptr;
    DWORD objectLength = 0;
    DWORD propertyBytes = 0;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    if (ok) {
        ok = BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &propertyBytes, 0));
    }
    if (ok && objectLength != 0) {
        hashObject = static_cast<PUCHAR>(HeapAlloc(GetProcessHeap(), 0, objectLength));
        ok = hashObject != nullptr;
    } else {
        ok = false;
    }
    if (ok) {
        ok = BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm, &hash, hashObject, objectLength, nullptr, 0, 0));
    }
    std::array<uint8_t, 16384> chunk{};
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) break;
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, chunk.data(), read, 0));
    }
    if (ok) {
        ok = BCRYPT_SUCCESS(BCryptFinishHash(
            hash, output->data(), static_cast<ULONG>(output->size()), 0));
    }
    if (hash) BCryptDestroyHash(hash);
    if (hashObject) HeapFree(GetProcessHeap(), 0, hashObject);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!CloseHandle(file)) ok = false;
    if (!ok) output->fill(0);
    return ok;
}

static bool ReadExecutableIdentity(ExecutableIdentity* identity) {
    if (!identity || !g_base) return false;
    *identity = {};
    bool headersValid = false;
    __try {
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
            static_cast<uint32_t>(dos->e_lfanew) < 0x100000u) {
            const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
                g_base + static_cast<uint32_t>(dos->e_lfanew));
            if (nt->Signature == IMAGE_NT_SIGNATURE &&
                nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                identity->machine = nt->FileHeader.Machine;
                identity->timestamp = nt->FileHeader.TimeDateStamp;
                identity->sizeOfImage = nt->OptionalHeader.SizeOfImage;
                identity->imageBase = nt->OptionalHeader.ImageBase;
                headersValid = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        headersValid = false;
    }
    return headersValid && ComputeExecutableSha256(&identity->sha256);
}

static bool DifficultyHookCreate(void*, uintptr_t target, void* detour, void** originalOut) {
    const MH_STATUS status = MH_CreateHook(reinterpret_cast<void*>(target), detour, originalOut);
    if (status != MH_OK)
        F7_Log("[ffx-hooks] F7 hook create failed rva=0x%08X minhook=%d\n",
            static_cast<unsigned>(target - g_base), static_cast<int>(status));
    return status == MH_OK;
}

static bool DifficultyHookRemove(void*, uintptr_t target) {
    const MH_STATUS status = MH_RemoveHook(reinterpret_cast<void*>(target));
    return status == MH_OK || status == MH_ERROR_NOT_CREATED;
}

static DifficultyDetourIo DifficultyCreateIo() {
    return {nullptr, &DifficultyHookCreate, &DifficultyHookRemove};
}

static bool DifficultyCloseAdmissionAndDrain(void*) {
    InterlockedExchange(&g_difficultyAccepting, 0);
    InterlockedExchange(&g_difficultyRetryArmed, 0);
    return DifficultyDrainCallbacks(5000);
}

static bool DifficultyDrainAfterDisable(void*) {
    return DifficultyDrainCallbacks(5000);
}

static MinHookBatch::NeutralizationFence DifficultyDrainFence() {
    return {nullptr, &DifficultyCloseAdmissionAndDrain, &DifficultyDrainAfterDisable};
}

using ResolveEncounterFn = unsigned __int8* (__cdecl*)(uint32_t, int*, int*, int*);
typedef int (__cdecl* InitSceneFn)();

static unsigned __int8* __cdecl ResolveEncounter_Shim(uint32_t a1, int* a2, int* a3, int* a4);
static int __cdecl InitScene_Shim();
static int __cdecl ActorPopulate_Shim();

static std::array<DifficultyDetourSpec, kDifficultyDetourCount>
DifficultyDetourSpecs() {
    return {{
        {g_base + kResolveEncounterRva,
         reinterpret_cast<void*>(&ResolveEncounter_Shim), &g_trampResolve},
        {g_base + kInitSystemSceneRva,
         reinterpret_cast<void*>(&InitScene_Shim), &g_trampScene},
        {g_base + kActorPopulateRva,
         reinterpret_cast<void*>(&ActorPopulate_Shim), &g_trampActorPopulate},
    }};
}

static bool DifficultyHasCreatedTargets() {
    for (const bool created : g_difficultyDetours.created) {
        if (created) return true;
    }
    return false;
}

using PositionReadFn = int (__cdecl*)(int, uintptr_t, int, int, int, float*);
static int __cdecl PositionRead_Shim(int setMode, uintptr_t actor, int area, int role, int slot, float* output) {
    InterlockedIncrement(&g_difficultyCallbacks);
    InterlockedIncrement(&g_positionCallbacks);
    struct Leave { ~Leave(){ InterlockedDecrement(&g_positionCallbacks); DifficultyCallbackLeave(); } } leave;
    const auto original = reinterpret_cast<PositionReadFn>(g_trampPositionRead);
    const int result = original ? original(setMode, actor, area, role, slot, output) : -1;
    if (InterlockedCompareExchange(&g_difficultyAccepting,0,0)!=0 &&
        InterlockedCompareExchange(&g_positionHookAccepting,0,0)!=0 &&
        area==0 && role==5) {
        const auto* flag = FindF8Flag("arena_plus.compose_f7");
        const auto status = flag ? GetF8RuntimeStatus(*flag) : F8RuntimeStatus{};
        if (status.availability == F8RuntimeAvailability::Available && status.hasAppliedValue && status.appliedValue) {
            if (setMode==0)
                CustomMixUltra::Runtime::ProductionPositionRead(result,setMode,actor,area,role,slot,output);
            else if (result==0 && output)
                CustomMixUltra::Runtime::ProductionPositionSet(actor,slot);
        } else
            CustomMixUltra::Runtime::ProductionClearPositionBattle();
    }
    return result;
}

static bool PositionReadSignatureMatches() {
    __try {
        return ArenaPositions::AccessorSignatureMatches(
            reinterpret_cast<const uint8_t*>(g_base + ArenaPositions::kAccessorRva),
            ArenaPositions::kAccessorSignatureBytes, g_base);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool PositionReadDrain(void*) {
    const ULONGLONG deadline = GetTickCount64() + 5000u;
    while (InterlockedCompareExchange(&g_positionCallbacks,0,0)!=0) {
        if (GetTickCount64() >= deadline) return false;
        Sleep(1); // Explicit normal-context install/retire fence only.
    }
    return true;
}
static bool PositionReadCloseAndDrain(void* context) {
    InterlockedExchange(&g_positionHookAccepting,0);
    return PositionReadDrain(context);
}
static MinHookBatch::NeutralizationFence PositionReadFence() {
    return {nullptr,&PositionReadCloseAndDrain,&PositionReadDrain};
}

static bool InstallPositionReadHook() {
    if (g_positionHookCreated) {
        const bool ready = InterlockedCompareExchange(&g_positionHookAccepting, 0, 0) != 0;
        F7_Log("[ffx-hooks] CustomMix position reader install=RETAINED ready=%d\n", ready ? 1 : 0);
        return ready;
    }
    // The shared owner has already validated the complete executable identity.
    if (g_difficultyGate != AdapterGateCode::Installed) {
        F7_Log("[ffx-hooks] CustomMix position reader install=FAILED stage=shared-runtime gate=%s\n",
            F7_DifficultyGateName(g_difficultyGate));
        return false;
    }
    if (!PositionReadSignatureMatches()) {
        F7_Log("[ffx-hooks] CustomMix position reader install=FAILED stage=signature rva=0x003AC000\n");
        return false;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCSTR>(&InstallPositionReadHook), &pinned)) {
        F7_Log("[ffx-hooks] CustomMix position reader install=FAILED stage=pin error=%lu\n", GetLastError());
        return false;
    }
    const uintptr_t target = g_base + ArenaPositions::kAccessorRva;
    if (!DifficultyHookCreate(nullptr, target,
        reinterpret_cast<void*>(&PositionRead_Shim), &g_trampPositionRead)) {
        F7_Log("[ffx-hooks] CustomMix position reader install=FAILED stage=create rva=0x003AC000\n");
        return false;
    }
    g_positionHookCreated = true;
    const auto report = MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::ArenaPositions,&target,1u,PositionReadFence());
    if (report.result != MinHookBatch::BatchResult::Applied) {
        F7_Log("[ffx-hooks] CustomMix position reader install=FAILED stage=batch result=%u primary=%u neutralization=%u mayHaveRun=%d\n",
            static_cast<unsigned>(report.result), static_cast<unsigned>(report.primaryFailure),
            static_cast<unsigned>(report.neutralizationFailure), report.mayHaveRun ? 1 : 0);
        return false;
    }
    InterlockedExchange(&g_positionHookAccepting,1);
    F7_Log("[ffx-hooks] CustomMix position reader install=READY rva=0x003AC000 owner=ArenaPositions\n");
    return true;
}

static bool PrepareNormalBattlePrograms() {
    try {
        std::string error;
        const bool ready=ArenaBattleProgram::LoadForCurrentModule(&error);
        F7_Log("[ffx-hooks] CustomMix normal profiles ready=%d detail=%s\n",
            ready?1:0,ready?"verified main/camera only":error.c_str());
        return ready;
    } catch(...) {
        F7_Log("[ffx-hooks] CustomMix normal profiles unavailable: allocation/read failure\n");
        return false;
    }
}

static AdapterGateCode DifficultyGateFromDetourCode(DifficultyDetourCode code) {
    switch (code) {
        case DifficultyDetourCode::Installed: return AdapterGateCode::Installed;
        case DifficultyDetourCode::HookCreateFailed: return AdapterGateCode::HookCreateFailed;
        case DifficultyDetourCode::HookRollbackFailed:
        case DifficultyDetourCode::TeardownRetryRequired:
            return AdapterGateCode::HookRollbackFailed;
        case DifficultyDetourCode::EnableFailedRetained:
            return AdapterGateCode::HookEnableFailed;
        case DifficultyDetourCode::CoordinatorNotReady:
            return AdapterGateCode::CoordinatorNotReady;
        case DifficultyDetourCode::CoordinatorBusy:
            return AdapterGateCode::CoordinatorBusy;
        case DifficultyDetourCode::CoordinatorPoisoned:
            return AdapterGateCode::CoordinatorPoisoned;
        case DifficultyDetourCode::RetainedInert:
            return AdapterGateCode::RetainedInert;
        default:
            return AdapterGateCode::InvalidArgument;
    }
}

static uint64_t NextDifficultyGeneration(uint64_t current) {
    // WHY: zero is reserved as missing evidence. Wrap remains nonzero, while the strictly-new
    // S.I.N. check below deliberately rejects wrapped evidence against its prior watermark.
    return current == UINT64_MAX ? 1u : current + 1u;
}

bool F7_SinObserveNaturalEncounter(const SinNatural::Evidence& evidence) {
    if(!SinNatural::Admitted(evidence) || g_explicitCaptureSuppression.Active() ||
        InterlockedCompareExchange(&g_difficultyAccepting,0,0)==0)return false;
    const auto config=F7_GetConfigSnapshot();
    const bool wanted=config.sinRamValid&&config.sinRam.enabled;
    const bool master=F7_IsEnabled();
    if(!wanted && !master && !DifficultyConfigEnablesBehavior(config.difficulty,config.difficultyValid))return false;
    AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
    const auto request=F7_BeginPendingBattleFieldRequest();
    const auto generation=NextDifficultyGeneration(g_difficultyGeneration);
    const std::uint32_t token=(static_cast<std::uint32_t>(evidence.field)<<16)|evidence.formation;
    SinRam::StageCode stage=SinRam::StageCode::InvalidTicket;
    if(wanted && generation!=0){
        SinRam::TransitionTicket ticket{};ticket.request=request;ticket.encounterToken=token;
        ticket.callerRva=SinNatural::kCallerRva;ticket.transitionGeneration=generation;
        stage=g_sinTransitionPublication.Stage(ticket);
    }
    const bool committed=F7_CommitPendingBattleFieldRequest(request,evidence.nativeFieldRow,BattleFieldSource::Natural);
    if(!committed){g_sinTransitionPublication.Cancel(request);F7_CancelPendingBattleFieldRequest(request);}
    ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
    if(master && committed){ConfigEdit edit{};edit.kind=ConfigEditKind::NaturalCapture;edit.first=evidence.nativeFieldRow;
        edit.second=evidence.group;edit.third=evidence.formation;F7_UpdateConfigSnapshot(&ApplyConfigEdit,&edit);}
    F7_Log("[ffx-hooks] S.I.N. random encounter field=%d row=%u group=%d formation=%u request=%u generation=%llu stage=%u committed=%d\n",
        evidence.field,evidence.nativeFieldRow,evidence.group,evidence.formation,request,
        static_cast<unsigned long long>(generation),static_cast<unsigned>(stage),committed?1:0);
    return committed;
}

static void DifficultyArmBattleGeneration() {
    const F7ConfigStateSnapshot configSnapshot = F7_GetConfigSnapshot();
    const bool f7Master = F7_IsEnabled();
    const bool sinWanted = configSnapshot.sinRamValid && configSnapshot.sinRam.enabled;
    AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
    if(InterlockedCompareExchange(&g_difficultyAccepting,0,0)==0) {
        ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);return;
    }
    // WHY: a prior battle must become unusable before either one-shot publication is consumed.
    // Every mismatch, corrupt ticket, or stale generation therefore fails closed to neutral.
    g_sinCurrentRequest = {};
    const BattleFieldTicket battleField = g_difficultyPendingBattleField.Consume();
    const SinRam::ConsumeResult sinPublication =
        g_sinTransitionPublication.Consume(battleField.request);
    g_difficultyCurrentBattleField = battleField;
    const uint64_t generation = NextDifficultyGeneration(g_difficultyGeneration);
    g_difficultyGeneration = generation;
    InterlockedExchange(&g_difficultyOwnerThread, static_cast<LONG>(GetCurrentThreadId()));
    if ((f7Master || sinWanted) && sinPublication.code == SinRam::ConsumeCode::Matched &&
        battleField.source == BattleFieldSource::Natural &&
        battleField.request != 0u && generation != 0u &&
        sinPublication.ticket.callerRva == SinNatural::kCallerRva &&
        sinPublication.ticket.transitionGeneration == generation &&
        sinPublication.ticket.transitionGeneration > g_sinLastAcceptedGeneration) {
        SinRam::RuntimeRequest request{};
        request.scriptManaged=true;request.scriptActorMask=0;
        request.config = configSnapshot.sinRamValid
            ? configSnapshot.sinRam
            : SinRam::Config{};
        request.encounterToken = sinPublication.ticket.encounterToken;
        request.nativeFieldRow = battleField.fieldRow;
        request.areaVisit = g_sinAreaVisit;
        g_sinAreaField = static_cast<uint16_t>(request.encounterToken>>16);
        request.origin = SinRam::EncounterOrigin::Natural;
        request.transitionCallerRva = sinPublication.ticket.callerRva;
        request.transitionRequestId = battleField.request;
        request.actorRequestId = battleField.request;
        request.transitionGeneration = sinPublication.ticket.transitionGeneration;
        g_sinCurrentRequest = request;
        g_sinLastAcceptedGeneration = sinPublication.ticket.transitionGeneration;
    }
    // Vanilla InitScene has already returned 8/10 on the admitted battle caller.
    // Its initializer ran exactly once; no actor bytes are consumed until population.
    g_difficultyRuntime.BeginGeneration(generation);
    RuntimeResult waiting{};
    waiting.code = ResultCode::NoActors;
    DifficultyPublishResult(waiting, 0);
    InterlockedExchange(&g_inBattle, 1);
    // R8-D1 v4: the shim never captures actor bytes. Arm the bounded post-populate retry
    // while the generation lock is still held so no concurrent producer can observe a
    // partially published generation between BeginGeneration and the retry publication.
    DifficultyArmRetry();
    ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
    F7_Log("[ffx-hooks] F7: Difficulty generation=%llu armed for post-populate apply ownerThread=%u\n",
           static_cast<unsigned long long>(g_difficultyGeneration), GetCurrentThreadId());
}

static bool DifficultyPopulateAdmitted(void*) {
    return InterlockedCompareExchange(&g_difficultyAccepting,0,0)!=0 &&
        InterlockedCompareExchange(&g_difficultyRetryArmed,0,0)!=0 &&
        CanServiceDifficultyRetry(static_cast<uint32_t>(InterlockedCompareExchange(&g_difficultyOwnerThread,0,0)),
            GetCurrentThreadId(),InterlockedCompareExchange(&g_difficultyInShim,0,0)!=0);
}
static bool DifficultyPopulateReadPhase(void*,uint8_t* phase) {return DifficultyReadSceneInitState(phase);}
static void DifficultyPopulateService(void*) {DifficultyRetryTick();}
static int DifficultyPopulateOriginalAndLeave(void*) {
    const int result=DifficultyCallPopulateOriginal(nullptr);
    InterlockedExchange(&g_difficultyInShim,0);
    return result;
}
static int __cdecl ActorPopulate_Shim() {
    InterlockedIncrement(&g_difficultyCallbacks);
    struct Leave {~Leave(){DifficultyCallbackLeave();}} leave;
    if(InterlockedCompareExchange(&g_difficultyAccepting,0,0)==0 ||
       InterlockedCompareExchange(&g_difficultyInShim,1,0)!=0)
        return DifficultyCallPopulateOriginal(nullptr);
    const PopulateIo io{nullptr,DifficultyPopulateOriginalAndLeave,DifficultyPopulateAdmitted,
        DifficultyPopulateReadPhase,DifficultyPopulateService};
    return RunPopulatePostOriginal(io);
}
static bool DifficultyPopulateSignatureMatches() {
    __try {return ValidatePopulateEvidence(reinterpret_cast<const uint8_t*>(g_base+kActorPopulateRva),
        kActorPopulatePreferredBody.size(),g_base);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

static bool InstallDifficultyHook() {
    ExecutableIdentity identity{};
    if (!ReadExecutableIdentity(&identity)) {
        g_difficultyGate = AdapterGateCode::InvalidArgument;
        return false;
    }
    const AdapterEvidence evidence{
        &identity,
        g_base,
        reinterpret_cast<const uint8_t*>(g_base + kResolveEncounterRva),
        kResolveEncounterPrefix.size(),
        reinterpret_cast<const uint8_t*>(g_base + kInitSystemSceneRva),
        kInitSystemScenePreferredPrefix.size(),
        reinterpret_cast<const uint8_t*>(g_base + kActorInitializerRva),
        kActorInitializerPrefix.size(),
        reinterpret_cast<const uint8_t*>(g_base + kActorAccessorRva),
        kActorAccessorSignature.size(),
    };
    // WHY: disk identity plus all four exact loaded-memory sequences are checked before any
    // target is created. A matching SHA cannot prove that the mapped code was not patched, and
    // the shared coordinator cannot compensate for a wrong prologue or accessor width.
    g_difficultyGate = ValidateAdapterEvidence(evidence);
    if (g_difficultyGate != AdapterGateCode::Supported) return false;
    if(!DifficultyPopulateSignatureMatches()) {g_difficultyGate=AdapterGateCode::PopulateSignatureMismatch;return false;}
    if (!DifficultyAutoStatusSignaturesMatch()) {
        g_difficultyGate = AdapterGateCode::AutoStatusSignatureMismatch;
        return false;
    }

    const MinHookBatch::InitializationResult initialized =
        MinHookBatch::EnsureProcessInitialized();
    if (initialized != MinHookBatch::InitializationResult::Ready) {
        g_difficultyGate = initialized == MinHookBatch::InitializationResult::Busy
            ? AdapterGateCode::CoordinatorBusy
            : initialized == MinHookBatch::InitializationResult::Poisoned ||
                      initialized == MinHookBatch::InitializationResult::FailedPoisoned
                ? AdapterGateCode::CoordinatorPoisoned
                : AdapterGateCode::CoordinatorNotReady;
        return false;
    }

    if (!g_difficultyDrainedEvent && g_difficultyDetours.mayHaveRun) {
        // Reachable callbacks require the original process-lifetime event. Replacing a missing
        // one would hide a lifecycle invariant violation rather than repair it.
        g_difficultyGate = AdapterGateCode::CoordinatorPoisoned;
        return false;
    }
    if (!g_difficultyDrainedEvent) {
        g_difficultyDrainedEvent = CreateEventA(nullptr, TRUE, TRUE, nullptr);
    }
    if (!g_difficultyDrainedEvent) {
        g_difficultyGate = AdapterGateCode::InvalidArgument;
        return false;
    }

    F7_RequestStop();
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount> specs =
        DifficultyDetourSpecs();
    const DifficultyDetourResult installed = InstallDifficultyDetours(
        DifficultyCreateIo(), &MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(), DifficultyDrainFence(), specs,
        &g_difficultyDetours);
    g_difficultyGate = DifficultyGateFromDetourCode(installed.code);
    if (installed.code != DifficultyDetourCode::Installed) {
        if (!g_difficultyDetours.mayHaveRun && !DifficultyHasCreatedTargets()) {
            // All three creates were rolled back before the first process-global Apply boundary.
            // Only this proof permits releasing outputs that no callback could have observed.
            CloseHandle(g_difficultyDrainedEvent);
            g_difficultyDrainedEvent = nullptr;
            g_trampResolve = nullptr;
            g_trampScene = nullptr;
            g_trampActorPopulate = nullptr;
        } else {
            F7_Log(
                "[ffx-hooks] F7: Difficulty batch retained after install failure "
                "(code=%u mayHaveRun=%d poisoned=%d); owner-thread retry or restart required\n",
                static_cast<unsigned>(installed.code),
                g_difficultyDetours.mayHaveRun ? 1 : 0,
                g_difficultyDetours.coordinatorPoisoned ? 1 : 0);
        }
        return false;
    }

    // The one Apply succeeded for all three exact targets. Opening admission afterward prevents
    // a callback from observing only part of the batch or unpublished trampoline outputs.
    InterlockedExchange(&g_difficultyAccepting, 1);
    return true;
}

// ── Hooks (MinHook — read-only / post-original) ───────────────────────────
static unsigned __int8* __cdecl ResolveEncounter_Shim(uint32_t a1, int* a2, int* a3, int* a4) {
    InterlockedIncrement(&g_difficultyCallbacks);
    const F7ConfigStateSnapshot configSnapshot = F7_GetConfigSnapshot();
    const bool f7Master = F7_IsEnabled();
    const bool sinWanted = configSnapshot.sinRamValid && configSnapshot.sinRam.enabled;
    // WHY: the Difficulty battle-field ticket is needed whenever a validated preset is
    // armed — the broad F7 master gates Force capture and S.I.N. evidence only.
    const bool difficultyActive = DifficultyConfigEnablesBehavior(
        configSnapshot.difficulty, configSnapshot.difficultyValid);
    const bool accepting =
        InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) != 0 &&
        (f7Master || difficultyActive || sinWanted);
    const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t rawReturnRva = g_base && returnAddress >= g_base
        ? returnAddress - g_base
        : 0u;
    const uint32_t returnRva = rawReturnRva <= UINT32_MAX
        ? static_cast<uint32_t>(rawReturnRva)
        : 0u;
    const ResolveEncounterFn original =
        reinterpret_cast<ResolveEncounterFn>(g_trampResolve);
    unsigned __int8* ret = original ? original(a1, a2, a3, a4) : nullptr;
    if (accepting && !g_explicitCaptureSuppression.Active() && ret && a2 && a3) {
        if (f7Master) {
            ConfigEdit edit{};
            edit.kind = ConfigEditKind::NaturalCapture;
            edit.first = *a2;
            edit.second = *a3;
            edit.third = 0;
            F7_UpdateConfigSnapshot(&ApplyConfigEdit, &edit);
        }

        AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
        const BattleFieldRequest request = F7_BeginPendingBattleFieldRequest();
        const uint64_t generation = NextDifficultyGeneration(g_difficultyGeneration);
        SinRam::StageCode stage = SinRam::StageCode::InvalidTicket;
        // Label/script lookups still support existing field capture, but only
        // the verified walking-step callback may publish natural curse evidence.
        // WHY: S.I.N. publication is auxiliary evidence. Its bounded Stage outcome can never
        // veto the established Difficulty field ticket for the same natural transition.
        const bool committed = F7_CommitPendingBattleFieldRequest(
            request, *a2, BattleFieldSource::Natural);
        if (!committed) {
            g_sinTransitionPublication.Cancel(request);
            F7_CancelPendingBattleFieldRequest(request);
        }
        ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
        F7_Log(
            "[ffx-hooks] F7: last encounter captured field=%d group=%d caller=0x%08X "
            "request=%u generation=%llu sinStage=%u commit=%d\n",
            *a2, *a3, returnRva, request,
            static_cast<unsigned long long>(generation),
            static_cast<unsigned>(stage), committed ? 1 : 0);
    }
    DifficultyCallbackLeave();
    return ret;
}

static int SharedBattleCallInitSceneOriginal(void*) {
    const InitSceneFn original = reinterpret_cast<InitSceneFn>(g_trampScene);
    return original ? original() : 0;
}

struct DifficultySceneOriginalContext {bool battleCaller=false;};
static int DifficultyCallInitSceneOriginal(void* context) {
    const auto* scene=static_cast<const DifficultySceneOriginalContext*>(context);
    if(!scene || !scene->battleCaller || InterlockedCompareExchange(&g_difficultyAccepting,0,0)==0 ||
       InterlockedCompareExchange(&g_difficultyInShim,1,0)!=0)
        return SharedBattleCallInitSceneOriginal(nullptr);
    InterlockedExchange(&g_difficultyRetryArmed,0);
    InterlockedExchange(&g_difficultyOwnerThread,0);
    const int result=SharedBattleCallInitSceneOriginal(nullptr);
    if(IsInitializedBattleSceneReturn(result))DifficultyArmBattleGeneration();
    InterlockedExchange(&g_difficultyInShim,0);
    return result;
}

struct CustomMixSharedBattleContext {
    uintptr_t returnRva = 0u;
    SharedBattleRuntime::OriginalIo original{};
    SharedBattleRuntime::ReservedSeamIo reserved{};
    SharedBattleRuntime::ComposerIo composer{};
    SharedBattleRuntime::InitSceneRunResult run{};
};

static bool RunSharedBattleInsideCustomMix(void* rawContext, int* originalResultOut) {
    if (!rawContext || !originalResultOut) return false;
    CustomMixSharedBattleContext& context =
        *static_cast<CustomMixSharedBattleContext*>(rawContext);
    context.run = SharedBattleRuntime::RunInitScene(
        context.returnRva, context.original, context.reserved, context.composer);
    *originalResultOut = context.run.originalResult;
    // WHY: the transaction callback reports invocation success independently of
    // the game's int return value. A legitimate zero result must remain accepted.
    return true;
}

static int __cdecl InitScene_Shim() {
    InterlockedIncrement(&g_difficultyCallbacks);
    if (InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) == 0) {
        const int result = SharedBattleCallInitSceneOriginal(nullptr);
        DifficultyCallbackLeave();
        return result;
    }

    const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t returnRva = g_base && returnAddress >= g_base
        ? returnAddress - g_base
        : 0u;
    const SharedBattleRuntime::ComposerIo* registered = g_initSceneComposer.Load();
    const SharedBattleRuntime::ComposerIo composer = registered
        ? *registered
        : SharedBattleRuntime::ComposerIo{};
    const SharedBattleRuntime::InitSceneCaller caller =
        SharedBattleRuntime::ClassifyInitSceneCaller(returnRva);
    DifficultySceneOriginalContext originalContext{caller==SharedBattleRuntime::InitSceneCaller::BattleState};
    const SharedBattleRuntime::OriginalIo original{
        &originalContext, &DifficultyCallInitSceneOriginal};
    const SharedBattleRuntime::ReservedSeamIo reserved{};
    SharedBattleRuntime::InitSceneRunResult run{};
    if (caller == SharedBattleRuntime::InitSceneCaller::BattleState) {
        // The admitted native InitScene return arms the new generation and flips this back ON.
        InterlockedExchange(&g_inBattle, 0);
        CustomMixSharedBattleContext context{
            returnRva, original, reserved, composer, {}};
        const CustomMixUltra::Runtime::NestedInitSceneIo nested{
            &context, &RunSharedBattleInsideCustomMix};
        // Recheck at the consumer: OFF cancels even if no menu tick ran after launch.
        if (!F7_ArenaMixEnabled() && CustomMixUltra::Runtime::ProductionStatus().code ==
                CustomMixUltra::Runtime::StatusCode::Queued)
            CustomMixUltra::Runtime::ProductionCancel(CustomMixUltra::Runtime::CancelReason::Cancel);
        // KEY: ExecuteTransaction wraps the complete shared run. Seymour's before,
        // guarded vanilla, and after phases therefore all see the temporary carrier.
        const CustomMixUltra::Runtime::BattleCompositionOutcome composed =
            CustomMixUltra::Runtime::RunProductionBattle(nested, GetTickCount64());
        run = context.run;
        if (composed.carrierProbeAttempted)
            F7_Log("[ffx-hooks] CustomMix transaction result=%u positions=%u restored=%d battlefield=%u sceneryOwned=%d normalProgram=%d source=%s\n",
                static_cast<unsigned>(composed.transaction.result),
                static_cast<unsigned>(composed.transaction.positionSlotsApplied),
                composed.transaction.restored ? 1 : 0,
                static_cast<unsigned>(composed.transaction.battlefield.applied),
                composed.transaction.battlefield.active ? 1 : 0,
                composed.normalProgramUsed?1:0,composed.programSource?composed.programSource:"none");
        if (!composed.originalResultAvailable) {
            F7_Log(
                "[ffx-hooks] F7: CustomMix nested InitScene result unavailable; "
                "shared runtime returned fallback=%d transaction=%u\n",
                run.originalResult,
                static_cast<unsigned>(composed.transaction.result));
        }
    } else {
        CustomMixUltra::Runtime::ProductionClearPositionBattle();
        if (caller == SharedBattleRuntime::InitSceneCaller::Bootstrap ||
            caller == SharedBattleRuntime::InitSceneCaller::SphereGridStartup)
            CustomMixUltra::Runtime::ProductionLeaveBattle();
        // Bootstrap, Sphere Grid startup, and unknown callers stay vanilla-only and
        // never compose a borrowed carrier. Known transitions may release the preceding
        // Mix's still-owned scenery; unknown callers gain no new write authority.
        run = SharedBattleRuntime::RunInitScene(
            returnRva, original, reserved, composer);
    }
    // KEY: this is the one process-wide owner of InitScene RVA 0x00383ED0. Seymour is a
    // consumer selected only by return RVA 0x0038321C; bootstrap 0x00381C76 and the startup /
    // Sphere Grid path 0x003821CF execute vanilla exactly once without behavioral composition.
    if (run.caller == SharedBattleRuntime::InitSceneCaller::BattleState) {
        if (F7_IsEnabled()) F7_ApplyMusicBattle();
    }
    DifficultyCallbackLeave();
    return run.originalResult;
}

bool F7_ArenaMixEnabled() {
    const F8FlagSpec* flag = FindF8Flag("arena_plus.compose_f7");
    return flag && ResolveF8Flag(*flag).value;
}

void F7_TickMainThread() {
    if (!F7_ArenaMixEnabled() && CustomMixUltra::Runtime::ProductionStatus().code ==
            CustomMixUltra::Runtime::StatusCode::Queued)
        CustomMixUltra::Runtime::ProductionCancel(CustomMixUltra::Runtime::CancelReason::Cancel);
    CustomMixUltra::Runtime::ProductionTick(GetTickCount64());
    // WHY (R8-D1): the Difficulty retry must drain even when the broad F7 master is OFF —
    // Difficulty owns its behavior independently (R2 admission split), so this call
    // deliberately precedes the master gate below.
    DifficultyRetryTick();
    if (!F7_IsEnabled()) return;
    F7_ForceTick();
}


// ── Install / Remove ──────────────────────────────────────────────────────
bool F7_InstallHooks(uintptr_t base, FFXHooksBlock* block, void (*log)(const char*),
                     bool sharedBattleRuntimeRequested, bool arenaMixRequested) {
    if (!base) return false;
    if (DifficultyHasCreatedTargets() && g_base && g_base != base) {
        if (log) log("[ffx-hooks] F7: retained Difficulty targets belong to another module base\n");
        return false;
    }
    g_base = base;
    g_block = block;
    g_log = log;

    // Saved preferences belong to the UI even when every gameplay feature is
    // OFF. Loading must precede admission: otherwise a saved S.I.N. seed resets
    // to neutral defaults, and Save can serialize an unhydrated sibling preset.
    F7_LoadConfig();

    // Gate: f7.inlive (INI) OR f7_inlive.flag OR FFXHOOKS_ENABLE_F7=1 (OFF by default).
    // WHY: an on-disk Difficulty preset also requests the shared batch by itself — the
    // broad master only gates behavior inside the callbacks, never infrastructure.
    const bool flagGate = FfxHooks::Config::CheckEnabled("f7.inlive", "FFXHOOKS_ENABLE_F7", "f7_inlive.flag", false);
    const bool difficultyRequested = F7_DifficultyRequestedFromDisk();
    const bool sinRequested = F7_SinRequestedFromDisk();
    if (!flagGate && !difficultyRequested && !sinRequested && !sharedBattleRuntimeRequested && !arenaMixRequested) {
        F7_Log("[ffx-hooks] F7: disabled (create modules\\config\\f7_inlive.flag or FFXHOOKS_ENABLE_F7=1)\n");
        return false;
    }
    if (g_difficultyDetours.active) {
        InterlockedExchange(&g_enabled, flagGate ? 1 : 0);
        const bool positionReaderReady = (flagGate || arenaMixRequested) && InstallPositionReadHook();
        const bool programReady=positionReaderReady&&PrepareNormalBattlePrograms();
        CustomMixUltra::Runtime::StartProduction(
            g_base,
            programReady && g_difficultyGate == AdapterGateCode::Installed &&
                InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) != 0,
            false);
        return true;
    }
    const F7ConfigStateSnapshot configSnapshot = F7_GetConfigSnapshot();

    const bool ok = InstallDifficultyHook();
    if (!ok) {
        F7_Log(
            "[ffx-hooks] F7: all-or-nothing Difficulty batch unavailable gate=%s\n",
            F7_DifficultyGateName(g_difficultyGate));
        return false;
    }
    InterlockedExchange(&g_enabled, flagGate ? 1 : 0);
    const bool positionReaderReady = (flagGate || arenaMixRequested) && InstallPositionReadHook();
    const bool programReady=positionReaderReady&&PrepareNormalBattlePrograms();
    CustomMixUltra::Runtime::StartProduction(
        g_base, programReady && g_difficultyGate == AdapterGateCode::Installed, false);
    F7_Log("[ffx-hooks] CustomMix position reader ready=%d rva=0x003AC000\n", positionReaderReady ? 1 : 0);
    if (F7_IsEnabled() && configSnapshot.config.music.fadeFrames > 0)
        FfxHooks::SetMusicHookMinFadeFrames(configSnapshot.config.music.fadeFrames);
    if (F7_IsEnabled() && configSnapshot.config.music.lockTrack >= 0)
        F7_MusicApplyLock();
    F7_Log("[ffx-hooks] F7: shared battle runtime installed ok=%d behavior=%s Difficulty=%s configured=%s force=%s musicLock=%d\n",
        ok ? 1 : 0, F7_IsEnabled() ? "ON" : "OFF", F7_DifficultyGateName(g_difficultyGate),
        configSnapshot.config.diffGlobal.enabled ? "ON" : "OFF",
        configSnapshot.config.force.hasLast ? "has-last" : "no-last",
        configSnapshot.config.music.lockTrack);
    return true;
}

bool F7_SharedBattleRuntimeReady(uintptr_t expectedModuleBase) {
    return expectedModuleBase != 0u && g_base == expectedModuleBase &&
           g_difficultyDetours.active &&
           InterlockedCompareExchange(&g_difficultyAccepting, 0, 0) != 0;
}

uintptr_t F7_SharedBattleInitSceneTarget() {
    return g_difficultyDetours.active ? g_base + kInitSystemSceneRva : 0u;
}

SharedBattleRuntime::ComposerSlotResult F7_RegisterInitSceneComposer(
    const SharedBattleRuntime::ComposerIo* composer) {
    if (!F7_SharedBattleRuntimeReady(g_base)) {
        return SharedBattleRuntime::ComposerSlotResult::InvalidArgument;
    }
    return g_initSceneComposer.Register(composer);
}

SharedBattleRuntime::ComposerSlotResult F7_UnregisterInitSceneComposer(
    const SharedBattleRuntime::ComposerIo* composer) {
    return g_initSceneComposer.Unregister(composer);
}

void F7_RequestStop() {
    // Loader-lock callers may only close admission. The coordinator, waits, and config/runtime
    // state belong to the explicit normal-context owner.
    InterlockedExchange(&g_difficultyAccepting, 0);
    InterlockedExchange(&g_difficultyRetryArmed, 0);
    InterlockedExchange(&g_positionHookAccepting,0);
    CustomMixUltra::Runtime::ProductionRequestStop();
}

void F7_RemoveHooks() {
    if (!F7_IsEnabled() && !DifficultyHasCreatedTargets()) return;
    if (g_initSceneComposer.Load()) {
        F7_Log("[ffx-hooks] F7: shared InitScene consumer is still registered; retire consumer first\n");
        return;
    }
    F7_RequestStop();
    if (g_positionHookCreated) {
        const uintptr_t target = g_base + ArenaPositions::kAccessorRva;
        const auto report = MinHookBatch::NeutralizeBatch(&MinHookBatch::ProcessCoordinator(),
            MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::ArenaPositions,&target,1u,PositionReadFence());
        if (!report.neutralized) {
            F7_Log("[ffx-hooks] CustomMix position reader retirement requires retry\n");
            return;
        }
        // Retain the trampoline and module, including suspended pre-counter entrants.
    }
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount> specs =
        DifficultyDetourSpecs();
    const DifficultyDetourResult retired = RetireDifficultyDetours(
        DifficultyCreateIo(), &MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(), DifficultyDrainFence(), specs,
        &g_difficultyDetours);
    if (retired.code != DifficultyDetourCode::RetainedInert &&
        retired.code != DifficultyDetourCode::Removed) {
        g_difficultyGate = DifficultyGateFromDetourCode(retired.code);
        F7_Log(
            "[ffx-hooks] F7: Difficulty retirement failed gate=%s; ownership and "
            "enabled state retained for normal-context retry\n",
            F7_DifficultyGateName(g_difficultyGate));
        return;
    }

    AcquireSRWLockExclusive(&g_difficultyRuntimeLock);
    // WHY: RetireDifficultyDetours has closed admission and drained every callback. Only beyond
    // that fence is it safe to erase one-shot transition evidence and the reusable request.
    g_sinTransitionPublication.Reset();
    g_sinCurrentRequest = {};
    g_sinLastAcceptedGeneration = 0u;
    g_difficultyCurrentBattleField = {};
    ReleaseSRWLockExclusive(&g_difficultyRuntimeLock);
    CustomMixUltra::Runtime::ProductionResetAfterDrain();

    if (g_block) F7_MusicClearOverride();
    InterlockedExchange(&g_enabled, 0);
    if (retired.code == DifficultyDetourCode::Removed &&
        !g_difficultyDetours.mayHaveRun) {
        // A complete create-only rollback proves that no machine prologue could have observed
        // these outputs. Reachable batches never enter this release branch.
        if (g_difficultyDrainedEvent) CloseHandle(g_difficultyDrainedEvent);
        g_difficultyDrainedEvent = nullptr;
        g_trampResolve = nullptr;
        g_trampScene = nullptr;
        g_trampActorPopulate = nullptr;
        g_base = 0;
        g_block = nullptr;
        F7_Log("[ffx-hooks] F7: never-applied Difficulty batch removed completely\n");
        g_log = nullptr;
        return;
    }

    g_difficultyGate = AdapterGateCode::RetainedInert;
    // WHY: an entrant can be suspended in a patched machine prologue before the first C++
    // counter. No teardown-time Restore runs, and targets, trampolines, event, module base, and
    // runtime ownership therefore remain process-lifetime even after the exact batch is inert.
    F7_Log(
        "[ffx-hooks] F7: exact Difficulty batch disabled; process-lifetime storage "
        "retained, use OFF/Apply Now before retirement to reverse RAM values\n");
}

#endif // FFXHOOKS_HAVE_POLYHOOK
} // namespace FfxHooks
