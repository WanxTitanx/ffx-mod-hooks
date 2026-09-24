#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/RonsoPoolStore.h"
#include <cstdio>
#include <fstream>
using namespace FfxHooks::RonsoPool;
namespace {
int checks=0,failures=0;
void Expect(bool ok,const char* why) {++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
bool Bytes(const std::wstring& path,std::array<uint8_t,112>* data,bool write) {
    HANDLE f=CreateFileW(path.c_str(),write?GENERIC_WRITE:GENERIC_READ,0,nullptr,
        write?CREATE_ALWAYS:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD count=0;const bool ok=(write?WriteFile(f,data->data(),112,&count,nullptr):ReadFile(f,data->data(),112,&count,nullptr))!=FALSE&&count==112;
    CloseHandle(f);return ok;
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    SaveImage image{};std::ifstream input(argv[1],std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(image.data()),image.size()))return 2;
    image[kSaveMaximum]=200;image[kSaveCharge]=150;SealSave(image);
    wchar_t temp[MAX_PATH]={};if(!GetTempPathW(MAX_PATH,temp))return 2;
    const std::wstring parent=std::wstring(temp)+L"ffx-pool-proof-"+std::to_wstring(GetCurrentProcessId());
    if(!CreateDirectoryW(parent.c_str(),nullptr))return 2;
    const std::wstring directory=parent+L"\\owners";
    const std::wstring slot=L"C:\\Users\\Fixture\\FINAL FANTASY X\\ffx_000";
    const std::wstring other=L"C:\\Users\\Fixture\\FINAL FANTASY X\\ffx_001";
    OwnerStore readonly;
    Expect(!readonly.Initialize(directory,false)&&GetFileAttributesW(directory.c_str())==INVALID_FILE_ATTRIBUTES,
           "OFF lookup creates no directory");
    OwnerStore store;Expect(store.Initialize(directory,true),"owned metadata directory created explicitly");
    Expect(OwnerStore::IsSavePath(slot)&&!OwnerStore::IsSavePath(slot+L".bak")&&
        !OwnerStore::IsSavePath(parent+L"\\other_000"),"only canonical FFX numeric save leaves accepted");
    const SavedOwner written{100,150,200};SavedOwner read{};
    Expect(store.Read(slot,image,&read)==OwnerRead::Missing,"absent ownership is distinguished from invalid ownership");
    Expect(store.Write(slot,image,written),"metadata written after successful save");
    Expect(store.Read(slot,image,&read)==OwnerRead::Found&&read.originalMax==100&&read.charge==150,
           "valid path/content-bound ownership round-trips");
    Expect(store.Read(other,image,&read)==OwnerRead::Missing,"identical data in a different slot cannot borrow ownership");
    auto changed=image;changed[18000]^=1;SealSave(changed);
    Expect(store.Read(slot,changed,&read)==OwnerRead::Missing,"changed save cannot borrow stale metadata");
    Expect(!store.Write(slot+L".bak",image,written),"backup path cannot receive active ownership");
    const auto record=store.RecordPath(slot,image);std::array<uint8_t,112> data{};
    Expect(!record.empty()&&Bytes(record,&data,false),"exact bounded binary record can be read");
    if(!record.empty()) {
        auto damaged=data;damaged[9]=101;
        Expect(Bytes(record,&damaged,true)&&store.Read(slot,image,&read)==OwnerRead::Invalid,
               "tampered original-capacity metadata is rejected");
        Expect(Bytes(record,&data,true),"private test record restored");
        const auto otherRecord=store.RecordPath(other,image);
        Expect(Bytes(otherRecord,&data,true)&&store.Read(other,image,&read)==OwnerRead::Invalid,
               "a copied metadata record fails the full canonical path digest");
        DeleteFileW(otherRecord.c_str());
        const SavedOwner wrong{100,151,200};
        Expect(!store.Write(slot,image,wrong)&&store.Read(slot,image,&read)==OwnerRead::Found&&read.charge==150,
               "mismatched values never replace a valid record");
        Expect(store.Write(slot,image,written),"repeated identical metadata write is safe");
        HANDLE locked=CreateFileW(record.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Expect(locked!=INVALID_HANDLE_VALUE&&!store.Write(slot,image,written),
               "locked metadata cannot report a successful atomic replacement");
        if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
        Expect(store.Read(slot,image,&read)==OwnerRead::Found&&read.originalMax==100,
               "failed metadata replacement preserves the prior valid ownership");
        DeleteFileW(record.c_str());
    }
    RemoveDirectoryW(directory.c_str());RemoveDirectoryW(parent.c_str());
    std::printf("RonsoPoolStoreRt1: %s (%d checks, %d failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
