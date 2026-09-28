#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace FfxHooks::ElementScan {
inline constexpr unsigned ExtraColumns=4;
struct Settings {
    std::uint32_t rgb[ExtraColumns]={0xFFE080u,0xA35CEFu,0x5FCF7Eu,0x6FB5FFu};unsigned extraBit=0x20;
    // Preferences do not enable the default-OFF master hook.
    // Preserve the legacy selected Custom column; its complementary native bit
    // gets a separate, initially hidden column without changing gameplay masks.
    unsigned enabled[ExtraColumns]={1,1,1,0};
};
inline constexpr float PanelWidth=560.f,RowWidth=540.f;
struct Orb {float x=0,y=4,size=32.7f;unsigned bit=0;std::uint32_t rgb=0;bool active=false;};
inline bool Valid(const Settings& v){
    if(v.extraBit!=0x20&&v.extraBit!=0x40)return false;
    for(unsigned i=0;i<ExtraColumns;++i)if(v.rgb[i]>0xFFFFFF||v.enabled[i]>1)return false;
    return true;
}
inline unsigned VisibleCount(const Settings& v){
    if(!Valid(v))return 0;
    unsigned count=0;
    for(unsigned i=0;i<ExtraColumns;++i)count+=v.enabled[i];
    return count;
}
inline float PanelWidthFor(const Settings& v){const unsigned n=VisibleCount(v);return n?371.f+63.f*n:385.f;}
inline std::array<Orb,ExtraColumns> Orbs(unsigned mask,const Settings& settings){
    std::array<Orb,ExtraColumns> out{};if(!Valid(settings)||mask>255)return out;
    const unsigned bits[]={0x10,0x80,settings.extraBit,0x60u^settings.extraBit};
    unsigned column=0;
    for(unsigned i=0;i<ExtraColumns;++i)if(settings.enabled[i]){
        out[column]={379.f+63.f*column,4.f,32.7f,bits[i],settings.rgb[i],(mask&bits[i])!=0};++column;
    }
    return out;
}
inline std::uint32_t NativeColor(std::uint32_t rgb){return 0x80000000u|((rgb&255u)<<16)|(rgb&0xFF00u)|((rgb>>16)&255u);}
struct Hsv {unsigned h=0,s=0,v=0;};
inline std::uint32_t FromHsv(Hsv hsv){
    if(hsv.h>=360||hsv.s>100||hsv.v>100)return 0;
    const double v=hsv.v/100.0,c=v*hsv.s/100.0,x=c*(1-std::abs(std::fmod(hsv.h/60.0,2.0)-1)),m=v-c;
    double r=0,g=0,b=0;
    switch(hsv.h/60){case 0:r=c;g=x;break;case 1:r=x;g=c;break;case 2:g=c;b=x;break;case 3:g=x;b=c;break;case 4:r=x;b=c;break;default:r=c;b=x;}
    return (static_cast<std::uint32_t>(std::lround((r+m)*255))<<16)|(static_cast<std::uint32_t>(std::lround((g+m)*255))<<8)|static_cast<std::uint32_t>(std::lround((b+m)*255));
}
inline Hsv ToHsv(std::uint32_t rgb){
    const double r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
    const double hi=(std::max)(r,(std::max)(g,b)),lo=(std::min)(r,(std::min)(g,b)),d=hi-lo;
    double h=0;if(d){if(hi==r)h=60*std::fmod((g-b)/d,6.0);else if(hi==g)h=60*((b-r)/d+2);else h=60*((r-g)/d+4);}
    if(h<0)h+=360;
    return {static_cast<unsigned>(std::lround(h))%360,static_cast<unsigned>(std::lround(hi?100*d/hi:0)),static_cast<unsigned>(std::lround(100*hi/255))};
}
}
