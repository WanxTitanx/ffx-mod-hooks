#pragma once
#include "OmegaLastSealConfig.h"
namespace FfxHooks::OmegaLastSeal {
inline Category DamageCategory(const unsigned char* row) noexcept {
    const unsigned kind=row?row[0x20]&3u:0u;
    return kind==1?Category::Physical:kind==2?Category::Magic:Category::Other;
}
inline bool IsZanmato(unsigned command) noexcept {
    return command==0x30E2||command==0x4085||command==0x410A||command==0x60E2;
}
inline void ZanmatoDamageView(CommandRow& output,const CommandRow& harmlessDamage) noexcept {
    // Reprimand is an admitted, non-elemental HP-only native command with no
    // death/ejection/status payload. Borrow its calculation, not its animation
    // or command ID; the final native HP boundary publishes the bounded impact.
    std::memcpy(output.data()+16,harmlessDamage.data()+16,76);
}
}
