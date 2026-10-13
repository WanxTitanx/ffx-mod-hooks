// Jarvis-HOOK: isolated native availability, ordered learned/equipment layers.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include <array>
#include <cstdio>
#include <cstring>
#if __has_include("../hooks/SharedCommandAvailability.h")
#include "../hooks/SharedCommandAvailability.h"
namespace S=FfxHooks::SharedCommandAvailability;
static unsigned checks=0,failures=0,learnedCalls=0,equipmentCalls=0;
static void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
static int __cdecl Learned(unsigned char actor,unsigned short command){
    ++learnedCalls;
    if(command==0x316F)return actor==8?1:0;
    return S::Original()(actor,command);
}
static int __cdecl Equipment(unsigned char actor,unsigned short command){
    ++equipmentCalls;const auto native=S::Learned(actor,command);
    return actor==0&&command==0x3016?native|1:native;
}
static int __cdecl Foreign(unsigned char,unsigned short){return 77;}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(image),"private exact PE relocates");
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    std::array<unsigned char,31*0xF90> actors{};
    const auto pointer=reinterpret_cast<std::uintptr_t>(actors.data());
    std::memcpy(reinterpret_cast<void*>(base+FfxHooks::ExecutableProfile::Rva<0xD334CC>()),&pointer,4);
    const auto native=reinterpret_cast<S::Function>(base+S::Rva);
    actors[0x664]=1; // Actor0 knows Attack; all other fixture commands are absent.
    Check(native(0,0x3000)==1&&native(0,0x3016)==0,"native learned bits are the baseline");
    Check(!S::RegisterLearned(Learned),"a consumer cannot register before verified startup");
    Check(S::Start(base),"one profile-gated native availability owner installs");
    const bool first=std::strcmp(argv[2],"learned-first")==0;
    if(first)Check(S::RegisterLearned(Learned),"learned provider registers first");
    Check(S::RegisterEquipment(Equipment),"equipment provider registers");
    if(!first)Check(S::RegisterLearned(Learned),"learned provider registers second");
    Check(S::Start(base)&&S::Profile(base),"another consumer accepts the owned entry");
    Check(S::RegisterLearned(Learned)&&!S::RegisterLearned(Foreign),"same provider is idempotent and foreign replacement fails");
    Check(native(0,0x3000)==1&&learnedCalls==1&&equipmentCalls==1,"ordinary command forwards once through both layers");
    Check(native(8,0x316F)==1&&native(0,0x316F)==0,"Valefor extension does not grant Aero to another actor");
    Check(native(0,0x3016)==1&&native(8,0x3016)==0,"equipment grant retains its own actor boundary");
    S::UnregisterLearned(Foreign);Check(native(8,0x316F)==1,"foreign cleanup cannot steal the learned provider");
    S::UnregisterEquipment(Equipment);
    Check(native(8,0x316F)==1&&native(0,0x3016)==0,"equipment stop leaves the learned layer and native commands intact");
    S::UnregisterLearned(Learned);
    Check(native(0,0x3000)==1&&native(0,0x3016)==0,"last unsubscribe restores native behavior");
    std::array<unsigned char,48> bytes{};std::memcpy(bytes.data(),reinterpret_cast<void*>(base+S::Rva),bytes.size());
    Check(S::MatchesOwned(base,bytes.data(),bytes.size())&&!S::MatchesOwned(base+1,bytes.data(),bytes.size()),"full GridTeach witness is bound to image and entry");
    bytes[20]^=1;Check(!S::MatchesOwned(base,bytes.data(),bytes.size()),"tail drift cannot impersonate the shared entry");
    std::printf("SHARED_COMMAND_AVAILABILITY %s %u/%u passed\n",argv[2],checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL shared command availability owner is missing; GridTeach/Vanguard cannot coexist");return 1;}
#endif
