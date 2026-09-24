#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::NativeLanguage {
enum class Choice : unsigned {GameDefault=0,English=1,Japanese=2};
struct Settings {Choice voice=Choice::GameDefault,sfx=Choice::GameDefault,video=Choice::GameDefault;};
inline bool Valid(Choice value){return static_cast<unsigned>(value)<=2;}
inline const char* Name(Choice value){return value==Choice::English?"English":value==Choice::Japanese?"Japanese":"Game default";}
struct Patch {std::uintptr_t rva=0;const char* original=nullptr;const char* replacement=nullptr;};
struct Plan {std::array<Patch,12> patches{};unsigned count=0;bool valid=true;};
// Native FFX asset references, verified on executable 78CE3439. Behavior source:
// Kaldaien/UnX faae4359, UnX/language.cpp (GPL-3.0-or-later). Data remains in the
// game's image; only the explicitly selected asset language is changed.
inline Plan BuildPlan(Settings settings){
    Plan plan{};
    if(!Valid(settings.voice)||!Valid(settings.sfx)||!Valid(settings.video)){plan.valid=false;return plan;}
    auto pair=[&](Choice choice,std::uintptr_t jp,std::uintptr_t en,const char* japanese,const char* english){
        if(choice==Choice::English)plan.patches[plan.count++]={jp,japanese,english};
        else if(choice==Choice::Japanese)plan.patches[plan.count++]={en,english,japanese};
    };
    pair(settings.voice,0x74F924,0x74F904,"Voice/JP/ffx_jp_voice_btl.fev","Voice/US/ffx_us_voice_btl.fev");
    pair(settings.voice,0x74FA14,0x74F9F8,"Voice/JP/VoiceFevMapper.txt","Voice/US/VoiceFevMapper.txt");
    pair(settings.voice,0x74FD78,0x74FD4C,"Voice/JP/ffx_jp_voice_btl_iop_bank00.fsb","Voice/US/ffx_us_voice_btl_iop_bank00.fsb");
    pair(settings.voice,0x74FA80,0x74FA74,"Voice/JP/","Voice/US/");
    pair(settings.voice,0x74FAE0,0x74FAF0,"ffx_jp_voice01","ffx_us_voice01");
    pair(settings.voice,0x74FB00,0x74FB10,"ffx_jp_voice270","ffx_us_voice270");
    pair(settings.sfx,0x74FB70,0x74FB60,"SFX/JP/%04d.fev","SFX/US/%04d.fev");
    pair(settings.sfx,0x74FCA0,0x74FC90,"SFX/JP/9999.fev","SFX/US/9999.fev");
    pair(settings.video,0x749BF1,0x749C29,"JP/FFX_VideoList.txt","US/FFX_VideoList.txt");
    if(settings.video!=Choice::GameDefault){
        plan.patches[plan.count++]={0x749C61,"Asia/FFX_VideoList.txt",settings.video==Choice::English?"US/FFX_VideoList.txt":"JP/FFX_VideoList.txt"};
        if(settings.video==Choice::Japanese)plan.patches[plan.count++]={0x74A0FC,"/MetaMenu/GameData/PS3Data/Video/US/timestamp_%s.txt","/MetaMenu/GameData/PS3Data/Video/JP/timestamp_JP.txt"};
    }
    for(unsigned i=0;i<plan.count;++i)if(std::strlen(plan.patches[i].replacement)>std::strlen(plan.patches[i].original))plan.valid=false;
    return plan;
}
}
