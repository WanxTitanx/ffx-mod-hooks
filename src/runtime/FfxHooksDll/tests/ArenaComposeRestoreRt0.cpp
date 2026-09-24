#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../hooks/ArenaComposeRestore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using FfxHooks::ArenaComposeRestore::ComposeAttempt;
using FfxHooks::ArenaComposeRestore::FinalizeComposeAttempt;
using FfxHooks::ArenaComposeRestore::FinalizeComposeAttemptForModule;
using FfxHooks::ArenaComposeRestore::IsStrictBattleId;
using FfxHooks::ArenaComposeRestore::MarkerExists;
using FfxHooks::ArenaComposeRestore::PendingMarkerMatchesBattleId;
using FfxHooks::ArenaComposeRestore::PrepareComposeAttempt;
using FfxHooks::ArenaComposeRestore::PrepareComposeAttemptForModule;
using FfxHooks::ArenaComposeRestore::ProductionDiskTransactionsAvailable;
using FfxHooks::ArenaComposeRestore::PublishComposeAttempt;
using FfxHooks::ArenaComposeRestore::RollbackComposeAttempt;
using FfxHooks::ArenaComposeRestore::RollbackComposeAttemptForModule;
using FfxHooks::ArenaComposeRestore::SealComposeAttempt;
using FfxHooks::ArenaComposeRestore::RestorePending;
using FfxHooks::ArenaComposeRestore::RestorePendingForModule;
using FfxHooks::ArenaComposeRestore::RestorePolicy;
using FfxHooks::ArenaComposeRestore::Result;

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool condition, const char* message) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) {
        std::fprintf(stderr, "fixture write failed: %ls\n", path.c_str());
        std::exit(2);
    }
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()),
            out.data(), needed, nullptr, nullptr) != needed) {
        return {};
    }
    return out;
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    for (const char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

struct Fixture {
    fs::path base;
    fs::path modRoot;
    fs::path vanillaRoot;
    fs::path battleDir;
    fs::path deploy;
    fs::path backup;
    fs::path manifest;
    fs::path marker;
    fs::path legacyManifest;
    std::string battleId = "mcyt00_22";
    unsigned manifestSequence = 0;

    explicit Fixture(const wchar_t* name) {
        wchar_t temp[MAX_PATH] = {};
        const DWORD len = GetTempPathW(MAX_PATH, temp);
        if (len == 0 || len >= MAX_PATH) std::exit(2);
        static unsigned sequence = 0;
        base = fs::path(temp) /
            (std::wstring(L"ffx-hooks-arena-restore-rt0-") +
             std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(++sequence) + L"-" + name);
        modRoot = base / L"mod-btl";
        vanillaRoot = base / L"vanilla-btl";
        battleDir = modRoot / L"mcyt00_22";
        deploy = battleDir / L"mcyt00_22.bin";
        backup = battleDir / L"mcyt00_22.bin.spiraforge.bak";
        manifest = base / L"compose-current.json";
        marker = base / L"arena_plus_compose_restore.pending";
        legacyManifest = base / L"compose_last.json";
        fs::create_directories(battleDir);
        WriteBytes(
            vanillaRoot / L"mcyt00_22" / L"mcyt00_22.bin",
            "ORIGINAL-CANONICAL-BYTES");
        WriteBytes(backup, "ORIGINAL-CANONICAL-BYTES");
        WriteBytes(deploy, "ORIGINAL-CANONICAL-BYTES");
        WriteManifest(deploy);
    }

    ~Fixture() {
        std::error_code ignored;
        fs::remove_all(base, ignored);
    }

    void WriteManifest(const fs::path& declaredDeploy, bool dryRun = false) {
        const std::string path = JsonEscape(Utf8(declaredDeploy.wstring()));
        WriteBytes(
            manifest,
            std::string("{\n") +
            "  \"schema\": \"arena-compose-v1\",\n" +
            "  \"battle_id\": \"" + battleId + "\",\n" +
            "  \"deploy_path\": \"" + path + "\",\n" +
            "  \"dry_run\": " + (dryRun ? "true" : "false") + ",\n" +
            "  \"composed_at\": \"rt0-" + std::to_string(++manifestSequence) + "\"\n" +
            "}\n");
    }

    Result Prepare(ComposeAttempt* attempt) const {
        return PrepareComposeAttempt(
            marker.c_str(), manifest.c_str(), modRoot.c_str(), vanillaRoot.c_str(),
            battleId.c_str(), attempt);
    }

    Result Finalize(const ComposeAttempt& attempt, bool featureEnabled = true) const {
        return FinalizeComposeAttempt(
            marker.c_str(), manifest.c_str(), modRoot.c_str(), attempt, featureEnabled);
    }

    fs::path StageDeploy(const ComposeAttempt& attempt) const {
        return fs::path(attempt.stagingModRoot) / L"mcyt00_22" / L"mcyt00_22.bin";
    }

    void SimulateLabOutput(
        const ComposeAttempt& attempt,
        const std::string& bytes = "COMPOSED-DARK-AEON-BYTES") {
        const fs::path stageDeploy = StageDeploy(attempt);
        fs::path stageBackup = stageDeploy;
        stageBackup += L".spiraforge.bak";
        WriteBytes(stageBackup, "ORIGINAL-CANONICAL-BYTES");
        WriteBytes(stageDeploy, bytes);
        WriteManifest(stageDeploy);
    }

    Result CreateMarker() const {
        Fixture* self = const_cast<Fixture*>(this);
        ComposeAttempt attempt = {};
        const Result prepared = Prepare(&attempt);
        if (prepared != Result::PreflightReady) return prepared;
        self->SimulateLabOutput(attempt);
        return Finalize(attempt);
    }
};

size_t EntryCount(const fs::path& directory) {
    size_t count = 0;
    for (const auto& ignored : fs::recursive_directory_iterator(directory)) {
        (void)ignored;
        ++count;
    }
    return count;
}

void TestStrictBattleIdGrammar() {
    const char* accepted[] = { "mcyt00_22", "ABC123", "a_b_C_9" };
    for (const char* value : accepted)
        Expect(IsStrictBattleId(value), "strict battle-id grammar accepts ASCII alnum/underscore");

    const char* rejected[] = {
        "", ".", "..", "../escape", "..\\escape", "mcyt00-22", "mcyt00 22",
        "C:escape", "/absolute", "\\\\server", "mcyt00_22.bin",
        "abcdefghijklmnopqrstuvwxyzABCDEF"
    };
    for (const char* value : rejected)
        Expect(!IsStrictBattleId(value), "strict battle-id grammar rejects traversal/punctuation/oversize");
}

void TestCreationBindsExactVerifiedDeployment() {
    Fixture fixture(L"create");
    Expect(fixture.CreateMarker() == Result::Created,
        "verified compose deployment creates the self-owned pending marker");
    Expect(MarkerExists(fixture.marker.c_str()), "created marker exists");

    Fixture mismatch(L"manifest-mismatch");
    ComposeAttempt mismatchAttempt = {};
    Expect(mismatch.Prepare(&mismatchAttempt) == Result::PreflightReady,
        "manifest mismatch fixture preflight succeeds");
    mismatch.WriteManifest(mismatch.base / L"outside" / L"mcyt00_22.bin");
    Expect(mismatch.Finalize(mismatchAttempt) == Result::ManifestMismatch,
        "manifest deploy path must equal the canonical derived destination");
    Expect(MarkerExists(mismatch.marker.c_str()),
        "mismatched manifest preserves PREPARED crash authority");
    Expect(
        RollbackComposeAttempt(
            mismatch.marker.c_str(), mismatch.modRoot.c_str(), mismatchAttempt) ==
            Result::AlreadyRestoredConsumed,
        "mismatched staged manifest consumes PREPARED with zero live write");

    Fixture dryRun(L"dry-run");
    ComposeAttempt dryRunAttempt = {};
    Expect(dryRun.Prepare(&dryRunAttempt) == Result::PreflightReady,
        "dry-run fixture preflight succeeds");
    dryRun.WriteManifest(dryRun.deploy, true);
    Expect(dryRun.Finalize(dryRunAttempt) == Result::InvalidManifest,
        "dry-run manifest cannot claim restore ownership");
    Expect(MarkerExists(dryRun.marker.c_str()),
        "dry-run failure preserves PREPARED crash authority");
}

void TestSameDestinationCanRefreshPendingIdentity() {
    Fixture fixture(L"refresh-same-destination");
    Expect(fixture.CreateMarker() == Result::Created, "initial pending marker created");
    Expect(PendingMarkerMatchesBattleId(fixture.marker.c_str(), "mcyt00_22"),
        "pending marker identifies its exact carrier");
    Expect(!PendingMarkerMatchesBattleId(fixture.marker.c_str(), "nagi05_23"),
        "pending marker rejects a different carrier");

    Expect(
        RestorePending(fixture.marker.c_str(), fixture.modRoot.c_str(), { false, true }) ==
            Result::Restored,
        "same carrier refresh first consumes the prior owned deployment");
    ComposeAttempt refreshAttempt = {};
    Expect(fixture.Prepare(&refreshAttempt) == Result::PreflightReady,
        "same carrier refresh starts only after prior recovery is consumed");
    fixture.SimulateLabOutput(refreshAttempt, "SECOND-COMPOSED-VERSION");
    Expect(fixture.Finalize(refreshAttempt) == Result::Created,
        "same carrier refresh publishes a new immutable attempt");
    Expect(
        RestorePending(fixture.marker.c_str(), fixture.modRoot.c_str(), { false, true }) ==
            Result::Restored,
        "refreshed marker restores the original backup");
    Expect(ReadBytes(fixture.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "refreshed marker preserves the first canonical backup");

    Fixture changedSource(L"refresh-source-drift");
    Expect(changedSource.CreateMarker() == Result::Created, "source drift fixture marker created");
    const std::string ownedMarker = ReadBytes(changedSource.marker);
    WriteBytes(changedSource.backup, "FOREIGN-ORIGINAL");
    Expect(
        RestorePending(
            changedSource.marker.c_str(), changedSource.modRoot.c_str(), { false, true }) ==
            Result::SourceHashMismatch,
        "recovery rejects refresh after backup identity drift");
    Expect(ReadBytes(changedSource.marker) == ownedMarker,
        "failed refresh preserves the prior pending marker");
}

void TestManifestFreshnessAndPreflightRollback() {
    Fixture looseBaseline(L"preflight-loose-baseline");
    fs::remove(looseBaseline.backup);
    WriteBytes(looseBaseline.deploy, "ORIGINAL-CANONICAL-BYTES");
    ComposeAttempt looseAttempt = {};
    Expect(looseBaseline.Prepare(&looseAttempt) == Result::PreflightReady,
        "preflight accepts an existing loose canonical carrier");
    Expect(ReadBytes(looseBaseline.backup) == "ORIGINAL-CANONICAL-BYTES",
        "preflight durably owns an exact backup before launching the lab");
    looseBaseline.SimulateLabOutput(looseAttempt, "LAB-COMPOSED-BYTES");
    Expect(looseBaseline.Finalize(looseAttempt) == Result::Created,
        "finalize binds deployment to the preflight-created backup");

    Fixture vanillaBaseline(L"preflight-vanilla-baseline");
    fs::remove(vanillaBaseline.backup);
    fs::remove(vanillaBaseline.deploy);
    ComposeAttempt vanillaAttempt = {};
    Expect(vanillaBaseline.Prepare(&vanillaAttempt) == Result::PreflightReady,
        "preflight accepts the exact vanilla carrier when no loose target exists");
    Expect(ReadBytes(vanillaBaseline.backup) == "ORIGINAL-CANONICAL-BYTES",
        "preflight copies and verifies vanilla bytes into the durable backup");
    vanillaBaseline.SimulateLabOutput(vanillaAttempt, "LAB-COMPOSED-FROM-VANILLA");
    Expect(vanillaBaseline.Finalize(vanillaAttempt) == Result::Created,
        "vanilla fallback finalize binds the exact preflight backup");

    Fixture staleManifest(L"manifest-freshness");
    fs::remove(staleManifest.marker);
    ComposeAttempt attempt = {};
    Expect(staleManifest.Prepare(&attempt) == Result::PreflightReady,
        "preflight captures baseline and pre-launch manifest identity");
    Expect(MarkerExists(staleManifest.marker.c_str()),
        "preflight durably publishes recovery authority before the external lab can run");
    WriteBytes(staleManifest.StageDeploy(attempt), "LAB-WROTE-BUT-MANIFEST-STAYED-STALE");
    Expect(staleManifest.Finalize(attempt) == Result::ManifestStale,
        "unchanged pre-launch manifest cannot authorize marker creation");
    Expect(MarkerExists(staleManifest.marker.c_str()),
        "stale manifest preserves PREPARED crash authority");
    Expect(
        RollbackComposeAttempt(staleManifest.marker.c_str(), staleManifest.modRoot.c_str(), attempt) ==
            Result::AlreadyRestoredConsumed,
        "failed finalization consumes PREPARED after verifying unchanged live bytes");
    Expect(ReadBytes(staleManifest.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "rollback restores the exact pre-launch canonical source");

    Fixture missingBaseline(L"missing-baseline");
    fs::remove(missingBaseline.backup);
    fs::remove(missingBaseline.deploy);
    fs::remove_all(missingBaseline.vanillaRoot);
    ComposeAttempt missingAttempt = {};
    Expect(missingBaseline.Prepare(&missingAttempt) == Result::SourceMissing,
        "compose is rejected before launch when no restorable baseline exists");
    Expect(!MarkerExists(missingBaseline.marker.c_str()),
        "failed preflight creates no marker");
}

void TestPreflightRejectsUnownedBackupAndRollbackPreservesThirdPartyBytes() {
    Fixture foreignBackup(L"foreign-backup-provenance");
    fs::remove(foreignBackup.marker);
    WriteBytes(foreignBackup.deploy, "CURRENT-LIVE-CANONICAL");
    WriteBytes(foreignBackup.backup, "UNOWNED-STALE-BACKUP");
    const std::string liveBefore = ReadBytes(foreignBackup.deploy);
    const std::string backupBefore = ReadBytes(foreignBackup.backup);
    ComposeAttempt foreignAttempt = {};
    Expect(foreignBackup.Prepare(&foreignAttempt) == Result::SourceHashMismatch,
        "preflight rejects an existing backup that does not equal the contained live carrier");
    Expect(ReadBytes(foreignBackup.deploy) == liveBefore,
        "foreign backup rejection preserves live bytes");
    Expect(ReadBytes(foreignBackup.backup) == backupBefore,
        "foreign backup rejection preserves the unowned backup for diagnosis");
    Expect(!MarkerExists(foreignBackup.marker.c_str()),
        "foreign backup rejection creates no recovery authority");

    Fixture absentLiveForeignBackup(L"foreign-backup-with-absent-live");
    fs::remove(absentLiveForeignBackup.deploy);
    WriteBytes(absentLiveForeignBackup.backup, "STALE-LEGACY-BACKUP");
    ComposeAttempt absentLiveAttempt = {};
    Expect(absentLiveForeignBackup.Prepare(&absentLiveAttempt) == Result::SourceHashMismatch,
        "absent live carrier accepts an existing backup only when it equals exact vanilla bytes");
    Expect(ReadBytes(absentLiveForeignBackup.backup) == "STALE-LEGACY-BACKUP",
        "foreign backup beside an absent live carrier is preserved for diagnosis");

    Fixture thirdParty(L"rollback-third-party");
    fs::remove(thirdParty.marker);
    WriteBytes(thirdParty.deploy, "ORIGINAL-CANONICAL-BYTES");
    ComposeAttempt thirdPartyAttempt = {};
    Expect(thirdParty.Prepare(&thirdPartyAttempt) == Result::PreflightReady,
        "third-party rollback fixture preflights from exact canonical bytes");
    WriteBytes(thirdParty.deploy, "THIRD-PARTY-WROTE-DURING-LAB");
    Expect(
        RollbackComposeAttempt(thirdParty.marker.c_str(), thirdParty.modRoot.c_str(), thirdPartyAttempt) ==
            Result::DestinationHashMismatch,
        "rollback refuses to overwrite output that lacks exact transaction ownership");
    Expect(ReadBytes(thirdParty.deploy) == "THIRD-PARTY-WROTE-DURING-LAB",
        "rollback conflict preserves third-party destination bytes");
    Expect(MarkerExists(thirdParty.marker.c_str()),
        "rollback conflict preserves the durable prepared marker");
}

void TestPreparedAndReadyCrashWindowsRecoverWithoutGuessingOwnership() {
    Fixture beforeLab(L"crash-before-lab");
    WriteBytes(beforeLab.deploy, "ORIGINAL-CANONICAL-BYTES");
    ComposeAttempt beforeLabAttempt = {};
    Expect(beforeLab.Prepare(&beforeLabAttempt) == Result::PreflightReady,
        "PREPARED authority is durable before the lab starts");
    Expect(MarkerExists(beforeLab.marker.c_str()), "PREPARED marker exists before lab launch");
    Expect(
        RestorePending(beforeLab.marker.c_str(), beforeLab.modRoot.c_str(), { false, true }) ==
            Result::AlreadyRestoredConsumed,
        "crash before lab consumes PREPARED authority without writing unchanged live bytes");
    Expect(ReadBytes(beforeLab.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "pre-lab recovery preserves the exact prestate");

    Fixture afterPublish(L"crash-after-publish-before-finalize");
    WriteBytes(afterPublish.deploy, "ORIGINAL-CANONICAL-BYTES");
    ComposeAttempt afterPublishAttempt = {};
    Expect(afterPublish.Prepare(&afterPublishAttempt) == Result::PreflightReady,
        "post-publish crash fixture prepares");
    afterPublish.SimulateLabOutput(afterPublishAttempt, "OWNED-STAGED-COMPOSE");
    Expect(
        SealComposeAttempt(
            afterPublish.marker.c_str(), afterPublish.manifest.c_str(), afterPublishAttempt, true) ==
            Result::StagedReady,
        "fresh staged output seals READY authority before live publish");
    Expect(
        PublishComposeAttempt(
            afterPublish.marker.c_str(), afterPublish.modRoot.c_str(), afterPublishAttempt, true) ==
            Result::Published,
        "DLL atomically publishes only the exact READY staged bytes");
    Expect(ReadBytes(afterPublish.deploy) == "OWNED-STAGED-COMPOSE",
        "publish commits the owned staged output");
    Expect(
        RestorePending(afterPublish.marker.c_str(), afterPublish.modRoot.c_str(), { false, true }) ==
            Result::Restored,
        "restart after publish but before FINALIZED restores from READY authority");
    Expect(ReadBytes(afterPublish.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "READY crash recovery restores the exact canonical source");
    Expect(!MarkerExists(afterPublish.marker.c_str()),
        "READY crash recovery consumes its authority exactly once");

    Fixture publishConflict(L"publish-third-party-conflict");
    WriteBytes(publishConflict.deploy, "ORIGINAL-CANONICAL-BYTES");
    ComposeAttempt publishConflictAttempt = {};
    Expect(publishConflict.Prepare(&publishConflictAttempt) == Result::PreflightReady,
        "publish conflict fixture prepares");
    publishConflict.SimulateLabOutput(publishConflictAttempt, "OWNED-STAGED-COMPOSE");
    Expect(
        SealComposeAttempt(
            publishConflict.marker.c_str(),
            publishConflict.manifest.c_str(),
            publishConflictAttempt,
            true) == Result::StagedReady,
        "publish conflict fixture seals exact staged output");
    WriteBytes(publishConflict.deploy, "THIRD-PARTY-BEFORE-PUBLISH");
    Expect(
        PublishComposeAttempt(
            publishConflict.marker.c_str(),
            publishConflict.modRoot.c_str(),
            publishConflictAttempt,
            true) == Result::DestinationHashMismatch,
        "publish refuses a live destination changed after PREPARED");
    Expect(ReadBytes(publishConflict.deploy) == "THIRD-PARTY-BEFORE-PUBLISH",
        "publish conflict preserves third-party bytes");
    Expect(MarkerExists(publishConflict.marker.c_str()),
        "publish conflict preserves READY authority for diagnosis");
}

void TestValidateOnlyIsZeroIoAndFeatureOffOnlyRecoversOwnedPending() {
    Expect(
        RestorePendingForModule(nullptr, nullptr, RestorePolicy{ true, false }) ==
            Result::BlockedValidateOnly,
        "validation-only adapter returns before module and path resolution");

    Fixture validateOnly(L"validate-only");
    Expect(validateOnly.CreateMarker() == Result::Created, "validate-only fixture marker created");
    const std::string validateMarkerBefore = ReadBytes(validateOnly.marker);
    const size_t validateEntriesBefore = EntryCount(validateOnly.base);
    Expect(
        RestorePending(
            validateOnly.marker.c_str(), validateOnly.modRoot.c_str(), RestorePolicy{ true, true }) ==
            Result::BlockedValidateOnly,
        "validation-only blocks restore before filesystem mutation");
    Expect(ReadBytes(validateOnly.deploy) == "COMPOSED-DARK-AEON-BYTES",
        "validation-only leaves deployed bytes unchanged");
    Expect(ReadBytes(validateOnly.marker) == validateMarkerBefore,
        "validation-only neither consumes nor rewrites marker");
    Expect(EntryCount(validateOnly.base) == validateEntriesBefore,
        "validation-only creates no temporary file");

    Fixture featureOff(L"feature-off");
    Expect(featureOff.CreateMarker() == Result::Created, "feature-off fixture marker created");
    Expect(
        RestorePending(
            featureOff.marker.c_str(), featureOff.modRoot.c_str(), RestorePolicy{ false, false }) ==
            Result::Restored,
        "feature OFF may recover an exact self-owned pending deployment");
    Expect(ReadBytes(featureOff.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "feature OFF recovery restores the exact owned backup");
    Expect(!MarkerExists(featureOff.marker.c_str()),
        "feature OFF recovery consumes the verified marker after readback");

    Fixture featureOffNoMarker(L"feature-off-no-marker");
    fs::remove(featureOffNoMarker.marker);
    const std::string noMarkerDeployBefore = ReadBytes(featureOffNoMarker.deploy);
    const size_t noMarkerEntriesBefore = EntryCount(featureOffNoMarker.base);
    Expect(
        RestorePending(
            featureOffNoMarker.marker.c_str(),
            featureOffNoMarker.modRoot.c_str(),
            RestorePolicy{ false, false }) == Result::NoMarker,
        "feature OFF without a self-owned marker performs no recovery");
    Expect(ReadBytes(featureOffNoMarker.deploy) == noMarkerDeployBefore,
        "feature OFF without a marker does not touch destination bytes");
    Expect(EntryCount(featureOffNoMarker.base) == noMarkerEntriesBefore,
        "feature OFF without a marker creates no file");

    Fixture disabledFinalize(L"feature-off-finalize");
    fs::remove(disabledFinalize.marker);
    WriteBytes(disabledFinalize.deploy, "ORIGINAL-CANONICAL-BYTES");
    ComposeAttempt disabledAttempt = {};
    Expect(disabledFinalize.Prepare(&disabledAttempt) == Result::PreflightReady,
        "compose attempt preflight starts while the live feature is ON");
    disabledFinalize.SimulateLabOutput(disabledAttempt, "LAB-WROTE-BEFORE-FEATURE-WENT-OFF");
    Expect(disabledFinalize.Finalize(disabledAttempt, false) == Result::BlockedFeatureOff,
        "feature OFF refuses to create or update a pending marker");
    Expect(MarkerExists(disabledFinalize.marker.c_str()),
        "feature OFF finalization retains PREPARED recovery authority");
    Expect(
        RollbackComposeAttempt(disabledFinalize.marker.c_str(), disabledFinalize.modRoot.c_str(), disabledAttempt) ==
            Result::AlreadyRestoredConsumed,
        "feature OFF transition consumes PREPARED after verifying live was never published");
    Expect(ReadBytes(disabledFinalize.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "feature OFF rollback restores the exact preflight baseline");

    Fixture disabledRefresh(L"feature-off-refresh");
    Expect(disabledRefresh.CreateMarker() == Result::Created,
        "feature-off refresh fixture owns an existing pending marker");
    Expect(
        RestorePending(
            disabledRefresh.marker.c_str(),
            disabledRefresh.modRoot.c_str(),
            RestorePolicy{ false, true }) == Result::Restored,
        "feature-off refresh fixture consumes prior attempt before starting another");
    ComposeAttempt disabledRefreshAttempt = {};
    Expect(disabledRefresh.Prepare(&disabledRefreshAttempt) == Result::PreflightReady,
        "same-carrier refresh preflights while enabled");
    disabledRefresh.SimulateLabOutput(disabledRefreshAttempt, "SECOND-LAB-WRITE-BEFORE-OFF");
    Expect(disabledRefresh.Finalize(disabledRefreshAttempt, false) == Result::BlockedFeatureOff,
        "feature OFF refuses to refresh existing marker authority");
    Expect(MarkerExists(disabledRefresh.marker.c_str()),
        "feature OFF preserves the current PREPARED marker byte-for-byte");
    Expect(
        RollbackComposeAttempt(disabledRefresh.marker.c_str(), disabledRefresh.modRoot.c_str(), disabledRefreshAttempt) ==
            Result::AlreadyRestoredConsumed,
        "feature OFF refresh transition consumes PREPARED with no live publication");
    Expect(!MarkerExists(disabledRefresh.marker.c_str()),
        "feature OFF rollback archives the consumed PREPARED authority");
}

void TestLegacyMalformedTraversalAndHashMismatchFailClosed() {
    Fixture legacy(L"legacy-only");
    WriteBytes(legacy.legacyManifest,
        "{\"schema\":\"arena-compose-v1\",\"battle_id\":\"mcyt00_22\"}");
    Expect(
        RestorePending(legacy.marker.c_str(), legacy.modRoot.c_str(), { false, true }) ==
            Result::NoMarker,
        "legacy compose_last without self-owned marker never authorizes restore");
    Expect(ReadBytes(legacy.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "legacy manifest alone performs no destination write");

    Fixture stale(L"stale-marker");
    WriteBytes(stale.marker, "format=ffx-hooks-arena-compose-restore/v0\n");
    Expect(
        RestorePending(stale.marker.c_str(), stale.modRoot.c_str(), { false, true }) ==
            Result::InvalidMarker,
        "stale marker version fails closed");
    Expect(ReadBytes(stale.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "stale marker performs no destination write");

    Fixture traversal(L"traversal-marker");
    WriteBytes(traversal.marker,
        "format=ffx-hooks-arena-compose-restore/v1\n"
        "battle_id=..\\escape\n"
        "deploy_relative=..\\escape\\escape.bin\n"
        "backup_relative=..\\escape\\escape.bin.spiraforge.bak\n"
        "source_sha256=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
        "deployed_sha256=BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n");
    Expect(
        RestorePending(traversal.marker.c_str(), traversal.modRoot.c_str(), { false, true }) ==
            Result::InvalidMarker,
        "traversal marker fails strict schema validation");
    Expect(ReadBytes(traversal.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "traversal marker performs no destination write");

    Fixture sourceMismatch(L"source-hash-mismatch");
    Expect(sourceMismatch.CreateMarker() == Result::Created, "source mismatch fixture marker created");
    WriteBytes(sourceMismatch.backup, "FOREIGN-BACKUP-BYTES");
    Expect(
        RestorePending(sourceMismatch.marker.c_str(), sourceMismatch.modRoot.c_str(), { false, true }) ==
            Result::SourceHashMismatch,
        "changed backup hash blocks restore");
    Expect(ReadBytes(sourceMismatch.deploy) == "COMPOSED-DARK-AEON-BYTES",
        "source hash mismatch performs no destination write");
    Expect(MarkerExists(sourceMismatch.marker.c_str()), "source mismatch preserves marker for diagnosis/retry");

    Fixture destinationMismatch(L"destination-hash-mismatch");
    Expect(destinationMismatch.CreateMarker() == Result::Created,
        "destination mismatch fixture marker created");
    WriteBytes(destinationMismatch.deploy, "THIRD-PARTY-DEPLOYMENT");
    Expect(
        RestorePending(destinationMismatch.marker.c_str(), destinationMismatch.modRoot.c_str(), { false, true }) ==
            Result::DestinationHashMismatch,
        "changed destination hash blocks overwrite");
    Expect(ReadBytes(destinationMismatch.deploy) == "THIRD-PARTY-DEPLOYMENT",
        "destination hash mismatch preserves third-party bytes");
    Expect(MarkerExists(destinationMismatch.marker.c_str()),
        "destination mismatch preserves marker for diagnosis/retry");
}

void TestValidPendingRestoresOnceAndConsumes() {
    Fixture fixture(L"valid-once");
    Expect(fixture.CreateMarker() == Result::Created, "valid restore fixture marker created");
    Expect(
        RestorePending(fixture.marker.c_str(), fixture.modRoot.c_str(), { false, true }) ==
            Result::Restored,
        "valid pending marker restores through verified transaction");
    Expect(ReadBytes(fixture.deploy) == "ORIGINAL-CANONICAL-BYTES",
        "valid restore publishes exact backup bytes");
    Expect(!MarkerExists(fixture.marker.c_str()), "successful readback consumes marker");
    Expect(
        RestorePending(fixture.marker.c_str(), fixture.modRoot.c_str(), { false, true }) ==
            Result::NoMarker,
        "consumed pending marker cannot execute twice");

    Fixture alreadyRestored(L"already-restored");
    Expect(alreadyRestored.CreateMarker() == Result::Created,
        "already-restored fixture marker created");
    WriteBytes(alreadyRestored.deploy, "ORIGINAL-CANONICAL-BYTES");
    Expect(
        RestorePending(
            alreadyRestored.marker.c_str(), alreadyRestored.modRoot.c_str(), { false, true }) ==
            Result::AlreadyRestoredConsumed,
        "verified already-restored bytes consume stale pending marker without copying");
    Expect(!MarkerExists(alreadyRestored.marker.c_str()),
        "already-restored readback consumes marker once");
}

void TestAtomicReplaceFailurePreservesMarker() {
    Fixture fixture(L"replace-failure");
    Expect(fixture.CreateMarker() == Result::Created, "replace failure fixture marker created");

    HANDLE lock = CreateFileW(
        fixture.deploy.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(lock != INVALID_HANDLE_VALUE, "exclusive destination fixture lock acquired");
    if (lock == INVALID_HANDLE_VALUE) return;

    const Result result = RestorePending(
        fixture.marker.c_str(), fixture.modRoot.c_str(), { false, true });
    CloseHandle(lock);

    Expect(result == Result::IoFailure, "atomic replace failure is reported fail-closed");
    Expect(ReadBytes(fixture.deploy) == "COMPOSED-DARK-AEON-BYTES",
        "failed replace leaves deployed bytes unchanged");
    Expect(MarkerExists(fixture.marker.c_str()), "failed replace preserves marker for retry");
}

void TestProductionAdaptersRemainFailClosed() {
    ComposeAttempt attempt = {};
    Expect(!ProductionDiskTransactionsAvailable(),
        "production must expose one truthful unavailable authority while transactions are quarantined");
    Expect(!FfxHooks::ArenaComposeRestore::ModuleMarkerPresent(nullptr),
        "production quarantine must reject the marker probe before resolving a module path");
    Expect(
        PrepareComposeAttemptForModule(
            nullptr, "missing-manifest", "missing-mod", "missing-vanilla", "mcyt00_22", &attempt) ==
            Result::Quarantined,
        "production preflight is quarantined before resolving or writing any path");
    Expect(
        FinalizeComposeAttemptForModule(
            nullptr, "missing-manifest", "missing-mod", attempt, true) == Result::Quarantined,
        "production finalize is quarantined before publication");
    Expect(
        RollbackComposeAttemptForModule(nullptr, "missing-mod", attempt) == Result::Quarantined,
        "production rollback is quarantined instead of guessing disk ownership");
    Expect(
        RestorePendingForModule(nullptr, "missing-mod", RestorePolicy{ false, true }) ==
            Result::Quarantined,
        "production boot/deferred recovery is quarantined before lock or marker IO");
}

}  // namespace

int main() {
    TestStrictBattleIdGrammar();
    TestCreationBindsExactVerifiedDeployment();
    TestSameDestinationCanRefreshPendingIdentity();
    TestManifestFreshnessAndPreflightRollback();
    TestPreflightRejectsUnownedBackupAndRollbackPreservesThirdPartyBytes();
    TestPreparedAndReadyCrashWindowsRecoverWithoutGuessingOwnership();
    TestValidateOnlyIsZeroIoAndFeatureOffOnlyRecoversOwnedPending();
    TestLegacyMalformedTraversalAndHashMismatchFailClosed();
    TestValidPendingRestoresOnceAndConsumes();
    TestAtomicReplaceFailurePreservesMarker();
    TestProductionAdaptersRemainFailClosed();

    if (g_failures != 0) {
        std::fprintf(stderr, "ArenaComposeRestoreRt0: FAIL (%d/%d checks failed)\n", g_failures, g_checks);
        return 1;
    }
    std::printf("ArenaComposeRestoreRt0: PASS (%d checks)\n", g_checks);
    return 0;
}
