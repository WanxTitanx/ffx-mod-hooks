#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/TextLanguageFiles.h"
#include <filesystem>
#include <iostream>
#include <thread>
#include <atomic>

using namespace FfxHooks::TextLanguage;
namespace {
unsigned checks=0,failures=0;
std::string error;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<": "<<error<<'\n';}}
bool ReadOne(HANDLE handle,unsigned char& value){DWORD got=0;return ReadFile(handle,&value,1,&got,nullptr)&&got==1;}
}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: TextLanguageFilesRt1 PACK\n";return 2;}
 const auto root=std::filesystem::absolute(argv[1]);Files files;
 Check(files.Initialize(root.wstring(),error),"package files admit and remain pinned");
 Check(files.Description().resources.size()>=6,"pinned package retains the parsed manifest");
 if(!files.Description().resources.empty()){
  const auto& resource=files.Description().resources[0];Bytes all;
  Check(files.Read(resource,all)&&all.size()==resource.size,"read uses verified file size");
  std::string hash;Check(Fingerprint(all,hash)&&hash==resource.sha256,"CNG fingerprint agrees with the producer");
  HANDLE first=reinterpret_cast<HANDLE>(files.Open(resource));HANDLE second=reinterpret_cast<HANDLE>(files.Open(resource));
  Check(first!=INVALID_HANDLE_VALUE&&second!=INVALID_HANDLE_VALUE,"independent stream handles open");
  unsigned char a=0,b=0,c=0;
  Check(ReadOne(first,a)&&ReadOne(first,b)&&ReadOne(second,c)&&a==all[0]&&b==all[1]&&c==all[0],"native streams never share their file cursor");
  CloseHandle(first);CloseHandle(second);
  const auto path=root/std::filesystem::path(resource.path);
  HANDLE writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
  Check(writer==INVALID_HANDLE_VALUE,"validated resource cannot be replaced by a writer");
  if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);
  Check(!DeleteFileW(path.c_str()),"validated resource cannot be deleted during the session");
  Resource foreign=resource;foreign.path="../escape";
  Check(files.Open(foreign)==static_cast<std::uintptr_t>(-1),"foreign resource cannot borrow a pinned identity");
  std::atomic<unsigned> errors{0};std::thread threads[4];
  for(auto& thread:threads)thread=std::thread([&]{for(unsigned n=0;n<50;++n){Bytes bytes;if(!files.Read(resource,bytes)||bytes!=all)++errors;}});
  for(auto& thread:threads)thread.join();
  Check(errors==0,"parallel readers preserve immutable contents and independent cursors");
 }
 Files missing;Check(!missing.Initialize((root/"missing").wstring(),error),"missing package rejects");
 std::cout<<"TextLanguageFiles RT1: "<<checks<<" checks, "<<failures<<" failures\n";
 return failures?1:0;
}
