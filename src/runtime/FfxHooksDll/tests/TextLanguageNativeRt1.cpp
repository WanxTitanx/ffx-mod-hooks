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
#include "../hooks/FahrenheitServices.h"
#include "TextLanguagePeFixture.inc"
#include "TextLanguageFixtureRvas.h"
using namespace FfxHooks::TextLanguage;
namespace N=FfxHooks::TextLanguage::Native;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s: %s\n",label,N::Detail());}}
#ifdef FFXHOOKS_TARGET_STEAM_20261001
using GlyphConsumer=const unsigned char*(__cdecl*)(const unsigned char*,void*,int);
bool CallGlyph(GlyphConsumer function,const unsigned char* input,void* output,const unsigned char*& next){
 __try{next=function(input,output,1);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void CheckNativeGlyphConsumer(unsigned char* image,const unsigned char* widths,unsigned customCount){
 // Exact Steam 0537b2a1... RVA, independently disassembled. The real language
 // getter, width lookup and glyph consumer execute; none is stubbed here.
 constexpr std::uint32_t rva=0x4B78B0;
 constexpr unsigned char prefix[]={0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x53,0x56,0x57,0x33,0xF6};
 const bool matched=std::memcmp(image+rva,prefix,sizeof(prefix))==0;
 Check(matched,"native glyph consumer has the examined Steam prologue");if(!matched)return;
 const auto function=reinterpret_cast<GlyphConsumer>(image+rva);
 for(const auto length:{64u,2048u}){
  std::array<unsigned char,2050> input{};input.front()=0xA5;input[length+1]=0x5A;
  for(unsigned n=0;n<length;++n)input[n+1]=static_cast<unsigned char>(n%2?242+(n/2)%customCount:0x50);
  bool valid=true;
  for(unsigned n=0;n<length&&valid;++n){
   std::array<unsigned char,0x44> output;output.fill(0xCC);const unsigned char* next=nullptr;
   valid=CallGlyph(function,input.data()+1+n,output.data()+16,next)&&next==input.data()+2+n;
   float advance=0;std::memcpy(&advance,output.data()+16+0x1C,sizeof(advance));
   valid=valid&&advance==widths[input[n+1]-48]*0.25f;
   for(unsigned j=0;j<16;++j)valid=valid&&output[j]==0xCC&&output[16+0x24+j]==0xCC;
  }
  Check(valid&&input.front()==0xA5&&input[length+1]==0x5A,
        "native glyph consumer advances through bounded long text and uses custom metrics without overwriting the glyph descriptor");
 }
}
#endif
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
Open peerOriginal=nullptr;
bool peerConflict=false;
int __cdecl PeerConflict(const char*){return peerConflict?1:0;}
int __fastcall PeerOpen(Stream* stream,void*,const char* path,int readOnly,int a3,int a4,int a5){
 const auto handle=(readOnly&255)?FfxHooks_FahrenheitOpenResourceV2(path):0;
 if(handle==static_cast<std::uintptr_t>(-1))return 1;
 if(handle){stream->file=reinterpret_cast<HANDLE>(handle);stream->archive=nullptr;return 0;}
 return peerOriginal(stream,path,readOnly,a3,a4,a5);
}
Stream* __fastcall PeerConstructor(Stream* stream,void*,const char* path,int readOnly,unsigned a3,unsigned a4,int a5){
 stream->file=INVALID_HANDLE_VALUE;stream->archive=nullptr;
 PeerOpen(stream,nullptr,path,readOnly,static_cast<int>(a3),static_cast<int>(a4),a5);return stream;
}
void SeedImports(unsigned char* image){
 Import(image,TextFixtureRva<0x70C138>(),reinterpret_cast<void*>(&CreateFileW));
 Import(image,TextFixtureRva<0x70C13C>(),reinterpret_cast<void*>(&GetFileSizeEx));
 Import(image,TextFixtureRva<0x70C140>(),reinterpret_cast<void*>(&ReadFile));
 Import(image,TextFixtureRva<0x70C144>(),reinterpret_cast<void*>(&FlushFileBuffers));
 Import(image,TextFixtureRva<0x70C148>(),reinterpret_cast<void*>(&SetFilePointer));
 Import(image,TextFixtureRva<0x70C0DC>(),reinterpret_cast<void*>(&CloseHandle));
#ifdef _MSC_VER
 #pragma warning(push)
 #pragma warning(disable:4996) // Exact native CRT import ABI in this isolated PE fixture; callers pass bounded paths.
#endif
 Import(image,TextFixtureRva<0x70C3E0>(),reinterpret_cast<void*>(&std::sprintf));
#ifdef _MSC_VER
 #pragma warning(pop)
#endif
 Jump(image,TextFixtureRva<0x353F0>(),reinterpret_cast<void*>(&QuietOpen));
 Jump(image,TextFixtureRva<0x22F6B0>(),reinterpret_cast<void*>(&Quiet));
 Jump(image,TextFixtureRva<0x21BF70>(),reinterpret_cast<void*>(&ArchiveManager));
 Jump(image,TextFixtureRva<0x21C0D0>(),reinterpret_cast<void*>(&NoArchive));
 Jump(image,TextFixtureRva<0x54925C>(),reinterpret_cast<void*>(&CopyWithStop));
 FlushInstructionCache(GetCurrentProcess(),image,0x700000);
}
}
int main(int argc,char** argv){
 if(argc!=4&&argc!=5){std::fprintf(stderr,"Usage: TextLanguageNativeRt1 FFX.exe PACK REFERENCE [active|concurrent|early|stop|stop-validated|stop-reading|stop-committed|bad-font|late]\n");return 2;}
 const std::string mode=argc==5?argv[4]:"active";
 const bool cooperative=mode=="cooperative"||mode=="cooperative-conflict"||mode=="cooperative-stop";
 if(!cooperative&&mode!="active"&&mode!="concurrent"&&mode!="early"&&mode!="stop"&&mode!="stop-validated"&&mode!="stop-reading"&&mode!="stop-committed"&&mode!="bad-font"&&mode!="late")return 2;
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
 std::memcpy(image+TextFixtureRva<0x8DED48>(),&localePointer,4);
 peerOriginal=reinterpret_cast<Open>(image+TextFixtureRva<0x208100>());
 const auto open=cooperative?reinterpret_cast<Open>(&PeerOpen):peerOriginal;const auto size=reinterpret_cast<Size>(image+TextFixtureRva<0x207F80>());
 const auto get=reinterpret_cast<Get>(image+TextFixtureRva<0x208250>());const auto seek=reinterpret_cast<Seek>(image+TextFixtureRva<0x2082A0>());
 const auto close=reinterpret_cast<Close>(image+TextFixtureRva<0x207F40>());const auto font=reinterpret_cast<FontCall>(image+TextFixtureRva<0x4AC0E0>());
 N::Settings off;Check(N::Start(0,off)&&!N::Inspect().installed,"OFF performs no native mutation");
 N::Settings settings;settings.enabled=true;settings.packageDirectory=package.wstring();settings.executablePath=executable.wstring();
 Check(!N::Start(0,settings),"unknown native image is rejected");
 settings.validateOnly=true;
 Check(N::Start(base,settings)&&N::Inspect().state==N::State::ValidateOnly&&!N::Inspect().installed,"validate-only leaves native entry points unchanged");
 settings.validateOnly=false;
 std::array<unsigned char,16> openBefore{};std::memcpy(openBefore.data(),image+TextFixtureRva<0x208100>(),openBefore.size());
 if(cooperative){
  auto& state=FfxHooks::Coexistence::runtime;state.Observe(true);
  Check(state.ConfigureServices(2,8)&&state.Ready(1,3)&&state.TryStart()&&state.Finish(true),"cooperative file ownership is explicit");
  FfxHooks::Coexistence::resourceConflict.store(PeerConflict);
  Jump(image,TextFixtureRva<0x207D80>(),reinterpret_cast<void*>(&PeerConstructor));
  FlushInstructionCache(GetCurrentProcess(),image+TextFixtureRva<0x207D80>(),5);
  if(mode=="cooperative-conflict"){
   peerConflict=true;
   Check(!N::Start(base,settings)&&N::Inspect().state==N::State::Conflict&&!N::Inspect().installed,"another EFL replacement rejects the whole paired package before any hook");
   Check(std::memcmp(openBefore.data(),image+TextFixtureRva<0x208100>(),openBefore.size())==0,"conflicting resource admission leaves native file worker intact");
   std::filesystem::current_path(cwd);std::filesystem::remove_all(temporary);
   std::printf("TextLanguageNative RT1 (%s): %u checks, %u failures\n",mode.c_str(),checks,failures);return failures?1:0;
  }
 }
 if(mode=="late"){
  const std::uint32_t occupied=1;std::memcpy(image+TextFixtureRva<0x1441DA4>(),&occupied,4);
  Check(!N::Start(base,settings)&&N::Inspect().state==N::State::TooLate&&!N::Inspect().installed,"late activation cannot replace an initialized font cache");
 }else Check(N::Start(base,settings,mode=="bad-font"?&ThrowingLogger:mode=="stop-validated"?&StopAfterValidation:nullptr)&&N::Inspect().installed,"exact PE arms its owned adapters");
 if(cooperative)Check(std::memcmp(openBefore.data(),image+TextFixtureRva<0x208100>(),openBefore.size())==0,"cooperative transport never installs a competing OpenStream hook");
 if(N::Inspect().installed){
  auto originalMetrics=Read((reference/"font/base.ftc").string().c_str());
  std::array<unsigned char,8> saveWriter{};std::memcpy(saveWriter.data(),image+TextFixtureRva<0x4B3EE3>(),8);
  const auto& selectedResource=input.Description().resources[0];
  const auto selectedRequest="../../../"+selectedResource.request.substr(1);
  bool activeExpected=cooperative||mode=="active"||mode=="concurrent"||mode=="stop-committed";
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
  std::uint32_t table=0;std::memcpy(&table,image+TextFixtureRva<0x1441DA4>(),4);
  Check(table&&reinterpret_cast<unsigned char*>(table)[242-48]==31&&reinterpret_cast<unsigned char*>(table)[244-48]==42,"real Western width consumer receives custom glyph metrics");
#ifdef FFXHOOKS_TARGET_STEAM_20261001
  if(mode=="active"&&table)CheckNativeGlyphConsumer(image,reinterpret_cast<const unsigned char*>(table),
      input.Description().fonts[0].profile==2?6u:4u);
#endif
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
  const auto mismatch=reinterpret_cast<Mismatch>(image+TextFixtureRva<0x387430>());
  Check(mismatch(1)==0&&mismatch(0)!=0,"native old-save language comparison still uses the original locale ID");
  Check(!N::Stop()&&N::Inspect().state==N::State::RestartRequired,"published text and font remain paired until restart");
  if(mode=="cooperative-stop")FfxHooks::Coexistence::runtime.Stop();
  Stream retained;Check(open(&retained,request.c_str(),1,0,0,1)==0&&size(&retained)==resource.size,"Stop cannot detach the font dependency of cached translations");close(&retained);
  }
  Check(locale[1]==1&&std::memcmp(image+TextFixtureRva<0x4B3EE3>(),saveWriter.data(),saveWriter.size())==0,"virtual text language preserves native locale and save-header code");
 }
 std::filesystem::current_path(cwd);std::filesystem::remove_all(temporary);
 std::printf("TextLanguageNative RT1 (%s): %u checks, %u failures\n",mode.c_str(),checks,failures);
 return failures?1:0;
}
