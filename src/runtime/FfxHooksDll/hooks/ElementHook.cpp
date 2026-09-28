#include "ElementHook.h"
#include "ElementScanSettings.h"
#include "ElementScanSprites.h"
#include "ElementalScanView.h"
#include "NativeUiHookSupport.h"
#include "NativePresentationEvidence.h"
#include <atomic>
#include <intrin.h>
#include <cstdio>
#include <cstring>

namespace FfxHooks {
namespace {
enum ElementTarget {Info,Row,Texture,Panel,FullFrame,FullData,FullDescription,FullRow,Tinted,Rotated,Text,NumberRight,NumberLeft,Glyph,ElementTargetCount};
constexpr std::uint32_t rvas[ElementTargetCount]={0x4939A0,0x494AB0,0x503BB0,0x4F41B0,0x49BBF0,0x49BEE0,0x49C740,0x495A50,0x503EE0,0x502EF0,0x5016B0,0x5055C0,0x505550,0x502040};
void* originals[ElementTargetCount]{};
std::uintptr_t module=0;
std::atomic<bool> active{false},attempted{false};
// Immutable startup choices, published before active. Shared draw targets are
// installed once even when both independently controlled features are enabled.
bool extraElementsEnabled=false,expandedStatsEnabled=false,numericScanEnabled=false;
static_assert(std::atomic<bool>::is_always_lock_free,"detach admission must be lock-free");
std::atomic<unsigned> drawingThread{0};
enum class ScanSection {Sensor,Frame,Data,Description};
struct ResourceQuad {unsigned atlas=0;float x=0,y=0,w=0,h=0,u0=0,v0=0,u1=0,v1=0;bool present=false;};
struct ResourceNumber {float x=0,y=0;unsigned style=0;float sx=0,sy=0;bool present=false;};
struct ScanScope {int actor=0;unsigned depth=0;ElementScan::Settings settings{};
    ScanSection section=ScanSection::Sensor;bool full=false,expanded=false,extras=false;
    bool numeric=false,sensorPanel=false;
    ElementalScanView::Snapshot numerical{};
    float panelX=0,panelY=0,panelW=0;
    unsigned char stats[8]{};unsigned mp=0,maxMp=0;
    ResourceQuad hpBand{},hpLabel{};ResourceNumber hpNumbers[2]{};
    const void* hpSlash=nullptr;int slashX=0,slashY=0;};
thread_local ScanScope* scope=nullptr;
using InfoFn=int(__cdecl*)(int,int,int);
using RowFn=int(__cdecl*)(int,int,int,int);
using TextureFn=int(__cdecl*)(unsigned,float,float,float,float,float,float,float,float);
using PanelFn=int(__cdecl*)(float,float,float,float,int);
using TintFn=int(__cdecl*)(unsigned,float,float,float,float,float,float,float,float,unsigned,unsigned);
using MaskFn=unsigned char(__cdecl*)(int,int);
using ScaleFn=float(__cdecl*)(float);
float X(float value){return reinterpret_cast<ScaleFn>(module+0x244990)(value);}
float Y(float value){return reinterpret_cast<ScaleFn>(module+0x2449D0)(value);}
bool InScope(){return active.load()&&scope&&scope->depth<4&&scope->extras&&ElementScan::VisibleCount(scope->settings)!=0;}
void Extras(int actor,int category,float x,float y){
    if(!InScope()||(actor&0xFF)!=(scope->actor&0xFF)||category<0||category>3)return;
    // The original visibility/target guards have already reached a real row.
    const unsigned mask=reinterpret_cast<MaskFn>(module+0x4975C0)(actor,category);
    const auto plain=reinterpret_cast<TextureFn>(originals[Texture]);
    const auto tinted=reinterpret_cast<TintFn>(module+0x503EE0);
    for(const auto& orb:ElementScan::Orbs(mask,scope->settings)){
        if(!orb.bit)continue;
        const auto link=ElementScan::Connector;
        plain(0x1AF,x+X(orb.x-63.f+orb.size),y+Y(orb.y),X(63.f-orb.size),Y(orb.size),link.u0,link.v0,link.u1,link.v1);
        const auto uv=orb.active?ElementScan::SilverSphere:ElementScan::InactiveSphere;
        if(orb.active){
            const auto color=ElementScan::NativeColor(orb.rgb);
            // Unlike the plain wrapper, this ABI has two final packed colors.
            tinted(0x1AF,x+X(orb.x),y+Y(orb.y),X(orb.size),Y(orb.size),uv.u0,uv.v0,uv.u1,uv.v1,color,color);
        }else plain(0x1AF,x+X(orb.x),y+Y(orb.y),X(orb.size),Y(orb.size),uv.u0,uv.v0,uv.u1,uv.v1);
    }
}
int __cdecl RowShim(int actor,int category,int x,int y){
    const int result=reinterpret_cast<RowFn>(originals[Row])(actor,category,x,y);
    Extras(actor,category,static_cast<float>(x),static_cast<float>(y));return result;
}
#include "ElementalScanDraw.inl"
#include "ElementScanDetails.inl"
int __cdecl TextureShim(unsigned atlas,float x,float y,float w,float h,float u0,float v0,float u1,float v1){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    if(InScope()&&scope->section==ScanSection::Sensor&&atlas==~0u&&
       (caller==0x493E5E||caller==0x493F17||caller==0x493FD6||caller==0x49408B))w+=X(ElementScan::PanelWidthFor(scope->settings)-385.f);
    if(FullScope()&&scope->section==ScanSection::Data&&caller>=0x49BEE0&&caller<0x49C740){
        if(scope->expanded&&(caller==0x49C30C||caller==0x49C3E6))return 0;
        if(scope->numeric&&(caller==0x49C30C||caller==0x49C3E6))y-=Y(NumericalExtraHeight());
        x-=X(ExtraWidth()*.5f);
        if(caller==0x49C016||caller==0x49C561)w+=X(ExtraWidth());
        // Affinity rows use truncated integer coordinates; comparing them with
        // scaled Y(706) misclassifies the first row at non-native resolutions.
        if(caller==0x49C016||caller==0x49C13A)y-=Y(ExtraHeight());
        if(scope->expanded){
            const ResourceQuad quad{atlas,x,y,w,h,u0,v0,u1,v1,true};
            if(caller==0x49C016)scope->hpBand=quad;
            if(caller==0x49C13A)scope->hpLabel=quad;
        }
    }
    const int result=reinterpret_cast<TextureFn>(originals[Texture])(atlas,x,y,w,h,u0,v0,u1,v1);
    // Resist is inline, unlike Weak/Absorb/Null. Its label starts at row+(25,6).
    if(InScope()&&caller==0x494150&&atlas==0x1B2)Extras(scope->actor,3,x-X(25.f),y-Y(6.f));
    return result;
}
int __cdecl PanelShim(float x,float y,float width,float height,int style){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    if(FullScope()&&scope->section==ScanSection::Frame&&caller==0x49BEB2){x-=X(ExtraWidth()*.5f);y-=Y(ExtraHeight());width+=X(ExtraWidth());height+=Y(ExtraHeight());}
    if(InScope()&&caller==0x493B8D)width=X(ElementScan::PanelWidthFor(scope->settings));
    if(active.load()&&scope&&scope->section==ScanSection::Sensor&&scope->depth<4&&caller==0x493B8D){
        scope->sensorPanel=true;scope->panelX=x;scope->panelY=y;scope->panelW=width;
    }
    return reinterpret_cast<PanelFn>(originals[Panel])(x,y,width,height,style);
}
int __cdecl InfoShim(int actor,int a,int b){
    if(!active.load()||(!extraElementsEnabled&&!numericScanEnabled))return reinterpret_cast<InfoFn>(originals[Info])(actor,a,b);
    unsigned expected=0;const auto thread=GetCurrentThreadId();
    drawingThread.compare_exchange_strong(expected,thread);
    if(drawingThread.load()!=thread)return reinterpret_cast<InfoFn>(originals[Info])(actor,a,b);
    ScanScope current{};current.actor=actor;auto* previous=scope;current.depth=previous?previous->depth+1:0;
    const bool valid=ElementScan::ReadSettings(current.settings);
    current.extras=extraElementsEnabled&&valid;
    scope=(current.extras||numericScanEnabled)&&current.depth<4?&current:nullptr;int result=0;
    __try{result=reinterpret_cast<InfoFn>(originals[Info])(actor,a,b);if(scope==&current)DrawSensorNumerical(current);}
    __finally{scope=previous;}
    return result;
}
}
bool StartElementHook(std::uintptr_t base,bool extraElements,bool expandedStats,bool validateOnly,void(*log)(const char*)){
    const bool numeric=Config::GetBool("elemental.numeric_scan",false);
    if((!extraElements&&!expandedStats&&!numeric)||validateOnly)return false;
    if(attempted.load())return active.load();
    if(!NativeUiSupport::Profile(base,NativePresentationEvidence::scan)){
        if(log)log("[ffx-hooks] Element Scan rejected: executable or drawing signatures differ\n");return false;}
    attempted=true;module=base;
    void* replacements[ElementTargetCount]={reinterpret_cast<void*>(&InfoShim),reinterpret_cast<void*>(&RowShim),reinterpret_cast<void*>(&TextureShim),reinterpret_cast<void*>(&PanelShim),reinterpret_cast<void*>(&FullFrameShim),reinterpret_cast<void*>(&FullDataShim),reinterpret_cast<void*>(&FullDescriptionShim),reinterpret_cast<void*>(&FullRowShim),reinterpret_cast<void*>(&TintShim),reinterpret_cast<void*>(&RotateShim),reinterpret_cast<void*>(&TextShim),reinterpret_cast<void*>(&NumberRightShim),reinterpret_cast<void*>(&NumberLeftShim),reinterpret_cast<void*>(&GlyphShim)};
    if(!NativeUiSupport::Install(base,rvas,replacements,originals,MinHookBatch::Owner::ElementScan,reinterpret_cast<const void*>(&StartElementHook)))return false;
    extraElementsEnabled=extraElements;expandedStatsEnabled=expandedStats;numericScanEnabled=numeric;active=true;
    if(log){
        log(expandedStats?"[ffx-hooks] Scan Expanded: ON (native stats and MP presentation)\n":"[ffx-hooks] Scan Expanded: OFF\n");
        log(extraElements?"[ffx-hooks] Scan Extra Elements: ON (Sensor and full Scan affinities)\n":"[ffx-hooks] Scan Extra Elements: OFF\n");
        log(numeric?"[ffx-hooks] Elemental numerical Scan: ON (admitted resolver snapshots only)\n":"[ffx-hooks] Elemental numerical Scan: OFF\n");
    }
    return true;
}
void InstallElementHook(std::uintptr_t base,bool extraElements,bool expandedStats,void(*log)(const char*)){
    const bool validateOnly=Config::CheckEnabled("core.validate_only","FFXHOOKS_VALIDATE_ONLY",nullptr,false);
    (void)StartElementHook(base,extraElements,expandedStats,validateOnly,log);
}
void RemoveElementHook(){active=false;}
bool IsElementHookInstalled(){return active.load()&&extraElementsEnabled;}
bool IsScanExpandedInstalled(){return active.load()&&expandedStatsEnabled;}
#ifdef FFXHOOKS_TESTING
void ElementScanEnvironmentForTests(void* info,void* panel,void* texture){
    // Private PE fixtures replace game-state/device endpoints, not the shims.
    if(active.load()&&info&&panel&&texture){originals[Info]=info;originals[Panel]=panel;originals[Texture]=texture;}
}
void ElementScanFullEnvironmentForTests(void* rotated,void* text,void* numberR,void* numberL,void* glyph){
    if(active.load()&&rotated&&text&&numberR&&numberL&&glyph){originals[Rotated]=rotated;originals[Text]=text;originals[NumberRight]=numberR;originals[NumberLeft]=numberL;originals[Glyph]=glyph;}
}
#endif
}
