#include "TextLanguageHook.h"
#include "FahrenheitCoexistenceCore.h"
#include "FahrenheitServices.h"
#include "TextLanguageFiles.h"
#include "TextLanguageSettings.h"
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace FfxHooks::TextLanguage::Native {
namespace {
static_assert(sizeof(void*)==4,"The profiled text adapter is x86 only");
constexpr std::uintptr_t OpenRva=0x208100,FontRva=0x4AC0E0;
constexpr std::uintptr_t LocalePointerRva=0x8DED48,WesternWidthsRva=0x1441DA4;
struct Stream {HANDLE file=INVALID_HANDLE_VALUE;void* archive=nullptr;};
static_assert(sizeof(Stream)==8,"Native stream extent");
using OpenFn=int(__thiscall*)(Stream*,const char*,int,int,int,int);
using SizeFn=std::uint32_t(__thiscall*)(Stream*);
using ReadFn=std::uint32_t(__thiscall*)(Stream*,void*,std::uint32_t);
using CloseFn=bool(__thiscall*)(Stream*);
using FontFn=int(__cdecl*)(const unsigned char*);
enum class Publication : std::uint8_t {Pending,Committed,Cancelled};
static_assert(std::atomic<Publication>::is_always_lock_free,"Detach cancellation must be lock-free");
struct Context {
 std::uintptr_t base=0,targets[2]{};
 std::size_t targetCount=2;
 bool cooperative=false;
 OpenFn originalOpen=nullptr;FontFn originalFont=nullptr;
 Files files;PreparedPack pack;std::size_t metrics=0;
 std::atomic<bool> installed{false},fontReady{false},closing{false},earlyResource{false};
 std::atomic<Publication> publication{Publication::Pending};
 bool admissionEnabled=false; // Protected by g_admission; never true during a partial MinHook batch.
 std::atomic<std::uint32_t> textOpens{0},fontOpens{0},fallbacks{0};
 LogFn log=nullptr;
};
// Published contexts and trampolines intentionally have process lifetime. A
// cached game string may retain a dependency on these font pages after Stop.
std::atomic<Context*> g_context{nullptr};
std::atomic<State> g_state{State::Off};
SRWLOCK g_start=SRWLOCK_INIT,g_admission=SRWLOCK_INIT;
thread_local bool g_sourceRead=false;
struct Exclusive {SRWLOCK* lock;explicit Exclusive(SRWLOCK& l):lock(&l){AcquireSRWLockExclusive(lock);}~Exclusive(){ReleaseSRWLockExclusive(lock);}};
struct Shared {SRWLOCK* lock;explicit Shared(SRWLOCK& l):lock(&l){AcquireSRWLockShared(lock);}~Shared(){ReleaseSRWLockShared(lock);}};
void Report(LogFn log,const char* detail) noexcept {
 if(log)try{log(detail);}catch(...){/* Diagnostics must never unwind through a native game callback. */}
}
bool Copy(std::uintptr_t at,void* output,std::size_t size){
 SIZE_T got=0;return at&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),output,size,&got)&&got==size;
}
template<std::size_t N> bool Signature(std::uintptr_t at,const std::array<unsigned char,N>& bytes){
 std::array<unsigned char,N> actual{};return Copy(at,actual.data(),N)&&actual==bytes;
}
bool Profile(std::uintptr_t base,const std::wstring& path){
 IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 pe{};
 if(!Copy(base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||
    !Copy(base+dos.e_lfanew,&pe,sizeof(pe))||pe.Signature!=IMAGE_NT_SIGNATURE||
    !F8Runtime::IsSupportedExecutable({pe.FileHeader.Machine,pe.OptionalHeader.Magic,pe.FileHeader.TimeDateStamp,pe.OptionalHeader.SizeOfImage}))return false;
 if(!Signature(base+OpenRva,std::array<unsigned char,6>{0x55,0x8B,0xEC,0x83,0xEC,0x08})||
    !Signature(base+FontRva,std::array<unsigned char,7>{0x55,0x8B,0xEC,0x56,0x8B,0x75,0x08}))return false;
 std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
 const auto size=file.tellg();if(!file||size<=0||size>32*1024*1024)return false;
 Bytes bytes(static_cast<std::size_t>(size));file.seekg(0);
 if(!file.read(reinterpret_cast<char*>(bytes.data()),size))return false;
 std::string hash;return Fingerprint(bytes,hash)&&hash==ExecutableSha256;
}
std::uint32_t Locale(const Context& context){
 std::uintptr_t manager=0;std::uint32_t value=UINT32_MAX;
 if(Copy(context.base+LocalePointerRva,&manager,4)&&manager)Copy(manager+4,&value,4);
 return value;
}
bool NativeSource(Context& context,const Resource& resource,Bytes& bytes){
 Stream stream;const std::string path="../../../"+resource.request.substr(1);
 if(context.cooperative){
  // Use the provider-owned constructor chain. The existing g_sourceRead fence
  // prevents our resource callback from redirecting this source-verification read.
  using Constructor=Stream*(__thiscall*)(Stream*,const char*,int,unsigned,unsigned,int);
  const auto create=reinterpret_cast<Constructor>(context.base+0x207D80);
  if(create(&stream,path.c_str(),1,0,0,1)!=&stream||
     (stream.file==INVALID_HANDLE_VALUE&&!stream.archive))return false;
 }else if(context.originalOpen(&stream,path.c_str(),1,0,0,1)!=0)return false;
 struct Closing {Stream* stream;CloseFn close;~Closing(){close(stream);}} closing{&stream,reinterpret_cast<CloseFn>(context.base+0x207F40)};
 const auto size=reinterpret_cast<SizeFn>(context.base+0x207F80)(&stream);
 if(size!=resource.sourceSize||size>MaxResourceBytes)return false;
 Bytes result(size);
 if(reinterpret_cast<ReadFn>(context.base+0x208250)(&stream,result.data(),size)!=size)return false;
 bytes=std::move(result);return true;
}
bool ReadResource(void* host,const Resource& resource,bool source,Bytes& bytes){
 auto& context=*static_cast<Context*>(host);
 return source?NativeSource(context,resource,bytes):context.files.Read(resource,bytes);
}
bool HashResource(void*,const Bytes& bytes,std::string& hash){return Fingerprint(bytes,hash);}
void Reject(Context& context,State state,const char* detail) noexcept {
 g_state.store(state);context.closing=true;
 Report(context.log,detail);
}
bool Prepare(Context& context,const unsigned char* input){
 if(context.closing||context.earlyResource)return false;
 Bytes original(304);std::string hash;
 if(!Copy(reinterpret_cast<std::uintptr_t>(input),original.data(),original.size())||!Fingerprint(original,hash))return false;
 const auto& descriptor=context.files.Description();
 const auto metric=std::find_if(descriptor.resources.begin(),descriptor.resources.end(),[](const Resource& r){return r.family==Family::Metrics;});
 if(metric==descriptor.resources.end()||metric->sourceSha256!=hash){Reject(context,State::InvalidPack,"[text-language] The native Western font differs from the package source.");return false;}
 struct Bypass {Bypass(){g_sourceRead=true;}~Bypass(){g_sourceRead=false;}} bypass;
 PreparedPack candidate;std::string error;const PackIo io{&context,ReadResource,HashResource};
 if(!AdmitPack(context.files.Json(),io,candidate,error)){Reject(context,State::InvalidPack,error.c_str());return false;}
 if(context.closing)return false;
 Report(context.log,"[text-language] Package validated; publishing paired text and font resources.");
 context.metrics=static_cast<std::size_t>(metric-descriptor.resources.begin());
 context.pack=std::move(candidate);return true;
}
int __cdecl RegisterFont(const unsigned char* input){
 auto& context=*g_context.load(std::memory_order_acquire);
 std::uint16_t slot=0;
 if(!Copy(reinterpret_cast<std::uintptr_t>(input)+8,&slot,2)||slot!=4||Locale(context)!=1)
  return context.originalFont(input);
 Exclusive lock(g_admission);
 const unsigned char* selected=input;
 try{
  if(!context.fontReady){
   if(!context.admissionEnabled){
    context.earlyResource=true;
    Reject(context,State::TooLate,"[text-language] Font initialization preceded complete hook activation. Restart required.");
   }else if(Prepare(context,input)){
    // This is the publication boundary: a stop that wins first must retain
    // native metrics. Once committed, cached text and its font stay paired.
    auto pending=Publication::Pending;
    if(context.publication.compare_exchange_strong(pending,Publication::Committed))
     selected=context.pack.resources[context.metrics].data();
   }
  }else selected=context.pack.resources[context.metrics].data();
 }catch(...){
  Reject(context,context.fontReady?State::RestartRequired:State::InvalidPack,
         "[text-language] Package preparation failed. No partial package was published.");
 }
 // Exactly one call: exceptions in package preparation cannot cause a double
 // native registration. The registrar copies the fixed-size metrics; cached
 // translated text retains the same immutable font until process restart.
 const auto result=context.originalFont(selected);
 if(selected!=input){
  if(result==0){context.fontReady=true;g_state=context.closing?State::RestartRequired:State::Active;}
  else Reject(context,State::RestartRequired,"[text-language] Native font registration failed. Restart required.");
 }
 return result;
}
bool ReadPath(const char* input,std::string& output){
 if(!input)return false;
 std::array<char,256> bytes{};
 // Read one byte at a time so a valid terminator beside a guard page stays valid.
 for(std::size_t n=0;n<bytes.size();++n){
  if(!Copy(reinterpret_cast<std::uintptr_t>(input)+n,&bytes[n],1))return false;
  if(!bytes[n]){output.assign(bytes.data(),n);return true;}
 }
 return false;
}
std::uintptr_t OpenSelected(Context& context,const std::string& request,bool readOnly){
 const auto* resource=Resolve(context.files.Description(),request,Locale(context),readOnly);
 if(!resource||resource->family==Family::Metrics)return 0;
 Shared lock(g_admission);
 if(context.fontReady){
  const auto handle=context.files.Open(*resource);
  if(handle!=static_cast<std::uintptr_t>(-1)){
   if(resource->family==Family::Atlas)++context.fontOpens;else ++context.textOpens;
   return handle;
  }
  Reject(context,State::RestartRequired,"[text-language] A pinned resource could not be reopened. Restart required.");
  return static_cast<std::uintptr_t>(-1);
 }
 if(!context.closing){
  context.earlyResource=true;Reject(context,State::TooLate,"[text-language] A dependent resource loaded before the Western font boundary. Native resources retained.");
 }
 return 0;
}
int __fastcall OpenStream(Stream* stream,void*,const char* path,int readOnly,int a3,int a4,int a5){
 auto& context=*g_context.load(std::memory_order_acquire);
 if(g_sourceRead)return context.originalOpen(stream,path,readOnly,a3,a4,a5);
 try{
  std::string request;Stream before;
  if((readOnly&255)&&ReadPath(path,request)&&Copy(reinterpret_cast<std::uintptr_t>(stream),&before,sizeof(before))&&
     before.file==INVALID_HANDLE_VALUE&&!before.archive){
   // Readers wait for the complete font/text transaction. Without this fence,
   // a racing atlas request could cache vanilla pixels while custom metrics
   // and translated strings were being published on the font thread.
   const auto handle=OpenSelected(context,request,true);
   if(handle==static_cast<std::uintptr_t>(-1))return 1;
   if(handle){
    stream->file=reinterpret_cast<HANDLE>(handle);stream->archive=nullptr;return 0;
   }
  }
 }catch(...){/* Allocation failures preserve the original loader contract. */}
 ++context.fallbacks;return context.originalOpen(stream,path,readOnly,a3,a4,a5);
}
}

bool Start(std::uintptr_t base,const Settings& settings,LogFn log){
 if(!Coexistence::FeatureAllowed("language.text")){g_state=State::Conflict;Report(log,"[text-language] Fahrenheit owns file redirection; original resources retained.");return false;}
 if(!TryAcquireSRWLockExclusive(&g_start))return false;
 struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&g_start);}} unlock;
 if(g_context.load())return false;
 if(!settings.enabled){g_state=State::Off;return true;}
 try{
  if(!Profile(base,settings.executablePath)){g_state=State::Unsupported;return false;}
  std::uintptr_t widths=0;
  if(!Copy(base+WesternWidthsRva,&widths,4)||widths){g_state=State::TooLate;return false;}
  if(GetModuleHandleW(L"unx.dll")){g_state=State::Conflict;return false;}
  auto next=std::make_unique<Context>();next->base=base;next->log=log;std::string error;
  next->cooperative=Coexistence::runtime.FileServicesAllowed();
  if(!next->files.Initialize(settings.packageDirectory,error)||next->files.Description().locale!=settings.locale){
   g_state=State::InvalidPack;Report(log,error.c_str());return false;
  }
  if(next->cooperative){
   const auto conflicts=Coexistence::resourceConflict.load(std::memory_order_acquire);
   if(!conflicts){g_state=State::Conflict;return false;}
   for(const auto& resource:next->files.Description().resources)if(conflicts(resource.request.c_str())){
    g_state=State::Conflict;Report(log,"[text-language] Another Fahrenheit mod replaces a paired text/font resource. No partial package was published.");return false;
   }
  }
  if(settings.validateOnly){g_state=State::ValidateOnly;return true;}
  if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready){g_state=State::Conflict;return false;}
  if(next->cooperative){
   next->targetCount=1;next->targets[0]=base+FontRva;
   next->originalOpen=reinterpret_cast<OpenFn>(base+OpenRva);
  }else{
   next->targets[0]=base+OpenRva;next->targets[1]=base+FontRva;
   if(MH_CreateHook(reinterpret_cast<void*>(next->targets[0]),reinterpret_cast<void*>(&OpenStream),reinterpret_cast<void**>(&next->originalOpen))!=MH_OK){g_state=State::Conflict;return false;}
  }
  if(MH_CreateHook(reinterpret_cast<void*>(base+FontRva),reinterpret_cast<void*>(&RegisterFont),reinterpret_cast<void**>(&next->originalFont))!=MH_OK){
   if(!next->cooperative)MH_RemoveHook(reinterpret_cast<void*>(next->targets[0]));g_state=State::Conflict;return false;
  }
  HMODULE module=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&OpenStream),&module)){
   for(std::size_t i=0;i<next->targetCount;++i)MH_RemoveHook(reinterpret_cast<void*>(next->targets[i]));g_state=State::Conflict;return false;
  }
  auto* context=next.release();g_context.store(context,std::memory_order_release);g_state=State::Armed;
  const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::TextLanguage,context->targets,context->targetCount);
  if(result.result!=MinHookBatch::BatchResult::Applied){context->closing=true;g_state=State::Conflict;return false;}
  context->installed=true;
  bool ready=false;
  {
   Exclusive lock(g_admission);
   widths=0;
   ready=!context->earlyResource&&!context->closing&&
       Copy(base+WesternWidthsRva,&widths,4)&&widths==0;
   context->admissionEnabled=ready;
   if(!ready)Reject(*context,State::TooLate,"[text-language] Native resources initialized during startup. Restart required.");
  }
  if(!ready){
   const auto retired=MinHookBatch::NeutralizeBatch(&MinHookBatch::ProcessCoordinator(),
       MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::TextLanguage,context->targets,context->targetCount);
   if(retired.result==MinHookBatch::BatchResult::Neutralized)context->installed=false;
   else g_state=State::Conflict;
  }
  return ready;
 }catch(...){
  if(auto* context=g_context.load())context->closing=true;
  g_state=State::InvalidPack;return false;
 }
}
bool StartConfigured(std::uintptr_t base,bool validateOnly,LogFn log){
 try{
  const auto selected=FfxHooks::TextLanguage::Settings::Selection();
  if(selected<0){g_state=State::InvalidPack;Report(log,"[text-language] Unknown text locale; original resources retained.");return false;}
  Settings settings;settings.enabled=selected==1;settings.validateOnly=validateOnly;
  if(settings.enabled){
   std::array<wchar_t,4096> executable{};
   const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
   if(!length||length>=executable.size()){g_state=State::Unsupported;return false;}
   settings.executablePath.assign(executable.data(),length);
   settings.packageDirectory=(std::filesystem::path(settings.executablePath).parent_path()/L"_isolated"/L"languages"/L"pt-BR").wstring();
  }
  return Start(base,settings,log);
 }catch(...){g_state=State::InvalidPack;return false;}
}
void RequestStop() noexcept {
 if(auto* context=g_context.load()){
  auto pending=Publication::Pending;
  context->publication.compare_exchange_strong(pending,Publication::Cancelled);
  context->closing=true;
  if(pending==Publication::Committed)g_state=State::RestartRequired;
 }
}
bool Stop() noexcept {
 auto* context=g_context.load();if(!context)return true;
 RequestStop();
 if(!TryAcquireSRWLockExclusive(&g_admission))return false;
 const bool published=context->publication.load()==Publication::Committed;ReleaseSRWLockExclusive(&g_admission);
 if(published){g_state=State::RestartRequired;return false;}
 if(!context->installed)return true;
 const auto result=MinHookBatch::NeutralizeBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::TextLanguage,context->targets,context->targetCount);
 if(result.result!=MinHookBatch::BatchResult::Neutralized){g_state=State::Conflict;return false;}
 context->installed=false;g_state=State::Stopped;return true;
}

extern "C" __declspec(dllexport) std::uintptr_t __cdecl FfxHooks_FahrenheitOpenResourceV2(const char* path){
 using namespace FfxHooks::TextLanguage::Native;
 auto* context=g_context.load(std::memory_order_acquire);
 if(!context||!context->cooperative||g_sourceRead)return 0;
 // Published paired resources outlive logical stop: a cached translated string
 // must never be combined with a fallback atlas after the service closes.
 if(!context->fontReady&&!FfxHooks::Coexistence::runtime.FileServicesAllowed())return 0;
 try{std::string request;if(!ReadPath(path,request))return 0;return OpenSelected(*context,request,true);}
 catch(...){return context->fontReady?static_cast<std::uintptr_t>(-1):0;}
}
Snapshot Inspect() noexcept {
 Snapshot out;out.state=g_state.load();if(auto* context=g_context.load()){
  out.installed=context->installed;out.fontReady=context->fontReady;
  out.textOpens=context->textOpens;out.fontOpens=context->fontOpens;out.nativeFallbacks=context->fallbacks;
 }return out;
}
const char* Detail() noexcept {
 switch(g_state.load()){
 case State::Off:return "Original text language";
 case State::Armed:return "Waiting for the English font boundary";
 case State::Active:return "Text package active - partial translation";
 case State::InvalidPack:return "Package invalid - original text retained";
 case State::Unsupported:return "Executable profile is unsupported";
 case State::TooLate:return "Resource cache already initialized - restart required";
 case State::Conflict:return "Native hook ownership conflict";
 case State::Stopped:return "Text hooks stopped";
 case State::RestartRequired:return "Restart required to restore original cached text";
 case State::ValidateOnly:return "Validate-only - native activation was not attempted";
 }return "Text language unavailable";
}
}
