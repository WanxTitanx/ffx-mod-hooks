#pragma once
#include "GridLearnedCore.h"
#include "RonsoPoolStore.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <string>
#include <vector>

namespace FfxHooks::GridLearned {
inline bool Fingerprint(const void* data,std::size_t size,Hash& output){
    output={};if(!data||!size||size>UINT32_MAX)return false;
    std::vector<std::uint8_t> object;
    struct Handles {
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
        ~Handles(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    } handles;
    if(BCryptOpenAlgorithmProvider(&handles.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    DWORD count=0,actual=0;
    if(BCryptGetProperty(handles.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&count),sizeof(count),&actual,0)<0||!count||count>65536)return false;
    object.resize(count);
    return BCryptCreateHash(handles.algorithm,&handles.hash,object.data(),count,nullptr,0,0)>=0&&
        BCryptHashData(handles.hash,const_cast<PUCHAR>(static_cast<const std::uint8_t*>(data)),static_cast<ULONG>(size),0)>=0&&
        BCryptFinishHash(handles.hash,output.data(),static_cast<ULONG>(output.size()),0)>=0;
}
inline bool CanonicalSavePath(const wchar_t* input,std::wstring& output){
    output.clear();if(!input)return false;
    const auto size=wcsnlen_s(input,4097);if(size==0||size>4096)return false;
    wchar_t absolute[4097]{};
    const DWORD count=GetFullPathNameW(input,4097,absolute,nullptr);
    if(!count||count>=4097)return false;
    std::wstring normalized(count,L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,absolute,static_cast<int>(count),
        normalized.data(),static_cast<int>(count),nullptr,nullptr,0))return false;
    for(auto& c:normalized)if(c==L'/')c=L'\\';
    if(!RonsoPool::OwnerStore::IsSavePath(normalized))return false;
    output=std::move(normalized);return true;
}
inline bool SaveIdentity(const std::wstring& path,const void* image,std::size_t size,Identity& identity){
    identity={};
    return RonsoPool::OwnerStore::IsSavePath(path)&&path.size()<=4096&&
        Fingerprint(path.data(),path.size()*sizeof(wchar_t),identity.path)&&
        Fingerprint(image,size,identity.image)&&identity.Valid();
}
enum class RecordRead {Missing,Found,Invalid,Unavailable};
class Store {
public:
    bool Initialize(const std::wstring& directory,bool create){
        directory_.clear();if(directory.empty())return false;
        auto attributes=GetFileAttributesW(directory.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES&&create){
            if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
            attributes=GetFileAttributesW(directory.c_str());
        }
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        directory_=directory;return true;
    }
    std::wstring Path(const Identity& identity) const {
        if(directory_.empty()||!identity.Valid())return {};
        return directory_+L"\\"+Hex(identity.path)+L"-"+Hex(identity.image)+L".bin";
    }
    RecordRead Read(const Identity& identity,Learned& learned) const {
        learned={};Record record{};const auto path=Path(identity);
        if(path.empty())return RecordRead::Unavailable;
        const auto read=ReadRaw(path,record);
        if(read!=RecordRead::Found)return read;
        return Decode(record,identity,learned)?RecordRead::Found:RecordRead::Invalid;
    }
    bool Write(const State& state) const {
        Record record{};if(!Encode(state,record))return false;
        const auto path=Path(state.Key());if(path.empty())return false;
        Record previous{};const auto old=ReadRaw(path,previous);
        if(old==RecordRead::Unavailable||old==RecordRead::Invalid)return false;
        if(old==RecordRead::Found){Learned ignored{};if(!Decode(previous,state.Key(),ignored))return false;}
        static std::atomic<unsigned> sequence{1};
        const auto temporary=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        DWORD bytes=0;
        bool ok=WriteFile(file,record.data(),static_cast<DWORD>(record.size()),&bytes,nullptr)&&bytes==record.size()&&FlushFileBuffers(file);
        if(!CloseHandle(file))ok=false;
        if(ok){
            bool exists=false;
            ok=SafeLeaf(path,exists)&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        }
        if(!ok)DeleteFileW(temporary.c_str());
        Learned verified{};
        return ok&&Read(state.Key(),verified)==RecordRead::Found&&verified==state.Words();
    }
private:
    static std::wstring Hex(const Hash& hash){
        constexpr wchar_t digits[]=L"0123456789abcdef";
        std::wstring value;value.reserve(32);
        for(std::size_t i=0;i<16;++i){value.push_back(digits[hash[i]>>4]);value.push_back(digits[hash[i]&15]);}
        return value;
    }
    static bool SafeLeaf(const std::wstring& path,bool& exists){
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){
            exists=false;const auto error=GetLastError();
            return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;
        }
        exists=true;return !(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    }
    static RecordRead ReadRaw(const std::wstring& path,Record& output){
        bool exists=false;if(!SafeLeaf(path,exists))return RecordRead::Invalid;
        if(!exists)return RecordRead::Missing;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)return RecordRead::Unavailable;
        LARGE_INTEGER length{};DWORD count=0;
        const bool ok=GetFileSizeEx(file,&length)&&length.QuadPart==static_cast<LONGLONG>(output.size())&&
            ReadFile(file,output.data(),static_cast<DWORD>(output.size()),&count,nullptr)&&count==output.size();
        CloseHandle(file);return ok?RecordRead::Found:RecordRead::Invalid;
    }
    std::wstring directory_;
};
} // namespace FfxHooks::GridLearned
