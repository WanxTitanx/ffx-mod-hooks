#pragma once
// Jarvis-HOOK: the same native letter proportions for every translated caption.
// Only absent Unicode scalars use a GDI contour, fitted into native metrics and
// the native grayscale palette. Whole translated strings never change family.
#include "UiNativeFont.h"
#include "UiCaptionFrame.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace FfxHooks::UiTypography {
struct Counts {unsigned lines=0,clipped=0,nativeGlyphs=0,fallbackGlyphs=0;};
inline HFONT FallbackFont=nullptr;
inline std::unordered_map<std::uint32_t,UiNativeFont::Glyph> FallbackGlyphs;
inline void Clear() noexcept {FallbackGlyphs.clear();if(FallbackFont)DeleteObject(FallbackFont);FallbackFont=nullptr;}
inline bool Scalar(const char*& text,std::uint32_t& cp){
    const auto* p=reinterpret_cast<const unsigned char*>(text);const auto n=UiLanguage::ScalarBytes(p);if(!n)return false;
    cp=*p;if(n>1){cp&=(1u<<(7-n))-1u;for(std::size_t i=1;i<n;++i)cp=(cp<<6)|(p[i]&63);}text+=n;return true;
}
inline const UiNativeFont::Glyph* Fallback(std::uint32_t cp,HFONT font,const UiNativeFont::Library& library){
    const auto found=FallbackGlyphs.find(cp);if(found!=FallbackGlyphs.end())return &found->second;
    if(FallbackGlyphs.size()>=1024)return nullptr;
    if(!FallbackFont){LOGFONTW info{};if(!GetObjectW(font,sizeof(info),&info))return nullptr;
        info.lfHeight=-64;info.lfWidth=0;info.lfWeight=FW_SEMIBOLD;info.lfQuality=ANTIALIASED_QUALITY;
        FallbackFont=CreateFontIndirectW(&info);if(!FallbackFont)return nullptr;}
    wchar_t text[3]{};int count=1;
    if(cp<=0xFFFF)text[0]=static_cast<wchar_t>(cp);
    else {const auto value=cp-0x10000;text[0]=static_cast<wchar_t>(0xD800+(value>>10));text[1]=static_cast<wchar_t>(0xDC00+(value&1023));count=2;}
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return nullptr;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=128;info.bmiHeader.biHeight=-128;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap||!bits){if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return nullptr;}
    const auto priorBitmap=SelectObject(dc,bitmap),priorFont=SelectObject(dc,FallbackFont);
    std::memset(bits,0,128*128*4);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
    RECT box{0,0,128,128};SIZE extent{};TEXTMETRICW metrics{};WORD indices[2]{};
    const bool covered=GetGlyphIndicesW(dc,text,count,indices,GGI_MARK_NONEXISTING_GLYPHS)!=GDI_ERROR&&
        indices[0]!=0xFFFF&&(count==1||indices[1]!=0xFFFF);
    const bool drawn=covered&&GetTextExtentPoint32W(dc,text,count,&extent)&&GetTextMetricsW(dc,&metrics)&&
        DrawTextW(dc,text,count,&box,DT_NOPREFIX|DT_SINGLELINE|DT_LEFT)>0;
    GdiFlush();UiNativeFont::Glyph glyph;
    if(drawn){
        glyph.advance=(std::clamp)(float(extent.cx)*14.f/64.f,2.f,14.f);
        const auto* palette=library.Find('0');const auto* pixels=static_cast<const std::uint32_t*>(bits);unsigned last=255;
        for(unsigned y=0;y<UiNativeFont::GlyphHeight;++y){unsigned sum=0,n=0;
            if(palette)for(unsigned x=0;x<UiNativeFont::GlyphWidth;++x){const auto p=palette->pixels[y*UiNativeFont::GlyphWidth+x];if((p>>24)>224){sum+=p&255;++n;}}
            if(n)last=sum/n;
            const auto sourceY=(std::min)(127u,static_cast<unsigned>((float(y)+.5f)*float(metrics.tmHeight)/float(UiNativeFont::GlyphHeight)));
            for(unsigned x=0;x<UiNativeFont::GlyphWidth;++x){const auto sourceX=(std::min)(127u,static_cast<unsigned>((float(x)+.5f)*64.f/float(UiNativeFont::GlyphWidth)));
                const auto p=pixels[sourceY*128+sourceX];const unsigned alpha=(std::max)({p&255,(p>>8)&255,(p>>16)&255});
                glyph.pixels[y*UiNativeFont::GlyphWidth+x]=alpha?(alpha<<24)|(last<<16)|(last<<8)|last:0;}
        }
    }
    SelectObject(dc,priorFont);SelectObject(dc,priorBitmap);DeleteObject(bitmap);DeleteDC(dc);
    if(!drawn)return nullptr;return &FallbackGlyphs.emplace(cp,std::move(glyph)).first->second;
}
struct Surface {
    unsigned char* bits=nullptr;int width=0,height=0,stride=0;
    explicit Surface(HDC dc){DIBSECTION dib{};const auto bitmap=GetCurrentObject(dc,OBJ_BITMAP);
        if(bitmap&&GetObjectW(bitmap,sizeof(dib),&dib)==sizeof(dib)&&dib.dsBm.bmBitsPixel==32&&dib.dsBm.bmBits&&dib.dsBm.bmWidthBytes>=dib.dsBm.bmWidth*4){
            bits=static_cast<unsigned char*>(dib.dsBm.bmBits);width=dib.dsBm.bmWidth;height=dib.dsBm.bmHeight;stride=dib.dsBm.bmWidthBytes;}}
    // Our Present owner creates a negative-height DIB. GetObject reports its
    // absolute height on Windows and cannot reconstruct that creation contract.
    std::uint32_t& At(int x,int y){return reinterpret_cast<std::uint32_t*>(bits+std::size_t(y)*std::size_t(stride))[x];}
};
inline std::uint32_t Sample(const UiNativeFont::Glyph& glyph,float x,float y){
    const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));const float fx=x-float(ix),fy=y-float(iy);
    const float weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};float a=0,colors[3]{};
    for(unsigned i=0;i<4;++i){const int sx=(std::clamp)(ix+int(i&1),0,int(UiNativeFont::GlyphWidth)-1),sy=(std::clamp)(iy+int(i>>1),0,int(UiNativeFont::GlyphHeight)-1);
        const auto p=glyph.pixels[unsigned(sy)*UiNativeFont::GlyphWidth+unsigned(sx)];const float alpha=float(p>>24)*weights[i];a+=alpha;
        for(unsigned c=0;c<3;++c)colors[c]+=float((p>>(8*c))&255)*alpha;}
    if(a<.5f)return 0;auto result=std::uint32_t(std::lround(a))<<24;
    for(unsigned c=0;c<3;++c)result|=std::uint32_t(std::lround(colors[c]/a))<<(8*c);return result;
}
inline void Blend(std::uint32_t& destination,std::uint32_t source){
    const unsigned alpha=source>>24;if(!alpha)return;
    const unsigned previous=(destination>>24)?destination>>24:(destination&0xFFFFFF)?255u:0u;
    const unsigned out=alpha+(previous*(255-alpha)+127)/255;if(!out)return;
    auto result=out<<24;
    for(unsigned shift=0;shift<24;shift+=8){const unsigned a=(source>>shift)&255,b=(destination>>shift)&255;
        result|=((a*alpha+(b*previous*(255-alpha)+127)/255+out/2)/out)<<shift;}
    destination=result;
}
inline void Blit(Surface& surface,const UiNativeFont::Glyph& glyph,float x,float y,float width,float height,const RECT& clip){
    const int left=(std::max)({0,int(clip.left),static_cast<int>(std::floor(x))});
    const int top=(std::max)({0,int(clip.top),static_cast<int>(std::floor(y))});
    const int right=(std::min)({surface.width,int(clip.right),static_cast<int>(std::ceil(x+width))});
    const int bottom=(std::min)({surface.height,int(clip.bottom),static_cast<int>(std::ceil(y+height))});
    for(int py=top;py<bottom;++py)for(int px=left;px<right;++px){
        const float sx=(float(px)+.5f-x)*float(UiNativeFont::GlyphWidth)/width-.5f,sy=(float(py)+.5f-y)*float(UiNativeFont::GlyphHeight)/height-.5f;
        Blend(surface.At(px,py),Sample(glyph,sx,sy));}
}
struct Character {const UiNativeFont::Glyph* glyph=nullptr;std::uint32_t cp=0;bool native=false;};
struct Row {std::size_t begin=0,end=0;float width=0;};
inline std::vector<Row> Rows(const std::vector<Character>& chars,float scale,float width,bool wrap){
    std::vector<Row> result;std::size_t first=0;
    while(first<chars.size()){
        std::size_t end=first,space=chars.size();float used=0,beforeSpace=0;
        while(end<chars.size()&&chars[end].cp!='\n'){
            const float next=chars[end].glyph->advance*scale;
            if(wrap&&end>first&&used+next>width)break;
            if(chars[end].cp==' '){space=end;beforeSpace=used;}used+=next;++end;
        }
        if(wrap&&end<chars.size()&&chars[end].cp!='\n'&&space<end){end=space;used=beforeSpace;}
        result.push_back({first,end,used});first=end;
        if(first<chars.size()&&chars[first].cp=='\n')++first;
        else while(first<chars.size()&&chars[first].cp==' ')++first;
        if(first==end&&first<chars.size()&&end==result.back().begin)++first;
    }
    return result;
}
inline bool Paint(HDC dc,const RECT& bounds,const UiCaption::Frame& frame,const UiNativeFont::Library& library,HFONT fallbackFont,Counts& counts){
    // Finish queued GDI writes before accessing this owner's existing DIB.
    GdiFlush();Surface surface(dc);if(!surface.bits)return false;
    const float w=float(bounds.right-bounds.left),h=float(bounds.bottom-bounds.top);
    constexpr float fits[]={1.f,.92f,.84f,.76f,.68f,.60f,.52f};
    for(unsigned lineIndex=0;lineIndex<frame.count;++lineIndex){const auto& line=frame.lines[lineIndex];std::vector<Character> chars;
        for(const char* p=line.text.data();*p;){std::uint32_t cp=0;if(!Scalar(p,cp))return false;
            if(cp=='\n'){chars.push_back({nullptr,cp,true});continue;}if(cp=='\r')continue;
            const auto* glyph=library.Find(cp);const bool native=glyph!=nullptr;if(!glyph)glyph=Fallback(cp,fallbackFont,library);if(!glyph)return false;
            chars.push_back({glyph,cp,native});}
        if(chars.empty())continue;
        RECT box{bounds.left+static_cast<LONG>(line.x*w),bounds.top+static_cast<LONG>(line.y*h),
                 bounds.left+static_cast<LONG>((line.x+line.width)*w),bounds.top+static_cast<LONG>((line.y+line.height)*h)};
        float sx=0,sy=0,step=0;std::vector<Row> rows;bool fitsBox=false;
        for(float fit:fits){
            sx=(line.title?.78f:.52f)*w/512.f*fit;sy=(line.title?1.f:.70f)*h/416.f*fit;step=18.f*sy*1.08f;
            rows=Rows(chars,sx,float(box.right-box.left),line.wrap);float widest=0;for(const auto& row:rows)widest=(std::max)(widest,row.width);
            fitsBox=widest<=float(box.right-box.left)&&(!rows.empty()&&(float(rows.size()-1)*step+18.f*sy)<=float(box.bottom-box.top));
            if(fitsBox)break;
        }
        const auto* ellipsis=library.Find(0x2026);bool ellipsize=false;
        if(!fitsBox){++counts.clipped;const auto capacity=(std::max)(1u,static_cast<unsigned>(float(box.bottom-box.top)/step));
            if(rows.size()>capacity)rows.resize(capacity);ellipsize=true;
            auto& last=rows.back();const float reserve=ellipsis?ellipsis->advance*sx:0;
            while(last.end>last.begin&&last.width+reserve>float(box.right-box.left)){--last.end;last.width-=chars[last.end].glyph->advance*sx;}
            last.width+=reserve;
        }
        float y=float(box.top);
        for(std::size_t rowIndex=0;rowIndex<rows.size();++rowIndex){const auto& row=rows[rowIndex];float x=float(box.left)+(line.center?(float(box.right-box.left)-row.width)*.5f:0);
            for(std::size_t i=row.begin;i<row.end;++i){const auto& c=chars[i];Blit(surface,*c.glyph,x,y,14.f*sx,18.f*sy,box);x+=c.glyph->advance*sx;
                if(c.native)++counts.nativeGlyphs;else ++counts.fallbackGlyphs;}
            if(ellipsize&&ellipsis&&rowIndex+1==rows.size())Blit(surface,*ellipsis,x,y,14.f*sx,18.f*sy,box);
            y+=step;
        }
        ++counts.lines;
    }
    return true;
}
}
