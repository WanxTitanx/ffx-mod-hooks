#pragma once
#include "customize_recipes.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace workshop {
// Transient read-only admission, not part of the v1 save or v5 host ABI.
// A live native table determines grants; no mod's ability IDs are blacklisted.
struct AcquisitionCatalog {std::uint64_t proof=0;bool ready=false;std::array<bool,131> present{};};
inline bool ReadAcquisition(const std::uint8_t* data,std::size_t size,AcquisitionCatalog& out) noexcept {
    out={};if(!data||size<20||size>32788)return false;
    const auto word=[&](unsigned at){return unsigned(data[at])|(unsigned(data[at+1])<<8);};
    const auto dword=[&](unsigned at){return std::uint32_t(word(at))|(std::uint32_t(word(at+2))<<16);};
    const unsigned count=word(10)+1;
    if(dword(0)!=1||word(8)||word(12)!=8||dword(16)!=20||count>4096||word(14)!=count*8||size!=20+count*8)return false;
    AcquisitionCatalog next;
    for(unsigned at=20;at<size;at+=8){const auto kind=word(at),target=word(at+2);
        if(!(kind&3)||!target||target==255)continue;
        // Only target availability belongs to this contract. Another profile
        // may use free/custom ingredient recipes; pricing remains separately
        // owned by the existing Workshop economy, not by this admission read.
        if(target<0x8000||target>0x8FFF)return false;
        if(target<=0x8082)next.present[target-0x8000]=true;
    }
    next.proof=0xCBF29CE484222325ULL;for(std::size_t i=0;i<size;++i)next.proof=(next.proof^data[i])*0x100000001B3ULL;
    if(!next.proof)return false;
    next.ready=true;out=next;return true;
}
inline bool AcquisitionAllowed(std::uint16_t word,const AcquisitionCatalog* catalog) noexcept {
    if(!catalog)return true; // Explicit legacy/offline caller, with no live profile.
    if(word<0x8000||word>0x8082)return true; // Extension identities have their own proof.
    // Preserve the intentional fallback for identities that never had a vanilla
    // recipe (including Capture). A removed native recipe is not that fallback.
    return catalog->ready&&catalog->proof&&(catalog->present[word-0x8000]||!CustomizeRecipes[word-0x8000].quantity);
}
inline std::uint64_t AcquisitionProof(std::uint64_t extension,const AcquisitionCatalog& catalog) noexcept {
    auto proof=extension^0x4143515549534954ULL;
    for(unsigned i=0;i<8;++i)proof=(proof^((catalog.proof>>(8*i))&255))*0x100000001B3ULL;
    return proof?proof:1;
}
}
