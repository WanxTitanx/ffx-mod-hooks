#include "Config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace FfxHooks::Config {

namespace {

constexpr int kMaxPairs = 256;
constexpr int kKeyLen = 128;
constexpr int kValLen = 512;
constexpr size_t kMaxIniBytes = 65535;

struct KeyValPair {
    char key[kKeyLen];
    char val[kValLen];
};

struct IniUpdate {
    std::string section;
    std::string key;
    std::string value;
};

enum class ReadOutcome {
    Found,
    Missing,
    Error,
};

using InternalTryEnvBoolFn = bool (*)(void*, const char*, bool*);
using InternalFlagExistsFn = bool (*)(void*, const char*, BoolSource*);
using InternalPersistTextFn = bool (*)(void*, const char*, const char*);

struct Providers {
    void* context = nullptr;
    InternalTryEnvBoolFn tryEnvBool = nullptr;
    InternalFlagExistsFn flagExists = nullptr;
    InternalPersistTextFn persistText = nullptr;
    bool (*archiveArenaMusicOff)(void*, bool) = nullptr;
};

SRWLOCK g_pairsLock = SRWLOCK_INIT;
SRWLOCK g_transactionLock = SRWLOCK_INIT;
KeyValPair g_pairs[kMaxPairs] = {};
int g_pairCount = 0;
char g_loadedPath[MAX_PATH] = {};
char g_persistPath[MAX_PATH] = {};
char g_iniText[kMaxIniBytes + 1] = {};
bool g_loaded = false;
Providers g_providers;
volatile LONG g_tempSequence = 0;

const char kDefaultIni[] =
    "[core]\n"
    "log_level = 1\n"
    "\n"
    "[plugins]\n"
    "# DLL load on/off (managed by F8 menu, persisted here)\n"
    "dinput8 = 1\n"
    "dxgi = 0\n"
    "unx = 0\n"
    "ffx_probe = 0\n"
    "\n"
    "[boosters]\n"
    "# UnX-style gameplay boosters\n"
    "entire_party_earns_ap = 0\n"
    "permanent_sensor = 0\n"
    "playable_seymour = 0\n"
    "speed_hack = 0\n"
    "speed_hack_fmv = 0\n"
    "\n"
    "[speed_hack]\n"
    "# Ctrl+Shift+K cycles 1x/2x/4x/8x while armed\n"
    "# Native 2x/4x is armed; this hook does not infer per-scene application\n"
    "# Custom 8x targets the reviewed field-scene service tick; broader coverage needs RT2\n"
    "# FMV acceleration is separately opt-in and requires restart\n"
    "# factor and speed_step are retained as ignored legacy keys\n"
    "factor = 8.0\n"
    "max_speed = 8.0\n"
    "speed_step = 2.0\n"
    "\n"
    "[cheats]\n"
    "# Debug flags (UnX style)\n"
    "invincible_party = 0\n"
    "invincible_enemies = 0\n"
    "always_overdrive = 0\n"
    "always_critical = 0\n"
    "damage_value = 0\n"
    "always_rare_drop = 0\n"
    // The legacy booleans remain stable gates; missing scalar keys still resolve to 100 in the
    // catalog so older user files retain their historical behavior.
    "ap_100x = 0\n"
    "ap_multiplier = 100\n"
    "gil_100x = 0\n"
    "gil_multiplier = 100\n"
    "\n"
    "[field_scout]\n"
    "master = 0\n"
    "heavy = 0\n"
    "max = 0\n"
    "map_only = 0\n"
    "ultra = 0\n"
    "\n"
    "[arena_plus]\n"
    "master = 0\n"
    "compose_f7 = 0\n"
    "unlock_all = 0\n"
    "resolver_log = 0\n"
    "victory_hook = 0\n"
    "music = 0\n"
    "\n"
    "[development]\n"
    "fastload_autosave = 0\n"
    "# Fastload loads autosave 000 next boot; FFXHOOKS_FASTLOAD_OBSERVE_ONLY=1 disables actions.\n"
    "\n"
    "[input]\n"
    "fix_background_input = 0\n"
    "block_windows_key = 0\n"
    "filter_ime = 0\n"
    "dialog_skip = 0\n"
    "\n"
    "[window]\n"
    "borderless = 0\n"
    "clip_cursor = 0\n"
    "hide_cursor = 0\n"
    "\n"
    "[camera]\n"
    "free_look = 0\n"
    "freeze_scene = 0\n"
    "\n"
    "[diagnostics]\n"
    "performance = 0\n"
    "\n"
    "[bindings]\n"
    "performance = 0\n"
    "borderless = 0\n"
    "free_camera = 0\n"
    "freeze_scene = 0\n"
    "\n"
    "[dashboard]\n"
    "# F8 dashboard: 1 = F8 opens the dashboard\n"
    "enabled = 1\n"
    "ingame_menu = 1\n"
    "\n"
    "[f7]\n"
    "# F7 In-Live + observe-only Monster AI evidence (legacy flags remain supported)\n"
    "inlive = 0\n"
    "aiswap = 0\n"
    "\n"
    "[music]\n"
    "# Battle music hook (legacy flags remain supported)\n"
    "enabled = 0\n"
    "arena_plus = 0\n"
    "\n"
    "[labs]\n"
    "# Lab hooks (legacy flags remain supported)\n"
    "nova_super_damage = 0\n"
    "nova_super_damage_log = 0\n"
    "# Ronso Mana owns OD 200, partial costs and saved charge; no Apply switch.\n"
    "kimahri_ronso_mana = 0\n"
    "equipment_workshop = 0\n"
    "nul_ward = 0\n"
    "nul_ward_apply = 0\n"
    "nul_ward_log = 0\n"
    "nul_ward_native_slots = 0\n"
    "nul_ward_p16 = 0\n"
    "nul_ward_p16_apply = 0\n"
    "nul_ward_teach = 0\n"
    "nul_ward_teach_grant = 0\n"
    "grid_teach = 0\n"
    "kimahri_lancet_dual_grant = 0\n"
    "item_stack_cap = 0\n"
    "item_stack_cap_value = 255\n"
    "item_stack_cap_log = 0\n"
    "double_triple_drop = 0\n"
    "double_triple_drop_log = 0\n"
    "element_scan_dark = 0\n"
    "ability_sfx = 0\n"
    "ability_sfx_log = 0\n"
    "field_probe_rt2 = 0\n"
    "field_probe_encounter = 0\n"
    "field_probe_texture = 0\n"
    "field_scout = 0\n"
    "field_scout_map_only = 0\n"
    "field_scout_heavy = 0\n"
    "field_scout_ultra = 0\n"
    "field_scout_max = 0\n"
    "fps_scout = 0\n"
    "arena_plus_compose_f7 = 0\n"
    "arena_plus_progress = 0\n"
    "\n"
    "[maechen]\n"
    "enabled = 0\n"
    "locale = pt\n";

class SharedPairsLock {
public:
    SharedPairsLock() { AcquireSRWLockShared(&g_pairsLock); }
    ~SharedPairsLock() { ReleaseSRWLockShared(&g_pairsLock); }
    SharedPairsLock(const SharedPairsLock&) = delete;
    SharedPairsLock& operator=(const SharedPairsLock&) = delete;
};

class ExclusivePairsLock {
public:
    ExclusivePairsLock() { AcquireSRWLockExclusive(&g_pairsLock); }
    ~ExclusivePairsLock() { ReleaseSRWLockExclusive(&g_pairsLock); }
    ExclusivePairsLock(const ExclusivePairsLock&) = delete;
    ExclusivePairsLock& operator=(const ExclusivePairsLock&) = delete;
};

class TransactionLock {
public:
    TransactionLock() { AcquireSRWLockExclusive(&g_transactionLock); }
    ~TransactionLock() { ReleaseSRWLockExclusive(&g_transactionLock); }
    TransactionLock(const TransactionLock&) = delete;
    TransactionLock& operator=(const TransactionLock&) = delete;
};

std::string TrimCopy(const std::string& input) {
    const size_t first = input.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const size_t last = input.find_last_not_of(" \t\r");
    return input.substr(first, last - first + 1);
}

bool ParseSectionLine(const std::string& input, std::string& sectionOut) {
    const std::string line = TrimCopy(input);
    if (line.empty() || line.front() != '[') return false;
    const size_t close = line.find(']');
    if (close == std::string::npos || close == 1) return false;
    sectionOut = TrimCopy(line.substr(1, close - 1));
    return !sectionOut.empty();
}

bool ParseKeyLine(const std::string& input, std::string& keyOut) {
    const std::string line = TrimCopy(input);
    if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') return false;
    const size_t equals = line.find('=');
    if (equals == std::string::npos) return false;
    keyOut = TrimCopy(line.substr(0, equals));
    return !keyOut.empty();
}

bool ParseIniText(const char* ini, size_t size, KeyValPair* pairsOut, int* countOut) {
    if (!ini || !pairsOut || !countOut || size > kMaxIniBytes) return false;
    *countOut = 0;
    std::string section;
    size_t position = 0;
    bool firstLine = true;

    while (position < size) {
        const char* lineStart = ini + position;
        const char* newline = static_cast<const char*>(memchr(lineStart, '\n', size - position));
        const size_t length = newline ? static_cast<size_t>(newline - lineStart) : size - position;
        std::string line(lineStart, length);
        position += length + (newline ? 1 : 0);

        if (firstLine && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        firstLine = false;
        line = TrimCopy(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;

        std::string parsedSection;
        if (ParseSectionLine(line, parsedSection)) {
            section = parsedSection;
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = TrimCopy(line.substr(0, equals));
        const std::string value = TrimCopy(line.substr(equals + 1));
        if (key.empty()) continue;

        const std::string flat = section.empty() ? key : section + "." + key;
        if (flat.size() >= kKeyLen || value.size() >= kValLen || *countOut >= kMaxPairs) return false;
        strcpy_s(pairsOut[*countOut].key, flat.c_str());
        strcpy_s(pairsOut[*countOut].val, value.c_str());
        ++(*countOut);
    }
    return true;
}

bool ParseBoolText(const char* text, bool* valueOut) {
    if (!text || !valueOut) return false;
    if (strcmp(text, "1") == 0 || _stricmp(text, "true") == 0 ||
        _stricmp(text, "yes") == 0 || _stricmp(text, "on") == 0) {
        *valueOut = true;
        return true;
    }
    if (strcmp(text, "0") == 0 || _stricmp(text, "false") == 0 ||
        _stricmp(text, "no") == 0 || _stricmp(text, "off") == 0) {
        *valueOut = false;
        return true;
    }
    return false;
}

bool TryCopyExactValue(const char* key, char* valueOut, size_t valueCapacity) {
    if (!key || !key[0] || !valueOut || valueCapacity == 0) return false;
    SharedPairsLock guard;
    for (int i = 0; i < g_pairCount; ++i) {
        if (_stricmp(g_pairs[i].key, key) == 0) {
            strncpy_s(valueOut, valueCapacity, g_pairs[i].val, _TRUNCATE);
            return true;
        }
    }
    return false;
}

bool ResolveDefaultPath(char* pathOut, size_t capacity) {
    if (!pathOut || capacity == 0 || capacity > MAX_PATH) return false;
    char modulePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH) == 0) return false;
    char* slash = strrchr(modulePath, '\\');
    if (!slash) return false;
    *slash = '\0';
    return _snprintf_s(pathOut, capacity, _TRUNCATE, "%s\\_isolated\\ffx-hooks.ini", modulePath) > 0;
}

ReadOutcome ReadFileText(const char* path, std::string& textOut) {
    if (!path || !path[0]) return ReadOutcome::Error;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                   ? ReadOutcome::Missing
                   : ReadOutcome::Error;
    }

    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0 &&
              size.QuadPart <= static_cast<LONGLONG>(kMaxIniBytes);
    std::string text;
    if (ok) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        const DWORD expected = static_cast<DWORD>(text.size());
        ok = ReadFile(file, text.data(), expected, &read, nullptr) != FALSE && read == expected;
    }
    if (CloseHandle(file) == FALSE) ok = false;
    if (!ok) return ReadOutcome::Error;
    textOut = std::move(text);
    return ReadOutcome::Found;
}

bool EnsureIsolatedDirectory(const char* path) {
    if (!path || !path[0]) return false;
    char directory[MAX_PATH] = {};
    strncpy_s(directory, path, _TRUNCATE);
    char* slash = strrchr(directory, '\\');
    if (!slash) return true;
    *slash = '\0';

    const DWORD attributes = GetFileAttributesA(directory);
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    const char* leaf = strrchr(directory, '\\');
    leaf = leaf ? leaf + 1 : directory;
    if (_stricmp(leaf, "_isolated") != 0) return false;
    return CreateDirectoryA(directory, nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool PersistTextAtomically(void*, const char* path, const char* text) {
    if (!path || !path[0] || !text || strlen(text) > kMaxIniBytes) return false;
    if (!EnsureIsolatedDirectory(path)) return false;

    char temporary[MAX_PATH] = {};
    const LONG sequence = InterlockedIncrement(&g_tempSequence);
    if (_snprintf_s(temporary, sizeof(temporary), _TRUNCATE, "%s.tmp.%lu.%ld", path,
                    GetCurrentProcessId(), sequence) <= 0) {
        return false;
    }

    HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    const DWORD byteCount = static_cast<DWORD>(strlen(text));
    DWORD written = 0;
    bool ok = WriteFile(file, text, byteCount, &written, nullptr) != FALSE && written == byteCount;
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (CloseHandle(file) == FALSE) ok = false;
    if (ok) {
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) DeleteFileA(temporary);
    return ok;
}

bool TryEnvBoolFromProcess(void*, const char* name, bool* valueOut) {
    if (!name || !name[0] || !valueOut) return false;
    char buffer[32] = {};
    const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (length == 0 || length >= sizeof(buffer)) return false;
    return ParseBoolText(buffer, valueOut);
}

bool FileExistsAt(const char* directory, const char* relative) {
    if (!directory || !relative) return false;
    char path[MAX_PATH] = {};
    if (_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s", directory, relative) <= 0) return false;
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Preserve the compatibility search order: modules, config, modules\config, then game root.
bool FlagExistsInKnownLocations(void*, const char* flagName, BoolSource* sourceOut) {
    if (!flagName || !flagName[0] || !sourceOut) return false;
    char root[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, root, MAX_PATH) == 0) return false;
    char* slash = strrchr(root, '\\');
    if (!slash) return false;
    *slash = '\0';

    char relative[MAX_PATH] = {};
    if (_snprintf_s(relative, sizeof(relative), _TRUNCATE, "modules\\%s", flagName) > 0 &&
        FileExistsAt(root, relative)) {
        *sourceOut = BoolSource::LegacyFlagModules;
        return true;
    }
    if (_snprintf_s(relative, sizeof(relative), _TRUNCATE, "config\\%s", flagName) > 0 &&
        FileExistsAt(root, relative)) {
        *sourceOut = BoolSource::LegacyFlagConfig;
        return true;
    }
    if (_snprintf_s(relative, sizeof(relative), _TRUNCATE, "modules\\config\\%s", flagName) > 0 &&
        FileExistsAt(root, relative)) {
        *sourceOut = BoolSource::LegacyFlagModulesConfig;
        return true;
    }
    if (FileExistsAt(root, flagName)) {
        *sourceOut = BoolSource::LegacyFlagRoot;
        return true;
    }
    return false;
}

bool TryEnvBool(const char* name, bool* valueOut) {
    const InternalTryEnvBoolFn provider = g_providers.tryEnvBool ? g_providers.tryEnvBool : &TryEnvBoolFromProcess;
    return provider(g_providers.context, name, valueOut);
}

bool FlagExists(const char* name, BoolSource* sourceOut) {
    const InternalFlagExistsFn provider = g_providers.flagExists ? g_providers.flagExists : &FlagExistsInKnownLocations;
    return provider(g_providers.context, name, sourceOut);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t position = 0;
    while (position < text.size()) {
        const size_t end = text.find('\n', position);
        std::string line = end == std::string::npos ? text.substr(position) : text.substr(position, end - position);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        position = end + 1;
    }
    return lines;
}

std::string JoinLines(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text.append(line);
        text.push_back('\n');
    }
    return text;
}

bool ApplyIniUpdate(std::string& text, const IniUpdate& update) {
    std::vector<std::string> lines = SplitLines(text);
    size_t sectionBegin = 0;
    size_t sectionEnd = lines.size();
    bool sectionFound = update.section.empty();

    if (update.section.empty()) {
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string section;
            if (ParseSectionLine(lines[i], section)) {
                sectionEnd = i;
                break;
            }
        }
    } else {
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string section;
            if (!ParseSectionLine(lines[i], section)) continue;
            if (!sectionFound && _stricmp(section.c_str(), update.section.c_str()) == 0) {
                sectionFound = true;
                sectionBegin = i + 1;
                sectionEnd = lines.size();
                continue;
            }
            if (sectionFound) {
                sectionEnd = i;
                break;
            }
        }
    }

    const std::string replacement = update.key + " = " + update.value;
    if (!sectionFound) {
        if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
        lines.push_back("[" + update.section + "]");
        lines.push_back(replacement);
        text = JoinLines(lines);
        return text.size() <= kMaxIniBytes;
    }

    bool replaced = false;
    for (size_t i = sectionBegin; i < sectionEnd; ++i) {
        std::string key;
        if (ParseKeyLine(lines[i], key) && _stricmp(key.c_str(), update.key.c_str()) == 0) {
            lines[i] = replacement;
            replaced = true;
        }
    }
    if (!replaced) {
        const auto insertAt = lines.begin() + static_cast<std::vector<std::string>::difference_type>(sectionEnd);
        lines.insert(insertAt, replacement);
    }
    text = JoinLines(lines);
    return text.size() <= kMaxIniBytes;
}

bool SplitFlatKey(const char* flatKey, std::string& sectionOut, std::string& keyOut) {
    if (!flatKey || !flatKey[0] || strlen(flatKey) >= kKeyLen) return false;
    const char* dot = strchr(flatKey, '.');
    if (dot) {
        sectionOut.assign(flatKey, static_cast<size_t>(dot - flatKey));
        keyOut = dot + 1;
    } else {
        sectionOut.clear();
        keyOut = flatKey;
    }
    return !keyOut.empty() && sectionOut.size() < 64 && keyOut.size() < kKeyLen;
}

void PublishState(const KeyValPair* pairs, int pairCount, const std::string& iniText,
                  const char* loadedPath, const char* persistPath) {
    ExclusivePairsLock guard;
    memcpy(g_pairs, pairs, static_cast<size_t>(pairCount) * sizeof(KeyValPair));
    if (pairCount < kMaxPairs) {
        memset(g_pairs + pairCount, 0, static_cast<size_t>(kMaxPairs - pairCount) * sizeof(KeyValPair));
    }
    g_pairCount = pairCount;
    strncpy_s(g_iniText, iniText.c_str(), _TRUNCATE);
    strncpy_s(g_loadedPath, loadedPath ? loadedPath : "", _TRUNCATE);
    strncpy_s(g_persistPath, persistPath ? persistPath : "", _TRUNCATE);
    g_loaded = true;
}

bool SnapshotState(std::string& iniTextOut, char* persistPathOut, size_t pathCapacity) {
    {
        SharedPairsLock guard;
        if (g_loaded) {
            iniTextOut = g_iniText;
            strncpy_s(persistPathOut, pathCapacity, g_persistPath, _TRUNCATE);
            return persistPathOut[0] != '\0';
        }
    }
    if (!ResolveDefaultPath(persistPathOut, pathCapacity)) return false;
    const ReadOutcome outcome = ReadFileText(persistPathOut, iniTextOut);
    if (outcome == ReadOutcome::Error) return false;
    if (outcome == ReadOutcome::Missing) iniTextOut = kDefaultIni;
    return true;
}

// Build from one snapshot, durably replace the complete document, then publish the parsed state.
// A failed replacement never exposes speculative values to concurrent readers.
bool SetValuesTransaction(const std::vector<IniUpdate>& updates) {
    if (updates.empty()) return false;
    TransactionLock transaction;

    std::string proposed;
    char target[MAX_PATH] = {};
    if (!SnapshotState(proposed, target, sizeof(target))) return false;
    for (const IniUpdate& update : updates) {
        if (update.key.empty() || update.value.size() >= kValLen || !ApplyIniUpdate(proposed, update)) return false;
    }

    KeyValPair parsed[kMaxPairs] = {};
    int parsedCount = 0;
    if (!ParseIniText(proposed.c_str(), proposed.size(), parsed, &parsedCount)) return false;

    const InternalPersistTextFn provider = g_providers.persistText ? g_providers.persistText : &PersistTextAtomically;
    if (!provider(g_providers.context, target, proposed.c_str())) return false;

    PublishState(parsed, parsedCount, proposed, target, target);
    return true;
}

} // namespace

// Missing storage selects built-in defaults; read or parse errors return without replacing state.
bool Load() {
    TransactionLock transaction;
    char target[MAX_PATH] = {};
    if (!ResolveDefaultPath(target, sizeof(target))) return false;

    std::string text;
    const ReadOutcome outcome = ReadFileText(target, text);
    if (outcome == ReadOutcome::Error) return false;
    const bool fromDisk = outcome == ReadOutcome::Found;
    if (outcome == ReadOutcome::Missing) text = kDefaultIni;

    KeyValPair parsed[kMaxPairs] = {};
    int parsedCount = 0;
    if (!ParseIniText(text.c_str(), text.size(), parsed, &parsedCount)) return false;
    PublishState(parsed, parsedCount, text, fromDisk ? target : "(built-in defaults)", target);
    return true;
}

bool TryGetBoolExact(const char* key, bool* valueOut) {
    if (!valueOut) return false;
    char value[kValLen] = {};
    return TryCopyExactValue(key, value, sizeof(value)) && ParseBoolText(value, valueOut);
}

bool GetBool(const char* section_key, bool defaultValue) {
    bool value = false;
    return TryGetBoolExact(section_key, &value) ? value : defaultValue;
}

int GetInt(const char* section_key, int defaultValue) {
    char value[kValLen] = {};
    if (!TryCopyExactValue(section_key, value, sizeof(value))) return defaultValue;
    char* end = nullptr;
    const long parsed = strtol(value, &end, 10);
    return end && *end == '\0' ? static_cast<int>(parsed) : defaultValue;
}

IntReadResult ReadIntExact(const char* section_key, int minimum, int maximum) {
    if (!section_key || !section_key[0] || minimum > maximum || minimum < 0) {
        return {IntReadState::Invalid, 0};
    }

    char text[kValLen] = {};
    if (!TryCopyExactValue(section_key, text, sizeof(text))) {
        return {IntReadState::Missing, 0};
    }
    if (!text[0]) return {IntReadState::Invalid, 0};

    // strtol accepts signs and leading whitespace. Reward multipliers deliberately accept only
    // literal decimal digits so malformed user configuration can never be silently normalized.
    uint64_t parsed = 0;
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text);
         *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return {IntReadState::Invalid, 0};
        const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
        if (parsed > (static_cast<uint64_t>((std::numeric_limits<int>::max)()) - digit) / 10u) {
            return {IntReadState::Invalid, 0};
        }
        parsed = parsed * 10u + digit;
    }

    const int value = static_cast<int>(parsed);
    if (value < minimum || value > maximum) return {IntReadState::Invalid, 0};
    return {IntReadState::Valid, value};
}

float GetFloat(const char* section_key, float defaultValue) {
    char value[kValLen] = {};
    if (!TryCopyExactValue(section_key, value, sizeof(value))) return defaultValue;
    char* end = nullptr;
    const float parsed = strtof(value, &end);
    return end && *end == '\0' ? parsed : defaultValue;
}

const char* GetString(const char* section_key, const char* defaultValue) {
    thread_local char value[kValLen] = {};
    if (TryCopyExactValue(section_key, value, sizeof(value))) return value;
    if (!defaultValue) return nullptr;
    strncpy_s(value, defaultValue, _TRUNCATE);
    return value;
}

BoolGateResult ResolveBoolGate(const BoolGateSpec& spec) {
    bool value = false;
    if (!spec.canonicalKey || !spec.canonicalKey[0]) return {spec.defaultValue, BoolSource::DefaultValue};

    if (spec.disableEnvName && TryEnvBool(spec.disableEnvName, &value) && value) {
        return {false, BoolSource::DisableEnvironment};
    }
    BoolSource flagSource = BoolSource::DefaultValue;
    if ((spec.offFlagName && FlagExists(spec.offFlagName, &flagSource)) ||
        (spec.globalOffFlagName && FlagExists(spec.globalOffFlagName, &flagSource))) {
        return {false, BoolSource::LegacyOffFlag};
    }
    if (spec.envName && TryEnvBool(spec.envName, &value)) return {value, BoolSource::Environment};

    bool marker = false;
    if (spec.authorityKey && TryGetBoolExact(spec.authorityKey, &marker) && marker &&
        TryGetBoolExact(spec.canonicalKey, &value)) {
        return {value, BoolSource::AuthoritativeCanonicalIni};
    }
    if (spec.unmarkedTrueIsLegacyEnable && TryGetBoolExact(spec.canonicalKey, &value) && value) {
        return {true, BoolSource::UnmarkedCanonicalIni};
    }
    if (spec.legacyKey && TryGetBoolExact(spec.legacyKey, &value)) return {value, BoolSource::LegacyIni};
    if (spec.flagName && FlagExists(spec.flagName, &flagSource)) return {true, flagSource};
    if (!spec.authorityKey && TryGetBoolExact(spec.canonicalKey, &value)) {
        return {value, BoolSource::UnmarkedCanonicalIni};
    }
    return {spec.defaultValue, BoolSource::DefaultValue};
}

bool SetBool(const char* section_key, bool value) {
    std::string section;
    std::string key;
    if (!SplitFlatKey(section_key, section, key)) return false;
    return SetValuesTransaction({{section, key, value ? "1" : "0"}});
}

bool SetInt(const char* section_key, int value) {
    std::string section;
    std::string key;
    if (!SplitFlatKey(section_key, section, key)) return false;
    return SetValuesTransaction({{section, key, std::to_string(value)}});
}

bool SetString(const char* section_key, const char* value) {
    if (!value) return false;
    std::string section;
    std::string key;
    if (!SplitFlatKey(section_key, section, key)) return false;
    return SetValuesTransaction({{section, key, value}});
}

bool SetAuthoritativeBools(const AuthoritativeBoolUpdate* values, std::size_t count) {
    if (!values || count == 0) return false;
    std::vector<IniUpdate> updates;
    updates.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        if (!values[index].spec) return false;
        const BoolGateSpec& spec = *values[index].spec;
        std::string canonicalSection;
        std::string canonicalKey;
        if (!SplitFlatKey(spec.canonicalKey, canonicalSection, canonicalKey)) return false;
        updates.push_back({canonicalSection, canonicalKey, values[index].value ? "1" : "0"});
        if (spec.authorityKey) {
            std::string authoritySection;
            std::string authorityKey;
            if (!SplitFlatKey(spec.authorityKey, authoritySection, authorityKey)) return false;
            updates.push_back({authoritySection, authorityKey, "1"});
        }
    }
    return SetValuesTransaction(updates);
}

bool SetAuthoritativeBool(const BoolGateSpec& spec, bool value) {
    const AuthoritativeBoolUpdate update = {&spec, value};
    return SetAuthoritativeBools(&update, 1);
}

bool EnableArenaMusicFromMenu(const BoolGateSpec& spec) {
    if (!spec.canonicalKey || std::strcmp(spec.canonicalKey,"arena_plus.music")!=0 ||
        !spec.offFlagName || std::strcmp(spec.offFlagName,"arena_plus_music.flag.off")!=0) return false;
    bool disabled=false;
    BoolSource source=BoolSource::DefaultValue;
    if ((spec.disableEnvName && TryEnvBool(spec.disableEnvName,&disabled) && disabled) ||
        (spec.globalOffFlagName && FlagExists(spec.globalOffFlagName,&source)))
        return SetAuthoritativeBool(spec,true);

    // F8 serializes this row. Other catalog rows never invoke this narrow migration.
    if (g_providers.archiveArenaMusicOff) {
        if(!g_providers.archiveArenaMusicOff(g_providers.context,false))return false;
        if(SetAuthoritativeBool(spec,true))return true;
        g_providers.archiveArenaMusicOff(g_providers.context,true);
        return false;
    }
    // A fake flag provider may not fall through to the real filesystem.
    if(g_providers.flagExists) return !FlagExists(spec.offFlagName,&source) && SetAuthoritativeBool(spec,true);

    char root[MAX_PATH]={};
    const DWORD length=GetModuleFileNameA(nullptr,root,MAX_PATH);
    if(!length || length>=MAX_PATH)return false;
    char* slash=std::strrchr(root,'\\');if(!slash)return false;*slash=0;
    const char* relative[]={"modules\\arena_plus_music.flag.off","config\\arena_plus_music.flag.off",
        "modules\\config\\arena_plus_music.flag.off","arena_plus_music.flag.off"};
    std::vector<std::pair<std::string,std::string>> moved;
    auto restore=[&](){
        for(auto it=moved.rbegin();it!=moved.rend();++it)
            // Never overwrite a new OFF marker created by another owner.
            MoveFileExA(it->second.c_str(),it->first.c_str(),MOVEFILE_WRITE_THROUGH);
    };
    for(const char* leaf:relative) {
        char path[MAX_PATH]={},backup[MAX_PATH]={};
        if(_snprintf_s(path,sizeof(path),_TRUNCATE,"%s\\%s",root,leaf)<0){restore();return false;}
        const DWORD attributes=GetFileAttributesA(path);
        if(attributes==INVALID_FILE_ATTRIBUTES) {
            const DWORD error=GetLastError();
            if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)continue;
            restore();return false;
        }
        if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){restore();return false;}
        const LONG sequence=InterlockedIncrement(&g_tempSequence);
        if(_snprintf_s(backup,sizeof(backup),_TRUNCATE,"%s.f8-on-%lu-%llu-%ld.bak",path,
            GetCurrentProcessId(),static_cast<unsigned long long>(GetTickCount64()),sequence)<0 ||
            !MoveFileExA(path,backup,MOVEFILE_WRITE_THROUGH)){restore();return false;}
        moved.emplace_back(path,backup);
    }
    if(SetAuthoritativeBool(spec,true))return true;
    restore();return false;
}

const char* BoolSourceName(BoolSource source) {
    switch (source) {
    case BoolSource::DefaultValue: return "DefaultValue";
    case BoolSource::DisableEnvironment: return "DisableEnvironment";
    case BoolSource::LegacyOffFlag: return "LegacyOffFlag";
    case BoolSource::Environment: return "Environment";
    case BoolSource::AuthoritativeCanonicalIni: return "AuthoritativeCanonicalIni";
    case BoolSource::LegacyIni: return "LegacyIni";
    case BoolSource::LegacyFlagModules: return "LegacyFlagModules";
    case BoolSource::LegacyFlagConfig: return "LegacyFlagConfig";
    case BoolSource::LegacyFlagModulesConfig: return "LegacyFlagModulesConfig";
    case BoolSource::LegacyFlagRoot: return "LegacyFlagRoot";
    case BoolSource::UnmarkedCanonicalIni: return "UnmarkedCanonicalIni";
    default: return "Unknown";
    }
}

const char* GetLoadedPath() {
    thread_local char path[MAX_PATH] = {};
    SharedPairsLock guard;
    strncpy_s(path, g_loadedPath, _TRUNCATE);
    return path;
}

bool LegacyFlagEnabled(const char* flagName) {
    BoolSource source = BoolSource::DefaultValue;
    return flagName && FlagExists(flagName, &source);
}

bool EnvFlagEnabled(const char* envName) {
    bool value = false;
    return envName && TryEnvBool(envName, &value) && value;
}

bool CheckEnabled(const char* section_key, const char* envName, const char* flagName, bool defaultValue) {
    bool value = false;
    if (envName && TryEnvBool(envName, &value)) return value;
    if (TryGetBoolExact(section_key, &value) && value) return true;
    BoolSource source = BoolSource::DefaultValue;
    if (flagName && FlagExists(flagName, &source)) return true;
    return defaultValue;
}

#ifdef FFXHOOKS_TESTING
void SetProvidersForTests(const TestProviders& providers) {
    TransactionLock transaction;
    g_providers = {providers.context, providers.tryEnvBool, providers.flagExists, providers.persistText, providers.archiveArenaMusicOff};
}

bool LoadTextForTests(const char* iniText, const char* path) {
    TransactionLock transaction;
    const char* selected = iniText ? iniText : kDefaultIni;
    const size_t size = strlen(selected);
    if (size > kMaxIniBytes) return false;

    char target[MAX_PATH] = {};
    if (path && path[0]) {
        strncpy_s(target, path, _TRUNCATE);
    } else if (!ResolveDefaultPath(target, sizeof(target))) {
        return false;
    }

    KeyValPair parsed[kMaxPairs] = {};
    int parsedCount = 0;
    if (!ParseIniText(selected, size, parsed, &parsedCount)) return false;
    PublishState(parsed, parsedCount, selected, iniText ? target : "(built-in defaults)", target);
    return true;
}

void ResetForTests() {
    TransactionLock transaction;
    {
        ExclusivePairsLock guard;
        memset(g_pairs, 0, sizeof(g_pairs));
        g_pairCount = 0;
        g_loadedPath[0] = '\0';
        g_persistPath[0] = '\0';
        g_iniText[0] = '\0';
        g_loaded = false;
    }
    g_providers = {};
}
#endif

} // namespace FfxHooks::Config
