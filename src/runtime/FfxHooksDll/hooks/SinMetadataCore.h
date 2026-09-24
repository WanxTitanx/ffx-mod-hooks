#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace FfxHooks::SinMetadata {
inline constexpr std::size_t kNameSize=40,kLootSize=280;
inline constexpr std::uintptr_t kNameOffset=0x540u,kLootPointerOffset=0xF88u;
inline constexpr std::uintptr_t kRewardRva=0x3990E0u;
inline constexpr std::array<std::uint8_t,22> kRewardPrefix={{
    0x55,0x8B,0xEC,0x83,0xEC,0x18,0x57,0x8B,0x7D,0x10,0x85,0xFF,
    0x0F,0x84,0xC1,0x02,0x00,0x00,0x83,0x7D,0x14,0x00,
}};
inline std::uint16_t ScaledReward(std::uint16_t base,unsigned threat){
    const std::uint32_t value=static_cast<std::uint32_t>(base)*(100u+10u*threat)/100u;
    return static_cast<std::uint16_t>(value>65535u?65535u:value);
}
inline bool RewardView(const std::array<std::uint8_t,kLootSize>& original,unsigned threat,
    std::array<std::uint8_t,kLootSize>* out){
    if(!out || threat<1 || threat>2)return false;
    *out=original;
    for(std::size_t offset=0;offset<6;offset+=2){
        const auto value=ScaledReward(static_cast<std::uint16_t>(original[offset]|(original[offset+1]<<8)),threat);
        (*out)[offset]=static_cast<std::uint8_t>(value);(*out)[offset+1]=static_cast<std::uint8_t>(value>>8);
    }
    return true;
}
inline bool NameView(const std::array<std::uint8_t,kNameSize>& original,unsigned curse,unsigned threat,
    std::array<std::uint8_t,kNameSize>* out){
    static constexpr const char* labels[]={"","Veil","March","Rush","Ward","Weave","Break","Salve","Chorus"};
    // Same recovered byte atlas used by NativeMenuShell. Preserve the original
    // encoded name, including localized glyphs; never truncate a control sequence.
    static constexpr char atlas[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    if(!out || curse<1 || curse>8 || threat<1 || threat>2)return false;
    std::size_t length=0;while(length<original.size() && original[length]){
        if(original[length]<0x30u)return false; // Opaque control payloads may contain zero bytes.
        ++length;
    }
    char suffix[24]{};const int count=std::snprintf(suffix,sizeof(suffix)," [%s T%u]",labels[curse],threat);
    if(!length || length==original.size() || count<=0 || length+static_cast<std::size_t>(count)>=original.size())return false;
    *out=original;
    for(int n=0;n<count;++n){const char* glyph=std::strchr(atlas,suffix[n]);if(!glyph)return false;
        (*out)[length++]=static_cast<std::uint8_t>(0x30+(glyph-atlas));}
    (*out)[length]=0;return true;
}
} // namespace FfxHooks::SinMetadata
