#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace FfxHooks::SpiraAbilities {
// Jarvis-HOOK: permissions and native categories are caller inputs. These
// arithmetic rules never identify a character, equip a word or grant a receipt.
inline int BargainDamage(int amount,bool positiveHpOutcome,bool source,bool target) noexcept {
    if(!positiveHpOutcome||amount<=0)return amount;
    std::int64_t result=amount;
    if(source)result=result*3/2;
    if(target)result=result*3/2;
    return static_cast<int>((std::min)(result,std::int64_t((std::numeric_limits<int>::max)())));
}
inline int HpCeiling(int native,unsigned owner,bool paidAeon,bool warden) noexcept {
    if(native!=9999&&native!=99999)return native;
    if(paidAeon&&owner>=8&&owner<18)return 999999;
    if(warden&&owner==2&&native==99999)return 999999;
    return native;
}
inline int MpCeiling(int native,unsigned owner,bool paidAeon) noexcept {
    return (native==999||native==9999)&&paidAeon&&owner>=8&&owner<18?9999:native;
}
inline int ManaSpring(int current,int maximum) noexcept {
    if(current<0||maximum<=0||current>=maximum)return current;
    return static_cast<int>((std::min)(std::int64_t(maximum),std::int64_t(current)+5));
}
inline unsigned DropMaximum(unsigned previous,bool twice,bool thrice) noexcept {
    return (std::max)(previous,thrice?3u:twice?2u:1u);
}
} // namespace FfxHooks::SpiraAbilities
