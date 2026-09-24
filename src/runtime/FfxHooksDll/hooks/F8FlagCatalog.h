#pragma once

#include "../shared/Config.h"

#include <cstddef>
#include <cstdint>

namespace FfxHooks {

enum class F8Activation : uint8_t { Live = 0, RestartRequired, NotWired, ReadOnly };
// ConfigPolled rows can acknowledge freshly resolved config. RuntimeAcknowledged rows remain
// pending until their producer publishes readback; None has no live application contract.
enum class F8ApplyMode : uint8_t { None = 0, ConfigPolled, RuntimeAcknowledged };
enum class F8RuntimeAvailability : uint8_t {
    NotApplicable = 0,
    Pending,
    Available,
    UnsupportedBuild,
    ProducerUnavailable,
    SignatureMismatch,
    RestorePending,
    Conflict,
    PlatformLimited
};

struct F8ScalarSpec {
    const char* canonicalKey;
    int defaultValue;
    int minimum;
    int maximum;
};

enum class F8ScalarState : uint8_t { NotApplicable = 0, Defaulted, Valid, Invalid };

struct F8ScalarResult {
    F8ScalarState state;
    int value;
};

struct F8FlagSpec {
    const char* tab;
    const char* label;
    Config::BoolGateSpec gate;
    const char* help;
    F8Activation activation;
    F8ApplyMode applyMode;
    // Scalar parameters belong to their legacy boolean row so catalog identity stays stable.
    const F8ScalarSpec* scalar = nullptr;
};

struct F8RuntimeStatus {
    // Producer readback is independent of the requested and source-resolved config values.
    F8RuntimeAvailability availability;
    bool hasAppliedValue;
    bool appliedValue;
    // A boolean debug-gate readback cannot prove which multiplier the inline stub consumed.
    bool hasAppliedScalar = false;
    int appliedScalar = 0;
};

enum class F8EditCode : uint8_t {
    Saved = 0,
    RejectedNotWired,
    RejectedUnavailable,
    RejectedInvalidParameter,
    PersistFailed
};

struct F8EditResult {
    F8EditCode code;
    bool requestedValue;
    Config::BoolGateResult effective;
    F8RuntimeStatus runtime;
};

// Per-row truth for bulk tab actions: "blocked" was three different causes collapsed
// into one counter. Quarantined rows are Unavailable, off-flag/env dominance is
// ExternalOverride, bad scalars are InvalidParameter, and a post-write effective value
// that still disagrees for an unrecognized reason is EffectiveMismatch.
enum class F8BulkRowCode : uint8_t {
    Changed = 0,
    Already,
    Unavailable,
    ExternalOverride,
    InvalidParameter,
    EffectiveMismatch,
    PersistFailed,
};

struct F8BulkRowResult {
    const F8FlagSpec* flag;
    F8BulkRowCode code;
    Config::BoolSource source;
};

constexpr size_t kF8BulkRowResultMax = 64;   // bounded catalog; no allocation in the pump

struct F8BulkEditResult {
    bool requestedValue;
    size_t eligible;
    size_t changed;
    size_t already;
    size_t unavailable;
    size_t externalOverride;
    size_t invalidParameter;
    size_t effectiveMismatch;
    bool persistFailed;
    F8BulkRowResult rows[kF8BulkRowResultMax];
    size_t rowCount;
};

enum class F8ScalarEditCode : uint8_t {
    Saved = 0,
    RejectedNotApplicable,
    RejectedInvalid,
    RejectedUnavailable,
    PersistFailed,
};

struct F8ScalarEditResult {
    F8ScalarEditCode code;
    int requestedValue;
    F8ScalarResult configured;
    F8RuntimeStatus runtime;
};

size_t F8FlagCount();
const F8FlagSpec& F8FlagAt(size_t index);
size_t F8TabCount();
const char* F8TabName(size_t index);
const F8FlagSpec* FindF8Flag(const char* canonicalKey);
Config::BoolGateResult ResolveF8Flag(const F8FlagSpec& flag);
F8ScalarResult ResolveF8Scalar(const F8FlagSpec& flag);
F8RuntimeStatus GetF8RuntimeStatus(const F8FlagSpec& flag);
F8EditResult SetF8FlagValue(const F8FlagSpec& flag, bool requestedValue);
F8BulkEditResult SetF8TabValues(const char* tab, bool requestedValue);
const char* F8BulkRowCodeName(F8BulkRowCode code);
// Names the concrete artifact currently dominating a gate (env var or .off filename)
// when the source implies one, so reports can say what holds a row.
const char* F8GateSourceDetail(const F8FlagSpec& flag, Config::BoolSource source);
F8ScalarEditResult SetF8ScalarValue(const F8FlagSpec& flag, int requestedValue);
bool PublishF8RuntimeStatus(
    const char* canonicalKey,
    F8RuntimeAvailability availability,
    bool hasAppliedValue,
    bool appliedValue);
bool PublishF8RuntimeScalarStatus(
    const char* canonicalKey,
    F8RuntimeAvailability availability,
    bool hasAppliedValue,
    bool appliedValue,
    bool hasAppliedScalar,
    int appliedScalar);
const char* F8ActivationName(F8Activation activation);
const char* F8AvailabilityName(F8RuntimeAvailability availability);

} // namespace FfxHooks
