#include "EquipmentWorkshopStore.h"
#include "RonsoPoolStore.h"
#include "RonsoPoolSave.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>
#include <vector>

namespace FfxHooks::EquipmentWorkshop {
namespace {
constexpr unsigned char magic[8]={'F','F','X','W','K','S','0','1'};
#pragma pack(push,1)
struct Record {unsigned char magic[8];Hash pathHash,imageHash,stateHash;workshop::State state;};
#pragma pack(pop)
std::atomic<unsigned> sequence{1};
bool Digest(const void* data,std::size_t size,Hash& out){
    if(!data || size>UINT32_MAX)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    DWORD objectSize=0,returned=0;
    bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&returned,0)>=0 && objectSize>0 && objectSize<=65536;
    std::vector<unsigned char> object(ok?objectSize:0);
    if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(size),0)>=0 && BCryptFinishHash(hash,out.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
bool Keys(const std::wstring& path,const SaveImage& image,Hash& pathHash,Hash& imageHash){
    if(!RonsoPool::OwnerStore::IsSavePath(path)||path.size()>4096)return false;
    std::wstring normalized(path.size(),L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path.data(),static_cast<int>(path.size()),normalized.data(),static_cast<int>(normalized.size()),nullptr,nullptr,0))return false;
    return Digest(normalized.data(),normalized.size()*sizeof(wchar_t),pathHash)&&Digest(image.data(),image.size(),imageHash);
}
std::wstring Hex(const Hash& hash){
    static constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring out;
    for(unsigned i=0;i<16;++i){out.push_back(digits[hash[i]>>4]);out.push_back(digits[hash[i]&15]);}return out;
}
bool MaterialCounts(const SaveImage& image,std::array<std::uint16_t,112>& out){
    std::array<bool,112> seen{};out.fill(0);
    for(unsigned slot=0;slot<256;++slot){const unsigned at=0x3F0C+2*slot;
        const unsigned id=image[at]|(unsigned(image[at+1])<<8);
        if(id>=0x2000 && id<0x2070){const auto item=id-0x2000;if(seen[item])return false;seen[item]=true;out[item]=image[0x410C+slot];}
    }
    return true;
}
StoreResult ReadLeaf(const std::wstring& path,Record& record){
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?StoreResult::Missing:StoreResult::Unavailable;}
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return StoreResult::Invalid;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return StoreResult::Unavailable;
    LARGE_INTEGER length{};DWORD read=0;
    const bool ok=GetFileSizeEx(file,&length)&&length.QuadPart==sizeof(record)&&ReadFile(file,&record,sizeof(record),&read,nullptr)&&read==sizeof(record);
    CloseHandle(file);return ok?StoreResult::Found:StoreResult::Invalid;
}
}
bool Fingerprint(const void* bytes,std::size_t size,Hash& out){return Digest(bytes,size,out);}
bool ImportSave(const SaveImage& image,std::uint64_t seed,workshop::State& out){
    std::array<std::uint16_t,112> items{};
    return RonsoPool::IsValidSave(image)&&MaterialCounts(image,items)&&
        workshop::Import(image.data()+0x44DC,items.data(),seed,out)==workshop::Error::Ok;
}
bool MatchesSave(const SaveImage& image,const workshop::State& state){
    if(workshop::Validate(state)!=workshop::Error::Ok)return false;
    std::array<std::uint16_t,112> items{};if(!MaterialCounts(image,items))return false;
    if(std::memcmp(items.data(),state.items,sizeof(state.items))!=0)return false;
    for(unsigned i=0;i<200;++i)if(std::memcmp(state.pieces[i].native,image.data()+0x44DC+22*i,22)!=0)return false;
    return true;
}
bool Store::Initialize(const std::wstring& directory,bool create){
    directory_.clear();if(directory.empty())return false;
    DWORD attrs=GetFileAttributesW(directory.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES && create){
        if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        attrs=GetFileAttributesW(directory.c_str());
    }
    if(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    directory_=directory;return true;
}
std::wstring Store::RecordPath(const std::wstring& path,const SaveImage& image) const {
    if(directory_.empty())return {};
    Hash pathHash{},imageHash{};if(!Keys(path,image,pathHash,imageHash))return {};
    return directory_+L"\\"+Hex(pathHash)+L"-"+Hex(imageHash)+L".bin";
}
StoreResult Store::Read(const std::wstring& path,const SaveImage& image,workshop::State& state) const {
    Hash diskHash{};if(!Digest(image.data(),image.size(),diskHash))return StoreResult::Unavailable;
    return ReadLoaded(path,diskHash,image,state);
}
StoreResult Store::ReadLoaded(const std::wstring& path,const Hash& diskHash,const SaveImage& image,workshop::State& state) const {
    Hash pathHash{},ignored{},stateHash{};
    if(directory_.empty() || !Keys(path,image,pathHash,ignored))return StoreResult::Unavailable;
    const auto leaf=directory_+L"\\"+Hex(pathHash)+L"-"+Hex(diskHash)+L".bin";
    Record record{};const auto result=ReadLeaf(leaf,record);if(result!=StoreResult::Found)return result;
    if(!Digest(&record.state,sizeof(record.state),stateHash)||
       std::memcmp(record.magic,magic,8)!=0 || record.pathHash!=pathHash || record.imageHash!=diskHash ||
       record.stateHash!=stateHash || !MatchesSave(image,record.state))return StoreResult::Invalid;
    state=record.state;return StoreResult::Found;
}
bool Store::Write(const std::wstring& path,const SaveImage& image,const workshop::State& state) const {
    if(!RonsoPool::IsValidSave(image)||!MatchesSave(image,state))return false;
    const auto leaf=RecordPath(path,image);if(leaf.empty())return false;
    workshop::State previous{};const auto prior=Read(path,image,previous);
    if(prior!=StoreResult::Found && prior!=StoreResult::Missing)return false;
    if(prior==StoreResult::Found && std::memcmp(&previous,&state,sizeof(state))==0)return true;
    Record record{};std::memcpy(record.magic,magic,8);record.state=state;
    if(!Keys(path,image,record.pathHash,record.imageHash)||!Digest(&record.state,sizeof(record.state),record.stateHash))return false;
    const auto temporary=leaf+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,&record,sizeof(record),&written,nullptr)&&written==sizeof(record)&&FlushFileBuffers(file);
    CloseHandle(file);
    if(ok && prior==StoreResult::Found)ok=CopyFileW(leaf.c_str(),(leaf+L".bak").c_str(),FALSE)!=FALSE;
    if(ok)ok=MoveFileExW(temporary.c_str(),leaf.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temporary.c_str());return false;}
    workshop::State verified{};return Read(path,image,verified)==StoreResult::Found && std::memcmp(&verified,&state,sizeof(state))==0;
}
}
