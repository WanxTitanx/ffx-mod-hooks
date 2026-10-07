// Jarvis-HOOK: both real owners in an isolated mapped executable; no game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/SharedElementRuntime.h"
#include "../hooks/VanguardRuntime.h"
#include "../shared/Config.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace S=FfxHooks::SharedElement;
namespace V=FfxHooks::Vanguard;
namespace C=FfxHooks::Config;
static unsigned checks=0,failures=0,resolved=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
static void Log(const char* text){std::fputs(text,stdout);}
static bool Resolve(const unsigned char*,const unsigned char*,unsigned mask,int amount,int* out) noexcept {
    ++resolved;if(mask!=0x101)return false;*out=amount/4;return true;
}
static bool Foreign(const unsigned char*,const unsigned char*,unsigned,int,int*) noexcept {return false;}
int main(int argc,char** argv){
    if(argc!=3||(std::strcmp(argv[2],"vanguard-first")&&std::strcmp(argv[2],"element-first")))return 2;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(image),"private executable relocations are normalized");
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    std::array<unsigned char,31*0xF90> actors{};
    const auto address=reinterpret_cast<std::uintptr_t>(actors.data());
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),&address,4);
    for(unsigned i=0;i<31;++i){actors[i*0xF90+0xC]=static_cast<unsigned char>(i);actors[i*0xF90+0xDC8]=1;}
    auto* target=actors.data()+18*0xF90;target[0x5DD]=2;
    std::array<unsigned char,96> command{};
    const auto native=reinterpret_cast<S::NativeFn>(base+S::kRva);
    Check(native(target,command.data(),1,1000)==1000,"unmodified native Fire does not exploit Ice weakness");
    C::ResetForTests();C::LoadTextForTests("[vanguard]\nelement_opposite_weakness=1\n[f8_authority]\nvanguard_element_opposite_weakness=1\n","C:\\private-shared-element.ini");
    const bool first=std::strcmp(argv[2],"vanguard-first")==0;
    if(first){Check(V::Start(base,false,Log),"Vanguard starts before external affinity");Check(S::Start(base),"shared affinity joins existing Vanguard ownership");}
    else {Check(S::Start(base),"shared affinity installs before Vanguard");Check(V::Start(base,false,Log),"Vanguard accepts the verified shared entry");}
    if(failures)return 1;
    Check(V::Active()&&native(target,command.data(),1,1000)==1250,"real Vanguard retains its opposite-element rule");
    static const S::Resolver resolver{Resolve},foreign{Foreign};
    Check(S::RegisterResolver(&resolver)&&S::RegisterResolver(&resolver),"same resolver registration is idempotent");
    Check(!S::RegisterResolver(&foreign),"another resolver cannot steal ownership");
    Check(native(target,command.data(),0x101,1000)==250&&resolved==1,"admitted external affinity never compounds Vanguard's multiplier");
    Check(native(target,command.data(),1,1000)==1250&&resolved==2,"unhandled affinity still executes real Vanguard once");
    Check(native(target,command.data(),2,1000)==1500,"direct native weakness is not compounded");
    S::UnregisterResolver(&foreign);
    Check(native(target,command.data(),0x101,-1000)==-250,"a foreign unsubscribe cannot alter signed affinity handling");
    V::RequestStop();
    Check(!V::Active()&&native(target,command.data(),0x101,1000)==250,"Vanguard stop does not disable the external resolver");
    Check(native(target,command.data(),1,1000)==1000,"stopped legacy handling returns exactly to native behavior");
    S::UnregisterResolver(&resolver);
    Check(native(target,command.data(),2,1000)==1500,"both consumers stopped leave native affinity intact");
    std::printf("SHARED_ELEMENT_VANGUARD %s %u/%u passed\n",argv[2],checks-failures,checks);
    return failures?1:0;
}
