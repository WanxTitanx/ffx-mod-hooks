// Jarvis-HOOK: real filesystem and production serializers, no game entrypoint.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include <cstdio>
#if __has_include("../hooks/FahrenheitManagedIo.inl")
#include "../hooks/FahrenheitServices.h"
#include "../hooks/RonsoPoolRuntime.h"
#include "../hooks/RonsoPoolSave.h"
#include "../hooks/NativeSaveEvents.h"
#include "PrivatePeFixture.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
using namespace FfxHooks;
using namespace FfxHooks::RonsoPool;
namespace {
int checks=0,failed=0,readEvents=0,writeEvents=0,prepared=0,finished=0,aborted=0,staged=0,verified=0;
unsigned readStarts=0,readRejections=0;
void ReadStarting(const unsigned char*) noexcept {++readStarts;}
void ReadRejected() noexcept {++readRejections;}
bool rejectMetadata=false;
wchar_t pendingExitMarker[4096]{};
unsigned char cookie;
void Check(bool value,const char* label){++checks;if(!value){++failed;std::printf("FAIL: %s\n",label);}}
void OnRead(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {++readEvents;}
void OnWrite(const wchar_t*,const unsigned char*,std::size_t) noexcept {++writeEvents;}
bool Project(const wchar_t*,const unsigned char*,unsigned char* output,std::size_t,void** state) noexcept {
    output[0x5630]=0x45;*state=&cookie;return true;
}
bool Prepare(void*,const wchar_t*,const unsigned char*,std::size_t) noexcept {++prepared;return !rejectMetadata;}
void Finish(void*,const unsigned char*,std::size_t,bool success) noexcept {
    if(success)++finished;else ++aborted;
    if(pendingExitMarker[0]){
        HANDLE file=CreateFileW(pendingExitMarker,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    }
}
void Staged(std::uint64_t,const wchar_t*,const unsigned char*,std::size_t) noexcept {++staged;}
void Verified(std::uint64_t,const wchar_t*,const unsigned char*,std::size_t) noexcept {++verified;}
void Aborted(std::uint64_t) noexcept {}
bool Put(const std::wstring& path,const SaveImage& bytes){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;DWORD written=0;
    const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
    return CloseHandle(file)&&ok;
}
bool Get(const std::wstring& path,SaveImage& bytes){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;DWORD read=0;
    const bool ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size();
    return CloseHandle(file)&&ok;
}
bool Read(const std::wstring& path,SaveImage& target,const SaveImage& disk,bool crcClear=true){
    const auto ticket=FfxHooks_FahrenheitBeginReadV2(path.c_str(),1000,target.data(),static_cast<std::uint32_t>(target.size()));
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    const bool transformed=ticket&&file!=INVALID_HANDLE_VALUE&&FfxHooks_FahrenheitTransformReadV2(ticket,reinterpret_cast<std::uintptr_t>(file),disk.data(),static_cast<std::uint32_t>(disk.size()));
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(!transformed){if(ticket)FfxHooks_FahrenheitAbortIoV2(ticket);return false;}
    if(crcClear)std::memset(target.data()+25844,0,4);
    return FfxHooks_FahrenheitEndReadV2(ticket,1)==1;
}
}
int main(int argc,char** argv){
    if(argc!=5)return 2;
    const bool gameplay=std::strcmp(argv[4],"on")==0;
    HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!image||!PrivatePeFixture::NormalizeRelocations(image))return 2;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    const auto root=std::filesystem::path(argv[3]);std::filesystem::create_directories(root);
    const auto path=(root/L"ffx_1000").wstring(),temporary=(root/L".owned-save.tmp").wstring();
    const auto other=(root/L"ffx_1001").wstring(),storePath=(root/L"metadata").wstring();
    SaveImage disk{},target{},output{},readback{};
    std::ifstream input(argv[2],std::ios::binary);if(!input.read(reinterpret_cast<char*>(disk.data()),disk.size()))return 2;
    auto& state=Coexistence::runtime;state.Observe(true);
    Check(state.ConfigureServices(2,4)&&state.Ready(1,3)&&state.TryStart(),"V2 save ownership is negotiated before producer setup");
    static const NativeSaveEvents::Observer observer=[](){NativeSaveEvents::Observer v{OnRead,OnWrite,nullptr};
        v.project=Project;v.prepare=Prepare;v.finish=Finish;v.writeStaged=Staged;v.writeVerified=Verified;v.writeAborted=Aborted;
        v.readStarting=ReadStarting;v.rejectRead=ReadRejected;return v;}();
    Check(NativeSaveEvents::Subscribe(&observer),"existing observer registry admits the negotiated transport");
    std::array<unsigned char,4> oldRead{},oldWrite{},oldClose{};
    std::memcpy(oldRead.data(),reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x70C3F4>()),4);
    std::memcpy(oldWrite.data(),reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x70C428>()),4);
    std::memcpy(oldClose.data(),reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x70C3F0>()),4);
    PreparedRuntime runtime{};
    Check(PrepareRuntime(base,gameplay,nullptr,&runtime,storePath.c_str()),"same profiled Ronso serialization owner prepares for managed I/O");
    Check(InstallIoImports(),"managed transport publishes readiness without touching CRT imports");
    ActivateRuntime();Check(state.Finish(true)&&IsVerifiedSaveIoReady(),"managed save verification is available after activation");
    Check(std::memcmp(oldRead.data(),reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x70C3F4>()),4)==0&&
          std::memcmp(oldWrite.data(),reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x70C428>()),4)==0&&
          std::memcmp(oldClose.data(),reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x70C3F0>()),4)==0,"all three native IAT cells remain byte-for-byte unchanged");
    Check(Put(path,disk)&&Put(other,disk),"owned input fixtures are written");
    Check(Read(path,target,disk)&&readEvents==1,"actual managed read reaches existing observers once after CRC admission");
    Check(target[kSaveMaximum]==(gameplay?200:disk[kSaveMaximum]),"Ronso conversion follows the independent gameplay flag");
    SealSave(target);const auto originalTarget=target;
    Check(FfxHooks_FahrenheitBeginWriteV2(path.c_str(),999,target.data(),kSaveSize,output.data())==0,"mismatched final slot is rejected before preparation");
    rejectMetadata=true;
    Check(FfxHooks_FahrenheitBeginWriteV2(path.c_str(),1000,target.data(),kSaveSize,output.data())==0&&Get(path,readback)&&readback==disk,"metadata rejection leaves the primary unchanged");
    rejectMetadata=false;
    const auto ticket=FfxHooks_FahrenheitBeginWriteV2(path.c_str(),1000,target.data(),kSaveSize,output.data());
    Check(ticket&&target==originalTarget&&output[0x5630]==0x45&&IsValidSave(output),"shared projections produce a sealed separate image without editing RAM");
    Check(FfxHooks_FahrenheitBeginWriteV2(path.c_str(),1000,target.data(),kSaveSize,readback.data())==0,"overlapping transaction cannot replace an active ticket");
    int stolen=1;std::thread stranger([&]{stolen=FfxHooks_FahrenheitEndWriteV2(ticket,1);});stranger.join();
    Check(stolen==0,"a foreign thread cannot complete or consume the ticket");
    HANDLE handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(handle!=INVALID_HANDLE_VALUE&&FfxHooks_FahrenheitOpenWriteV2(ticket,reinterpret_cast<std::uintptr_t>(handle))==1,"temp file identity is bound before the only primary image write");
    DWORD written=0;bool wrote=WriteFile(handle,output.data(),static_cast<DWORD>(output.size()),&written,nullptr)&&written==output.size();
    wrote=FlushFileBuffers(handle)&&CloseHandle(handle)&&wrote;
    Check(wrote&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"closed temporary replaces the intended primary exactly once");
    Check(FfxHooks_FahrenheitEndWriteV2(ticket,1)==1&&writeEvents==1&&finished==1&&staged==1&&verified==1,"exact close/readback commits each existing observer once");
    Check(FfxHooks_FahrenheitEndWriteV2(ticket,1)==0&&writeEvents==1,"stale completion cannot publish twice");
    Check(Get(path,disk)&&Read(path,target,disk),"saved projected bytes reload through the same canonical owner");
    const auto startsBefore=readStarts,rejectionsBefore=readRejections;
    const auto wrong=FfxHooks_FahrenheitBeginReadV2(path.c_str(),1000,target.data(),kSaveSize);
    handle=CreateFileW(other.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(wrong&&FfxHooks_FahrenheitTransformReadV2(wrong,reinterpret_cast<std::uintptr_t>(handle),disk.data(),kSaveSize)==0,"same bytes under a different path cannot forge read provenance");
    CloseHandle(handle);Check(FfxHooks_FahrenheitAbortIoV2(wrong)==1,"failed read has one bounded abort");
    Check(readStarts==startsBefore+1&&readRejections==rejectionsBefore+1,"failed issued read invalidates its buffer and rejects once each");
    const auto cancel=FfxHooks_FahrenheitBeginReadV2(path.c_str(),1000,target.data(),kSaveSize);
    Check(cancel&&FfxHooks_FahrenheitCancelReadV2(target.data(),kSaveSize)==1&&FfxHooks_FahrenheitEndReadV2(cancel,1)==0,"cancel invalidates any pending generation for the reused buffer");
    Check(Read(path,target,disk),"a later valid read establishes fresh provenance after failure");
    if(std::strcmp(argv[4],"pending-exit")==0){
        SealSave(target);
        const auto pending=FfxHooks_FahrenheitBeginWriteV2(path.c_str(),1000,target.data(),kSaveSize,output.data());
        Check(pending!=0,"an issued transaction may outlive its caller at process exit");
        wcscpy_s(pendingExitMarker,(root/L"unexpected-exit-finalization.txt").c_str());
        // Parent checks the marker after CRT global destruction has completed.
        std::printf("Fahrenheit pending-exit RT1: %d checks, %d failures\n",checks,failed);
        return failed?1:0;
    }
    state.Stop();Check(FfxHooks_FahrenheitBeginReadV2(path.c_str(),1000,target.data(),kSaveSize)==0,"terminal stop cannot admit another managed transaction");
    std::printf("Fahrenheit managed I/O RT1 %s: %d checks, %d failures\n",argv[4],checks,failed);
    return failed?1:0;
}
#else
int main(){std::puts("FAIL: managed transport is not connected to production save serialization");return 1;}
#endif
