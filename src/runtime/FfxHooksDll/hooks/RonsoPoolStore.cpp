#include "RonsoPoolStore.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>
#include <vector>
namespace FfxHooks::RonsoPool {
namespace {
using Hash=std::array<uint8_t,32>;
using Record=std::array<uint8_t,112>;
constexpr uint8_t kMagic[8]={'F','F','X','R','O','N','2','0'};
std::atomic<uint32_t> nextTemporary{1};
bool Digest(const void* data,size_t length,Hash* out) {
    if(!data || !out || length>UINT32_MAX)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    DWORD objectBytes=0,actual=0;
    bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),
        sizeof(objectBytes),&actual,0)>=0 && objectBytes>0 && objectBytes<=65536;
    std::vector<uint8_t> object(ok?objectBytes:0);
    if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),objectBytes,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const uint8_t*>(data)),
        static_cast<ULONG>(length),0)>=0 && BCryptFinishHash(hash,out->data(),static_cast<ULONG>(out->size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
bool Keys(const std::wstring& path,const SaveImage& image,Hash* pathHash,Hash* imageHash) {
    if(!OwnerStore::IsSavePath(path) || path.size()>4096)return false;
    std::wstring normalized(path.size(),L'\0');
    if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path.data(),static_cast<int>(path.size()),
        normalized.data(),static_cast<int>(normalized.size()),nullptr,nullptr,0)==0)return false;
    return Digest(normalized.data(),normalized.size()*sizeof(wchar_t),pathHash)&&
        Digest(image.data(),image.size(),imageHash);
}
std::wstring ShortHex(const Hash& bytes) {
    static constexpr wchar_t digits[]=L"0123456789abcdef";
    std::wstring text;
    for(size_t i=0;i<16;++i){text.push_back(digits[bytes[i]>>4]);text.push_back(digits[bytes[i]&15]);}
    return text;
}
bool SafeLeaf(const std::wstring& path,bool* exists) {
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        const DWORD error=GetLastError();*exists=false;
        return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND;
    }
    *exists=true;return (attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))==0;
}
OwnerRead ReadRecord(const std::wstring& path,Record* out) {
    bool exists=false;if(!SafeLeaf(path,&exists))return OwnerRead::Invalid;
    if(!exists)return OwnerRead::Missing;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return OwnerRead::Unavailable;
    LARGE_INTEGER length{};DWORD bytes=0;
    const bool ok=GetFileSizeEx(file,&length)&&length.QuadPart==112&&
        ReadFile(file,out->data(),112,&bytes,nullptr)&&bytes==112;
    CloseHandle(file);return ok?OwnerRead::Found:OwnerRead::Invalid;
}
}
bool OwnerStore::IsSavePath(const std::wstring& path) noexcept {
    if(path.empty() || path.find(L'\0')!=std::wstring::npos)return false;
    const size_t slash=path.find_last_of(L"/\\");
    if(slash==std::wstring::npos)return false;
    const size_t at=slash+1;
    if(path.size()-at!=7 || _wcsnicmp(path.c_str()+at,L"ffx_",4)!=0)return false;
    for(size_t i=at+4;i<path.size();++i)if(path[i]<L'0'||path[i]>L'9')return false;
    return true;
}
bool OwnerStore::Initialize(const std::wstring& directory,bool create) {
    directory_.clear();if(directory.empty())return false;
    DWORD attributes=GetFileAttributesW(directory.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES && create) {
        if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        attributes=GetFileAttributesW(directory.c_str());
    }
    if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    directory_=directory;return true;
}
std::wstring OwnerStore::RecordPath(const std::wstring& path,const SaveImage& image) const {
    if(directory_.empty())return {};
    Hash pathHash{},imageHash{};if(!Keys(path,image,&pathHash,&imageHash))return {};
    // Full hashes are checked in the record; the128-bit filename components keep
    // ordinary Windows game-directory paths below legacy MAX_PATH.
    return directory_+L"\\"+ShortHex(pathHash)+L"-"+ShortHex(imageHash)+L".bin";
}
OwnerRead OwnerStore::Read(const std::wstring& path,const SaveImage& image,SavedOwner* owner) const {
    if(!owner)return OwnerRead::Invalid;
    *owner={};const auto name=RecordPath(path,image);if(name.empty())return OwnerRead::Unavailable;
    Record record{};const auto read=ReadRecord(name,&record);if(read!=OwnerRead::Found)return read;
    Hash pathHash{},imageHash{},integrity{};
    if(!Keys(path,image,&pathHash,&imageHash)||!Digest(record.data(),80,&integrity))return OwnerRead::Unavailable;
    if(std::memcmp(record.data(),kMagic,8)!=0 || record[8]!=1 ||
        record[9]==0 || record[9]>200 || record[10]>200 || record[11]!=200 ||
        record[12]||record[13]||record[14]||record[15]||
        std::memcmp(record.data()+16,pathHash.data(),32)!=0||
        std::memcmp(record.data()+48,imageHash.data(),32)!=0||
        std::memcmp(record.data()+80,integrity.data(),32)!=0||
        record[10]!=image[kSaveCharge]||record[11]!=image[kSaveMaximum])return OwnerRead::Invalid;
    *owner={record[9],record[10],record[11]};return OwnerRead::Found;
}
bool OwnerStore::Write(const std::wstring& path,const SaveImage& image,const SavedOwner& owner) const {
    if(!IsValidSave(image)||owner.originalMax==0||owner.originalMax>200||owner.maximum!=200||
        owner.charge>200||owner.charge!=image[kSaveCharge]||owner.maximum!=image[kSaveMaximum])return false;
    const auto name=RecordPath(path,image);if(name.empty())return false;
    Hash pathHash{},imageHash{},integrity{};Record record{};
    if(!Keys(path,image,&pathHash,&imageHash))return false;
    std::memcpy(record.data(),kMagic,8);record[8]=1;record[9]=owner.originalMax;
    record[10]=owner.charge;record[11]=owner.maximum;
    std::memcpy(record.data()+16,pathHash.data(),32);std::memcpy(record.data()+48,imageHash.data(),32);
    if(!Digest(record.data(),80,&integrity))return false;
    std::memcpy(record.data()+80,integrity.data(),32);
    Record existing{};const auto prior=ReadRecord(name,&existing);
    if(prior==OwnerRead::Unavailable)return false;
    if(prior==OwnerRead::Found && (std::memcmp(existing.data()+16,record.data()+16,64)!=0))return false;
    bool exists=false;if(!SafeLeaf(name,&exists))return false;
    const auto temporary=name+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(nextTemporary.fetch_add(1));
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD bytes=0;bool ok=WriteFile(file,record.data(),112,&bytes,nullptr)&&bytes==112&&FlushFileBuffers(file);
    if(!CloseHandle(file))ok=false;
    if(ok)ok=MoveFileExW(temporary.c_str(),name.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temporary.c_str());
    SavedOwner verified{};
    return ok&&Read(path,image,&verified)==OwnerRead::Found&&verified.originalMax==owner.originalMax&&
        verified.charge==owner.charge&&verified.maximum==owner.maximum;
}
}
