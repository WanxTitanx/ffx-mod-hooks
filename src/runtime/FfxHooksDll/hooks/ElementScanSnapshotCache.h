#pragma once
#include "ElementalScanView.h"
#include <array>
#include <atomic>

namespace FfxHooks::ElementalScanView {
// The battle owner publishes values; the UI copies an immutable page without
// reading actors, synchronizing status state, or calling native gameplay helpers.
class SnapshotCache {
    struct Frame {
        unsigned actor=31;std::uint64_t generation=0;
        std::array<Snapshot,MaximumPages> pages{};
        std::array<bool,MaximumPages> valid{};
    };
    struct Slot {std::atomic<unsigned> users{0};Frame frame{};};
    static constexpr unsigned Writing=~0u;
    std::array<Slot,3> slots_{};
    std::atomic<unsigned> published_{3};
public:
    using ReadPage=bool(*)(unsigned,unsigned,Snapshot&) noexcept;
    bool Publish(unsigned actor,std::uint64_t generation,ReadPage read) noexcept {
        if(actor>=31||!generation||!read)return false;
        const unsigned active=published_.load(std::memory_order_acquire);
        for(unsigned index=0;index<slots_.size();++index){
            if(index==active)continue;
            unsigned expected=0;
            if(!slots_[index].users.compare_exchange_strong(expected,Writing,std::memory_order_acq_rel))continue;
            auto& frame=slots_[index].frame;frame={};frame.actor=actor;frame.generation=generation;
            bool any=false;
            for(unsigned page=0;page<MaximumPages;++page){
                frame.valid[page]=read(actor,page,frame.pages[page])&&frame.pages[page].generation==generation;
                any=any||frame.valid[page];
            }
            slots_[index].users.store(0,std::memory_order_release);
            if(any)published_.store(index,std::memory_order_release);
            return any;
        }
        return false;
    }
    bool Copy(unsigned actor,unsigned page,std::uint64_t generation,Snapshot& output) noexcept {
        output={};if(actor>=31||page>=MaximumPages||!generation)return false;
        for(unsigned attempt=0;attempt<3;++attempt){
            const unsigned index=published_.load(std::memory_order_acquire);
            if(index>=slots_.size())return false;
            auto& slot=slots_[index];unsigned users=slot.users.load(std::memory_order_acquire);
            if(users==Writing||users>=Writing-1)continue;
            if(!slot.users.compare_exchange_strong(users,users+1,std::memory_order_acq_rel))continue;
            if(published_.load(std::memory_order_acquire)!=index){slot.users.fetch_sub(1,std::memory_order_release);continue;}
            const bool valid=slot.frame.actor==actor&&slot.frame.generation==generation&&slot.frame.valid[page];
            if(valid)output=slot.frame.pages[page];
            slot.users.fetch_sub(1,std::memory_order_release);
            return valid;
        }
        return false;
    }
};
} // namespace FfxHooks::ElementalScanView
