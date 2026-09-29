#pragma once
#include <atomic>
#include <cstdint>
namespace FfxHooks::SeymourOverdrive {
// Request and revision are one publication: OFF/ON cannot reauthorize an old event.
class Control {
public:
    std::uint64_t Read() const noexcept {return packet_.load(std::memory_order_acquire);}
    bool Current(std::uint64_t token) const noexcept {
        return (token&1u)!=0&&!stopped_.load(std::memory_order_acquire)&&Read()==token;
    }
    bool Publish(bool requested) noexcept {
        auto old=Read();
        for(;;){
            if(stopped_.load(std::memory_order_acquire))return false;
            if((old&1u)==static_cast<unsigned>(requested))return true;
            if((old>>1)==(UINT64_MAX>>1)){Stop();return false;}
            const auto next=(((old>>1)+1u)<<1)|static_cast<unsigned>(requested);
            if(packet_.compare_exchange_weak(old,next,std::memory_order_acq_rel,std::memory_order_acquire))
                return !stopped_.load(std::memory_order_acquire);
        }
    }
    void Stop() noexcept {stopped_.store(true,std::memory_order_release);}
private:
    std::atomic<std::uint64_t> packet_{0};
    std::atomic<bool> stopped_{false};
};
}
