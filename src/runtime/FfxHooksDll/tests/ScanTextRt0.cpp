// Jarvis-HOOK: reproduce the extra Sensor text's eight-pass outline admission.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include "NativePlainText.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#ifndef _MSC_VER
#define __cdecl
#endif
static unsigned checks=0,failures=0,bodyGlyphs=0,outlineCalls=0;
static bool nested=false,failDraw=false;
static bool numericRequested=false,guardAvailable=true;
static unsigned guardInstalls=0;
static std::atomic<unsigned> foreignOutlines{0};
static std::thread::id owner=std::this_thread::get_id();
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
namespace EquipmentMenu {bool Active(){return false;}}
struct Menu {void* obj=nullptr;};
static Menu g_nativeMenu,g_arenaPlusMenu,g_sinMenu,g_f7Menu;
static bool ArenaPlusComposePick_IsActive(){return false;}
static bool NativeMenuHubCloseDrainPending(){return false;}
namespace FfxHooks {
bool Maechen_MenuOwned(){return false;}
namespace Arcana::NativeUi {bool TextDrawingActive(){return false;}}
namespace Config {bool GetBool(const char*,bool){return numericRequested;}}
}
using FnNativeTextOutline=int(__cdecl*)(void*,void*,float);
static int __cdecl Outline(void*,void*,float){
    if(std::this_thread::get_id()!=owner)++foreignOutlines;else ++outlineCalls;
    return 8;
}
static std::uint64_t g_nativeTextOutlineTramp=reinterpret_cast<std::uintptr_t>(&Outline);
enum {Text};
static void* originals[1]{};
namespace NativeText=FfxHooks::NativeText;
static void Log(const char*,...){}
static bool StartNativeTextOutlineGuard(std::uintptr_t){++guardInstalls;return guardAvailable;}
#define FFXHOOKS_HAVE_POLYHOOK
#include "ScanTextAdapter.inc"
static int __cdecl Draw(unsigned,const unsigned char* text,float,float,unsigned,float,float){
    if(failDraw)throw std::runtime_error("isolated draw failure");
    if(nested){nested=false;NumericalText("Ward",0,0,1,1);}
    std::thread foreign([]{NativeTextOutline_MenuGuard(nullptr,nullptr,1);});foreign.join();
    for(;*text;++text){NativeTextOutline_MenuGuard(nullptr,nullptr,1);++bodyGlyphs;}
    return 41;
}
#ifdef _WIN32
static int __cdecl NativeFault(unsigned,const unsigned char*,float,float,unsigned,float,float){
    RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return 0;
}
static bool NativeFaultRestores(){
    __try {NativeText::DrawPlain(NativeFault,0,nullptr,0,0,0,1,1);}
    __except(EXCEPTION_EXECUTE_HANDLER){return !NativeText::PlainTextActive();}
    return false;
}
#endif
int main(){
    originals[Text]=reinterpret_cast<void*>(&Draw);
    NumericalText("Imperil resist 0%",0,0,.34f,.58f);
    Check(bodyGlyphs==17,"the crash-dump label reaches the normal glyph body intact");
    Check(outlineCalls==0,"extra Sensor text cannot enter the eight-pass native outline emitter");
    Check(foreignOutlines==1,"Scan text suppression belongs only to its drawing thread");
    Check(NativeTextOutline_MenuGuard(nullptr,nullptr,1)==8&&outlineCalls==1,
          "ordinary native text resumes its outline immediately after the Scan label");
    outlineCalls=bodyGlyphs=0;nested=true;
    NumericalText("Imperil",0,0,1,1);
    Check(bodyGlyphs==11&&outlineCalls==0,"nested labels retain plain drawing until the outer text returns");
    Check(NativeTextOutline_MenuGuard(nullptr,nullptr,1)==8,"nested drawing restores vanilla outline admission");
    failDraw=true;
    try{NumericalText("failure",0,0,1,1);}catch(const std::runtime_error&){}
    Check(NativeTextOutline_MenuGuard(nullptr,nullptr,1)==8,"a failed draw restores vanilla outline admission");
    g_f7Menu.obj=&g_f7Menu;
    Check(NativeTextOutline_MenuGuard(nullptr,nullptr,1)==0,"existing F7 menu ownership still suppresses outlines");
    Check(!ScanPresentationTextReady(0x400000,false,false,false)&&!guardInstalls,
          "disabled Scan never installs a text dependency");
    Check(!ScanPresentationTextReady(0x400000,true,true,true)&&!guardInstalls,
          "validation-only Scan leaves native text unchanged");
    guardAvailable=false;
    Check(!ScanPresentationTextReady(0x400000,true,false,false)&&guardInstalls==1,
          "Scan admission fails closed when its text dependency is unavailable");
    guardAvailable=true;numericRequested=true;
    Check(ScanPresentationTextReady(0x400000,false,false,false)&&guardInstalls==2,
          "standalone numerical Scan requires its text dependency without F8 or Arcana");
    numericRequested=false;
    Check(ScanPresentationTextReady(0x400000,false,true,false)&&guardInstalls==3,
          "expanded Scan protects its extra stat labels too");
#ifdef _WIN32
    Check(NativeFaultRestores(),"a caught native SEH fault restores outline admission under EHsc");
#endif
    std::printf("ScanTextRt0 %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
