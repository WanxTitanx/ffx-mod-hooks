#pragma once
// Jarvis-HOOK: progressive elemental exposure, independent of legacy timed
// status stacks. The adapter supplies admitted action/actor/turn identities.
#include <array>
#include <cstdint>
#include <algorithm>
#include <limits>
namespace FfxHooks::SpiraElemental {
inline constexpr unsigned Actors=31,Elements=32,Actions=16,MaxHits=4096;
inline constexpr int MaximumExposure=25000,ImperilStep=2500,AurochsStep=10000;
struct ActorKey {
    std::uint64_t generation=0,incarnation=0;unsigned slot=Actors;
    bool operator==(const ActorKey& b) const noexcept {return generation==b.generation&&incarnation==b.incarnation&&slot==b.slot;}
};
struct ActionKey {std::uint64_t generation=0,sequence=0;unsigned slot=Actions;};
struct Base {
    std::array<int,Elements> exposure{};
    std::array<unsigned,Elements> resistance{};
    std::uint32_t locked=0,immune=0;
};
struct Random {void* context=nullptr;bool(*draw31)(void*,unsigned&) noexcept=nullptr;};
inline bool Raw(const Random& rng,unsigned& out) noexcept {
    return rng.draw31&&rng.draw31(rng.context,out)&&out<=0x7FFFFFFFu;
}
inline bool Chance(unsigned basisPoints,const Random& rng) noexcept {
    if(!basisPoints)return false;if(basisPoints>=10000)return true;
    unsigned raw=0;return Raw(rng,raw)&&std::uint64_t(raw)*10000<std::uint64_t(basisPoints)*0x80000000ull;
}
inline bool Geometric(unsigned ordinal,const Random& rng) noexcept {
    if(ordinal>=MaxHits)return false;
    // Native indexed RNG publishes31 bits. Split long probabilities into
    // bounded independent chunks instead of truncating12.5/6.25/... to integers.
    unsigned bits=ordinal+1;
    while(bits){const unsigned chunk=bits>31?31:bits;unsigned raw=0;
        if(!Raw(rng,raw)||raw>=(1u<<(31-chunk)))return false;
        bits-=chunk;
    }
    return true;
}
struct Hit {bool landed=false,hostile=false,absorbed=false,physical=false;std::uint32_t elements=0;};
struct Change {ActorKey actor{};unsigned element=0;int before=0,after=0;bool ward=false;};
using Notify=void(*)(void*,const Change&) noexcept;
class State {
    struct Actor {ActorKey key{};std::array<int,Elements> delta{};std::uint32_t lastTurn=0;bool active=false;};
    struct Snapshot {ActorKey key{};Base base{};std::array<int,Elements> exposure{};bool active=false;};
    struct Pending {
        std::array<unsigned,Elements> touches{},bonus{};
        std::uint32_t suppressed=0;bool monsterAttempted=false;
    };
    struct Action {
        ActionKey key{};ActorKey source{};unsigned parent=0,ordinal=0;std::uint32_t touches=0;
        std::array<Snapshot,Actors> snapshots{};std::array<Pending,Actors> pending{};bool active=false;
    };
    std::array<Actor,Actors> actors_{};
    std::array<Action,Actions> actions_{};
    std::uint64_t generation_=0,sequence_=0;
    Notify notify_=nullptr;void* notifyContext_=nullptr;
    static int Exposed(int base,int delta) noexcept {
        if(base<0)return base;
        return static_cast<int>((std::max)(std::int64_t(0),(std::min)(std::int64_t(MaximumExposure),std::int64_t(base)+delta)));
    }
    static bool Valid(const Base& base) noexcept {
        for(unsigned e=0;e<Elements;++e)if(base.exposure[e]<-10000||base.exposure[e]>MaximumExposure||base.resistance[e]>10000)return false;
        return true;
    }
    Action* Find(ActionKey key) noexcept {
        if(!generation_||key.generation!=generation_||!key.sequence||key.slot>=Actions)return nullptr;
        auto& a=actions_[key.slot];return a.active&&a.key.sequence==key.sequence?&a:nullptr;
    }
    const Action* Find(ActionKey key) const noexcept {return const_cast<State*>(this)->Find(key);}
    bool Debuff(const Snapshot& target,unsigned element,const Random& rng) noexcept {
        const auto bit=std::uint32_t{1}<<element;
        return target.active&&!(target.base.locked&bit)&&!(target.base.immune&bit)&&target.exposure[element]>=0&&
            Chance(10000-target.base.resistance[element],rng);
    }
    void Publish(ActorKey actor,unsigned element,int base,int before,int after,bool ward) noexcept {
        actors_[actor.slot].delta[element]=after-base;
        if(before!=after&&notify_)notify_(notifyContext_,{actor,element,before,after,ward});
    }
public:
    void SetNotifier(Notify notify,void* context=nullptr) noexcept {notify_=notify;notifyContext_=context;}
    void Start(std::uint64_t generation) noexcept {actors_={};actions_={};generation_=generation;sequence_=0;}
    void Stop() noexcept {Start(0);}
    bool Existing(ActorKey key) const noexcept {
        return generation_&&key.generation==generation_&&key.incarnation&&key.slot<Actors&&actors_[key.slot].active&&actors_[key.slot].key==key;
    }
    bool Spawn(ActorKey key) noexcept {
        if(!generation_||key.generation!=generation_||!key.incarnation||key.slot>=Actors)return false;
        if(Existing(key))return true;
        if(actors_[key.slot].active)Remove(actors_[key.slot].key);
        actors_[key.slot]={};actors_[key.slot].active=true;actors_[key.slot].key=key;return true;
    }
    bool Remove(ActorKey key) noexcept {
        if(!Existing(key))return false;
        actors_[key.slot]={};for(auto& a:actions_)if(a.active&&a.source==key)a.active=false;return true;
    }
    int Live(ActorKey key,unsigned element,int base) const noexcept {
        return Existing(key)&&element<Elements?Exposed(base,actors_[key.slot].delta[element]):base;
    }
    int Delta(ActorKey key,unsigned element) const noexcept {return Existing(key)&&element<Elements?actors_[key.slot].delta[element]:0;}
    ActionKey Begin(ActorKey source,unsigned parent,std::uint32_t touches,const std::array<Base,Actors>& bases) noexcept {
        if(!Existing(source)||sequence_==(std::numeric_limits<std::uint64_t>::max)())return {};
        for(unsigned i=0;i<Actors;++i)if(actors_[i].active&&!Valid(bases[i]))return {};
        for(unsigned i=0;i<Actions;++i)if(!actions_[i].active){auto& a=actions_[i];a={};
            a.key={generation_,++sequence_,i};a.source=source;a.parent=parent;a.touches=touches;a.active=true;
            for(unsigned id=0;id<Actors;++id)if(actors_[id].active){auto& snap=a.snapshots[id];snap.active=true;
                snap.key=actors_[id].key;snap.base=bases[id];
                for(unsigned e=0;e<Elements;++e)snap.exposure[e]=(bases[id].locked&(std::uint32_t{1}<<e))?bases[id].exposure[e]:Live(snap.key,e,bases[id].exposure[e]);
            }
            return a.key;
        }
        return {};
    }
    bool SnapshotExposure(ActionKey action,ActorKey actor,unsigned element,int& out) const noexcept {
        const auto* a=Find(action);if(!a||!Existing(actor)||element>=Elements)return false;
        const auto& snap=a->snapshots[actor.slot];if(!snap.active||!(snap.key==actor))return false;
        out=snap.exposure[element];return true;
    }
    int Exposure(ActionKey action,ActorKey actor,unsigned element) const noexcept {int value=10000;SnapshotExposure(action,actor,element,value);return value;}
    bool Hit(ActionKey key,ActorKey target,const SpiraElemental::Hit& hit,const Random& rng) noexcept {
        auto* a=Find(key);if(!a||!Existing(a->source)||!Existing(target)||!hit.landed||!hit.hostile||hit.absorbed)return false;
        const auto& snap=a->snapshots[target.slot];if(!snap.active||!(snap.key==target))return false;
        auto& pending=a->pending[target.slot];
        if(a->source.slot>=18){
            if(!hit.elements||pending.monsterAttempted)return false;pending.monsterAttempted=true;
            if(!Chance(2500,rng))return true;
            for(unsigned e=0;e<Elements;++e)if((hit.elements&(std::uint32_t{1}<<e))&&Debuff(snap,e,rng))pending.touches[e]=ImperilStep;
            return true;
        }
        // Parent ownership is supplied by the adapter. Shared result commands
        // and their OverdriveCategory are deliberately not classification inputs.
        if(a->source.slot==4&&a->parent==0x3077){
            const auto fresh=hit.elements&~pending.suppressed;pending.suppressed|=hit.elements;
            for(unsigned e=0;e<Elements;++e)if(fresh&(std::uint32_t{1}<<e)){
                pending.touches[e]=0;
                if(Debuff(snap,e,rng))pending.bonus[e]=AurochsStep;
            }
        }
        if(!hit.physical)return true;
        const unsigned ordinal=a->ordinal;if(a->ordinal<MaxHits)++a->ordinal;
        const auto candidates=a->touches&~pending.suppressed;if(!candidates)return true;
        if(!Geometric(ordinal,rng))return true;
        for(unsigned e=0;e<Elements;++e)if((candidates&(std::uint32_t{1}<<e))&&Debuff(snap,e,rng))
            pending.touches[e]=(std::min)(unsigned(MaximumExposure),pending.touches[e]+ImperilStep);
        return true;
    }
    bool Cancel(ActionKey key) noexcept {auto* a=Find(key);if(!a)return false;a->active=false;return true;}
    bool Finish(ActionKey key,const std::array<Base,Actors>& current) noexcept {
        auto* a=Find(key);if(!a||!Existing(a->source))return false;
        for(unsigned id=0;id<Actors;++id){const auto& snap=a->snapshots[id];
            if(!snap.active||!Existing(snap.key)||!Valid(current[id]))continue;
            for(unsigned e=0;e<Elements;++e){const unsigned add=a->pending[id].touches[e]+a->pending[id].bonus[e];
                const int base=current[id].exposure[e];if(!add||base<0||(current[id].locked&(std::uint32_t{1}<<e)))continue;
                const int before=Live(snap.key,e,base),after=(std::min)(MaximumExposure,before+static_cast<int>(add));
                Publish(snap.key,e,base,before,after,false);
            }
        }
        a->active=false;return true;
    }
    bool Turn(ActorKey actor,std::uint32_t edge,std::uint32_t equippedWards,const Base& base) noexcept {
        if(!Existing(actor)||!edge||edge<=actors_[actor.slot].lastTurn||!Valid(base))return false;
        actors_[actor.slot].lastTurn=edge;
        for(unsigned e=0;e<Elements;++e){const int baseline=base.exposure[e];
            if(baseline<0||(base.locked&(std::uint32_t{1}<<e)))continue;
            const int before=Live(actor,e,baseline);
            const bool grows=actor.slot<18?(equippedWards&(std::uint32_t{1}<<e))!=0:
                (baseline>0&&baseline<10000)||(before>0&&before<10000);
            if(!grows||!before)continue;
            const int after=(std::max)(0,before-(actor.slot<18?500:1000));Publish(actor,e,baseline,before,after,true);
        }
        return true;
    }
};
}
