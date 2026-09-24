#pragma once

#include <cstdint>

namespace FfxHooks::ResolverOwner {

// ResolveEncounter is a single machine entry, so it cannot safely host independent PolyHook
// and MinHook trampolines. This portable plan is captured once at startup before either owner
// can create a detour. F7 wins because its three-target Difficulty batch requires pristine
// ResolveEncounter bytes and composes the encounter consumers behind one lifecycle owner.
enum class SharedResolverStartupOwner : uint8_t {
    None,
    ResolverLog,
    F7Difficulty,
};

enum class SharedResolverStartupReason : uint8_t {
    NotRequested,
    ResolverLogSelected,
    F7Reserved,
    ConflictF7ResolverOwner,
};

struct SharedResolverStartupPlan {
    SharedResolverStartupOwner owner = SharedResolverStartupOwner::None;
    SharedResolverStartupReason reason = SharedResolverStartupReason::NotRequested;
};

struct SharedResolverStartupExecution {
    bool installAttempted = false;
    bool installed = false;
};

using SharedResolverInstallAttempt = bool (*)(void*) noexcept;

inline SharedResolverStartupPlan PlanSharedResolverStartup(
    bool resolverLogRequested, bool f7DifficultyRequested) noexcept {
    if (f7DifficultyRequested) {
        return {
            SharedResolverStartupOwner::F7Difficulty,
            resolverLogRequested
                ? SharedResolverStartupReason::ConflictF7ResolverOwner
                : SharedResolverStartupReason::F7Reserved,
        };
    }
    if (resolverLogRequested) {
        return {
            SharedResolverStartupOwner::ResolverLog,
            SharedResolverStartupReason::ResolverLogSelected,
        };
    }
    return {};
}

inline const char* SharedResolverStartupReasonName(
    SharedResolverStartupReason reason) noexcept {
    switch (reason) {
    case SharedResolverStartupReason::NotRequested:
        return "RESOLVER_NOT_REQUESTED";
    case SharedResolverStartupReason::ResolverLogSelected:
        return "RESOLVER_LOG_SELECTED";
    case SharedResolverStartupReason::F7Reserved:
        return "F7_RESOLVER_OWNER_RESERVED";
    case SharedResolverStartupReason::ConflictF7ResolverOwner:
        return "CONFLICT_F7_RESOLVER_OWNER";
    default:
        return "UNKNOWN_RESOLVER_OWNER_REASON";
    }
}

inline SharedResolverStartupExecution ExecuteSharedResolverStartup(
    const SharedResolverStartupPlan& plan, void* context,
    SharedResolverInstallAttempt installAttempt) noexcept {
    // WHY: putting the callback behind the same tested owner decision makes both-ON incapable
    // of reaching InstallResolverLogHook, instead of relying on fragile call-site ordering.
    if (plan.owner != SharedResolverStartupOwner::ResolverLog ||
        plan.reason != SharedResolverStartupReason::ResolverLogSelected ||
        !installAttempt) {
        return {};
    }
    return {true, installAttempt(context)};
}

} // namespace FfxHooks::ResolverOwner
