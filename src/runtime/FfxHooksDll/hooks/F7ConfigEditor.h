#pragma once
#include "F7InLive.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace FfxHooks::F7Editor {
enum class Result {Applied,Invalid,Conflict,Full};
namespace MusicRows {
inline constexpr int Lock=0,Battle=1,Randomizer=2,Fade=3,Count=4,FirstSlot=5,
    OpenMix=FirstSlot+F7_PLAYLIST_MAX,Save=OpenMix+1,Reset=Save+1,Back=Reset+1;
inline bool Track(int row) noexcept {return row==Lock||row==Battle||(row>=FirstSlot&&row<OpenMix);}
}
inline bool ValidMusic(const F7MusicConfig& value) noexcept {
    const auto track=[](int id){return id==-1||(id>0&&id<=0xB5);};
    if(!track(value.lockTrack)||!track(value.battleTrack)||value.fadeFrames<0||value.fadeFrames>600||
       value.playlistCount<0||value.playlistCount>F7_PLAYLIST_MAX)return false;
    for(int i=0;i<value.playlistCount;++i)if(value.playlist[i]<1||value.playlist[i]>0xB5)return false;
    return true;
}
inline bool EqualMusic(const F7MusicConfig& a,const F7MusicConfig& b) noexcept {
    if(a.lockTrack!=b.lockTrack||a.battleTrack!=b.battleTrack||a.randomizer!=b.randomizer||
       a.fadeFrames!=b.fadeFrames||a.playlistCount!=b.playlistCount)return false;
    for(int i=0;i<F7_PLAYLIST_MAX;++i)if(a.playlist[i]!=b.playlist[i])return false;
    return true;
}
inline int PlaylistTrack(const F7MusicConfig& value,std::uint32_t selection) noexcept {
    if(!ValidMusic(value)||!value.playlistCount)return -1;
    return value.playlist[selection%static_cast<unsigned>(value.playlistCount)];
}
inline int FadeFrames(const F7MusicConfig& value) noexcept {return (std::clamp)(value.fadeFrames,0,600);}
inline bool FormatMusicNumber(int row,int value,char* output,std::size_t capacity) noexcept {
    if(!output||!capacity||(row!=MusicRows::Fade&&row!=MusicRows::Count))return false;
    const int count=std::snprintf(output,capacity,row==MusicRows::Fade?"%d frames":"%d / 8",value);
    return count>=0&&static_cast<std::size_t>(count)<capacity;
}
inline bool ValidDifficulty(const F7Difficulty::DifficultyConfig& value) noexcept {
    std::array<char,F7Difficulty::kMaxJsonBytes> text{};std::size_t size=0;
    return F7Difficulty::SerializeConfig(value,text.data(),text.size(),&size).code==F7Difficulty::ConfigCode::Ok;
}
inline bool EqualDifficulty(const F7Difficulty::DifficultyConfig& a,const F7Difficulty::DifficultyConfig& b) noexcept {
    std::array<char,F7Difficulty::kMaxJsonBytes> x{},y{};std::size_t nx=0,ny=0;
    return F7Difficulty::SerializeConfig(a,x.data(),x.size(),&nx).code==F7Difficulty::ConfigCode::Ok&&
        F7Difficulty::SerializeConfig(b,y.data(),y.size(),&ny).code==F7Difficulty::ConfigCode::Ok&&
        nx==ny&&std::memcmp(x.data(),y.data(),nx)==0;
}
inline Result PutArea(F7Difficulty::DifficultyConfig& value,int field,const F7Difficulty::Preset& preset,bool enabled) noexcept {
    if(field<0||field>65535||value.areaCount>value.areas.size())return Result::Invalid;
    auto candidate=value;std::size_t slot=0;
    while(slot<candidate.areaCount&&candidate.areas[slot].fieldRow!=field)++slot;
    if(slot==candidate.areaCount){if(slot==candidate.areas.size())return Result::Full;++candidate.areaCount;}
    candidate.areas[slot]={enabled,field,preset};
    if(!ValidDifficulty(candidate))return Result::Invalid;
    value=candidate;return Result::Applied;
}
inline bool EraseArea(F7Difficulty::DifficultyConfig& value,std::size_t slot) noexcept {
    if(value.areaCount>value.areas.size()||slot>=value.areaCount)return false;
    for(std::size_t i=slot;i+1<value.areaCount;++i)value.areas[i]=value.areas[i+1];
    value.areas[--value.areaCount]={};return true;
}
// Compare and publish only the edited subsystem under the existing config lock.
// Force telemetry and other settings changed while a menu is open are preserved.
Result CommitMusic(const F7MusicConfig& expected,const F7MusicConfig& requested);
Result CommitDifficulty(const F7Difficulty::DifficultyConfig& expected,const F7Difficulty::DifficultyConfig& requested);
}
