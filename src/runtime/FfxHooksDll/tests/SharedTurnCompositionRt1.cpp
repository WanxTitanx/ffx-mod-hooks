// Jarvis-HOOK: both real subscribers share one private native CTB entry.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/VanguardRuntime.h"
#include "../hooks/PhaseTurnEdgeHook.h"
#include "../hooks/SharedTurnRuntime.h"
#include "../shared/Config.h"
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
namespace V=FfxHooks::Vanguard;namespace T=FfxHooks::SharedTurn;
static unsigned checks=0,failures=0,observations=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
static void Observe(const FfxHooks::PhaseTurnEdgeEvent& event){if(event.actorSlot==8&&event.battleActiveFlag==1)++observations;}
static void W16(unsigned char* p,unsigned n){p[0]=static_cast<unsigned char>(n);p[1]=static_cast<unsigned char>(n>>8);}
static void W32(unsigned char* p,int n){std::memcpy(p,&n,4);}
static int R32(const unsigned char* p){int n=0;std::memcpy(&n,p,4);return n;}
int main(int argc,char** argv){
    if(argc!=3)return 2;const bool first=std::strcmp(argv[2],"phase-first")==0;
    if(!first&&std::strcmp(argv[2],"vanguard-first"))return 2;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    Check(PrivatePeFixture::NormalizeRelocations(image),"shared-turn private image relocates");
    std::array<unsigned char,31*0xF90> actors{};auto* actor=actors.data()+8*0xF90;
    actor[0xC]=8;W16(actor+0xE,8);actor[0xDC8]=1;actor[0x592]=255;actor[0x593]=3;
    W32(actor+0x594,1000);W32(actor+0x5D0,1000);W32(actor+0x598,999);W32(actor+0x5D4,100);
    const auto ap=reinterpret_cast<std::uintptr_t>(actors.data());std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),&ap,4);
    std::vector<unsigned char> kernel(20+148*108+1);W16(kernel.data(),1);W16(kernel.data()+10,147);W16(kernel.data()+12,108);W16(kernel.data()+14,148*108);W32(kernel.data()+16,20);
    for(const auto& a:V::Abilities){W16(kernel.data()+20+108*a.id,static_cast<unsigned>(kernel.size())-(20+148*108));
        for(const char* c=a.label;*c;++c)kernel.push_back(*c==' '?58:*c=='\''?65:*c=='-'?71:static_cast<unsigned char>(*c+15));kernel.push_back(0);}
    const auto kp=reinterpret_cast<std::uintptr_t>(kernel.data());const auto size=static_cast<unsigned short>(kernel.size());
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A944>()),&kp,4);std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A970>()),&size,2);
    auto* gear=reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD30F2C>()+3*22);std::memset(gear,0,22);gear[2]=1;gear[4]=gear[6]=8;gear[5]=1;gear[11]=4;
    for(unsigned i=0;i<4;++i)W16(gear+14+2*i,255);W16(gear+14,0x8090);
    *reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>())=1;
    FfxHooks::Config::ResetForTests();Check(FfxHooks::Config::LoadTextForTests("[vanguard]\nmp_regen=1\n[f8_authority]\nvanguard_mp_regen=1\n","C:\\private-shared-turn.ini"),"only MP Regen is enabled");
    FfxHooks::SetPhaseTurnEdgeCallback(&Observe);
    if(first){Check(FfxHooks::InstallPhaseTurnEdgeHook(base,nullptr).ok,"Phase installs first");Check(V::Start(base,false,nullptr),"Vanguard joins existing turn ownership");}
    else{Check(V::Start(base,false,nullptr),"Vanguard installs first");Check(FfxHooks::InstallPhaseTurnEdgeHook(base,nullptr).ok,"Phase joins existing turn ownership");}
    if(failures)return 1;
    using Edge=void(__cdecl*)(unsigned,void*);const auto edge=reinterpret_cast<Edge>(base+::FfxHooks::ExecutableProfile::Rva<0x3B13D0>());
    edge(8,actor);Check(R32(actor+0x5D4)==119&&observations==1,"one native edge produces one regeneration and one legacy observation");
    edge(8,actor);Check(R32(actor+0x5D4)==138&&observations==2,"a real subsequent turn has its own distinct observation");
    T::sequence.store(UINT32_MAX-1);edge(8,actor);const int finalMp=R32(actor+0x5D4);const auto finalObserved=observations;
    edge(8,actor);edge(8,actor);
    Check(T::sequence.load()==UINT32_MAX&&R32(actor+0x5D4)==finalMp&&observations==finalObserved,"sequence exhaustion is terminal rather than reusing an old turn identity");
    // Private reset only to exercise teardown after the exhaustion assertion.
    T::sequence.store(500);V::RequestStop();edge(8,actor);
    Check(R32(actor+0x5D4)==finalMp&&observations==finalObserved+1&&FfxHooks::IsPhaseTurnEdgeHookInstalled(),"Vanguard stop leaves Phase active without changing MP");
    FfxHooks::RemovePhaseTurnEdgeHook();edge(8,actor);
    Check(observations==finalObserved+1&&!FfxHooks::IsPhaseTurnEdgeHookInstalled(),"both detached consumers leave a retained native-only trampoline");
    std::printf("SHARED_TURN_COMPOSITION %s %u/%u passed\n",argv[2],checks-failures,checks);return failures?1:0;
}
