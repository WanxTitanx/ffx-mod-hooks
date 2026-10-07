#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/ArcanaNativeUi.h"
#include "../hooks/EquipmentWorkshopNativeUi.h"
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0,actor=0,dir=0,edge=0,nativeEquipCalls=0,tarotLabels=0;
static State state;static std::uint64_t session=1;static NativeUi::Images lastImages;
static unsigned scopedTextDraws=0;
static unsigned lockedLabels=0,blockedMessages=0;
static unsigned windows=0,captions=0;
static float windowYs[2]{},captionYs[2]{};
static int portraitDirection=1;
static std::uintptr_t fixtureBase=0;
static DWORD captureThread=0;
static bool captureReady=true;
struct Rect {float x,y,w,h;};
static Rect abilityBoxes[32]{},abilityIcons[32]{};
static float abilityTextY[32]{};
static unsigned abilityBoxCount=0,abilityIconCount=0,abilityTextCount=0,statusLabelCount=0;
static char statusLabels[32][96]{};
static unsigned char gear[22]{},definition[108]{};
static const unsigned char abilityName[]={0x50,0x71,0x78,0x7B,0x78,0x83,0x88,0};
static unsigned soundCount=0,lastSound=0,nativeFontCalls=0,nativeCaptionCalls=0,nativeCaptionId=0,gridCalls=0;
static bool soundArguments=true,nativeFontStyle=true;
static int slideValue=0;
static Rect nativeCaptionRect{};
static unsigned drawSequence=0,cursorSequence=0,lastTarotBackground=0,cursorCount=0;
static float cursorY=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static bool Capture(State& out,std::uint64_t& generation) noexcept {if(!captureReady||GetCurrentThreadId()!=captureThread)return false;out=state;generation=session;return true;}
static Error EquipCallback(std::uint64_t generation,std::uint64_t revision,unsigned owner,unsigned slot,std::int16_t card,bool transfer) noexcept {
    if(generation!=session)return Error::Stale;
    return Equip(state,revision,owner,slot,card,transfer);
}
static Error ModeCallback(std::uint64_t generation,std::uint64_t revision,Mode mode,bool resolve) noexcept {
    if(generation!=session)return Error::Stale;
    auto slots=state.slots;if(resolve)for(auto& s:slots)s[2]=kEmpty;
    return ChangeMode(state,revision,mode,resolve?&slots:nullptr);
}
static void ImagesCallback(const NativeUi::Images& images) noexcept {lastImages=images;}
static void Log(const char* text){std::fputs(text,stdout);}
static int __cdecl Nothing(){return 0;}
static unsigned __cdecl Actor(){return actor;}
static int __cdecl PortraitDirection(){return portraitDirection;}
static int __cdecl OnePartyMember(){return 1;}
static unsigned __cdecl Weapon(unsigned){return 0x5000;}
static unsigned __cdecl Armor(unsigned){return 0x5001;}
static unsigned __cdecl ReadDir(){return dir;}
static unsigned __cdecl ReadEdge(){return edge;}
static unsigned __cdecl ReadHeld(){return 0;}
static int __cdecl CursorX(){return 227;}
static int __cdecl CursorCapture(float,float y,int){cursorSequence=++drawSequence;++cursorCount;cursorY=y;return 0;}
static int __cdecl QuadCapture(float,float,float width,float height,unsigned,unsigned){
    ++drawSequence;
    const auto sx=reinterpret_cast<float(__cdecl*)(float)>(fixtureBase+(::FfxHooks::ExecutableProfile::Rva<0x244990>()));
    const auto sy=reinterpret_cast<float(__cdecl*)(float)>(fixtureBase+(::FfxHooks::ExecutableProfile::Rva<0x2449D0>()));
    if(std::fabs(width-sx(1140.f))<.01f&&std::fabs(height-sy(54.f))<.01f)lastTarotBackground=drawSequence;
    return 0;
}
static void __cdecl WindowCapture(float x,float y,float w,float h,int style){
    if(windows<2)windowYs[windows]=y;++windows;
    if(style==14&&abilityBoxCount<32)abilityBoxes[abilityBoxCount++]={x,y,w,h};
}
static int __cdecl IconCapture(float x,float y,float w,float h,unsigned,unsigned,unsigned,unsigned,unsigned){if(abilityIconCount<32)abilityIcons[abilityIconCount++]={x,y,w,h};return 0;}
static void CaptureLabel(const unsigned char* text){
    if(!text||statusLabelCount>=32)return;
    constexpr char alphabet[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    auto& label=statusLabels[statusLabelCount++];unsigned i=0;
    for(;i<sizeof(label)-1&&text[i];++i)label[i]=text[i]>=0x30&&unsigned(text[i]-0x30)<sizeof(alphabet)-1?alphabet[text[i]-0x30]:'?';
    label[i]=0;
}
static int __cdecl AbilityTextCapture(const unsigned char* text,float,float y,unsigned style,float sx,float sy){
    if(text==abilityName){if(abilityTextCount<32)abilityTextY[abilityTextCount++]=y;}
    else {++nativeFontCalls;nativeFontStyle&=!NativeUi::TextDrawingActive()&&style==0&&sy>0&&sy<=1.f&&std::fabs(sx-.78f*sy)<.001f;CaptureLabel(text);}
    return 0;
}
static int __cdecl MeasureNative(const unsigned char* text,float* width,unsigned,float sx,float){
    const float scale=reinterpret_cast<float(__cdecl*)(float)>(fixtureBase+(::FfxHooks::ExecutableProfile::Rva<0x244990>()))(1.f);
    *width=float(std::strlen(reinterpret_cast<const char*>(text)))*15.f*sx*scale;return 0;
}
static void __cdecl NativeCaption(unsigned id,float x,float y,float w,float h,unsigned alpha){++nativeCaptionCalls;nativeCaptionId=id;nativeCaptionRect={x,y,w,h};nativeFontStyle&=alpha==128;}
static int __cdecl SoundBackend(int channel,int id,int pan,int volume){++soundCount;lastSound=static_cast<unsigned>(id);soundArguments&=channel==0&&pan==63&&volume==127;return 0;}
static int __cdecl StatusAnimation(int,int){return slideValue;}
static int __cdecl StatGrid(int,int,unsigned,int){++gridCalls;return 0;}
static const unsigned char* __cdecl Gear(unsigned,const unsigned char** text){if(text)*text=nullptr;return gear;}
static const unsigned char* __cdecl Definition(unsigned,const unsigned char** text){if(text)*text=abilityName;return definition;}
static const void* __cdecl UnusedKernel(unsigned,int* count){if(count)*count=0;return nullptr;}
static int __cdecl EmptyStatusList(int,int,int,int,unsigned* output){std::memset(output,0,8*sizeof(unsigned));return 8;}
static void ResetRows(){abilityBoxCount=abilityIconCount=abilityTextCount=statusLabelCount=nativeFontCalls=nativeCaptionCalls=gridCalls=0;nativeFontStyle=true;}
static bool HasStatus(const char* label){for(unsigned i=0;i<statusLabelCount;++i)if(!std::strcmp(statusLabels[i],label))return true;return false;}
static DWORD WINAPI ForeignStatus(void* object){reinterpret_cast<int(__cdecl*)(void*)>(fixtureBase+(::FfxHooks::ExecutableProfile::Rva<0x4D2760>()))(object);return 0;}
static void __cdecl CaptionCapture(int id,float,float y,float,float){if(id==6||id==7){captionYs[id-6]=y;++captions;}}
static int __cdecl TextCapture(int,const unsigned char* text,float,float,char,float,float){
    const unsigned char tarot[]={0x63,0x70,0x81,0x7E,0x83};
    const unsigned char locked[]={0x5B,0x7E,0x72,0x7A,0x74,0x73};
    const unsigned char blocked[]={0x63,0x77,0x78,0x82,0x3A,0x72};
    if(text&&!std::memcmp(text,tarot,sizeof(tarot)))++tarotLabels;
    if(text&&!std::memcmp(text,locked,sizeof(locked)))++lockedLabels;
    if(text&&!std::memcmp(text,blocked,sizeof(blocked)))++blockedMessages;
    if(NativeUi::TextDrawingActive())++scopedTextDraws;
    CaptureLabel(text);
    return 0;
}
static int __cdecl EquipNative(unsigned,unsigned,unsigned){++nativeEquipCalls;return 1;}
static bool Patch(std::uintptr_t base,unsigned rva,void* function){
    auto* at=reinterpret_cast<unsigned char*>(base+rva);DWORD before=0,ignored=0;
    if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&before))return false;
    at[0]=0xE9;const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(function)-reinterpret_cast<std::uintptr_t>(at)-5);
    std::memcpy(at+1,&displacement,4);FlushInstructionCache(GetCurrentProcess(),at,5);
    return VirtualProtect(at,5,before,&ignored)!=FALSE;
}
int main(int argc,char** argv){
    captureThread=GetCurrentThreadId();
    std::setvbuf(stdout,nullptr,_IONBF,0);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* error)->LONG {
        if(error->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
            std::printf("FIXTURE AV eip=%08lX rva=%08lX address=%p\n",error->ContextRecord->Eip,static_cast<unsigned long>(error->ContextRecord->Eip-fixtureBase),reinterpret_cast<void*>(error->ExceptionRecord->ExceptionInformation[1]));
        return EXCEPTION_CONTINUE_SEARCH;
    });
    if(argc!=2&&argc!=3){std::puts("Usage: ArcanaNativeUiRt1 private-FFX.exe [workshop]");return 2;}
    const bool workshop=argc==3;
    HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!image||!PrivatePeFixture::NormalizeRelocations(image)){std::puts("FAIL private fixture mapping");return 2;}
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    fixtureBase=base;
    NativeUi::Callbacks callbacks{Capture,EquipCallback,ModeCallback,ImagesCallback};
    if(workshop)Check(FfxHooks::EquipmentWorkshop::NativeUi::Start(base,true,false,Log)&&FfxHooks::EquipmentWorkshop::NativeUi::StatusBridgeInstalled(base),"fixture starts the actual shared Workshop Status bridge first");
    unsigned char original[32]{};std::memcpy(original,reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x4ceff0>()),32);
    Check(!NativeUi::Start(base,false,false,callbacks,Log)&&!NativeUi::Active(),"OFF does not install the Equip extension");
    Check(!NativeUi::Start(base,true,true,callbacks,Log)&&!std::memcmp(original,reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x4ceff0>()),32),"validate-only preserves native controller bytes");
    const bool started=NativeUi::Start(base,true,false,callbacks,Log);
    Check(started&&NativeUi::Active(),"supported profile installs native Equip init/control/draw integration");
    if(!started){std::printf("ArcanaNativeUiRt1 %u/%u passed\n",checks-failures,checks);return 1;}
    // Only external dependencies are substituted in this private mapped image.
    // The real Equip init/control/draw bodies and our detours execute below.
    for(unsigned rva:{(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4a9820u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4a9870u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4a9920u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4cfcf0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4c2bd0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4c2bf0u>())>())>())>()),
                      (::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x486de0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4f5c10u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4e71d0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4dce30u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4c1770u>())>())>())>()),
                      (::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4c0bb0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4d4eb0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4f5f70u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4f8bb0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4c0c90u>())>())>())>()),
                      (::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x5016b0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4c0640u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4f4b20u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4c0550u>())>())>())>())})Check(Patch(base,rva,reinterpret_cast<void*>(Nothing)),"isolate native dependency");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4a9810>()),reinterpret_cast<void*>(Actor))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4a9c20>()),reinterpret_cast<void*>(Weapon))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4a97d0>()),reinterpret_cast<void*>(Armor)),"provide a valid permanent-party actor and native equipment");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4be440>()),reinterpret_cast<void*>(ReadDir))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4be480>()),reinterpret_cast<void*>(ReadEdge))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4be3e0>()),reinterpret_cast<void*>(ReadHeld)),"provide native edge/repeat input");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x3ab990>()),reinterpret_cast<void*>(EquipNative)),"observe calls to the real equipment setter boundary");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4d5470>()),reinterpret_cast<void*>(CursorX))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x5016b0>()),reinterpret_cast<void*>(TextCapture)),"isolate native font measurement and observe encoded draw calls");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4a9820>()),reinterpret_cast<void*>(Actor))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4a97f0>()),reinterpret_cast<void*>(PortraitDirection)),"supply party selection while retaining the real portrait transition routine");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4a9ae0>()),reinterpret_cast<void*>(OnePartyMember)),"bound the native party-count dependency for delegated controls");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4f5f70>()),reinterpret_cast<void*>(WindowCapture))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4f8bb0>()),reinterpret_cast<void*>(CaptionCapture)),"observe the native Equipment and Abilities header geometry");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4C0640>()),reinterpret_cast<void*>(CursorCapture))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4F4B20>()),reinterpret_cast<void*>(QuadCapture)),"observe the native cursor and opaque Tarot paint order");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x41E5F0>()),reinterpret_cast<void*>(SoundBackend)),"observe the audio backend while retaining the real native menu sound dispatcher");
    *reinterpret_cast<unsigned*>(base+::FfxHooks::ExecutableProfile::Rva<0xF3D6A0>())=0;
    AwardAll(state,0);
    std::puts("Native UI fixture: dependencies isolated; entering real Equip init");
    alignas(4) unsigned char context[96]{};
    using Control=int(__cdecl*)(void*);using Draw=int(__cdecl*)();
    reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf800>())(context);
    std::puts("Native UI fixture: real init returned; exercising root/picker");
    *reinterpret_cast<unsigned*>(context+0x1c)=10;
    *reinterpret_cast<unsigned*>(base+::FfxHooks::ExecutableProfile::Rva<0x146a5d4>())=0x1000;
    auto step=[&](unsigned d,unsigned e){dir=d;edge=e;reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4ceff0>())(context);dir=edge=0;};
    soundCount=0;step(0,0);Check(soundCount==0,"idle private controls emit no menu sound");
    step(0x4000,0);Check(soundCount==1&&lastSound==1,"a consumed root movement reaches the native move sound once");
    step(0x4000,0);step(0,0x20);step(0x4000,0);step(0x4000,0);step(0,0x20);
    Check(soundCount==6&&lastSound==1&&soundArguments,"picker navigation and successful equipment have one cue per input through the original sound wrapper");
    Check(state.slots[0][0]==0&&nativeEquipCalls==0,"Tarot selection equips the real Arcana state without calling native weapon setter");
    const auto beforeCancel=soundCount;step(0,0x20);step(0,0x40);
    Check(soundCount==beforeCancel+2&&lastSound==4,"opening and backing out of the picker produces confirm then native cancel without duplicate cues");
    Check(*reinterpret_cast<unsigned*>(base+::FfxHooks::ExecutableProfile::Rva<0x146a5e4>())<=1,"private Tarot category never escapes into native Weapon/Armor index");
    *reinterpret_cast<unsigned*>(base+::FfxHooks::ExecutableProfile::Rva<0x146a5f0>())=0xFF;
    std::puts("Native UI fixture: exercising real Equip draw");
    const float scaleY=reinterpret_cast<float(__cdecl*)(float)>(base+::FfxHooks::ExecutableProfile::Rva<0x2449d0>())(1.f);
    windows=captions=0;drawSequence=cursorSequence=lastTarotBackground=cursorCount=0;
    reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
    Check(cursorCount==1&&lastTarotBackground!=0&&cursorSequence>lastTarotBackground&&std::fabs(cursorY-492.f*scaleY)<.01f,"selected Tarot draws its single native arrow after the opaque row backgrounds");
    Check(scaleY>0&&windows>=2&&std::fabs(windowYs[0]-640.f*scaleY)<.01f&&std::fabs(windowYs[1]-640.f*scaleY)<.01f,"both actual native header panels clear the two Tarot rows");
    Check(captions==2&&std::fabs(captionYs[0]-652.f*scaleY)<.01f&&std::fabs(captionYs[1]-652.f*scaleY)<.01f,"both actual native header captions follow their panels");
    Check(scopedTextDraws>0&&!NativeUi::TextDrawingActive(),"Arcana owns glyph-outline suppression only during its native draw callback");
    Check(lastImages.count>0&&lastImages.generation!=0,"native Equip draw publishes only its contextual image requests");
    Check(tarotLabels>=2,"native text primitive receives the two new Tarot row labels");
    step(0,0x20);actor=1;step(0,0);step(0,0x20);step(0x4000,0);step(0x4000,0);
    reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();const auto pickerImageGeneration=lastImages.generation;
    step(0,0x20);reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
    Check(state.slots[0][0]==0&&state.slots[1][0]==kEmpty,"cross-actor transfer waits for its confirmation");
    Check(lastImages.count==0,"transfer confirmation hides the picker image plan");
    step(0,0x20);reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
    Check(state.slots[0][0]==kEmpty&&state.slots[1][0]==0,"confirmed native picker transfer preserves one global owner");
    Check(lastImages.count>0&&lastImages.generation>pickerImageGeneration,"closing a modal publishes a fresh visible image generation");
    auto* transition=reinterpret_cast<int*>(base+::FfxHooks::ExecutableProfile::Rva<0x1FCC3C8>());
    auto* portraitFrom=reinterpret_cast<int*>(base+::FfxHooks::ExecutableProfile::Rva<0x1FCC3C4>());
    auto* portraitTo=reinterpret_cast<int*>(base+::FfxHooks::ExecutableProfile::Rva<0x1FCC3C0>());
    auto* previousActor=reinterpret_cast<int*>(base+::FfxHooks::ExecutableProfile::Rva<0x1FCC3BC>());
    *transition=0;*portraitFrom=*portraitTo=*previousActor=0;actor=2;
    step(0,0);Check(*transition==4096&&*portraitTo==2,"idle Tarot frame starts the actual portrait transition after an actor change");
    step(0,0);Check(*transition==3687,"portrait transition advances exactly once per consumed idle frame");
    for(unsigned frame=0;frame<10;++frame)step(0,0);
    Check(*transition==0&&*portraitFrom==2&&*portraitTo==2&&*previousActor==2,"portrait finishes updating without a held button or reopening Equip");
    std::puts("Native UI fixture: delegated portrait tick");*transition=4096;step(4,0);
    Check(*transition==3687,"delegated native controller ticks the portrait once without a second Arcana tick");
    std::puts("Native UI fixture: reverse portrait tick");*transition=0;actor=0;portraitDirection=-1;step(0,0);
    Check(*transition==-4096,"reverse actor transition starts through the native direction rule");
    for(unsigned frame=0;frame<11;++frame)step(0,0);
    Check(*transition==0&&*portraitFrom==0&&*portraitTo==0,"reverse portrait transition also completes during idle Tarot frames");
    std::puts("Native UI fixture: Constellation layout");state.mode=Mode::Constellation;++state.revision;windows=captions=0;
    // The delegated vanilla control refreshed its real gear-preview identity.
    // This fixture still substitutes the unavailable native equipment table.
    *reinterpret_cast<unsigned*>(base+::FfxHooks::ExecutableProfile::Rva<0x146a5f0>())=0xFF;
    reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
    Check(windows>=2&&std::fabs(windowYs[0]-700.f*scaleY)<.01f&&std::fabs(windowYs[1]-700.f*scaleY)<.01f&&
          captions==2&&std::fabs(captionYs[0]-712.f*scaleY)<.01f&&std::fabs(captionYs[1]-712.f*scaleY)<.01f,"both native headers clear all three Constellation slots");
    Equip(state,state.revision,0,0,0,true);Equip(state,state.revision,0,1,1,true);
    step(0x4000,0);step(0x4000,0);lockedLabels=0;
    reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
    Check(lockedLabels>0,"native Equip visibly marks the empty third slot locked for two Majors");
    const auto lockedRevision=state.revision;blockedMessages=0;const auto beforeBlockedSound=soundCount;step(0,0x20);
    reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
    Check(blockedMessages>0&&lastImages.count==0&&state.revision==lockedRevision&&state.slots[0][2]==kEmpty,
          "native confirm opens the capacity explanation instead of the picker or an equip transaction");
    Check(soundCount==beforeBlockedSound+1&&lastSound==3,"locked confirmation dispatches one native error sound and Draw does not replay it");
    step(0,0x20);reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
    Check(lastImages.count>0&&state.revision==lockedRevision,"acknowledging the lock returns to Equip and restores its image plan");
    Check(lastSound==1&&soundCount==beforeBlockedSound+2,"acknowledging the lock has one confirmation sound");
    std::puts("Native UI fixture: real equipment row geometry");
    gear[11]=4;const unsigned short words[]={0x8000,0x8001,0x8002,0x8003};std::memcpy(gear+14,words,sizeof(words));
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x3ABBF0>()),reinterpret_cast<void*>(Gear))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x398EC0>()),reinterpret_cast<void*>(Nothing))&&
          Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x3909C0>()),reinterpret_cast<void*>(Definition)),"provide bounded gear and ability data while retaining the original row renderer");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4E6AF0>()),reinterpret_cast<void*>(IconCapture))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x505AB0>()),reinterpret_cast<void*>(AbilityTextCapture)),"observe the actual native row icon and text primitives");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x505290>()),reinterpret_cast<void*>(MeasureNative))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4F8D50>()),reinterpret_cast<void*>(NativeCaption)),"observe native font metrics and the original caption-atlas consumer");
    *reinterpret_cast<unsigned*>(base+::FfxHooks::ExecutableProfile::Rva<0x146a5f0>())=0x5000;
    for(auto mode:{Mode::Twin,Mode::Constellation}){
        state.mode=mode;++state.revision;ResetRows();reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4cf640>())();
        bool fit=abilityBoxCount==4&&abilityIconCount==4&&abilityTextCount==4;
        const float scaleX=reinterpret_cast<float(__cdecl*)(float)>(base+::FfxHooks::ExecutableProfile::Rva<0x244990>())(1.f);
        for(unsigned i=0;i<4&&fit;++i){
            const auto& box=abilityBoxes[i];const auto& icon=abilityIcons[i];
            fit&=icon.y>=box.y&&icon.y+icon.h<=box.y+box.h+.01f&&abilityTextY[i]>=box.y&&abilityTextY[i]+32.f*scaleY<=box.y+box.h+.01f;
            fit&=std::fabs(icon.w-(mode==Mode::Twin?38.f:32.f)*scaleX)<.01f;
            if(i)fit&=box.y>abilityBoxes[i-1].y+abilityBoxes[i-1].h;
        }
        Check(fit,"native equipment backgrounds, icons and text insets fit together in each deck mode");
    }
    std::puts("Native UI fixture: Status Auto-Abilities");
    for(unsigned rva:{(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4D3090u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4F9280u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4BF0F0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4BF0D0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4C2310u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4C2C10u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4C22E0u>())>())>())>()),(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<(::FfxHooks::ExecutableProfile::Rva<0x4C0CA0u>())>())>())>())})
        Check(Patch(base,rva,reinterpret_cast<void*>(Nothing)),"isolate Status device, header and animation dependencies");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x390250>()),reinterpret_cast<void*>(UnusedKernel)),"supply unused command metadata while retaining the real Auto-Abilities builder");
    unsigned char statusObject[152]{};*reinterpret_cast<unsigned*>(statusObject+0x68)=0xAA;state={};AwardAll(state,0);actor=0;
    Equip(state,state.revision,0,0,6);Equip(state,state.revision,0,1,13);
    const auto beforeStatus=state;
    ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);
    Check(abilityTextCount==8&&HasStatus("Arcana")&&HasStatus("Deathstrike 100%")&&HasStatus("Deathproof"),"actual Status draw retains equipment rows and adds equipped Arcana abilities");
    Check(nativeFontCalls==Ui::BuildStatusRows(state,0).count+1&&nativeFontStyle,"Arcana uses the normal ability font, its native outline and coherent proportional sizing");
    const float statusScaleX=reinterpret_cast<float(__cdecl*)(float)>(base+::FfxHooks::ExecutableProfile::Rva<0x244990>())(1.f);
    Check(nativeCaptionCalls==1&&nativeCaptionId==0xAA&&std::fabs(nativeCaptionRect.w-430.f*statusScaleX)<.01f&&std::fabs(nativeCaptionRect.h-36.f*scaleY)<.01f,"Arcana reuses the current Auto-Abilities atlas title at native geometry");
    Check(HasStatus("Share healing 25% (cap 10% HP/action)")&&HasStatus("Damage vs Death immunity +20%"),"Status displays conditional Arcana effects with their parameters");
    Check(state.slots==beforeStatus.slots&&state.revision==beforeStatus.revision&&!NativeUi::TextDrawingActive(),"Status drawing does not mutate loadouts or leak text-rendering scope");
    actor=1;ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);
    Check(statusLabelCount==0&&abilityTextCount==8,"changing Status character immediately removes the previous actor's Arcana list");
    actor=0;captureReady=false;ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);
    Check(statusLabelCount==0,"an unbound save snapshot does not expose stale Arcana abilities");captureReady=true;
    ResetRows();auto foreign=CreateThread(nullptr,0,ForeignStatus,statusObject,0,nullptr);
    if(foreign){WaitForSingleObject(foreign,INFINITE);CloseHandle(foreign);}
    Check(foreign&&statusLabelCount==0,"foreign-thread Status calls cannot acquire the save-owned Arcana snapshot");
    // Other native pages require unrelated command tables. Keep their renderer
    // executing while supplying an empty query only for these negative gates.
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4D2DE0>()),reinterpret_cast<void*>(EmptyStatusList)),"bound unrelated Status-page list dependencies");
    for(unsigned offset:{0x58u,0x60u,0x64u}){
        *reinterpret_cast<unsigned*>(statusObject+offset)=1;ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);
        Check(statusLabelCount==0,"other Status pages and Aeons retain their native display");*reinterpret_cast<unsigned*>(statusObject+offset)=0;
    }
    if(workshop){FfxHooks::EquipmentWorkshop::NativeUi::Stop();ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);
        Check(HasStatus("Deathproof"),"Arcana remains visible through the shared bridge after Workshop presentation stops");}
    std::puts("Native UI fixture: Status overview equipped slots");
    Check(Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4D5150>()),reinterpret_cast<void*>(StatGrid))&&
          Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4D4140>()),reinterpret_cast<void*>(Nothing))&&Patch(base,(::FfxHooks::ExecutableProfile::Rva<0x4D3090>()),reinterpret_cast<void*>(StatusAnimation)),"isolate unchanged overview stats/footer and supply the shared animation offset");
    reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4D4EA0>())();
    state.mode=Mode::Constellation;ResetRows();const auto beforeOverview=state;
    reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())(statusObject);
    Check(gridCalls==1&&abilityBoxCount==3&&HasStatus("Tarot I: The Lovers")&&HasStatus("Tarot II: Death")&&HasStatus("Tarot III: Locked"),"original Status overview appends all three current slots below Armor");
    bool inGap=nativeFontStyle;for(unsigned i=0;i<abilityBoxCount;++i)inGap&=std::fabs(abilityBoxes[i].y-490.f*scaleY)<.01f&&abilityBoxes[i].y+abilityBoxes[i].h<552.f*scaleY;
    Check(inGap&&state.revision==beforeOverview.revision&&state.slots==beforeOverview.slots,"overview slots fit before the original stats and do not mutate equipment");
    const auto setOverview=reinterpret_cast<int(__cdecl*)(int)>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2630>());
    const auto advanceOverview=reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4D3250>());
    setOverview(341);ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())(statusObject);
    Check(statusLabelCount==0&&abilityBoxCount==0,"starting Display Abilities immediately retires the overview Tarot row");
    for(unsigned frame=0;frame<13;++frame)advanceOverview();
    Check(reinterpret_cast<Draw>(base+::FfxHooks::ExecutableProfile::Rva<0x4D48E0>())()==4096,"the actual native controller transition reaches the fully hidden overview");
    ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())(statusObject);
    Check(statusLabelCount==0,"a cached overview draw cannot leak Tarot labels over the abilities page");
    ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);
    Check(HasStatus("Deathproof")&&!HasStatus("Tarot I: The Lovers"),"only the active Auto-Abilities Arcana content remains visible");
    *reinterpret_cast<void**>(base+::FfxHooks::ExecutableProfile::Rva<0x146A7E0>())=statusObject;
    reinterpret_cast<int(__cdecl*)(int)>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2650>())(0);
    ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);
    Check(statusLabelCount==0,"the native retiring-page flag immediately hides Arcana effects when advancing past Auto-Abilities");
    statusObject[0x41]=0;*reinterpret_cast<short*>(statusObject+0x54)=0;
    setOverview(-341);for(unsigned frame=0;frame<13;++frame)advanceOverview();
    ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())(statusObject);
    Check(HasStatus("Tarot I: The Lovers"),"returning through the real native transition restores the current overview slots");
    const float firstX=abilityBoxes[0].x;slideValue=2048;ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())(statusObject);
    Check(std::fabs(abilityBoxes[0].x-firstX-170.f)<.01f,"overview slots follow the same integer animation offset as native equipment");slideValue=0;
    state.mode=Mode::Twin;actor=1;ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())(statusObject);
    Check(abilityBoxCount==2&&HasStatus("Tarot I: Empty")&&!HasStatus("Tarot I: The Lovers"),"overview immediately follows character changes and the two-slot mode");
    NativeUi::Stop();
    Check(!NativeUi::Active()&&lastImages.count==0,"logical stop closes input and preview admission without removing entered trampolines");
    ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D2760>())(statusObject);Check(statusLabelCount==0,"logical stop removes Arcana Status drawing");
    ResetRows();reinterpret_cast<Control>(base+::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())(statusObject);Check(statusLabelCount==0&&abilityBoxCount==0&&gridCalls==1,"logical stop retains the native overview without Arcana additions");
    std::printf("ArcanaNativeUiRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
