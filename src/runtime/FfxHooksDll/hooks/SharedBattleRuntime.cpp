#include "SharedBattleRuntime.h"

namespace FfxHooks::SharedBattleRuntime {
namespace {

struct GuardedOriginal {
    OriginalIo original{};
    ReservedSeamIo reserved{};
    uintptr_t returnRva = 0;
    int result = 0;
    uint32_t attempts = 0;
    bool called = false;
};

int CallOriginalOnce(void* context) {
    GuardedOriginal& guarded = *static_cast<GuardedOriginal*>(context);
    ++guarded.attempts;
    if (guarded.called) return guarded.result;
    guarded.called = true;
    if (guarded.reserved.beforeOriginal) {
        guarded.reserved.beforeOriginal(guarded.reserved.context, guarded.returnRva);
    }
    guarded.result = guarded.original.call
        ? guarded.original.call(guarded.original.context)
        : 0;
    if (guarded.reserved.afterOriginal) {
        guarded.reserved.afterOriginal(
            guarded.reserved.context, guarded.returnRva, guarded.result);
    }
    return guarded.result;
}

} // namespace

bool AnyConsumerRequiresRuntime(const ConsumerRequest& request) noexcept {
    return request.difficulty || request.seymour || request.sin || request.customMix;
}

InitSceneCaller ClassifyInitSceneCaller(uintptr_t returnRva) noexcept {
    switch (returnRva) {
        case kBootstrapInitSceneReturnRva: return InitSceneCaller::Bootstrap;
        case kSphereGridStartupInitSceneReturnRva: return InitSceneCaller::SphereGridStartup;
        case kBattleStateInitSceneReturnRva: return InitSceneCaller::BattleState;
        default: return InitSceneCaller::Unknown;
    }
}

InitSceneRunResult RunInitScene(
    uintptr_t returnRva, const OriginalIo& original, const ReservedSeamIo& reserved,
    const ComposerIo& composer) noexcept {
    InitSceneRunResult result{};
    result.caller = ClassifyInitSceneCaller(returnRva);

    // WHY: bootstrap and the Japanese startup path both call the same target, but the latter also
    // loads Sphere Grid buffers. Only the battle state-machine caller may reach behavioral seams.
    const bool battleCaller = result.caller == InitSceneCaller::BattleState;
    GuardedOriginal guarded{original, battleCaller ? reserved : ReservedSeamIo{}, returnRva};
    const OriginalIo guardedIo{&guarded, &CallOriginalOnce};
    if (battleCaller && composer.compose) {
        result.composerInvoked = true;
        composer.compose(composer.context, returnRva, guardedIo);
    }
    if (!guarded.called) (void)CallOriginalOnce(&guarded);
    result.originalResult = guarded.result;
    result.originalCallAttempts = guarded.attempts;
    return result;
}

ComposerSlotResult ComposerSlot::Register(const ComposerIo* composer) noexcept {
    if (!composer || !composer->compose) return ComposerSlotResult::InvalidArgument;
    const ComposerIo* expected = nullptr;
    if (composer_.compare_exchange_strong(
            expected, composer, std::memory_order_release, std::memory_order_acquire)) {
        return ComposerSlotResult::Registered;
    }
    return expected == composer
        ? ComposerSlotResult::AlreadyRegistered
        : ComposerSlotResult::Conflict;
}

ComposerSlotResult ComposerSlot::Unregister(const ComposerIo* composer) noexcept {
    if (!composer) return ComposerSlotResult::InvalidArgument;
    const ComposerIo* expected = composer;
    if (composer_.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return ComposerSlotResult::Unregistered;
    }
    return ComposerSlotResult::NotOwner;
}

const ComposerIo* ComposerSlot::Load() const noexcept {
    return composer_.load(std::memory_order_acquire);
}

} // namespace FfxHooks::SharedBattleRuntime
