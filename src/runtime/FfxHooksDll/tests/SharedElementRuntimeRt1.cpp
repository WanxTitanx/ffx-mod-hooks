// Jarvis-HOOK: one native affinity owner, independent of consumer startup order.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdio>
#include <cstring>
#if __has_include("../hooks/SharedElementRuntime.h")
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/SharedElementRuntime.h"
namespace {
namespace S=FfxHooks::SharedElement;
unsigned checks=0,failures=0,legacyCalls=0,extendedCalls=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
int __cdecl Legacy(const unsigned char* target,const unsigned char* command,unsigned mask,int amount){
    ++legacyCalls;return reinterpret_cast<S::NativeFn>(S::Original())(target,command,mask,amount)+7;
}
bool Extended(const unsigned char*,const unsigned char*,unsigned mask,int amount,int* result) noexcept {
    ++extendedCalls;if(mask!=0x101)return false;*result=amount/4;return true;
}
const S::Resolver extended{Extended};
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(image),"exact private PE relocations");
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    auto original=reinterpret_cast<S::NativeFn>(base+0x38A420);
    unsigned char target[0xF90]{},command[96]{};target[0x5DD]=1;
    Check(original(target,command,1,1000)==1500,"native affinity baseline");
    const bool legacyFirst=argv[2][0]=='1';
    Check(S::Start(base),"shared affinity entry installs once with a verified signature");
    if(legacyFirst)Check(S::RegisterLegacy(Legacy),"legacy consumer registers first");
    Check(S::RegisterResolver(&extended),"external resolver registers without replacing the original");
    if(!legacyFirst)Check(S::RegisterLegacy(Legacy),"legacy consumer registers second");
    Check(S::Start(base),"same-image startup is idempotent");
    Check(original(target,command,0x101,1000)==250&&legacyCalls==0&&extendedCalls==1,"extended policy replaces affinity exactly once");
    Check(original(target,command,1,1000)==1507&&legacyCalls==1,"unhandled requests preserve native and selected legacy behavior");
    Check(!S::RegisterResolver(nullptr),"null resolver is rejected");
    S::UnregisterResolver(&extended);
    Check(original(target,command,1,1000)==1507&&extendedCalls==2,"stopped extension leaves the other consumer intact");
    S::UnregisterLegacy(Legacy);
    Check(original(target,command,1,1000)==1500,"last unsubscribe restores native behavior");
    unsigned char bytes[16]{};std::memcpy(bytes,reinterpret_cast<void*>(base+0x38A420),sizeof(bytes));
    Check(S::MatchesOwned(base,bytes,sizeof(bytes))&&!S::MatchesOwned(base+1,bytes,sizeof(bytes)),"ownership proof is bound to image and bytes");
    bytes[0]^=1;Check(!S::MatchesOwned(base,bytes,sizeof(bytes)),"changed entry cannot impersonate ownership");
    std::printf("SHARED_ELEMENT_RUNTIME_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production SharedElementRuntime.h is missing");return 1;}
#endif
