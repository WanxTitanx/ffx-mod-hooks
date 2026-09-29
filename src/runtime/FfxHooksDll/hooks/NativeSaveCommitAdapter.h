#pragma once
#include "NativeSaveCommitCore.h"
#include "NativeSaveEvents.h"
#include <atomic>
#include <memory>

namespace FfxHooks::NativeSaveCommit {
struct Io {
    bool (*ready)() noexcept=nullptr;
    std::uint32_t (*epoch)() noexcept=nullptr;
    std::uint32_t (*thread)() noexcept=nullptr;
    bool (*file)(void*,FileIdentity&) noexcept=nullptr;
    bool (*readback)(const wchar_t*,FileIdentity&,RonsoPool::SaveImage&) noexcept=nullptr;
    std::uint32_t (*getError)() noexcept=nullptr;
    void (*setError)(std::uint32_t) noexcept=nullptr;
    int* (*errorNumber)() noexcept=nullptr;
    bool Valid() const noexcept {
        return ready&&epoch&&thread&&file&&readback&&getError&&setError&&errorNumber;
    }
};

class Runtime {
public:
    bool Configure(Io io) noexcept {
        if(!io.Valid()||attempted_.exchange(true,std::memory_order_acq_rel))return false;
        io_=io;configured_.store(true,std::memory_order_release);
        return !stopping_.load(std::memory_order_acquire);
    }
    void Stop() noexcept {stopping_.store(true,std::memory_order_release);}

    void BeforeWrite(void* stream) noexcept {
        if(!configured_.load(std::memory_order_acquire))return;
        ErrorScope errors(io_);
        try {
            bool inFlight=false;
            const auto stale=tracker_.InvalidateStream(reinterpret_cast<std::uintptr_t>(stream),inFlight);
            if(inFlight)Stop(); // Nested writes/close reuse invalidate both participants.
            NativeSaveEvents::WriteAborted(stale.serial);
        } catch(...) {Stop();}
    }

    Ticket Stage(void* stream,std::wstring_view path,const RonsoPool::SaveImage& image) noexcept {
        if(!configured_.load(std::memory_order_acquire))return {};
        ErrorScope errors(io_);Ticket ticket{};
        BeforeWrite(stream);
        try {
            if(!Ready()||!NativeSaveEvents::VerifiedRequested())return {};
            const auto epoch=io_.epoch(),thread=io_.thread();FileIdentity identity{};
            if(!io_.file(stream,identity))return {};
            ticket=tracker_.BeginWrite(reinterpret_cast<std::uintptr_t>(stream),identity,epoch,thread,path,image);
            if(!ticket.Valid())return {};
            std::array<wchar_t,4097> terminated{};
            for(std::size_t i=0;i<path.size();++i)terminated[i]=path[i];
            NativeSaveEvents::WriteStaged(ticket.serial,terminated.data(),image.data(),image.size());
            if(!Ready()||io_.epoch()!=epoch||io_.thread()!=thread){
                tracker_.FinishWrite(ticket,false);NativeSaveEvents::WriteAborted(ticket.serial);return {};
            }
            return ticket;
        } catch(...) {Stop();NativeSaveEvents::WriteAborted(ticket.serial);return {};}
    }

    void Finish(Ticket ticket,bool complete) noexcept {
        if(!configured_.load(std::memory_order_acquire)||!ticket.Valid())return;
        ErrorScope errors(io_);
        try {
            const bool accepted=Ready()&&complete;
            if(!tracker_.FinishWrite(ticket,accepted)||!accepted)NativeSaveEvents::WriteAborted(ticket.serial);
        } catch(...) {Stop();NativeSaveEvents::WriteAborted(ticket.serial);}
    }

    template<class CloseFn>
    int Close(void* stream,CloseFn original) {
        if(!configured_.load(std::memory_order_acquire))return original(stream);
        std::unique_ptr<CloseAttempt> attempt;
        {
            ErrorScope incoming(io_);
            try {
                if(Ready()&&tracker_.HasStream(reinterpret_cast<std::uintptr_t>(stream))){
                    FileIdentity identity{};
                    if(!io_.file(stream,identity))identity={};
                    attempt=std::make_unique<CloseAttempt>(tracker_.BeginClose(
                        reinterpret_cast<std::uintptr_t>(stream),identity,io_.epoch(),io_.thread()));
                }
            } catch(...) {Stop();}
        }
        // Exactly one original close, outside metadata catch/retry paths.
        const int result=original(stream);
        ErrorScope nativeErrors(io_);
        if(!attempt)return result;
        bool verified=false;
        try {
            auto actual=std::make_unique<RonsoPool::SaveImage>();FileIdentity file{};
            const bool read=attempt->valid&&result==0&&Ready()&&io_.readback(attempt->path.data(),file,*actual);
            verified=tracker_.CompleteClose(*attempt,read&&Ready(),file,io_.epoch(),io_.thread(),*actual);
            if(verified&&Ready())NativeSaveEvents::WriteVerified(attempt->ticket.serial,attempt->path.data(),actual->data(),actual->size());
            else verified=false;
        } catch(...) {Stop();}
        if(!verified)NativeSaveEvents::WriteAborted(attempt->ticket.serial);
        return result;
    }

    void Reset() noexcept {
        if(!configured_.load(std::memory_order_acquire))return;
        ErrorScope errors(io_);
        try {tracker_.Reset();}catch(...){Stop();}
    }

private:
    class ErrorScope {
    public:
        explicit ErrorScope(const Io& io) noexcept : io_(io),error_(io.getError()),number_(io.errorNumber()) {
            if(number_)savedNumber_=*number_;
        }
        ~ErrorScope(){if(number_)*number_=savedNumber_;io_.setError(error_);}
        ErrorScope(const ErrorScope&)=delete;
        ErrorScope& operator=(const ErrorScope&)=delete;
    private:
        const Io& io_;std::uint32_t error_;int* number_;int savedNumber_=0;
    };
    bool Ready() const noexcept {return !stopping_.load(std::memory_order_acquire)&&io_.ready();}
    Io io_{};Tracker tracker_;
    std::atomic<bool> attempted_{false},configured_{false},stopping_{false};
};
} // namespace FfxHooks::NativeSaveCommit
