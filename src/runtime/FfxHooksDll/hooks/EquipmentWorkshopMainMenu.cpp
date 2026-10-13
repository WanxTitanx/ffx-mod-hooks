#include "EquipmentWorkshopMainMenu.h"
#include "EquipmentWorkshopMainMenuEvidence.h"
#include "NativeUiHookSupport.h"
#include "../shared/ExecutableProfile.h"
#include <atomic>

namespace FfxHooks::EquipmentWorkshop::MainMenu {
namespace {
// The descriptor ABI is independently verified against Register/Activate/
// Suspend/Remove. In particular +8 is registration, not activation.
struct Descriptor {
    void (__cdecl *control)(Descriptor*);
    void (__cdecl *draw)(Descriptor*);
    void (__cdecl *registered)(Descriptor*,unsigned);
    void (__cdecl *activate)(Descriptor*,unsigned);
    void (__cdecl *suspend)(Descriptor*);
    void (__cdecl *remove)(Descriptor*);
    unsigned char controlActive,drawActive,extraControl,extraDraw;
    unsigned state;
};
static_assert(sizeof(Descriptor)==32&&offsetof(Descriptor,state)==28,"native x86 submenu ABI");
enum Hook {Build,Count,Id,Enabled,Kind,Select,Name,Help,Remove,Row,HookCount};
constexpr std::uint32_t rvas[HookCount]={0x4E1C60,0x4E2280,0x4E2270,0x4E2290,0x4E2260,0x4E24C0,0x4DDF80,0x4DDF60,0x4E19D0,0x4E22A0};
constexpr unsigned slot=24,option=0x7FFF,rootRva=0x85B2B0,registryRva=0x1440888;
using NoArg=int(__cdecl*)();
using OneArg=int(__cdecl*)(int);
using TwoArgs=int(__cdecl*)(int,int);
using Label=const unsigned char*(__cdecl*)(int);
std::uintptr_t image=0;
void* originals[HookCount]{};
Ui ui{};
void(*logger)(const char*)=nullptr;
std::atomic<bool> running{false},attempted{false},attached{false};
static_assert(std::atomic<bool>::is_always_lock_free,"detach closes admission without locks");
std::atomic<DWORD> owner{0};
unsigned nativeCount=0;
bool opened=false;
unsigned char title[192]{},help[384]{};
void __cdecl Control(Descriptor*);
void __cdecl Draw(Descriptor*);
void __cdecl Registered(Descriptor*,unsigned);
void __cdecl Activate(Descriptor*,unsigned);
void __cdecl Suspend(Descriptor*);
void __cdecl Removed(Descriptor*);
Descriptor descriptor{Control,Draw,Registered,Activate,Suspend,Removed,0,0,0,0,0};
template<class T> T Read(unsigned rva) noexcept {
    T value{};if(image)NativeUiSupport::Copy(&value,reinterpret_cast<void*>(image+rva),sizeof(value));return value;
}
bool Owner() noexcept {return owner.load()==GetCurrentThreadId();}
bool Root() noexcept {return Read<std::uintptr_t>(registryRva+4)==image+rootRva;}
bool OwnSlot() noexcept {return Read<Descriptor*>(registryRva+4*slot)==&descriptor;}
bool View() noexcept {
    return attached.load()&&Owner()&&Root()&&OwnSlot()&&Read<unsigned>(0x1471688)==nativeCount;
}
bool Extra(int index) noexcept {return View()&&index==static_cast<int>(nativeCount);}
void NativeSuspend(unsigned id){reinterpret_cast<void(__cdecl*)(unsigned)>(image+0x4AAD70)(id);}
void NativeActivate(unsigned id){reinterpret_cast<void(__cdecl*)(unsigned,int)>(image+0x4AA100)(id,0);}
bool Cleanup(bool resume) noexcept {
    if(!attached.load())return true;
    if(!Owner())return false;
    if(opened&&!ui.close())return false;
    opened=false;
    if(OwnSlot()){
        NativeSuspend(slot);
        reinterpret_cast<void(__cdecl*)(unsigned)>(image+0x4AAB20)(slot);
    }
    // The native selection belongs to this root. Its virtual last row must not
    // survive logical disable; other options and native records are untouched.
    if(Root()&&nativeCount&&Read<unsigned>(0x1471548)>=nativeCount){
        const unsigned selection=nativeCount-1;
        NativeUiSupport::Copy(reinterpret_cast<void*>(image+0x1471548),&selection,sizeof(selection));
    }
    attached=false;
    if(resume&&Root()&&Read<unsigned>(0xF40824)&&!(Read<unsigned>(0x14408F8)&0x800000))NativeActivate(1);
    return true;
}
void __cdecl Registered(Descriptor* d,unsigned){d->state=0;opened=false;}
void __cdecl Activate(Descriptor* d,unsigned){d->state=1;opened=false;}
void __cdecl Suspend(Descriptor* d){d->state=0;}
void __cdecl Removed(Descriptor* d){
    if(Owner()&&opened)ui.close();opened=false;d->state=0;
}
void ReturnToRoot(){
    if(!OwnSlot())return;
    NativeSuspend(slot);opened=false;
    if(Root()&&Read<unsigned>(0xF40824))NativeActivate(1);
}
void __cdecl Control(Descriptor* d){
    // Activate invokes this once *before* publishing the active flags. Opening
    // or returning then would race the parent's own native suspend transition.
    if(d!=&descriptor||!Owner()||!d->controlActive)return;
    if(!running.load()){Cleanup(true);return;}
    if(!View()){Cleanup(false);return;}
    if(d->state==1){
        if(Read<int>(0x1471558)>0)return;
        if(!ui.ready()||!ui.open()){ReturnToRoot();return;}
        opened=true;d->state=2;
        if(logger)logger("[ffx-hooks] Workshop main menu: native submenu opened\n");
    }else if(d->state==2&&!ui.busy())ReturnToRoot();
}
void __cdecl Draw(Descriptor* d){
    if(d==&descriptor&&Owner()&&View()&&d->drawActive)
        reinterpret_cast<void(__cdecl*)()>(image+0x4F5C10)();
}
bool StandardRoot(){
    if(!Root()||Read<unsigned>(0x14408F8)&0x800000)return false;
    const unsigned count=Read<unsigned>(0x1471688);
    if(count<6||count>11)return false;
    unsigned seen=0;
    for(unsigned i=0;i<count;++i){
        const unsigned id=Read<unsigned>(0x1471568+i*8),flags=Read<unsigned>(0x147156C+i*8);
        if(id>=32||!((1u<<id)&0x83FF)||(seen&(1u<<id))||(flags&0xFFFF)!=1)return false;
        const unsigned kind=id==1?1u:(id==2||id==3||id==4||id==15)?2u:0u;
        if((flags>>16)!=kind)return false;
        seen|=1u<<id;
    }
    return (seen&0x8313)==0x8313;
}
int __cdecl BuildShim(int initial,int mode){
    if(attached.load()&&Owner())Cleanup(false);
    const int result=reinterpret_cast<TwoArgs>(originals[Build])(initial,mode);
    if(!running.load()||!ui.ready()||!StandardRoot()||Read<std::uintptr_t>(registryRva+4*slot))return result;
    owner=GetCurrentThreadId();nativeCount=Read<unsigned>(0x1471688);
    ui.label(false,title,sizeof(title));ui.label(true,help,sizeof(help));
    if(!title[0]||!help[0])return result;
    reinterpret_cast<void(__cdecl*)(unsigned,Descriptor*)>(image+0x4AAAB0)(slot,&descriptor);
    attached=OwnSlot();return result;
}
int __cdecl CountShim(){return reinterpret_cast<NoArg>(originals[Count])()+(View()?1:0);}
int __cdecl IdShim(int index){return Extra(index)?option:reinterpret_cast<OneArg>(originals[Id])(index);}
int __cdecl EnabledShim(int index){return Extra(index)?(running.load()&&ui.ready()?1:0):reinterpret_cast<OneArg>(originals[Enabled])(index);}
int __cdecl KindShim(int index){return Extra(index)?0:reinterpret_cast<OneArg>(originals[Kind])(index);}
// These native leaves only change EAX. The game's optimized caller keeps its
// selected index in ECX across Kind, and its next index across Count. A normal
// C++ detour would legally clobber them and dispatch the wrong native option.
__declspec(naked) int __cdecl CountEntry(){__asm {
    pushfd
    push ecx
    push edx
    call CountShim
    pop edx
    pop ecx
    popfd
    ret
}}
#define WORKSHOP_OPTION_ENTRY(name,body) \
__declspec(naked) int __cdecl name(int){__asm {pushfd} __asm {push ecx} __asm {push edx} \
    __asm {push dword ptr [esp+16]} __asm {call body} __asm {add esp,4} \
    __asm {pop edx} __asm {pop ecx} __asm {popfd} __asm {ret}}
WORKSHOP_OPTION_ENTRY(IdEntry,IdShim)
WORKSHOP_OPTION_ENTRY(EnabledEntry,EnabledShim)
WORKSHOP_OPTION_ENTRY(KindEntry,KindShim)
#undef WORKSHOP_OPTION_ENTRY
const unsigned char* __cdecl NameShim(int id){return id==option&&View()?title:reinterpret_cast<Label>(originals[Name])(id);}
const unsigned char* __cdecl HelpShim(int id){return id==option&&View()?help:reinterpret_cast<Label>(originals[Help])(id);}
int __cdecl SelectShim(int index){
    if(!Extra(index))return reinterpret_cast<OneArg>(originals[Select])(index);
    if(running.load()&&ui.ready()){NativeActivate(slot);NativeSuspend(1);}
    return option;
}
int __cdecl RemoveShim(){if(attached.load()&&Owner())Cleanup(false);return reinterpret_cast<NoArg>(originals[Remove])();}
int __cdecl RowShim(int index,float horizontal,int a3,int a4,int fade){
    using Function=int(__cdecl*)(int,float,int,int,int);
    if(!Extra(index))return reinterpret_cast<Function>(originals[Row])(index,horizontal,a3,a4,fade);
    // Native rows select their atlas strip by index. The appended row has no
    // authored strip; reuse the first stock button while retaining its own
    // position, text, fade and native cursor. No atlas/resource is rewritten.
    using Scale=float(__cdecl*)(float);
    const auto xScale=reinterpret_cast<Scale>(image+0x2447E0),yScale=reinterpret_cast<Scale>(image+0x244820);
    const int level=fade<0?0:fade>4096?4096:fade;
    const float x=xScale(1304.f)+horizontal;
    const float y=((yScale(194.f)+yScale(75.f)*index)*level+3.f*(4096-level))/4096.f;
    const float width=xScale(476.f),height=yScale(60.f);
    const unsigned color=0x00808080u|(static_cast<unsigned>(level/32)<<24);
    using Quad=void(__cdecl*)(unsigned,float,float,float,float,float,float,float,float,unsigned,unsigned);
    reinterpret_cast<Quad>(image+0x503EE0)(0xFFFFFFFDu,x,y,width,height,1118.f,0.f,1599.f,67.f,color,color);
    const int disabled=EnabledShim(index)==0;float textWidth=0;
    using Measure=int(__cdecl*)(const unsigned char*,float*,int,float);
    reinterpret_cast<Measure>(image+0x505290)(title,&textWidth,disabled,.78f);
    using Text=int(__cdecl*)(const unsigned char*,float,float,int,float,float,unsigned char);
    const int result=reinterpret_cast<Text>(image+0x505970)(title,x+(width-textWidth)*.5f,y+yScale(7.f),disabled,.78f,1.f,static_cast<unsigned char>(level/32));
    if(Read<unsigned>(0x1471548)==static_cast<unsigned>(index)&&level==4096)
        return reinterpret_cast<int(__cdecl*)(float,float,int)>(image+0x4C0670)(x+xScale(115.f),y+yScale(10.f),0);
    return result;
}
}
bool Start(std::uintptr_t base,bool enabled,bool validateOnly,const Ui& adapter,void(*log)(const char*)){
    if(!ExecutableProfile::Steam20261001||!enabled||validateOnly||!adapter.ready||!adapter.open||!adapter.busy||!adapter.close||!adapter.label)return false;
    if(attempted.load())return running.load();
    if(!Evidence::Profile(base)){
        if(log)log("[ffx-hooks] Workshop main menu rejected: native signatures differ\n");return false;
    }
    attempted=true;image=base;ui=adapter;logger=log;
    void* replacements[HookCount]={reinterpret_cast<void*>(&BuildShim),reinterpret_cast<void*>(&CountEntry),reinterpret_cast<void*>(&IdEntry),reinterpret_cast<void*>(&EnabledEntry),reinterpret_cast<void*>(&KindEntry),reinterpret_cast<void*>(&SelectShim),reinterpret_cast<void*>(&NameShim),reinterpret_cast<void*>(&HelpShim),reinterpret_cast<void*>(&RemoveShim),reinterpret_cast<void*>(&RowShim)};
    if(!NativeUiSupport::Install(base,rvas,replacements,originals,MinHookBatch::Owner::EquipmentWorkshopMenu,reinterpret_cast<const void*>(&Start)))return false;
    running=true;
    if(log)log("[ffx-hooks] Workshop main menu: native submenu integration armed\n");return true;
}
void Pump() noexcept {
    if(attached.load()&&Owner()&&(!running.load()||!Root()||!OwnSlot()))Cleanup(Root()&&descriptor.controlActive);
}
void RequestStop() noexcept {running=false;}
bool StopReady() noexcept {RequestStop();if(!attached.load())return true;if(!Owner())return false;return Cleanup(descriptor.controlActive!=0);}
bool Active() noexcept {return running.load();}
bool DrawText(const unsigned char* text,float x,float y,float width,bool compact) noexcept {
    if(!running.load()||!View()||!text||width<=0)return false;
    const float nominal=compact?.52f:.78f;float measured=0;
    using Measure=int(__cdecl*)(const unsigned char*,float*,int,float);
    reinterpret_cast<Measure>(image+0x505290)(text,&measured,0,nominal);
    const float ratio=measured>width?width/measured:1.f;
    using Text=int(__cdecl*)(const unsigned char*,float,float,int,float,float,unsigned char);
    reinterpret_cast<Text>(image+0x505970)(text,x,y,0,nominal*ratio,(compact?.7f:1.f)*ratio,128);
    return true;
}
bool AbilityName(unsigned word,unsigned char* output,std::size_t capacity,std::size_t& length) noexcept {
    if(!running.load()||!View()||!output||capacity<2||word<0x8000||word>0x8FFF)return false;
    const auto bank=Read<std::uint32_t>(0xD2A944);
    const auto extended=ui.bankBytes?ui.bankBytes(2,bank):0;
    const auto size=extended?extended:Read<std::uint16_t>(0xD2A970);
    unsigned char header[20]{};if(bank<0x10000||size<20||size>0x25800||bank>UINT32_MAX-size||
        !NativeUiSupport::Copy(header,reinterpret_cast<void*>(bank),sizeof(header)))return false;
    auto u16=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
    const unsigned low=u16(header+8),high=u16(header+10),stride=u16(header+12),block=u16(header+14),id=word&0xFFF;
    if(header[0]!=1||header[1]||header[2]||header[3]||header[16]!=20||header[17]||header[18]||header[19]||
       high<low||high>4095||stride!=108||(high-low+1)*stride!=block||20u+block>=size||id<low||id>high)return false;
    unsigned char offsetBytes[2]{};if(!NativeUiSupport::Copy(offsetBytes,reinterpret_cast<void*>(bank+20+(id-low)*stride),2))return false;
    const unsigned at=20+block+u16(offsetBytes);if(at>=size)return false;
    for(std::size_t i=0;i<capacity&&i<size-at;++i){
        unsigned char byte=0;if(!NativeUiSupport::Copy(&byte,reinterpret_cast<void*>(bank+at+i),1))return false;
        if(!byte){output[i]=0;length=i;return i!=0;}
        if(byte<32||i+1==capacity)return false;output[i]=byte;
    }
    return false;
}
}
