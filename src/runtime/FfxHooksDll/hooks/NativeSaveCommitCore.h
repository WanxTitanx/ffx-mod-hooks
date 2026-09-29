#pragma once
#include "RonsoPoolSave.h"
#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string_view>

namespace FfxHooks::NativeSaveCommit {
// This is bookkeeping for the existing native save producer, not a second
// save writer. It performs no file I/O and cannot turn fwrite into a commit.
struct FileIdentity {
    std::uint32_t volume=0;
    std::uint64_t index=0;
    bool Valid() const noexcept {return index!=0;}
    friend bool operator==(const FileIdentity& a,const FileIdentity& b) noexcept {
        return a.volume==b.volume&&a.index==b.index;
    }
};
struct Ticket {
    std::uintptr_t stream=0;
    std::uint64_t serial=0;
    bool Valid() const noexcept {return stream!=0&&serial!=0;}
    friend bool operator==(const Ticket& a,const Ticket& b) noexcept {
        return a.stream==b.stream&&a.serial==b.serial;
    }
};
struct CloseAttempt {
    Ticket ticket{};
    FileIdentity file{};
    std::uint32_t epoch=0,thread=0;
    std::array<wchar_t,4097> path{};
    RonsoPool::SaveImage image{};
    bool valid=false;
};

class Tracker {
public:
    static constexpr unsigned kCapacity=8;

    Ticket BeginWrite(std::uintptr_t stream,FileIdentity file,std::uint32_t epoch,
                      std::uint32_t thread,std::wstring_view path,
                      const RonsoPool::SaveImage& image) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Overlap and FILE-address reuse must not revive earlier provenance.
        if(auto* previous=FindStream(stream)){*previous={};return {};}
        if(!stream||!file.Valid()||!epoch||!thread||path.empty()||path.size()>4096||
           path.find(L'\0')!=std::wstring_view::npos||!RonsoPool::IsValidSave(image)||
           serial_==(std::numeric_limits<std::uint64_t>::max)())return {};
        for(auto& record:records_)if(record.phase==Phase::Empty){
            record.value.ticket={stream,++serial_};
            record.value.file=file;record.value.epoch=epoch;record.value.thread=thread;
            for(std::size_t i=0;i<path.size();++i)record.value.path[i]=path[i];
            record.value.image=image;record.phase=Phase::Writing;
            return record.value.ticket;
        }
        return {};
    }

    bool FinishWrite(Ticket ticket,bool complete) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* record=Find(ticket);
        if(!record||record->phase!=Phase::Writing)return false;
        if(!complete)*record={};
        else record->phase=Phase::Buffered;
        return true;
    }

    CloseAttempt BeginClose(std::uintptr_t stream,FileIdentity file,
                            std::uint32_t epoch,std::uint32_t thread) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* record=FindStream(stream);
        if(!record)return {};
        CloseAttempt result{};
        result.ticket=record->value.ticket;
        if(record->phase!=Phase::Buffered||!(record->value.file==file)||
           record->value.epoch!=epoch||record->value.thread!=thread){
            *record={};return result;
        }
        result=record->value;result.valid=true;
        record->phase=Phase::Closing;
        return result;
    }

    bool CompleteClose(const CloseAttempt& attempt,bool nativeCloseSucceeded,
                       FileIdentity readbackFile,std::uint32_t epoch,
                       std::uint32_t thread,const RonsoPool::SaveImage& actual) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!attempt.valid)return false;
        auto* record=Find(attempt.ticket);
        // A stale completion must neither publish nor cancel a new occupant.
        if(!record||record->phase!=Phase::Closing)return false;
        const auto& expected=record->value;
        const bool accepted=nativeCloseSucceeded&&expected.file==readbackFile&&
            expected.epoch==epoch&&expected.thread==thread&&
            attempt.file==expected.file&&attempt.epoch==expected.epoch&&
            attempt.thread==expected.thread&&attempt.path==expected.path&&
            attempt.image==expected.image&&actual==expected.image&&RonsoPool::IsValidSave(actual);
        *record={};
        return accepted;
    }

    bool HasStream(std::uintptr_t stream) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& record:records_)if(record.phase!=Phase::Empty&&
            record.value.ticket.stream==stream)return true;
        return false;
    }

    Ticket InvalidateStream(std::uintptr_t stream,bool& inFlight) {
        std::lock_guard<std::mutex> lock(mutex_);
        inFlight=false;
        auto* record=FindStream(stream);
        if(!record)return {};
        const auto ticket=record->value.ticket;
        inFlight=record->phase==Phase::Writing||record->phase==Phase::Closing;
        *record={};return ticket;
    }

    // Normal-context cleanup only. Loader-lock stop must close admission in the
    // owning adapter atomically and leave this process-lifetime storage alone.
    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& record:records_)record={};
    }

private:
    enum class Phase {Empty,Writing,Buffered,Closing};
    struct Record {CloseAttempt value{};Phase phase=Phase::Empty;};
    Record* FindStream(std::uintptr_t stream) noexcept {
        if(!stream)return nullptr;
        for(auto& record:records_)if(record.phase!=Phase::Empty&&
            record.value.ticket.stream==stream)return &record;
        return nullptr;
    }
    Record* Find(Ticket ticket) noexcept {
        if(!ticket.Valid())return nullptr;
        auto* record=FindStream(ticket.stream);
        return record&&record->value.ticket==ticket?record:nullptr;
    }
    mutable std::mutex mutex_;
    std::array<Record,kCapacity> records_{};
    std::uint64_t serial_=0;
};
} // namespace FfxHooks::NativeSaveCommit
