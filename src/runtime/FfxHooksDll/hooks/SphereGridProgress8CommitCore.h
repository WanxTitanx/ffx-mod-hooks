#pragma once
#include "SphereGridProgress8Core.h"
#include <memory>
#include <mutex>

namespace FfxHooks::SphereGridProgress8Commit {
// Jarvis-HOOK. The native publisher owns tickets; this queue only owns immutable
// companion bytes. Verifying a ticket does not establish filesystem success.
struct Scope {
    std::uint64_t session=0;
    std::uint32_t thread=0;
    std::uint64_t layoutGeneration=0,request=0;
    bool Valid() const noexcept {return session&&thread&&layoutGeneration&&request;}
    friend bool operator==(const Scope& a,const Scope& b) noexcept {
        return a.session==b.session&&a.thread==b.thread&&
            a.layoutGeneration==b.layoutGeneration&&a.request==b.request;
    }
};
struct Publication {
    std::uint64_t ticket=0;
    Scope scope{};
    SphereGridProgress8::Key key{};
    std::shared_ptr<const SphereGridProgress8::Bytes> bytes;
};
class Queue {
public:
    static constexpr std::size_t Capacity=8;
    bool Stage(std::uint64_t ticket,const Scope& scope,const SphereGridProgress8::Key& key,
               const SphereGridProgress8::Snapshot& state) noexcept {
        if(stopping_.load(std::memory_order_acquire))return false;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_.load(std::memory_order_acquire)||!ticket)return false;
            // Duplicate staging revokes, rather than overwriting, provenance.
            if(auto* duplicate=Find(ticket)){*duplicate={};return false;}
            if(ticket<=highestTicket_)return false;
            highestTicket_=ticket;
            if(!scope.Valid()||!key.Valid()||!state.Valid())return false;
            Slot* available=nullptr;
            for(auto& slot:slots_)if(!slot.value.ticket){available=&slot;break;}
            if(!available)return false;
            SphereGridProgress8::Bytes encoded;
            if(!SphereGridProgress8::Encode(key,state,encoded))return false;
            auto bytes=std::make_shared<const SphereGridProgress8::Bytes>(std::move(encoded));
            if(stopping_.load(std::memory_order_acquire))return false;
            available->value={ticket,scope,key,std::move(bytes)};
            available->issued=false;
            return true;
        }catch(...){RequestStop();return false;}
    }
    bool Verify(std::uint64_t ticket,const Scope& scope,const SphereGridProgress8::Key& key,
                Publication& output) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            auto* slot=Find(ticket);
            if(!slot||slot->issued)return false;
            if(stopping_.load(std::memory_order_acquire)||!scope.Valid()||
               !(slot->value.scope==scope)||!(slot->value.key==key)){
                *slot={};return false;
            }
            // Keep the slot during disk publication. A replay cannot acquire a
            // second lease or revoke the operation already using the first one.
            slot->issued=true;output=slot->value;return true;
        }catch(...){RequestStop();return false;}
    }
    bool Current(const Publication& publication,const Scope& scope) noexcept {
        if(stopping_.load(std::memory_order_acquire)||!scope.Valid()||
           !(publication.scope==scope))return false;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto* slot=Find(publication.ticket);
            return slot&&slot->issued&&Same(slot->value,publication)&&
                !stopping_.load(std::memory_order_acquire);
        }catch(...){RequestStop();return false;}
    }
    void Finish(const Publication& publication) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(auto* slot=Find(publication.ticket)){
                if(slot->issued&&Same(slot->value,publication))*slot={};
            }
        }catch(...){RequestStop();}
    }
    void Abort(std::uint64_t ticket) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);if(auto* slot=Find(ticket))*slot={};}
        catch(...){RequestStop();}
    }
    void Invalidate() noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);for(auto& slot:slots_)slot={};}
        catch(...){RequestStop();}
    }
    // Loader-lock fallback: close admission without locking or freeing buffers.
    void RequestStop() noexcept {stopping_.store(true,std::memory_order_release);}
private:
    struct Slot {Publication value{};bool issued=false;};
    Slot* Find(std::uint64_t ticket) noexcept {
        if(ticket)for(auto& slot:slots_)if(slot.value.ticket==ticket)return &slot;
        return nullptr;
    }
    static bool Same(const Publication& a,const Publication& b) noexcept {
        return a.ticket==b.ticket&&a.scope==b.scope&&a.key==b.key&&a.bytes&&a.bytes==b.bytes&&
            !a.bytes.owner_before(b.bytes)&&!b.bytes.owner_before(a.bytes);
    }
    std::array<Slot,Capacity> slots_{};
    std::mutex mutex_;
    std::atomic<bool> stopping_{false};
    std::uint64_t highestTicket_=0;
};
static_assert(std::atomic<bool>::is_always_lock_free,"Grid8 detach stop must not lock");
} // namespace FfxHooks::SphereGridProgress8Commit
