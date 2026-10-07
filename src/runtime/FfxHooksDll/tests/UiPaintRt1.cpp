// Jarvis-HOOK: isolated GDI and menu publication; never loads the game or DLL.
#include <windows.h>
// GDI execution time is not a menu heartbeat. Keep the fixture clock explicit
// so a slow CI worker cannot expire a frame while testing its drawn pixels.
static DWORD uiTestTick=10000;
static DWORD UiTestTickCount() noexcept {return uiTestTick;}
#define GetTickCount UiTestTickCount
#include "../hooks/UiLanguagePaint.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
static unsigned checks=0,failures=0;
static bool nativeFixture=false;
static std::filesystem::path previewDirectory;
static void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
static void WaitFont(FfxHooks::UiLanguage::Locale locale){
    if(!nativeFixture)return;
    FfxHooks::UiNativeFont::Request(locale);
    for(unsigned i=0;i<6000&&FfxHooks::UiNativeFont::Loading(locale);++i){Sleep(5);FfxHooks::UiNativeFont::Request(locale);}
    Check(FfxHooks::UiNativeFont::Current(locale)!=nullptr,"private original font archive becomes ready outside the drawing thread");
}
static void SavePreview(const void* pixels,int width,int height,FfxHooks::UiLanguage::Locale locale){
    if(previewDirectory.empty()||width!=1920)return;
    const auto name=previewDirectory/(std::string("ui-")+FfxHooks::UiLanguage::Code(locale)+".bmp");
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4D42;file.bfOffBits=sizeof(file)+sizeof(info);
    info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    info.biSizeImage=static_cast<DWORD>(width*height*4);file.bfSize=file.bfOffBits+info.biSizeImage;
    std::ofstream out(name,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));
    out.write(static_cast<const char*>(pixels),info.biSizeImage);
}
static void Ready(FfxHooks::UiLanguage::Locale locale){
    WaitFont(locale);
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
            WaitFont(locale);
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
            Check(UiOverlay::LastPaintedLines==3,"all Unicode captions are drawn");
            if(nativeFixture){Check(UiOverlay::LastNativeGlyphs>0,"localized rows retain the original game glyphs");
                if(index<=5)Check(UiOverlay::LastFallbackGlyphs==0,"Western captions never replace native letters with a system font");}
            Check(UiOverlay::LastClippedLines==0,"sample layout fits at this resolution");
            bool painted=false;const auto* rgb=static_cast<const std::uint32_t*>(pixels);
            for(std::size_t i=0;i<bytes/4;++i)if(rgb[i]){painted=true;break;}
            Check(painted,"rendered DIB contains actual glyph pixels");
            unsigned titlePixels=0,outside=0;
            for(int y=0;y<resolution[1];++y)for(int x=0;x<resolution[0];++x){
                if(!(rgb[std::size_t(y)*std::size_t(resolution[0])+std::size_t(x)]&0xFFFFFF))continue;
                const bool horizontal=x>=resolution[0]/10&&x<resolution[0]*9/10;
                const bool title=y>=resolution[1]/10&&y<resolution[1]*17/100;
                const bool help=y>=resolution[1]/5&&y<resolution[1]*32/100;
                const bool name=y>=resolution[1]*2/5&&y<resolution[1]*46/100;
                if(horizontal&&title)++titlePixels;if(!horizontal||!(title||help||name))++outside;
            }
            Check(titlePixels>0&&outside==0,"glyph pixels keep the requested top-down caption positions and clipping");
            SavePreview(pixels,resolution[0],resolution[1],locale);
            Check(UiOverlay::Copy(copy,GetTickCount(),&revision),"capture of the displayed frame");
            UiOverlay::Clear();
            Check(!UiOverlay::Current(locale,copy.epoch,revision,true),"late presentation cannot resurrect a closed menu");
        }
        UiOverlay::ReleaseFonts();SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    }
}
static void NativeTypographyCase(){
    using namespace FfxHooks;
    char enabled[2]{};if(!GetEnvironmentVariableA("FFXHOOKS_TEST_NATIVE_TYPOGRAPHY",enabled,2))return;
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=1920;info.bmiHeader.biHeight=-1080;info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
    const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    const auto old=SelectObject(dc,bitmap);UiOverlay::Clear();Ready(UiLanguage::Locale::Portuguese);
    {UiOverlay::FrameScope frame(UiLanguage::Locale::Portuguese);UiOverlay::Caption("0123456789",.1f,.1f,.8f,.1f,true);}
    std::memset(pixels,0,1920u*1080u*4u);UiOverlay::Paint(dc,{0,0,1920,1080});GdiFlush();
    unsigned visible=0,neutral=0;const auto* rgb=static_cast<const std::uint32_t*>(pixels);
    for(unsigned i=0;i<1920u*1080u;++i){const auto p=rgb[i];const unsigned r=(p>>16)&255,g=(p>>8)&255,b=p&255;
        if(r>30||g>30||b>30){++visible;if(std::abs(int(r)-int(b))<=1&&std::abs(int(r)-int(g))<=4)++neutral;}}
    Check(visible>100&&neutral*100u/visible>95u,
          "localized digits preserve the neutral gradient of the original FFX atlas");
    if(nativeFixture)Check(UiOverlay::LastNativeGlyphs==10&&UiOverlay::LastFallbackGlyphs==0,
                          "all localized digits come from the actual game atlas");
    UiOverlay::ReleaseFonts();SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
int main(){
    using namespace FfxHooks;
    wchar_t archive[4096]{},previews[4096]{};
    if(GetEnvironmentVariableW(L"FFXHOOKS_TEST_NATIVE_FONT_VBF",archive,4096)){
        nativeFixture=true;UiNativeFont::Configure(archive);
        if(GetEnvironmentVariableW(L"FFXHOOKS_TEST_UI_PREVIEWS",previews,4096)){
            previewDirectory=previews;std::filesystem::create_directories(previewDirectory);}}
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
    GdiCases();NativeTypographyCase();
    Check(UiCaption::ResolveOverlayAlpha(0x80787878u)==0x80787878u,"native antialias coverage stays translucent during texture upload");
    Check(UiCaption::ResolveOverlayAlpha(0x00787878u)==0xFF787878u&&UiCaption::ResolveOverlayAlpha(0)==0,
          "legacy GDI text becomes visible while untouched background remains transparent");
    UiOverlay::Clear();UiOverlay::ReleaseFonts();
    std::printf("UI renderer: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
