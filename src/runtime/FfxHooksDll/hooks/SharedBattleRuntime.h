#pragma once

#include <atomic>
#include <cstdint>

namespace FfxHooks::SharedBattleRuntime {

// Executable identity: FFX.exe SHA-256 78CE3439...D3DB5CED, PE32 image base 0x00400000.
// IDA read-only xrefs prove that InitScene RVA 0x00383ED0 has exactly these three callers.
inline constexpr uintptr_t kBootstrapInitSceneReturnRva = 0x00381C76u;
inline constexpr uintptr_t kSphereGridStartupInitSceneReturnRva = 0x003821CFu;
inline constexpr uintptr_t kBattleStateInitSceneReturnRva = 0x0038321Cu;

enum class InitSceneCaller : uint8_t {
    Unknown = 0,
    Bootstrap,
    SphereGridStartup,
    BattleState,
};

struct ConsumerRequest {
    bool difficulty = false;
    bool seymour = false;
    bool sin = false;
    bool customMix = false;
};

// WHY: Playable Seymour is a LIVE flag, so its exact profile-gated entry/exit infrastructure
// must be installed before the user can turn the behavior ON from F8. This capability bit does
// not authorize a roster mutation: the independently default-OFF command is checked inside the
// admitted battle callback, whose OFF path calls vanilla exactly once without calling Assign.
inline constexpr bool kSeymourLiveProducerRequiresInfrastructure = true;

bool AnyConsumerRequiresRuntime(const ConsumerRequest&) noexcept;
InitSceneCaller ClassifyInitSceneCaller(uintptr_t returnRva) noexcept;

struct OriginalIo {
    void* context = nullptr;
    int (*call)(void*) = nullptr;
};

// CustomMix owns this reserved around-original seam only after a separate reviewed consumer is
// registered. Null callbacks are intentionally inert today.
struct ReservedSeamIo {
    void* context = nullptr;
    void (*beforeOriginal)(void*, uintptr_t returnRva) = nullptr;
    void (*afterOriginal)(void*, uintptr_t returnRva, int originalResult) = nullptr;
};

// A composer may bracket the guarded OriginalIo, but cannot execute vanilla more than once. The
// descriptor must have process lifetime because a machine-prologue entrant can outlive teardown.
struct ComposerIo {
    void* context = nullptr;
    void (*compose)(void*, uintptr_t returnRva, const OriginalIo&) = nullptr;
};

struct InitSceneRunResult {
    InitSceneCaller caller = InitSceneCaller::Unknown;
    int originalResult = 0;
    uint32_t originalCallAttempts = 0;
    bool composerInvoked = false;
};

InitSceneRunResult RunInitScene(
    uintptr_t returnRva, const OriginalIo&, const ReservedSeamIo&, const ComposerIo&) noexcept;

enum class ComposerSlotResult : uint8_t {
    Registered = 0,
    AlreadyRegistered,
    Conflict,
    Unregistered,
    NotOwner,
    InvalidArgument,
};

class ComposerSlot {
public:
    ComposerSlot() = default;
    ComposerSlot(const ComposerSlot&) = delete;
    ComposerSlot& operator=(const ComposerSlot&) = delete;

    ComposerSlotResult Register(const ComposerIo*) noexcept;
    ComposerSlotResult Unregister(const ComposerIo*) noexcept;
    const ComposerIo* Load() const noexcept;

private:
    std::atomic<const ComposerIo*> composer_{nullptr};
};

static_assert(std::atomic<const ComposerIo*>::is_always_lock_free);

} // namespace FfxHooks::SharedBattleRuntime
