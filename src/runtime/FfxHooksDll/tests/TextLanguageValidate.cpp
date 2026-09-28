// Jarvis-HOOK. Offline admission of an external pack against a private source snapshot.
#include "../hooks/TextLanguagePack.h"
#ifdef _WIN32
#include "../hooks/TextLanguageFiles.h"
#else
#include <openssl/sha.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace FfxHooks::TextLanguage;
namespace {
struct Host {
 std::filesystem::path pack,source;
#ifdef _WIN32
 Files files;
#endif
};
#ifndef _WIN32
std::filesystem::path PackageRoot(const char* argument){
 const auto absolute=std::filesystem::absolute(argument);
 std::filesystem::path current;
 // Inspect the spelling before canonicalization; otherwise a redirected root
 // can pass preflight even though the native file pinning rejects that path.
 for(const auto& component:absolute){
  current/=component;
  if(std::filesystem::is_symlink(current))throw std::runtime_error("Package root is redirected");
 }
 if(!std::filesystem::is_directory(absolute))throw std::runtime_error("Package root is missing");
 return std::filesystem::canonical(absolute);
}
#endif
bool ReadBounded(const std::filesystem::path& root,const std::string& relative,std::size_t limit,Bytes& out){
 if(!SafeRelativePath(relative))return false;
 auto path=root;
 for(const auto& component:std::filesystem::path(relative)){
  path/=component;
  if(std::filesystem::is_symlink(path))return false;
 }
 if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>limit)return false;
 std::ifstream file(path,std::ios::binary);if(!file)return false;
 Bytes bytes;bytes.reserve(static_cast<std::size_t>(std::filesystem::file_size(path)));
 char buffer[4096];
 while(file){file.read(buffer,sizeof(buffer));const auto count=file.gcount();
  if(count<0||static_cast<std::size_t>(count)>limit-bytes.size())return false;
  bytes.insert(bytes.end(),buffer,buffer+count);
 }
 if(!file.eof())return false;
 out=std::move(bytes);return true;
}
bool Read(void* value,const Resource& resource,bool source,Bytes& bytes){
 const auto& host=*static_cast<Host*>(value);
 if(source)return ReadBounded(host.source,resource.path,resource.sourceSize,bytes);
#ifdef _WIN32
 return host.files.Read(resource,bytes);
#else
 return ReadBounded(host.pack,resource.path,resource.size,bytes);
#endif
}
bool Hash(void*,const Bytes& bytes,std::string& out){
#ifdef _WIN32
 return Fingerprint(bytes,out);
#else
 unsigned char hash[SHA256_DIGEST_LENGTH]{};
 if(!SHA256(bytes.data(),bytes.size(),hash))return false;
 constexpr char hex[]="0123456789abcdef";std::string result;
 for(const auto c:hash){result+=hex[c>>4];result+=hex[c&15];}
 out=std::move(result);return true;
#endif
}
}
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"Usage: TextLanguageValidate PACK REFERENCE\n";return 2;}
 try{
  Host host;host.source=std::filesystem::canonical(argv[2]);
  std::string json,error;
#ifdef _WIN32
  host.pack=std::filesystem::absolute(argv[1]);
  // Reuse the real runtime path, reparse-point, hash and handle-ownership checks.
  // Keep all package files pinned through admission instead of reopening paths.
  if(!host.files.Initialize(host.pack.wstring(),error))throw std::runtime_error(error);
  json=host.files.Json();
#else
  host.pack=PackageRoot(argv[1]);
  Bytes raw;if(!ReadBounded(host.pack,"manifest.json",MaxManifestBytes,raw))throw std::runtime_error("Manifest is missing, redirected or oversized");
  json.assign(raw.begin(),raw.end());
#endif
  PreparedPack pack;const PackIo io{&host,Read,Hash};
  if(!AdmitPack(json,io,pack,error))throw std::runtime_error(error);
  std::size_t menu=0,battle=0,events=0;
  for(const auto& resource:pack.manifest.resources){
   menu+=resource.family==Family::Menu;battle+=resource.family==Family::Battle;events+=resource.family==Family::Event;
  }
  std::cout<<"ADMITTED RT0 locale="<<pack.manifest.locale<<" schema="<<pack.manifest.schemaVersion
           <<" api="<<pack.manifest.hookApi<<" menu="<<menu<<" battle="<<battle<<" events="<<events
           <<" resources="<<pack.resources.size()<<"\n";
  std::cout<<"Source identity will be checked again at the native font boundary. Restart-only; live validation is separate.\n";
  return 0;
 }catch(const std::exception& error){std::cerr<<"REJECTED: "<<error.what()<<'\n';return 1;}
}
