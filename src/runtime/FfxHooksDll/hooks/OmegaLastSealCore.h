#pragma once
// Jarvis-HOOK: deterministic encounter policy; no engine addresses or save writes.
#include <array>
#include <cstdint>
#include <algorithm>

namespace FfxHooks::OmegaLastSeal {
inline constexpr unsigned Body=100,Light=911,Dark=912,LightEcho=913,DarkEcho=914;
inline constexpr unsigned Attack=0x410F,CoreEnergy=0x4022,Rain=0x4023;
inline constexpr unsigned Rupture=0x61E1,Reprimand=0x61E2,Reconstitution=0x61E3,Concentration=0x61E4,LastSeal=0x61E5,Supernova=0x61E6;
inline constexpr unsigned Protectga=0x61E7,Shellga=0x61E8,Dispelga=0x61E9,HolyAttack=0x61EA,DarkAttack=0x61EB,HolyFury=0x61EC,DarkFury=0x61ED;
inline constexpr unsigned AntiFlare=0x61CB,HolyFlare=0x61DD,Hastega=0x61D8;
enum class Category : unsigned {Physical,Magic,Other};
enum class Kind : unsigned {Normal,Concentrate,Rebuild,FinalEntry,Nova,Reaction};
enum class Notice : unsigned {None,Exposure,RebuildWarning,Preparation,Cancelled,FinalPending,FinalEntry};
struct Admission {bool exactEncounter=false,explicitLaunch=false;unsigned field=0,formation=0,body=0;std::uint64_t generation=0;};
struct Life {std::uint32_t hp=0,maximum=0;unsigned helpers=0,silenced=0;std::uint32_t party=0;
    unsigned missingProtect=0,missingShell=0,missingHaste=0;bool partyBuffs=false;};
struct Plan {Kind kind=Kind::Concentrate;unsigned count=0,revive=0;std::array<unsigned,3> commands{};bool distinct=false;};
struct Action {std::uint64_t token=0;unsigned actor=31,role=5;bool party=false,normal=false,completed=false;Category category=Category::Other;std::uint32_t bodyDamage=0;Plan plan{};};
struct Effect {Notice notice=Notice::None;unsigned revive=0,cleanup=0,counter=31;};
struct State {
    bool active=false,finalPending=false,finalEntered=false,exposed=false,skipOffence=false,preparing=false;
    unsigned phase=0,gauge=0,exposure=0,response=0,helpers=0,streak=0;
    Category lastCategory=Category::Other;
    std::array<unsigned,2> guardActions{};std::array<bool,2> rebuild{};
    std::array<std::uint64_t,31> completed{};
    std::uint64_t generation=0,exposureAction=0,preparationAction=0;
};
inline unsigned Count(unsigned mask) noexcept {unsigned count=0;for(;mask;mask&=mask-1)++count;return count;}
inline bool Begin(State& state,const Admission& admission,const Life& life) noexcept {
    state={};if(!admission.exactEncounter||admission.field!=1077||admission.formation!=10||admission.body!=Body||!admission.generation||!life.hp||life.maximum!=3000000||life.helpers!=3)return false;
    state.active=true;state.generation=admission.generation;state.helpers=life.helpers;return true;
}
inline bool Sealed(const Life& life,Category category) noexcept {
    if(category==Category::Physical)return (life.helpers&5)!=0;
    if(category==Category::Magic)return (life.helpers&10)!=0;
    return life.helpers!=0;
}
inline std::uint32_t Limit(const State& state,const Life& life,unsigned role,Category category,std::uint32_t amount) noexcept {
    if(!state.active||role!=0||!amount)return amount;
    if(Sealed(life,category))return 0;
    if(!state.finalEntered){const auto floor=life.maximum/4;return life.hp>floor?(std::min)(amount,life.hp-floor):0;}
    return amount;
}
inline constexpr std::uint32_t ZanmatoBodyLimit=99999;
inline std::uint32_t ZanmatoImpact(const State& state,const Life& life,unsigned role,Category category,std::uint32_t nativeAmount) noexcept {
    if(!state.active||role>4)return nativeAmount;
    if(role)return nativeAmount;
    return Limit(state,life,role,category,(std::min)(nativeAmount,ZanmatoBodyLimit));
}
inline void Expose(State& s,std::uint64_t action) noexcept {
    s.exposed=true;s.exposure=4;s.exposureAction=action;s.skipOffence=true;s.gauge=0;s.preparing=false;s.response=0;s.streak=0;s.rebuild={};s.guardActions={};
}
inline Effect Observe(State& s,const Life& life,std::uint64_t action) noexcept {
    Effect out;if(!s.active)return out;
    if(!life.hp){s={};out.cleanup=15;return out;}
    const unsigned phase=std::uint64_t(life.hp)*2<=life.maximum?2u:std::uint64_t(life.hp)*4<=std::uint64_t(life.maximum)*3?1u:0u;
    s.phase=s.finalEntered?3:(std::max)(s.phase,phase);
    if(!s.finalEntered&&!s.finalPending&&life.hp<=life.maximum/4){
        s.finalPending=true;s.exposed=false;s.exposure=0;s.preparing=false;s.response=0;s.gauge=0;s.streak=0;s.rebuild={};s.guardActions={};out.notice=Notice::FinalPending;
    }
    const unsigned previous=s.helpers;s.helpers=life.helpers&15;
    for(unsigned i=0;i<2;++i)if(!(life.helpers&(1u<<i))||(life.silenced&(1u<<i))||s.finalEntered){s.rebuild[i]=false;s.guardActions[i]=0;}
    if(previous&&!(life.helpers&15)&&!s.finalPending){Expose(s,action);out.notice=Notice::Exposure;}
    return out;
}
inline Plan Single(Kind kind,unsigned command,unsigned revive=0) noexcept {Plan p;p.kind=kind;p.count=1;p.commands[0]=command;p.revive=revive;return p;}
inline Plan Choose(State& s,const Life& life,unsigned role,unsigned roll) noexcept {
    if(!s.active||role>4||!life.hp)return {};
    if(role==0){
        if(s.finalPending)return Single(Kind::FinalEntry,LastSeal,15);
        if(s.exposed){
            if(!s.exposure){if(!s.finalEntered)return Single(Kind::Rebuild,Reconstitution,3);}
            else if(s.skipOffence)return Single(Kind::Concentrate,Concentration);
        }
        if(s.preparing){
            if(!life.helpers){Expose(s,0);return Single(Kind::Concentrate,Concentration);}
            if(s.response)return Single(Kind::Concentrate,Concentration);
            Plan p;p.kind=Kind::Nova;p.count=(std::min)(3u,Count(life.helpers));p.commands.fill(Supernova);return p;
        }
        if(s.phase==0){constexpr unsigned commands[]={Attack,CoreEnergy,Rain,Rupture};return Single(Kind::Normal,commands[roll%4]);}
        if(roll%6==4&&life.missingHaste)return Single(Kind::Normal,Hastega);
        if(s.phase>=2&&roll%6==5&&life.partyBuffs)return Single(Kind::Normal,Dispelga);
        Plan p;p.kind=Kind::Normal;p.count=2;
        if(roll%2==0&&Count(life.party)>=2){p.commands[0]=p.commands[1]=Attack;p.distinct=true;}
        else {p.commands[0]=CoreEnergy;p.commands[1]=(roll&2)?HolyFlare:AntiFlare;}
        return p;
    }
    const unsigned bit=1u<<(role-1);if(!(life.helpers&bit))return Single(Kind::Concentrate,Concentration);
    const bool light=role==1||role==3,silenced=(life.silenced&bit)!=0;
    if(role<=2&&!s.finalEntered&&s.rebuild[role-1]&&!silenced)return Single(Kind::Rebuild,Reconstitution,role==1?2:1);
    if(silenced)return Single(Kind::Normal,light?HolyAttack:DarkAttack);
    if(role>=3)return Single(Kind::Normal,light?HolyFury:DarkFury);
    if(roll%3==2&&(light?life.missingProtect:life.missingShell))return Single(Kind::Normal,light?Protectga:Shellga);
    return Single(Kind::Normal,roll%2?(light?HolyFlare:AntiFlare):(light?HolyAttack:DarkAttack));
}
inline Effect Complete(State& s,const Life& life,const Action& a) noexcept {
    auto out=Observe(s,life,a.token);
    if(!s.active||!a.completed||!a.normal||!a.token||a.actor>=31||s.completed[a.actor]>=a.token)return out;
    s.completed[a.actor]=a.token;
    if(a.party){
        if(s.finalPending)return out;
        if(s.exposed&&s.exposure&&a.token!=s.exposureAction)--s.exposure;
        if(s.exposed&&!s.exposure&&s.finalEntered){s.exposed=false;s.skipOffence=false;}
        if(s.preparing&&s.response&&a.token!=s.preparationAction)--s.response;
        if(s.phase>=2&&!s.preparing&&a.bodyDamage){
            if(a.category==Category::Other){s.lastCategory=Category::Other;s.streak=0;return out;}
            if(s.lastCategory!=a.category){s.lastCategory=a.category;s.streak=0;}
            if(++s.streak==3){s.streak=0;out.counter=a.actor;}
        }
        return out;
    }
    if(a.role>4)return out;
    if(a.role&&!(life.helpers&(1u<<(a.role-1))))return out;
    switch(a.plan.kind){
        case Kind::FinalEntry:
            if(a.role==0&&s.finalPending){s.finalPending=false;s.finalEntered=true;s.phase=3;s.exposed=false;s.exposure=0;s.skipOffence=false;s.preparing=false;s.response=0;s.gauge=50;s.streak=0;s.rebuild={};s.guardActions={};out.revive=15;out.notice=Notice::FinalEntry;}
            return out;
        case Kind::Rebuild:
            if(s.finalEntered||s.finalPending)return out;
            if(a.role==0){s.exposed=false;s.exposure=0;s.skipOffence=false;s.streak=0;out.revive=3;}
            else if(a.role<=2&&(life.helpers&(1u<<(a.role-1)))&&!(life.silenced&(1u<<(a.role-1)))){out.revive=a.plan.revive;s.rebuild[a.role-1]=false;s.guardActions[a.role-1]=0;}
            return out;
        case Kind::Concentrate:if(a.role==0)s.skipOffence=false;return out;
        case Kind::Nova:s.gauge=0;s.preparing=false;s.response=0;return out;
        case Kind::Reaction:return out;
        case Kind::Normal:break;
    }
    if(s.finalPending)return out;
    if(a.role>=1&&a.role<=2&&!s.finalEntered&&Count(life.helpers)==1&&!(life.silenced&(1u<<(a.role-1)))){
        auto& turns=s.guardActions[a.role-1];if(turns<2&&++turns==2){s.rebuild[a.role-1]=true;out.notice=Notice::RebuildWarning;}
    }
    if(!s.exposed&&!s.preparing){
        s.gauge=(std::min)(100u,s.gauge+(a.role?10u:20u));
        if(s.gauge==100){
            if(!life.helpers){Expose(s,a.token);out.notice=Notice::Exposure;}
            else {s.preparing=true;s.response=s.finalEntered?2:3;s.preparationAction=a.token;s.streak=0;out.notice=Notice::Preparation;}
        }
    }
    return out;
}
} // namespace FfxHooks::OmegaLastSeal
