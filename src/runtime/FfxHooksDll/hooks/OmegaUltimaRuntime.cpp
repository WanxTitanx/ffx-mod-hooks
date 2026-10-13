#include "OmegaUltimaRuntime.h"
#include "OmegaUltimaSettings.h"
#include "CustomMixWindowsAdapter.h"
#include "NativeUiHookSupport.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace FfxHooks::OmegaUltima::Runtime {
namespace {
using Byte=unsigned char;
namespace Carrier=CustomMixUltra::WindowsAdapter;
constexpr auto SaveRva=ExecutableProfile::Rva<0xD2CA90>();
std::atomic<bool> active{false};
std::atomic<const char*> detail{"Omega Ultima is OFF"};
std::atomic<DWORD> owner{0};
std::uintptr_t image=0;Log logger=nullptr;
bool nativeAssetReady=false;
thread_local bool inside=false;
struct Frame {
    FormationProof proof{};Layout layout{};char name[10]{};
    std::array<std::vector<Byte>,8> views{};
};
struct Pack {Manifest manifest;std::array<Frame,FormationCount> frames;};
// Published formation/ATEL interior pointers can survive the InitScene call.
// At most one immutable pack is published per process; logical Stop never frees it.
Pack* pack=nullptr;
struct Ticket {SinNatural::Evidence evidence{};std::uint32_t request=0;Bridge::CurrentRequest current=nullptr;};
Ticket pending{};std::uint32_t lastRequest=0;
#ifdef FFXHOOKS_TESTING
AssetReader assetReader=nullptr;Random testRandom{};
#endif
template<class T> bool Read(std::uintptr_t address,T& value) noexcept {
    return NativeUiSupport::Copy(&value,reinterpret_cast<const void*>(address),sizeof(value));
}
void LogLine(const char* text) noexcept {if(logger)try{logger(text);}catch(...) {}}
bool File(const std::filesystem::path& path,std::vector<Byte>& bytes,unsigned maximum,unsigned exact=0) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};DWORD count=0;bool ok=false;
    if(GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
       !info.nFileSizeHigh&&info.nFileSizeLow&&info.nFileSizeLow<=maximum&&(!exact||info.nFileSizeLow==exact)){
        try {bytes.resize(info.nFileSizeLow);ok=ReadFile(file,bytes.data(),info.nFileSizeLow,&count,nullptr)&&count==info.nFileSizeLow;}
        catch(...){ok=false;}
    }
    CloseHandle(file);return ok;
}
bool Profile(std::uintptr_t base) noexcept {
#ifndef FFXHOOKS_TARGET_STEAM_20261001
    (void)base;return false;
#else
    Byte header[4096]{};F8Runtime::ExecutableIdentity identity{};
    if(!NativeUiSupport::Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity))return false;
    // Steam 25501027: MsGetSaveEventAddress returns the payload base directly.
    Byte expected[6]={0xB8,0,0,0,0,0xC3},actual[6]{};
    const auto root=static_cast<std::uint32_t>(base+SaveRva);std::memcpy(expected+1,&root,4);
    if(!NativeUiSupport::Copy(actual,reinterpret_cast<void*>(base+0x385240),6)||std::memcmp(actual,expected,6))return false;
    // Same verified native stream chain used by CaptureCascadeBattle. No hook
    // is installed here; EFL/VBF selection must serve the exact approved clone.
    const std::uintptr_t rvas[]={ExecutableProfile::Rva<0x207D80>(),ExecutableProfile::Rva<0x207F40>(),ExecutableProfile::Rva<0x207F80>(),ExecutableProfile::Rva<0x208250>()};
    const Byte prefixes[][10]={{0x55,0x8b,0xec,0x8b,0x45,0x08,0x56,0x8b,0xf1,0xc7},{0x56,0x8b,0xf1,0x8b,0x4e,0x04,0x85,0xc9,0x74,0x0c},{0x55,0x8b,0xec,0x8b,0x41,0x04,0x83,0xec,0x08,0x85},{0x55,0x8b,0xec,0x51,0x8b,0x41,0x04,0x85,0xc0,0x74}};
    for(unsigned i=0;i<4;++i){Byte bytes[10]{};if(!NativeUiSupport::Copy(bytes,reinterpret_cast<void*>(base+rvas[i]),10)||std::memcmp(bytes,prefixes[i],10))return false;}
    return true;
#endif
}
struct Stream {HANDLE file=INVALID_HANDLE_VALUE;void* archive=nullptr;};
static_assert(sizeof(Stream)==8,"Native x86 stream extent");
bool NativeClone(Byte* bytes,unsigned size) noexcept {
#ifdef FFXHOOKS_TESTING
    if(assetReader)return assetReader(image,bytes,size);
#endif
    Stream stream;bool opened=false,valid=false;
    __try {
        using Create=Stream*(__thiscall*)(Stream*,const char*,int,unsigned,unsigned,int);
        using Size=unsigned(__thiscall*)(Stream*);
        using Load=unsigned(__thiscall*)(Stream*,void*,unsigned);
        opened=reinterpret_cast<Create>(image+ExecutableProfile::Rva<0x207D80>())(&stream,
            "../../../ffx_ps2/ffx/master/jppc/battle/mon/_m910/m910.bin",1,0,0,1)==&stream;
        if(opened&&(stream.file!=INVALID_HANDLE_VALUE||stream.archive)&&
           reinterpret_cast<Size>(image+ExecutableProfile::Rva<0x207F80>())(&stream)==size)
            valid=reinterpret_cast<Load>(image+ExecutableProfile::Rva<0x208250>())(&stream,bytes,size)==size;
    }__except(EXCEPTION_EXECUTE_HANDLER){valid=false;}
    if(opened)__try{reinterpret_cast<bool(__thiscall*)(Stream*)>(image+ExecutableProfile::Rva<0x207F40>())(&stream);}__except(EXCEPTION_EXECUTE_HANDLER){valid=false;}
    return valid;
}
bool AdmitNativeClone() noexcept {
    if(nativeAssetReady)return true;
    try {std::vector<Byte> bytes(pack->manifest.cloneSize);
        if(!NativeClone(bytes.data(),static_cast<unsigned>(bytes.size()))||Hash(bytes.data(),bytes.size())!=pack->manifest.cloneHash){
            active=false;detail="Omega Ultima native clone unavailable; restart required";LogLine("[ffx-hooks] Omega Ultima: native loader rejected m910; original encounters retained\n");return false;
        }
        nativeAssetReady=true;return true;
    }catch(...){return false;}
}
bool RandomDraw(void*,unsigned bound,unsigned& value) noexcept {
    if(!active.load())return false;
#ifdef FFXHOOKS_TESTING
    if(testRandom.draw)return testRandom.draw(testRandom.context,bound,value);
#endif
    std::uint32_t raw=0;
    return BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&raw),sizeof(raw),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0&&Bounded(raw,bound,value);
}
void Natural(const SinNatural::Evidence& evidence,std::uint32_t request,Bridge::CurrentRequest current) noexcept {
    if(!active.load()||!pack)return;
    if(!SinNatural::Admitted(evidence)||!request||!current||!current(request,evidence.nativeFieldRow))return;
    const DWORD thread=GetCurrentThreadId();
    if(!EligibleFormation(evidence.field,evidence.formation)){if(owner.load()==thread)pending={};return;}
    DWORD zero=0;owner.compare_exchange_strong(zero,thread);
    if(owner.load()!=thread)return;
    pending={};
    if(request==lastRequest)return;
    pending={evidence,request,current};
}
struct Loan {std::uint32_t original=0,view=0;std::uint16_t size=0;bool restored=false;};
bool Exchange(const Loan& loan,bool enter) noexcept {
    std::uint16_t size=0;if(!Read(image+Carrier::kCarrierSizeRva,size)||size!=loan.size)return false;
    const auto expected=enter?loan.original:loan.view,next=enter?loan.view:loan.original;
    __try {return static_cast<std::uint32_t>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(image+Carrier::kCarrierPointerRva),static_cast<LONG>(next),static_cast<LONG>(expected)))==expected;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Invoke(Loan* loan,void* context,Bridge::Original original,int* result) noexcept {
    bool returned=false;inside=true;
    __try {returned=original(context,result);}
    __finally {
        loan->restored=Exchange(*loan,false);
        if(!loan->restored){active=false;detail="Omega Ultima resource ownership lost; restart required";}
        inside=false;
    }
    return returned;
}
bool StillCurrent(const Frame& frame,std::uint32_t expected,std::uint16_t extent) noexcept {
    std::uint32_t root=0;std::uint16_t size=0;char name[10]{};
    if(!Read(image+Carrier::kCarrierPointerRva,root)||root!=expected||!root||root>UINT32_MAX-extent||
       !Read(image+Carrier::kCarrierSizeRva,size)||size!=extent||
       !Read(image+Carrier::kCarrierNameRva,name)||std::memcmp(name,frame.name,sizeof(name)))return false;
    // Native loader and RNG callbacks may yield to another owner. Revalidate
    // the complete source immediately before the compare-owned pointer loan.
    std::array<Byte,4096> bytes{};std::uint64_t hash=0xCBF29CE484222325ULL;
    for(unsigned offset=0;offset<size;){const unsigned remaining=size-offset,count=remaining>bytes.size()?static_cast<unsigned>(bytes.size()):remaining;
        if(!NativeUiSupport::Copy(bytes.data(),reinterpret_cast<const void*>(root+offset),count))return false;
        for(unsigned i=0;i<count;++i){hash^=bytes[i];hash*=0x100000001B3ULL;}offset+=count;
    }
    return hash==frame.proof.hash;
}
bool Run(std::uintptr_t base,void* context,Bridge::Original original,int* result) noexcept {
    if(!original||!result)return false;
    if(!active.load()||!pack||base!=image||owner.load()!=GetCurrentThreadId()||inside)return original(context,result);
    const auto ticket=pending;pending={};
    if(!ticket.request||ticket.request==lastRequest||!ticket.current||!ticket.current(ticket.request,ticket.evidence.nativeFieldRow))return original(context,result);
    lastRequest=ticket.request;
    Byte flags=0;if(!Read(image+SaveRva+CompletionOffset,flags)||!UltimaResolved(flags))return original(context,result);
    Frame* frame=nullptr;
    for(auto& value:pack->frames)if(value.proof.field==OmegaEncounter::PackageField(ticket.evidence.field)&&value.proof.index==ticket.evidence.formation){frame=&value;break;}
    std::uint32_t root=0;std::uint16_t size=0;char name[10]{};
    if(!frame||!Read(image+Carrier::kCarrierPointerRva,root)||!Read(image+Carrier::kCarrierSizeRva,size)||!Read(image+Carrier::kCarrierNameRva,name)||
       !root||root>UINT32_MAX-size||size!=frame->proof.size||std::memcmp(name,frame->name,sizeof(name)))return original(context,result);
    try {std::vector<Byte> bytes(size);Layout layout;
        if(!NativeUiSupport::Copy(bytes.data(),reinterpret_cast<void*>(root),size)||!Inspect(frame->proof,bytes.data(),size,layout))return original(context,result);
    }catch(...){return original(context,result);}
    if(!AdmitNativeClone()||!active.load())return original(context,result);
    const auto selection=Select(true,flags,frame->layout.eligible,{nullptr,RandomDraw});
    if(!selection.selected||!active.load()||!ticket.current(ticket.request,ticket.evidence.nativeFieldRow)||
       !Read(image+SaveRva+CompletionOffset,flags)||!UltimaResolved(flags)||!StillCurrent(*frame,root,size))return original(context,result);
    Loan loan{root,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(frame->views[selection.slot].data())),size};
    // Only the carrier pointer is loaned; the immutable view has exactly the
    // same size. No actor fields or native formation bytes are ever patched.
    if(!Exchange(loan,true))return original(context,result);
    const bool returned=Invoke(&loan,context,original,result);
    if(!loan.restored){active=false;detail="Omega Ultima resource ownership lost; restart required";LogLine("[ffx-hooks] Omega Ultima: resource owner changed; further replacement stopped\n");}
    if(returned&&loan.restored&&logger){char line[160]{};std::snprintf(line,sizeof(line),
        "[ffx-hooks] Omega Ultima: formation=%s slot=%u species=910; resource restored\n",frame->name,selection.slot);LogLine(line);}
    return returned;
}
const Bridge::Provider provider{Natural,Run};
}
bool Requested() noexcept {return Settings::Requested();}
bool Start(std::uintptr_t base,const std::filesystem::path& game,bool validateOnly,Log log){
    if(validateOnly||!Requested())return false;
    if(pack)return base==image&&active.load();
    logger=log;
    try {
    if(!Profile(base)){detail="Omega Ultima executable profile unavailable";return false;}
    std::vector<Byte> text;
    if(!File(game/L"modules"/L"config"/L"_omega-ultima-v1.json",text,32768)){detail="Omega Ultima manifest unavailable";return false;}
    auto candidate=std::make_unique<Pack>();
    const auto parsed=ParseManifest(std::string_view(reinterpret_cast<const char*>(text.data()),text.size()),candidate->manifest);
    if(parsed!=ManifestCode::Ready){detail=parsed==ManifestCode::Disabled?"Omega Ultima is OFF":"Omega Ultima manifest invalid or unresolved";return false;}
    const auto battle=game/L"data"/L"mods"/L"ffx_ps2"/L"ffx"/L"master"/L"jppc"/L"battle";
    std::vector<Byte> clone;
    if(!File(battle/L"mon"/L"_m910"/L"m910.bin",clone,1024*1024,candidate->manifest.cloneSize)||
       Hash(clone.data(),clone.size())!=candidate->manifest.cloneHash){detail="Omega Ultima clone is missing or changed";return false;}
    for(unsigned i=0;i<FormationCount;++i){auto& frame=candidate->frames[i];frame.proof=candidate->manifest.formations[i];
        std::snprintf(frame.name,sizeof(frame.name),"omeg%02u_%02u",frame.proof.field-1076,frame.proof.index);
        const std::wstring name(frame.name,frame.name+9);std::vector<Byte> bytes;
        if(!File(battle/L"btl"/name/(name+L".bin"),bytes,65535,frame.proof.size)||!Inspect(frame.proof,bytes.data(),bytes.size(),frame.layout)){
            detail="Omega Ultima formation package is missing or changed";return false;
        }
        for(unsigned slot=0;slot<8;++slot)if(frame.layout.eligible&(1u<<slot))
            if(!Build(frame.proof,bytes.data(),bytes.size(),slot,frame.views[slot]))return false;
    }
    if(!Bridge::Register(&provider)){detail="Omega Ultima encounter consumer unavailable";return false;}
    image=base;pack=candidate.release();pending={};lastRequest=0;nativeAssetReady=false;owner=0;
    detail="Omega Ultima waiting for a supported natural encounter";active=true;return true;
    }catch(...){detail="Omega Ultima package preparation failed; original encounters retained";return false;}
}
void Stop() noexcept {active=false;Bridge::Unregister(&provider);detail="Omega Ultima is OFF";}
bool Active() noexcept {return active.load();}
const char* Detail() noexcept {return detail.load();}
#ifdef FFXHOOKS_TESTING
void SetProvidersForTests(AssetReader reader,Random random) noexcept {assetReader=reader;testRandom=random;}
#endif
} // namespace FfxHooks::OmegaUltima::Runtime
