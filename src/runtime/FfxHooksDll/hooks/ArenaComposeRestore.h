#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>

namespace FfxHooks::ArenaComposeRestore {

constexpr wchar_t kPendingMarkerFileName[] = L"arena_plus_compose_restore.pending";

enum class Result : unsigned {
    Created = 0,
    Updated,
    PreflightReady,
    NoRestoreNeeded,
    Restored,
    AlreadyRestoredConsumed,
    BlockedValidateOnly,
    BlockedFeatureOff,
    NoMarker,
    PendingExists,
    InvalidManifest,
    ManifestStale,
    ManifestMismatch,
    InvalidMarker,
    PathRejected,
    SourceMissing,
    DestinationMissing,
    SourceHashMismatch,
    DestinationHashMismatch,
    IoFailure,
    PostRestoreMismatch,
    StagedReady,
    Published,
    Quarantined,
};

struct RestorePolicy {
    bool validateOnly = false;
    // Explicit-path RT0 models owned teardown while feature OFF. Production module adapters remain
    // quarantined and therefore never reach this disk transaction.
    bool featureEnabled = false;
};

// POD snapshot carried across the external compose process. The lab receives stagingModRoot, never
// the live mod root. The immutable attempt id binds PREPARED/READY/FINALIZED authority sidecars.
struct ComposeAttempt {
    char battleId[32] = {};
    char attemptId[33] = {};
    char expectedSourceSha256[65] = {};
    char destinationBeforeSha256[65] = {};
    char manifestBeforeSha256[65] = {};
    wchar_t stagingModRoot[1024] = {};
    bool destinationExisted = false;
    bool manifestExisted = false;
};

// Battle IDs become path components. Keeping the grammar deliberately smaller than NTFS prevents
// separators, drive syntax, dot segments, alternate data streams, and normalization ambiguity.
bool IsStrictBattleId(const char* battleId);

// One public predicate owns the production promotion gate. UI/status code must consult the same
// authority as the module adapters so an offline transaction prototype cannot be advertised LIVE.
bool ProductionDiskTransactionsAvailable();

bool MarkerExists(const wchar_t* markerPath);
bool PendingMarkerMatchesBattleId(const wchar_t* markerPath, const char* battleId);
bool ResolveModuleMarkerPath(HMODULE module, wchar_t* outPath, size_t outPathCount);

// These explicit-path entry points are the testable transaction boundary. Finalize accepts
// responsibility only for output that changed after the matching preflight. RestorePending never
// consults compose_last.json or any other legacy signal.
Result PrepareComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* composeManifestPath,
    const wchar_t* modBtlRoot,
    const wchar_t* vanillaBtlRoot,
    const char* expectedBattleId,
    ComposeAttempt* outAttempt);
Result FinalizeComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* composeManifestPath,
    const wchar_t* modBtlRoot,
    const ComposeAttempt& attempt,
    bool featureEnabled);
Result SealComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* composeManifestPath,
    const ComposeAttempt& attempt,
    bool featureEnabled);
Result PublishComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot,
    const ComposeAttempt& attempt,
    bool featureEnabled);
Result RollbackComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot,
    const ComposeAttempt& attempt);
Result RestorePending(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot,
    RestorePolicy policy);

// ANSI adapters match the existing Arena+ path plumbing while all validation and writes stay in
// the wide-character transaction above.
bool ModuleMarkerPresent(HMODULE module);
Result PrepareComposeAttemptForModule(
    HMODULE module,
    const char* composeManifestPath,
    const char* modBtlRoot,
    const char* vanillaBtlRoot,
    const char* expectedBattleId,
    ComposeAttempt* outAttempt);
Result FinalizeComposeAttemptForModule(
    HMODULE module,
    const char* composeManifestPath,
    const char* modBtlRoot,
    const ComposeAttempt& attempt,
    bool featureEnabled);
Result RollbackComposeAttemptForModule(
    HMODULE module,
    const char* modBtlRoot,
    const ComposeAttempt& attempt);
bool ComposeAttemptStagingModRootAnsi(
    const ComposeAttempt& attempt,
    char* outPath,
    size_t outPathCount);
Result RestorePendingForModule(
    HMODULE module,
    const char* modBtlRoot,
    RestorePolicy policy);

const char* ResultName(Result result);

}  // namespace FfxHooks::ArenaComposeRestore
