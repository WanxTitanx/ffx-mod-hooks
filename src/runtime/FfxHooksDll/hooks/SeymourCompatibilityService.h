#pragma once
#include "SeymourCompatibilityCore.h"

namespace FfxHooks::SeymourCompatibility {
struct QueryIo {
    void* context=nullptr;
    int (*original)(void*,int,std::uint32_t)=nullptr;
    bool (*capture)(void*,Scope&)=nullptr;
};

// Query-time policy never changes learned or native disabled-command masks.
// The original is called exactly once, including every unavailable/OFF path.
inline int ServiceQuery(bool requested,int actor,std::uint32_t command,const QueryIo& io) {
    if(!io.original)return 1;
    Scope before{},after{};
    bool admitted=false;
    if(requested&&actor==7&&io.capture){
        try {admitted=io.capture(io.context,before)&&before.Valid();}catch(...){admitted=false;}
    }
    const int result=io.original(io.context,actor,command);
    if(!admitted)return result;
    try {
        if(!io.capture(io.context,after))return result;
        return FilterCommand(result,actor,command,true,before,after);
    }catch(...){return result;}
}

struct VisibilityOriginal {
    void* context=nullptr;
    void (*call)(void*,int,std::uint8_t)=nullptr;
};

// A lease holds the full identities of the two records it changed. Cleanup is
// independent of admitting new work: OFF may restore this lease, never adopt a
// new one. The adapter must serialize calls and supply cleanup-scoped callbacks.
class VisibilityLease {
public:
    bool Pending() const noexcept {return owned_[0]||owned_[1];}

    bool Restore(const Io& io) noexcept {
        if(!Pending())return true;
        if(!io.read||!io.current||!io.compareFlag)return false;
        try {
            Pair observed{};
            if(!io.current(io.context,before_)||!io.read(io.context,observed)||
               !observed.Valid()||!(observed==last_))return false;
            // Full preflight avoids a partial restore merely because the other
            // record was transferred, replaced or re-equipped since this lease.
            for(unsigned i=0;i<2;++i){
                if(!owned_[i])continue;
                if(!io.current(io.context,before_)||
                   !io.compareFlag(io.context,i,last_.gear[i],before_.gear[i][3]))return false;
                last_.gear[i][3]=before_.gear[i][3];
            }
            if(!io.read(io.context,observed)||!(observed==last_)||
               !io.current(io.context,before_))return false;
            owned_={};
            return true;
        }catch(...){return false;}
    }

    Outcome Apply(bool requested,int actor,std::uint8_t enable,
                  const VisibilityOriginal& original,const Io& edit,const Io& cleanup) {
        if(!original.call)return Outcome::Rejected;
        original.call(original.context,actor,enable);
        if(actor!=7)return Outcome::Inactive;
        if(Pending()&&!Restore(cleanup))return Outcome::Partial;
        if(!requested)return Outcome::Inactive;
        if(!edit.read||!edit.current||!edit.compareFlag)return Outcome::Rejected;
        Track context{this,&edit,false};
        const Io tracked{&context,&TrackRead,&TrackCurrent,&TrackCompare};
        const auto result=ApplyVisibility(true,actor,enable,tracked);
        if(Pending()&&(result==Outcome::Rejected||result==Outcome::RolledBack))return Outcome::Partial;
        return result;
    }

private:
    struct Track {VisibilityLease* lease;const Io* io;bool captured;};
    Pair before_{},last_{};
    std::array<bool,2> owned_{};
    static bool TrackRead(void* p,Pair& out) {
        auto& t=*static_cast<Track*>(p);
        if(!t.io->read(t.io->context,out))return false;
        if(!t.captured){t.lease->before_=t.lease->last_=out;t.lease->owned_={};t.captured=true;}
        return true;
    }
    static bool TrackCurrent(void* p,const Pair& expected) {
        auto& t=*static_cast<Track*>(p);return t.io->current(t.io->context,expected);
    }
    static bool TrackCompare(void* p,unsigned slot,const Gear& expected,std::uint8_t desired) {
        auto& t=*static_cast<Track*>(p);
        if(!t.captured||slot>=2)return false;
        const auto previous=t.lease->last_.gear[slot];
        const bool wasOwned=t.lease->owned_[slot];
        // Retain a possible write if a callback throws. False explicitly means
        // no write under the Io contract and can discard this tentative record.
        t.lease->last_.gear[slot]=expected;
        t.lease->last_.gear[slot][3]=desired;
        t.lease->owned_[slot]=desired!=t.lease->before_.gear[slot][3];
        if(t.io->compareFlag(t.io->context,slot,expected,desired))return true;
        t.lease->last_.gear[slot]=previous;t.lease->owned_[slot]=wasOwned;return false;
    }
};
} // namespace FfxHooks::SeymourCompatibility
