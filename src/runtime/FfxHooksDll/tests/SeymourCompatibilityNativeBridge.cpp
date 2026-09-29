// Isolated test bridge: exports actual core rules, never attaches to FFX.
#include "../hooks/SeymourCompatibilityCore.h"
using namespace FfxHooks::SeymourCompatibility;
extern "C" int SeymourTestFilter(int actor,unsigned command,int nativeResult){
    const Scope s{(std::uint64_t{3}<<32)|7u,11,7};
    return FilterCommand(nativeResult,actor,command,true,s,s);
}
extern "C" unsigned SeymourTestVisibility(unsigned flags,unsigned enable){
    return VisibilityFlags(static_cast<std::uint8_t>(flags),static_cast<std::uint8_t>(enable));
}
