#include "TextLanguageFiles.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <utility>
namespace FfxHooks::TextLanguage {
namespace {
constexpr auto Invalid=static_cast<std::uintptr_t>(-1);
HANDLE Handle(std::uintptr_t h){return reinterpret_cast<HANDLE>(h);}
std::uintptr_t Value(HANDLE h){return reinterpret_cast<std::uintptr_t>(h);}
struct AutoHandle {HANDLE value=INVALID_HANDLE_VALUE;~AutoHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
void Keep(std::vector<std::uintptr_t>& handles,std::uintptr_t value){
 AutoHandle owner{Handle(value)};
 handles.push_back(value);
 owner.value=INVALID_HANDLE_VALUE;
}
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::wstring FullPath(const std::wstring& path){
 std::array<wchar_t,4096> full{};const auto n=GetFullPathNameW(path.c_str(),static_cast<DWORD>(full.size()),full.data(),nullptr);
 Require(n>3&&n<full.size(),"Package path is too long or invalid");std::wstring result(full.data(),n);
 Require(result[1]==L':'&&result[2]==L'\\',"Package must be on a local drive");
 while(result.size()>3&&result.back()==L'\\')result.pop_back();
 return result;
}
bool SameFinalPath(HANDLE handle,const std::wstring& full){
 std::array<wchar_t,4096> path{};const auto n=GetFinalPathNameByHandleW(handle,path.data(),static_cast<DWORD>(path.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
 if(!n||n>=path.size())return false;
 const auto expected=L"\\\\?\\"+full;
 return CompareStringOrdinal(path.data(),static_cast<int>(n),expected.c_str(),static_cast<int>(expected.size()),TRUE)==CSTR_EQUAL;
}
std::uintptr_t Pin(const std::wstring& full,bool directory){
 AutoHandle h;h.value=CreateFileW(full.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,
 directory?(FILE_SHARE_READ|FILE_SHARE_WRITE):FILE_SHARE_READ,nullptr,OPEN_EXISTING,
 FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_FLAG_SEQUENTIAL_SCAN),nullptr);
 BY_HANDLE_FILE_INFORMATION info{};
 if(h.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(h.value,&info)||GetFileType(h.value)!=FILE_TYPE_DISK||
 (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory||!SameFinalPath(h.value,full))return Invalid;
 const auto result=Value(h.value);h.value=INVALID_HANDLE_VALUE;return result;
}
bool ReadPinned(std::uintptr_t pinned,std::size_t limit,Bytes& output){
 AutoHandle reader;reader.value=ReOpenFile(Handle(pinned),GENERIC_READ,FILE_SHARE_READ,FILE_FLAG_SEQUENTIAL_SCAN);
 if(reader.value==INVALID_HANDLE_VALUE)return false;
 LARGE_INTEGER size{};
 if(!GetFileSizeEx(reader.value,&size)||size.QuadPart<0||static_cast<std::uint64_t>(size.QuadPart)>limit)return false;
 Bytes bytes(static_cast<std::size_t>(size.QuadPart));std::size_t at=0;
 while(at<bytes.size()){
  const auto count=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-at,65536));DWORD got=0;
  if(!ReadFile(reader.value,bytes.data()+at,count,&got,nullptr)||!got||got>count)return false;
  at+=got;
 }
 output=std::move(bytes);return true;
}
std::size_t Find(const Manifest& m,const Resource& value){
 for(std::size_t n=0;n<m.resources.size();++n){const auto& r=m.resources[n];
  if(r.id==value.id&&r.path==value.path&&r.request==value.request&&r.family==value.family&&
     r.size==value.size&&r.sourceSize==value.sourceSize&&r.sha256==value.sha256&&r.sourceSha256==value.sourceSha256&&r.font==value.font)return n;
 }
 return m.resources.size();
}
}
Files::~Files(){Close();}
void Files::Close(){
 for(const auto h:files_)if(h!=Invalid)CloseHandle(Handle(h));
 for(auto it=directories_.rbegin();it!=directories_.rend();++it)if(*it!=Invalid)CloseHandle(Handle(*it));
 files_.clear();directories_.clear();
}
bool Files::Initialize(const std::wstring& directory,std::string& error){
 try{
  Require(files_.empty()&&directories_.empty(),"Package files are already pinned");Files next;
  const auto full=FullPath(directory);const auto root=Pin(full,true);
  Require(root!=Invalid,"Package root is missing, redirected or unavailable");Keep(next.directories_,root);
  const auto manifest=Pin(full+L"\\manifest.json",false);
  Require(manifest!=Invalid,"Manifest is missing, redirected or writable by another owner");Keep(next.files_,manifest);
  Bytes bytes;Require(ReadPinned(manifest,MaxManifestBytes,bytes),"Manifest exceeds its bound or cannot be read");
  next.json_.assign(bytes.begin(),bytes.end());
  if(!ParseManifest(next.json_,next.manifest_,error))return false;
  std::set<std::wstring> directories;
  for(const auto& r:next.manifest_.resources){
   std::wstring path(r.path.begin(),r.path.end());std::replace(path.begin(),path.end(),L'/',L'\\');
   for(auto at=path.find(L'\\');at!=std::wstring::npos;at=path.find(L'\\',at+1)){
    const auto parent=full+L"\\"+path.substr(0,at);
    if(directories.insert(parent).second){const auto pinned=Pin(parent,true);
     Require(pinned!=Invalid,"Package directory is missing or redirected");Keep(next.directories_,pinned);}
   }
   const auto file=Pin(full+L"\\"+path,false);
   Require(file!=Invalid,"Resource is missing, redirected or writable by another owner");Keep(next.files_,file);
   Bytes data;std::string hash;
   Require(ReadPinned(file,r.size,data)&&data.size()==r.size,"Resource extent differs from its manifest");
   Require(Fingerprint(data,hash)&&hash==r.sha256,"Resource hash differs from its manifest");
  }
  files_.swap(next.files_);directories_.swap(next.directories_);json_.swap(next.json_);manifest_=std::move(next.manifest_);
  error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool Files::Read(const Resource& r,Bytes& output) const {
 const auto n=Find(manifest_,r);return n<manifest_.resources.size()&&n+1<files_.size()&&ReadPinned(files_[n+1],r.size,output);
}
std::uintptr_t Files::Open(const Resource& r) const {
 const auto n=Find(manifest_,r);if(n>=manifest_.resources.size()||n+1>=files_.size())return Invalid;
 return Value(ReOpenFile(Handle(files_[n+1]),GENERIC_READ,FILE_SHARE_READ,FILE_FLAG_SEQUENTIAL_SCAN));
}
bool Fingerprint(const Bytes& bytes,std::string& output){
 if(bytes.size()>MaxPackBytes)return false;
 struct Algorithm {BCRYPT_ALG_HANDLE value=nullptr;~Algorithm(){if(value)BCryptCloseAlgorithmProvider(value,0);}} algorithm;
 if(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
 DWORD objectSize=0,returned=0;
 bool ok=BCryptGetProperty(algorithm.value,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&returned,0)>=0&&objectSize>0&&objectSize<=65536;
 std::vector<unsigned char> object(ok?objectSize:0);std::array<unsigned char,32> digest{};
 // Destruction order keeps the hash object buffer alive until BCryptDestroyHash,
 // including allocation failures while formatting the final fingerprint.
 struct Hash {BCRYPT_HASH_HANDLE value=nullptr;~Hash(){if(value)BCryptDestroyHash(value);}} hash;
 if(ok)ok=BCryptCreateHash(algorithm.value,&hash.value,object.data(),objectSize,nullptr,0,0)>=0;
 if(ok&&!bytes.empty())ok=BCryptHashData(hash.value,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0;
 if(ok)ok=BCryptFinishHash(hash.value,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
 if(!ok)return false;
 constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
 for(auto value:digest){result+=hex[value>>4];result+=hex[value&15];}
 output=std::move(result);return true;
}
}
