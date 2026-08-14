#pragma once
// Config.h — single-config INI parser (replaces 50+ .flag files)
// _isolated/ffx-hooks.ini with dual-source fallback (.flag files)
// Priority: env var > INI > legacy .flag file

#include <cstdint>

namespace FfxHooks::Config {

/// Load INI from _isolated/ffx-hooks.ini. Returns true if loaded.
bool Load();

/// Get a boolean value. Checks env > INI > default.
bool GetBool(const char* section_key, bool defaultValue);

/// Get an integer value. Checks env > INI > default.
int  GetInt (const char* section_key, int  defaultValue);

/// Get a float value. Checks env > INI > default.
float GetFloat(const char* section_key, float defaultValue);

/// Get a string value. Checks env > INI > default.
const char* GetString(const char* section_key, const char* defaultValue);

/// Set a boolean value (writes to INI in runtime).
bool SetBool(const char* section_key, bool value);

/// Returns full path of loaded config (for diagnostics).
const char* GetLoadedPath();

/// Legacy .flag file check (deprecated, will be removed)
bool LegacyFlagEnabled(const char* flagName);

/// Env var check (persists as override)
bool EnvFlagEnabled(const char* envName);

} // namespace FfxHooks::Config
