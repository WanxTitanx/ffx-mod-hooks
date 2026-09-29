#pragma once
#include "SeymourSessionCore.h"
#include "RonsoPoolSave.h"
#include <cstring>

namespace FfxHooks::SeymourSession {
// Serialized by the runtime adapter. Tracks the native destination, not file
// previews; a complete byte-for-byte payload readback admits the owner thread.
class ActiveLoad {
public:
    struct Io {
        void* context=nullptr;
        bool (*copy)(void*,void*,const void*,std::size_t)=nullptr;
    };
    explicit ActiveLoad(std::uintptr_t destination=0) noexcept:destination_(destination){}
    void Configure(std::uintptr_t destination) noexcept {
        core_.Invalidate();destination_=destination;
    }
    void Begin(std::uint64_t cookie,void* destination,const void* source,
               std::uint32_t thread,const Io& io) noexcept {
        if(!destination_||reinterpret_cast<std::uintptr_t>(destination)!=destination_)return;
        const auto attempt=core_.Begin(cookie,thread);
        if(!attempt.Valid())return;
        for(auto& entry:pending_)if(!entry.attempt.cookie){
            entry.attempt=attempt;entry.valid=false;
            if(!source||!io.copy)return;
            try {
                if(!io.copy(io.context,entry.image.data(),source,entry.image.size()))return;
                entry.valid=ValidSource(entry.image);
            }catch(...){entry.valid=false;}
            return;
        }
        core_.Invalidate();
    }
    bool End(std::uint64_t cookie,bool completed,std::uint32_t thread,const Io& io) noexcept {
        if(!cookie)return false;
        for(auto& entry:pending_)if(entry.attempt.cookie==cookie){
            const auto attempt=entry.attempt;
            bool verified=false;
            try {
                if(completed&&entry.valid&&attempt.thread==thread&&io.copy){
                    // This is a comparison buffer only, never a game/save writer.
                    std::array<unsigned char,RonsoPool::kSaveSize-64> observed{};
                    verified=io.copy(io.context,observed.data(),reinterpret_cast<const void*>(destination_),observed.size())&&
                        std::memcmp(observed.data(),entry.image.data()+64,observed.size())==0;
                }
            }catch(...){verified=false;}
            entry={};return core_.End(attempt,verified,thread);
        }
        return false;
    }
    bool Reset(std::uint32_t thread) noexcept {return core_.Reset(thread);}
    void Invalidate() noexcept {core_.Invalidate();}
    void Stop() noexcept {core_.Stop();}
    Token Capture(std::uint32_t thread,bool cleanup=false) const noexcept {return core_.Capture(thread,cleanup);}
    bool Current(const Token& token,std::uint32_t thread,bool cleanup=false) const noexcept {
        return core_.Current(token,thread,cleanup);
    }
private:
    static bool ValidSource(const RonsoPool::SaveImage& source) noexcept {
        if(RonsoPool::IsValidSave(source))return true;
        // The verified native reader can clear its payload CRC before the copy.
        // Preserve original bytes for comparison; validate a separate image.
        constexpr std::size_t crc=25844;
        if(source[crc]||source[crc+1]||source[crc+2]||source[crc+3])return false;
        const auto checksum=RonsoPool::SaveChecksum(source);
        const auto header=static_cast<std::uint16_t>(source[26]|(unsigned(source[27])<<8));
        if(static_cast<std::uint16_t>(checksum)!=header)return false;
        auto normalized=source;
        for(unsigned i=0;i<4;++i)normalized[crc+i]=static_cast<unsigned char>(checksum>>(8*i));
        return RonsoPool::IsValidSave(normalized);
    }
    struct Entry {Attempt attempt{};RonsoPool::SaveImage image{};bool valid=false;};
    Core core_;
    std::uintptr_t destination_=0;
    std::array<Entry,Capacity> pending_{};
};
} // namespace FfxHooks::SeymourSession
