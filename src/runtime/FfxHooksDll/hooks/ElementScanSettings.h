#pragma once
#include "ElementScanCore.h"
#include "../shared/Config.h"
namespace FfxHooks::ElementScan {
inline constexpr const char* ColorKeys[]={"element_scan.holy_rgb","element_scan.dark_rgb","element_scan.extra_rgb"};
inline constexpr const char* EnabledKeys[]={"element_scan.holy_enabled","element_scan.dark_enabled","element_scan.extra_enabled"};
inline constexpr const char* BitKey="element_scan.extra_bit";
inline bool ReadSettings(Settings& out){
    Settings value{};
    for(unsigned i=0;i<3;++i){const auto read=Config::ReadIntExact(ColorKeys[i],0,0xFFFFFF);
        if(read.state==Config::IntReadState::Invalid)return false;
        if(read.state==Config::IntReadState::Valid)value.rgb[i]=static_cast<std::uint32_t>(read.value);
        const auto enabled=Config::ReadIntExact(EnabledKeys[i],0,1);
        if(enabled.state==Config::IntReadState::Invalid)return false;
        if(enabled.state==Config::IntReadState::Valid)value.enabled[i]=static_cast<unsigned>(enabled.value);
    }
    const auto bit=Config::ReadIntExact(BitKey,32,64);
    if(bit.state==Config::IntReadState::Invalid)return false;
    if(bit.state==Config::IntReadState::Valid)value.extraBit=static_cast<unsigned>(bit.value);
    if(!Valid(value))return false;out=value;return true;
}
inline bool SaveEnabled(unsigned index,bool enabled){
    if(index>=3||!Config::SetInt(EnabledKeys[index],enabled?1:0))return false;
    const auto read=Config::ReadIntExact(EnabledKeys[index],0,1);
    return read.state==Config::IntReadState::Valid&&read.value==(enabled?1:0);
}
inline bool SaveColor(unsigned index,std::uint32_t rgb){
    if(index>=3||rgb>0xFFFFFF||!Config::SetInt(ColorKeys[index],static_cast<int>(rgb)))return false;
    const auto read=Config::ReadIntExact(ColorKeys[index],0,0xFFFFFF);
    return read.state==Config::IntReadState::Valid&&static_cast<std::uint32_t>(read.value)==rgb;
}
inline bool SaveBit(unsigned bit){
    if((bit!=32&&bit!=64)||!Config::SetInt(BitKey,static_cast<int>(bit)))return false;
    const auto read=Config::ReadIntExact(BitKey,32,64);return read.state==Config::IntReadState::Valid&&static_cast<unsigned>(read.value)==bit;
}
}
