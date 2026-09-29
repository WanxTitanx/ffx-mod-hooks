#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/EquipmentWorkshopNativeUi.h"
#include "../hooks/ArcanaNativeUi.h"
#include "../hooks/ElementHook.h"
#include "../hooks/ElementScanSettings.h"
#include "../hooks/NativePresentationEvidence.h"
#include "PrivatePeFixture.h"
#include "WorkshopEconomyFixture.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <cmath>
namespace W=FfxHooks::EquipmentWorkshop;
namespace E=FfxHooks::ElementScan;
static unsigned checks=0,failures=0;
static std::uintptr_t imageBase=0;
static LONG WINAPI Diagnostic(EXCEPTION_POINTERS* exception){
    const auto* record=exception->ExceptionRecord;const auto* context=exception->ContextRecord;
    if(record->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
        std::printf("PRIVATE_EXCEPTION code=%08lX eip=%08lX imageRva=%08lX eax=%08lX esi=%08lX edi=%08lX address=%08lX\n",
            record->ExceptionCode,context->Eip,static_cast<unsigned long>(context->Eip-imageBase),context->Eax,context->Esi,context->Edi,
            record->NumberParameters>1?static_cast<unsigned long>(record->ExceptionInformation[1]):0ul);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);} }
static bool Write(std::uintptr_t address,const void* data,std::size_t size){
    DWORD before=0,after=0;if(!VirtualProtect(reinterpret_cast<void*>(address),size,PAGE_EXECUTE_READWRITE,&before))return false;
    std::memcpy(reinterpret_cast<void*>(address),data,size);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),size);
    return VirtualProtect(reinterpret_cast<void*>(address),size,before,&after)!=FALSE;
}
struct Patch {std::uintptr_t at=0;unsigned char before[5]{};};
static std::vector<Patch> patches;
static void Redirect(unsigned rva,const void* destination){
    Patch patch{};patch.at=imageBase+rva;std::memcpy(patch.before,reinterpret_cast<void*>(patch.at),5);patches.push_back(patch);
    unsigned char jump[5]={0xE9};const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination)-patch.at-5);
    std::memcpy(jump+1,&delta,4);if(!Write(patch.at,jump,5))throw std::runtime_error("Private fixture patch failed");
}
static void Log(const char* text){std::fputs(text,stdout);}
static bool NoArcanaSave(FfxHooks::Arcana::State&,std::uint64_t&) noexcept {return false;}
static FfxHooks::Arcana::Error NoArcanaEquip(std::uint64_t,std::uint64_t,unsigned,unsigned,std::int16_t,bool) noexcept {return FfxHooks::Arcana::Error::InvalidState;}
static FfxHooks::Arcana::Error NoArcanaMode(std::uint64_t,std::uint64_t,FfxHooks::Arcana::Mode,bool) noexcept {return FfxHooks::Arcana::Error::InvalidState;}
static void NoArcanaImages(const FfxHooks::Arcana::NativeUi::Images&) noexcept {}
static float viewportWidth=512.f,viewportHeight=416.f;
static float __cdecl ScaleX(float value){return value*(viewportWidth/1920.f);}
static float __cdecl ScaleY(float value){return value*(viewportHeight/1080.f);}
static unsigned frames=0,rectangles=0,textures=0,mpLabels=0;
static float lastWidth=0;
static std::vector<std::string> labels;
static std::vector<unsigned> colors;
static std::vector<float> bandWidths;
struct BandDraw {float x,y,width,height;};
struct TextDraw {std::string text;float x,y;};
struct TextureDraw {unsigned atlas;float x,y,w,h,u0,v0,u1,v1;};
struct NumberDraw {bool left;int value;float x,y;unsigned style;float sx,sy;};
struct GlyphDraw {const void* text;int x,y;};
static std::vector<BandDraw> bands;
static std::vector<TextDraw> textDraws;
static std::vector<BandDraw> highlights;
static std::vector<TextureDraw> textureDraws;
static std::vector<NumberDraw> numberDraws;
static std::vector<GlyphDraw> glyphDraws;
struct SpriteCorner {float x,y,u,v;unsigned r,g,b,a;};
struct SpriteQuad {SpriteCorner corners[2];unsigned texture;};
static std::vector<SpriteQuad> sprites;
static int __cdecl Clip(float*,float*,float*,float*,float*,float*,float*,float*){return 1;}
static unsigned __cdecl Atlas(unsigned id){return id==0x3F00?0x12345678u:id==0x3E80?0x87654321u:0u;}
static int __cdecl TextureSize(unsigned,float* width,float* height){if(width)*width=1024.f;if(height)*height=1024.f;return 1;}
static int __cdecl Submit(SpriteCorner* corners,unsigned texture,int,int,int){
    SpriteQuad quad{};std::memcpy(quad.corners,corners,sizeof(quad.corners));quad.texture=texture;sprites.push_back(quad);
    const auto& c=quad.corners[0];colors.push_back((c.a<<24)|(c.b<<16)|(c.g<<8)|c.r);return 93;
}
static int __cdecl Frame(){++frames;return 51;}
static int __cdecl NoDevice(){return 0;}
static int __cdecl Panel(float,float,float width,float,int){lastWidth=width;return 81;}
static int __cdecl Rect(float x,float y,float width,float height,unsigned first,unsigned){++rectangles;highlights.push_back({x,y,width,height});colors.push_back(first);return 91;}
static int __cdecl Texture(unsigned atlas,float x,float y,float width,float height,float u0,float v0,float u1,float v1){
    ++textures;textureDraws.push_back({atlas,x,y,width,height,u0,v0,u1,v1});if(atlas==0xEA)++mpLabels;if(atlas==~0u){bandWidths.push_back(width);bands.push_back({x,y,width,height});}return 71;}
static int __cdecl Text(const unsigned char* text,float x,float y,unsigned,float,float){
    std::string value;for(unsigned i=0;text&&i<190&&text[i];++i)value.push_back(static_cast<char>(text[i]));labels.push_back(value);textDraws.push_back({value,x,y});return 41;
}
static int __cdecl Measure(const unsigned char*,float* width,unsigned,float,float){if(width)*width=80;return 0;}
static const unsigned char* __cdecl EmptyText(){static const unsigned char empty[]={0x5E,0};return empty;}
static bool HasRank(unsigned rank){
    const std::string suffix=std::string(1,0x3A)+char(0x45)+(rank==10?"10":std::to_string(rank));
    for(const auto& label:labels)if(label.size()>=suffix.size()&&label.compare(label.size()-suffix.size(),suffix.size(),suffix)==0)return true;
    return false;
}
static void ResetDraw(){labels.clear();colors.clear();sprites.clear();bandWidths.clear();bands.clear();textDraws.clear();highlights.clear();textureDraws.clear();numberDraws.clear();glyphDraws.clear();frames=rectangles=textures=mpLabels=0;lastWidth=0;}
static bool HasColor(unsigned rgb){for(auto color:colors)if(color==E::NativeColor(rgb))return true;return false;}
static unsigned affinity[4]={0x10,0x80,0x20,0xB0},maskCalls[4]{};
static unsigned char __cdecl Mask(int,int category){if(category<0||category>3)return 0;++maskCalls[category];return static_cast<unsigned char>(affinity[category]);}
// Calls a real mapped E8 site, so _ReturnAddress() tests production caller gates.
// Only its unrelated continuation is redirected inside this private image.
static void* Callsite(unsigned callRva,unsigned argumentCount){
    auto* entry=static_cast<unsigned char*>(VirtualAlloc(nullptr,128,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!entry)throw std::bad_alloc();
    std::vector<unsigned char> code={0x55,0x8B,0xEC};
    for(unsigned i=argumentCount;i;--i){code.insert(code.end(),{0xFF,0x75,static_cast<unsigned char>(4+4*i)});}
    code.push_back(0xE9);const auto delta=static_cast<std::uint32_t>(imageBase+callRva-reinterpret_cast<std::uintptr_t>(entry)-code.size()-4);
    for(unsigned i=0;i<4;++i)code.push_back(static_cast<unsigned char>(delta>>(8*i)));
    const auto tail=code.size();code.insert(code.end(),{0x8B,0xE5,0x5D,0xC3});std::memcpy(entry,code.data(),code.size());
    Redirect(callRva+5,entry+tail);FlushInstructionCache(GetCurrentProcess(),entry,code.size());return entry;
}
static void* panelSite=nullptr;static void* resistanceSite=nullptr;
static void* sensorBandSite=nullptr;
static bool showScan=true;
static int __cdecl ScanBody(int actor,int,int){
    if(!showScan)return 17;
    reinterpret_cast<int(__cdecl*)(float,float,float,float,int)>(panelSite)(10,20,ScaleX(385),120,0);
    if(sensorBandSite)reinterpret_cast<int(__cdecl*)(unsigned,float,float,float,float,float,float,float,float)>(sensorBandSite)(~0u,10,20,ScaleX(365),ScaleY(40),0,0,1,1);
    for(int category=0;category<3;++category)reinterpret_cast<int(__cdecl*)(int,int,int,int)>(imageBase+0x494AB0)(actor,category,10,20+category*20);
    reinterpret_cast<int(__cdecl*)(unsigned,float,float,float,float,float,float,float,float)>(resistanceSite)(0x1B2,10+ScaleX(25),80+ScaleY(6),20,10,0,0,1,1);
    return 23;
}
static DWORD WINAPI ForeignScan(void*){reinterpret_cast<int(__cdecl*)(int,int,int)>(imageBase+0x4939A0)(0x1000,0,0);return 0;}
#include "WorkshopCustomizeNativeCases.inl"
#include "WorkshopExtendedUiCases.inl"
#include "FullScanCoverageCases.inl"
#include "ElementalNumericScanCases.inl"
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,Diagnostic);
    if(argc!=5&&argc!=6)return 2;
    const unsigned scanMode=argc==6?static_cast<unsigned>(std::strtoul(argv[5],nullptr,10)):3u;
    if(scanMode>5)return 2;
    const bool scanNumeric=scanMode>=4;
    const bool scanElements=(scanMode&1)!=0,scanExpanded=(scanMode&2)!=0;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    imageBase=reinterpret_cast<std::uintptr_t>(image);Check(PrivatePeFixture::NormalizeRelocations(image),"private PE relocations match runtime semantics");if(failures)return 2;
    if(!scanMode){
        unsigned char before[32]{};std::memcpy(before,reinterpret_cast<const void*>(imageBase+0x49BEE0),sizeof(before));
        Check(!FfxHooks::StartElementHook(imageBase,false,false,false,Log)&&!FfxHooks::IsElementHookInstalled()&&!FfxHooks::IsScanExpandedInstalled(),"both Scan features OFF install no presentation hooks");
        Check(!std::memcmp(before,reinterpret_cast<const void*>(imageBase+0x49BEE0),sizeof(before)),"both Scan features OFF leave the native data function untouched");
        std::printf("NATIVE_PRESENTATION_RT1 %u/%u passed (Scan mode 0)\n",checks-failures,checks);return failures?1:0;
    }
    const std::wstring directory(argv[3],argv[3]+std::strlen(argv[3])),savePath=directory+L"\\ffx_090";
    W::SaveImage native{};std::ifstream file(argv[2],std::ios::binary);if(!file.read(reinterpret_cast<char*>(native.data()),native.size()))return 2;
    WorkshopEconomyFixture::Seed(native);WorkshopEconomyFixture::Mode(2);
    unsigned char gear[22]{};gear[2]=1;gear[6]=255;gear[11]=4;const std::uint16_t words[]={0x8062,0x8063,0x8062,255};std::memcpy(gear+14,words,8);
    std::memcpy(native.data()+0x44DC,gear,22);FfxHooks::RonsoPool::SealSave(native);
    workshop::State state{};Check(W::ImportSave(native,12345,state),"private native inventory imports");
    auto& piece=state.pieces[0];piece.fifthUnlocked=1;piece.fifth=0x8064;piece.abilities[4]=state.nextId++;piece.mode=2;
    piece.ranks[0]=1;piece.ranks[1]=2;piece.ranks[2]=3;piece.ranks[4]=4;
    W::Store store;Check(store.Initialize(directory,true)&&store.Write(savePath,native,state),"ranked fixture uses the real persisted sidecar");
    Check(W::StartForTests(imageBase,true,directory.c_str(),Log)&&W::LoadForTests(savePath.c_str(),native,native)&&W::CommitLoadForTests(native),"real runtime admits loaded identity");
    Check(!W::NativeUi::Start(imageBase,false,false,Log)&&!FfxHooks::StartElementHook(imageBase,false,false,false,Log),"new drawing hooks default OFF");
    Check(!W::NativeUi::Start(imageBase,true,true,Log)&&!FfxHooks::StartElementHook(imageBase,scanElements,scanExpanded,true,Log),"validation-only suppresses both hook installations");
    auto* signature=reinterpret_cast<unsigned char*>(imageBase+0x4D02B0);const unsigned char original=*signature,bad=0xCC;
    Check(Write(reinterpret_cast<std::uintptr_t>(signature),&bad,1)&&!W::NativeUi::Start(imageBase,true,false,Log),"unmatched drawing profile installs nothing");
    Check(Write(reinterpret_cast<std::uintptr_t>(signature),&original,1),"private signature restored after negative control");
    Check(W::NativeUi::Start(imageBase,true,false,Log)&&W::NativeUi::Active(),"native detail hooks install on the supported image");
    if(scanMode==5)Check(FfxHooks::Config::LoadTextForTests("[f8_authority]\nelemental_core=1\n[elemental]\ncore=1\n","C:\\private-auto-elements.ini"),"Core configuration has no hidden numerical-Scan flag");
    else if(scanNumeric)NumericScanTest::Configure(0);
    Check(FfxHooks::StartElementHook(imageBase,scanElements,scanExpanded,false,Log)&&
          FfxHooks::IsElementHookInstalled()==scanElements&&FfxHooks::IsScanExpandedInstalled()==scanExpanded,"Scan startup exposes the two independently selected features");
    const auto nativeX=reinterpret_cast<float(__cdecl*)(float)>(imageBase+0x244990);
    const auto nativeY=reinterpret_cast<float(__cdecl*)(float)>(imageBase+0x2449D0);
    const float beforeX=nativeX(740.f),beforeY=nativeY(552.f);
    const FfxHooks::Arcana::NativeUi::Callbacks arcana{NoArcanaSave,NoArcanaEquip,NoArcanaMode,NoArcanaImages};
    Check(FfxHooks::Arcana::NativeUi::Start(imageBase,true,false,arcana,Log),"Arcana installs after Scan has admitted its unmodified shared scale dependencies");
    Check(std::fabs(nativeX(740.f)-beforeX)<.001f&&std::fabs(nativeY(552.f)-beforeY)<.001f,"Arcana scale hooks preserve native Scan coordinates outside Equip");
    Check(FfxHooks::StartElementHook(imageBase,scanElements,scanExpanded,false,Log)&&
          FfxHooks::IsElementHookInstalled()==scanElements&&FfxHooks::IsScanExpandedInstalled()==scanExpanded,"later startup reuses the admitted Scan hooks after Arcana owns shared scales");
    if(failures)return 1;
    std::ifstream kernelFile(argv[4],std::ios::binary);std::vector<unsigned char> kernel((std::istreambuf_iterator<char>(kernelFile)),{});if(kernel.size()<14000)return 2;
    const auto kernelAddress=reinterpret_cast<std::uintptr_t>(kernel.data());std::memcpy(reinterpret_cast<void*>(imageBase+0xD2A944),&kernelAddress,4);
    const auto kernelBefore=kernel;
    // UI getters also request a name from an unrelated unloaded kernel table.
    // The detail code does not use that equipment-name pointer.
    Redirect(0x3ABE10,reinterpret_cast<const void*>(&EmptyText));
    Redirect(0x244990,reinterpret_cast<const void*>(&ScaleX));Redirect(0x2449D0,reinterpret_cast<const void*>(&ScaleY));
    W::NativeUi::FrameEnvironmentForTests(reinterpret_cast<void*>(&Frame));Redirect(0x505AB0,reinterpret_cast<const void*>(&Text));Redirect(0x505290,reinterpret_cast<const void*>(&Measure));
    Redirect(0x4E6AF0,reinterpret_cast<const void*>(&NoDevice));Redirect(0x4F9230,reinterpret_cast<const void*>(&NoDevice));Redirect(0x38FD40,reinterpret_cast<const void*>(&EmptyText));
    // Panel and Texture are already hooked: replace their original body AFTER
    // the relocated prologue with a bridge only in the Scan test below.
    *reinterpret_cast<unsigned*>(imageBase+0x146A5F0)=0;
    std::uint16_t selectedGear[]={0};const auto choices=reinterpret_cast<std::uintptr_t>(selectedGear);std::memcpy(reinterpret_cast<void*>(imageBase+0x146A9F8),&choices,4);
    const auto field=reinterpret_cast<int(__cdecl*)()>(imageBase+0x4D02B0);
    const auto custom=reinterpret_cast<int(__cdecl*)(unsigned)>(imageBase+0x4D63C0);
    std::puts("CASE native Equipment");ResetDraw();field();Check(frames==5&&labels.size()==4&&HasRank(1)&&HasRank(2)&&HasRank(3)&&HasRank(4),"Equipment uses five native backgrounds and distinct duplicate-ability ranks");
    std::puts("CASE native Customize");ResetDraw();custom(0);Check(frames==5&&labels.size()==4&&HasRank(4),"Customize displays the fifth through the actual drawing consumer");
    // Battle panel needs an inert device bridge before invoking its full loop.
    Redirect(0x4F41B0,reinterpret_cast<const void*>(&Panel));
    *reinterpret_cast<unsigned*>(imageBase+0xD2A8E0)=1;
    Check(!W::Capture(state),"battle still denies mutation snapshots");
    std::puts("CASE native battle equipment");ResetDraw();reinterpret_cast<int(__cdecl*)(unsigned,float,float)>(imageBase+0x4F34C0)(0,10,20);
    Check(labels.size()==5&&HasRank(1)&&HasRank(2)&&HasRank(3)&&HasRank(4),"battle window measures and renders all five slots without opening mutation admission");
    Check(kernel==kernelBefore&&std::memcmp(reinterpret_cast<void*>(imageBase+0xD30F2C),gear,22)==0,"draws preserve the kernel and the 22-byte native record");
    workshop::Piece shown{};Check(!W::ReadPresentation(gear,shown),"identical bytes at a foreign pointer cannot borrow the equipped identity");
    ExtendedEquipmentCases(store,savePath,native,state);
    auto empty=state;empty.pieces[0].fifth=255;empty.pieces[0].abilities[4]=0;empty.pieces[0].ranks[4]=0;
    // Independently authored UI fixtures cannot replace one path/revision with
    // divergent extension bytes under the new anti-replay storage contract.
    const auto emptyPath=directory+L"\\ffx_089",fourPath=directory+L"\\ffx_088";
    Check(store.Write(emptyPath,native,empty)&&W::LoadForTests(emptyPath.c_str(),native,native)&&W::CommitLoadForTests(native),"empty fifth fixture reloads through its own real save association");
    ResetDraw();field();Check(frames==5&&labels.size()==3&&HasRank(3),"empty unlocked fifth retains an empty native row");
    auto four=empty;four.pieces[0].fifthUnlocked=0;
    Check(store.Write(fourPath,native,four)&&W::LoadForTests(fourPath.c_str(),native,native)&&W::CommitLoadForTests(native),"four-slot refined fixture reloads through its independent save path");
    ResetDraw();custom(0);Check(frames==4&&labels.size()==3&&HasRank(1)&&HasRank(3),"four-slot equipment gets rank labels without an extra row");
    W::NativeUi::Stop();ResetDraw();field();Check(frames==4&&labels.size()==3&&!HasRank(1),"stopping native details immediately restores vanilla four-slot presentation");
    // Restore the panel entry trampoline before exercising the Scan caller gate.
    auto panelAt=std::find_if(patches.begin(),patches.end(),[](const Patch& p){return p.at==imageBase+0x4F41B0;});
    Check(panelAt!=patches.end()&&Write(panelAt->at,panelAt->before,5),"Scan panel hook restored after battle device substitution");
    if(panelAt!=patches.end())patches.erase(panelAt);
    FfxHooks::ElementScanEnvironmentForTests(reinterpret_cast<void*>(&ScanBody),reinterpret_cast<void*>(&Panel),reinterpret_cast<void*>(&Texture));
    Redirect(0x4F4B20,reinterpret_cast<const void*>(&Rect));Redirect(0x4975C0,reinterpret_cast<const void*>(&Mask));
    Redirect(0x4F4DF0,reinterpret_cast<const void*>(&NoDevice));
    // Exercise the actual eleven-argument sprite wrapper, not a test renderer.
    Redirect(0x4E5A20,reinterpret_cast<const void*>(&Clip));
    Redirect(0x4AC870,reinterpret_cast<const void*>(&Atlas));
    Redirect(0x4AC3B0,reinterpret_cast<const void*>(&TextureSize));
    Redirect(0x23F090,reinterpret_cast<const void*>(&Submit));
    using ColoredTexture=int(__cdecl*)(unsigned,float,float,float,float,float,float,float,float,unsigned,unsigned);
    const auto colored=reinterpret_cast<ColoredTexture>(imageBase+0x503EE0);
    ResetDraw();colored(0x1AF,10,20,30,40,.42f,.95f,.46f,.98f,0x80402010u,0x80706050u);
    Check(sprites.size()==1&&sprites[0].texture==0x12345678u&&sprites[0].corners[0].r==16&&sprites[0].corners[1].b==112&&
          sprites[0].corners[1].x==40&&sprites[0].corners[1].y==60,
          "native colored sprite ABI preserves atlas, coordinates and both packed colors");
    panelSite=Callsite(0x493B88,5);resistanceSite=Callsite(0x49414B,9);
    sensorBandSite=Callsite(0x493E59,9);
    const auto scan=reinterpret_cast<int(__cdecl*)(int,int,int)>(imageBase+0x4939A0);
    if(scanNumeric){NumericScanTest::Run(scan,scanElements);FfxHooks::RemoveElementHook();
        std::printf("ELEMENTAL_NUMERICAL_SCAN_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    const E::Settings palette{};
    if(scanElements){
    std::puts("CASE native Scan adapter");ResetDraw();Check(scan(0x1000,0,0)==23,"Scan detour preserves the original environment return value");
    Check(std::fabs(lastWidth-ScaleX(E::PanelWidth))<.01f&&HasColor(palette.rgb[0])&&HasColor(palette.rgb[1])&&HasColor(palette.rgb[2]),"native Scan panel and all three colored columns are rendered");
    Check(rectangles==0&&sprites.size()==6,"extra affinities reuse original sphere artwork instead of rectangle bands");
    Check(maskCalls[0]&&maskCalls[1]&&maskCalls[2]&&maskCalls[3],"Weak Absorb Null and inline Resist all use their actual masks");
    for(unsigned third:{32u,64u})for(unsigned selected=0;selected<16;++selected){
        char settings[200]{};_snprintf_s(settings,sizeof(settings),_TRUNCATE,
            "[element_scan]\nholy_enabled=%u\ndark_enabled=%u\nextra_enabled=%u\nextra_bit=%u\nother_enabled=%u\n",
            selected&1,(selected>>1)&1,(selected>>2)&1,third,(selected>>3)&1);
        FfxHooks::Config::LoadTextForTests(settings,"C:\\private-scan-visibility.ini");
        for(auto& mask:affinity)mask=0xF0;
        for(auto& calls:maskCalls)calls=0;
        ResetDraw();const int result=scan(0x1000,0,0);
        const unsigned count=(selected&1)+((selected>>1)&1)+((selected>>2)&1)+((selected>>3)&1);
        const float expectedWidth=count?371.f+63.f*count:385.f;
        Check(result==23&&std::fabs(lastWidth-ScaleX(expectedWidth))<.01f,
              "native Scan width follows zero through four selected extras");
        Check(bandWidths.size()==1&&std::fabs(bandWidths[0]-ScaleX(365+expectedWidth-385))<.01f,"Sensor band stretches to match the admitted extra columns");
        Check(rectangles==0&&sprites.size()==4*count,"native Scan emits one tinted original sphere per selected active column");
        for(const auto& sprite:sprites){
            const auto& first=sprite.corners[0];const auto& last=sprite.corners[1];
            Check(sprite.texture==0x12345678u&&first.u>.42f&&last.u<.467f&&first.v>.949f&&last.v<.989f&&
                  std::fabs((last.x-first.x)-ScaleX(32.7f))<.001f&&std::fabs((last.y-first.y)-ScaleY(32.7f))<.001f,
                  "extra sprite samples the original silver sphere at native mask dimensions");
        }
        for(unsigned i=0;i<4;++i)Check(HasColor(palette.rgb[i])==bool(selected&(1u<<i)),"independent native toggles preserve all four selected color identities");
        Check(maskCalls[0]==(count?2u:1u)&&maskCalls[1]==(count?2u:1u)&&maskCalls[2]==(count?2u:1u)&&maskCalls[3]==(count?1u:0u),
              "zero extras add no speculative affinity reads and selected extras cover all four categories");
    }
    FfxHooks::Config::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-scan.ini");
    affinity[0]=0x10;affinity[1]=0x80;affinity[2]=0x20;affinity[3]=0xB0;
    showScan=false;ResetDraw();Check(scan(0x1000,0,0)==17&&colors.empty()&&textures==0,"hidden information produces no speculative affinity reads or drawing");showScan=true;
    ResetDraw();const HANDLE thread=CreateThread(nullptr,0,ForeignScan,nullptr,0,nullptr);
    if(thread){WaitForSingleObject(thread,5000);CloseHandle(thread);}
    Check(thread&&std::fabs(lastWidth-ScaleX(385))<.01f&&!HasColor(palette.rgb[0]),"another thread retains the original Scan presentation");
    FfxHooks::Config::LoadTextForTests("[element_scan]\nholy_rgb=1122867\nextra_bit=64\n","C:\\private-scan.ini");affinity[2]=0x40;
    ResetDraw();scan(0x1000,0,0);Check(HasColor(0x112233)&&HasColor(palette.rgb[2]),"saved RGB and alternate third-element bit apply on the next frame");
    FfxHooks::Config::LoadTextForTests("[element_scan]\nholy_rgb=-1\n","C:\\private-scan.ini");ResetDraw();scan(0x1000,0,0);
    Check(std::fabs(lastWidth-ScaleX(385))<.01f&&!HasColor(0x112233),"invalid palette falls back to vanilla width and drawing");
    FfxHooks::Config::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-scan.ini");
    }else{
        ResetDraw();scan(0x1000,0,0);
        Check(std::fabs(lastWidth-ScaleX(385))<.01f&&sprites.empty(),"Scan Expanded alone preserves the vanilla Sensor width and elements");
    }
    FullScanTest::Run(scanElements,scanExpanded);
    FfxHooks::RemoveElementHook();W::RequestStop();
    ResetDraw();Check(scan(0x1000,0,0)==23&&std::fabs(lastWidth-ScaleX(385))<.01f&&!HasColor(palette.rgb[0]),"Scan stop restores vanilla presentation without freeing reachable trampolines");
    // Execute the real affinity arithmetic on the actor bytes owned by Difficulty.
    unsigned char target[0xF90]{};
    using AffinityDamage=int(__cdecl*)(const void*,unsigned,unsigned,int);
    const auto affinityDamage=reinterpret_cast<AffinityDamage>(imageBase+0x38A420);
    const unsigned fields[]={0x5DD,0x5DC,0x5DA,0x5DB};const int damages[]={1500,500,-1000,0};
    for(unsigned bit:{0x10u,0x80u,0x20u,0x40u})for(unsigned category=0;category<4;++category){
        std::memset(target,0,sizeof(target));target[fields[category]]=static_cast<unsigned char>(bit);
        Check(affinityDamage(target,0,bit,1000)==damages[category],"native Holy Darkness and Custom damage follows weak resist absorb and null masks");
        Check(affinityDamage(target,0,1,1000)==1000&&target[fields[category]]==bit,"extra affinities preserve unrelated attacks and actor data");
    }
    CustomizeNativeTest::Run();
    std::printf("NATIVE_PRESENTATION_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
