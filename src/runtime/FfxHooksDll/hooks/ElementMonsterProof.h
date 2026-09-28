#pragma once
#include "ElementPackAdmission.h"
#include <array>
#include <cstring>
#include <limits>

namespace FfxHooks::ElementalDominion {
enum class MonsterProofError { None,Identity,Layout,Fingerprint };

// A profile is bound to an admitted incarnation and a complete immutable input
// file. The runtime rechecks the native header and stat section before using it;
// allocation/lifecycle changes require a fresh full-file digest outside a hit.
class MonsterProof {
public:
    static constexpr unsigned HeaderBytes=0x30,StatCapacity=192;
    bool Admit(const ActorProfile& profile,unsigned slot,unsigned identity,const void* nativeFile,
               const unsigned char* copy,std::size_t size,BankDigest hash,MonsterProofError& error) noexcept {
        error=MonsterProofError::None;
        if(profile.kind!=ProfileKind::Monster||slot<18||slot>=31||identity!=profile.id||identity==0xFFFF||
           !nativeFile||!copy||!hash||size!=profile.fileBytes||size<HeaderBytes||size>8u*1024u*1024u||
           reinterpret_cast<std::uintptr_t>(nativeFile)>(std::numeric_limits<std::uintptr_t>::max)()-size){
            error=MonsterProofError::Identity;return false;
        }
        const auto ai=Word32(copy+4),worker=Word32(copy+8),stats=Word32(copy+12);
        if(Word32(copy+0x20)!=size||ai<HeaderBytes||ai>=worker||worker>=stats||stats>=size){
            error=MonsterProofError::Layout;return false;
        }
        std::array<unsigned char,32> digest{};
        if(!ValidFingerprint(profile.fileSha256)||!hash(copy,size,digest)){
            error=MonsterProofError::Fingerprint;return false;
        }
        constexpr char hex[]="0123456789abcdef";
        for(unsigned i=0;i<digest.size();++i)
            if(profile.fileSha256[i*2]!=hex[digest[i]>>4]||profile.fileSha256[i*2+1]!=hex[digest[i]&15]){
                error=MonsterProofError::Fingerprint;return false;
            }
        MonsterProof candidate;
        candidate.slot_=slot;candidate.identity_=identity;candidate.file_=nativeFile;
        candidate.size_=static_cast<unsigned>(size);candidate.statOffset_=stats;
        candidate.statBytes_=static_cast<unsigned>((std::min)(size-stats,std::size_t(StatCapacity)));
        std::memcpy(candidate.header_.data(),copy,HeaderBytes);
        std::memcpy(candidate.stats_.data(),copy+stats,candidate.statBytes_);
        candidate.active_=true;*this=candidate;return true;
    }
    bool Matches(unsigned slot,unsigned identity,const void* nativeFile,
                 const unsigned char* copy,std::size_t size) const noexcept {
        return active_&&copy&&size==size_&&MatchesView(slot,identity,nativeFile,
            copy,HeaderBytes,copy+statOffset_,statBytes_);
    }
    bool MatchesView(unsigned slot,unsigned identity,const void* nativeFile,
                     const void* header,std::size_t headerBytes,const void* stats,std::size_t statBytes) const noexcept {
        return active_&&slot==slot_&&identity==identity_&&nativeFile==file_&&header&&stats&&
               headerBytes==HeaderBytes&&statBytes==statBytes_&&
               std::memcmp(header,header_.data(),HeaderBytes)==0&&std::memcmp(stats,stats_.data(),statBytes_)==0;
    }
    unsigned StatOffset() const noexcept {return active_?statOffset_:0;}
    unsigned StatBytes() const noexcept {return active_?statBytes_:0;}
    void Clear() noexcept {*this={};}
private:
    static std::uint32_t Word32(const unsigned char* bytes) noexcept {
        return bytes[0]|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
    }
    unsigned slot_=31,identity_=0xFFFF,size_=0,statOffset_=0,statBytes_=0;
    const void* file_=nullptr;
    std::array<unsigned char,HeaderBytes> header_{};
    std::array<unsigned char,StatCapacity> stats_{};
    bool active_=false;
};
} // namespace FfxHooks::ElementalDominion
