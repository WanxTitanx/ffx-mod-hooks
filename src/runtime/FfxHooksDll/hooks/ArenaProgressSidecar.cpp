// Arena+ progress sidecar implementation (Fase 6). See header for contract.

#include "ArenaProgressSidecar.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace FfxHooks {

namespace {

constexpr int kMaxFlags = 128;
constexpr size_t kFlagKeyCap = 96;
constexpr size_t kEvidenceCap = 192;
constexpr size_t kStringPoolCap = 16 * 1024;
constexpr size_t kFileScratchCap = 64 * 1024;

struct FlagEntry {
    char        key[kFlagKeyCap];
    bool        cleared;
    char        firstClearUtc[40];
    char        lastClearUtc[40];
    int         clearCount;
    const char* evidence; // nullable; pool-owned
};

struct ModuleState {
    bool        enabled;
    bool        initialized;
    char        sidecarPath[MAX_PATH];
    FlagEntry   flags[kMaxFlags];
    int         flagsUsed;
    char        stringPool[kStringPoolCap];
    size_t      stringPoolUsed;
    ArenaProgressLogFn logger;
};

ModuleState g_state = {};

void LogLine(const char* fmt, ...) {
    if (!g_state.logger) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    if (n < 0) n = static_cast<int>(strlen(buf));
    g_state.logger(buf);
}

bool EnvFlagOn(const char* name) {
    char buf[16] = {};
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return false;
    return buf[0] == '1' || buf[0] == 't' || buf[0] == 'T' || buf[0] == 'y' || buf[0] == 'Y';
}

bool FileExists(const char* path) {
    if (!path || !path[0]) return false;
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void GetModuleDir(char* out, size_t outLen) {
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&GetModuleDir), &mod);
    char path[MAX_PATH] = {};
    GetModuleFileNameA(mod, path, MAX_PATH);
    lstrcpynA(out, path, static_cast<int>(outLen));
    char* slash = strrchr(out, '\\');
    if (slash) *slash = '\0';
}

void ResolveSidecarPath(char* out, size_t outLen) {
    DWORD n = GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_PROGRESS_PATH", out, static_cast<DWORD>(outLen));
    if (n > 0 && n < outLen) return;

    char dir[MAX_PATH] = {};
    GetModuleDir(dir, sizeof(dir));
    _snprintf_s(out, outLen, _TRUNCATE,
                "%s\\mods\\Spira Reforge\\arena\\progress\\spira-arena-progress.json", dir);
    if (FileExists(out)) return;
    _snprintf_s(out, outLen, _TRUNCATE, "%s\\spira-arena-progress.json", dir);
}

bool ModuleFlagOn(const char* relPath) {
    char dir[MAX_PATH] = {};
    GetModuleDir(dir, sizeof(dir));
    char full[MAX_PATH] = {};
    _snprintf_s(full, sizeof(full), _TRUNCATE, "%s\\%s", dir, relPath);
    return FileExists(full);
}

bool GateOn() {
    return EnvFlagOn("FFXHOOKS_ENABLE_ARENA_PLUS_PROGRESS") ||
           ModuleFlagOn("arena_plus_progress.flag") ||
           ModuleFlagOn("config\\arena_plus_progress.flag");
}

const char* InternString(const char* src) {
    if (!src) return nullptr;
    size_t len = strlen(src);
    if (len + 1 > kStringPoolCap - g_state.stringPoolUsed) return nullptr;
    char* dst = g_state.stringPool + g_state.stringPoolUsed;
    memcpy(dst, src, len + 1);
    g_state.stringPoolUsed += len + 1;
    return dst;
}

int FindFlag(const char* key) {
    if (!key || !key[0]) return -1;
    for (int i = 0; i < g_state.flagsUsed; ++i) {
        if (_stricmp(g_state.flags[i].key, key) == 0) return i;
    }
    return -1;
}

int InsertFlag(const char* key) {
    if (g_state.flagsUsed >= kMaxFlags) return -1;
    int i = g_state.flagsUsed++;
    FlagEntry& e = g_state.flags[i];
    lstrcpynA(e.key, key, kFlagKeyCap);
    e.cleared = false;
    e.firstClearUtc[0] = '\0';
    e.lastClearUtc[0] = '\0';
    e.clearCount = 0;
    e.evidence = nullptr;
    return i;
}

// JSON scanner helpers (defensive, single-line / single-row aware).
bool ReadStringField(const char* start, const char* end, const char* key, char* out, size_t outLen) {
    if (!start || !end || !key || !out || outLen < 2) return false;
    char needle[64];
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* hit = strstr(start, needle);
    if (!hit || hit >= end) return false;
    const char* p = hit + strlen(needle);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p != '"') return false;
    ++p;
    size_t i = 0;
    while (p < end && *p != '"' && i + 1 < outLen) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

bool ReadBoolField(const char* start, const char* end, const char* key, bool* out) {
    if (!start || !end || !key || !out) return false;
    char needle[64];
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* hit = strstr(start, needle);
    if (!hit || hit >= end) return false;
    const char* p = hit + strlen(needle);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p + 4 <= end && strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (p + 5 <= end && strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

bool ReadIntField(const char* start, const char* end, const char* key, int* out) {
    if (!start || !end || !key || !out) return false;
    char needle[64];
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* hit = strstr(start, needle);
    if (!hit || hit >= end) return false;
    const char* p = hit + strlen(needle);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    int sign = 1;
    if (p < end && *p == '-') { sign = -1; ++p; }
    int v = 0;
    bool seen = false;
    while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; seen = true; }
    if (!seen) return false;
    *out = sign * v;
    return true;
}

bool ReadFileBytes(const char* path, char* buf, size_t bufLen, size_t* readBytes) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart >= static_cast<LONGLONG>(bufLen)) {
        CloseHandle(h);
        return false;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, static_cast<DWORD>(size.QuadPart), &got, nullptr);
    CloseHandle(h);
    if (!ok || got == 0) return false;
    buf[got] = '\0';
    if (readBytes) *readBytes = got;
    return true;
}

bool ParseSidecar(const char* json, size_t jsonLen) {
    if (!json || jsonLen == 0) return false;
    const char* end = json + jsonLen;
    const char* flagsKey = strstr(json, "\"flags\"");
    if (!flagsKey || flagsKey >= end) return true; // valid file with no flags

    const char* objStart = strchr(flagsKey, '{');
    if (!objStart) return true;
    int depth = 1;
    const char* cursor = objStart + 1;
    while (cursor < end && depth > 0) {
        // Find a key "..." :
        const char* keyStart = nullptr;
        while (cursor < end && depth > 0) {
            if (*cursor == '{') ++depth;
            else if (*cursor == '}') { --depth; if (depth == 0) break; }
            else if (*cursor == '"') { keyStart = cursor; break; }
            ++cursor;
        }
        if (depth == 0 || !keyStart) break;
        const char* keyEnd = strchr(keyStart + 1, '"');
        if (!keyEnd || keyEnd >= end) break;
        char flagKey[kFlagKeyCap] = {};
        size_t n = static_cast<size_t>(keyEnd - keyStart - 1);
        if (n >= kFlagKeyCap) n = kFlagKeyCap - 1;
        memcpy(flagKey, keyStart + 1, n);
        flagKey[n] = '\0';

        const char* valStart = strchr(keyEnd + 1, '{');
        if (!valStart || valStart >= end) break;
        int vdepth = 1;
        const char* valEnd = valStart + 1;
        while (valEnd < end && vdepth > 0) {
            if (*valEnd == '{') ++vdepth;
            else if (*valEnd == '}') --vdepth;
            if (vdepth == 0) break;
            ++valEnd;
        }
        if (vdepth != 0) break;

        bool cleared = false;
        int clearCount = 0;
        char firstClear[40] = {};
        char lastClear[40] = {};
        char evidence[kEvidenceCap] = {};
        ReadBoolField(valStart, valEnd, "cleared", &cleared);
        ReadIntField(valStart, valEnd, "clear_count", &clearCount);
        ReadStringField(valStart, valEnd, "first_clear_utc", firstClear, sizeof(firstClear));
        ReadStringField(valStart, valEnd, "last_clear_utc", lastClear, sizeof(lastClear));
        ReadStringField(valStart, valEnd, "evidence", evidence, sizeof(evidence));

        int idx = FindFlag(flagKey);
        if (idx < 0) idx = InsertFlag(flagKey);
        if (idx >= 0) {
            g_state.flags[idx].cleared = cleared;
            g_state.flags[idx].clearCount = clearCount;
            lstrcpynA(g_state.flags[idx].firstClearUtc, firstClear, sizeof(g_state.flags[idx].firstClearUtc));
            lstrcpynA(g_state.flags[idx].lastClearUtc, lastClear, sizeof(g_state.flags[idx].lastClearUtc));
            if (evidence[0]) g_state.flags[idx].evidence = InternString(evidence);
        }

        cursor = valEnd + 1;
    }
    return true;
}

void IsoUtcNow(char* out, size_t outLen) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    _snprintf_s(out, outLen, _TRUNCATE, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

bool WriteSidecar() {
    if (!g_state.sidecarPath[0]) return false;
    char tmpPath[MAX_PATH];
    _snprintf_s(tmpPath, sizeof(tmpPath), _TRUNCATE, "%s.tmp", g_state.sidecarPath);
    HANDLE h = CreateFileA(tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LogLine("[ffx-hooks] ArenaPlus progress write open failed (path=%s err=%lu)\n",
                tmpPath, GetLastError());
        return false;
    }
    char nowBuf[40];
    IsoUtcNow(nowBuf, sizeof(nowBuf));

    auto writeStr = [&](const char* s) {
        DWORD wrote = 0;
        WriteFile(h, s, static_cast<DWORD>(strlen(s)), &wrote, nullptr);
    };

    char header[256];
    _snprintf_s(header, sizeof(header), _TRUNCATE,
                "{\n  \"$schema\": \"./spira-arena-progress.schema.json\",\n"
                "  \"format\": \"spira-arena-progress\",\n  \"format_version\": 1,\n"
                "  \"profile_key\": \"default\",\n  \"updated_utc\": \"%s\",\n  \"flags\": {",
                nowBuf);
    writeStr(header);

    bool first = true;
    char row[1024];
    for (int i = 0; i < g_state.flagsUsed; ++i) {
        const FlagEntry& e = g_state.flags[i];
        const char* sep = first ? "\n" : ",\n";
        const char* first_iso = e.firstClearUtc[0] ? e.firstClearUtc : "null-placeholder";
        const char* last_iso = e.lastClearUtc[0] ? e.lastClearUtc : "null-placeholder";
        const bool first_null = e.firstClearUtc[0] == '\0';
        const bool last_null = e.lastClearUtc[0] == '\0';
        if (e.evidence) {
            _snprintf_s(row, sizeof(row), _TRUNCATE,
                        "%s    \"%s\": { \"cleared\": %s, \"clear_count\": %d, "
                        "\"first_clear_utc\": %s%s%s, \"last_clear_utc\": %s%s%s, "
                        "\"evidence\": \"%s\" }",
                        sep, e.key, e.cleared ? "true" : "false", e.clearCount,
                        first_null ? "null" : "\"", first_null ? "" : first_iso, first_null ? "" : "\"",
                        last_null ? "null" : "\"", last_null ? "" : last_iso, last_null ? "" : "\"",
                        e.evidence);
        } else {
            _snprintf_s(row, sizeof(row), _TRUNCATE,
                        "%s    \"%s\": { \"cleared\": %s, \"clear_count\": %d, "
                        "\"first_clear_utc\": %s%s%s, \"last_clear_utc\": %s%s%s }",
                        sep, e.key, e.cleared ? "true" : "false", e.clearCount,
                        first_null ? "null" : "\"", first_null ? "" : first_iso, first_null ? "" : "\"",
                        last_null ? "null" : "\"", last_null ? "" : last_iso, last_null ? "" : "\"");
        }
        writeStr(row);
        (void)first_iso; (void)last_iso;
        first = false;
    }
    writeStr(first ? "}\n}\n" : "\n  }\n}\n");
    CloseHandle(h);

    if (!MoveFileExA(tmpPath, g_state.sidecarPath, MOVEFILE_REPLACE_EXISTING)) {
        LogLine("[ffx-hooks] ArenaPlus progress rename failed (tmp=%s -> %s err=%lu)\n",
                tmpPath, g_state.sidecarPath, GetLastError());
        DeleteFileA(tmpPath);
        return false;
    }
    return true;
}

void ApplyFakeClearEnv() {
    char buf[1024] = {};
    DWORD n = GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_FAKE_CLEAR", buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return;
    char* save = nullptr;
    for (char* tok = strtok_s(buf, ",;", &save); tok; tok = strtok_s(nullptr, ",;", &save)) {
        while (*tok == ' ') ++tok;
        if (!*tok) continue;
        int idx = FindFlag(tok);
        if (idx < 0) idx = InsertFlag(tok);
        if (idx >= 0) {
            FlagEntry& e = g_state.flags[idx];
            e.cleared = true;
            if (!e.firstClearUtc[0]) IsoUtcNow(e.firstClearUtc, sizeof(e.firstClearUtc));
            IsoUtcNow(e.lastClearUtc, sizeof(e.lastClearUtc));
            ++e.clearCount;
            if (!e.evidence) e.evidence = InternString("FFXHOOKS_ARENAPLUS_FAKE_CLEAR seed");
            LogLine("[ffx-hooks] ArenaPlus progress FAKE_CLEAR seeded %s\n", tok);
        }
    }
}

} // namespace

ArenaProgressLoadResult ArenaProgress_Initialize(ArenaProgressLogFn logger) {
    g_state.logger = logger;
    g_state.flagsUsed = 0;
    g_state.stringPoolUsed = 0;
    g_state.enabled = false;
    g_state.initialized = false;

    if (!GateOn()) {
        LogLine("[ffx-hooks] ArenaPlus progress not armed (arena_plus_progress.flag)\n");
        return { false, 0, 0 };
    }

    ResolveSidecarPath(g_state.sidecarPath, sizeof(g_state.sidecarPath));
    if (!g_state.sidecarPath[0]) {
        LogLine("[ffx-hooks] ArenaPlus progress no sidecar path resolved\n");
        return { false, 0, 0 };
    }

    if (FileExists(g_state.sidecarPath)) {
        static char scratch[kFileScratchCap];
        size_t bytes = 0;
        if (ReadFileBytes(g_state.sidecarPath, scratch, sizeof(scratch), &bytes)) {
            ParseSidecar(scratch, bytes);
        } else {
            LogLine("[ffx-hooks] ArenaPlus progress read failed (path=%s)\n", g_state.sidecarPath);
        }
    } else {
        LogLine("[ffx-hooks] ArenaPlus progress sidecar absent, will create on first record (path=%s)\n",
                g_state.sidecarPath);
    }

    ApplyFakeClearEnv();

    g_state.enabled = true;
    g_state.initialized = true;

    int cleared = 0;
    for (int i = 0; i < g_state.flagsUsed; ++i) if (g_state.flags[i].cleared) ++cleared;
    LogLine("[ffx-hooks] ArenaPlus progress loaded path=%s flags=%d cleared=%d\n",
            g_state.sidecarPath, g_state.flagsUsed, cleared);
    return { true, g_state.flagsUsed, cleared };
}

bool ArenaProgress_Enabled() { return g_state.enabled; }

bool ArenaProgress_IsRowCleared(const char* progressFlag) {
    if (!g_state.enabled || !progressFlag || !progressFlag[0]) return false;
    int idx = FindFlag(progressFlag);
    return idx >= 0 && g_state.flags[idx].cleared;
}

bool ArenaProgress_RecordCleared(const char* progressFlag, const char* note) {
    if (!g_state.enabled || !progressFlag || !progressFlag[0]) return false;
    int idx = FindFlag(progressFlag);
    if (idx < 0) idx = InsertFlag(progressFlag);
    if (idx < 0) return false;
    FlagEntry& e = g_state.flags[idx];
    bool changed = !e.cleared;
    e.cleared = true;
    if (!e.firstClearUtc[0]) IsoUtcNow(e.firstClearUtc, sizeof(e.firstClearUtc));
    IsoUtcNow(e.lastClearUtc, sizeof(e.lastClearUtc));
    ++e.clearCount;
    if (note && note[0]) {
        const char* interned = InternString(note);
        if (interned) e.evidence = interned;
    }
    if (!WriteSidecar()) {
        LogLine("[ffx-hooks] ArenaPlus progress write FAILED for %s\n", progressFlag);
        return changed;
    }
    LogLine("[ffx-hooks] ArenaPlus progress recorded %s count=%d\n", progressFlag, e.clearCount);
    return changed;
}

int ArenaProgress_ClearedCount() {
    int cleared = 0;
    for (int i = 0; i < g_state.flagsUsed; ++i) if (g_state.flags[i].cleared) ++cleared;
    return cleared;
}

const char* ArenaProgress_SidecarPath() {
    return g_state.sidecarPath;
}

} // namespace FfxHooks
