#include "FieldScoutHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif

namespace FfxHooks {

namespace {

using BuildTextureSlotFn = int(__cdecl*)(int64_t a0, int a1, int a2, uint32_t* a3, char* source, int a5);
using GraphicFieldMapLoadFn = void(__cdecl*)(char* mapPath, int slot);
using LoadAndActivateDriverFn = void(__thiscall*)(void* self, char* assetPath, int mode);
using GetInstanceNameByIndexFn = char*(__thiscall*)(void* container, int index);
using MsBattleEncountExeFn = int(__cdecl*)(int selector, int group, float walkDelta);




using ChrSetWorldPositionFn = int(__cdecl*)(int instHandle, float x, float y, float z);
using TakaraLoadFn = void*(__cdecl*)(int takaraIndex);
using WarpActorFn = int(__cdecl*)(int actor, float x, float y, float z, int snap);
using SampleZoneSlotFn = int(__cdecl*)(int zoneRoot, int slotIndex);
using WireInstanceToSceneNodesFn = void(__fastcall*)(void* thisPtr, void* edx, int a2, int a3, void* a4, void* a5, void* a6, int a7, bool a8);
using CommitInstanceMappingsFn = int(__fastcall*)(void* thisPtr, void* edx, int a2, int a3, int a4, void* a5);

static bool                 g_installed = false;
static bool                 g_mapOnly = false;
static bool                 g_heavy = false;
static bool                 g_max = false;
static volatile LONG        g_shuttingDown = 0;
static FieldScoutUltraOptions g_ultra = {};
static FieldScoutLogFn      g_logFn = nullptr;
static uintptr_t            g_base = 0;
static void*                g_module = nullptr;
static FILE*                g_session = nullptr;
static FILE*                g_traceSession = nullptr;
static char                 g_sessionPath[512] = {};
static char                 g_tracePath[512] = {};
static CRITICAL_SECTION     g_lock;
static volatile LONG        g_lockInit = 0;
static volatile LONG        g_totalHits = 0;
static volatile LONG        g_uniqueAssets = 0;
static volatile LONG        g_droppedDedupe = 0;
static volatile LONG        g_traceSamples = 0;
static volatile LONG        g_traceStop = 0;
static HANDLE               g_traceThread = nullptr;
static char                 g_currentArea[32] = {};
static char                 g_currentField[32] = {};
static volatile LONG        g_captureQuiesced = 0;
static volatile LONG        g_heavyActive = 0;
static bool                 g_minhookQueued = false;

static unsigned             g_maxUniquePaths = 250000u;
static constexpr unsigned   kSeenEntryBytes = 384u;
static char*                g_seenBlob = nullptr;
static unsigned             g_seenCount = 0;

static void WriteJsonLine(const char* jsonLine);
static bool ReadPlayerAnchor(float* px, float* py, float* pz, int* sceneId, int* mapToken);
static bool FloatLooksSane(float v);
static void RecordSceneNodePlaced(const char* name, int index, float wx, float wy, float wz, bool hasWorld, const char* source);
static void RecordChestSpawn(const char* name, float x, float y, float z, const char* source);
static void RecordChrSpawn(uint32_t slotIndex, uint16_t chrId, const char* chrName, float x, float y, float z, const char* source);
static void RecordNpcSpawn(uint32_t slotIndex, uint16_t chrId, const char* chrName, float x, float y, float z, const char* source);
static void RecordTriggerSpawn(const char* name, float x, float y, float z, const char* source, const char* triggerClass);
static void RecordUltraStub(const char* category, const char* status, const char* note);
static void RecordMaxTakara(int takaraIndex, const char* source);
static void RecordMaxWarp(float x, float y, float z, const char* source, int snap);
static void RecordMaxZoneSlot(int slotIndex, int groupByte);
static void EmitUltraFieldLoadSamples(const char* reason);
static bool UltraActive(bool categoryEnabled);
static bool MaxActive(bool categoryEnabled);
static uint8_t ReadZoneByte(uintptr_t rva);
static uint8_t ReadSceneEncounterGroupByte();
static void StartPlayerTraceThread();
static void WriteTraceLine(const char* jsonLine);
static bool ApplyQueuedHooks(const char* context);
static void CloseSessionFile();
static void ScanActiveChrInstances();
static bool PathAlreadySeen(const char* path);
static bool RememberPath(const char* path);
static bool RememberPathIfNew(const char* path);

#ifdef FFXHOOKS_HAVE_POLYHOOK
static BuildTextureSlotFn         g_textureTrampoline = nullptr;
static GraphicFieldMapLoadFn      g_fieldLoadTrampoline = nullptr;
static LoadAndActivateDriverFn    g_activateTrampoline = nullptr;
static GetInstanceNameByIndexFn   g_instanceNameTrampoline = nullptr;
static MsBattleEncountExeFn       g_encounterTrampoline = nullptr;




static ChrSetWorldPositionFn    g_chrSetPosTrampoline = nullptr;
static TakaraLoadFn           g_takaraLoadTrampoline = nullptr;
static WarpActorFn            g_warpActorTrampoline = nullptr;
static SampleZoneSlotFn       g_sampleZoneTrampoline = nullptr;
static WireInstanceToSceneNodesFn g_wireInstanceTrampoline = nullptr;
static CommitInstanceMappingsFn g_commitMappingsTrampoline = nullptr;
static uint64_t                   g_textureTrampolineVa = 0;
static uint64_t                   g_fieldLoadTrampolineVa = 0;
static uint64_t                   g_activateTrampolineVa = 0;
static uint64_t                   g_instanceNameTrampolineVa = 0;
static uint64_t                   g_encounterTrampolineVa = 0;




static uint64_t                   g_chrSetPosTrampolineVa = 0;
static uint64_t                   g_takaraLoadTrampolineVa = 0;
static uint64_t                   g_warpActorTrampolineVa = 0;
static uint64_t                   g_sampleZoneTrampolineVa = 0;
static uint64_t                   g_wireInstanceTrampolineVa = 0;
static uint64_t                   g_commitMappingsTrampolineVa = 0;
#endif

static void EnsureLock() {
    if (InterlockedCompareExchange(&g_lockInit, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
    }
}

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[768] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static bool ModuleRelativePath(const char* relativePath, char* outPath, size_t outPathSize) {
    if (!g_module || !relativePath || !relativePath[0] || !outPath || outPathSize == 0) return false;
    char modulePath[MAX_PATH] = {};
    if (GetModuleFileNameA(static_cast<HMODULE>(g_module), modulePath, sizeof(modulePath)) == 0) return false;
    char* slash = strrchr(modulePath, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';
    _snprintf_s(outPath, outPathSize, _TRUNCATE, "%s%s", modulePath, relativePath);
    return true;
}

static void JsonEscape(const char* in, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!in) return;
    size_t w = 0;
    for (const char* p = in; *p && w + 2 < outSize; ++p) {
        const char c = *p;
        if (c == '\\' || c == '"') {
            if (w + 2 >= outSize) break;
            out[w++] = '\\';
            out[w++] = c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            if (w + 1 >= outSize) break;
            out[w++] = ' ';
        } else {
            out[w++] = c;
        }
    }
    out[w] = '\0';
}

static bool PathLooksLikeGeometry(const char* path) {
    if (!path) return false;
    return strstr(path, ".dae.phyre") != nullptr ||
           strstr(path, ".ahwin32") != nullptr ||
           strstr(path, ".ags.phyre") != nullptr ||
           strstr(path, ".cdf.phyre") != nullptr ||
           (strstr(path, "/map/") != nullptr && strstr(path, "/mdl/") != nullptr);
}

static bool PathLooksLikePs3Asset(const char* path) {
    if (!path || !path[0]) return false;
    return strstr(path, "PS3Data/") != nullptr ||
           strstr(path, "ps3data/") != nullptr ||
           strstr(path, ".phyre") != nullptr ||
           strstr(path, ".dds") != nullptr ||
           strstr(path, ".ahwin32") != nullptr;
}

static bool PathLooksInteresting(const char* path) {
    if (!path || !path[0]) return false;
    if (PathLooksLikeGeometry(path)) return true;
    if (g_heavy && PathLooksLikePs3Asset(path)) return true;
    if (g_mapOnly) {
        return strstr(path, "/map/") != nullptr &&
               (strstr(path, "/tex/") != nullptr || strstr(path, "/mdl/") != nullptr);
    }
    if (!PathLooksLikePs3Asset(path)) return false;
    return strstr(path, "/map/") != nullptr ||
           strstr(path, "/chr/") != nullptr ||
           strstr(path, "/eff/") != nullptr ||
           strstr(path, "/menu/") != nullptr ||
           strstr(path, "/obj/") != nullptr ||
           strstr(path, "/event/") != nullptr ||
           strstr(path, "/magic/") != nullptr ||
           strstr(path, "/sound/") != nullptr ||
           strstr(path, "/Sound/") != nullptr ||
           strstr(path, "/battle/") != nullptr ||
           strstr(path, "/btlmap/") != nullptr ||
           strstr(path, "/abmap/") != nullptr;
}

static bool SceneNodeNameInteresting(const char* name) {
    if (!name || !name[0]) return false;
    return strstr(name, "scene") != nullptr ||
           strstr(name, "layer") != nullptr ||
           strstr(name, "Shape") != nullptr ||
           strstr(name, "textureAnimation") != nullptr;
}

static bool StrContainsInsensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return false;
    const size_t nLen = strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        if (_strnicmp(p, needle, nLen) == 0) return true;
    }
    return false;
}

static bool NameLooksLikeChest(const char* name) {
    if (!name || !name[0]) return false;
    static const char* kPatterns[] = {
        "bauro",
        "chest",
        "takara",
        "treasure",
        "tbox",
        "h_treasure",
        "obj_treasure",
    };
    for (const char* pat : kPatterns) {
        if (StrContainsInsensitive(name, pat)) return true;
    }
    return false;
}

static volatile LONG g_currentGen = 0;
static void SafeReadCurrentField(char* areaOut, size_t areaSize, char* fieldOut, size_t fieldSize) {
    for (int retry = 0; retry < 4; ++retry) {
        const LONG gen1 = g_currentGen;
        if (areaOut && areaSize > 0) {
            _snprintf_s(areaOut, areaSize, _TRUNCATE, "%s", g_currentArea);
        }
        if (fieldOut && fieldSize > 0) {
            _snprintf_s(fieldOut, fieldSize, _TRUNCATE, "%s", g_currentField);
        }
        const LONG gen2 = g_currentGen;
        if (gen1 == gen2) break; /* consistent read */
    }
}
static void SetCurrentMapField(const char* area, const char* field) {
    if (area && area[0]) {
        _snprintf_s(g_currentArea, sizeof(g_currentArea), _TRUNCATE, "%s", area);
    } else {
        g_currentArea[0] = '\0';
    }
    if (field && field[0]) {
        _snprintf_s(g_currentField, sizeof(g_currentField), _TRUNCATE, "%s", field);
    } else {
        g_currentField[0] = '\0';
    }
    InterlockedIncrement(&g_currentGen);
}

/* Title/logo maps (titl00, …) must not get heavy hooks or synthetic ingest — soft-locks boot. */
static bool IsTitleBootField(const char* field) {
    return field && field[0] && _strnicmp(field, "titl", 4) == 0;
}

static bool ReadBattleActiveFlag() {
    if (!g_base) return false;
    __try {
        return *reinterpret_cast<const uint8_t*>(g_base + RVA_FFX_BATTLE_ACTIVE_FLAG) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool PathLooksLikeBattleLoad(const char* path) {
    if (!path || !path[0]) return false;
    return strstr(path, "/battle/") != nullptr ||
           strstr(path, "/btlmap/") != nullptr ||
           strstr(path, "/abmap/") != nullptr;
}

static void NoteBattleTransitionFromPath(const char* path) {
    if (PathLooksLikeBattleLoad(path)) {
        InterlockedExchange(&g_captureQuiesced, 1);
    } else if (path && (strstr(path, "/map/") != nullptr || strstr(path, "map/") != nullptr)) {
        if (!ReadBattleActiveFlag()) {
            InterlockedExchange(&g_captureQuiesced, 0);
        }
    }
}

static bool ShouldSkipFieldScoutCapture() {
    if (InterlockedCompareExchange(&g_captureQuiesced, 0, 0) != 0) return true;
    return ReadBattleActiveFlag();
}

static bool UltraActive(bool categoryEnabled) {
    return g_heavy && g_ultra.master && categoryEnabled;
}

static bool MaxActive(bool categoryEnabled) {
    return g_max && UltraActive(categoryEnabled);
}

static bool NameLooksLikeFieldTrigger(const char* name) {
    if (!name || !name[0]) return false;
    static const char* kPatterns[] = {
        "warp", "save", "trigger", "door", "gate", "telop", "event", "point", "exit", "enter",
    };
    for (const char* pat : kPatterns) {
        if (StrContainsInsensitive(name, pat)) return true;
    }
    return false;
}

/* FFX CHR basename prefix: c=party, n=npc, m=monster, f=obj/prop, s=summon, w=weapon. */
static const char* ChrCategoryFromName(const char* chrName, uint16_t chrId) {
    (void)chrId;
    if (!chrName || !chrName[0]) return "unknown";
    const char* base = strrchr(chrName, '/');
    base = base ? base + 1 : chrName;
    const char* backslash = strrchr(base, '\\');
    if (backslash) base = backslash + 1;
    char c = base[0];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    switch (c) {
        case 'c': return "party";
        case 'n': return "story_npc";
        case 'm': return "field_enemy";
        case 'f': return "field_prop";
        case 's': return "summon";
        case 'w': return "weapon";
        default: return "unknown";
    }
}

static void RecordUltraStub(const char* category, const char* status, const char* note) {
    if (!category || !UltraActive(true)) return;

    char dedupeKey[420] = {};
    _snprintf_s(dedupeKey, sizeof(dedupeKey), _TRUNCATE, "ultra_stub|%s|%s", category, status ? status : "");
    if (!RememberPathIfNew(dedupeKey)) return;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    char escCat[64] = {};
    JsonEscape(category, escCat, sizeof(escCat));
    char escStatus[64] = {};
    JsonEscape(status ? status : "pending", escStatus, sizeof(escStatus));
    char escNote[256] = {};
    JsonEscape(note ? note : "", escNote, sizeof(escNote));

    char line[900] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"ultra_stub\",\"category\":\"%s\",\"status\":\"%s\",\"note\":\"%s\","
        "\"area\":\"%s\",\"field\":\"%s\",\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        escCat,
        escStatus,
        escNote,
        g_currentArea,
        g_currentField,
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteJsonLine(line);
}

static void EmitUltraFieldLoadSamples(const char* reason) {
    if (!g_ultra.master || !g_heavy) return;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    if (UltraActive(g_ultra.fieldLogic) && !MaxActive(g_ultra.fieldLogic)) {
        RecordUltraStub(
            "field_logic",
            "partial",
            "CHR/trigger heuristics live; ATEL warpToPoint/takara/chest-state/doors = offline RE pending");
    }
    if (UltraActive(g_ultra.collision)) {
        RecordUltraStub("collision", "blocked", "walkmesh/battle-camera zones: zero decode — RE pending");
    }
    if (UltraActive(g_ultra.encounters)) {
        const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
        const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
        char line[520] = {};
        _snprintf_s(
            line,
            sizeof(line),
            _TRUNCATE,
            "{\"kind\":\"ultra_encounter_sample\",\"reason\":\"%s\",\"zoneA\":%u,\"zoneB\":%u,"
            "\"area\":\"%s\",\"field\":\"%s\",\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
            reason ? reason : "field_load",
            static_cast<unsigned>(zoneA),
            static_cast<unsigned>(zoneB),
            g_currentArea,
            g_currentField,
            static_cast<double>(px),
            static_cast<double>(py),
            static_cast<double>(pz),
            sceneId,
            mapToken);
        WriteTraceLine(line);
    }
    if (UltraActive(g_ultra.sceneEnv)) {
        char line[480] = {};
        _snprintf_s(
            line,
            sizeof(line),
            _TRUNCATE,
            "{\"kind\":\"ultra_scene_env_sample\",\"reason\":\"%s\",\"area\":\"%s\",\"field\":\"%s\","
            "\"sceneId\":%d,\"mapToken\":%d,\"note\":\"lights/weather/music/2d-layer hierarchy RE pending\"}",
            reason ? reason : "field_load",
            g_currentArea,
            g_currentField,
            sceneId,
            mapToken);
        WriteJsonLine(line);
    }
    if (UltraActive(g_ultra.pipelineHints)) {
        char line[420] = {};
        _snprintf_s(
            line,
            sizeof(line),
            _TRUNCATE,
            "{\"kind\":\"ultra_pipeline_hint\",\"copyPs3Assets\":true,\"richShards\":true,"
            "\"coordCalibration\":\"rt2_pending\",\"area\":\"%s\",\"field\":\"%s\"}",
            g_currentArea,
            g_currentField);
        WriteJsonLine(line);
    }
}

static void RecordTriggerSpawn(
    const char* name,
    float x,
    float y,
    float z,
    const char* source,
    const char* triggerClass) {
    if (!UltraActive(g_ultra.fieldLogic) || !name || !name[0]) return;
    if (!FloatLooksSane(x) || !FloatLooksSane(y) || !FloatLooksSane(z)) return;

    char dedupeKey[420] = {};
    _snprintf_s(
        dedupeKey,
        sizeof(dedupeKey),
        _TRUNCATE,
        "trigger_spawn|%s|%.1f|%.1f|%.1f",
        name,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z));
    if (!RememberPathIfNew(dedupeKey)) return;

    char escName[512] = {};
    JsonEscape(name, escName, sizeof(escName));
    char escSource[64] = {};
    JsonEscape(source ? source : "unknown", escSource, sizeof(escSource));
    char escClass[64] = {};
    JsonEscape(triggerClass ? triggerClass : "heuristic", escClass, sizeof(escClass));

    char line[1100] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"trigger_spawn\",\"name\":\"%s\",\"triggerClass\":\"%s\",\"source\":\"%s\","
        "\"area\":\"%s\",\"field\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}",
        escName,
        escClass,
        escSource,
        g_currentArea,
        g_currentField,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z));
    WriteJsonLine(line);
}

static void RecordMaxTakara(int takaraIndex, const char* source) {
    if (!MaxActive(g_ultra.fieldLogic)) return;

    char dedupeKey[256] = {};
    _snprintf_s(dedupeKey, sizeof(dedupeKey), _TRUNCATE, "max_takara|%d", takaraIndex);
    if (!RememberPathIfNew(dedupeKey)) return;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    char escSource[64] = {};
    JsonEscape(source ? source : "takara_load", escSource, sizeof(escSource));

    char line[720] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"max_takara\",\"takaraIndex\":%d,\"source\":\"%s\","
        "\"area\":\"%s\",\"field\":\"%s\","
        "\"anchorX\":%.3f,\"anchorY\":%.3f,\"anchorZ\":%.3f,"
        "\"sceneId\":%d,\"mapToken\":%d}",
        takaraIndex,
        escSource,
        g_currentArea,
        g_currentField,
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteJsonLine(line);
}

static void RecordMaxWarp(float x, float y, float z, const char* source, int snap) {
    if (!MaxActive(g_ultra.fieldLogic)) return;
    if (!FloatLooksSane(x) || !FloatLooksSane(y) || !FloatLooksSane(z)) return;

    char dedupeKey[320] = {};
    _snprintf_s(
        dedupeKey,
        sizeof(dedupeKey),
        _TRUNCATE,
        "max_warp|%.2f|%.2f|%.2f",
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z));
    if (!RememberPathIfNew(dedupeKey)) return;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    char escSource[64] = {};
    JsonEscape(source ? source : "warp", escSource, sizeof(escSource));

    char line[760] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"max_warp\",\"source\":\"%s\",\"snap\":%d,"
        "\"area\":\"%s\",\"field\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
        "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        escSource,
        snap,
        g_currentArea,
        g_currentField,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteJsonLine(line);
}

static void RecordMaxZoneSlot(int slotIndex, int groupByte) {
    if (!MaxActive(g_ultra.encounters)) return;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);
    const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
    const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
    const uint8_t sceneGroup = ReadSceneEncounterGroupByte();

    char line[620] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"max_zone_slot\",\"slotIndex\":%d,\"groupByte\":%d,\"valid\":%d,\"sceneGroup\":%u,"
        "\"zoneA\":%u,\"zoneB\":%u,\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,"
        "\"sceneId\":%d,\"mapToken\":%d,\"area\":\"%s\",\"field\":\"%s\"}",
        slotIndex,
        groupByte,
        groupByte >= 0 ? 1 : 0,
        static_cast<unsigned>(sceneGroup),
        static_cast<unsigned>(zoneA),
        static_cast<unsigned>(zoneB),
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken,
        g_currentArea,
        g_currentField);
    WriteJsonLine(line);
}

static void RecordNpcSpawn(
    uint32_t slotIndex,
    uint16_t chrId,
    const char* chrName,
    float x,
    float y,
    float z,
    const char* source) {
    if (!UltraActive(g_ultra.fieldLogic)) return;
    if (chrName && NameLooksLikeChest(chrName)) return;

    /* ponytail: optional second dedupe pass by npc_actor|chrId|chrName updating XYZ
     * instead of a new JSONL line — skipped here (position-quantized key is risky). */
    char dedupeKey[420] = {};
    _snprintf_s(
        dedupeKey,
        sizeof(dedupeKey),
        _TRUNCATE,
        "npc_spawn|%u|%u|%.1f|%.1f|%.1f",
        static_cast<unsigned>(slotIndex),
        static_cast<unsigned>(chrId),
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z));
    if (!RememberPathIfNew(dedupeKey)) return;

    char escName[96] = {};
    JsonEscape(chrName ? chrName : "", escName, sizeof(escName));
    char escSource[64] = {};
    JsonEscape(source ? source : "scan", escSource, sizeof(escSource));
    const char* chrCategory = ChrCategoryFromName(chrName, chrId);

    char line[960] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"npc_spawn\",\"chrId\":%u,\"chrName\":\"%s\",\"chrCategory\":\"%s\","
        "\"confidence\":\"heuristic\",\"slot\":%u,\"source\":\"%s\","
        "\"area\":\"%s\",\"field\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
        "\"ebpLink\":\"pending\"}",
        static_cast<unsigned>(chrId),
        escName,
        chrCategory,
        static_cast<unsigned>(slotIndex),
        escSource,
        g_currentArea,
        g_currentField,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z));
    WriteJsonLine(line);
}

static const char* ClassifyPath(const char* path) {
    if (!path) return "other";
    if (strstr(path, ".dae.phyre")) return "phyre_dae";
    if (strstr(path, ".ahwin32")) return "phyre_manifest";
    if (strstr(path, ".ags.phyre")) return "phyre_ags";
    if (strstr(path, "/map/") && strstr(path, "/tex/")) return "map_tex";
    if (strstr(path, "/map/") && strstr(path, "/mdl/")) return "map_mesh";
    if (strstr(path, "/map/") && strstr(path, "/2d/")) return "map_2d";
    if (strstr(path, "/map/")) return "map_other";
    if (strstr(path, "/btlmap/")) return "btlmap";
    if (strstr(path, "/chr/")) return "chr";
    if (strstr(path, "/eff/")) return "effect";
    if (strstr(path, "/magic/")) return "magic";
    if (strstr(path, "/event/") || strstr(path, "/obj/")) return "event_obj";
    if (strstr(path, "/menu/") || strstr(path, "/abmap/")) return "menu";
    if (strstr(path, "/sound/") || strstr(path, "/Sound/")) return "sound";
    if (strstr(path, "/battle/")) return "battle";
    return "other";
}

static bool ExtractMapFieldLoose(const char* path, char* areaOut, size_t areaSize, char* fieldOut, size_t fieldSize) {
    if (areaOut && areaSize) areaOut[0] = '\0';
    if (fieldOut && fieldSize) fieldOut[0] = '\0';
    if (!path) return false;

    const char* mapTag = strstr(path, "/map/");
    const char* mapTag2 = strstr(path, "map/");
    const char* start = mapTag ? mapTag + 5 : (mapTag2 ? mapTag2 + 4 : nullptr);
    if (!start) return false;

    const char* slash1 = strchr(start, '/');
    if (!slash1) return false;

    if (areaOut && areaSize > 0) {
        const size_t n = static_cast<size_t>(slash1 - start);
        const size_t cap = n < areaSize - 1 ? n : areaSize - 1;
        memcpy(areaOut, start, cap);
        areaOut[cap] = '\0';
    }

    const char* fieldStart = slash1 + 1;
    const char* slash2 = strchr(fieldStart, '/');
    const char* end = slash2 ? slash2 : fieldStart + strlen(fieldStart);
    while (end > fieldStart && (end[-1] == '/' || end[-1] == '\\')) --end;

    if (fieldOut && fieldSize > 0 && end > fieldStart) {
        const size_t n = static_cast<size_t>(end - fieldStart);
        const size_t cap = n < fieldSize - 1 ? n : fieldSize - 1;
        memcpy(fieldOut, fieldStart, cap);
        fieldOut[cap] = '\0';
    }
    return fieldOut && fieldOut[0] != '\0';
}

static bool ExtractMapField(const char* path, char* areaOut, size_t areaSize, char* fieldOut, size_t fieldSize) {
    if (ExtractMapFieldLoose(path, areaOut, areaSize, fieldOut, fieldSize)) return true;
    const char* mapTag = strstr(path, "/map/");
    if (!mapTag) return false;
    mapTag += 5;
    const char* slash1 = strchr(mapTag, '/');
    if (!slash1) return false;
    const size_t areaLen = static_cast<size_t>(slash1 - mapTag);
    if (areaOut && areaSize > 0) {
        const size_t n = areaLen < areaSize - 1 ? areaLen : areaSize - 1;
        memcpy(areaOut, mapTag, n);
        areaOut[n] = '\0';
    }
    const char* fieldStart = slash1 + 1;
    const char* slash2 = strchr(fieldStart, '/');
    if (!slash2) return false;
    if (fieldOut && fieldSize > 0) {
        const size_t fieldLen = static_cast<size_t>(slash2 - fieldStart);
        const size_t n = fieldLen < fieldSize - 1 ? fieldLen : fieldSize - 1;
        memcpy(fieldOut, fieldStart, n);
        fieldOut[n] = '\0';
    }
    return true;
}

static bool ParseTileFromBasename(const char* path, int* tier, int* tileX, int* tileY, int* tileW, int* tileH) {
    if (tier) *tier = -1;
    if (tileX) *tileX = 0;
    if (tileY) *tileY = 0;
    if (tileW) *tileW = 0;
    if (tileH) *tileH = 0;
    if (!path) return false;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    unsigned a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    if (sscanf_s(base, "%u_%u_%u_%u_%u_%u", &a, &b, &c, &d, &e, &f) >= 4) {
        if (tier) *tier = static_cast<int>(b);
        if (tileX) *tileX = static_cast<int>(c);
        if (tileY) *tileY = static_cast<int>(d);
        if (tileW) *tileW = static_cast<int>(e);
        if (tileH) *tileH = static_cast<int>(f);
        return true;
    }
    return false;
}

static bool ReadPlayerAnchor(float* px, float* py, float* pz, int* sceneId, int* mapToken) {
    if (px) *px = 0.0f;
    if (py) *py = 0.0f;
    if (pz) *pz = 0.0f;
    if (sceneId) *sceneId = 0;
    if (mapToken) *mapToken = 0;
    if (!g_base) return false;
    __try {
        void* const inst = *reinterpret_cast<void* const*>(g_base + RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR);
        if (inst) {
            const float* pos = reinterpret_cast<const float*>(reinterpret_cast<const char*>(inst) + FFX_CHR_INSTANCE_WORLD_X_OFFSET);
            if (px) *px = pos[0];
            if (py) *py = pos[1];
            if (pz) *pz = pos[2];
        }
        const char* scene = reinterpret_cast<const char*>(g_base + RVA_FFX_SCENE_STATE_OBJECT);
        if (sceneId) *sceneId = *reinterpret_cast<const int*>(scene + FFX_SCENE_STATE_SCENE_ID_OFFSET);
        if (mapToken) *mapToken = *reinterpret_cast<const int*>(scene + FFX_SCENE_STATE_MAP_TOKEN_OFFSET);
        return inst != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool IsPlausiblePtr(const void* p) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000u && v < 0x7FFE0000u;
}

static bool FloatLooksSane(float v) {
    return v == v && v > -100000.0f && v < 100000.0f;
}

static bool ReadMatrixTranslation(const void* matrix4x4, float* tx, float* ty, float* tz) {
    if (!matrix4x4 || !tx || !ty || !tz) return false;
    __try {
        const float* m = reinterpret_cast<const float*>(matrix4x4);
        *tx = m[12];
        *ty = m[13];
        *tz = m[14];
        return FloatLooksSane(*tx) && FloatLooksSane(*ty) && FloatLooksSane(*tz);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool NodeLooksPlausible(const void* node) {
    if (!IsPlausiblePtr(node)) return false;
    __try {
        const char* nodeBytes = reinterpret_cast<const char*>(node);
        const void* local = nodeBytes + FFX_PNODE_LOCAL_MATRIX_OFFSET;
        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        if (!ReadMatrixTranslation(local, &tx, &ty, &tz)) return false;
        const void* worldPtr = *reinterpret_cast<void* const*>(nodeBytes + FFX_PNODE_WORLD_MATRIX_PTR_OFFSET);
        return IsPlausiblePtr(worldPtr) || (fabsf(tx) + fabsf(ty) + fabsf(tz)) < 50000.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool TryReadNodeName(const void* node, char* outName, size_t outSize) {
    if (!outName || outSize == 0) return false;
    outName[0] = '\0';
    if (!IsPlausiblePtr(node)) return false;
    __try {
        const char* base = reinterpret_cast<const char*>(node) + FFX_PNODE_NAME_OFFSET;
        const char* inlineName = base;
        if (inlineName[0] >= 'a' && inlineName[0] <= 'z') {
            _snprintf_s(outName, outSize, _TRUNCATE, "%s", inlineName);
            return outName[0] != '\0';
        }
        const char* heapName = *reinterpret_cast<const char* const*>(base);
        if (IsPlausiblePtr(heapName) && heapName[0]) {
            _snprintf_s(outName, outSize, _TRUNCATE, "%s", heapName);
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outName[0] = '\0';
    }
    return false;
}

static void* ResolvePNodeFromContainer(void* container, int index) {
    if (!IsPlausiblePtr(container) || index < 0 || index > 4096) return nullptr;
    __try {
        static const unsigned kCountOffs[] = { 4u, 8u, 12u, 16u };
        static const unsigned kDataOffs[] = { 8u, 12u, 16u, 20u, 4u, 0u };
        for (unsigned countOff : kCountOffs) {
            const int count = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(container) + countOff);
            if (index >= count || count <= 0 || count > 8192) continue;
            for (unsigned dataOff : kDataOffs) {
                void* tablePtr = *reinterpret_cast<void* const*>(
                    reinterpret_cast<const char*>(container) + dataOff);
                if (!IsPlausiblePtr(tablePtr)) continue;
                void* const* data = reinterpret_cast<void* const*>(tablePtr);
                void* entry = data[index];
                if (NodeLooksPlausible(entry)) return entry;
                if (!IsPlausiblePtr(entry)) continue;
                void* inner = *reinterpret_cast<void* const*>(entry);
                if (NodeLooksPlausible(inner)) return inner;
                inner = *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(entry) + 4);
                if (NodeLooksPlausible(inner)) return inner;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return nullptr;
}

static bool ComposeWorldTranslationFromNode(void* node, float* wx, float* wy, float* wz) {
    if (!node || !wx || !wy || !wz) return false;
    /* ComposeWorldMatrix trampoline removed (was naked-jmp passthrough, zero data value,
       and interfered with MH_ApplyQueued). Fall back to local matrix translation only. */
    return ReadMatrixTranslation(
        reinterpret_cast<const char*>(node) + FFX_PNODE_LOCAL_MATRIX_OFFSET,
        wx,
        wy,
        wz);
}

static void ParseSceneLayerFromName(const char* name, int* sceneOut, int* layerOut) {
    if (sceneOut) *sceneOut = -1;
    if (layerOut) *layerOut = -1;
    if (!name) return;
    unsigned scene = 0, layer = 0;
    if (sscanf_s(name, "scene%ulayer%uShape", &scene, &layer) >= 2) {
        if (sceneOut) *sceneOut = static_cast<int>(scene);
        if (layerOut) *layerOut = static_cast<int>(layer);
    }
}

static void RecordSceneNodePlaced(
    const char* name,
    int index,
    float wx,
    float wy,
    float wz,
    bool hasWorld,
    const char* source) {
    if (!name || !name[0]) return;

    char dedupeKey[420] = {};
    _snprintf_s(
        dedupeKey,
        sizeof(dedupeKey),
        _TRUNCATE,
        "scene_node_placed|%s|%d|%.1f|%.1f|%.1f",
        name,
        index,
        static_cast<double>(wx),
        static_cast<double>(wy),
        static_cast<double>(wz));
    if (!RememberPathIfNew(dedupeKey)) return;
    InterlockedIncrement(&g_totalHits);

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    int scene = -1, layer = -1;
    ParseSceneLayerFromName(name, &scene, &layer);

    char esc[512] = {};
    JsonEscape(name, esc, sizeof(esc));
    char escSource[64] = {};
    JsonEscape(source ? source : "unknown", escSource, sizeof(escSource));

    char line[1200] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"scene_node_placed\",\"path\":\"%s\",\"source\":\"%s\",\"index\":%d,"
        "\"scene\":%d,\"layer\":%d,\"hasWorld\":%s,"
        "\"area\":\"%s\",\"field\":\"%s\","
        "\"wx\":%.3f,\"wy\":%.3f,\"wz\":%.3f,"
        "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        esc,
        escSource,
        index,
        scene,
        layer,
        hasWorld ? "true" : "false",
        g_currentArea,
        g_currentField,
        static_cast<double>(wx),
        static_cast<double>(wy),
        static_cast<double>(wz),
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteJsonLine(line);

    if (hasWorld && NameLooksLikeChest(name)) {
        RecordChestSpawn(name, wx, wy, wz, source);
    } else if (hasWorld && UltraActive(g_ultra.fieldLogic) && NameLooksLikeFieldTrigger(name)) {
        RecordTriggerSpawn(name, wx, wy, wz, source, "scene_node_heuristic");
    }
}

static void RecordChestSpawn(
    const char* name,
    float x,
    float y,
    float z,
    const char* source) {
    if (!name || !name[0]) return;
    if (!FloatLooksSane(x) || !FloatLooksSane(y) || !FloatLooksSane(z)) return;

    char dedupeKey[420] = {};
    _snprintf_s(
        dedupeKey,
        sizeof(dedupeKey),
        _TRUNCATE,
        "chest_spawn|%s|%.1f|%.1f|%.1f",
        name,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z));
    if (!RememberPathIfNew(dedupeKey)) return;
    InterlockedIncrement(&g_totalHits);

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    char escName[512] = {};
    JsonEscape(name, escName, sizeof(escName));
    char escSource[64] = {};
    JsonEscape(source ? source : "unknown", escSource, sizeof(escSource));

    char line[1100] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"chest_spawn\",\"name\":\"%s\",\"source\":\"%s\","
        "\"area\":\"%s\",\"field\":\"%s\","
        "\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
        "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        escName,
        escSource,
        g_currentArea,
        g_currentField,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteJsonLine(line);
}

static void RecordChrSpawn(
    uint32_t slotIndex,
    uint16_t chrId,
    const char* chrName,
    float x,
    float y,
    float z,
    const char* source) {
    if (ShouldSkipFieldScoutCapture()) return;
    char dedupeKey[420] = {};
    _snprintf_s(
        dedupeKey,
        sizeof(dedupeKey),
        _TRUNCATE,
        "chr_spawn|%u|%u|%.1f|%.1f|%.1f",
        static_cast<unsigned>(slotIndex),
        static_cast<unsigned>(chrId),
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z));
    if (!RememberPathIfNew(dedupeKey)) return;
    InterlockedIncrement(&g_totalHits);

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    char escName[96] = {};
    JsonEscape(chrName ? chrName : "", escName, sizeof(escName));
    char escSource[64] = {};
    JsonEscape(source ? source : "scan", escSource, sizeof(escSource));
    const char* chrCategory = ChrCategoryFromName(chrName, chrId);

    char line[960] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"chr_spawn\",\"chrId\":%u,\"chrName\":\"%s\",\"chrCategory\":\"%s\","
        "\"confidence\":\"heuristic\",\"slot\":%u,\"source\":\"%s\","
        "\"area\":\"%s\",\"field\":\"%s\","
        "\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        static_cast<unsigned>(chrId),
        escName,
        chrCategory,
        static_cast<unsigned>(slotIndex),
        escSource,
        g_currentArea,
        g_currentField,
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteJsonLine(line);

    if (chrName && NameLooksLikeChest(chrName)) {
        RecordChestSpawn(chrName, x, y, z, source);
    } else {
        RecordNpcSpawn(slotIndex, chrId, chrName, x, y, z, source);
    }
}

static void ScanActiveChrInstances() {
    if (!g_base || !g_heavy || ShouldSkipFieldScoutCapture()) return;
    __try {
        const uint32_t count = *reinterpret_cast<const uint32_t*>(g_base + RVA_FFX_ACTIVE_CHR_INSTANCE_COUNT);
        const uint32_t table = *reinterpret_cast<const uint32_t*>(g_base + RVA_FFX_ACTIVE_CHR_INSTANCE_TABLE);
        if (count == 0 || count > 4096 || !IsPlausiblePtr(reinterpret_cast<const void*>(static_cast<uintptr_t>(table)))) {
            return;
        }
        for (uint32_t idx = 0; idx < count; ++idx) {
            const char* inst = reinterpret_cast<const char*>(static_cast<uintptr_t>(table) + idx * FFX_ACTIVE_CHR_INSTANCE_STRIDE);
            if (inst[FFX_CHR_INSTANCE_ACTIVE_OFFSET] == 0) continue;
            const uint16_t chrId = *reinterpret_cast<const uint16_t*>(inst + FFX_CHR_INSTANCE_ID_OFFSET);
            if (chrId == 0) continue;
            const float x = *reinterpret_cast<const float*>(inst + FFX_CHR_INSTANCE_WORLD_X_OFFSET);
            const float y = *reinterpret_cast<const float*>(inst + FFX_CHR_INSTANCE_WORLD_X_OFFSET + 4);
            const float z = *reinterpret_cast<const float*>(inst + FFX_CHR_INSTANCE_WORLD_X_OFFSET + 8);
            if (!FloatLooksSane(x) || !FloatLooksSane(y) || !FloatLooksSane(z)) continue;
            if (fabsf(x) < 0.01f && fabsf(y) < 0.01f && fabsf(z) < 0.01f) continue;
            const char* chrName = nullptr;
            const char* const namePtr = *reinterpret_cast<const char* const*>(inst + FFX_CHR_INSTANCE_NAME_PTR_OFFSET);
            if (IsPlausiblePtr(namePtr)) chrName = namePtr;
            RecordChrSpawn(idx, chrId, chrName, x, y, z, "active_table");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* Caller must hold g_lock. */
static bool SeenStoreContainsLocked(const char* path) {
    if (!g_seenBlob || !path) return false;
    const unsigned n = g_seenCount;
    for (unsigned i = 0; i < n; ++i) {
        if (_stricmp(g_seenBlob + (i * kSeenEntryBytes), path) == 0) return true;
    }
    return false;
}

/* Atomic check-then-insert under g_lock; returns true if the path is NEW
 * (i.e. caller should emit). Returns false if already seen OR store is full.
 * Fixes P0 (race/TOCTOU heap-overflow) + the check-then-add window. */
static bool RememberPathIfNew(const char* path) {
    if (!path) return false;
    EnterCriticalSection(&g_lock);
    if (!g_seenBlob || InterlockedCompareExchange(&g_shuttingDown, 0, 0) != 0) {
        LeaveCriticalSection(&g_lock);
        return false;
    }
    if (SeenStoreContainsLocked(path)) {
        LeaveCriticalSection(&g_lock);
        return false;
    }
    if (g_seenCount >= g_maxUniquePaths) {
        InterlockedIncrement(&g_droppedDedupe);
        LeaveCriticalSection(&g_lock);
        return false;
    }
    const unsigned slotIndex = g_seenCount;            // single read under lock
    char* slot = g_seenBlob + (slotIndex * kSeenEntryBytes);
    _snprintf_s(slot, kSeenEntryBytes, _TRUNCATE, "%s", path);
    g_seenCount = slotIndex + 1;                        // publish after write
    InterlockedIncrement(&g_uniqueAssets);
    LeaveCriticalSection(&g_lock);
    return true;
}

/* Thin wrappers kept for any non-paired call sites: both serialize on g_lock. */
static bool PathAlreadySeen(const char* path) {
    EnterCriticalSection(&g_lock);
    const bool seen = SeenStoreContainsLocked(path);
    LeaveCriticalSection(&g_lock);
    return seen;
}

static bool RememberPath(const char* path) {
    return RememberPathIfNew(path);
}

static bool EnsureSeenStore() {
    if (g_seenBlob) return true;
    const SIZE_T bytes = static_cast<SIZE_T>(g_maxUniquePaths) * kSeenEntryBytes;
    g_seenBlob = static_cast<char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes));
    return g_seenBlob != nullptr;
}

static void FreeSeenStore() {
    EnterCriticalSection(&g_lock);
    if (g_seenBlob) {
        HeapFree(GetProcessHeap(), 0, g_seenBlob);
        g_seenBlob = nullptr;
    }
    g_seenCount = 0;
    LeaveCriticalSection(&g_lock);
}

static void WriteJsonLine(const char* jsonLine);
static void RecordManifestAsset(const char* manifestKind, const char* path, int extraInt, const char* extraLabel);

static void WriteJsonLineToClosingFile(FILE* file, const char* jsonLine) {
    if (!file || !jsonLine) return;
    EnterCriticalSection(&g_lock);
    fputs(jsonLine, file);
    fputc('\n', file);
    LeaveCriticalSection(&g_lock);
}

static void EmitSyntheticFieldPack(const char* area, const char* field) {
    if (ShouldSkipFieldScoutCapture()) return;
    if (!area || !field || !area[0] || !field[0]) return;
    char path[384] = {};
    const char* templates[] = {
        "map/%s/%s/mdl/d3d11/%s.dae.phyre",
        "map/%s/%s/mdl/d3d11/textureanimation.ags.phyre",
        "map/%s/%s/2d/mdl/d3d11/%s.dae.phyre",
        "map/%s/%s/tex/d3d11",
        "map/%s/%s/fp/tex/D3D11",
    };
    for (const char* tmpl : templates) {
        _snprintf_s(path, sizeof(path), _TRUNCATE, tmpl, area, field, field);
        RecordManifestAsset("geometry_inferred", path, 0, nullptr);
    }
}

static uint8_t ReadZoneByte(uintptr_t rva) {
    if (!g_base) return 0;
    __try {
        return *reinterpret_cast<const uint8_t*>(g_base + rva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static uint8_t ReadSceneEncounterGroupByte() {
    return ReadZoneByte(RVA_FFX_SCENE_STATE_ENCOUNTER_GROUP_BYTE);
}

static void WriteJsonLineTo(FILE* file, const char* jsonLine) {
    if (!file || !jsonLine) return;
    EnterCriticalSection(&g_lock);
    if (InterlockedCompareExchange(&g_shuttingDown, 0, 0) == 0 && file) {
        fputs(jsonLine, file);
        fputc('\n', file);
        if (!g_heavy) {
            fflush(file);
        }
    }
    LeaveCriticalSection(&g_lock);
}

static void WriteJsonLine(const char* jsonLine) {
    WriteJsonLineTo(g_session, jsonLine);
}

static void WriteTraceLine(const char* jsonLine) {
    WriteJsonLineTo(g_traceSession ? g_traceSession : g_session, jsonLine);
}

static void FlushSessions() {
    EnterCriticalSection(&g_lock);
    if (g_session) fflush(g_session);
    if (g_traceSession) fflush(g_traceSession);
    LeaveCriticalSection(&g_lock);
}

static void RecordPlayerTrace(float px, float py, float pz, int sceneId, int mapToken, const char* reason) {
    char line[480] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"player_trace\",\"reason\":\"%s\",\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        reason ? reason : "sample",
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteTraceLine(line);
    InterlockedIncrement(&g_traceSamples);
}

static DWORD WINAPI PlayerTraceThreadProc(LPVOID /*param*/) {
    float lastPx = 0.0f, lastPy = 0.0f, lastPz = 0.0f;
    int lastSceneId = -1;
    bool haveLast = false;
    unsigned flushCounter = 0;

    while (InterlockedCompareExchange(&g_traceStop, 0, 0) == 0) {
        Sleep(1500);
        if (InterlockedCompareExchange(&g_traceStop, 0, 0) != 0) break;
        if (ShouldSkipFieldScoutCapture()) {
            haveLast = false;
            continue;
        }

        float px = 0.0f, py = 0.0f, pz = 0.0f;
        int sceneId = 0, mapToken = 0;
        if (!ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken)) continue;

        bool shouldLog = !haveLast;
        if (haveLast) {
            const float dx = px - lastPx;
            const float dy = py - lastPy;
            const float dz = pz - lastPz;
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= 2.25f) shouldLog = true; /* ~1.5 units */
            if (sceneId != lastSceneId) shouldLog = true;
        }

        if (shouldLog) {
            char localArea[32] = {}, localField[32] = {};
            SafeReadCurrentField(localArea, sizeof(localArea), localField, sizeof(localField));
            const char* reason = (!haveLast) ? "start" : (sceneId != lastSceneId ? "scene_change" : "move");
            RecordPlayerTrace(px, py, pz, sceneId, mapToken, reason);
            if (UltraActive(g_ultra.encounters)) {
                const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
                const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
                char line[480] = {};
                _snprintf_s(
                    line,
                    sizeof(line),
                    _TRUNCATE,
                    "{\"kind\":\"ultra_zone_trace\",\"zoneA\":%u,\"zoneB\":%u,\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,"
                    "\"sceneId\":%d,\"mapToken\":%d,\"area\":\"%s\",\"field\":\"%s\"}",
                    static_cast<unsigned>(zoneA),
                    static_cast<unsigned>(zoneB),
                    static_cast<double>(px),
                    static_cast<double>(py),
                    static_cast<double>(pz),
                    sceneId,
                    mapToken,
                    localArea,
                    localField);
                WriteTraceLine(line);
            }
            lastPx = px;
            lastPy = py;
            lastPz = pz;
            lastSceneId = sceneId;
            haveLast = true;
        }

        if (++flushCounter >= 20) {
            flushCounter = 0;
            FlushSessions();
        }

        if ((flushCounter % 4) == 0) {
            ScanActiveChrInstances();
        }
    }
    return 0;
}

static void StartPlayerTraceThread() {
    if (!g_heavy || g_traceThread) return;
    InterlockedExchange(&g_traceStop, 0);
    g_traceThread = CreateThread(nullptr, 0, PlayerTraceThreadProc, nullptr, 0, nullptr);
    if (g_traceThread) {
        HookLog("[ffx-hooks] FieldScout heavy player-trace thread started");
    }
}

static void StopPlayerTraceThread() {
    if (!g_traceThread) return;
    InterlockedExchange(&g_traceStop, 1);
    DWORD waitResult = WaitForSingleObject(g_traceThread, 10000);
    if (waitResult == WAIT_TIMEOUT) {
        HookLog("[ffx-hooks] WARN FieldScout player-trace thread did not exit within 10s — will retry");
        waitResult = WaitForSingleObject(g_traceThread, 5000);
    }
    if (waitResult != WAIT_OBJECT_0) {
        HookLog("[ffx-hooks] WARN FieldScout player-trace thread exit wait=%lu — forcing close", waitResult);
    }
    CloseHandle(g_traceThread);
    g_traceThread = nullptr;
}

static void RecordManifestAssetWithMeta(
    const char* manifestKind,
    const char* path,
    int extraInt,
    const char* extraLabel,
    unsigned polyMeta,
    bool hasPolyMeta,
    int texArg1,
    int texArg2,
    int texArg5) {
    if (!path || !path[0] || ShouldSkipFieldScoutCapture()) return;
    InterlockedIncrement(&g_totalHits);

    char dedupeKey[420] = {};
    if (extraLabel && extraLabel[0]) {
        _snprintf_s(dedupeKey, sizeof(dedupeKey), _TRUNCATE, "%s|%s|%d", manifestKind, path, extraInt);
    } else {
        _snprintf_s(dedupeKey, sizeof(dedupeKey), _TRUNCATE, "%s|%s", manifestKind, path);
    }
    if (!RememberPathIfNew(dedupeKey)) return;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);

    char area[32] = {};
    char field[32] = {};
    ExtractMapField(path, area, sizeof(area), field, sizeof(field));

    int tier = -1, tileX = 0, tileY = 0, tileW = 0, tileH = 0;
    ParseTileFromBasename(path, &tier, &tileX, &tileY, &tileW, &tileH);

    char esc[512] = {};
    JsonEscape(path, esc, sizeof(esc));

    char line[1800] = {};
    const unsigned shift17 = hasPolyMeta ? ((polyMeta >> 17) & 0x7FFFu) : 0u;

    if (extraLabel && extraLabel[0]) {
        if (hasPolyMeta) {
            _snprintf_s(
                line, sizeof(line), _TRUNCATE,
                "{\"kind\":\"%s\",\"cat\":\"%s\",\"path\":\"%s\",\"area\":\"%s\",\"field\":\"%s\","
                "\"%s\":%d,\"tier\":%d,\"tileX\":%d,\"tileY\":%d,\"tileW\":%d,\"tileH\":%d,"
                "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d"
                ",\"polyMeta\":%u,\"shift17\":%u,\"texA1\":%d,\"texA2\":%d,\"texA5\":%d}",
                manifestKind, ClassifyPath(path), esc, area, field,
                extraLabel, extraInt, tier, tileX, tileY, tileW, tileH,
                static_cast<double>(px), static_cast<double>(py), static_cast<double>(pz),
                sceneId, mapToken, polyMeta, shift17, texArg1, texArg2, texArg5);
        } else {
            _snprintf_s(
                line, sizeof(line), _TRUNCATE,
                "{\"kind\":\"%s\",\"cat\":\"%s\",\"path\":\"%s\",\"area\":\"%s\",\"field\":\"%s\","
                "\"%s\":%d,\"tier\":%d,\"tileX\":%d,\"tileY\":%d,\"tileW\":%d,\"tileH\":%d,"
                "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
                manifestKind, ClassifyPath(path), esc, area, field,
                extraLabel, extraInt, tier, tileX, tileY, tileW, tileH,
                static_cast<double>(px), static_cast<double>(py), static_cast<double>(pz),
                sceneId, mapToken);
        }
    } else if (hasPolyMeta) {
        _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "{\"kind\":\"%s\",\"cat\":\"%s\",\"path\":\"%s\",\"area\":\"%s\",\"field\":\"%s\","
            "\"tier\":%d,\"tileX\":%d,\"tileY\":%d,\"tileW\":%d,\"tileH\":%d,"
            "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d"
            ",\"polyMeta\":%u,\"shift17\":%u,\"texA1\":%d,\"texA2\":%d,\"texA5\":%d}",
            manifestKind, ClassifyPath(path), esc, area, field,
            tier, tileX, tileY, tileW, tileH,
            static_cast<double>(px), static_cast<double>(py), static_cast<double>(pz),
            sceneId, mapToken, polyMeta, shift17, texArg1, texArg2, texArg5);
    } else {
        _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "{\"kind\":\"%s\",\"cat\":\"%s\",\"path\":\"%s\",\"area\":\"%s\",\"field\":\"%s\","
            "\"tier\":%d,\"tileX\":%d,\"tileY\":%d,\"tileW\":%d,\"tileH\":%d,"
            "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
            manifestKind, ClassifyPath(path), esc, area, field,
            tier, tileX, tileY, tileW, tileH,
            static_cast<double>(px), static_cast<double>(py), static_cast<double>(pz),
            sceneId, mapToken);
    }
    WriteJsonLine(line);

    if (g_logFn && (g_uniqueAssets <= 25 || (g_uniqueAssets % 50) == 0)) {
        HookLog("[field-scout] %s #%ld %s field=%s",
            manifestKind,
            g_uniqueAssets,
            ClassifyPath(path),
            field[0] ? field : "?");
    }
}

static void RecordManifestAsset(
    const char* manifestKind,
    const char* path,
    int extraInt,
    const char* extraLabel) {
    RecordManifestAssetWithMeta(manifestKind, path, extraInt, extraLabel, 0, false, 0, 0, 0);
}

static void RecordEncounterTrace(int selector, int group, float walkDelta) {
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);
    const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
    const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
    const uint8_t sceneGroup = g_max ? ReadSceneEncounterGroupByte() : 0;
    char line[620] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"encounter\",\"sel\":%d,\"group\":%d,\"walkDelta\":%.3f,\"zoneA\":%u,\"zoneB\":%u,"
        "\"sceneGroup\":%u,\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        selector,
        group,
        static_cast<double>(walkDelta),
        static_cast<unsigned>(zoneA),
        static_cast<unsigned>(zoneB),
        static_cast<unsigned>(sceneGroup),
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteTraceLine(line);
}

static void RecordPolyMetaTrace(unsigned polyMeta, int decoded) {
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    int sceneId = 0, mapToken = 0;
    ReadPlayerAnchor(&px, &py, &pz, &sceneId, &mapToken);
    const unsigned shift17 = (polyMeta >> 17) & 0x7FFFu;
    char line[480] = {};
    _snprintf_s(
        line,
        sizeof(line),
        _TRUNCATE,
        "{\"kind\":\"poly_meta\",\"polyMeta\":%u,\"shift17\":%u,\"decoded\":%d,"
        "\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sceneId\":%d,\"mapToken\":%d}",
        polyMeta,
        shift17,
        decoded,
        static_cast<double>(px),
        static_cast<double>(py),
        static_cast<double>(pz),
        sceneId,
        mapToken);
    WriteTraceLine(line);
}

static bool OpenSessionFile() {
    char dirPath[MAX_PATH] = {};
    if (!ModuleRelativePath("field-scout\\", dirPath, sizeof(dirPath))) return false;
    CreateDirectoryA(dirPath, nullptr);

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    _snprintf_s(
        g_sessionPath,
        sizeof(g_sessionPath),
        _TRUNCATE,
        "%ssession-%04u%02u%02u-%02u%02u%02u.jsonl",
        dirPath,
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond);

    g_session = fopen(g_sessionPath, "wb");
    if (!g_session) return false;

    if (g_heavy) {
        _snprintf_s(
            g_tracePath,
            sizeof(g_tracePath),
            _TRUNCATE,
            "%ssession-%04u%02u%02u-%02u%02u%02u-trace.jsonl",
            dirPath,
            st.wYear,
            st.wMonth,
            st.wDay,
            st.wHour,
            st.wMinute,
            st.wSecond);
        g_traceSession = fopen(g_tracePath, "wb");
    }

    char escPath[600] = {};
    JsonEscape(g_sessionPath, escPath, sizeof(escPath));
    char escTrace[600] = {};
    if (g_tracePath[0]) JsonEscape(g_tracePath, escTrace, sizeof(escTrace));
    char fullHeader[1400] = {};
    const char* modeStr = g_max ? "max_heavy_world_walk"
                          : (g_ultra.master ? "ultra_heavy_world_walk"
                          : (g_heavy ? "heavy_world_walk" : "world_walk"));
    _snprintf_s(
        fullHeader,
        sizeof(fullHeader),
        _TRUNCATE,
        "{\"kind\":\"session_start\",\"scoutVersion\":8,\"mode\":\"%s\",\"mapOnly\":%s,\"geometry\":true,"
        "\"dedupeCap\":%u,\"heavy\":%s,\"ultra\":%s,\"max\":%s,"
        "\"ultraFieldLogic\":%s,\"ultraCollision\":%s,\"ultraEncounters\":%s,"
        "\"ultraSceneEnv\":%s,\"ultraPipelineHints\":%s,"
        "\"traceFile\":\"%s\",\"base\":\"0x%08X\",\"sessionFile\":\"%s\"}",
        modeStr,
        g_mapOnly ? "true" : "false",
        g_maxUniquePaths,
        g_heavy ? "true" : "false",
        g_ultra.master ? "true" : "false",
        g_max ? "true" : "false",
        g_ultra.fieldLogic ? "true" : "false",
        g_ultra.collision ? "true" : "false",
        g_ultra.encounters ? "true" : "false",
        g_ultra.sceneEnv ? "true" : "false",
        g_ultra.pipelineHints ? "true" : "false",
        escTrace,
        static_cast<unsigned>(g_base),
        escPath);
    WriteJsonLine(fullHeader);

    if (g_ultra.master) {
        if (UltraActive(g_ultra.collision)) {
            RecordUltraStub("collision", "blocked", "walkmesh decode not shipped — flag armed for future hook");
        }
        if (UltraActive(g_ultra.fieldLogic) && !MaxActive(g_ultra.fieldLogic)) {
            RecordUltraStub(
                "field_logic",
                "partial",
                "npc_spawn/trigger_spawn heuristics; warpToPoint/takara/chest-state/doors need ATEL+save RE");
        }
    }
    return true;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK

static int __cdecl BuildTextureSlot_FieldScoutHook(
    int64_t a0,
    int a1,
    int a2,
    uint32_t* a3,
    char* source,
    int a5) {
    if (source && source[0]) {
        NoteBattleTransitionFromPath(source);
    }
    if (ShouldSkipFieldScoutCapture()) {
        return g_textureTrampoline(a0, a1, a2, a3, source, a5);
    }
    if (source && PathLooksInteresting(source)) {
        unsigned polyMeta = 0;
        bool hasPoly = false;
        if (g_heavy && a3) {
            __try {
                polyMeta = *a3;
                hasPoly = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                hasPoly = false;
            }
        }
        RecordManifestAssetWithMeta("asset", source, 0, nullptr, polyMeta, hasPoly, a1, a2, a5);
    }
    return g_textureTrampoline(a0, a1, a2, a3, source, a5);
}

static void __cdecl GraphicFieldMapLoad_FieldScoutHook(char* mapPath, int slot) {
    char area[32] = {};
    char field[32] = {};
    bool haveField = false;

    if (mapPath && mapPath[0]) {
        NoteBattleTransitionFromPath(mapPath);
        if (!ShouldSkipFieldScoutCapture()) {
            RecordManifestAsset("field_load", mapPath, slot, "slot");
        }
        if (ExtractMapFieldLoose(mapPath, area, sizeof(area), field, sizeof(field))) {
            SetCurrentMapField(area, field);
            haveField = true;
        }
    }

    /* g_heavyActive must be set BEFORE the trampoline fires — CommitInstanceMappings
       and WireInstanceToSceneNodes are called by the engine during the original load,
       and they check g_heavyActive to decide whether to emit JSONL.
       ApplyQueuedHooks was previously called here (inside the field-load hook on the
       main thread), which caused deadlocks because MH_ApplyQueued suspends ALL threads
       and some hold locks the field-load thread needs. The deferred apply now runs
       exclusively on the worker thread via ApplyFieldScoutQueuedHooks() from dllmain. */
    if (g_heavy && !IsTitleBootField(field)) {
        if (!InterlockedCompareExchange(&g_heavyActive, 1, 0)) {
            StartPlayerTraceThread();
            HookLog("[ffx-hooks] FieldScout heavy hooks active");
        }
    }

    /* Run original load */
    g_fieldLoadTrampoline(mapPath, slot);

    if (haveField && !IsTitleBootField(field) && !ShouldSkipFieldScoutCapture()) {
        EmitSyntheticFieldPack(area, field);
        EmitUltraFieldLoadSamples("field_load");
    }
}

static void __fastcall LoadAndActivateDriver_FieldScoutHook(
    void* self,
    void* /*edx*/,
    char* assetPath,
    int mode) {
    if (assetPath && assetPath[0]) {
        NoteBattleTransitionFromPath(assetPath);
    }
    if (!ShouldSkipFieldScoutCapture() &&
        assetPath && assetPath[0] && PathLooksLikePs3Asset(assetPath) && !IsTitleBootField(g_currentField)) {
        const char* kind = PathLooksLikeGeometry(assetPath) ? "geometry" : "asset";
        RecordManifestAsset(kind, assetPath, mode, "mode");
    }
    g_activateTrampoline(self, assetPath, mode);
}

static char* __fastcall GetInstanceNameByIndex_FieldScoutHook(
    void* container,
    void* /*edx*/,
    int index) {
    char* name = g_instanceNameTrampoline(container, index);
    if (ShouldSkipFieldScoutCapture() || !name || !name[0] || IsTitleBootField(g_currentField)) return name;
    if (name && name[0] && (g_heavy || SceneNodeNameInteresting(name))) {
        RecordManifestAsset("scene_node", name, index, "index");
        if (g_heavy) {
            float wx = 0.0f, wy = 0.0f, wz = 0.0f;
            bool hasWorld = false;
            void* node = ResolvePNodeFromContainer(container, index);
            if (node && ComposeWorldTranslationFromNode(node, &wx, &wy, &wz)) {
                hasWorld = true;
                RecordSceneNodePlaced(name, index, wx, wy, wz, hasWorld, "instance_index");
            }
        }
    }
    return name;
}



/* Resolve slot index from chr instance pointer by scanning the active CHR table.
   Returns 0xFFFFFFFF if not found or on error. O(n) scan for n = active CHR count (<4096). */
static uint32_t FindChrSlotByHandle(const void* inst) {
    if (!g_base || !inst) return 0xFFFFFFFFu;
    if (!g_heavy) return 0xFFFFFFFFu;
    __try {
        const uint32_t count = *reinterpret_cast<const uint32_t*>(g_base + RVA_FFX_ACTIVE_CHR_INSTANCE_COUNT);
        const uint32_t table = *reinterpret_cast<const uint32_t*>(g_base + RVA_FFX_ACTIVE_CHR_INSTANCE_TABLE);
        if (count == 0 || count > 4096 || !IsPlausiblePtr(reinterpret_cast<const void*>(static_cast<uintptr_t>(table)))) {
            return 0xFFFFFFFFu;
        }
        for (uint32_t idx = 0; idx < count; ++idx) {
            const char* entry = reinterpret_cast<const char*>(static_cast<uintptr_t>(table) + idx * FFX_ACTIVE_CHR_INSTANCE_STRIDE);
            if (reinterpret_cast<const void*>(entry) == inst) {
                return idx;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0xFFFFFFFFu;
}
static int __cdecl ChrSetWorldPosition_FieldScoutHook(
    int instHandle,
    float x,
    float y,
    float z) {
    if (g_heavy && instHandle && FloatLooksSane(x) && FloatLooksSane(y) && FloatLooksSane(z)) {
        __try {
            const void* inst = reinterpret_cast<const void*>(instHandle);
            const uint16_t chrId = *reinterpret_cast<const uint16_t*>(reinterpret_cast<const char*>(inst) + FFX_CHR_INSTANCE_ID_OFFSET);
            const char* chrName = nullptr;
            const char* const namePtr = *reinterpret_cast<const char* const*>(
                reinterpret_cast<const char*>(inst) + FFX_CHR_INSTANCE_NAME_PTR_OFFSET);
            if (IsPlausiblePtr(namePtr)) chrName = namePtr;
            const uint32_t slotIndex = FindChrSlotByHandle(inst);
            RecordChrSpawn(slotIndex, chrId, chrName, x, y, z, "set_world_pos");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return g_chrSetPosTrampoline(instHandle, x, y, z);
}

static int __cdecl MsBattleEncountExe_QuiesceHook(int selector, int group, float walkDelta) {
    InterlockedExchange(&g_captureQuiesced, 1);
    return g_encounterTrampoline(selector, group, walkDelta);
}



static void* __cdecl TakaraLoad_MaxHook(int takaraIndex) {
    if (MaxActive(g_ultra.fieldLogic)) {
        RecordMaxTakara(takaraIndex, "atel_takara_load");
    }
    return g_takaraLoadTrampoline(takaraIndex);
}

static int __cdecl WarpActor_MaxHook(int actor, float x, float y, float z, int snap) {
    if (MaxActive(g_ultra.fieldLogic)) {
        RecordMaxWarp(x, y, z, "warp_actor", snap);
    }
    return g_warpActorTrampoline(actor, x, y, z, snap);
}

static int __cdecl SampleZoneSlot_MaxHook(int zoneRoot, int slotIndex) {
    const int groupByte = g_sampleZoneTrampoline(zoneRoot, slotIndex);
    if (MaxActive(g_ultra.encounters) && slotIndex >= 0 && slotIndex < 8 && groupByte >= 0) {
        RecordMaxZoneSlot(slotIndex, groupByte);
    }
    return groupByte;
}

static void __fastcall WireInstanceToSceneNodes_FieldScoutHook(
    void* thisPtr,
    void* a2_edx,
    int a3,
    int a4,
    void* a5,
    void* a6,
    void* a7,
    int a8,
    bool a9) {
    if (!ShouldSkipFieldScoutCapture() && g_heavy && InterlockedCompareExchange(&g_heavyActive, 0, 0) && thisPtr) {
        char line[256] = {};
        _snprintf_s(line, sizeof(line), _TRUNCATE,
            "{\"kind\":\"wire_instance\",\"this@%p\":\"\",\"edx@%p\":\"\",\"a3\":%d,\"a4\":%d,\"a5@%p\":\"\",\"a6@%p\":\"\"}",
            thisPtr, a2_edx, a3, a4, a5, a6);
        WriteTraceLine(line);
    }
    g_wireInstanceTrampoline(thisPtr, a2_edx, a3, a4, a5, a6, a7, a8, a9);
}

static int __fastcall FieldMap_CommitInstanceMappings_Hook(
    void* thisPtr,
    void* a2_edx,
    int a3,
    int a4,
    int a5,
    void* a6) {
    int result = 0;
    if (!ShouldSkipFieldScoutCapture() && g_heavy && InterlockedCompareExchange(&g_heavyActive, 0, 0) && thisPtr && a6) {
        char line[256] = {};
        _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "{\"kind\":\"wire_commit\",\"this@%p\":\"\",\"edx@%p\":\"\",\"a3\":%d,\"a4\":%d,\"a5\":%d,\"a6@%p\":\"\"}",
            thisPtr, a2_edx, a3, a4, a5, a6);
        WriteTraceLine(line);
    }
    __try {
        result = g_commitMappingsTrampoline(thisPtr, a2_edx, a3, a4, a5, a6);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return result;
}


static bool InstallDetour(
    uintptr_t targetVa,
    uint64_t* trampolineOut,
    void* hookFn,
    void** origOut,
    const char* label) {
    /* Create hook (stores original bytes, does NOT write jmp) */
    if (MH_CreateHook(reinterpret_cast<void*>(targetVa), hookFn, reinterpret_cast<void**>(origOut)) != MH_OK) {
        HookLog("[ffx-hooks] ERROR FieldScout %s MH_CreateHook failed @0x%08X", label, static_cast<unsigned>(targetVa));
        return false;
    }
    /* Queue for batch activation (MH_ApplyQueued suspends all threads, writes all jmps atomically) */
    if (MH_QueueEnableHook(reinterpret_cast<void*>(targetVa)) != MH_OK) {
        HookLog("[ffx-hooks] ERROR FieldScout %s MH_QueueEnableHook failed @0x%08X", label, static_cast<unsigned>(targetVa));
        MH_RemoveHook(reinterpret_cast<void*>(targetVa));
        return false;
    }
    if (trampolineOut) *trampolineOut = reinterpret_cast<uint64_t>(*origOut);
    HookLog("[ffx-hooks] FieldScout %s queued @0x%08X trampoline=0x%p", label, static_cast<unsigned>(targetVa), *origOut);
    return true;
}

static void UnhookDetour() {
    /* MinHook manages hook removal via MH_DisableHook/MH_RemoveHook during cleanup.
       Individual unhooks are not needed since RemoveFieldScoutHook calls MH_Uninitialize. */
}

/* Apply all queued MinHook hooks — suspends all threads, writes jmps, relocates EIPs, resumes threads. */
static bool ApplyQueuedHooks(const char* context) {
    if (g_minhookQueued) return true;
    if (MH_ApplyQueued() != MH_OK) {
        HookLog("[ffx-hooks] ERROR FieldScout MH_ApplyQueued (%s) failed", context);
        return false;
    }
    g_minhookQueued = true;
    HookLog("[ffx-hooks] FieldScout MH_ApplyQueued (%s) OK", context);
    return true;
}


/* Heavy hooks installed at boot (not deferred). g_heavyActive gates logging on field load.
   No deferred thread = no race condition with field transitions. */

#endif // FFXHOOKS_HAVE_POLYHOOK

} // namespace

FieldScoutInstallResult InstallFieldScoutHook(
    uintptr_t moduleBase,
    void* moduleHandle,
    bool mapTexturesOnly,
    bool heavyMode,
    FieldScoutUltraOptions ultraOptions,
    bool maxMode,
    FieldScoutLogFn log) {
    FieldScoutInstallResult result = {};
    g_base = moduleBase;
    g_module = moduleHandle;
    g_logFn = log;
    g_mapOnly = mapTexturesOnly;
    g_heavy = heavyMode;
    g_ultra = heavyMode ? ultraOptions : FieldScoutUltraOptions{};
    g_max = heavyMode && g_ultra.master && maxMode;
    if (!g_ultra.master) {
        g_ultra.fieldLogic = false;
        g_ultra.collision = false;
        g_ultra.encounters = false;
        g_ultra.sceneEnv = false;
        g_ultra.pipelineHints = false;
    }
    g_maxUniquePaths = heavyMode ? (g_ultra.master ? 2000000u : 1000000u) : 250000u;
    g_seenCount = 0;
    g_totalHits = 0;
    g_uniqueAssets = 0;
    g_droppedDedupe = 0;
    g_traceSamples = 0;
    g_tracePath[0] = '\0';
    g_traceSession = nullptr;

    EnsureLock();

#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_installed)
        RemoveFieldScoutHook(log);

    InterlockedExchange(&g_shuttingDown, 0);
    InterlockedExchange(&g_heavyActive, 0);
    InterlockedExchange(&g_captureQuiesced, 0);

    if (!EnsureSeenStore()) {
        if (log) log("[ffx-hooks] FieldScout failed to allocate dedupe store");
        return result;
    }

    if (!OpenSessionFile()) {
        if (log) log("[ffx-hooks] FieldScout failed to open session file");
        return result;
    }

    /* Initialize MinHook (must be before any MH_CreateHook) */
    g_minhookQueued = false;
    if (MH_Initialize() != MH_OK) {
        if (log) log("[ffx-hooks] ERROR FieldScout MH_Initialize failed");
        if (g_session) { fclose(g_session); g_session = nullptr; }
        return result;
    }

    unsigned hooked = 0;

    if (InstallDetour(
            moduleBase + RVA_FFX_PSDATA_BUILD_TEXTURE_SLOT_LOADTIME,
            &g_textureTrampolineVa,
            reinterpret_cast<void*>(&BuildTextureSlot_FieldScoutHook),
            reinterpret_cast<void**>(&g_textureTrampoline),
                "BuildTextureSlotLoadTime"))
        ++hooked;

    if (InstallDetour(
            moduleBase + RVA_FFX_FIELDMAP_LOAD_ENTRY_GRAPHIC_FIELDMAP,
            &g_fieldLoadTrampolineVa,
            reinterpret_cast<void*>(&GraphicFieldMapLoad_FieldScoutHook),
            reinterpret_cast<void**>(&g_fieldLoadTrampoline),
                "GraphicFieldMapLoad"))
        ++hooked;

    if (InstallDetour(
            moduleBase + RVA_FFX_FIELDMAP_LOAD_AND_ACTIVATE_DRIVER,
            &g_activateTrampolineVa,
            reinterpret_cast<void*>(&LoadAndActivateDriver_FieldScoutHook),
            reinterpret_cast<void**>(&g_activateTrampoline),
                "LoadAndActivateDriver"))
        ++hooked;

    if (InstallDetour(
            moduleBase + RVA_FFX_PHYRE_GET_INSTANCE_NAME_BY_INDEX,
            &g_instanceNameTrampolineVa,
            reinterpret_cast<void*>(&GetInstanceNameByIndex_FieldScoutHook),
            reinterpret_cast<void**>(&g_instanceNameTrampoline),
                "GetInstanceNameByIndex"))
        ++hooked;

    /* Heavy hooks — created at boot (MH_CreateHook is safe, no jmp written yet).
       Enabled via MH_ApplyQueued on first field load (suspends all threads, writes jmps atomically).

       ComposeWorldMatrix (RVA 0x1067C0): per-frame scene-graph compose.
       Uses __declspec(naked) + jmp tail-call because the original uses retn 4
       (callee-pops the stack arg). A __fastcall wrapper would do ret 4 + the
       original's retn 4 = double-pop stack corruption. Naked jmp avoids any
       C++ frame — the trampoline returns directly to the game caller.

       SetupSceneNode (RVA 0x2F6D40) is NOT hooked: IDA proved it is
       FFX_FieldMap_BindMaterialTextureSampler (binds TextureSampler into
       material param buffers, 3 args: fieldmap, &materialSlot, shadowFlag).
       It is NOT a PNode setup function. WireInstanceToSceneNodes (0x25B0F0,
       already hooked) covers scene graph wiring at field load time.

       ComposeWorldMatrix is NOT hooked — it is called every frame and even
       a naked jmp trampoline interferes with MinHook's thread suspension during
       MH_ApplyQueued. The passthrough hook provides zero data value. */
    if (g_heavy) {
        if (InstallDetour(
                moduleBase + RVA_FFX_BATTLE_ENCOUNTER_EXE,
                &g_encounterTrampolineVa,
                reinterpret_cast<void*>(&MsBattleEncountExe_QuiesceHook),
                reinterpret_cast<void**>(&g_encounterTrampoline),
                "MsBattleEncountExeQuiesce"))
            ++hooked;

        if (InstallDetour(
                moduleBase + RVA_FFX_FIELDMAP_WIRE_INSTANCE_TO_SCENE_NODES,
                &g_wireInstanceTrampolineVa,
                reinterpret_cast<void*>(&WireInstanceToSceneNodes_FieldScoutHook),
                reinterpret_cast<void**>(&g_wireInstanceTrampoline),
                "WireInstanceToSceneNodes"))
            ++hooked;

        if (InstallDetour(
                moduleBase + RVA_FFX_FIELDMAP_COMMIT_INSTANCE_MAPPINGS,
                &g_commitMappingsTrampolineVa,
                reinterpret_cast<void*>(&FieldMap_CommitInstanceMappings_Hook),
                reinterpret_cast<void**>(&g_commitMappingsTrampoline),
                "CommitInstanceMappings"))
            ++hooked;

        /* ChrSetWorldPosition: captures CHR spawns (party/NPC/monster placement).
           ABI FIXED (W1): original is __cdecl retn 0 (NOT __thiscall).
           Oracle-validated: int __cdecl(int instHandle, float x, float y, float z) @ 0x82B500. */
        if (InstallDetour(
                moduleBase + RVA_FFX_CHR_SET_WORLD_POSITION,
                &g_chrSetPosTrampolineVa,
                reinterpret_cast<void*>(&ChrSetWorldPosition_FieldScoutHook),
                reinterpret_cast<void**>(&g_chrSetPosTrampoline),
                "ChrSetWorldPosition"))
            ++hooked;
    }

    /* MAX-mode hooks: ATEL treasure load, actor warp, encounter zone slot.
       All RVAs Oracle-validated: __cdecl, retn 0.
       Installed in main path (W4) — replaces deprecated InstallFieldScoutHeavyDetours. */
    if (g_max) {
        if (MaxActive(g_ultra.fieldLogic)) {
            if (InstallDetour(
                    moduleBase + RVA_FFX_ATEL_LOAD_TAKARA_ROW,
                    &g_takaraLoadTrampolineVa,
                    reinterpret_cast<void*>(&TakaraLoad_MaxHook),
                    reinterpret_cast<void**>(&g_takaraLoadTrampoline),
                "AtelLoadTakaraRow"))
                ++hooked;

            if (InstallDetour(
                    moduleBase + RVA_FFX_FIELD_WARP_ACTOR_TO_POSITION,
                    &g_warpActorTrampolineVa,
                    reinterpret_cast<void*>(&WarpActor_MaxHook),
                    reinterpret_cast<void**>(&g_warpActorTrampoline),
                "FieldWarpActorToPosition"))
                ++hooked;
        }

        if (MaxActive(g_ultra.encounters)) {
            if (InstallDetour(
                    moduleBase + RVA_FFX_FIELD_SAMPLE_ENCOUNTER_ZONE_SLOT,
                    &g_sampleZoneTrampolineVa,
                    reinterpret_cast<void*>(&SampleZoneSlot_MaxHook),
                    reinterpret_cast<void**>(&g_sampleZoneTrampoline),
                "FieldSampleEncounterZoneSlot"))
                ++hooked;
        }
    }

    /* Apply is deferred — MH_ApplyQueued at boot causes deadlocks (MinHook suspends all threads;
       some hold locks our init thread needs). The dllmain HooksWorkerThread calls InstallFieldScoutHook,
       then arms heavy hooks from a timer. We let that same thread do ApplyQueued after boot settles. */

    if (hooked > 0) {
        g_installed = true;
        result.ok = true;
        _snprintf_s(result.sessionPath, sizeof(result.sessionPath), _TRUNCATE, "%s", g_sessionPath);
        result.uniqueAssets = 0;
        HookLog("[ffx-hooks] FieldScout armed session=%s mapOnly=%d heavy=%d ultra=%d max=%d hooks=%u dedupe=%u "
                  "ultra(fl=%d col=%d enc=%d env=%d pipe=%d)",
            g_sessionPath,
            mapTexturesOnly ? 1 : 0,
            heavyMode ? 1 : 0,
            g_ultra.master ? 1 : 0,
            g_max ? 1 : 0,
            hooked,
            g_maxUniquePaths,
            g_ultra.fieldLogic ? 1 : 0,
            g_ultra.collision ? 1 : 0,
            g_ultra.encounters ? 1 : 0,
            g_ultra.sceneEnv ? 1 : 0,
            g_ultra.pipelineHints ? 1 : 0);
    } else if (g_session) {
        if (g_traceSession) { fclose(g_traceSession); g_traceSession = nullptr; }
        fclose(g_session);
        g_session = nullptr;
    }
#else
    if (log) log("[ffx-hooks] WARN FieldScout requires FFXHOOKS_HAVE_POLYHOOK");
#endif
    return result;
}

bool ApplyFieldScoutQueuedHooks(FieldScoutLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    return ApplyQueuedHooks("worker");
#else
    return false;
#endif
}

bool RemoveFieldScoutHook(FieldScoutLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    InterlockedExchange(&g_shuttingDown, 1);
    InterlockedExchange(&g_heavyActive, 0);
    g_minhookQueued = false;
    StopPlayerTraceThread();
    /* MinHook: MH_Uninitialize disables ALL hooks and frees resources atomically */
    MH_Uninitialize();
    /* Reset trampoline pointers (MinHook nulls them on Uninitialize, but be safe) */
    g_textureTrampoline = nullptr;
    g_fieldLoadTrampoline = nullptr;
    g_activateTrampoline = nullptr;
    g_instanceNameTrampoline = nullptr;
    g_encounterTrampoline = nullptr;
    g_chrSetPosTrampoline = nullptr;
    g_takaraLoadTrampoline = nullptr;
    g_warpActorTrampoline = nullptr;
    g_sampleZoneTrampoline = nullptr;
    g_wireInstanceTrampoline = nullptr;
    g_commitMappingsTrampoline = nullptr;
#endif

    Sleep(50);

    FILE* sessionToClose = nullptr;
    FILE* traceToClose = nullptr;
    EnterCriticalSection(&g_lock);
    sessionToClose = g_session;
    traceToClose = g_traceSession;
    g_session = nullptr;
    g_traceSession = nullptr;
    LeaveCriticalSection(&g_lock);

    if (sessionToClose) {
        char footer[420] = {};
        _snprintf_s(
            footer,
            sizeof(footer),
            _TRUNCATE,
            "{\"kind\":\"session_end\",\"totalHits\":%ld,\"uniqueAssets\":%ld,\"droppedDedupe\":%ld,\"traceSamples\":%ld}",
            g_totalHits,
            g_uniqueAssets,
            g_droppedDedupe,
            g_traceSamples);
        WriteJsonLineToClosingFile(sessionToClose, footer);
        if (traceToClose) {
            char traceFooter[280] = {};
            _snprintf_s(
                traceFooter,
                sizeof(traceFooter),
                _TRUNCATE,
                "{\"kind\":\"trace_end\",\"traceSamples\":%ld}",
                g_traceSamples);
            WriteJsonLineToClosingFile(traceToClose, traceFooter);
            fflush(traceToClose);
            fclose(traceToClose);
        }
        fflush(sessionToClose);
        fclose(sessionToClose);
        if (log) {
            char msg[640] = {};
            _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                "[ffx-hooks] FieldScout session closed unique=%ld hits=%ld trace=%ld path=%s",
                g_uniqueAssets, g_totalHits, g_traceSamples, g_sessionPath);
            log(msg);
        }
    }

    FreeSeenStore();
    g_installed = false;
    return true;
}

bool IsFieldScoutHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
