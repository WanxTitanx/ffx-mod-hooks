#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "../hooks/RonsoPoolRuntime.h"
#include "../hooks/RonsoPoolSave.h"
#include "../hooks/RonsoPoolStore.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/RonsoPoolEvidence.h"
#include "PrivatePeFixture.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <atomic>
#include <cstdlib>
#include <new>
namespace AllocationFailureFixture {
std::atomic<bool> armed{false};
std::atomic<std::size_t> rejected{0};
}
// Test-executable allocator; production keeps its actual make_unique and IO path.
void* operator new(std::size_t size){
    if(size>=3*FfxHooks::RonsoPool::kSaveSize&&AllocationFailureFixture::armed.exchange(false)){
        AllocationFailureFixture::rejected=size;throw std::bad_alloc();
    }
    if(void* result=std::malloc(size?size:1))return result;
    throw std::bad_alloc();
}
void operator delete(void* address) noexcept {std::free(address);}
void operator delete(void* address,std::size_t) noexcept {std::free(address);}
#include <filesystem>
using namespace FfxHooks::RonsoPool;
namespace {
using OpenFn=void*(__cdecl*)(const wchar_t*,const wchar_t*);
using CloseFn=int(__cdecl*)(void*);
using ReadFn=size_t(__cdecl*)(void*,size_t,size_t,void*);
int checks=0,failures=0,resets=0;
unsigned observedReads=0,observedWrites=0;
unsigned rejectedReadResets=0;
void ObservedReset() noexcept {++rejectedReadResets;}
unsigned diskMaximum=0,loadedMaximum=0,writtenMaximum=0;
constexpr std::size_t projectionOffset=0x5630;
std::uint32_t projectionBaseline=0;
unsigned projectionPrepared=0,projectionCommitted=0,projectionAborted=0;
bool rejectProjectionMetadata=false;
unsigned char projectionCookie=0;
bool ProjectTemporary(const wchar_t*,const unsigned char*,unsigned char* output,std::size_t size,void** cookie) noexcept {
    if(size!=kSaveSize)return false;
    *cookie=&projectionCookie;std::memcpy(output+projectionOffset,&projectionBaseline,4);return true;
}
bool PrepareProjection(void* cookie,const wchar_t*,const unsigned char* bytes,std::size_t size) noexcept {
    SaveImage image{};if(cookie!=&projectionCookie||size!=image.size())return false;
    std::memcpy(image.data(),bytes,size);
    if(IsValidSave(image))++projectionPrepared;
    return !rejectProjectionMetadata;
}
void FinishProjection(void* cookie,const unsigned char*,std::size_t,bool success) noexcept {
    if(cookie!=&projectionCookie)return;
    if(success)++projectionCommitted;else ++projectionAborted;
}
bool NeedsProjection() noexcept {return true;}
void ObservedRead(const wchar_t*,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    if(size==kSaveSize){++observedReads;diskMaximum=disk[kSaveMaximum];loadedMaximum=loaded[kSaveMaximum];}
}
void ObservedWrite(const wchar_t*,const unsigned char* actual,std::size_t size) noexcept {
    if(size==kSaveSize){++observedWrites;writtenMaximum=actual[kSaveMaximum];}
}
uintptr_t base=0;
OpenFn openFile=nullptr;CloseFn closeFile=nullptr;
void Expect(bool ok,const char* why){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
bool Patch(uintptr_t address,const void* data,size_t size) {
    DWORD prior=0;if(!VirtualProtect(reinterpret_cast<void*>(address),size,PAGE_EXECUTE_READWRITE,&prior))return false;
    std::memcpy(reinterpret_cast<void*>(address),data,size);DWORD ignored=0;
    const bool ok=VirtualProtect(reinterpret_cast<void*>(address),size,prior,&ignored)!=FALSE;
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),size);return ok;
}
int __cdecl ResetProvider(){++resets;return 42;}
__declspec(naked) size_t __cdecl NativeWrite(size_t,void*,void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push esi
        push edi
        mov edi,[ebp+0Ch]
        mov esi,[ebp+10h]
        call dword ptr [ebp+14h]
        pop edi
        pop esi
        pop ebp
        ret
    }
}
__declspec(naked) size_t __cdecl NativeRead(void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push esi
        mov esi,[ebp+8]
        call dword ptr [ebp+0Ch]
        pop esi
        pop ebp
        ret
    }
}
bool Put(const std::wstring& path,const SaveImage& bytes) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD count=0;const bool ok=WriteFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();
    CloseHandle(f);return ok;
}
bool Disk(const std::wstring& path,SaveImage* bytes) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD count=0;const bool ok=ReadFile(f,bytes->data(),static_cast<DWORD>(bytes->size()),&count,nullptr)&&count==bytes->size();
    CloseHandle(f);return ok;
}
std::wstring Canonical(const std::wstring& path) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return {};
    wchar_t full[4096]={};const DWORD length=GetFinalPathNameByHandleW(f,full,4096,FILE_NAME_NORMALIZED);
    CloseHandle(f);return length&&length<4096?std::wstring(full,length):std::wstring();
}
bool GameRead(const std::wstring& path,SaveImage* bytes) {
    void* file=openFile(path.c_str(),L"rb");if(!file)return false;
    uint32_t count=static_cast<uint32_t>(bytes->size());void* data=bytes->data();
    Patch(base+::FfxHooks::ExecutableProfile::Rva<0x8E72F4>(),&count,4);Patch(base+::FfxHooks::ExecutableProfile::Rva<0x8E72F8>(),&data,4);
    const size_t got=NativeRead(file,reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x2F0213>()));closeFile(file);return got==count;
}
bool GameWrite(const std::wstring& path,SaveImage* bytes) {
    void* file=openFile(path.c_str(),L"wb");if(!file)return false;
    const size_t got=NativeWrite(bytes->size(),bytes->data(),file,reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x2F06B8>()));
    closeFile(file);return got==bytes->size();
}
}
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    const bool active=std::strcmp(argv[4],"on")==0;
    const auto crtPath=std::filesystem::path(argv[1]).parent_path()/L"msvcr110.dll";
    HMODULE game=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES),crt=LoadLibraryW(crtPath.c_str());
    if(!game||!crt)return 2;base=reinterpret_cast<uintptr_t>(game);
    Expect(PrivatePeFixture::NormalizeRelocations(game),"private image uses runtime HIGHLOW relocation semantics on Windows and Wine");
    if(failures)return 2;
    const auto read=GetProcAddress(crt,"fread"),write=GetProcAddress(crt,"fwrite");
    openFile=reinterpret_cast<OpenFn>(GetProcAddress(crt,"_wfopen"));closeFile=reinterpret_cast<CloseFn>(GetProcAddress(crt,"fclose"));
    if(!read||!write||!openFile||!closeFile)return 2;
    Patch(base+::FfxHooks::ExecutableProfile::Rva<0x70C3F4>(),&read,4);Patch(base+::FfxHooks::ExecutableProfile::Rva<0x70C428>(),&write,4);
    const std::wstring root(argv[3],argv[3]+std::strlen(argv[3]));
    const std::wstring file=root+L"\\ffx_000",foreign=root+L"\\ffx_001",resetSave=root+L"\\ffx_002";
    PreparedRuntime prepared{};
    const auto observer=[](){FfxHooks::NativeSaveEvents::Observer value{ObservedRead,ObservedWrite,ObservedReset};
        value.rejectRead=ObservedReset;return value;}();
    Expect(FfxHooks::NativeSaveEvents::Subscribe(&observer),"passive save subscriber registered before producer installation");
    const bool preparedOk=PrepareRuntime(base,active,nullptr,&prepared,root.c_str());
    if(!preparedOk){
        MEMORY_BASIC_INFORMATION info{};VirtualQuery(reinterpret_cast<void*>(base),&info,sizeof(info));
        std::printf("PREPARE_DIAGNOSTIC base=%p type=%lx state=%lx directoryAttributes=%lx error=%lu\n",game,info.Type,info.State,GetFileAttributesW(root.c_str()),GetLastError());
        for(const auto& span:Evidence::kSpans){
            VirtualQuery(reinterpret_cast<void*>(base+span.rva),&info,sizeof(info));
            const auto* bytes=reinterpret_cast<const unsigned char*>(base+span.rva);
            if(!Evidence::Matches(span,bytes,span.size,base)||info.Type!=MEM_IMAGE)
                std::printf("PROFILE_DIAGNOSTIC rva=%lx type=%lx protection=%lx matches=%d preferred=%d\n",static_cast<unsigned long>(span.rva),info.Type,info.Protect,Evidence::Matches(span,bytes,span.size,base),Evidence::Matches(span,bytes,span.size,0x400000));
        }
    }
    Expect(preparedOk,"production save adapter prepared in private image");
    Expect(InstallIoImports(),"only the two FFX import cells are intercepted");
    const uint8_t returnAfterCall[]={0x83,0xC4,0x10,0xC3};
    Patch(base+::FfxHooks::ExecutableProfile::Rva<0x2F0228>(),returnAfterCall,sizeof(returnAfterCall));Patch(base+::FfxHooks::ExecutableProfile::Rva<0x2F06C5>(),returnAfterCall,sizeof(returnAfterCall));
    uint16_t scene=23;Patch(base+::FfxHooks::ExecutableProfile::Rva<0xD2CA90>(),&scene,2);
    ActivateRuntime();
    SaveImage original{};std::ifstream input(argv[2],std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(original.data()),original.size()))return 2;
    SaveImage image{},disk{};
    if(active) {
        Expect(Put(file,original),"private old save fixture created");
        Expect(GameRead(file,&image)&&image[kSaveCharge]==100&&image[kSaveMaximum]==200&&IsValidSave(image),
               "actual native fread caller loads100 as100/200 without granting charge");
        Expect(observedReads==1&&diskMaximum==100&&loadedMaximum==200,"save observer receives disk bytes and post-Ronso runtime bytes once");
        Expect(Disk(file,&disk)&&disk==original,"loading never edits the disk save");
        image[kSaveCharge]=150;image[kSaveMaximum]=200;SealSave(image);
        Expect(GameWrite(file,&image),"native fwrite caller saves full charge");
        Expect(observedWrites==1&&writtenMaximum==200,"save observer receives the exact successfully written pool image");
        Expect(Disk(file,&disk)&&disk[kSaveCharge]==150&&disk[kSaveMaximum]==200&&IsValidSave(disk),
               "persisted native record contains coherent150/200");
        Expect(GameRead(file,&image)&&image[kSaveCharge]==150&&image[kSaveMaximum]==200,
               "same-slot reopening recovers full charge and ownership");
        OwnerStore metadataStore;metadataStore.Initialize(root,false);
        const auto metadataPath=metadataStore.RecordPath(Canonical(file),disk);
        std::array<uint8_t,112> metadataBytes{};DWORD transferred=0;
        HANDLE metadataFile=CreateFileW(metadataPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        Expect(metadataFile!=INVALID_HANDLE_VALUE,"native save created a real bound ownership record");
        if(metadataFile!=INVALID_HANDLE_VALUE) {
            ReadFile(metadataFile,metadataBytes.data(),112,&transferred,nullptr);auto corrupted=metadataBytes;corrupted[9]^=1;
            SetFilePointer(metadataFile,0,nullptr,FILE_BEGIN);WriteFile(metadataFile,corrupted.data(),112,&transferred,nullptr);CloseHandle(metadataFile);
            Expect(GameRead(file,&image)&&image==disk,"corrupt ownership metadata cannot rewrite a loaded save");
            metadataFile=CreateFileW(metadataPath.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
            WriteFile(metadataFile,metadataBytes.data(),112,&transferred,nullptr);CloseHandle(metadataFile);
            Expect(GameRead(file,&image)&&image==disk,"a fresh verified load recovers only from valid ownership");
        }
        auto bad=original;bad[1234]^=1;
        Expect(Put(foreign,bad)&&GameRead(foreign,&image)&&image==bad&&!IsValidSave(image),
               "native invalid save remains invalid, never repaired or charged");
        Put(foreign,original);
        Expect(GameRead(foreign,&image)&&image[kSaveCharge]==100&&image[kSaveMaximum]==200,
               "switching to another valid vanilla slot does not inherit old surplus");
    } else {
        void* raw=openFile(file.c_str(),L"rb");
        auto intercepted=*reinterpret_cast<ReadFn*>(base+::FfxHooks::ExecutableProfile::Rva<0x70C3F4>());
        Expect(raw&&intercepted(image.data(),1,image.size(),raw)==image.size()&&image[kSaveMaximum]==200,
               "foreign caller is forwarded unchanged even with the same save leaf");
        Expect(observedReads==0,"foreign caller never publishes a native save event");
        if(raw)closeFile(raw);
        Expect(GameRead(file,&image)&&image[kSaveCharge]==100&&image[kSaveMaximum]==100&&IsValidSave(image),
               "fresh OFF process restores100 runtime capacity while retaining surplus");
        image[kSaveCharge]=40;SealSave(image);
        Expect(GameWrite(file,&image)&&Disk(file,&disk)&&disk[kSaveCharge]==90&&disk[kSaveMaximum]==200,
               "OFF spending preserves50 dormant plus40 visible across saving");
        auto unknown=original;unknown[kSaveCharge]=150;unknown[kSaveMaximum]=200;SealSave(unknown);
        Expect(Put(foreign,unknown)&&GameRead(foreign,&image)&&image==unknown,
               "another slot cannot borrow ownership or surplus");
        // Recreate an owned pending surplus, then exercise the production reset seam.
        // The original provider is inert; the game's reset prefix was validated above.
        Put(file,disk);GameRead(file,&image);
        const size_t resetIndex=prepared.count-1;*prepared.hooks[resetIndex].original=reinterpret_cast<void*>(&ResetProvider);
        const auto reset=reinterpret_cast<int(__cdecl*)()>(prepared.hooks[resetIndex].replacement);
        Expect(reset()==42&&resets==1,"reset clears session state and forwards native exactly once");
        image=original;image[kSaveCharge]=40;SealSave(image);
        Expect(GameWrite(resetSave,&image)&&Disk(resetSave,&disk)&&disk==image,
               "new game never inherits another save's dormant charge");
        Expect(observedWrites==2&&writtenMaximum==100,"native-only writes also publish exact successful save bytes");
    }
    const auto allocationSave=root+L"\\ffx_003";
    const auto unknownSave=root+L"\\unidentified-save.bin";
    Expect(Put(unknownSave,original)&&!GameRead(unknownSave,&image),
           "the known native save caller cannot load a full save without a verifiable save identity");
    Expect(Disk(unknownSave,&disk)&&disk==original,"identity rejection preserves the unrecognized file verbatim");
    Expect(Put(allocationSave,original),"allocation-failure case uses a separate private native save");
    const auto readsBeforeFailure=observedReads,resetsBeforeFailure=rejectedReadResets;
    AllocationFailureFixture::rejected=0;AllocationFailureFixture::armed=true;
    const bool admittedFailure=GameRead(allocationSave,&image);AllocationFailureFixture::armed=false;
    Expect(AllocationFailureFixture::rejected>=3*kSaveSize&&!admittedFailure&&observedReads==readsBeforeFailure&&rejectedReadResets==resetsBeforeFailure+1,
           "allocation failure before checkpoint selection rejects the native load and resets the subscriber, never falling through to an old save");
    Expect(Disk(allocationSave,&disk)&&disk==original,"rejected load leaves all native save bytes unchanged");
    Expect(GameRead(allocationSave,&image)&&IsValidSave(image)&&image[kSaveMaximum]==(active?200:100),
           "a fresh read after the transient allocation failure revalidates and loads normally");
    const FfxHooks::NativeSaveEvents::Observer projectionObserver{ObservedRead,ObservedWrite,nullptr,ProjectTemporary,PrepareProjection,FinishProjection,NeedsProjection};
    Expect(FfxHooks::NativeSaveEvents::Subscribe(&projectionObserver),"Arcana projection coexists with the passive save observer");
    std::memcpy(&projectionBaseline,original.data()+projectionOffset,4);
    Expect(GameRead(allocationSave,&image),"projection starts from an observed native save session");
    if(active){image[kSaveCharge]=150;image[kSaveMaximum]=200;}
    const std::uint32_t temporaryHp=54321;std::memcpy(image.data()+projectionOffset,&temporaryHp,4);SealSave(image);
    const SaveImage liveImage=image;
    const auto projectedSave=root+L"\\ffx_904";
    Expect(GameWrite(projectedSave,&image)&&image==liveImage,"native write projects a copy and preserves the live source image");
    std::uint32_t persistedHp=0;
    const bool projectedRead=Disk(projectedSave,&disk);std::memcpy(&persistedHp,disk.data()+projectionOffset,4);
    Expect(projectedRead&&persistedHp==projectionBaseline&&IsValidSave(disk),"actual CRT output removes temporary HP and carries a valid native checksum");
    if(active){OwnerStore projectedOwner;SavedOwner owner{};projectedOwner.Initialize(root,false);
        Expect(projectedOwner.Read(Canonical(projectedSave),disk,&owner)==OwnerRead::Found&&owner.charge==150,
               "Ronso ownership binds the exact projected file bytes, including temporary stat removal");}
    Expect(projectionPrepared==1&&projectionCommitted==1,"metadata prepares against sealed bytes and commits only after complete native write");
    Expect(GameRead(projectedSave,&image)&&image[kSaveMaximum]==(active?200:100),"a projected native write reloads the same Ronso ownership");
    rejectProjectionMetadata=true;
    Expect(GameWrite(projectedSave,&image)&&Disk(projectedSave,&disk)&&IsValidSave(disk),"metadata failure still preserves a complete projected vanilla save");
    Expect(projectionAborted==1&&projectionCommitted==1,"failed metadata is not reported as committed");
    FfxHooks::NativeSaveEvents::Unsubscribe(&projectionObserver);
    RequestStop();Expect(RestoreIoImports(),"retirement restores original save imports");
    FfxHooks::NativeSaveEvents::Unsubscribe(&observer);
    std::printf("RonsoPoolIoRt1 %s: %s (%d checks, %d failures)\n",active?"ON":"OFF",failures?"FAIL":"PASS",checks,failures);
    FreeLibrary(game);FreeLibrary(crt);return failures?1:0;
}
