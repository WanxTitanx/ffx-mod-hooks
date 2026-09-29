#pragma once
#include "SphereGridProgress8CommitCore.h"
#include "RonsoPoolSave.h"

namespace FfxHooks::SphereGridProgress8Save {
namespace Codec=SphereGridProgress8;
namespace Commit=SphereGridProgress8Commit;
using Hash=SphereGridProgress::Hash;
enum class Persist {Written,Unchanged,Rejected,Unavailable,Retired,Unverified};
struct Admission {
    void* context=nullptr;
    bool (*current)(void*,const Codec::Key&)=nullptr;
};
struct Io {
    void* context=nullptr;
    bool (*describe)(void*,Commit::Scope&,Codec::Key&)=nullptr;
    bool (*capture)(void*,const Commit::Scope&,Codec::Snapshot&)=nullptr;
    bool (*identify)(void*,const wchar_t*,const RonsoPool::SaveImage&,Hash&,Hash&)=nullptr;
    bool (*current)(void*,const Commit::Scope&,const Codec::Key&)=nullptr;
    Persist (*publish)(void*,const Codec::Key&,const Codec::Snapshot&,const Admission&)=nullptr;
    bool Valid() const noexcept {return describe&&capture&&identify&&current&&publish;}
};

// Jarvis-HOOK. Consumes the existing native publisher's staged/verified/aborted
// events. Disk I/O never happens on Stage, and live state is never recaptured on
// Verified. Platform glue supplies profile/session/hash and disk endpoints.
class Service {
public:
    bool Configure(const Io& io) noexcept {
        if(!io.Valid()||attempted_.exchange(true,std::memory_order_acq_rel))return false;
        io_=io;configured_.store(true,std::memory_order_release);return Ready();
    }
    bool Stage(std::uint64_t ticket,const wchar_t* path,const RonsoPool::SaveImage& image) noexcept {
        Operation operation(busy_);
        if(!operation.held||!Ready()){queue_.Abort(ticket);return false;}
        const auto revision=revision_.load(std::memory_order_acquire);
        try {
            Commit::Scope scope{};Codec::Key key{};Codec::Snapshot snapshot;
            if(!Describe(path,image,scope,key)||!Current(revision,scope,key)||
               !io_.capture(io_.context,scope,snapshot)||!Current(revision,scope,key)||
               !queue_.Stage(ticket,scope,key,snapshot)||!Current(revision,scope,key)){
                queue_.Abort(ticket);return false;
            }
            return true;
        }catch(...){queue_.Abort(ticket);RequestStop();return false;}
    }
    Persist Verified(std::uint64_t ticket,const wchar_t* path,const RonsoPool::SaveImage& image) noexcept {
        Operation operation(busy_);
        // A nested confirmation must not cancel a publication currently inside
        // the disk callback. Its original holder alone finishes that lease.
        if(!operation.held)return Persist::Rejected;
        if(!Ready()){queue_.Abort(ticket);return Persist::Rejected;}
        const auto revision=revision_.load(std::memory_order_acquire);
        Commit::Publication publication{};
        struct Finish {
            Commit::Queue& queue;Commit::Publication& value;
            ~Finish(){queue.Finish(value);}
        } finish{queue_,publication};
        try {
            Commit::Scope scope{};Codec::Key key{};
            if(!Describe(path,image,scope,key)||!Current(revision,scope,key)){
                queue_.Abort(ticket);return Persist::Rejected;
            }
            if(!queue_.Verify(ticket,scope,key,publication))return Persist::Rejected;
            Codec::Snapshot frozen;
            if(!publication.bytes||!Codec::Decode(*publication.bytes,key,frozen))return Persist::Rejected;
            DiskCall call{this,&publication,revision};
            const Admission admission{&call,&DiskCurrent};
            if(!DiskCurrent(&call,key))return Persist::Rejected;
            const auto result=io_.publish(io_.context,key,frozen,admission);
            if((result==Persist::Written||result==Persist::Unchanged)&&!DiskCurrent(&call,key))
                return Persist::Retired;
            return result;
        }catch(...){RequestStop();return Persist::Unavailable;}
    }
    void Abort(std::uint64_t ticket) noexcept {queue_.Abort(ticket);}
    void Invalidate() noexcept {
        auto previous=revision_.load(std::memory_order_acquire);
        for(;;){
            if(previous==UINT64_MAX){RequestStop();break;}
            if(revision_.compare_exchange_weak(previous,previous+1,std::memory_order_acq_rel,
                                               std::memory_order_acquire))break;
        }
        queue_.Invalidate();
    }
    void RequestStop() noexcept {
        stopping_.store(true,std::memory_order_release);queue_.RequestStop();
    }
private:
    struct Operation {
        std::atomic_flag& flag;bool held;
        explicit Operation(std::atomic_flag& value) noexcept
            :flag(value),held(!value.test_and_set(std::memory_order_acquire)){}
        ~Operation(){if(held)flag.clear(std::memory_order_release);}
    };
    struct DiskCall {Service* service;const Commit::Publication* publication;std::uint64_t revision;};
    bool Ready() const noexcept {
        return configured_.load(std::memory_order_acquire)&&!stopping_.load(std::memory_order_acquire);
    }
    bool Describe(const wchar_t* path,const RonsoPool::SaveImage& image,
                  Commit::Scope& scope,Codec::Key& key) {
        return path&&*path&&RonsoPool::IsValidSave(image)&&io_.describe(io_.context,scope,key)&&
            scope.Valid()&&io_.identify(io_.context,path,image,key.path,key.image)&&key.Valid();
    }
    bool Current(std::uint64_t revision,const Commit::Scope& scope,const Codec::Key& key) {
        return Ready()&&revision_.load(std::memory_order_acquire)==revision&&
            io_.current(io_.context,scope,key)&&Ready()&&revision_.load(std::memory_order_acquire)==revision;
    }
    static bool DiskCurrent(void* context,const Codec::Key& key) noexcept {
        auto& call=*static_cast<DiskCall*>(context);auto& service=*call.service;
        const auto& publication=*call.publication;
        try {
            return key==publication.key&&service.Current(call.revision,publication.scope,key)&&
                service.queue_.Current(publication,publication.scope)&&
                service.Current(call.revision,publication.scope,key);
        }catch(...){service.RequestStop();return false;}
    }
    Io io_{};
    Commit::Queue queue_;
    std::atomic<bool> attempted_{false},configured_{false},stopping_{false};
    std::atomic<std::uint64_t> revision_{1};
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;
};
} // namespace FfxHooks::SphereGridProgress8Save
