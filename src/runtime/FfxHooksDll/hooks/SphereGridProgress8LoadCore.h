#pragma once
#include "SphereGridProgress8Core.h"
#include "SeymourSessionCore.h"
#include "RonsoPoolSave.h"
#include <cstring>
#include <memory>

namespace FfxHooks::SphereGridProgress8Load {
using Hash=SphereGridProgress::Hash;
using Token=SeymourSession::Token;
struct Identity {
    Hash path{},image{};
    bool Valid() const noexcept {
        const auto nonzero=[](const Hash& h){return std::any_of(h.begin(),h.end(),[](auto b){return b!=0;});};
        return nonzero(path)&&nonzero(image);
    }
    friend bool operator==(const Identity& a,const Identity& b) noexcept {return a.path==b.path&&a.image==b.image;}
};
struct Claim {
    std::uint64_t revision=0,cookie=0,request=0;Token session{};Identity save{};
    bool Valid() const noexcept {return revision&&cookie&&request&&session.Valid()&&save.Valid();}
    friend bool operator==(const Claim& a,const Claim& b) noexcept {
        return a.revision==b.revision&&a.cookie==b.cookie&&a.request==b.request&&a.session==b.session&&a.save==b.save;
    }
};
// Jarvis-HOOK. Bind an already-confirmed session to the exact file buffer used
// by that load. This is neither a second native loader nor a save-session owner.
class Tracker {
public:
    static constexpr std::size_t ReadCapacity=16,PendingCapacity=16;
    explicit Tracker(std::uintptr_t destination=0) noexcept:destination_(destination){}
    void Configure(std::uintptr_t destination) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);Retire();reads_={};pending_={};destination_=destination;}
        catch(...){RequestStop();}
    }
    void ReadStarting(std::uintptr_t source) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);if(auto* record=FindRead(source))*record={};}
        catch(...){RequestStop();}
    }
    bool ReadCompleted(std::uintptr_t source,const Identity& save,const RonsoPool::SaveImage& image) noexcept {
        if(stopping_.load(std::memory_order_acquire))return false;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(auto* prior=FindRead(source))*prior={};
            if(!source||!save.Valid()||stopping_.load(std::memory_order_acquire))return false;
            auto normalized=image;if(!Normalize(normalized))return false;
            if(readSerial_==UINT64_MAX){RequestStop();return false;}
            Read* selected=nullptr;
            for(auto& record:reads_)if(!record.source){selected=&record;break;}
            if(!selected)selected=&*std::min_element(reads_.begin(),reads_.end(),[](const Read& a,const Read& b){return a.serial<b.serial;});
            auto bytes=std::make_shared<const RonsoPool::SaveImage>(std::move(normalized));
            if(stopping_.load(std::memory_order_acquire))return false;
            *selected={source,++readSerial_,save,std::move(bytes)};return true;
        }catch(...){RequestStop();return false;}
    }
    void Begin(std::uint64_t cookie,std::uintptr_t destination,std::uintptr_t source,
               std::uint32_t thread,std::uint64_t request,const RonsoPool::SaveImage* observed) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!destination_||destination!=destination_)return;
            Retire();
            if(stopping_.load(std::memory_order_acquire)||!cookie||cookie<=lastCookie_||!thread)return;
            lastCookie_=cookie;Pending* selected=nullptr;
            for(auto& record:pending_)if(!record.cookie){selected=&record;break;}
            if(!selected){RequestStop();return;}
            selected->cookie=cookie;selected->revision=revision_;selected->source=source;
            selected->thread=thread;selected->request=request;
            const auto* read=FindRead(source);if(!read||!request||!observed)return;
            auto normalized=*observed;
            if(!Normalize(normalized)||normalized!=*read->normalized)return;
            selected->readSerial=read->serial;selected->save=read->save;
            // Preserve exact pre-copy bytes, including a legitimate cleared CRC.
            selected->observed=std::make_shared<const RonsoPool::SaveImage>(*observed);
        }catch(...){RequestStop();}
    }
    bool End(std::uint64_t cookie,bool completed,std::uint32_t thread,const Token& confirmed,
             const unsigned char* payload,std::size_t size) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            auto* found=FindPending(cookie);if(!found)return false;
            auto attempt=std::move(*found);*found={};
            if(stopping_.load(std::memory_order_acquire)||!completed||!confirmed.Valid()||
               thread!=attempt.thread||confirmed.thread!=thread||!attempt.request||
               !payload||size!=RonsoPool::kSaveSize-64||!attempt.observed||attempt.revision!=revision_)return false;
            for(const auto& other:pending_)if(other.cookie)return false;
            const auto* read=FindRead(attempt.source);
            if(!read||read->serial!=attempt.readSerial||!(read->save==attempt.save)||
               std::memcmp(payload,attempt.observed->data()+64,size)!=0)return false;
            if(stopping_.load(std::memory_order_acquire))return false;
            active_={revision_,cookie,attempt.request,confirmed,attempt.save};return true;
        }catch(...){RequestStop();return false;}
    }
    Claim Capture(const Token& session,std::uint64_t request) const noexcept {
        if(stopping_.load(std::memory_order_acquire))return {};
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!active_.Valid()||!(active_.session==session)||active_.request!=request||stopping_.load(std::memory_order_acquire))return {};
            return active_;
        }catch(...){return {};}
    }
    bool Current(const Claim& claim,const Token& session,std::uint64_t request) const noexcept {
        return claim.Valid()&&claim==Capture(session,request);
    }
    void Invalidate() noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);Retire();reads_={};pending_={};}
        catch(...){RequestStop();}
    }
    void RequestStop() noexcept {stopping_.store(true,std::memory_order_release);}
private:
    struct Read {
        std::uintptr_t source=0;std::uint64_t serial=0;Identity save{};
        std::shared_ptr<const RonsoPool::SaveImage> normalized;
    };
    struct Pending {
        std::uint64_t cookie=0,revision=0,request=0,readSerial=0;
        std::uintptr_t source=0;std::uint32_t thread=0;Identity save{};
        std::shared_ptr<const RonsoPool::SaveImage> observed;
    };
    static bool Normalize(RonsoPool::SaveImage& image) noexcept {
        if(RonsoPool::IsValidSave(image))return true;
        constexpr std::size_t at=25844;
        if(image[at]||image[at+1]||image[at+2]||image[at+3])return false;
        const auto checksum=RonsoPool::SaveChecksum(image);
        const auto header=static_cast<std::uint16_t>(image[26]|(std::uint16_t(image[27])<<8));
        if(static_cast<std::uint16_t>(checksum)!=header)return false;
        for(unsigned i=0;i<4;++i)image[at+i]=static_cast<unsigned char>(checksum>>(8*i));
        return RonsoPool::IsValidSave(image);
    }
    void Retire() noexcept {active_={};if(revision_==UINT64_MAX){RequestStop();return;}++revision_;}
    Read* FindRead(std::uintptr_t source) noexcept {
        if(source)for(auto& record:reads_)if(record.source==source)return &record;
        return nullptr;
    }
    Pending* FindPending(std::uint64_t cookie) noexcept {
        if(cookie)for(auto& record:pending_)if(record.cookie==cookie)return &record;
        return nullptr;
    }
    mutable std::mutex mutex_;
    std::array<Read,ReadCapacity> reads_{};
    std::array<Pending,PendingCapacity> pending_{};
    Claim active_{};std::uintptr_t destination_=0;
    std::uint64_t revision_=0,lastCookie_=0,readSerial_=0;
    std::atomic<bool> stopping_{false};
};
} // namespace FfxHooks::SphereGridProgress8Load
