#include "Config.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace FfxHooks::Config {

static const int kMaxPairs = 256;
static const int kKeyLen = 128;
static const int kValLen = 512;

struct KeyValPair {
    char key[kKeyLen];
    char val[kValLen];
};

static KeyValPair g_pairs[kMaxPairs] = {};
static int g_pairCount = 0;
static char g_loadedPath[MAX_PATH] = {};
static bool g_loaded = false;
static CRITICAL_SECTION g_lock;

/* â”€â”€ Built-in defaults â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static const char kDefaultIni[] =
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
    "\n"
    "[speed_hack]\n"
    "# F2 turbo (future)\n"
    "max_speed = 8.0\n"
    "speed_step = 2.0\n"
    "\n"
    "[cheats]\n"
    "# Debug flags (UnX style)\n"
    "always_overdrive = 0\n"
    "always_critical = 0\n"
    "damage_value = 0\n"
    "always_rare_drop = 0\n"
    "ap_100x = 0\n"
    "gil_100x = 0\n"
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
    "resolver_log = 0\n"
    "victory_hook = 0\n"
    "\n"
    "[input]\n"
    "fix_background_input = 1\n"
    "block_windows_key = 1\n"
    "filter_ime = 1\n"
    "\n"
    "[dashboard]\n"
    "# F8 dashboard (Operacao Demonio 2026-08-02): 1 = F8 abre o dashboard (dono da tecla)\n"
    "enabled = 0\n";

/* â”€â”€ INI Parser â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void Trim(char* s) {
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
}

static const char* ParseIni(const char* ini, int size) {
    char section[64] = "";
    const char* end = ini + size;
    const char* p = ini;
    char line[1024];
    int lineLen = 0;

    while (p < end) {
        /* read a line */
        lineLen = 0;
        while (p < end && *p != '\n') {
            if (lineLen < (int)sizeof(line) - 1) line[lineLen++] = *p;
            p++;
        }
        if (p < end) p++; /* skip \n */
        line[lineLen] = '\0';
        Trim(line);

        /* skip BOM bytes (UTF-8 BOM: EF BB BF) */
        unsigned char* uline = (unsigned char*)line;
        if (uline[0] == 0xEF && uline[1] == 0xBB && uline[2] == 0xBF) {
            memmove(line, line + 3, strlen(line) - 2);
        }

        /* skip empty and comment */
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') continue;

        /* section [section] */
        if (line[0] == '[') {
            char* endBracket = strchr(line, ']');
            if (endBracket) {
                int len = (int)(endBracket - line - 1);
                if (len > 0 && len < (int)sizeof(section)) {
                    memcpy(section, line + 1, len);
                    section[len] = '\0';
                }
            }
            continue;
        }

        /* key = value */
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        Trim(key);
        Trim(value);
        if (key[0] == '\0' || value[0] == '\0') continue;

        /* flatten key: section.key */
        char flat[kKeyLen];
        if (section[0])
            _snprintf_s(flat, sizeof(flat), _TRUNCATE, "%s.%s", section, key);
        else
            lstrcpynA(flat, key, (int)sizeof(flat));

        if (g_pairCount < kMaxPairs) {
            lstrcpynA(g_pairs[g_pairCount].key, flat, kKeyLen);
            lstrcpynA(g_pairs[g_pairCount].val, value, kValLen);
            g_pairCount++;
        }
    }
    return nullptr;
}

/* â”€â”€ Public API â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

bool Load() {
    InitializeCriticalSection(&g_lock);

    /* Try _isolated/ffx-hooks.ini first */
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, sizeof(path));
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        lstrcpynA(lastSlash + 1, "_isolated\\ffx-hooks.ini", (int)(path + sizeof(path) - lastSlash - 1));
        HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD size = GetFileSize(hFile, nullptr);
            if (size > 0 && size < 65536) {
                char* buf = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
                if (buf) {
                    DWORD read = 0;
                    if (ReadFile(hFile, buf, size, &read, nullptr)) {
                        buf[read] = '\0';
                        ParseIni(buf, (int)read);
                        lstrcpynA(g_loadedPath, path, (int)sizeof(g_loadedPath));
                        g_loaded = true;
                    }
                    HeapFree(GetProcessHeap(), 0, buf);
                }
            }
            CloseHandle(hFile);
        }
    }

    if (!g_loaded) {
        /* No INI found â€” use defaults (don't create file, just use hardcoded defaults) */
        ParseIni(kDefaultIni, (int)strlen(kDefaultIni));
        g_loaded = true;
        char msg[128] = {};
        _snprintf_s(msg, sizeof(msg), _TRUNCATE, "(built-in defaults)");
        lstrcpynA(g_loadedPath, msg, (int)sizeof(g_loadedPath));
    }

    return g_loaded;
}

static const char* FindValue(const char* section_key) {
    /* 1. Exact match */
    for (int i = 0; i < g_pairCount; i++) {
        if (_stricmp(g_pairs[i].key, section_key) == 0)
            return g_pairs[i].val;
    }

    /* 2. Underscore->dot conversion: "field_scout_heavy" â†’ "field_scout.heavy"
       Try replacing EACH underscore L2R so section names with underscores work. */
    char dotKey[kKeyLen];
    lstrcpynA(dotKey, section_key, (int)sizeof(dotKey));
    const int dkLen = (int)strlen(dotKey);
    for (int j = 0; j < dkLen; j++) {
        if (dotKey[j] == '_') {
            dotKey[j] = '.';
            for (int i = 0; i < g_pairCount; i++) {
                if (_stricmp(g_pairs[i].key, dotKey) == 0)
                    return g_pairs[i].val;
            }
            dotKey[j] = '_'; /* restore for next position */
        }
    }

    /* 3. Suffix match: "music" â†’ *.music â†’ "arena_plus.music" */
    char dotSuf[kKeyLen];
    _snprintf_s(dotSuf, sizeof(dotSuf), _TRUNCATE, ".%s", section_key);
    const int sLen = (int)strlen(dotSuf);
    for (int i = 0; i < g_pairCount; i++) {
        const char* k = g_pairs[i].key;
        const int kLen = (int)strlen(k);
        if (kLen > sLen && _stricmp(k + kLen - sLen, dotSuf) == 0)
            return g_pairs[i].val;
    }

    return nullptr;
}

bool GetBool(const char* section_key, bool defaultValue) {
    const char* val = FindValue(section_key);
    if (!val) return defaultValue;
    if (strcmp(val, "1") == 0 || _stricmp(val, "true") == 0 || _stricmp(val, "yes") == 0)
        return true;
    return false;
}

int GetInt(const char* section_key, int defaultValue) {
    const char* val = FindValue(section_key);
    if (!val) return defaultValue;
    return atoi(val);
}

float GetFloat(const char* section_key, float defaultValue) {
    const char* val = FindValue(section_key);
    if (!val) return defaultValue;
    return (float)atof(val);
}

const char* GetString(const char* section_key, const char* defaultValue) {
    const char* val = FindValue(section_key);
    return val ? val : defaultValue;
}

bool SetBool(const char* section_key, bool value) {
    /* update in-memory */
    for (int i = 0; i < g_pairCount; i++) {
        if (_stricmp(g_pairs[i].key, section_key) == 0) {
            lstrcpynA(g_pairs[i].val, value ? "1" : "0", kValLen);
            break;
        }
    }
    /* write INI file */
    if (g_loadedPath[0] && g_loadedPath[0] != '(') {
        char tmp[MAX_PATH] = {};
        _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", g_loadedPath);
        FILE* f = nullptr;
        if (fopen_s(&f, tmp, "w") == 0 && f) {
            char section[64] = "";
            for (int i = 0; i < g_pairCount; i++) {
                char* dot = strchr(g_pairs[i].key, '.');
                if (dot) {
                    int secLen = (int)(dot - g_pairs[i].key);
                    if (secLen != (int)strlen(section) || strncmp(g_pairs[i].key, section, secLen) != 0) {
                        memcpy(section, g_pairs[i].key, secLen);
                        section[secLen] = '\0';
                        fprintf(f, "\n[%s]\n", section);
                    }
                    fprintf(f, "%s = %s\n", dot + 1, g_pairs[i].val);
                } else {
                    fprintf(f, "%s = %s\n", g_pairs[i].key, g_pairs[i].val);
                }
            }
            fclose(f);
            MoveFileExA(tmp, g_loadedPath, MOVEFILE_REPLACE_EXISTING);
        }
    }
    return true;
}

const char* GetLoadedPath() {
    return g_loadedPath;
}

bool LegacyFlagEnabled(const char* flagName) {
    /* Try modules/flagName first, then config\\flagName */
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, sizeof(path));
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        lstrcpynA(lastSlash + 1, "modules\\", (int)(path + sizeof(path) - lastSlash - 1));
        strcat_s(path, sizeof(path), flagName);
        HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            return true;
        }
        /* Try config\flagName */
        GetModuleFileNameA(nullptr, path, sizeof(path));
        lastSlash = strrchr(path, '\\');
        if (lastSlash) {
            lstrcpynA(lastSlash + 1, "config\\", (int)(path + sizeof(path) - lastSlash - 1));
            strcat_s(path, sizeof(path), flagName);
            hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                CloseHandle(hFile);
                return true;
            }
        }
    }
    return false;
}

bool EnvFlagEnabled(const char* envName) {
    char buf[8] = {};
    return GetEnvironmentVariableA(envName, buf, (DWORD)sizeof(buf)) > 0 && buf[0] == '1';
}

/* â”€â”€ Combined check: env > INI > legacy .flag file â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static bool CheckEnabled(const char* section_key, const char* envName, const char* flagName, bool defaultValue) {
    if (EnvFlagEnabled(envName)) return true;
    if (GetBool(section_key, false)) return true;
    if (LegacyFlagEnabled(flagName)) return true;
    if (LegacyFlagEnabled(flagName)) { /* try config\ prefix */
        char prefixed[64] = "config\\";
        strcat_s(prefixed, sizeof(prefixed), flagName);
        return LegacyFlagEnabled(prefixed);
    }
    return defaultValue;
}

} // namespace FfxHooks::Config
