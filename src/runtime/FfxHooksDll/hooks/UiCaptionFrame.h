#pragma once
#include "UiLanguage.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace FfxHooks::UiCaption {
inline constexpr unsigned MaxLines=64;
inline constexpr std::size_t MaxText=2048;
inline constexpr std::uint32_t MaxAgeMs=250;
inline bool Fresh(std::uint32_t stamp,std::uint32_t now) noexcept {return now-stamp<=MaxAgeMs;}
struct Line {
    std::array<char,MaxText> text{};
    float x=0,y=0,width=0,height=0;
    bool title=false,wrap=false,center=false;
};
struct Frame {
    std::array<Line,MaxLines> lines{};
    unsigned count=0;
    UiLanguage::Locale locale=UiLanguage::Locale::English;
    std::uint32_t stamp=0,epoch=0;
    bool Add(const char* text,float x,float y,float width,float height,
             bool title=false,bool wrap=false,bool center=false) noexcept {
        if(count==MaxLines||!UiLanguage::ValidUtf8(text)||
           !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||
           x<0||y<0||x>=1||y>=1||width<=0||height<=0||width>1||height>1)return false;
        auto& line=lines[count];
        UiLanguage::CopyUtf8(line.text.data(),line.text.size(),text);
        line.x=x;line.y=y;line.width=(std::min)(width,1-x);line.height=(std::min)(height,1-y);
        line.title=title;line.wrap=wrap;line.center=center;++count;return true;
    }
};
}
