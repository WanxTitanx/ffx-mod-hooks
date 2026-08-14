// Arena+ progress sidecar (Fase 6).
// Persistent JSON file at mods/Spira Reforge/arena/progress/spira-arena-progress.json
// tracking which Arena+ ladder rows the player has cleared on this profile, separate
// from the vanilla save. Used by FfxHooksDll to compute tier-lock UI state
// (LOCKED / READY / CLEARED) and never writes vanilla save bytes.
//
// All entry points are no-ops unless ArenaProgress_Enabled() is true (gated by
// arena_plus_progress.flag). Failures NEVER throw; they log and fall back to
// "not cleared".
//
// Victory detection is intentionally NOT wired here. The plumbing exposes
// ArenaProgress_RecordCleared() so a future battle-end hook can call it once
// the engine confirms WIN; until then, FFXHOOKS_ARENAPLUS_FAKE_CLEAR env var
// lets devs seed flags manually for UI testing.

#pragma once

#include <Windows.h>

namespace FfxHooks {

using ArenaProgressLogFn = void (*)(const char* msg);

struct ArenaProgressLoadResult {
    bool ok;
    int  flagsLoaded;
    int  flagsCleared;
};

// Initializes the sidecar reader/writer. Logs status via 'logger' (may be null).
// Looks for the JSON in this order:
//   1. $FFXHOOKS_ARENAPLUS_PROGRESS_PATH (absolute path)
//   2. <DllDir>/mods/Spira Reforge/arena/progress/spira-arena-progress.json
//   3. <DllDir>/spira-arena-progress.json (fallback)
// Returns ok=false if the gate flag is off or any I/O step fails; in either case
// the module stays in "no-op" mode and IsRowCleared() returns false.
ArenaProgressLoadResult ArenaProgress_Initialize(ArenaProgressLogFn logger);

// True iff the gate flag (arena_plus_progress.flag) is on AND a sidecar file is loaded.
bool ArenaProgress_Enabled();

// Lookup: returns true iff `progressFlag` is present in the loaded sidecar with cleared=true.
// Returns false if progressFlag is null/empty or the module is disabled.
bool ArenaProgress_IsRowCleared(const char* progressFlag);

// Records a victory for `progressFlag`. No-op when disabled or when progressFlag is empty.
// `note` is optional free-form evidence (e.g. "RT2 build vX.Y"); pass null to omit.
// Persists synchronously to disk. Returns true if the in-memory state changed.
bool ArenaProgress_RecordCleared(const char* progressFlag, const char* note);

// Number of cleared rows currently loaded (debug/diagnostic).
int ArenaProgress_ClearedCount();

// Resolved sidecar path (may be empty if not initialized). The buffer is owned by the
// module; do not free.
const char* ArenaProgress_SidecarPath();

} // namespace FfxHooks
