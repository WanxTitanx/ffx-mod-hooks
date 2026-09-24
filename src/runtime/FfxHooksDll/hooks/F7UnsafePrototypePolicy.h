#pragma once

#include <cstdint>

namespace FfxHooks {

// These identifiers cover prototypes whose old implementations could launch
// helper processes or materialize battle data. They intentionally do not cover
// the normal RAM-first F7 editors or the established Arena+ Compose routes.
enum class F7UnsafePrototype : std::uint8_t {
    CustomMixUltra = 0,
    LegacySinWriter = 1,
};

struct F7UnsafePrototypeStatus {
    bool available;
    bool requested;
    const char* reason;
};

// WHY: this containment layer deliberately ignores stale configuration sources.
// Reporting them as requested would imply a reader/telemetry contract that the
// production adapters do not have. One portable decision keeps every caller
// unavailable and prevents the menu and installer from drifting apart.
inline F7UnsafePrototypeStatus ResolveF7UnsafePrototype(
    F7UnsafePrototype prototype,
    bool flagRequested,
    bool environmentRequested) noexcept {
    (void)flagRequested;
    (void)environmentRequested;
    constexpr bool requested = false;
    switch (prototype) {
    case F7UnsafePrototype::CustomMixUltra:
        return {
            false,
            requested,
            "Unavailable: isolated transactional output and encounter carrier are not validated.",
        };
    case F7UnsafePrototype::LegacySinWriter:
        return {
            false,
            requested,
            "Unavailable: legacy disk writer is quarantined; no S.I.N. areas are supported.",
        };
    default:
        return {false, requested, "Unavailable: unknown prototype."};
    }
}

} // namespace FfxHooks
