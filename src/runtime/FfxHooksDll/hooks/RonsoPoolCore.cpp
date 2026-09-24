#include "RonsoPoolCore.h"
namespace FfxHooks::RonsoPool {
namespace {
bool Owned(const Facts& f) noexcept {
    return f.enabled && !f.stopping && f.ownedPartyActor && f.characterId==kCharacter;
}
uint16_t PlayerCommand(uint16_t id) noexcept {
    const uint16_t group=id&0xF000u;
    return group==0 || group==0x3000u ? id&0x0FFFu : UINT16_MAX;
}
}
Decision Evaluate(const Facts& f) noexcept {
    if(!Owned(f))return {};
    const uint16_t id=PlayerCommand(f.commandId);
    if(id<kFirstCommand || id>kLastCommand || f.commandCost==0 || f.commandCost==255)
        return {Availability::Native,true};
    return {f.learned && f.nativeRestrictionsAllow && f.charge<=kCapacity &&
            f.charge>=f.commandCost ? Availability::Allow : Availability::Block,true};
}
Availability EvaluateHeader(const Facts& header,const std::array<Facts,12>& children) noexcept {
    if(!Owned(header) || PlayerCommand(header.commandId)!=kHeaderCommand)return Availability::Native;
    if(!header.learned || !header.nativeRestrictionsAllow)return Availability::Block;
    for(size_t i=0;i<children.size();++i) {
        const auto& child=children[i];
        if(PlayerCommand(child.commandId)!=kFirstCommand+i || child.charge!=header.charge)continue;
        if(Evaluate(child).availability==Availability::Allow)return Availability::Allow;
    }
    return Availability::Block;
}
bool CanRestore(const Owner& owner,uint64_t generation,uint32_t actorToken,uint8_t observedMax) noexcept {
    return owner.active && !owner.conflict && owner.generation!=0 && owner.actorToken!=0 &&
        owner.generation==generation && owner.actorToken==actorToken && observedMax==kCapacity &&
        owner.originalMax>0 && owner.originalMax<=kCapacity;
}
}
