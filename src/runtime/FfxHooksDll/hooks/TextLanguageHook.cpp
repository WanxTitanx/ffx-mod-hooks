#include "../shared/ExecutableProfile.h"
#include "TextLanguageHook.h"
#include "FahrenheitCoexistenceCore.h"
#include "FahrenheitServices.h"
#include "TextLanguageFiles.h"
#include "TextLanguageSettings.h"
#include "TextLanguageKernelSites.h"
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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#endif

namespace FfxHooks::TextLanguage::Native {
namespace {
static_assert(sizeof(void*)==4,"The profiled text adapter is x86 only");
constexpr std::uintptr_t OpenRva=(::FfxHooks::ExecutableProfile::Rva<0x208100>()),FontRva=(::FfxHooks::ExecutableProfile::Rva<0x4AC0E0>());
constexpr std::uintptr_t LocalePointerRva=(::FfxHooks::ExecutableProfile::Rva<0x8DED48>()),WesternWidthsRva=(::FfxHooks::ExecutableProfile::Rva<0x1441DA4>());
struct Stream {HANDLE file=INVALID_HANDLE_VALUE;void* archive=nullptr;};
static_assert(sizeof(Stream)==8,"Native stream extent");
using OpenFn=int(__thiscall*)(Stream*,const char*,int,int,int,int);
using SizeFn=std::uint32_t(__thiscall*)(Stream*);
using ReadFn=std::uint32_t(__thiscall*)(Stream*,void*,std::uint32_t);
using CloseFn=bool(__thiscall*)(Stream*);
using FontFn=int(__cdecl*)(const unsigned char*);
using KernelLoadFn=int(__cdecl*)(int);
using KernelReadFn=std::uint32_t(__cdecl*)(const char*,void*);
using HeapAllocFn=void*(__cdecl*)(std::uint32_t);
using HeapFreeFn=void(__cdecl*)(void*);
using AlignFn=void(__cdecl*)(std::uint32_t);
using AccountFn=void(__cdecl*)(void*,std::uint32_t);
using BudgetFn=void(__cdecl*)(std::uint32_t);
enum class Publication : std::uint8_t {Pending,Committed,Cancelled};
static_assert(std::atomic<Publication>::is_always_lock_free,"Detach cancellation must be lock-free");
struct Context {
 std::uintptr_t base=0,targets[4]{};
 std::size_t targetCount=2;
 bool cooperative=false;
 OpenFn originalOpen=nullptr;FontFn originalFont=nullptr;KernelLoadFn originalKernel=nullptr;
 BudgetFn originalBudget=nullptr;
 std::atomic<bool> budgetReported{false};
 // The 32-bit kernel table adapter is part of this batch. Fixed before enabling.
 bool kernelTables=false;
 SRWLOCK kernelLock=SRWLOCK_INIT;
 std::array<std::atomic<std::uint32_t>,KernelSites::Sites.size()> kernelCapacity{};
 std::array<std::atomic<std::uint32_t>,KernelSites::Sites.size()> kernelLength{};
 std::array<std::atomic<bool>,KernelSites::Sites.size()> kernelReported{};
 std::atomic<std::uint32_t> kernelLoads{0},kernelExtended{0},kernelRelocations{0};
 Files files;PreparedPack pack;std::size_t metrics=0;
 std::atomic<bool> installed{false},fontReady{false},closing{false},earlyResource{false};
 std::atomic<Publication> publication{Publication::Pending};
 bool admissionEnabled=false; // Protected by g_admission; never true during a partial MinHook batch.
 std::atomic<std::uint32_t> textOpens{0},fontOpens{0},fallbacks{0},retainedOpens{0},retainedReports{0};
 LogFn log=nullptr;
};
// Published contexts and trampolines intentionally have process lifetime. A
// cached game string may retain a dependency on these font pages after Stop.
std::atomic<Context*> g_context{nullptr};
std::atomic<State> g_state{State::Off};
SRWLOCK g_start=SRWLOCK_INIT,g_admission=SRWLOCK_INIT;
thread_local bool g_sourceRead=false;
struct Exclusive {SRWLOCK* lock;explicit Exclusive(SRWLOCK& l):lock(&l){AcquireSRWLockExclusive(lock);}~Exclusive(){ReleaseSRWLockExclusive(lock);}};
struct TryExclusive {
 SRWLOCK* lock;bool held;
 explicit TryExclusive(SRWLOCK& value):lock(&value),held(TryAcquireSRWLockExclusive(lock)!=0){}
 ~TryExclusive(){if(held)ReleaseSRWLockExclusive(lock);}
};
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
bool SourceFailure(Context& context,const Resource& resource,const char* stage,std::uint32_t observed=0) noexcept {
 char detail[512]{};
 std::snprintf(detail,sizeof(detail),"[text-language] Source %s failed: %s (expected_bytes=%u observed=%u).",
               stage,resource.request.c_str(),resource.sourceSize,observed);
 Report(context.log,detail);return false;
}
bool NativeSource(Context& context,const Resource& resource,Bytes& bytes){
 std::string member;
 if(!NativeSourceRequest(resource.request,member))return SourceFailure(context,resource,"path");
 Stream stream;const std::string path="../../../"+member;
 if(context.cooperative){
  // Use the provider-owned constructor chain. The existing g_sourceRead fence
  // prevents our resource callback from redirecting this source-verification read.
  using Constructor=Stream*(__thiscall*)(Stream*,const char*,int,unsigned,unsigned,int);
  const auto create=reinterpret_cast<Constructor>(context.base + (::FfxHooks::ExecutableProfile::Rva<0x207D80>()));
  if(create(&stream,path.c_str(),1,0,0,1)!=&stream||
     (stream.file==INVALID_HANDLE_VALUE&&!stream.archive))return SourceFailure(context,resource,"open");
 }else if(context.originalOpen(&stream,path.c_str(),1,0,0,1)!=0||
           (stream.file==INVALID_HANDLE_VALUE&&!stream.archive))return SourceFailure(context,resource,"open");
 struct Closing {Stream* stream;CloseFn close;~Closing(){close(stream);}} closing{&stream,reinterpret_cast<CloseFn>(context.base + (::FfxHooks::ExecutableProfile::Rva<0x207F40>()))};
 const auto size=reinterpret_cast<SizeFn>(context.base + (::FfxHooks::ExecutableProfile::Rva<0x207F80>()))(&stream);
 if(size!=resource.sourceSize||size>MaxResourceBytes)return SourceFailure(context,resource,"size",size);
 Bytes result(size);
 const auto read=reinterpret_cast<ReadFn>(context.base + (::FfxHooks::ExecutableProfile::Rva<0x208250>()))(&stream,result.data(),size);
 if(read!=size)return SourceFailure(context,resource,"read",read);
 bytes=std::move(result);return true;
}
bool ReadResource(void* host,const Resource& resource,bool source,Bytes& bytes){
 auto& context=*static_cast<Context*>(host);
 return source?NativeSource(context,resource,bytes):context.files.Read(resource,bytes);
}
bool HashResource(void*,const Bytes& bytes,std::string& hash){return Fingerprint(bytes,hash);}
void RetainResource(void* host,const Resource& resource,const char* reason) noexcept {
 auto& context=*static_cast<Context*>(host);
 // One line per retained resource, bounded: a whole-mod redeploy must not flood the log.
 if(context.retainedReports.fetch_add(1)>=32)return;
 char detail[512]{};
 std::snprintf(detail,sizeof(detail),"[text-language] Keeping the native file for %s: %s.",resource.request.c_str(),reason?reason:"retained");
 Report(context.log,detail);
}
void Reject(Context& context,State state,const char* detail) noexcept {
 g_state.store(state);context.closing=true;
 Report(context.log,detail);
}
bool OriginalMetricsMatch(const Context& context,const unsigned char* input){
 Bytes original(304);std::string hash;
 if(!Copy(reinterpret_cast<std::uintptr_t>(input),original.data(),original.size())||!Fingerprint(original,hash))return false;
 const auto& descriptor=context.files.Description();
 const auto metric=std::find_if(descriptor.resources.begin(),descriptor.resources.end(),[](const Resource& r){return r.family==Family::Metrics;});
 return metric!=descriptor.resources.end()&&metric->sourceSha256==hash;
}
bool Prepare(Context& context,const unsigned char* input=nullptr){
 if(context.closing||context.earlyResource)return false;
 const auto& descriptor=context.files.Description();
 const auto metric=std::find_if(descriptor.resources.begin(),descriptor.resources.end(),[](const Resource& r){return r.family==Family::Metrics;});
 if(metric==descriptor.resources.end()||(input&&!OriginalMetricsMatch(context,input))){Reject(context,State::InvalidPack,"[text-language] The native Western font differs from the package source.");return false;}
 struct Bypass {Bypass(){g_sourceRead=true;}~Bypass(){g_sourceRead=false;}} bypass;
 PreparedPack candidate;std::string error;
 // Tables above 0xFFF0 bytes are admitted only while the 32-bit adapter owns their loader.
 const PackIo io{&context,ReadResource,HashResource,RetainResource,context.kernelTables};
 if(!AdmitPack(context.files.Json(),io,candidate,error)){Reject(context,State::InvalidPack,error.c_str());return false;}
 if(context.closing)return false;
 char summary[256]{};
 std::snprintf(summary,sizeof(summary),"[text-language] Package validated; publishing paired text and font resources (%u of %u keep the native file).",
               static_cast<unsigned>(candidate.retainedCount),static_cast<unsigned>(candidate.manifest.resources.size()));
 Report(context.log,summary);
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
  if(context.publication.load()==Publication::Committed){
   // The real boot loads kernel text and atlas pages before registering the
   // Western font. Once any paired resource escaped, the immutable admitted
   // metrics must survive stop or a later source change as well.
   selected=context.pack.resources[context.metrics].data();
   if(!context.fontReady&&!OriginalMetricsMatch(context,input))
    Reject(context,State::RestartRequired,"[text-language] Native font input changed after resource publication; retaining paired metrics until restart.");
  }else if(!context.fontReady){
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
  }
 }catch(...){
  const bool committed=context.publication.load()==Publication::Committed;
  if(committed)selected=context.pack.resources[context.metrics].data();
  Reject(context,committed?State::RestartRequired:State::InvalidPack,
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
 Exclusive lock(g_admission);
 if(context.publication.load()!=Publication::Committed&&!context.closing){
  if(!context.admissionEnabled){
   context.earlyResource=true;
   Reject(context,State::TooLate,"[text-language] Resource access preceded complete hook activation. Restart required.");
  }else{
   try{
    // Validate the complete source/output/font group on the first native
    // resource read. Calling the registrar first is not the game's boot order;
    // do not call it artificially or let a vanilla atlas enter the cache.
    if(Prepare(context)){
     auto pending=Publication::Pending;
     if(context.publication.compare_exchange_strong(pending,Publication::Committed))
      Report(context.log,"[text-language] Paired package reserved before native font registration.");
    }
   }catch(...){Reject(context,State::InvalidPack,"[text-language] Resource-first package preparation failed. Native resources retained.");}
  }
 }
 if(context.publication.load()==Publication::Committed){
  // A retained request (changed mod source, oversized native table) keeps the
  // native/File Loader file. The pack is immutable once committed.
  const auto index=static_cast<std::size_t>(resource-context.files.Description().resources.data());
  if(index<context.pack.retained.size()&&context.pack.retained[index]){++context.retainedOpens;return 0;}
  const auto handle=context.files.Open(*resource);
  if(handle!=static_cast<std::uintptr_t>(-1)){
   if(resource->family==Family::Atlas)++context.fontOpens;else ++context.textOpens;
   return handle;
  }
  Reject(context,State::RestartRequired,"[text-language] A pinned resource could not be reopened. Restart required.");
  return static_cast<std::uintptr_t>(-1);
 }
 return 0;
}
static_assert(std::size(ExtendedKernelTables)==KernelSites::Sites.size()&&
              ExtendedKernelTables[0].kind==KernelSites::Sites[0].kind&&ExtendedKernelTables[1].kind==KernelSites::Sites[1].kind&&
              ExtendedKernelTables[2].kind==KernelSites::Sites[2].kind,"Adapter sites follow the admitted kernel table list");
// The native kernel profile: prologue, the three owned case bodies' jump-table
// slots, their reader calls and their u16 size stores. Absolute operands are
// relocated with the image, rel32 calls are position independent.
bool KernelProfile(std::uintptr_t base){
 namespace K=KernelSites;
 if(!Signature(base+K::Load,K::Prologue))return false;
 for(const auto& site:K::Sites){
  std::uint32_t entry=0;
  if(!Copy(base+K::JumpTable+4u*static_cast<std::uint32_t>(site.kind),&entry,4)||entry!=base+site.entry)return false;
  std::array<unsigned char,5> call{0xE8};const auto relative=static_cast<std::uint32_t>(K::Read-(site.readCall+5u));
  std::memcpy(call.data()+1,&relative,4);
  std::array<unsigned char,6> store{0x66,0xA3};const auto global=static_cast<std::uint32_t>(base+site.size16);
  std::memcpy(store.data()+2,&global,4);
  if(!Signature(base+site.readCall,call)||!Signature(base+site.sizeStore,store))return false;
 }
 return true;
}
// The banks are allocated by the first load and never freed by the game. A
// non-empty bank means the native loader already ran with 16-bit sizes.
bool KernelBanksEmpty(std::uintptr_t base){
 for(const auto& site:KernelSites::Sites){std::uint32_t bank=1;if(!Copy(base+site.bank,&bank,4)||bank)return false;}
 return true;
}
bool KernelBudgetProfile(std::uintptr_t base){
 namespace K=KernelSites;
 // Complete 70-byte finalizer, with every HIGHLOW and relative operand rebuilt
 // for the exact selected image. Do not infer one profile from a uniform delta.
 std::array<unsigned char,70> body{
  0x55,0x8B,0xEC,0x8B,0x45,0x08,0x8B,0x0D,0,0,0,0,0x2B,0xC1,0x83,0xE8,0x10,0x85,0xC0,0x7E,0x1D,
  0x99,0x83,0xE2,0x3F,0x03,0xC2,0x83,0xE0,0xC0,0x50,0xE8,0,0,0,0,0x8B,0x0D,0,0,0,0,0x83,0xC4,0x04,
  0xA3,0,0,0,0,0x3B,0x4D,0x08,0x7E,0x0D,0xC7,0x45,0x08,0,0,0,0,0x5D,0xE9,0,0,0,0,0x5D,0xC3};
 const auto put=[&](std::size_t at,std::uint32_t value){std::memcpy(body.data()+at,&value,4);};
 put(8,static_cast<std::uint32_t>(base+K::BudgetUsed));
 put(38,static_cast<std::uint32_t>(base+K::BudgetUsed));
 put(46,static_cast<std::uint32_t>(base+K::BudgetPadding));
 put(58,static_cast<std::uint32_t>(base+K::BudgetError));
 put(32,K::HeapAlloc-(K::BudgetFinish+36u));
 put(64,K::Fatal-(K::BudgetFinish+68u));
 std::array<unsigned char,13> caller{0x68,0x00,0x00,0x07,0x00,0xE8,0,0,0,0,0x83,0xC4,0x04};
 const auto relative=K::BudgetFinish-K::BudgetReturn;
 std::memcpy(caller.data()+6,&relative,4);
 return Signature(base+K::BudgetFinish,body)&&Signature(base+K::BudgetReturn-10,caller);
}
void __cdecl FinishKernelBudget(std::uint32_t requested){
 auto& context=*g_context.load(std::memory_order_acquire);
#ifdef _MSC_VER
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#else
 const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#endif
 if(caller==context.base+KernelSites::BudgetReturn&&requested==KernelSites::OriginalBudget&&
    context.publication.load(std::memory_order_acquire)==Publication::Committed){
  // Publication makes this allowance immutable. It covers only validated kernel
  // translations; changed/retained resources and graphics cannot enlarge it.
  requested+=context.pack.kernelBudgetGrowth;
  if(context.pack.kernelBudgetGrowth&&!context.budgetReported.exchange(true)){
   char detail[192]{};
   std::snprintf(detail,sizeof(detail),"[text-language] Kernel memory budget: %u bytes (+%u admitted text growth).",
                 static_cast<unsigned>(requested),static_cast<unsigned>(context.pack.kernelBudgetGrowth));
   Report(context.log,detail);
  }
 }
 // Keep the real usage counter, native padding allocation and over-budget check.
 context.originalBudget(requested);
}
// FFX_Kernel_LoadFileToTable for cases 0/2/3 with the table size kept in 32 bits.
// Same staging extent, helper order and globals as the native case body; the u16
// global still receives the truncated size although nothing else reads it.
int __cdecl LoadKernelTable(int kind){
 auto& context=*g_context.load(std::memory_order_acquire);
 namespace K=KernelSites;
 std::size_t index=0;
 while(index<K::Sites.size()&&K::Sites[index].kind!=kind)++index;
 if(index==K::Sites.size())return context.originalKernel(kind);
 // Stop must not retire the loader between a native read and publication of its
 // extended bank. The same fence serializes bank pointer/capacity updates.
 Exclusive kernel(context.kernelLock);
 if(context.closing&&context.publication.load()!=Publication::Committed&&!context.kernelExtended.load())
  return context.originalKernel(kind);
 const auto& site=K::Sites[index];const auto base=context.base;
 const auto allocate=reinterpret_cast<HeapAllocFn>(base+K::HeapAlloc);
 void* staging=allocate(KernelStagingBytes);
 const auto size=reinterpret_cast<KernelReadFn>(base+K::Read)(reinterpret_cast<const char*>(base+site.name),staging);
 *reinterpret_cast<volatile std::uint16_t*>(base+site.size16)=static_cast<std::uint16_t>(size);
 auto* bank=reinterpret_cast<void* volatile*>(base+site.bank);
 const auto plan=PlanKernelBank(*bank!=nullptr,context.kernelCapacity[index].load(),size);
 if(plan.allocate){
  reinterpret_cast<AlignFn>(base+K::Align)(0x40);
  void* fresh=allocate(size);*bank=fresh;
  reinterpret_cast<AccountFn>(base+K::Account)(fresh,size);
  context.kernelCapacity[index]=size;
  if(plan.retainOld){
   // Cached entry/string pointers may still reference the old bank; it stays allocated.
   ++context.kernelRelocations;
   char detail[192]{};
   std::snprintf(detail,sizeof(detail),"[text-language] Kernel table %s outgrew its bank (%u bytes); the previous bank is retained.",
                 ExtendedKernelTables[index].leaf,static_cast<unsigned>(size));
   Report(context.log,detail);
  }
 }
 std::memcpy(*bank,staging,size);
 context.kernelLength[index]=size;
 reinterpret_cast<HeapFreeFn>(base+K::HeapFree)(staging);
 ++context.kernelLoads;
 if(size>KernelU16Limit){
  ++context.kernelExtended;
  if(context.closing)g_state=State::RestartRequired;
  if(!context.kernelReported[index].exchange(true)){
   char detail[160]{};
   std::snprintf(detail,sizeof(detail),"[text-language] Kernel table %s loaded with %u bytes through the 32-bit adapter.",
                 ExtendedKernelTables[index].leaf,static_cast<unsigned>(size));
   Report(context.log,detail);
  }
 }
 return -1;
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
  if(!Profile(base,settings.executablePath)||!KernelBudgetProfile(base)){g_state=State::Unsupported;return false;}
  std::uintptr_t widths=0;
  if(!Copy(base+WesternWidthsRva,&widths,4)||widths){g_state=State::TooLate;Report(log,"[text-language] Western font cache was initialized before hook startup. Restart required.");return false;}
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
  if(MH_CreateHook(reinterpret_cast<void*>(base+KernelSites::BudgetFinish),reinterpret_cast<void*>(&FinishKernelBudget),
                   reinterpret_cast<void**>(&next->originalBudget))!=MH_OK){
   for(std::size_t i=0;i<next->targetCount;++i)MH_RemoveHook(reinterpret_cast<void*>(next->targets[i]));
   g_state=State::Conflict;return false;
  }
  next->targets[next->targetCount++]=base+KernelSites::BudgetFinish;
  if(settings.extendedKernelTables){
   // Optional: when it cannot be armed, the package keeps the 16-bit table limits.
   const char* unavailable=nullptr;
   if(!KernelProfile(base))unavailable="the native loader differs from the reviewed bytes";
   else if(!KernelBanksEmpty(base))unavailable="kernel tables were loaded before hook startup";
   else if(MH_CreateHook(reinterpret_cast<void*>(base+KernelSites::Load),reinterpret_cast<void*>(&LoadKernelTable),
                         reinterpret_cast<void**>(&next->originalKernel))!=MH_OK)unavailable="the native loader could not be hooked";
   else{next->targets[next->targetCount++]=base+KernelSites::Load;next->kernelTables=true;}
   if(unavailable){
    char detail[192]{};
    std::snprintf(detail,sizeof(detail),"[text-language] 32-bit kernel table adapter unavailable: %s. 16-bit table limits apply.",unavailable);
    Report(log,detail);
   }
  }
  HMODULE module=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&OpenStream),&module)){
   for(std::size_t i=0;i<next->targetCount;++i)MH_RemoveHook(reinterpret_cast<void*>(next->targets[i]));g_state=State::Conflict;return false;
  }
  auto* context=next.release();g_context.store(context,std::memory_order_release);g_state=State::Armed;
  const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::TextLanguage,context->targets,context->targetCount);
  if(result.result!=MinHookBatch::BatchResult::Applied){context->closing=true;g_state=State::Conflict;return false;}
  context->installed=true;
  if(context->kernelTables)Report(log,"[text-language] 32-bit kernel table adapter armed for command, a_ability and item.");
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
   settings.extendedKernelTables=FfxHooks::TextLanguage::Settings::ExtendedKernelTables();
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
 // This is non-blocking even when Stop is requested from the native reader.
 // A callback queued behind retirement observes closing before it can publish.
 TryExclusive kernel(context->kernelLock);if(!kernel.held)return false;
 if(!TryAcquireSRWLockExclusive(&g_admission))return false;
 const bool published=context->publication.load()==Publication::Committed;ReleaseSRWLockExclusive(&g_admission);
 // A table loaded above the 16-bit limit must never meet the native loader again.
 if(published||context->kernelExtended.load()){g_state=State::RestartRequired;return false;}
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
 if(context->publication.load()!=Publication::Committed&&!FfxHooks::Coexistence::runtime.FileServicesAllowed())return 0;
 try{std::string request;if(!ReadPath(path,request))return 0;return OpenSelected(*context,request,true);}
 catch(...){return context->publication.load()==Publication::Committed?static_cast<std::uintptr_t>(-1):0;}
}
std::uint32_t KernelBankBytes(unsigned kind,std::uintptr_t bank) noexcept {
 auto* context=g_context.load(std::memory_order_acquire);
 if(!context||!context->kernelTables||!bank||!TryAcquireSRWLockShared(&context->kernelLock))return 0;
 std::uint32_t length=0;
 for(std::size_t i=0;i<KernelSites::Sites.size();++i){const auto& site=KernelSites::Sites[i];
  if(static_cast<unsigned>(site.kind)==kind&&*reinterpret_cast<const std::uintptr_t*>(context->base+site.bank)==bank){length=context->kernelLength[i].load();break;}}
 ReleaseSRWLockShared(&context->kernelLock);return length;
}
bool DecodeNameLiteral(const unsigned char* bytes,std::size_t length,char* output,std::size_t capacity) noexcept {
 try {
  if(!bytes||!output||!capacity||capacity>4096)return false;
  const Font native{};const Font* font=&native;
  auto* context=g_context.load(std::memory_order_acquire);
  if(context&&context->fontReady){const auto& manifest=context->files.Description();
   if(context->metrics>=manifest.resources.size())return false;
   const auto& metric=manifest.resources[context->metrics].id;
   for(const auto& candidate:manifest.fonts)if(candidate.metrics==metric){font=&candidate;break;}}
  std::string text;if(!DecodeLiteral(*font,bytes,length,text)||text.size()>=capacity)return false;
  std::memcpy(output,text.c_str(),text.size()+1);return true;
 }catch(...){return false;}
}
bool EncodeFieldLiteral(const char* literal,unsigned char* output,std::size_t capacity,std::size_t& size) noexcept {
 try {
  if(!literal||!output||!capacity||capacity>4096)return false;
  auto* context=g_context.load(std::memory_order_acquire);
  if(!context||!context->fontReady)return false;
  const auto& manifest=context->files.Description();
  if(context->metrics>=manifest.resources.size())return false;
  const auto& metric=manifest.resources[context->metrics].id;
  const Font* font=nullptr;for(const auto& candidate:manifest.fonts)if(candidate.metrics==metric){font=&candidate;break;}
  if(!font)return false;
  std::vector<std::uint8_t> result;result.reserve(capacity);
  const char* first=literal;std::size_t total=0;
  for(const char* at=literal;;++at){
   if(++total>8192)return false;
   if(*at&&*at!='\n')continue;
   std::vector<std::uint8_t> line;std::string error;
   if(!EncodeLiteral(*font,std::string_view(first,at-first),capacity-result.size()-1,line,error))return false;
   result.insert(result.end(),line.begin(),line.end());
   if(!*at)break;
   if(result.size()+1>=capacity)return false;
   result.push_back(3);first=at+1;
  }
  if(result.size()>=capacity)return false;
  result.push_back(0);std::memcpy(output,result.data(),result.size());size=result.size();return true;
 } catch(...) {return false;}
}
Snapshot Inspect() noexcept {
 Snapshot out;out.state=g_state.load();if(auto* context=g_context.load()){
  out.installed=context->installed;out.fontReady=context->fontReady;
  out.textOpens=context->textOpens;out.fontOpens=context->fontOpens;out.nativeFallbacks=context->fallbacks;
  out.retainedOpens=context->retainedOpens;
  out.kernelTables=context->kernelTables&&context->installed;out.kernelLoads=context->kernelLoads;
  out.kernelExtendedLoads=context->kernelExtended;out.kernelRelocations=context->kernelRelocations;
  if(context->publication.load()==Publication::Committed)out.retainedResources=static_cast<std::uint32_t>(context->pack.retainedCount);
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
