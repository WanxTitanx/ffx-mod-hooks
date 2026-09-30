// Jarvis-HOOK: isolated GDI and menu publication; never loads the game or DLL.
#include <windows.h>
// GDI execution time is not a menu heartbeat. Keep the fixture clock explicit
// so a slow CI worker cannot expire a frame while testing its drawn pixels.
static DWORD uiTestTick=10000;
static DWORD UiTestTickCount() noexcept {return uiTestTick;}
#define GetTickCount UiTestTickCount
#include "../hooks/UiLanguagePaint.h"
#include <cstdio>
#include <cstring>
static unsigned checks=0,failures=0;
static void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
static void Ready(FfxHooks::UiLanguage::Locale locale){
    FfxHooks::UiOverlay::ReadyTick.store(GetTickCount());
    FfxHooks::UiOverlay::ReadyLocale.store(static_cast<int>(locale));
}
static void GdiCases(){
    using namespace FfxHooks;
    const int resolutions[][2]={{1280,720},{1920,1080},{3840,2160}};
    static UiCaption::Frame copy;
    for(const auto& resolution:resolutions){
        HDC dc=CreateCompatibleDC(nullptr);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=resolution[0];info.bmiHeader.biHeight=-resolution[1];
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        Check(dc&&bitmap&&pixels,"real GDI surface allocated");
        if(!dc||!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);continue;}
        const auto old=SelectObject(dc,bitmap);
        const RECT bounds{0,0,resolution[0],resolution[1]};
        const auto bytes=static_cast<std::size_t>(resolution[0])*resolution[1]*4;
        for(unsigned index=1;index<UiLanguage::LocaleCount;++index){
            const auto locale=static_cast<UiLanguage::Locale>(index);
            UiOverlay::Clear();UiOverlay::ReleaseFonts();
            {UiOverlay::FrameScope request(locale);Check(UiOverlay::Building==nullptr,"English retained before font admission");}
            UiOverlay::Paint(dc,bounds);
            Check(UiOverlay::FontsAvailable,"OS font covers the entire selected catalog");
            Check(UiOverlay::ReadyLocale.load()==-1,"painting alone does not promise a presented surface");
            std::uint32_t revision=0;
            Check(UiOverlay::Copy(copy,GetTickCount(),&revision),"priming request remains available");
            Check(UiOverlay::Current(locale,copy.epoch,revision,true),"presented priming frame admits its locale");
            {
                UiOverlay::FrameScope frame(locale);
                Check(UiOverlay::Caption(UiLanguage::Text("F8 - Settings"),.1f,.1f,.8f,.07f,true),"translated title captured");
                Check(UiOverlay::Caption(UiLanguage::Text("Choose the language of the DLL menus. Game text and audio are independent."),.1f,.2f,.8f,.12f,false,true),"translated help captured");
                Check(UiOverlay::Caption(UiLanguage::Name(locale),.1f,.4f,.8f,.06f),"native language name captured");
            }
            std::memset(pixels,0,bytes);UiOverlay::Paint(dc,bounds);GdiFlush();
            Check(UiOverlay::LastPaintedLines==3,"all Unicode captions drawn by GDI");
            Check(UiOverlay::LastClippedLines==0,"sample layout fits at this resolution");
            bool painted=false;const auto* rgb=static_cast<const std::uint32_t*>(pixels);
            for(std::size_t i=0;i<bytes/4;++i)if(rgb[i]){painted=true;break;}
            Check(painted,"rendered DIB contains actual glyph pixels");
            Check(UiOverlay::Copy(copy,GetTickCount(),&revision),"capture of the displayed frame");
            UiOverlay::Clear();
            Check(!UiOverlay::Current(locale,copy.epoch,revision,true),"late presentation cannot resurrect a closed menu");
        }
        UiOverlay::ReleaseFonts();SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    }
}
int main(){
    using namespace FfxHooks;
    const auto pt=UiLanguage::Locale::Portuguese;
    Ready(pt);
    UiOverlay::Paint(nullptr,{0,0,1280,720});
    Check(UiOverlay::Displayable(pt)==UiLanguage::Locale::English,"missing surface withdraws readiness");
    Ready(pt);
    UiOverlay::Clear();
    Check(UiOverlay::Displayable(pt)==UiLanguage::Locale::English,"close withdraws readiness");
    static UiCaption::Frame copy;
    Ready(pt);
    {UiOverlay::FrameScope scope(pt);UiOverlay::Caption("row",.2f,.2f,.5f,.05f);UiOverlay::Clear();}
    Check(!UiOverlay::Copy(copy,GetTickCount()),"close invalidates in-flight frame");
    Ready(pt);
    {UiOverlay::FrameScope scope(pt);UiOverlay::Caption("row",.2f,.2f,.5f,.05f);}
    const auto revision=UiOverlay::Revision.load();
    {UiOverlay::FrameScope scope(pt);UiOverlay::Caption("row",.2f,.2f,.5f,.05f);}
    Check(UiOverlay::Revision.load()==revision,"unchanged captions do not force texture uploads");
    Check(sizeof(UiOverlay::FrameScope)<64,"caption buffer stays off the x86 stack");
    UiOverlay::ReadyTick.store(GetTickCount()-UiCaption::MaxAgeMs-1);
    Check(UiOverlay::Displayable(pt)==UiLanguage::Locale::English,"lost Present heartbeat restores English");
    Ready(pt);
    {UiOverlay::FrameScope parent(pt);
        UiOverlay::Caption("first",.1f,.1f,.5f,.05f);
        {UiOverlay::FrameScope nested(UiLanguage::Locale::English);Check(!UiOverlay::Caption("nested",.1f,.1f,.5f,.05f),"nested frame uses native fallback");}
        UiOverlay::Caption("second",.1f,.2f,.5f,.05f);
    }
    Check(UiOverlay::Copy(copy,GetTickCount())&&copy.count==2,"nested drawing preserves the outer snapshot");
    uiTestTick+=UiCaption::MaxAgeMs+1;
    Check(!UiOverlay::Copy(copy,GetTickCount()),"an actually expired caption frame is still rejected");
    GdiCases();
    UiOverlay::Clear();UiOverlay::ReleaseFonts();
    std::printf("UI renderer: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
