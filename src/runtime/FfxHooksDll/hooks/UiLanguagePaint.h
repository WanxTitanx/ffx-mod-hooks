#pragma once
// Jarvis-HOOK: called only by the existing Present owner, outside DllMain.
#include "UiLanguageOverlay.h"
#include "UiTypography.h"
#include <vector>
#include <algorithm>
#include <cwchar>

namespace FfxHooks::UiOverlay {
inline HFONT Fonts[7]{};
inline int FontHeight=-1,FontLocale=-1;
inline bool FontsAvailable=false;
inline unsigned LastPaintedLines=0,LastClippedLines=0;
inline unsigned LastNativeGlyphs=0,LastFallbackGlyphs=0;
inline std::uint32_t LastPaintedRevision=0;
inline std::uint32_t LastPaintedEpoch=0;
inline UiLanguage::Locale LastPaintedLocale=UiLanguage::Locale::English;
inline bool LastPaintedActive=false;

inline void ReleaseFonts() noexcept {
    LastPaintedActive=false;
    UiTypography::Clear();
    ReadyLocale.store(-1,std::memory_order_release);
    for(auto& font:Fonts){if(font)DeleteObject(font);font=nullptr;}
    FontsAvailable=false;FontHeight=-1;FontLocale=-1;
}
inline bool FontCovers(HDC dc,HFONT font,const std::vector<wchar_t>& glyphs) {
    if(!font||glyphs.empty())return false;
    std::vector<WORD> indices;
    try {indices.resize(glyphs.size());} catch(...) {return false;}
    const auto old=SelectObject(dc,font);
    if(!old||old==HGDI_ERROR)return false;
    const DWORD result=GetGlyphIndicesW(dc,glyphs.data(),static_cast<int>(glyphs.size()),indices.data(),GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(dc,old);
    return result!=GDI_ERROR&&std::find(indices.begin(),indices.end(),WORD(0xFFFF))==indices.end();
}
inline bool EnsureFonts(HDC dc,int height,UiLanguage::Locale locale) {
    const int index=static_cast<int>(locale);
    if(FontHeight==height&&FontLocale==index)return FontsAvailable;
    ReleaseFonts();FontHeight=height;FontLocale=index;
    std::array<bool,65536> present{};
    for(unsigned c=32;c<127;++c)present[c]=true;
    auto collect=[&](const char* text){
        wchar_t wide[UiCaption::MaxText]{};
        const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,wide,static_cast<int>(std::size(wide)));
        if(!count)return false;
        for(int i=0;i<count-1;++i)present[static_cast<unsigned short>(wide[i])]=true;
        return true;
    };
    if(!collect(UiLanguage::Name(locale)))return false;
    for(std::size_t row=0;row<UiLanguage::EntryCount();++row)
        if(!collect(UiLanguage::Text(UiLanguage::EntryKey(row),locale)))return false;
    std::vector<wchar_t> glyphs;
    for(unsigned c=1;c<present.size();++c)if(present[c])glyphs.push_back(static_cast<wchar_t>(c));
    const wchar_t* preferred=locale==UiLanguage::Locale::Japanese?L"Yu Gothic UI":
        locale==UiLanguage::Locale::Korean?L"Malgun Gothic":
        locale==UiLanguage::Locale::Chinese?L"Microsoft YaHei UI":L"Segoe UI";
    const wchar_t* candidates[]={preferred,L"Noto Sans CJK JP",L"Noto Sans CJK KR",L"Noto Sans CJK SC",
        L"Meiryo",L"Malgun Gothic",L"Microsoft YaHei",L"Segoe UI",L"Arial Unicode MS",L"DejaVu Sans"};
    const wchar_t* selected=nullptr;
    for(const auto* face:candidates){
        HFONT sample=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face);
        const bool covers=FontCovers(dc,sample,glyphs);
        if(sample)DeleteObject(sample);
        if(covers){selected=face;break;}
    }
    if(!selected)return false;
    constexpr float scales[]={.030f,.026f,.023f,.021f,.019f,.017f,.014f};
    for(unsigned i=0;i<std::size(Fonts);++i){
        const int size=(std::max)(10,static_cast<int>(height*scales[i]));
        Fonts[i]=CreateFontW(-size,0,0,0,i==0?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,selected);
        if(!Fonts[i]){ReleaseFonts();FontHeight=height;FontLocale=index;return false;}
    }
    FontsAvailable=true;return true;
}

inline void PaintCore(HDC dc,const RECT& surface) {
    // Present already serializes this renderer. Large snapshots stay off the x86 stack.
    static UiCaption::Frame frame{};
    LastPaintedLines=0;LastClippedLines=0;
    LastNativeGlyphs=LastFallbackGlyphs=0;
    LastPaintedActive=false;
    const bool current=Copy(frame,GetTickCount(),&LastPaintedRevision);
    if(!dc||!current){
        ReadyLocale.store(-1,std::memory_order_release);return;
    }
    const int width=surface.right-surface.left,height=surface.bottom-surface.top;
    UiNativeFont::Request(frame.locale);
    if(UiNativeFont::Loading(frame.locale)){Unavailable();return;}
    if(width<=0||height<=0||!EnsureFonts(dc,height,frame.locale)){
        ReadyLocale.store(-1,std::memory_order_release);return;
    }
    LastPaintedEpoch=frame.epoch;LastPaintedLocale=frame.locale;
    if(const auto* native=UiNativeFont::Current(frame.locale)){
        UiTypography::Counts counts;LastPaintedActive=UiTypography::Paint(dc,surface,frame,*native,Fonts[2],counts);
        LastPaintedLines=counts.lines;LastClippedLines=counts.clipped;
        LastNativeGlyphs=counts.nativeGlyphs;LastFallbackGlyphs=counts.fallbackGlyphs;
        if(!LastPaintedActive)Unavailable();return;
    }
    bool success=true;
    for(unsigned i=0;i<frame.count;++i){
        const auto& line=frame.lines[i];
        wchar_t wide[UiCaption::MaxText]{};
        const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.text.data(),-1,wide,static_cast<int>(std::size(wide)));
        if(count<=0){success=false;continue;}
        if(count==1)continue;
        RECT box{surface.left+static_cast<LONG>(line.x*width),surface.top+static_cast<LONG>(line.y*height),
            surface.left+static_cast<LONG>((line.x+line.width)*width),surface.top+static_cast<LONG>((line.y+line.height)*height)};
        const int saved=SaveDC(dc);if(!saved){success=false;continue;}
        if(!SetBkMode(dc,TRANSPARENT)||IntersectClipRect(dc,box.left,box.top,box.right,box.bottom)==ERROR){success=false;RestoreDC(dc,saved);continue;}
        UINT flags=DT_NOPREFIX|(line.wrap?DT_WORDBREAK:DT_SINGLELINE)|(line.center?DT_CENTER:DT_LEFT);
        bool fits=false;
        for(unsigned font=line.title?0:2;font<std::size(Fonts);++font){
            const auto previous=SelectObject(dc,Fonts[font]);RECT measured=box;
            if(!previous||previous==HGDI_ERROR||!DrawTextW(dc,wide,count-1,&measured,flags|DT_CALCRECT)){success=false;break;}
            fits=measured.right-measured.left<=box.right-box.left&&measured.bottom-measured.top<=box.bottom-box.top;
            if(fits)break;
        }
        if(!fits)++LastClippedLines;
        RECT shadow=box;OffsetRect(&shadow,1,1);
        SetTextColor(dc,RGB(1,1,1));DrawTextW(dc,wide,count-1,&shadow,flags|DT_END_ELLIPSIS);
        SetTextColor(dc,RGB(240,246,250));
        if(DrawTextW(dc,wide,count-1,&box,flags|DT_END_ELLIPSIS)>0)++LastPaintedLines;
        else success=false;
        if(!RestoreDC(dc,saved))success=false;
    }
    LastPaintedActive=success;
    if(!success)Unavailable();
}
inline void Paint(HDC dc,const RECT& surface) noexcept {
    try {PaintCore(dc,surface);} catch(...) {ReleaseFonts();Unavailable();}
}
inline bool CanPresent() noexcept {
    return !LastPaintedActive||Current(LastPaintedLocale,LastPaintedEpoch,LastPaintedRevision);
}
inline void Presented() noexcept {
    // Only the Present owner can confirm a successful GPU upload and draw.
    if(LastPaintedActive)(void)Current(LastPaintedLocale,LastPaintedEpoch,LastPaintedRevision,true);
    else Unavailable();
}
}
