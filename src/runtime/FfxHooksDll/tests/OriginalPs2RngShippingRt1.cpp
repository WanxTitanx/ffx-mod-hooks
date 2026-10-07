#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "../hooks/OriginalPs2RngRuntime.h"
#include "../hooks/F8FlagCatalog.h"
namespace FfxHooks {
bool PublishF8RuntimeStatus(const char*,F8RuntimeAvailability,bool,bool){return true;}
}
int main(int argc,char** argv){
    namespace R=FfxHooks::OriginalPs2Rng::Runtime;
    // No testing define, input override, executable mapping or game process.
    const bool enabled=argc>1&&std::strcmp(argv[1],"enabled")==0;
    const bool started=R::Start(0,enabled,false);
    const auto status=R::Status();
    const bool safe=!started&&status.initializations==0&&
        (enabled?(status.code==R::Code::Unavailable&&status.reason==R::Reason::UnsupportedProfile):status.code==R::Code::Disabled);
    std::printf("ORIGINAL_PS2_RNG_SHIPPING_GATE_RT1 %s %s\n",enabled?"unsupported":"off",safe?"PASS":"FAIL");
    return safe?0:1;
}
