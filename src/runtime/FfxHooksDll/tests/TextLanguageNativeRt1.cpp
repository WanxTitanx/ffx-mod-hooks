#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <malloc.h>
#include <array>
#include <algorithm>
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
#include "../hooks/TextLanguageKernelSites.h"
#include "TextLanguagePeFixture.inc"
#include "TextLanguageFixtureRvas.h"
using namespace FfxHooks::TextLanguage;
namespace N=FfxHooks::TextLanguage::Native;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s: %s\n",label,N::Detail());}}
std::string FixtureMember(const Resource& resource){
 // Observed native keys: ffx_ps2 for kernel/event data, ffx_data/gamedata for
 // Phyre graphics. The old harness incorrectly created ffx_data/ffx_ps2 too.
 constexpr std::string_view kernel="/ffx_data/ffx_ps2/";
 return resource.request.compare(0,kernel.size(),kernel)==0?resource.request.substr(10):resource.request.substr(1);
}
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
// Kernel table adapter fixture. The game heap, alignment/accounting helpers and
// the regional reader are observed stubs shared by the native case bodies and the
// adapter; synthetic payloads cover both sides of the 16-bit size limit.
namespace KernelFixture {
struct Block {void* address;std::uint32_t size;};
std::vector<Block> blocks;std::size_t mark=0;
std::vector<void*> freed;std::vector<std::uint32_t> alignments,accounted;std::vector<std::string> names,lines;
std::uint32_t payload=0;
bool stopInRead=false,stopResult=true;
unsigned char Pattern(std::uint32_t at){return static_cast<unsigned char>((at*131u)^(at>>8)^0x5Au);}
void* __cdecl GameAlloc(std::uint32_t size){void* block=_aligned_malloc(size?size:16u,64);blocks.push_back({block,size});return block;}
void __cdecl GameFree(void* block){freed.push_back(block);}
void __cdecl Align(std::uint32_t value){alignments.push_back(value);}
void __cdecl Account(void*,std::uint32_t size){accounted.push_back(size);}
std::uint32_t __cdecl Read(const char* name,void* staging){
 names.emplace_back(name);
 // Like the native reader: the 16-byte-rounded extent lands in the caller's buffer.
 const auto rounded=(payload+15u)&~15u;auto* out=static_cast<unsigned char*>(staging);
 for(std::uint32_t at=0;at<rounded;++at)out[at]=Pattern(at+payload);
 if(stopInRead){stopInRead=false;stopResult=N::Stop();}
 return rounded;
}
void Capture(const char* line){lines.emplace_back(line?line:"");}
void Reset(std::uint32_t next){mark=blocks.size();freed.clear();alignments.clear();accounted.clear();names.clear();payload=next;}
std::size_t NewBlocks(){return blocks.size()-mark;}
std::uint32_t Allocated(const void* address){for(const auto& block:blocks)if(block.address==address)return block.size;return UINT32_MAX;}
bool Holds(const void* bank,std::uint32_t seed,std::uint32_t size){
 const auto* bytes=static_cast<const unsigned char*>(bank);if(!bytes)return false;
 for(std::uint32_t at=0;at<size;++at)if(bytes[at]!=Pattern(at+seed))return false;
 return true;
}
bool Logged(const char* text){for(const auto& line:lines)if(line.find(text)!=std::string::npos)return true;return false;}
bool Released(const void* block){return std::find(freed.begin(),freed.end(),block)!=freed.end();}
}
using KernelLoad=int(__cdecl*)(int);
void KernelChecks(unsigned char* image,const std::string& mode){
 namespace K=FfxHooks::TextLanguage::KernelSites;namespace F=KernelFixture;
 const auto load=reinterpret_cast<KernelLoad>(image+K::Load);
 const auto bankOf=[&](std::uint32_t rva){void* bank=nullptr;std::memcpy(&bank,image+rva,4);return bank;};
 const auto size16=[&](std::size_t n){std::uint16_t value=0;std::memcpy(&value,image+K::Sites[n].size16,2);return value;};
 const bool stopDuringRead=mode=="kernel-stop-reading"||mode=="cooperative-kernel-stop-reading";
 const bool armed=mode=="kernel-tables"||mode=="cooperative-kernel-tables"||stopDuringRead;
 Check(N::Inspect().installed&&N::Inspect().kernelTables==armed,"the 32-bit kernel adapter is armed only when enabled before the first table load");
 if(mode=="kernel-tables-late"){
  Check(F::Logged("loaded before hook startup"),"a table loaded before startup keeps the 16-bit loader and says why");
  return;
 }
 if(armed)Check(F::Logged("adapter armed"),"the armed adapter is reported");
 if(stopDuringRead){
  F::Reset(0x10010u);F::stopInRead=true;
  Check(load(0)==-1,"the already-started kernel load completes");
  Check(!F::stopResult,"Stop defers while an oversized kernel read is in flight");
  Check(N::Inspect().installed&&N::Inspect().kernelTables,
        "a completed extended bank never loses its 32-bit loader during Stop");
  Check(F::Allocated(bankOf(K::Sites[0].bank))==0x10010u,"the in-flight load really published a 32-bit bank");
  Check(!N::Stop()&&N::Inspect().kernelTables,"after publication Stop keeps the dependent adapter installed");
  return;
 }
 // 0x10010 bytes: the native u16 global wraps to 0x0010 after rounding.
 F::Reset(0x10010u);Check(load(0)==-1,"the command case completes");
 const auto command=bankOf(K::Sites[0].bank);
 Check(F::names==std::vector<std::string>{"command"},"the native regional reader receives the native table name");
 Check(size16(0)==0x0010u,"the native u16 global keeps its truncated value");
 Check(F::freed.size()==1&&F::Allocated(F::freed[0])==0x25800u,"the native staging buffer is released");
 if(!armed){
  // Red without the adapter: the unmodified loader allocates and copies 16 bytes.
  Check(command&&F::Allocated(command)==0x0010u&&F::accounted==std::vector<std::uint32_t>{0x0010u},
        "without the adapter the native loader truncates command.bin above 0xFFF0 bytes");
  Check(N::Stop()&&!N::Inspect().installed,"an unpublished adapter-free session still retires");
  return;
 }
 Check(command&&F::Allocated(command)==0x10010u&&F::Holds(command,0x10010u,0x10010u),"the command bank holds every byte above the 16-bit limit");
 Check(F::alignments==std::vector<std::uint32_t>{0x40}&&F::accounted==std::vector<std::uint32_t>{0x10010u},
       "alignment and memory accounting follow the native order with the full size");
 Check(N::KernelBankBytes(0,reinterpret_cast<std::uintptr_t>(command))==0x10010u,"published bank exposes its complete 32-bit extent to native UI readers");
 Check(N::Inspect().kernelLoads==1&&N::Inspect().kernelExtendedLoads==1&&F::Logged("command.bin loaded with 65552 bytes"),"the extended load is counted and reported");
 F::Reset(0x10010u);
 Check(load(0)==-1&&bankOf(K::Sites[0].bank)==command&&F::NewBlocks()==1&&F::accounted.empty()&&F::Holds(command,0x10010u,0x10010u),
       "a reload that fits reuses the bank like the native loader");
 F::Reset(0x100u);
 Check(load(0)==-1&&bankOf(K::Sites[0].bank)==command&&F::NewBlocks()==1&&F::Holds(command,0x100u,0x100u)&&size16(0)==0x100u,
       "a smaller reload copies into the same bank");
 Check(N::KernelBankBytes(0,reinterpret_cast<std::uintptr_t>(command))==0x100u,"smaller reload reports current length rather than allocation capacity");
 F::Reset(0x12000u);Check(load(0)==-1,"a larger command reload completes");
 const auto grown=bankOf(K::Sites[0].bank);
 Check(grown&&grown!=command&&F::Allocated(grown)==0x12000u&&F::Holds(grown,0x12000u,0x12000u)&&!F::Released(command)&&!F::Released(grown)&&
       N::Inspect().kernelRelocations==1&&F::Logged("outgrew its bank"),"a larger reload gets a fresh bank and the cached one is never freed");
 for(std::size_t n=1;n<K::Sites.size();++n){
  const auto size=0x10020u+static_cast<std::uint32_t>(n)*0x40u;F::Reset(size);
  Check(load(K::Sites[n].kind)==-1,"an owned table case completes");
  const auto bank=bankOf(K::Sites[n].bank);
  Check(bank&&F::Allocated(bank)==size&&F::Holds(bank,size,size)&&F::names==std::vector<std::string>{n==1?"a_ability":"item"},
        "a_ability and item keep every byte above the 16-bit limit");
 }
 // Cases 1, 4 and 5 run the original body through the trampoline. Their banks
 // (ply_rom 0xD2A938, monmagic1 0xD2A930) share one RVA in both reviewed PEs.
 F::Reset(0x200u);
 Check(load(1)==-1&&F::names==std::vector<std::string>{"ply_rom"}&&F::Allocated(bankOf(0xD2A938))==0x200u&&
       F::Holds(bankOf(0xD2A938),0x200u,0x200u)&&F::Released(F::blocks[F::mark].address),"ply_rom keeps the native case body");
 F::Reset(0x12345u);
 Check(load(4)==-1&&F::names==std::vector<std::string>{"monmagic1"}&&F::Allocated(bankOf(0xD2A930))==0x12350u&&
       F::Holds(bankOf(0xD2A930),0x12345u,0x12350u),"monmagic1 keeps the native 32-bit case body");
 Check(load(6)==0,"the loader still ends its table sequence natively");
 Check(N::Inspect().kernelLoads==6&&N::Inspect().kernelExtendedLoads==5,"only owned cases are served by the adapter");
 Check(!N::Stop()&&N::Inspect().state==N::State::RestartRequired&&N::Inspect().kernelTables,
       "an extended table keeps the adapter installed until restart");
}
struct Stream {HANDLE file=INVALID_HANDLE_VALUE;void* archive=nullptr;};
using Open=int(__thiscall*)(Stream*,const char*,int,int,int,int);
using Size=std::uint32_t(__thiscall*)(Stream*);
using Get=std::uint32_t(__thiscall*)(Stream*,void*,std::uint32_t);
using Seek=std::uint32_t(__thiscall*)(Stream*,std::int32_t,int);
using Close=int(__thiscall*)(Stream*);
using FontCall=int(__cdecl*)(const unsigned char*);
namespace BudgetFixture {
// Independently disassembled native accounting finalizer and its only caller.
constexpr std::uint32_t Finish=FfxHooks::ExecutableProfile::Steam20261001?0x399F20u:0x399F30u;
constexpr std::uint32_t Call=FfxHooks::ExecutableProfile::Steam20261001?0x381C23u:0x381CF3u;
constexpr std::uint32_t Used=FfxHooks::ExecutableProfile::Steam20261001?0xD36000u:0xD35FF8u;
constexpr std::uint32_t Padding=Used+4;
constexpr std::uint32_t Fatal=FfxHooks::ExecutableProfile::Steam20261001?0x39F210u:0x39F230u;
unsigned errors=0;
std::string reason;
void __cdecl Error(const char* text){++errors;reason=text?text:"";}
void SetUsed(unsigned char* image,std::uint32_t value){
 std::memcpy(image+Used,&value,4);const std::uint32_t zero=0;std::memcpy(image+Padding,&zero,4);
 errors=0;reason.clear();KernelFixture::Reset(0);
}
void Checks(unsigned char* image,const std::filesystem::path& reference,FontCall font){
 // All accounting instructions execute unmodified. Only the fatal sink returns
 // to the harness instead of spinning forever, and the heap records padding.
 Jump(image,Fatal,reinterpret_cast<void*>(&Error));
 Jump(image,FfxHooks::TextLanguage::KernelSites::HeapAlloc,reinterpret_cast<void*>(&KernelFixture::GameAlloc));
 // Execute the original PUSH/CALL/ADD sequence, retaining its actual return RVA.
 image[Call+8]=0xC3;
 FlushInstructionCache(GetCurrentProcess(),image,0x700000);
 const auto finish=reinterpret_cast<void(__cdecl*)()>(image+Call-5);
 const auto direct=reinterpret_cast<void(__cdecl*)(std::uint32_t)>(image+Finish);
 SetUsed(image,0x70040u);finish();
 Check(errors==1&&reason=="mem_over","unpublished translations cannot enlarge the native memory budget");
 auto metrics=Read((reference/"font/base.ftc").string().c_str());
 Check(font(metrics.data())==0&&N::Inspect().fontReady,"budget regression publishes the real paired package");
 SetUsed(image,508672u);finish();
 Check(errors==0,"the observed 508672-byte boot completes within the admitted translation budget");
 std::uint32_t actual=0;std::memcpy(&actual,image+Used,4);
 Check(actual==508672u,"budget extension never rewrites the native accounting counter");
 SetUsed(image,0x70040u);direct(0x70000u);
 Check(errors==1&&reason=="mem_over","calls outside the examined boot caller retain the native limit");
 SetUsed(image,0x200000u);finish();
 Check(errors==1&&reason=="mem_over","unexplained memory growth still reaches the native limit check");
 Check(!N::Stop(),"published translated banks retain their budget adapter until restart");
 SetUsed(image,508672u);finish();
 Check(errors==0,"logical stop keeps the budget needed by published translated banks");
}
}
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
 if(argc!=4&&argc!=5){std::fprintf(stderr,"Usage: TextLanguageNativeRt1 FFX.exe PACK REFERENCE [active|concurrent|early|stop|stop-validated|stop-reading|stop-committed|bad-font|late|kernel-tables|kernel-tables-off|kernel-tables-late|cooperative-kernel-tables|kernel-stop-reading|cooperative-kernel-stop-reading]\n");return 2;}
 const std::string mode=argc==5?argv[4]:"active";
 const bool budgetMode=mode=="kernel-budget"||mode=="cooperative-kernel-budget";
 const bool cooperative=mode=="cooperative"||mode=="cooperative-conflict"||mode=="cooperative-stop"||mode=="cooperative-kernel-budget"||
  mode=="cooperative-early"||mode=="cooperative-stop-before-font"||mode=="cooperative-kernel-tables"||mode=="cooperative-kernel-stop-reading";
 const bool beforeFont=mode=="early"||mode=="boot-order"||mode=="atlas-first"||mode=="stop-before-font"||
  mode=="bad-font-after-resource"||mode=="cooperative-early"||mode=="cooperative-stop-before-font";
 const bool kernelMode=mode=="kernel-tables"||mode=="kernel-tables-off"||mode=="kernel-tables-late"||mode=="cooperative-kernel-tables"||mode=="kernel-stop-reading"||mode=="cooperative-kernel-stop-reading";
 if(!cooperative&&!beforeFont&&!kernelMode&&!budgetMode&&mode!="active"&&mode!="overlay-drift"&&mode!="concurrent"&&mode!="concurrent-early"&&mode!="stop"&&mode!="stop-validated"&&mode!="stop-reading"&&mode!="early-stop-reading"&&mode!="early-invalid-source"&&mode!="stop-committed"&&mode!="bad-font"&&mode!="late")return 2;
 const auto executable=std::filesystem::absolute(argv[1]);const auto package=std::filesystem::absolute(argv[2]);
 const auto reference=std::filesystem::absolute(argv[3]);
 const auto temporary=std::filesystem::current_path()/("mod006-native-"+std::to_string(GetCurrentProcessId()));
 if(std::filesystem::exists(temporary))return 3;
 Files input;std::string error;
 if(!input.Initialize(package.wstring(),error)){std::fprintf(stderr,"Fixture pack rejected: %s\n",error.c_str());return 4;}
 for(const auto& r:input.Description().resources){
  const auto target=temporary/FixtureMember(r);std::filesystem::create_directories(target.parent_path());
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
 if(kernelMode){
  namespace K=FfxHooks::TextLanguage::KernelSites;
  Jump(image,K::HeapAlloc,reinterpret_cast<void*>(&KernelFixture::GameAlloc));
  Jump(image,K::HeapFree,reinterpret_cast<void*>(&KernelFixture::GameFree));
  Jump(image,K::Align,reinterpret_cast<void*>(&KernelFixture::Align));
  Jump(image,K::Account,reinterpret_cast<void*>(&KernelFixture::Account));
  Jump(image,K::Read,reinterpret_cast<void*>(&KernelFixture::Read));
  FlushInstructionCache(GetCurrentProcess(),image,0x700000);
  if(mode=="kernel-tables-late"){const std::uint32_t loaded=0x1000;std::memcpy(image+K::Sites[0].bank,&loaded,4);}
  settings.extendedKernelTables=mode!="kernel-tables-off";
 }
 if(mode=="late"){
  const std::uint32_t occupied=1;std::memcpy(image+TextFixtureRva<0x1441DA4>(),&occupied,4);
  Check(!N::Start(base,settings)&&N::Inspect().state==N::State::TooLate&&!N::Inspect().installed,"late activation cannot replace an initialized font cache");
 }else Check(N::Start(base,settings,kernelMode?&KernelFixture::Capture:mode=="bad-font"?&ThrowingLogger:mode=="stop-validated"?&StopAfterValidation:nullptr)&&N::Inspect().installed,"exact PE arms its owned adapters");
 if(cooperative)Check(std::memcmp(openBefore.data(),image+TextFixtureRva<0x208100>(),openBefore.size())==0,"cooperative transport never installs a competing OpenStream hook");
 if(budgetMode){
  BudgetFixture::Checks(image,reference,font);
  std::filesystem::current_path(cwd);std::filesystem::remove_all(temporary);
  std::printf("TextLanguageNative RT1 (%s): %u checks, %u failures\n",mode.c_str(),checks,failures);
  return failures?1:0;
 }
 if(kernelMode){
  // The adapter is independent of the file transport: cooperative mode owns the
  // font and kernel loader only.
  KernelChecks(image,mode);
  std::filesystem::current_path(cwd);std::filesystem::remove_all(temporary);
  std::printf("TextLanguageNative RT1 (%s): %u checks, %u failures\n",mode.c_str(),checks,failures);
  return failures?1:0;
 }
 if(N::Inspect().installed){
  auto originalMetrics=Read((reference/"font/base.ftc").string().c_str());
  std::array<unsigned char,8> saveWriter{};std::memcpy(saveWriter.data(),image+TextFixtureRva<0x4B3EE3>(),8);
  const auto& selectedResource=input.Description().resources[0];
  const auto selectedRequest="../../../"+FixtureMember(selectedResource);
  // A changed native source (mod redeploy) retains only that resource; the rest of
  // the package is still published and paired with the font.
  const Resource* changedSource=mode=="early-invalid-source"?&selectedResource:nullptr;
  if(mode=="overlay-drift"){
   const auto& entries=input.Description().resources;
   const auto changed=std::find_if(entries.begin(),entries.end(),[](const Resource& r){return r.id=="monmagic2-bin";});
   Check(changed!=entries.end(),"the real mod drift resource is present in the package");
   if(changed!=entries.end()){
    changedSource=&*changed;std::string actualHash;
    const auto bytes=Read((reference/changed->path).string().c_str());
    Check(Fingerprint(bytes,actualHash)&&actualHash!=changed->sourceSha256,"the actual overlay fixture differs from the translation's bound source");
   }
  }
  bool activeExpected=cooperative||beforeFont||mode=="active"||mode=="concurrent"||mode=="concurrent-early"||mode=="stop-committed"||changedSource!=nullptr;
  if(mode=="early-invalid-source"){
   // Mutate only this process's private native-source copy, never the input
   // package/reference or the installed game's resource tree.
   const auto source=temporary/FixtureMember(selectedResource);
   std::fstream changed(source,std::ios::in|std::ios::out|std::ios::binary);char first=0;
   changed.read(&first,1);first^=1;changed.seekp(0);changed.write(&first,1);changed.close();
   Stream early;Check(open(&early,selectedRequest.c_str(),1,0,0,1)==0&&size(&early)==selectedResource.sourceSize,
       "a changed native source keeps that resource on its original loader");close(&early);
   const auto retained=N::Inspect();
   Check(retained.state==N::State::Armed&&retained.retainedResources==1&&retained.retainedOpens==1&&
         !retained.textOpens&&!retained.fontOpens&&!retained.fontReady,
       "only the changed resource is retained before the natural font registration");
  }
  const auto readPaired=[&](const Resource& entry){
   const auto request="../../../"+FixtureMember(entry);Stream stream;Bytes expected;
   const bool metrics=entry.family==Family::Metrics;
   if(metrics)expected=Read((reference/entry.path).string().c_str());else input.Read(entry,expected);
   Bytes actual(expected.size());
   const bool opened=open(&stream,request.c_str(),1,0,0,1)==0;
   Check(opened&&size(&stream)==expected.size()&&get(&stream,actual.data(),static_cast<std::uint32_t>(actual.size()))==actual.size()&&actual==expected,
       metrics?"the native registrar still receives its source metrics":"resource-first boot reads the complete admitted translation before font registration");
   close(&stream);
  };
  if(beforeFont){
   if(mode=="boot-order"){
    // Actual Steam/File Loader boot: these kernel banks precede the four atlas
    // pages, which themselves precede the native Western metric registration.
    for(const auto* id:{"monster1-bin","monster2-bin","monster3-bin"}){
     const auto& entries=input.Description().resources;
     const auto entry=std::find_if(entries.begin(),entries.end(),[&](const Resource& r){return r.id==id;});
     Check(entry!=entries.end(),"the real boot kernel resource is present in the fixture");if(entry!=entries.end())readPaired(*entry);
    }
   }else if(mode!="atlas-first")readPaired(selectedResource);
   for(const auto& entry:input.Description().resources)if(entry.family==Family::Atlas)readPaired(entry);
   for(const auto& entry:input.Description().resources)if(entry.family==Family::Metrics)readPaired(entry);
   const auto early=N::Inspect();
   Check(!early.fontReady&&(early.textOpens||early.fontOpens),"resource publication precedes the natural native font call");
   if(mode=="stop-before-font"||mode=="cooperative-stop-before-font"){
    Check(!N::Stop()&&N::Inspect().state==N::State::RestartRequired,"stop cannot abandon a package already cached before font registration");
    if(cooperative)FfxHooks::Coexistence::runtime.Stop();
    readPaired(selectedResource);
   }
  }else if(mode=="stop")Check(N::Stop()&&!N::Inspect().installed,"stop before publication restores original native entries");
  if(mode=="bad-font"||mode=="bad-font-after-resource")originalMetrics[64]^=1;
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
  }else if(mode=="concurrent-early"||mode=="early-stop-reading"){
   sourceEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sourceRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   const auto fontStarted=CreateEventW(nullptr,TRUE,FALSE,nullptr),fontDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   Check(sourceEntered&&sourceRelease&&fontStarted&&fontDone,"resource-first race barriers allocate");
   pauseSource=true;int opened=-1,registered=-1;std::uint32_t observedSize=0;
   std::thread reader([&]{Stream stream;opened=open(&stream,selectedRequest.c_str(),1,0,0,1);observedSize=size(&stream);close(&stream);});
   const bool entered=WaitForSingleObject(sourceEntered,5000)==WAIT_OBJECT_0;
   Check(entered,"the first resource validates the entire package through native source IO");
   if(mode=="early-stop-reading")Check(!N::Stop(),"stop cancels resource-first preparation without waiting for its IO");
   std::thread registrar([&]{SetEvent(fontStarted);registered=font(originalMetrics.data());SetEvent(fontDone);});
   Check(WaitForSingleObject(fontStarted,5000)==WAIT_OBJECT_0,"the native font thread starts while resource admission is pending");
   if(entered)Check(WaitForSingleObject(fontDone,100)==WAIT_TIMEOUT,"font registration waits for the resource admission transaction");
   SetEvent(sourceRelease);reader.join();registrar.join();
   Check(registered==0&&opened==0&&observedSize==(activeExpected?selectedResource.size:selectedResource.sourceSize),
       "the first resource and concurrent registrar agree on translated or original bytes");
   if(!activeExpected)Check(N::Stop()&&!N::Inspect().installed,"cancelled resource admission drains and restores the native entries");
   CloseHandle(sourceEntered);CloseHandle(sourceRelease);CloseHandle(fontStarted);CloseHandle(fontDone);
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
   if(mode=="bad-font-after-resource")Check(N::Inspect().state==N::State::RestartRequired,
       "foreign metrics discovered after publication retain the validated pair and request restart");
  }
  Check(N::Inspect().fontReady==activeExpected,"native font publication matches the admitted or rejected session");
  if(!activeExpected){
   Stream unchanged;Check(open(&unchanged,selectedRequest.c_str(),1,0,0,1)==0&&size(&unchanged)==selectedResource.sourceSize,"failed or stopped admission preserves original resource routing");close(&unchanged);
   if(mode=="bad-font")Check(N::Inspect().state==N::State::InvalidPack,"foreign font identity is diagnosed without activation");
   if(mode=="stop-validated")Check(N::Stop()&&!N::Inspect().installed,
       "stop after validation wins publication and permits hook retirement after callback drain");
  }
  if(activeExpected&&N::Inspect().fontReady){
  std::uint32_t table=0;std::memcpy(&table,image+TextFixtureRva<0x1441DA4>(),4);
  Check(table&&reinterpret_cast<unsigned char*>(table)[242-48]==31&&reinterpret_cast<unsigned char*>(table)[244-48]==42,"real Western width consumer receives custom glyph metrics");
#ifdef FFXHOOKS_TARGET_STEAM_20261001
  if(mode=="active"&&table)CheckNativeGlyphConsumer(image,reinterpret_cast<const unsigned char*>(table),
      input.Description().fonts[0].profile==2?6u:4u);
#endif
  const auto& entries=input.Description().resources;
  const auto& resource=*std::find_if(entries.begin(),entries.end(),[&](const Resource& r){
   return &r!=changedSource&&r.family!=Family::Metrics&&r.family!=Family::Atlas;});
  const auto request="../../../"+FixtureMember(resource);
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
   const auto name="../../../"+FixtureMember(entry);Stream read;
   if(&entry==changedSource){
    const auto native=Read((temporary/FixtureMember(entry)).string().c_str());Bytes observedNative(native.size());
    Check(open(&read,name.c_str(),1,0,0,1)==0&&size(&read)==native.size()&&
          get(&read,observedNative.data(),static_cast<std::uint32_t>(observedNative.size()))==observedNative.size()&&observedNative==native,
          "a retained resource preserves every byte of the changed native source, including gameplay fields");
    close(&read);continue;
   }
   Bytes wanted;input.Read(entry,wanted);Bytes got(entry.size);
   const bool opened=open(&read,name.c_str(),1,0,0,1)==0;
   Check(opened&&size(&read)==entry.size&&get(&read,got.data(),static_cast<std::uint32_t>(got.size()))==got.size()&&got==wanted,"every declared text/event/atlas reaches the native read and size consumer");
   close(&read);
  }
  if(changedSource)Check(N::Inspect().retainedResources==1&&N::Inspect().retainedOpens>0,
      "only the changed resource is retained while the other text and font resources remain active");
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
