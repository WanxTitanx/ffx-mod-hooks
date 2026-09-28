#include "ArcanaCore.h"
#include "ArcanaCatalog.generated.h"
#include <algorithm>
#include <limits>

namespace FfxHooks::Arcana {
namespace {
bool ValidMode(Mode mode) noexcept {return mode==Mode::Twin||mode==Mode::Constellation;}
unsigned SlotCount(Mode mode) noexcept {return mode==Mode::Twin?2:3;}
Error Capacity(const State& s,unsigned actor) noexcept {
    unsigned weight=0;
    for(auto id:s.slots[actor])if(id!=kEmpty){
        const auto* card=FindCard(static_cast<unsigned>(id));
        if(!card)return Error::InvalidCard;
        weight+=s.mode==Mode::Constellation&&card->major?2:1;
    }
    return weight>(s.mode==Mode::Twin?2u:4u)?Error::Capacity:Error::None;
}
Error Revision(const State& state,std::uint64_t expected) noexcept {
    if(state.revision!=expected)return Error::Stale;
    return state.revision==std::numeric_limits<std::uint64_t>::max()?Error::RevisionExhausted:Error::None;
}
bool Additive(EffectKind kind) noexcept {
    return (kind>=EffectKind::HpPercent&&kind<=EffectKind::LuckFlat)||
        kind==EffectKind::IncomingDamage||kind==EffectKind::OutgoingDamage||
        kind==EffectKind::WhiteHealing||kind==EffectKind::Healing;
}
std::int64_t Scale(std::int64_t value,int percent) noexcept {
    if(value<=0||percent<=0)return 0;
    const auto limit=std::numeric_limits<std::int64_t>::max();
    return value>limit/percent?limit:value*percent/100;
}
}
const Card* FindCard(unsigned id) noexcept {return id<kCardCount?&kCatalog[id]:nullptr;}
Error Validate(const State& s) noexcept {
    if(!ValidMode(s.mode))return Error::InvalidState;
    for(auto owned:s.acquired)if(owned>1)return Error::InvalidState;
    std::array<bool,kCardCount> seen{};
    for(unsigned actor=0;actor<kActorCount;++actor){
        for(unsigned slot=0;slot<kMaximumSlots;++slot){
            const auto id=s.slots[actor][slot];
            if(id==kEmpty)continue;
            if(slot>=SlotCount(s.mode))return Error::InvalidSlot;
            if(id<0||id>=static_cast<int>(kCardCount))return Error::InvalidCard;
            if(!s.acquired[static_cast<unsigned>(id)])return Error::NotAcquired;
            if(seen[static_cast<unsigned>(id)])return Error::InvalidState;
            seen[static_cast<unsigned>(id)]=true;
        }
        if(const auto error=Capacity(s,actor);error!=Error::None)return error;
    }
    return Error::None;
}
Error Equip(State& s,std::uint64_t expected,unsigned actor,unsigned slot,std::int16_t id,bool transfer) noexcept {
    if(const auto error=Validate(s);error!=Error::None)return error;
    if(const auto error=Revision(s,expected);error!=Error::None)return error;
    if(actor>=kActorCount)return Error::InvalidActor;
    if(slot>=SlotCount(s.mode))return Error::InvalidSlot;
    if(id!=kEmpty&&(id<0||id>=static_cast<int>(kCardCount)))return Error::InvalidCard;
    if(id!=kEmpty&&!s.acquired[static_cast<unsigned>(id)])return Error::NotAcquired;
    if(s.slots[actor][slot]==id)return Error::None;
    auto candidate=s;
    if(id!=kEmpty)for(auto& owner:candidate.slots)for(auto& old:owner)if(old==id){
        if(!transfer)return Error::TransferRequired;
        old=kEmpty;
    }
    candidate.slots[actor][slot]=id;
    if(const auto error=Validate(candidate);error!=Error::None)return error;
    ++candidate.revision;s=candidate;return Error::None;
}
Error ChangeMode(State& s,std::uint64_t expected,Mode mode,const Slots* resolved) noexcept {
    if(const auto error=Validate(s);error!=Error::None)return error;
    if(const auto error=Revision(s,expected);error!=Error::None)return error;
    if(!ValidMode(mode))return Error::InvalidState;
    auto candidate=s;candidate.mode=mode;
    if(resolved){
        for(unsigned actor=0;actor<kActorCount;++actor)for(auto id:(*resolved)[actor])
            if(id!=kEmpty&&std::find(s.slots[actor].begin(),s.slots[actor].end(),id)==s.slots[actor].end())
                return Error::InvalidState;
        candidate.slots=*resolved;
    }else if(mode==Mode::Twin)for(const auto& slots:s.slots)if(slots[2]!=kEmpty)return Error::ResolutionRequired;
    if(const auto error=Validate(candidate);error!=Error::None)return error;
    if(candidate.mode==s.mode&&candidate.slots==s.slots)return Error::None;
    ++candidate.revision;s=candidate;return Error::None;
}
Error Award(State& s,unsigned id) noexcept {
    if(const auto error=Validate(s);error!=Error::None)return error;
    if(!FindCard(id))return Error::InvalidCard;
    if(s.acquired[id])return Error::None;
    if(const auto error=Revision(s,s.revision);error!=Error::None)return error;
    s.acquired[id]=1;++s.revision;return Error::None;
}
int Owner(const State& s,unsigned id) noexcept {
    if(!FindCard(id)||Validate(s)!=Error::None)return -1;
    for(unsigned actor=0;actor<kActorCount;++actor)for(auto slot:s.slots[actor])
        if(slot==static_cast<int>(id))return static_cast<int>(actor);
    return -1;
}
Error AwardAll(State& s,std::uint64_t expected) noexcept {
    if(const auto error=Validate(s);error!=Error::None)return error;
    if(const auto error=Revision(s,expected);error!=Error::None)return error;
    if(std::all_of(s.acquired.begin(),s.acquired.end(),[](auto value){return value==1;}))return Error::None;
    s.acquired.fill(1);++s.revision;return Error::None;
}
Effects Aggregate(const State& s,unsigned actor) noexcept {
    Effects result;
    if(actor>=kActorCount||Validate(s)!=Error::None)return result;
    for(auto id:s.slots[actor])if(id!=kEmpty)for(const auto& effect:kCatalog[id].effects){
        if(effect.kind==EffectKind::None)continue;
        auto& value=result.values[static_cast<unsigned>(effect.kind)];
        value=Additive(effect.kind)?value+effect.value:(std::max)(value,int(effect.value));
    }
    return result;
}
std::uint32_t BoostStat(std::uint32_t base,int percent,int flat,std::uint32_t cap) noexcept {
    const auto result=Scale(base,(std::max)(0,100+percent))+flat;
    return static_cast<std::uint32_t>((std::clamp)(result,std::int64_t(0),std::int64_t(cap)));
}
std::int64_t Damage(const DamageContext& d,const Effects& source,const Effects& target) noexcept {
    if(d.amount<=0)return d.amount;
    auto result=d.amount;
    if(d.hp&&d.healing){
        result=Scale(result,100+source.Get(EffectKind::Healing)+(d.whiteMagic?source.Get(EffectKind::WhiteHealing):0));
    }else if(d.hp&&!d.fixed&&!d.fractional){
        int outgoing=source.Get(EffectKind::OutgoingDamage);
        if(d.overdrive)outgoing+=source.Get(EffectKind::OverdriveDamage);
        if(d.elements&3)outgoing+=source.Get(EffectKind::ElementDamageFireIce);
        if(d.elements&12)outgoing+=source.Get(EffectKind::ElementDamageLightningWater);
        if(d.elements&16)outgoing+=source.Get(EffectKind::ElementDamageHoly);
        if(d.deathImmune)outgoing+=source.Get(EffectKind::DeathImmuneDamage);
        result=Scale(result,100+outgoing);
        result=Scale(result,100+target.Get(EffectKind::IncomingDamage));
    }
    return (std::min)(result,std::int64_t(d.cap));
}
std::uint32_t MpCost(std::uint32_t base,const Effects& e,bool black,bool white,bool free,bool one) noexcept {
    if(!base||free)return 0;
    if(one)return 1;
    int reduction=e.Get(EffectKind::MpReduction)+(white?e.Get(EffectKind::WhiteMpReduction):0);
    if(e.Get(EffectKind::HalfMp)||(black&&e.Get(EffectKind::HalfBlackMp))||(white&&e.Get(EffectKind::HalfWhiteMp)))base=base/2+base%2;
    const auto numerator=std::uint64_t(base)*static_cast<unsigned>((std::max)(0,100-reduction));
    return static_cast<std::uint32_t>((std::max)(std::uint64_t(1),(numerator+99)/100));
}
std::uint32_t CtbDelay(std::uint32_t native,const Effects& e,bool first) noexcept {
    if(!native)return 0;
    const auto value=Scale(native,100+e.Get(EffectKind::CtbIncrease)-e.Get(EffectKind::CtbReduction)-(first?e.Get(EffectKind::FirstCtbReduction):0));
    return static_cast<std::uint32_t>((std::clamp)(value,std::int64_t(1),std::int64_t(UINT32_MAX)));
}
unsigned RewardRate(unsigned native,const Effects& e,EffectKind kind) noexcept {
    const int value=e.Get(kind);
    const auto rate=kind==EffectKind::DropMultiplier?static_cast<unsigned>((std::max)(1,value)*100):
        static_cast<unsigned>((std::max)(0,100+value));
    return (std::max)(native,rate);
}
}
