#pragma once
#include "ExecutableStartupGate.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>

namespace FfxHooks::ExecutableStartup {
// Call only from the pinned worker, before provider/config/native services.
// The known file size bounds reads and hashing; no game entrypoint is executed.
inline bool VerifyModuleFile(HMODULE module) noexcept {
    if(!module)return false;
    std::array<wchar_t,32768> path{};
    const DWORD length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!length||length>=path.size())return false;
    std::array<unsigned char,4096> object{};
    struct Resources {
        HANDLE file=INVALID_HANDLE_VALUE;
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        BCRYPT_HASH_HANDLE hash=nullptr;
        ~Resources(){
            if(hash)BCryptDestroyHash(hash);
            if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
            if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        }
    } resources;
    resources.file=CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(resources.file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(resources.file,&size)||size.QuadPart!=ExecutableProfile::FileBytes)return false;
    if(BCryptOpenAlgorithmProvider(&resources.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    DWORD objectSize=0,resultSize=0;
    if(BCryptGetProperty(resources.algorithm,BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&resultSize,0)<0||
        resultSize!=sizeof(objectSize)||!objectSize||objectSize>4096)return false;
    if(BCryptCreateHash(resources.algorithm,&resources.hash,object.data(),objectSize,nullptr,0,0)<0)return false;
    std::array<unsigned char,65536> buffer{};
    DWORD remaining=ExecutableProfile::FileBytes;
    while(remaining){
        const DWORD wanted=(std::min)(remaining,static_cast<DWORD>(buffer.size()));
        DWORD received=0;
        if(!ReadFile(resources.file,buffer.data(),wanted,&received,nullptr)||received!=wanted||
            BCryptHashData(resources.hash,buffer.data(),received,0)<0)return false;
        remaining-=received;
    }
    std::array<unsigned char,32> hash{};
    return BCryptFinishHash(resources.hash,hash.data(),static_cast<ULONG>(hash.size()),0)>=0&&
        HashMatches(hash.data(),hash.size());
}
}
