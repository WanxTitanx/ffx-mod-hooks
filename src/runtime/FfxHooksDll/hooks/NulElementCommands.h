#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::NulElements {
// The high two bits exist only in this hook's private charge bank. They are
// never written into the game's BYTE element mask or native status timers.
inline constexpr unsigned Holy=0x10,Shadow=0x80,Earth=0x20,Wind=0x40,Poison=0x100,Gravity=0x200;
inline constexpr unsigned Native=Holy|Shadow|Earth|Wind,All=Native|Poison|Gravity;
struct Command {unsigned id,mask,animation;const char* name;};
inline constexpr std::array<Command,6> Commands{{
    {320,Holy,870,"NulHoly"},{321,Shadow,871,"NulShadow"},
    {370,Earth,872,"NulEarth"},{371,Wind,873,"NulWind"},
    {372,Poison,874,"NulPoison"},{373,Gravity,875,"NulGravity"}
}};
inline const Command* Find(unsigned encoded) noexcept {
    if((encoded&0xFFFFF000u)!=0x3000u)return nullptr;
    for(const auto& c:Commands)if((encoded&0xFFFu)==c.id)return &c;
    return nullptr;
}
inline unsigned KeyMask(const char* key) noexcept {
    if(!key)return 0;
    if(!std::strcmp(key,"hook.custom03")||!std::strcmp(key,"spira.poison"))return Poison;
    if(!std::strcmp(key,"hook.custom04")||!std::strcmp(key,"spira.gravity"))return Gravity;
    return 0;
}
inline bool Canonical(const Command& command,const unsigned char* row,std::size_t size) noexcept {
    if(!row||size<96)return false;
    const unsigned animation=row[16]|(unsigned(row[17])<<8),second=row[18]|(unsigned(row[19])<<8);
    if(animation!=command.animation||second||row[35]||row[37]!=2||row[42]||row[43]!=1||row[45]||row[26]!=5)return false;
    if(!((row[23]==4&&row[24]==4&&row[25]==1)||(row[23]==2&&row[24]==2&&row[25]==255)))return false;
    for(unsigned i=46;i<92;++i)if(row[i])return false;
    return true;
}
inline unsigned Grant(unsigned encoded,const unsigned char* row,std::size_t size,bool expanded) noexcept {
    const auto* command=Find(encoded);if(!command||!row||size<96)return 0;
    if(expanded&&Canonical(*command,row,size))return command->mask;
    // Existing Radiant/Umbral saves retain their original two-command adapter.
    if(command->id<322&&!(row[35]&1)&&(row[16]|(unsigned(row[17])<<8))==146)return command->mask;
    return 0;
}
}
