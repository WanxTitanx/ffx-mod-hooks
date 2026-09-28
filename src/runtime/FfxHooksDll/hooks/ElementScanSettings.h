#pragma once
#include "ElementScanCore.h"
#include "ElementMenuCatalog.h"
#include "../shared/Config.h"
namespace FfxHooks::ElementScan {
inline constexpr const char* ColorKeys[]={"element_scan.holy_rgb","element_scan.dark_rgb","element_scan.extra_rgb","element_scan.other_rgb"};
inline constexpr const char* EnabledKeys[]={"element_scan.holy_enabled","element_scan.dark_enabled","element_scan.extra_enabled","element_scan.other_enabled"};
inline constexpr const char* BitKey="element_scan.extra_bit";
inline bool HookSettingKey(const char* element,const char* suffix,char (&out)[128]){
    if(!element||!ElementalDominion::ValidKey(element))return false;
    std::snprintf(out,sizeof(out),"element_scan.hook.%s.%s",element,suffix);return true;
}
inline bool ReadHookPresentation(const ElementMenu::Item& element,std::uint32_t& rgb,bool& enabled){
    rgb=element.rgb;enabled=true;char colorKey[128]{},enabledKey[128]{};
    if(!element.available||element.nativeBit||!HookSettingKey(element.key,"rgb",colorKey)||!HookSettingKey(element.key,"enabled",enabledKey))return false;
    const auto color=Config::ReadIntExact(colorKey,0,0xFFFFFF),visible=Config::ReadIntExact(enabledKey,0,1);
    if(color.state==Config::IntReadState::Invalid||visible.state==Config::IntReadState::Invalid)return false;
    if(color.state==Config::IntReadState::Valid)rgb=static_cast<std::uint32_t>(color.value);
    if(visible.state==Config::IntReadState::Valid)enabled=visible.value!=0;return true;
}
inline bool SaveHookPresentation(const char* element,const char* suffix,unsigned value){
    char key[128]{};
    const bool color=std::strcmp(suffix,"rgb")==0;
    if((!color&&std::strcmp(suffix,"enabled")!=0)||value>(color?0xFFFFFFu:1u)||!HookSettingKey(element,suffix,key))return false;
    if(!Config::SetInt(key,static_cast<int>(value)))return false;
    const auto read=Config::ReadIntExact(key,0,color?0xFFFFFF:1);
    return read.state==Config::IntReadState::Valid&&static_cast<unsigned>(read.value)==value;
}
inline bool ReadSettings(Settings& out){
    Settings value{};
    for(unsigned i=0;i<ExtraColumns;++i){const auto read=Config::ReadIntExact(ColorKeys[i],0,0xFFFFFF);
        if(read.state==Config::IntReadState::Invalid)return false;
        if(read.state==Config::IntReadState::Valid)value.rgb[i]=static_cast<std::uint32_t>(read.value);
        const auto enabled=Config::ReadIntExact(EnabledKeys[i],0,1);
        if(enabled.state==Config::IntReadState::Invalid)return false;
        if(enabled.state==Config::IntReadState::Valid)value.enabled[i]=static_cast<unsigned>(enabled.value);
    }
    const auto bit=Config::ReadIntExact(BitKey,32,64);
    if(bit.state==Config::IntReadState::Invalid)return false;
    if(bit.state==Config::IntReadState::Valid)value.extraBit=static_cast<unsigned>(bit.value);
    if(!Valid(value))return false;
    out=value;
    return true;
}
inline bool SaveEnabled(unsigned index,bool enabled){
    if(index>=ExtraColumns||!Config::SetInt(EnabledKeys[index],enabled?1:0))return false;
    const auto read=Config::ReadIntExact(EnabledKeys[index],0,1);
    return read.state==Config::IntReadState::Valid&&read.value==(enabled?1:0);
}
inline bool SaveColor(unsigned index,std::uint32_t rgb){
    if(index>=ExtraColumns||rgb>0xFFFFFF||!Config::SetInt(ColorKeys[index],static_cast<int>(rgb)))return false;
    const auto read=Config::ReadIntExact(ColorKeys[index],0,0xFFFFFF);
    return read.state==Config::IntReadState::Valid&&static_cast<std::uint32_t>(read.value)==rgb;
}
inline bool SaveBit(unsigned bit){
    if((bit!=32&&bit!=64)||!Config::SetInt(BitKey,static_cast<int>(bit)))return false;
    const auto read=Config::ReadIntExact(BitKey,32,64);return read.state==Config::IntReadState::Valid&&static_cast<unsigned>(read.value)==bit;
}
inline bool RegisteredPresentation(const char* key,unsigned nativeBit,std::uint32_t originalRgb,std::uint32_t& rgb,bool& visible){
    rgb=originalRgb;visible=true;
    if(nativeBit){
        if(nativeBit<0x10)return true;
        Settings settings{};if(!ReadSettings(settings))return false;
        const unsigned index=nativeBit==0x10?0:nativeBit==0x80?1:nativeBit==settings.extraBit?2:3;
        rgb=settings.rgb[index];visible=settings.enabled[index]!=0;return true;
    }
    ElementMenu::Item item{};item.available=true;item.rgb=originalRgb;
    if(!key||!ElementalDominion::ValidKey(key))return false;
    std::snprintf(item.key,sizeof(item.key),"%s",key);return ReadHookPresentation(item,rgb,visible);
}
}
