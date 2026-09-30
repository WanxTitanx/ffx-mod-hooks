#include <cstdio>
#if __has_include("../hooks/FahrenheitServices.h")
#include "../hooks/FahrenheitServices.h"
using namespace FfxHooks::Coexistence;
int main(){
    int checks=0,failed=0;const auto check=[&](bool ok,const char* label){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",label);}};
    State cold;check(!cold.ConfigureServices(2,12),"an absent peer cannot claim services");
    State peer;peer.Observe(true);
    check(!peer.ConfigureServices(1,12)&&!peer.ConfigureServices(2,3)&&!peer.ConfigureServices(2,16),"wrong protocol and undeclared service bits are rejected");
    check(!peer.SaveServicesAllowed()&&!peer.FileServicesAllowed(),"V1 detection alone does not enable persistence or resources");
    check(peer.ConfigureServices(2,12)&&peer.ConfigureServices(2,12),"one negotiated provider is idempotent");
    check(!peer.ConfigureServices(2,4),"provider capability changes require a process restart");
    check(peer.SaveServicesAllowed()&&peer.FileServicesAllowed(),"explicit V2 services admit their consumers");
    check(!peer.NativeSaveIoAllowed()&&!peer.NativeRenderAllowed(),"managed services never reopen native IAT I/O or Present ownership");
    for(const char* key:{"labs.equipment_workshop","arcana.enabled","labs.grid_teach","elemental.nul_spells","vanguard.enabled","seymour.enabled","sphere_grid.enabled","language.text"})
        check(FeatureBlock(peer,key)==Block::None,"negotiated transport, not a blanket block, controls supported consumers");
    check(!peer.Ready(1,15)&&peer.Ready(1,3)&&peer.TryStart()&&peer.Finish(true),"existing V1 frame handshake remains distinct and compatible");
    check(!peer.ConfigureServices(2,12),"late service attachment cannot alter a running process");
    peer.Stop();check(!peer.SaveServicesAllowed()&&!peer.FileServicesAllowed(),"terminal stop revokes service admission");
    check(FeatureBlock(peer,"arcana.enabled")==Block::ManagedSaveProtocol,"stopped services cannot be enabled through F8");
    State onlySave;onlySave.Observe(true);check(onlySave.ConfigureServices(2,4)&&onlySave.SaveServicesAllowed()&&!onlySave.FileServicesAllowed(),"save and resource capabilities are independently negotiated");
    std::printf("Fahrenheit services RT0: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
#else
int main(){std::puts("FAIL: explicit cooperative service negotiation is missing");return 1;}
#endif
