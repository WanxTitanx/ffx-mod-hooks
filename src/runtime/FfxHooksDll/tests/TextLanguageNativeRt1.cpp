#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <stdexcept>
#include "../hooks/TextLanguageHook.h"
#include "../hooks/TextLanguageFiles.h"
#include "TextLanguagePeFixture.inc"
using namespace FfxHooks::TextLanguage;
namespace N=FfxHooks::TextLanguage::Native;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s: %s\n",label,N::Detail());}}
int __cdecl Quiet(const char*,...){return 0;}
int __cdecl QuietOpen(int,const char*,...){return 0;}
std::atomic<bool> pauseSource{false};
std::atomic<bool> stopOnFontCopy{false};
HANDLE sourceEntered=nullptr,sourceRelease=nullptr;
void* __cdecl ArchiveManager(const char*){
 if(pauseSource.exchange(false)){
  SetEvent(sourceEntered);
  if(WaitForSingleObject(sourceRelease,5000)!=WAIT_OBJECT_0)throw std::runtime_error("Source fixture barrier timed out");
 }
 return reinterpret_cast<void*>(1);
}
void ThrowingLogger(const char*){throw std::runtime_error("Injected diagnostic allocation failure");}
void StopAfterValidation(const char*){N::RequestStop();}
void* __cdecl CopyWithStop(void* output,const void* input,std::size_t size){
 if(stopOnFontCopy.exchange(false))N::RequestStop();
 return std::memcpy(output,input,size);
}
void* __fastcall NoArchive(void*,void*){return nullptr;}
void Jump(unsigned char* image,std::uint32_t rva,void* destination){
 image[rva]=0xE9;const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination)-reinterpret_cast<std::uintptr_t>(image+rva+5));
 std::memcpy(image+rva+1,&delta,4);
}
void Import(unsigned char* image,std::uint32_t rva,void* address){std::memcpy(image+rva,&address,4);}
struct Stream {HANDLE file=INVALID_HANDLE_VALUE;void* archive=nullptr;};
using Open=int(__thiscall*)(Stream*,const char*,int,int,int,int);
using Size=std::uint32_t(__thiscall*)(Stream*);
using Get=std::uint32_t(__thiscall*)(Stream*,void*,std::uint32_t);
using Seek=std::uint32_t(__thiscall*)(Stream*,std::int32_t,int);
using Close=int(__thiscall*)(Stream*);
using FontCall=int(__cdecl*)(const unsigned char*);
void SeedImports(unsigned char* image){
 Import(image,0x70C138,reinterpret_cast<void*>(&CreateFileW));
 Import(image,0x70C13C,reinterpret_cast<void*>(&GetFileSizeEx));
 Import(image,0x70C140,reinterpret_cast<void*>(&ReadFile));
 Import(image,0x70C144,reinterpret_cast<void*>(&FlushFileBuffers));
 Import(image,0x70C148,reinterpret_cast<void*>(&SetFilePointer));
 Import(image,0x70C0DC,reinterpret_cast<void*>(&CloseHandle));
#ifdef _MSC_VER
 #pragma warning(push)
 #pragma warning(disable:4996) // Exact native CRT import ABI in this isolated PE fixture; callers pass bounded paths.
#endif
 Import(image,0x70C3E0,reinterpret_cast<void*>(&std::sprintf));
#ifdef _MSC_VER
 #pragma warning(pop)
#endif
 Jump(image,0x353F0,reinterpret_cast<void*>(&QuietOpen));
 Jump(image,0x22F6B0,reinterpret_cast<void*>(&Quiet));
 Jump(image,0x21BF70,reinterpret_cast<void*>(&ArchiveManager));
 Jump(image,0x21C0D0,reinterpret_cast<void*>(&NoArchive));
 Jump(image,0x54925C,reinterpret_cast<void*>(&CopyWithStop));
 FlushInstructionCache(GetCurrentProcess(),image,0x700000);
}
}
int main(int argc,char** argv){
 if(argc!=4&&argc!=5){std::fprintf(stderr,"Usage: TextLanguageNativeRt1 FFX.exe PACK REFERENCE [active|concurrent|early|stop|stop-validated|stop-reading|stop-committed|bad-font|late]\n");return 2;}
 const std::string mode=argc==5?argv[4]:"active";
 if(mode!="active"&&mode!="concurrent"&&mode!="early"&&mode!="stop"&&mode!="stop-validated"&&mode!="stop-reading"&&mode!="stop-committed"&&mode!="bad-font"&&mode!="late")return 2;
 const auto executable=std::filesystem::absolute(argv[1]);const auto package=std::filesystem::absolute(argv[2]);
 const auto reference=std::filesystem::absolute(argv[3]);
 const auto temporary=std::filesystem::current_path()/("mod006-native-"+std::to_string(GetCurrentProcessId()));
 if(std::filesystem::exists(temporary))return 3;
 Files input;std::string error;
 if(!input.Initialize(package.wstring(),error)){std::fprintf(stderr,"Fixture pack rejected: %s\n",error.c_str());return 4;}
 for(const auto& r:input.Description().resources){
  const auto target=temporary/r.request.substr(1);std::filesystem::create_directories(target.parent_path());
  std::filesystem::copy_file(reference/r.path,target);
 }
 const auto cwd=std::filesystem::current_path();std::filesystem::create_directories(temporary/"work/a/b");
 std::filesystem::current_path(temporary/"work/a/b");
 auto* image=MapPe(Read(executable.string().c_str()));if(!image)return 5;
 const auto base=reinterpret_cast<std::uintptr_t>(image);SeedImports(image);
 std::array<std::uint32_t,2> locale{0,1};auto* localePointer=locale.data();
 std::memcpy(image+0x8DED48,&localePointer,4);
 const auto open=reinterpret_cast<Open>(image+0x208100);const auto size=reinterpret_cast<Size>(image+0x207F80);
 const auto get=reinterpret_cast<Get>(image+0x208250);const auto seek=reinterpret_cast<Seek>(image+0x2082A0);
 const auto close=reinterpret_cast<Close>(image+0x207F40);const auto font=reinterpret_cast<FontCall>(image+0x4AC0E0);
 N::Settings off;Check(N::Start(0,off)&&!N::Inspect().installed,"OFF performs no native mutation");
 N::Settings settings;settings.enabled=true;settings.packageDirectory=package.wstring();settings.executablePath=executable.wstring();
 Check(!N::Start(0,settings),"unknown native image is rejected");
 settings.validateOnly=true;
 Check(N::Start(base,settings)&&N::Inspect().state==N::State::ValidateOnly&&!N::Inspect().installed,"validate-only leaves native entry points unchanged");
 settings.validateOnly=false;
 if(mode=="late"){
  const std::uint32_t occupied=1;std::memcpy(image+0x1441DA4,&occupied,4);
  Check(!N::Start(base,settings)&&N::Inspect().state==N::State::TooLate&&!N::Inspect().installed,"late activation cannot replace an initialized font cache");
 }else Check(N::Start(base,settings,mode=="bad-font"?&ThrowingLogger:mode=="stop-validated"?&StopAfterValidation:nullptr)&&N::Inspect().installed,"exact PE arms both native adapters");
 if(N::Inspect().installed){
  auto originalMetrics=Read((reference/"font/base.ftc").string().c_str());
  std::array<unsigned char,8> saveWriter{};std::memcpy(saveWriter.data(),image+0x4B3EE3,8);
  const auto& selectedResource=input.Description().resources[0];
  const auto selectedRequest="../../../"+selectedResource.request.substr(1);
  bool activeExpected=mode=="active"||mode=="concurrent"||mode=="stop-committed";
  if(mode=="early"){
   Stream early;Check(open(&early,selectedRequest.c_str(),1,0,0,1)==0&&size(&early)==selectedResource.sourceSize,"early text request retains native bytes");close(&early);
  }else if(mode=="stop")Check(N::Stop()&&!N::Inspect().installed,"stop before publication restores original native entries");
  if(mode=="bad-font")originalMetrics[64]^=1;
  if(mode=="stop-reading"){
   sourceEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sourceRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   Check(sourceEntered&&sourceRelease,"stop fixture barriers allocate");
   pauseSource=true;int registered=-1;
   std::thread registrar([&]{registered=font(originalMetrics.data());});
   Check(WaitForSingleObject(sourceEntered,5000)==WAIT_OBJECT_0,"stop races real source IO");
   Check(!N::Stop(),"stop cancels admission without blocking the in-flight callback");
   SetEvent(sourceRelease);registrar.join();
   Check(registered==0&&!N::Inspect().fontReady,"cancelled IO retains native font metrics");
   Check(N::Stop()&&!N::Inspect().installed,"drained cancelled callback permits hook retirement");
   CloseHandle(sourceEntered);CloseHandle(sourceRelease);
  }else if(mode=="concurrent"){
   sourceEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sourceRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   const auto started=CreateEventW(nullptr,TRUE,FALSE,nullptr),done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   Check(sourceEntered&&sourceRelease&&started&&done,"concurrent fixture barriers allocate");
   pauseSource=true;int registered=-1,opened=-1;std::uint32_t observedSize=0;
   std::thread registrar([&]{registered=font(originalMetrics.data());});
   Check(WaitForSingleObject(sourceEntered,5000)==WAIT_OBJECT_0,"font transaction reaches its real source IO boundary");
   std::thread reader([&]{SetEvent(started);Stream stream;opened=open(&stream,selectedRequest.c_str(),1,0,0,1);observedSize=size(&stream);close(&stream);SetEvent(done);});
   Check(WaitForSingleObject(started,5000)==WAIT_OBJECT_0,"racing native reader begins");
   Check(WaitForSingleObject(done,100)==WAIT_TIMEOUT,"a racing native read waits for font admission instead of caching vanilla bytes");
   SetEvent(sourceRelease);registrar.join();reader.join();
   Check(registered==0&&opened==0&&observedSize==selectedResource.size,"both native callbacks observe the same complete package generation");
   CloseHandle(sourceEntered);CloseHandle(sourceRelease);CloseHandle(started);CloseHandle(done);
  }else{
   bool escaped=false;int registered=-1;
   stopOnFontCopy=mode=="stop-committed";
   try{registered=font(originalMetrics.data());}catch(...){escaped=true;}
   Check(!escaped&&registered==0,"native font callback contains package and diagnostic exceptions");
   if(mode=="stop-committed")Check(!stopOnFontCopy&&N::Inspect().state==N::State::RestartRequired,
       "stop after publication commitment retains the paired font until restart");
  }
  Check(N::Inspect().fontReady==activeExpected,"native font publication matches the admitted or rejected session");
  if(!activeExpected){
   Stream unchanged;Check(open(&unchanged,selectedRequest.c_str(),1,0,0,1)==0&&size(&unchanged)==selectedResource.sourceSize,"failed or stopped admission preserves original resource routing");close(&unchanged);
   if(mode=="bad-font")Check(N::Inspect().state==N::State::InvalidPack,"foreign font identity is diagnosed without activation");
   if(mode=="stop-validated")Check(N::Stop()&&!N::Inspect().installed,
       "stop after validation wins publication and permits hook retirement after callback drain");
  }
  if(activeExpected){
  std::uint32_t table=0;std::memcpy(&table,image+0x1441DA4,4);
  Check(table&&reinterpret_cast<unsigned char*>(table)[242-48]==31&&reinterpret_cast<unsigned char*>(table)[244-48]==42,"real Western width consumer receives custom glyph metrics");
  const auto& resource=input.Description().resources[0];const auto request="../../../"+resource.request.substr(1);
  Bytes expected;input.Read(resource,expected);Stream stream;
  Check(open(&stream,request.c_str(),1,0,0,1)==0&&stream.archive==nullptr,"native open creates an OS-backed translated stream");
  Check(size(&stream)==resource.size,"native size query sees translated extent before allocation");
  Bytes actual(resource.size);Check(get(&stream,actual.data(),static_cast<std::uint32_t>(actual.size()))==actual.size()&&actual==expected,"native reader supplies exact translated bytes");
  Check(seek(&stream,0,FILE_BEGIN)==0,"native seek resets the independent cursor");
  unsigned char first=0;Check(get(&stream,&first,1)==1&&first==expected[0],"native read after seek is consistent");
  close(&stream);Check(stream.file==INVALID_HANDLE_VALUE,"native close releases the translated stream");
  for(const auto id:{0u,2u,3u,4u,5u,6u,9u,10u,18u}){
   locale[1]=id;Stream original;Check(open(&original,request.c_str(),1,0,0,1)==0&&size(&original)==resource.sourceSize,"other native locales retain original resources");close(&original);
  }
  locale[1]=1;
  for(const auto& entry:input.Description().resources){
   if(entry.family==Family::Metrics)continue;
   const auto name="../../../"+entry.request.substr(1);Stream read;
   Bytes wanted;input.Read(entry,wanted);Bytes got(entry.size);
   const bool opened=open(&read,name.c_str(),1,0,0,1)==0;
   Check(opened&&size(&read)==entry.size&&get(&read,got.data(),static_cast<std::uint32_t>(got.size()))==got.size()&&got==wanted,"every declared text/event/atlas reaches the native read and size consumer");
   close(&read);
  }
  using Mismatch=int(__cdecl*)(int);
  const auto mismatch=reinterpret_cast<Mismatch>(image+0x387430);
  Check(mismatch(1)==0&&mismatch(0)!=0,"native old-save language comparison still uses the original locale ID");
  Check(!N::Stop()&&N::Inspect().state==N::State::RestartRequired,"published text and font remain paired until restart");
  Stream retained;Check(open(&retained,request.c_str(),1,0,0,1)==0&&size(&retained)==resource.size,"Stop cannot detach the font dependency of cached translations");close(&retained);
  }
  Check(locale[1]==1&&std::memcmp(image+0x4B3EE3,saveWriter.data(),saveWriter.size())==0,"virtual text language preserves native locale and save-header code");
 }
 std::filesystem::current_path(cwd);std::filesystem::remove_all(temporary);
 std::printf("TextLanguageNative RT1 (%s): %u checks, %u failures\n",mode.c_str(),checks,failures);
 return failures?1:0;
}
