#include <cstdio>
#if !__has_include("../hooks/ArcanaFieldPresentation.h")
int main(){std::puts("FAIL: native Arcana field message adapter is missing");return 1;}
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include "PrivatePeFixture.h"
#include "../hooks/ArcanaFieldPresentation.h"
#include "../hooks/ArcanaFieldEvidence.generated.h"
#include "../hooks/CaptureCascadeCore.h"
#include "../hooks/CaptureCascadeText.h"
#include <cstring>
#include <fstream>
using namespace FfxHooks::Arcana;
namespace {
unsigned checks=0,failures=0;
bool captureReady=false;
Presentation::State presentation;
unsigned acknowledgements=0;
std::uint64_t activeGeneration=1;
std::uintptr_t fixtureBase=0;
bool systemReady=false;
FfxHooks::CaptureCascade::State systemState;
unsigned systemAcknowledgements=0;
using GeometryFn=void(__cdecl*)(int,int,int,int,int);
GeometryFn originalGeometry=nullptr;
struct Rectangle {int x=0,y=0,width=0,height=0;} lastRectangle;
unsigned geometryCalls=0;
void __cdecl Geometry(int slot,int x,int y,int width,int height){
    if(slot==7){lastRectangle={x,y,width,height};++geometryCalls;}
    originalGeometry(slot,x,y,width,height);
}
bool CaptureSystem(std::uint16_t,bool,Presentation::View& view,std::uint64_t& generation) noexcept {
    if(!systemReady)return false;generation=activeGeneration;view={};
    const auto current=FfxHooks::CaptureCascade::Current(systemState,FfxHooks::CaptureCascade::Aeon::Count);
    if(current.kind!=FfxHooks::CaptureCascade::Tutorial::None){view.kind=Presentation::Kind::System;view.card=0;view.page=current.page;view.revision=current.revision;}
    return true;
}
Error AckSystem(std::uint64_t generation,const Presentation::View& view,Presentation::Navigation input) noexcept {
    if(generation!=activeGeneration||view.kind!=Presentation::Kind::System)return Error::Stale;
    const FfxHooks::CaptureCascade::View current{FfxHooks::CaptureCascade::Tutorial::Sin,view.page,FfxHooks::CaptureCascade::kNoFamily,view.revision};
    if(!FfxHooks::CaptureCascade::Acknowledge(systemState,current,input==Presentation::Navigation::Previous,input==Presentation::Navigation::Skip))return Error::Stale;
    ++systemAcknowledgements;return Error::None;
}
bool FormatSystem(const Presentation::View& view,char* text,std::size_t capacity) noexcept {
    return FfxHooks::CaptureCascade::Format({FfxHooks::CaptureCascade::Tutorial::Sin,view.page,FfxHooks::CaptureCascade::kNoFamily,view.revision},false,text,capacity);
}
void Check(bool b,const char* label){++checks;if(!b){++failures;std::printf("FAIL: %s\n",label);}}
bool Capture(Presentation::State& out,std::uint64_t& generation) noexcept {if(!captureReady)return false;out=presentation;generation=activeGeneration;return true;}
Error Ack(std::uint64_t generation,const Presentation::View& view,Presentation::Navigation input) noexcept {
    if(generation!=activeGeneration||!Presentation::Acknowledge(presentation,view,input))return Error::Stale;
    ++acknowledgements;return Error::None;
}
bool Focus() noexcept {return true;}
void Log(const char* text){std::printf("%s",text);}
static const unsigned char noticeTemplate[]={0x5e,0x71,0x83,0x70,0x78,0x7d,0x74,0x73,0x3a,0x13,0x30,0};
unsigned __cdecl Template(int id){Check(id==16408,"exact singular native item-received template requested");return reinterpret_cast<unsigned>(noticeTemplate);}
bool Patch(std::uintptr_t at,void* target){
    unsigned char jump[5]={0xe9};const auto relative=static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target)-at-5);std::memcpy(jump+1,&relative,4);
    DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(at),jump,5);return VirtualProtect(reinterpret_cast<void*>(at),5,old,&ignored)&&FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),5);
}
bool ResolveCrt(HMODULE image,const char* filename){
    auto* bytes=reinterpret_cast<unsigned char*>(image);auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes+dos->e_lfanew);
    auto* imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;imports->Name;++imports){
        const char* library=reinterpret_cast<const char*>(bytes+imports->Name);
        if(_stricmp(library,"msvcr110.dll"))continue;
        char path[MAX_PATH]{};strncpy_s(path,filename,_TRUNCATE);char* slash=std::strrchr(path,'\\');if(!slash)slash=std::strrchr(path,'/');if(!slash)return false;
        strcpy_s(slash+1,sizeof(path)-static_cast<std::size_t>(slash+1-path),library);
        const auto module=LoadLibraryA(path);if(!module)return false;
        auto* names=reinterpret_cast<IMAGE_THUNK_DATA32*>(bytes+imports->OriginalFirstThunk);
        auto* slots=reinterpret_cast<IMAGE_THUNK_DATA32*>(bytes+imports->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots){
            if(IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))continue;
            auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(bytes+names->u1.AddressOfData);
            const auto function=GetProcAddress(module,reinterpret_cast<const char*>(name->Name));if(!function)return false;
            DWORD old=0,ignored=0;if(!VirtualProtect(slots,4,PAGE_READWRITE,&old))return false;
            slots->u1.Function=reinterpret_cast<DWORD>(function);if(!VirtualProtect(slots,4,old,&ignored))return false;
        }
    }
    return true;
}
LONG WINAPI Diagnostic(EXCEPTION_POINTERS* info){
    static LONG reporting=0;
    if(info->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&!InterlockedExchange(&reporting,1)){
        const auto* cpu=info->ContextRecord;
        std::printf("FIELD_PRIVATE_EXCEPTION pc=%08lX base=%08lX rva=%08lX eax=%08lX ecx=%08lX edx=%08lX\n",cpu->Eip,static_cast<unsigned long>(fixtureBase),static_cast<unsigned long>(cpu->Eip-fixtureBase),cpu->Eax,cpu->Ecx,cpu->Edx);
        auto frame=cpu->Ebp;
        for(unsigned i=0;i<5&&frame;++i){
            __try {const auto* p=reinterpret_cast<const DWORD*>(frame);std::printf("FIELD_PRIVATE_STACK %u return=%08lX rva=%08lX args=%08lX,%08lX,%08lX\n",i,p[1],static_cast<unsigned long>(p[1]-fixtureBase),p[2],p[3],p[4]);if(p[0]<=frame||p[0]-frame>1024*1024)break;frame=p[0];}
            __except(EXCEPTION_EXECUTE_HANDLER){break;}
        }
        InterlockedExchange(&reporting,0);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}
int main(int argc,char** argv){
    if(argc!=2&&argc!=3)return 2;
    const auto module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(module&&PrivatePeFixture::NormalizeRelocations(module),"matching private executable maps with normalized relocations");
    if(!module||failures)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    fixtureBase=base;AddVectoredExceptionHandler(1,Diagnostic);
    Check(FieldPresentation::Profile(base),"all native field entry and data witnesses match the actual executable");
    FieldPresentation::Callbacks callbacks;callbacks.capture=Capture;callbacks.acknowledge=Ack;
    Check(!FieldPresentation::Start(base,false,false,callbacks,nullptr)&&!FieldPresentation::Active(),"disabled Arcana installs no field message producer");
    Check(!FieldPresentation::Start(base,true,true,callbacks,nullptr)&&!FieldPresentation::Active(),"validation-only mode does not install field hooks");
    const auto rva=FieldPresentation::Evidence::Rva(FieldPresentation::Evidence::Entry::Sync);
    unsigned char saved=0;std::memcpy(&saved,reinterpret_cast<void*>(base+rva),1);
    DWORD old=0,ignored=0;Check(VirtualProtect(reinterpret_cast<void*>(base+rva),1,PAGE_EXECUTE_READWRITE,&old)!=FALSE,"private witness mutation is writable");
    *reinterpret_cast<unsigned char*>(base+rva)^=1;
    Check(!FieldPresentation::Profile(base),"changed native field producer fails admission");
    *reinterpret_cast<unsigned char*>(base+rva)=saved;
    Check(VirtualProtect(reinterpret_cast<void*>(base+rva),1,old,&ignored)!=FALSE&&FieldPresentation::Profile(base),"original native witness is restored exactly");
    FieldPresentation::Callbacks missing;
    Check(!FieldPresentation::Start(base,true,false,missing,nullptr)&&!FieldPresentation::Active(),"missing persistence bindings cannot claim native UI ownership");
    callbacks.focused=Focus;
    callbacks.supplemental={CaptureSystem,AckSystem,FormatSystem,nullptr};
    Check(FieldPresentation::Start(base,true,false,callbacks,Log),"verified field producer installs with valid persistence bindings");
    if(failures)return 1;
    namespace E=FieldPresentation::Evidence;
    Check(ResolveCrt(module,argv[1]),"native field helper uses its actual private CRT imports");
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    constexpr std::uintptr_t geometryRva=0x4AFBB0;
#else
    constexpr std::uintptr_t geometryRva=0x4AFB60;
#endif
    constexpr unsigned char geometryPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x24,0x53,0x56,0x57};
    Check(std::memcmp(reinterpret_cast<void*>(base+geometryRva),geometryPrefix,sizeof(geometryPrefix))==0,"actual native rectangle compositor has the verified profile");
    Check(MH_CreateHook(reinterpret_cast<void*>(base+geometryRva),reinterpret_cast<void*>(&Geometry),reinterpret_cast<void**>(&originalGeometry))==MH_OK&&
        MH_EnableHook(reinterpret_cast<void*>(base+geometryRva))==MH_OK,"fixture observes native measured bounds while preserving the real compositor");
    if(failures)return 1;
    // Match the native language service used by the existing composition
    // fixture; DONT_RESOLVE_DLL_REFERENCES never initializes game settings.
    std::array<unsigned,8> language{};language[1]=1;
    const auto languageAddress=reinterpret_cast<std::uintptr_t>(language.data());
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x8DED48>()),&languageAddress,4);
    // Supply font metrics, not a substitute parser/layout implementation. The
    // width-table getter's absolute operand is verified against both PEs.
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    constexpr std::uintptr_t metricsGetter=0x4AC3D0;
#else
    constexpr std::uintptr_t metricsGetter=0x4AC380;
#endif
    constexpr unsigned char metricsPrefix[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0xc1,0xe0,0x04,0x8b,0x80};
    Check(std::memcmp(reinterpret_cast<void*>(base+metricsGetter),metricsPrefix,sizeof(metricsPrefix))==0,"font metric table getter matches its exact ABI");
    std::uintptr_t metricsTable=0;std::memcpy(&metricsTable,reinterpret_cast<void*>(base+metricsGetter+11),4);
    std::array<unsigned char,0x1000> widths{};widths.fill(16);
    if(argc==3){
        std::array<unsigned char,304> metrics{};std::ifstream file(argv[2],std::ios::binary);
        Check(file.read(reinterpret_cast<char*>(metrics.data()),metrics.size()).good(),"actual Western metric fixture has its full 304-byte extent");
        if(failures)return 1;
        std::memcpy(widths.data(),metrics.data()+64,208);
    }
    const auto widthsAddress=reinterpret_cast<std::uintptr_t>(widths.data());
    for(unsigned i=0;i<8;++i)std::memcpy(reinterpret_cast<void*>(metricsTable+i*16),&widthsAddress,4);
    reinterpret_cast<void(__cdecl*)(int)>(base+E::Rva(E::Entry::InitSlotBank))(0);
    *reinterpret_cast<unsigned char*>(base+E::kBank)=0;
    const auto context=base+E::kContexts;
    *reinterpret_cast<std::uintptr_t*>(base+E::kContextPointer)=context;
    std::memset(reinterpret_cast<void*>(context),0,568);
    *reinterpret_cast<unsigned char*>(context)=3;*reinterpret_cast<unsigned char*>(context+1)=8;
    auto* character=VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(character!=nullptr,"private field character storage exists");
    if(!character)return 1;
    *reinterpret_cast<std::uintptr_t*>(base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)=reinterpret_cast<std::uintptr_t>(character);
    *reinterpret_cast<std::uint16_t*>(base+RVA_FFX_SCENE_STATE_OBJECT)=42;
    *reinterpret_cast<std::uint32_t*>(base+RVA_FFX_FASTLOAD_ACTIVE_SCENE)=42;
    *reinterpret_cast<std::int32_t*>(base+RVA_FFX_CURRENT_MENU_SCREEN_ID)=-1;
    *reinterpret_cast<std::uint32_t*>(base+RVA_FFX_SCENE_TRANSITION_PENDING)=0;
    *reinterpret_cast<unsigned char*>(base+RVA_FFX_BATTLE_ACTIVE_FLAG)=0;
    for(unsigned i=0;i<16;++i)*reinterpret_cast<unsigned char*>(base+E::kInputMap+i)=static_cast<unsigned char>(i);
    std::memset(reinterpret_cast<void*>(base+E::kEvents),0,352);
    // Font/dictionary asset loading is outside this isolated field-call fixture.
    // Only the template provider is substituted, after code admission succeeded.
    Check(Patch(base+E::Rva(E::Entry::Template),reinterpret_cast<void*>(Template)),"private native dictionary dependency is supplied");
    Presentation::Owned before{},owned{};owned[36]=1;Presentation::ObserveAwards(presentation,before,owned);captureReady=true;
    reinterpret_cast<int(__cdecl*)()>(base+E::Rva(E::Entry::Sync))();
    const auto nativeEvent=base+E::kEvents+7*44;
    Check(FieldPresentation::Status()==FieldPresentation::Code::Showing&&*reinterpret_cast<std::uint16_t*>(nativeEvent+20)==1,
          "actual field producer opens the native item-received window");
    Check(presentation.count==1&&acknowledgements==0,"opening a native box never consumes its receipt");
    const auto sync=reinterpret_cast<int(__cdecl*)()>(base+E::Rva(E::Entry::Sync));
    const auto phase=[&](std::uint16_t value){
        *reinterpret_cast<std::uint16_t*>(base+E::kManagerPrimary+7*28)=value;
        *reinterpret_cast<std::uint16_t*>(base+E::kFramePrimary+7*124+8)=value;
        *reinterpret_cast<std::uint16_t*>(base+E::kWindowPrimary+7*304+46)=3;
    };
    phase(2);*reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0x20;sync();
    Check(*reinterpret_cast<std::uint16_t*>(nativeEvent+20)==2&&acknowledgements==0,"opening input held during native reading cannot close receipt");
    *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;sync();
    *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0x20;sync();
    Check(*reinterpret_cast<std::uint16_t*>(base+E::kFramePrimary+7*124+8)==3&&acknowledgements==0,"fresh input invokes actual native close but does not acknowledge during animation");
    // The isolated harness has no render loop. Advance the native animation's
    // terminal phase, then exercise the real field producer/ownership cleanup.
    phase(4);sync();
    Check(acknowledgements==1&&presentation.count==0&&Presentation::Current(presentation).kind==Presentation::Kind::Introduction,"native close completion acknowledges exactly once and schedules full intro");
    *reinterpret_cast<std::uintptr_t*>(base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)=reinterpret_cast<std::uintptr_t>(character);
    *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;sync();
    Check(FieldPresentation::Status()==FieldPresentation::Code::Showing,"first-card introduction opens through the actual native field API");
    std::printf("GUIDE_NATIVE_RECT x=%d y=%d width=%d height=%d\n",lastRectangle.x,lastRectangle.y,lastRectangle.width,lastRectangle.height);
    Check(geometryCalls&&lastRectangle.width>0&&lastRectangle.width<480&&lastRectangle.height>0&&lastRectangle.height<380,
          "guide background fits native text measurement instead of a nearly full-screen fixed rectangle");
    ++activeGeneration;phase(2);sync();
    Check(*reinterpret_cast<std::uint16_t*>(base+E::kFramePrimary+7*124+8)==3,"same-scene load closes a surviving owned native slot");
    phase(4);sync();
    Check(acknowledgements==1&&Presentation::Current(presentation).page==0&&*reinterpret_cast<std::uint16_t*>(nativeEvent+20)==0,"stale cleanup restores native slot without consuming another save's tutorial");
    for(unsigned page=0;page<Presentation::kGuidePageCount;++page){
        *reinterpret_cast<std::uintptr_t*>(base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)=reinterpret_cast<std::uintptr_t>(character);
        *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;sync();
        Check(FieldPresentation::Status()==FieldPresentation::Code::Showing&&Presentation::Current(presentation).page==page,"each guide page opens with its own actual native text and geometry");
        Check(lastRectangle.x>=0&&lastRectangle.y>=4&&lastRectangle.x+lastRectangle.width<=512&&lastRectangle.y+lastRectangle.height<=412,
              "all guide pages remain within the native logical canvas");
        phase(2);sync();*reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0x20;sync();phase(4);sync();
    }
    Check(presentation.intro==Presentation::Intro::Complete&&acknowledgements==13,"the twelve native guide pages finish only after twelve fresh confirmations");
    systemReady=true;captureReady=false;
    FfxHooks::CaptureCascade::ObserveRegion(systemState,FfxHooks::CaptureCascade::Aeon::Shiva,true);
    for(unsigned page=0;page<3;++page){
        *reinterpret_cast<std::uintptr_t*>(base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)=reinterpret_cast<std::uintptr_t>(character);
        *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;sync();
        Check(FieldPresentation::Status()==FieldPresentation::Code::Showing&&systemState.sin.page==page,"SIN tutorial uses native field layout without an Arcana save binding");
        phase(2);sync();*reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0x20;sync();
        Check(systemAcknowledgements==page,"system tutorial waits for actual close completion");
        phase(4);sync();
    }
    Check(systemState.sin.complete&&systemAcknowledgements==3&&acknowledgements==13,"system tutorial persistence is independent of Arcana receipts");
    systemReady=false;captureReady=true;++activeGeneration;
    Check(Presentation::AdoptLegacy(presentation,owned),"fixture reopens an existing collection's introduction without new awards");
    *reinterpret_cast<std::uintptr_t*>(base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)=reinterpret_cast<std::uintptr_t>(character);
    *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;sync();phase(2);sync();
    *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0x40;sync();
    Check(*reinterpret_cast<std::uint16_t*>(base+E::kFramePrimary+7*124+8)==3&&presentation.intro!=Presentation::Intro::Complete,
          "native Cancel starts the real native close before persisting a skip");
    phase(4);sync();*reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;
    Check(presentation.intro==Presentation::Intro::Complete&&acknowledgements==14&&Presentation::Validate(presentation,owned),
          "native close acknowledges skipped introduction once without changing the card collection");
    int largestWidth=0,largestHeight=0;
    for(unsigned card=0;card<Presentation::kCardCount;++card){
        Presentation::Owned collection{};collection[card]=1;++activeGeneration;
        Check(Presentation::AdoptLegacy(presentation,collection),"each possible first card has a valid optional guide");
        *reinterpret_cast<std::uintptr_t*>(base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)=reinterpret_cast<std::uintptr_t>(character);
        *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;sync();
        Check(FieldPresentation::Status()==FieldPresentation::Code::Showing&&lastRectangle.width>0&&lastRectangle.height>0&&
              lastRectangle.x>=0&&lastRectangle.y>=4&&lastRectangle.x+lastRectangle.width<=512&&lastRectangle.y+lastRectangle.height<=412,
              "all 90 first-card descriptions fit their native measured window");
        largestWidth=(std::max)(largestWidth,lastRectangle.width);largestHeight=(std::max)(largestHeight,lastRectangle.height);
        phase(2);sync();
        // Swap each official physical input bit with native Cancel. The real
        // normalizer, not a test callback, resolves all keyboard/pad bindings.
        const unsigned physical=card%16;
        auto* map=reinterpret_cast<unsigned char*>(base+E::kInputMap);
        map[6]=static_cast<unsigned char>(physical);map[physical]=6;
        *reinterpret_cast<std::uint16_t*>(base+E::kHeld)=static_cast<std::uint16_t>(1u<<physical);
        sync();phase(4);sync();*reinterpret_cast<std::uint16_t*>(base+E::kHeld)=0;
        for(unsigned bit=0;bit<16;++bit)map[bit]=static_cast<unsigned char>(bit);
        Check(presentation.intro==Presentation::Intro::Complete&&Presentation::Validate(presentation,collection),
              "native Cancel preserves every possible first-card collection");
    }
    std::printf("GUIDE_NATIVE_MAX width=%d height=%d cards=%u\n",largestWidth,largestHeight,Presentation::kCardCount);
    std::array<unsigned char,256> camera{},projectionOwner{};
    const auto cameraAddress=reinterpret_cast<std::uintptr_t>(camera.data());
    const auto projectionAddress=reinterpret_cast<std::uintptr_t>(projectionOwner.data());
    std::memcpy(projectionOwner.data()+220,&cameraAddress,4);
    std::memcpy(reinterpret_cast<void*>(base+E::kProjectionOwner),&projectionAddress,4);
    float matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
    std::memcpy(camera.data()+116,matrix,sizeof(matrix));
    float projectedX=0,projectedY=0;
    Check(FieldPresentation::Project(0,0,0,projectedX,projectedY)&&projectedX==.5f&&projectedY==.5f,"actual native field projection maps the camera center to normalized screen center");
    Check(FieldPresentation::Project(.5f,.5f,0,projectedX,projectedY)&&projectedX==.25f&&projectedY==.75f,"native projection preserves observed field axes and viewport scaling");
    matrix[15]=-1;std::memcpy(camera.data()+116,matrix,sizeof(matrix));
    Check(!FieldPresentation::Project(0,0,0,projectedX,projectedY),"world symbols behind the actual native camera are not exposed");
    FieldPresentation::Stop();
    FreeLibrary(module);
    std::printf("ArcanaFieldPresentationRt1: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
#endif
