#pragma once
#include <atomic>
#include <cstdint>

namespace FfxHooks::RecoveryBattleEpoch {
struct Snapshot {std::uint32_t generation=0,thread=0;};
// The shared native InitScene owner publishes one packed value, only for the
// genuine battle-state caller. No new detour or second battle composer is added.
inline std::atomic<std::uint64_t> publication{0};
inline Snapshot Read() noexcept {
    const auto value=publication.load(std::memory_order_acquire);
    return {static_cast<std::uint32_t>(value>>32),static_cast<std::uint32_t>(value)};
}
inline void Begin(std::uint32_t thread) noexcept {
    if(!thread)return;
    auto current=publication.load(std::memory_order_acquire);
    for(;;){
        const auto generation=static_cast<std::uint32_t>(current>>32);
        // Exhaustion closes this auxiliary feature; never recycle a live epoch.
        if(generation==UINT32_MAX)return;
        const auto next=(std::uint64_t(generation+1)<<32)|thread;
        if(publication.compare_exchange_weak(current,next,std::memory_order_acq_rel))return;
    }
}
inline bool OwnedBy(const Snapshot& value,std::uint32_t thread) noexcept {
    return value.generation!=0&&value.generation!=UINT32_MAX&&value.thread==thread;
}
} // namespace FfxHooks::RecoveryBattleEpoch
