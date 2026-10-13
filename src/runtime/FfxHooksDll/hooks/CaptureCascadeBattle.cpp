#include "CaptureCascadeBattle.h"
#include "CaptureCascadeBattleCore.h"
#include "CaptureCascadeCatalog.generated.h"
#include "CaptureCascadeRuntime.h"
#include "CustomMixWindowsAdapter.h"
#include "NativeUiHookSupport.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

namespace FfxHooks::CaptureCascade::Battle {
namespace {
static_assert(std::atomic<bool>::is_always_lock_free,"Pinned views outlive lock-free logical teardown");
std::atomic<bool> available{false};
std::atomic<bool> nativeAssets{false};
void(*logger)(const char*)=nullptr;
void(*warning)()=nullptr;
struct Frame {std::vector<unsigned char> bytes;Selection selection;std::uint64_t originalHash=0;};
// Native ATEL/formation/geometry pointers may outlive InitScene. Immutable views
// are cached and pinned, never freed from a scene callback or loader-lock Stop.
std::array<Frame*,256> frames{};
unsigned frameCount=0;
thread_local bool inside=false;
void Log(const char* message) noexcept {if(logger)try{logger(message);}catch(...){}}
std::uint32_t Dword(const unsigned char* p) noexcept {return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
bool Read(std::uintptr_t p,void* output,std::size_t size) noexcept {return NativeUiSupport::Copy(output,reinterpret_cast<const void*>(p),size);}
bool Write(std::uintptr_t p,const void* input,std::size_t size) noexcept {return NativeUiSupport::Copy(reinterpret_cast<void*>(p),input,size);}
struct Loan {std::uintptr_t base=0;std::uint32_t root=0,view=0;std::uint16_t size=0;const Frame* frame=nullptr;bool entered=false,restored=false;};
bool Exchange(Loan& loan,bool enter) noexcept {
    using namespace CustomMixUltra::WindowsAdapter;
    std::uint32_t pointer=0;std::uint16_t size=0;
    if(!Read(loan.base+kCarrierPointerRva,&pointer,4)||!Read(loan.base+kCarrierSizeRva,&size,2))return false;
    const auto expectedPointer=enter?loan.root:loan.view;
    const auto expectedSize=enter?loan.size:static_cast<std::uint16_t>(loan.frame->bytes.size());
    if(pointer!=expectedPointer||size!=expectedSize)return false;
    const auto next=enter?loan.view:loan.root;
    const auto nextSize=enter?static_cast<std::uint16_t>(loan.frame->bytes.size()):loan.size;
    if(!Write(loan.base+kCarrierPointerRva,&next,4))return false;
    if(!Write(loan.base+kCarrierSizeRva,&nextSize,2)){
        Write(loan.base+kCarrierPointerRva,&expectedPointer,4);return false;
    }
    return true;
}
bool References(const Loan& loan) noexcept {
    const auto* bytes=loan.frame->bytes.data();
    const std::uintptr_t rvas[]={ExecutableProfile::Rva<0xD2A9AC>(),ExecutableProfile::Rva<0xD2A9B4>(),ExecutableProfile::Rva<0xD2A9C0>(),ExecutableProfile::Rva<0xD2A9B0>()};
    for(unsigned i=0;i<4;++i){std::uint32_t pointer=0;
        if(!Read(loan.base+rvas[i],&pointer,4)||pointer!=loan.view+Dword(bytes+4+i*4))return false;
    }
    return true;
}
bool Invoke(Loan* loan,void* context,bool(*original)(void*,int*),int* result) noexcept {
    bool returned=false;
    __try {returned=original(context,result);}
    __finally {loan->restored=Exchange(*loan,false);}
    return returned;
}
bool Asset(const std::filesystem::path& path,const Catalog::Monster& monster){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};DWORD count=0;
    const bool shape=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
        !info.nFileSizeHigh&&info.nFileSizeLow==monster.size;
    std::vector<unsigned char> bytes(shape?monster.size:0);
    const bool read=shape&&ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();
    CloseHandle(file);return read&&FormationHash(bytes.data(),bytes.size())==monster.hash;
}
struct Stream {HANDLE file=INVALID_HANDLE_VALUE;void* archive=nullptr;};
static_assert(sizeof(Stream)==8,"Native stream extent");
bool NativeAsset(std::uintptr_t base,const Catalog::Monster& monster,unsigned char* bytes) noexcept {
    Stream stream;bool opened=false,valid=false;
    char path[160]{};std::snprintf(path,sizeof(path),"../../../ffx_ps2/ffx/master/jppc/battle/mon/_m%03u/m%03u.bin",monster.destination,monster.destination);
    __try {
        using Create=Stream*(__thiscall*)(Stream*,const char*,int,unsigned,unsigned,int);
        using Size=std::uint32_t(__thiscall*)(Stream*);
        using Read=std::uint32_t(__thiscall*)(Stream*,void*,std::uint32_t);
        opened=reinterpret_cast<Create>(base+ExecutableProfile::Rva<0x207D80>())(&stream,path,1,0,0,1)==&stream;
        if(opened&&(stream.file!=INVALID_HANDLE_VALUE||stream.archive)&&
           reinterpret_cast<Size>(base+ExecutableProfile::Rva<0x207F80>())(&stream)==monster.size)
            valid=reinterpret_cast<Read>(base+ExecutableProfile::Rva<0x208250>())(&stream,bytes,monster.size)==monster.size;
    }__except(EXCEPTION_EXECUTE_HANDLER){valid=false;}
    if(opened)__try{reinterpret_cast<bool(__thiscall*)(Stream*)>(base+ExecutableProfile::Rva<0x207F40>())(&stream);}__except(EXCEPTION_EXECUTE_HANDLER){valid=false;}
    return valid&&FormationHash(bytes,monster.size)==monster.hash;
}
}
bool Initialize(const std::filesystem::path& game,void(*log)(const char*),void(*notify)()) noexcept {
    if(available)return true;
    logger=log;warning=notify;
    try{
        for(const auto& monster:Catalog::monsters){
            wchar_t folder[16]{},name[16]{};std::swprintf(folder,16,L"_m%03u",monster.destination);std::swprintf(name,16,L"m%03u.bin",monster.destination);
            const auto path=game/L"data"/L"mods"/L"ffx_ps2"/L"ffx"/L"master"/L"jppc"/L"battle"/L"mon"/folder/name;
            if(!Asset(path,monster)){Log("[ffx-hooks] Capture Cascade: missing or changed patrol asset; feature remains unavailable\n");return false;}
        }
        available=true;return true;
    }catch(...){return false;}
}
bool Ready() noexcept {return available.load();}
bool AdmitNativeAssets(std::uintptr_t base) noexcept {
    if(!available)return false;
    if(nativeAssets)return true;
    try{
        // Verify the provider-owned read chain on the field thread. Loose files
        // alone cannot prove that the active EFL/VBF loader serves the new IDs.
        const std::uintptr_t rvas[]={ExecutableProfile::Rva<0x207D80>(),ExecutableProfile::Rva<0x207F40>(),ExecutableProfile::Rva<0x207F80>(),ExecutableProfile::Rva<0x208250>()};
        const unsigned char expected[][10]={{0x55,0x8b,0xec,0x8b,0x45,0x08,0x56,0x8b,0xf1,0xc7},{0x56,0x8b,0xf1,0x8b,0x4e,0x04,0x85,0xc9,0x74,0x0c},{0x55,0x8b,0xec,0x8b,0x41,0x04,0x83,0xec,0x08,0x85},{0x55,0x8b,0xec,0x51,0x8b,0x41,0x04,0x85,0xc0,0x74}};
        for(unsigned i=0;i<4;++i){unsigned char bytes[10]{};if(!Read(base+rvas[i],bytes,10)||std::memcmp(bytes,expected[i],10)){available=false;return false;}}
        for(const auto& monster:Catalog::monsters){std::vector<unsigned char> bytes(monster.size);if(!NativeAsset(base,monster,bytes.data())){available=false;Log("[ffx-hooks] Capture Cascade: native loader did not serve the exact patrol assets; awakening disabled\n");return false;}}
        nativeAssets=true;return true;
    }catch(...){available=false;return false;}
}
void Stop() noexcept {available=false;nativeAssets=false;}
bool Run(std::uintptr_t base,void* context,bool(*original)(void*,int*),int* result) noexcept {
    if(!original||!result)return false;
    if(!Ready()||!nativeAssets||!Runtime::Ready()||inside)return original(context,result);
    SinNatural::Evidence evidence;std::uint32_t entropy=0;
    if(!Runtime::ConsumeNatural(evidence,entropy))return original(context,result);
    const Catalog::Formation* proof=nullptr;
    for(const auto& entry:Catalog::formations)if(entry.field==static_cast<unsigned>(evidence.field)&&entry.group==static_cast<unsigned>(evidence.group)&&entry.formation==evidence.formation){proof=&entry;break;}
    if(!proof||!proof->mask)return original(context,result);
    using namespace CustomMixUltra::WindowsAdapter;
    std::uint32_t root=0;std::uint16_t size=0;char name[10]{};
    if(!Read(base+kCarrierPointerRva,&root,4)||!Read(base+kCarrierSizeRva,&size,2)||!Read(base+kCarrierNameRva,name,sizeof(name))||
       !root||size!=proof->size||name[9]||std::strcmp(name,proof->name))return original(context,result);
    State state;std::uint64_t session=0;
    if(!Runtime::Snapshot(state,session))return original(context,result);
    const auto region=Catalog::RegionForField(proof->field);const auto family=static_cast<unsigned>(region);
    if(family>=kFamilies)return original(context,result);
    Frame* selected=nullptr;
    try{
        std::vector<unsigned char> source(size);if(!Read(root,source.data(),source.size()))return original(context,result);
        auto candidate=std::make_unique<Frame>();Config config;config.enabled=true;
        const FormationProof description{size,proof->slotsOffset,proof->hash,static_cast<std::uint8_t>(proof->mask)};
        const auto& pool=Catalog::candidates[family];
        const auto built=BuildFormation(description,source.data(),source.size(),config,state,region,pool.data(),pool.size(),entropy,candidate->bytes,candidate->selection);
        if(built!=FormationCode::Prepared)return original(context,result);
        candidate->originalHash=proof->hash;
        for(unsigned i=0;i<frameCount;++i)if(frames[i]->originalHash==candidate->originalHash&&frames[i]->bytes==candidate->bytes){selected=frames[i];break;}
        if(!selected){
            if(frameCount==frames.size()){Log("[ffx-hooks] Capture Cascade: immutable view cache full; native encounter retained\n");return original(context,result);}
            selected=candidate.release();frames[frameCount++]=selected;
        }
    }catch(...){return original(context,result);}
    Loan loan{base,root,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(selected->bytes.data())),size,selected};
    if(!Exchange(loan,true))return original(context,result);
    loan.entered=true;inside=true;
    const bool returned=Invoke(&loan,context,original,result);
    inside=false;
    if(!loan.restored){available=false;Log("[ffx-hooks] Capture Cascade: resource ownership changed; further replacement stopped\n");}
    if(returned&&loan.restored&&References(loan)){
        if(warning)warning();
        char text[180]{};std::snprintf(text,sizeof(text),"[ffx-hooks] Capture Cascade: patrol formation=%s groups=%u actor-mask=0x%02X; native resource restored\n",proof->name,selected->selection.groups,selected->selection.changed);Log(text);
    }
    return returned;
}
}
