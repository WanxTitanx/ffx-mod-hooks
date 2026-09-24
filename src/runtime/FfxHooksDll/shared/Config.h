#pragma once
// Loads one flattened snapshot from _isolated/ffx-hooks.ini.
// Exact getters read that snapshot only; compatibility resolution is explicit below.

#include <cstddef>
#include <cstdint>

namespace FfxHooks::Config {

enum class BoolSource : uint8_t {
    DefaultValue = 0,
    DisableEnvironment,
    LegacyOffFlag,
    Environment,
    AuthoritativeCanonicalIni,
    LegacyIni,
    LegacyFlagModules,
    LegacyFlagConfig,
    LegacyFlagModulesConfig,
    LegacyFlagRoot,
    UnmarkedCanonicalIni
};

struct BoolGateSpec {
    const char* canonicalKey;
    const char* authorityKey;
    const char* legacyKey;
    const char* envName;
    const char* flagName;
    const char* disableEnvName;
    const char* offFlagName;
    const char* globalOffFlagName;
    bool defaultValue;
    // Legacy CheckEnabled treated unmarked true as opt-in and false as additive
    // with a flag file. Only migrated rows that need that contract opt in here.
    bool unmarkedTrueIsLegacyEnable = false;
};

struct BoolGateResult {
    bool value;
    BoolSource source;
};

enum class IntReadState : uint8_t {
    Missing = 0,
    Valid,
    Invalid,
};

struct IntReadResult {
    IntReadState state;
    int value;
};

/// Load the INI or built-in defaults. Read/parse failure preserves the published snapshot.
bool Load();

/// Read a boolean from the exact flattened INI key, otherwise return the supplied default.
bool GetBool(const char* section_key, bool defaultValue);

/// Read a boolean only from the exact flattened INI key.
bool TryGetBoolExact(const char* key, bool* valueOut);

/// Resolve disable environment/off flags > environment override > authoritative canonical INI
/// > legacy INI > legacy flag > eligible unmarked canonical INI > default.
BoolGateResult ResolveBoolGate(const BoolGateSpec& spec);

/// Read an integer from the exact flattened INI key, otherwise return the supplied default.
int  GetInt (const char* section_key, int  defaultValue);

/// Read strict unsigned decimal text from an exact key and distinguish absence from invalid data.
IntReadResult ReadIntExact(const char* section_key, int minimum, int maximum);

/// Read a float from the exact flattened INI key, otherwise return the supplied default.
float GetFloat(const char* section_key, float defaultValue);

/// Read a string from the exact flattened INI key, otherwise return the supplied default.
const char* GetString(const char* section_key, const char* defaultValue);

/// Legacy resolver: environment override > exact INI true > four flag roots > default.
/// Exact INI false falls through; catalog consumers should use ResolveBoolGate instead.
bool CheckEnabled(const char* section_key, const char* envName, const char* flagName, bool defaultValue);

/// Persist an exact INI boolean; environment and flag sources are never written.
bool SetBool(const char* section_key, bool value);

/// Persist one integer through the same atomic full-document replacement as all other setters.
bool SetInt(const char* section_key, int value);

/// Set a string value (writes to INI in runtime). 2026-08-16 (Maechen extracted path).
bool SetString(const char* section_key, const char* value);

struct AuthoritativeBoolUpdate {
    const BoolGateSpec* spec;
    bool value;
};

/// Atomically persist canonical booleans and their optional authority markers in one replacement.
bool SetAuthoritativeBools(const AuthoritativeBoolUpdate* updates, std::size_t count);

/// Atomically persist a canonical boolean and its optional authority marker.
bool SetAuthoritativeBool(const BoolGateSpec& spec, bool value);

/// An explicit Arena+ Music ON archives its own legacy OFF markers. Global OFF
/// and environment overrides remain authoritative; persistence failure restores markers.
bool EnableArenaMusicFromMenu(const BoolGateSpec& spec);

/// Stable diagnostic name for a gate source.
const char* BoolSourceName(BoolSource source);

/// Returns full path of loaded config (for diagnostics).
const char* GetLoadedPath();

/// Check modules, config, modules\config, then the game root for a legacy flag.
bool LegacyFlagEnabled(const char* flagName);

/// Read a process-environment override without persisting it.
bool EnvFlagEnabled(const char* envName);

#ifdef FFXHOOKS_TESTING
using TryEnvBoolFn = bool (*)(void*, const char*, bool*);
using FlagExistsFn = bool (*)(void*, const char*, BoolSource*);
using PersistTextFn = bool (*)(void*, const char*, const char*);
using ArchiveArenaMusicOffFn = bool (*)(void*, bool restore);
struct TestProviders {
    void* context;
    TryEnvBoolFn tryEnvBool;
    FlagExistsFn flagExists;
    PersistTextFn persistText;
    ArchiveArenaMusicOffFn archiveArenaMusicOff = nullptr;
};
void SetProvidersForTests(const TestProviders& providers);
bool LoadTextForTests(const char* iniText, const char* path);
void ResetForTests();
#endif

} // namespace FfxHooks::Config
