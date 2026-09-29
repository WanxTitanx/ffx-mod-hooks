#pragma once
#include "SeymourSessionCore.h"
#include <array>
#include <algorithm>
#include <cstdint>

namespace FfxHooks::SeymourPersistentRoster {
using Token=SeymourSession::Token;
struct Image {
    std::uint8_t party=0;
    std::array<std::uint8_t,3> front{};
    std::array<std::uint8_t,17> reserve{};
    friend bool operator==(const Image& a,const Image& b) noexcept {
        return a.party==b.party&&a.front==b.front&&a.reserve==b.reserve;
    }
};
inline std::array<unsigned,256> Counts(const Image& image) noexcept {
    std::array<unsigned,256> counts{};
    for(auto id:image.front)++counts[id];
    for(auto id:image.reserve)++counts[id];
    return counts;
}
inline bool ValidMembers(const Image& image) noexcept {
    const auto counts=Counts(image);
    for(unsigned id=0;id<255;++id)if(counts[id]&&(id>=18||counts[id]!=1))return false;
    return true;
}
inline bool Present(const Image& image) noexcept {
    return image.party==0x11&&ValidMembers(image)&&Counts(image)[7]==1;
}
inline bool ExactAddition(const Image& before,const Image& after) noexcept {
    if(before.party!=0x10||after.party!=0x11||before.front!=after.front||
       !ValidMembers(before)||!ValidMembers(after)||Counts(before)[7])return false;
    unsigned changes=0;
    for(unsigned i=0;i<before.reserve.size();++i){
        if(before.reserve[i]==after.reserve[i])continue;
        if(before.reserve[i]!=255||after.reserve[i]!=7)return false;
        ++changes;
    }
    return changes==1;
}
inline bool SameOwnedMembers(const Image& before,const Image& current) noexcept {
    if(before.party!=0x10||!ValidMembers(before)||!Present(current))return false;
    auto expected=Counts(before);
    if(expected[7]||!expected[255])return false;
    --expected[255];++expected[7];return expected==Counts(current);
}
inline bool Restored(const Image& before,const Image& current) noexcept {
    return before.party==0x10&&current.party==0x10&&ValidMembers(before)&&
           ValidMembers(current)&&Counts(before)[7]==0&&Counts(before)==Counts(current);
}
enum class Outcome:unsigned {Off,Applied,Owned,Borrowed,Restored,Rejected,RestorePending};
struct Io {
    void* context=nullptr;
    // Returns only an already-confirmed active load/new-game identity. It never
    // discovers or switches active identity from a preview read or content hash.
    bool (*session)(void*,bool cleanup,Token&)=nullptr;
    bool (*current)(void*,const Token&,bool cleanup)=nullptr;
    bool (*read)(void*,Image&)=nullptr;
    // Uses original Assign(7, on), not private writes to the party lists.
    bool (*assign)(void*,bool)=nullptr;
};
class Lease {
public:
    bool Owned() const noexcept {return owned_;}
    Outcome Update(bool requested,const Io& io){
        if(!requested&&!owned_)return Outcome::Off;
        if(!io.session||!io.current||!io.read||!io.assign)return Failure();
        Token now{};
        if(!io.session(io.context,!requested,now)||!now.Valid())return Failure();
        if(owned_&&!(now==token_)){
            // A proved new active load owns all current roster bytes. Retire the
            // obsolete bookkeeping without writing any byte of the new save.
            owned_=false;baseline_={};token_={};
        }
        if(!requested&&!owned_)return Outcome::Off;
        if(!io.current(io.context,now,!requested))return Failure();
        Image current{};
        if(!io.read(io.context,current)||!ValidMembers(current)||
           !io.current(io.context,now,!requested))return Failure();
        if(owned_){
            if(Restored(baseline_,current)){owned_=false;return requested?Outcome::Rejected:Outcome::Restored;}
            if(!SameOwnedMembers(baseline_,current))return Outcome::RestorePending;
            if(requested)return Outcome::Owned;
            (void)io.assign(io.context,false);
            Image after{};
            if(!io.current(io.context,token_,true)||!io.read(io.context,after)||
               !Restored(baseline_,after)||!io.current(io.context,token_,true))return Outcome::RestorePending;
            owned_=false;return Outcome::Restored;
        }
        if(Present(current))return Outcome::Borrowed;
        if(current.party!=0x10||Counts(current)[7]||
           std::find(current.reserve.begin(),current.reserve.end(),255)==current.reserve.end())return Outcome::Rejected;
        baseline_=current;token_=now;owned_=true;
        // Ownership is tentative BEFORE the native call: a thrown callback can
        // have changed memory, so later cleanup must retain this exact baseline.
        (void)io.assign(io.context,true);
        Image after{};
        if(!io.current(io.context,token_,false)||!io.read(io.context,after)||
           !io.current(io.context,token_,false))return Outcome::RestorePending;
        if(after==baseline_){owned_=false;return Outcome::Rejected;}
        return ExactAddition(baseline_,after)?Outcome::Applied:Outcome::RestorePending;
    }
private:
    Outcome Failure() const noexcept {return owned_?Outcome::RestorePending:Outcome::Rejected;}
    Token token_{};
    Image baseline_{};
    bool owned_=false;
};
} // namespace FfxHooks::SeymourPersistentRoster
