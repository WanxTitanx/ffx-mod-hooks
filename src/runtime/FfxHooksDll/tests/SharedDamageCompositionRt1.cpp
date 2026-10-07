// Jarvis-HOOK: both real production consumers on one isolated mapped PE.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/VanguardRuntime.h"
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/SharedDamageRuntime.h"
#include "../shared/Config.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace V=FfxHooks::Vanguard;
namespace W=FfxHooks::EquipmentWorkshop;
namespace S=FfxHooks::SharedDamage;
static unsigned checks=0,failures=0,originalCalls=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void Log(const char* text){std::fputs(text,stdout);}
static unsigned __cdecl Endpoint(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned){++originalCalls;return 713;}
static bool ForeignPolicy(const void*,const void*,int) noexcept {return true;}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    const bool workshopFirst=std::strcmp(argv[3],"workshop-first")==0;
    if(!workshopFirst&&std::strcmp(argv[3],"vanguard-first")!=0)return 2;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!image)return 2;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    Check(PrivatePeFixture::NormalizeRelocations(image),"composition fixture normalizes its private image");
    const std::wstring directory(argv[2],argv[2]+std::strlen(argv[2]));
    FfxHooks::Config::ResetForTests();
    Check(FfxHooks::Config::LoadTextForTests("[vanguard]\nhealing_ignore_shell=1\n[f8_authority]\nvanguard_healing_ignore_shell=1\n","C:\\private-composition.ini"),
          "only healing is requested; no other combat option is coupled");
    std::array<unsigned char,31*0xF90> actors{};
    const auto table=reinterpret_cast<std::uintptr_t>(actors.data());
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),&table,4);
    *reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>())=1;
    if(workshopFirst){
        Check(W::StartForTests(base,true,directory.c_str(),Log),"Workshop installs first");
        Check(V::Start(base,false,Log),"Vanguard joins the existing shared entries");
    }else{
        Check(V::Start(base,false,Log),"Vanguard installs first");
        Check(W::StartForTests(base,true,directory.c_str(),Log),"Workshop admits only the exact shared installed bytes");
    }
    Check(V::Active()&&W::Requested(),"both independent consumers remain active");
    if(failures)return 1;
    Check(S::workshop.load()!=nullptr&&S::combat.load()!=nullptr,"one native owner serves both consumers");
    static const S::CombatCallbacks foreign{ForeignPolicy};
    Check(!S::RegisterCombat(&foreign),"a different consumer cannot replace an already owned policy");
    std::array<unsigned char,96> command{};std::array<unsigned char,44> info{};
    for(unsigned kind=1;kind<=2;++kind){
        const auto call=reinterpret_cast<S::ProtectionFn>(base+(kind==1?::FfxHooks::ExecutableProfile::Rva<0x38AE00>(): ::FfxHooks::ExecutableProfile::Rva<0x38AE80>()));
        command[0x20]=static_cast<unsigned char>(kind);info[0xA]=info[0xB]=1;
        unsigned flags=0x21;int divisor=7;const auto prior=info;
        Check(call(command.data(),&flags,&divisor,info.data(),-1000)==-1000&&flags==0x21&&divisor==7&&info==prior,
              "composed native healing retains all guard state and result fields");
        Check(call(command.data(),&flags,&divisor,info.data(),1000)==500,
              "composed native damage keeps ordinary protection");
    }
    W::DamageProducerForTests(reinterpret_cast<void*>(&Endpoint));
    const auto damage=reinterpret_cast<S::DamageFn>(base+::FfxHooks::ExecutableProfile::Rva<0x38E680>());
    Check(damage(0,actors.data(),1,actors.data()+0xF90,command.data(),0x3000,info.data(),0,0,0,0)==713&&originalCalls==1,
          "shared damage enters the Workshop frame and original endpoint exactly once");
    V::RequestStop();command[0x20]=2;unsigned flags=0;int divisor=0;
    const auto shell=reinterpret_cast<S::ProtectionFn>(base+::FfxHooks::ExecutableProfile::Rva<0x38AE80>());
    Check(shell(command.data(),&flags,&divisor,info.data(),-1000)==-500&&W::Requested(),
          "Vanguard stop restores native healing without stopping Workshop");
    W::RequestStop();flags=0;divisor=0;
    Check(shell(command.data(),&flags,&divisor,info.data(),-1000)==-500&&
          !S::workshop.load()&&!S::combat.load(),"stopped shared entries stay safely native-only");
    std::printf("SHARED_DAMAGE_COMPOSITION %s %u/%u passed\n",argv[3],checks-failures,checks);
    return failures?1:0;
}
