// Jarvis-HOOK: real native Vanguard entry ownership, no game session.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "WorkshopFieldFixture.h"
#include "../hooks/OmegaT7Evidence.h"
#include "../hooks/NativeUiHookSupport.h"
#include "../hooks/VanguardRuntime.h"
#include "../shared/Config.h"
#include <array>
#include <cstdio>
namespace FfxHooks::RonsoPool {bool IsSaveIoReady() noexcept{return true;}}
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static bool Profile(std::uintptr_t base){
#ifdef FFXHOOKS_OMEGA_STATUS_OWNER_V1
    return FfxHooks::OmegaT7::Evidence::Profile(base);
#else
    return FfxHooks::NativeUiSupport::Profile(base,FfxHooks::OmegaT7::Evidence::spans);
#endif
}
int main(int argc,char** argv){
    if(argc!=2)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(image),"private exact Steam PE relocations");
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    Check(Profile(base),"Omega raw native witnesses match before other startup owners");
    FfxHooks::Config::LoadTextForTests("[vanguard]\nhero_caution=1\n[f8_authority]\nvanguard_hero_caution=1\n","C:\\private-omega-vanguard.ini");
    Check(FfxHooks::Vanguard::Start(base,false,nullptr),"actual Vanguard native producers install");
    Check(Profile(base),"Omega admission survives the real installed Vanguard status owner");
    for(const auto rva:{0x38AE00u,0x3B4B70u}){
        auto* code=reinterpret_cast<unsigned char*>(base+rva);const auto old=code[8];const unsigned char changed=old^1;
        Check(WorkshopFieldFixture::Write(base+rva+8,&changed,1),"private negative witness mutation");
        Check(!Profile(base),"changed owned entry or unrelated leaf is still rejected");
        Check(WorkshopFieldFixture::Write(base+rva+8,&old,1),"private original witness restored");
    }
    Check(Profile(base),"restored exact owned witnesses regain admission");
    FfxHooks::Vanguard::RequestStop();
    Check(Profile(base),"retained neutral Vanguard wrapper remains a verified native route");
    Check(!Profile(base+1),"ownership cannot be borrowed by a different module identity");
    std::printf("OMEGA_VANGUARD_PROFILE_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
