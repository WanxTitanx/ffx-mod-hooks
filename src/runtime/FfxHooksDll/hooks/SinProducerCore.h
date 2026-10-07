#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace FfxHooks::SinProducer {
using NaturalObserver = void (*)(int, int, float, int, std::uintptr_t);
using RewardView = const void* (*)(const void*);
struct Observers { NaturalObserver natural=nullptr; RewardView reward=nullptr; };

// A resident producer accepts one resident consumer. Stop removes only that
// consumer; the original gateway remains callable for the process lifetime.
class Slot {
    std::atomic<const Observers*> observers_{nullptr};
public:
    bool Attach(const Observers* observers) noexcept {
        if (!observers || (!observers->natural && !observers->reward)) return false;
        const Observers* expected=nullptr;
        return observers_.compare_exchange_strong(expected, observers) || expected==observers;
    }
    void Detach(const Observers* observers) noexcept {
        observers_.compare_exchange_strong(observers, nullptr);
    }
    void Natural(int field,int group,float distance,int result,std::uintptr_t caller) const {
        const auto* observers=observers_.load(std::memory_order_acquire);
        if (observers && observers->natural) observers->natural(field,group,distance,result,caller);
    }
    const void* Reward(const void* original) const {
        const auto* observers=observers_.load(std::memory_order_acquire);
        if (observers && observers->reward) {
            const auto* view=observers->reward(original);
            if (view) return view;
        }
        return original;
    }
};

inline bool OwnsJump(const unsigned char* bytes,std::uintptr_t site,std::uintptr_t replacement) noexcept {
    if (!bytes || bytes[0]!=0xE9 || site>UINT32_MAX-5u || replacement>UINT32_MAX) return false;
    std::uint32_t delta=0;std::memcpy(&delta,bytes+1,sizeof(delta));
    return static_cast<std::uint32_t>(site+5u)+delta==static_cast<std::uint32_t>(replacement);
}

template<class NativeReward>
void ForwardRewards(const Slot& observers,NativeReward native,int id,void* actor,
                    const void* loot,int overkill,int extra) {
    // The fifth native argument is read near the end of the reward function.
    // Preserve it even when the consumer supplies a read-only loot view.
    native(id,actor,observers.Reward(loot),overkill,extra);
}
} // namespace FfxHooks::SinProducer
