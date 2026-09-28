#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace FfxHooks::BattleDamage {

enum class Component : std::uint8_t { Hp, Mp, Ctb, Other };
inline constexpr Component FromNativePass(unsigned remaining) noexcept {
    return remaining==3?Component::Hp:remaining==2?Component::Mp:
           remaining==1?Component::Ctb:Component::Other;
}

// The native selector is authoritative. This equivalent is for authoring and
// isolated verification; a live consumer passes the already selected EBX cap.
inline constexpr std::int32_t NativeUpper(unsigned commandFlags,bool equipmentBdl) noexcept {
    return (commandFlags&0x80u)!=0?99999:
           (commandFlags&0x40u)!=0?9999:equipmentBdl?99999:9999;
}

struct Policy {
    Component component=Component::Hp;
    std::int32_t nativeUpper=9999;
    bool magicEnabled=false;
    bool offensiveSpell=false;
    // The owner/receipt/profile authority resolves this grant before the clamp.
    bool aeonBreakAuthorized=false;
    bool legacyNovaBypass=false;
    bool nonlethal=false;
    std::int32_t targetCurrentHp=0;
};
struct Result {
    std::int32_t damage=0;
    std::int32_t upper=9999;
    bool valid=true;
};

// Runs at the existing upper-clamp site, before native writeback. The lower
// clamp remains in the original graph and must not be rerun with this ceiling.
inline Result ClampUpper(std::int32_t damage,const Policy& policy) noexcept {
    if(policy.nativeUpper<0 ||
       (policy.component==Component::Hp&&policy.nonlethal&&policy.targetCurrentHp<0))
        return {damage,policy.nativeUpper,false};
    std::int32_t upper=policy.nativeUpper;
    if(policy.component==Component::Hp){
        const bool finite=(policy.magicEnabled&&policy.offensiveSpell)||policy.aeonBreakAuthorized;
        if(finite){
            // A suppressed/missing BDL stays suppressed, including for Nova.
            // Unknown native selectors are retained rather than expanded.
            if(policy.nativeUpper==99999)upper=999999;
        }else if(policy.legacyNovaBypass){
            upper=(std::numeric_limits<std::int32_t>::max)();
        }
        if(policy.nonlethal){
            const std::int32_t aliveBound=policy.targetCurrentHp>0?policy.targetCurrentHp-1:0;
            upper=(std::min)(upper,aliveBound);
        }
    }
    return {damage>upper?upper:damage,upper,true};
}

enum class GravityMode : std::uint8_t { Native, ReplaceBase, PreserveImmunity, Invalid };
struct GravityInput {
    bool enabled=false;
    bool profileMatched=false;
    bool nativeImmune=false;
    bool overrideNativeImmunity=false;
    std::int32_t currentHp=0;
    std::int32_t maximumHp=0;
};
struct GravityResult {
    GravityMode mode=GravityMode::Native;
    std::int32_t base=0;
};
inline GravityResult Gravity(const GravityInput& input) noexcept {
    if(!input.enabled||!input.profileMatched)return {};
    if(input.currentHp<0||input.maximumHp<0)return {GravityMode::Invalid,0};
    if(input.nativeImmune&&!input.overrideNativeImmunity)
        return {GravityMode::PreserveImmunity,0};
    return {GravityMode::ReplaceBase,input.maximumHp/16};
}

} // namespace FfxHooks::BattleDamage
