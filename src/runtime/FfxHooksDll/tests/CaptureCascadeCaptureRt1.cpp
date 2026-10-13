#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/CaptureCascadeCapture.h"
#include "../hooks/CaptureCascadeCore.h"
#include <array>
#include <cstdio>
#include <cstring>
namespace {bool enabled=false,bound=true;unsigned records=0,recorded=0,messages=0,lastMessage=0,checks=0,failures=0;
void Check(bool v,const char* text){++checks;if(!v){++failures;std::printf("FAIL: %s\n",text);}}
void Log(const char* text){std::printf("%s",text);}
int __cdecl Message(int,unsigned,int id,int,int){++messages;lastMessage=static_cast<unsigned>(id);return 1;}
bool Bytes(std::uintptr_t at,const void* bytes,std::size_t size){DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),size,PAGE_EXECUTE_READWRITE,&old))return false;std::memcpy(reinterpret_cast<void*>(at),bytes,size);return VirtualProtect(reinterpret_cast<void*>(at),size,old,&ignored)&&FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),size);}
bool Jump(std::uintptr_t at,std::uintptr_t destination){unsigned char code[5]={0xe9};const auto rel=static_cast<std::int32_t>(destination-at-5);std::memcpy(code+1,&rel,4);return Bytes(at,code,5);}
}
namespace FfxHooks::CaptureCascade::Runtime {
bool Requested() noexcept {return enabled;}
bool Ready() noexcept {return enabled&&bound;}
bool RecordCapture(unsigned model) noexcept {if(!Ready())return false;++records;recorded=model;return true;}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    using namespace FfxHooks;using namespace CaptureCascade;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(image&&PrivatePeFixture::NormalizeRelocations(image),"exact private PE maps with normalized relocations");if(failures)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    Check(!Capture::Start(base,Log),"default OFF does not install the capture hook");
    enabled=true;Check(Capture::Start(base,Log),"native Capture weapon and death producers match before install");if(failures)return 1;
    Check(Jump(base+ExecutableProfile::Rva<0x39CFF0>(),reinterpret_cast<std::uintptr_t>(&Message)),"isolated message queue dependency is supplied after admission");
    std::array<unsigned char,32*0xF90> actors{};const auto pool=reinterpret_cast<std::uintptr_t>(actors.data());
    std::memcpy(reinterpret_cast<void*>(base+ExecutableProfile::Rva<0xD334CC>()),&pool,4);
    *reinterpret_cast<unsigned char*>(base+ExecutableProfile::Rva<0xD2A8E0>())=1;
    auto* actor=actors.data()+18*0xF90;
    const std::uint16_t slot=18,monster=0x1000+334,arena=255;
    std::memcpy(actor+0xC,&slot,2);std::memcpy(actor+0xE,&monster,2);std::memcpy(actor+0x6D6,&arena,2);actor[0xDD0]=1;
    // Read the actual relocated counter operand; the runtime never accesses it
    // for a Dark Aeon. The normal path below executes the original counter code.
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    constexpr unsigned getter=0x390AF0;
#else
    constexpr unsigned getter=0x390B40;
#endif
    std::uintptr_t counts=0;std::memcpy(&counts,reinterpret_cast<void*>(base+getter+14),4);
    std::array<unsigned char,104> before{};std::memcpy(before.data(),reinterpret_cast<void*>(counts),before.size());
    auto* thunk=static_cast<unsigned char*>(VirtualAlloc(nullptr,64,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(thunk!=nullptr,"private native-call thunk allocated");if(!thunk)return 1;
    // cdecl thunk forwards both arguments into the real death-handler call site,
    // producing the exact return address required by the runtime evidence gate.
    const unsigned char forward[]={0xff,0x74,0x24,0x08,0xff,0x74,0x24,0x08};std::memcpy(thunk,forward,sizeof(forward));
    Check(Jump(reinterpret_cast<std::uintptr_t>(thunk+8),base+ExecutableProfile::Rva<0x38C8F6>()-5),"thunk enters the verified native death call");
    const unsigned char exit[]={0x83,0xc4,0x08,0xc3};Check(Bytes(base+ExecutableProfile::Rva<0x38C8F6>(),exit,sizeof(exit)),"only the post-call private continuation is bounded");
    const auto invoke=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(thunk);
    Check(invoke(18,255)==1&&records==1&&recorded==334&&lastMessage==0x300A,"native Dark Aeon capture records and emits ordinary capture success");
    Check(!std::memcmp(before.data(),reinterpret_cast<void*>(counts),before.size()),"Dark Aeon capture changes no ordinary Arena counter");
    actor[0xDD0]=0;Check(invoke(18,255)==0&&records==1&&lastMessage==0x300C,"missing native Capture marker preserves uncapturable behavior");actor[0xDD0]=1;
    const auto direct=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(base+ExecutableProfile::Rva<0x390B80>());
    Check(direct(18,255)==0&&records==1,"foreign caller cannot forge a Capture event");
    bound=false;Check(invoke(18,255)==0&&records==1,"unbound save never records a capture");bound=true;
    *reinterpret_cast<unsigned char*>(counts+5)=0;
    Check(invoke(18,5)==1&&*reinterpret_cast<unsigned char*>(counts+5)==1&&records==1,"ordinary Arena capture still executes the real native increment");
    Capture::Stop();Check(invoke(18,255)==0&&records==1,"logical teardown restores vanilla behavior through original trampoline");
    std::printf("CaptureCascadeCaptureRt1: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
