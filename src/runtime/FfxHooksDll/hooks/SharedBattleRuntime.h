#include "../shared/ExecutableProfile.h"
#pragma once

#include <atomic>
#include <array>
#include <cstdint>

namespace FfxHooks::SharedBattleRuntime {

// Executable identity: FFX.exe SHA-256 78CE3439...D3DB5CED, PE32 image base 0x00400000.
// IDA read-only xrefs prove that InitScene RVA 0x00383ED0 has exactly these three callers.
inline constexpr uintptr_t kBootstrapInitSceneReturnRva = (::FfxHooks::ExecutableProfile::Rva<0x00381C76u>());
inline constexpr uintptr_t kSphereGridStartupInitSceneReturnRva = (::FfxHooks::ExecutableProfile::Rva<0x003821CFu>());
inline constexpr uintptr_t kBattleStateInitSceneReturnRva = (::FfxHooks::ExecutableProfile::Rva<0x0038321Cu>());

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
    bool arcana = false;
};

// Passive process-lifetime invalidation is separate from CustomMix's reserved
// around-original seam and Seymour's composer. It cannot repeat the producer.
struct ActionObserver {void (*newBattle)() noexcept;};
inline std::atomic<const ActionObserver*> actionObserver{nullptr};
inline bool RegisterActionObserver(const ActionObserver* value) noexcept {
    if(!value||!value->newBattle)return false;
    const ActionObserver* expected=nullptr;
    return actionObserver.compare_exchange_strong(expected,value)||expected==value;
}
inline void UnregisterActionObserver(const ActionObserver* value) noexcept {
    const ActionObserver* expected=value;(void)actionObserver.compare_exchange_strong(expected,nullptr);
}
enum class ActionConsumer : unsigned {Elemental,Aeon,NulWard,Count};
inline std::array<std::atomic<const ActionObserver*>,static_cast<unsigned>(ActionConsumer::Count)> actionConsumers{};
inline bool RegisterActionObserver(ActionConsumer consumer,const ActionObserver* value) noexcept {
    const auto index=static_cast<unsigned>(consumer);
    if(index>=actionConsumers.size()||!value||!value->newBattle)return false;
    const ActionObserver* expected=nullptr;
    return actionConsumers[index].compare_exchange_strong(expected,value)||expected==value;
}
inline void UnregisterActionObserver(ActionConsumer consumer,const ActionObserver* value) noexcept {
    const auto index=static_cast<unsigned>(consumer);if(index>=actionConsumers.size()||!value)return;
    const ActionObserver* expected=value;(void)actionConsumers[index].compare_exchange_strong(expected,nullptr);
}
inline bool ActionConsumersRequested() noexcept {
    if(actionObserver.load()!=nullptr)return true;
    for(const auto& slot:actionConsumers)if(slot.load()!=nullptr)return true;
    return false;
}
inline void NotifyActionConsumers() noexcept {
    const auto* primary=actionObserver.load();if(primary)primary->newBattle();
    for(const auto& slot:actionConsumers){const auto* value=slot.load();if(value)value->newBattle();}
}

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
