#include <cstdio>
#if __has_include("../hooks/FahrenheitInputPolicy.h")
#include "../hooks/FahrenheitInputPolicy.h"
using namespace FfxHooks::Coexistence;
int main(){
    int checks=0,failed=0;
    const auto check=[&](bool ok,const char* label){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",label);}};
    State native;native.Observe(false);
    check(NativeInputAllowed(native,0)&&NativeInputAllowed(native,3),"standalone input ignores absent peer capture");
    State peer;peer.Observe(true);
    check(!NativeInputAllowed(peer,0),"waiting peer cannot open native menus");
    check(peer.Ready(1,3)&&peer.TryStart()&&peer.Finish(true),"fixture activates the real bridge state machine");
    check(NativeInputAllowed(peer,0),"active bridge without capture permits input");
    for(unsigned capture:{1u,2u,3u,4u})check(!NativeInputAllowed(peer,capture),"keyboard/mouse or unknown peer capture blocks all native menu input");
    check(NativeInputAllowed(peer,0),"capture release reopens input admission without changing startup");
    peer.Stop();check(!NativeInputAllowed(peer,0),"terminal peer closes input even when its last capture bits are zero");
    std::printf("Fahrenheit input RT0: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
#else
int main(){std::puts("FAIL: shared native menu input policy is missing");return 1;}
#endif
