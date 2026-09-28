#pragma once
#include "ElementPackCore.h"
#include <array>
#include <cstdint>
#include <limits>

namespace FfxHooks::ElementalDominion {
inline constexpr unsigned ActorCount=31,MaximumActions=16,MaximumEffects=16;

struct ActorToken {
    std::uint64_t generation=0,incarnation=0;
    unsigned slot=ActorCount;
    bool Valid() const noexcept {return generation&&incarnation&&slot<ActorCount;}
    bool operator==(const ActorToken& other) const noexcept {
        return generation==other.generation&&incarnation==other.incarnation&&slot==other.slot;
    }
};
struct ActionToken {
    std::uint64_t generation=0,sequence=0;
    unsigned slot=MaximumActions;
    bool Valid() const noexcept {return generation&&sequence&&slot<MaximumActions;}
};
struct ActorRules {
    unsigned imperilLimit=4;
    std::array<unsigned,ElementLimit> imperilResistanceBp{};
    std::array<bool,ElementLimit> imperilImmune{},locked{};
    bool Valid() const noexcept {
        if(imperilLimit>4)return false;
        for(auto value:imperilResistanceBp)if(value>10000)return false;
        return true;
    }
};
struct TimedAffinity {
    unsigned imperil=0,ward=0,nul=0;
    unsigned imperilTurns=0,wardTurns=0,nulTurns=0;
    bool valid=false,locked=false,imperilImmune=false;
    unsigned imperilResistanceBp=0;
};
enum class QueueResult { Queued,Miss,Duplicate,UnknownAction,UnknownActor,Invalid };

// Single battle-thread ownership is enforced by the native adapter, not by a
// per-hit mutex. Storage is fixed and process-lived; no native actor bytes or
// save fields are used for these statuses. Runtime places this object outside
// its small native call stack.
class BattleState {
    struct Actor {
        ActorToken token{};
        ActorRules rules{};
        std::array<TimedAffinity,ElementLimit> effects{};
        std::uint64_t completed=0;
        bool active=false;
    };
    struct Pending {
        ActorToken target{};
        std::array<ElementEffect,MaximumEffects> effects{};
        unsigned count=0,roll=0;
        bool queued=false;
    };
    struct Action {
        ActionToken token{};
        ActorToken user{};
        std::array<Actor,ActorCount> snapshots{};
        std::array<Pending,ActorCount> pending{};
        bool active=false,consumesAction=true;
    };
public:
    bool Start() noexcept {
        actors_={};actions_={};sequence_=0;
        if(generation_==(std::numeric_limits<std::uint64_t>::max)()){running_=false;return false;}
        ++generation_;running_=true;return true;
    }
    void Stop() noexcept {running_=false;actors_={};actions_={};}
    std::uint64_t Generation() const noexcept {return running_?generation_:0;}
    ActorToken Current(unsigned slot) const noexcept {
        return running_&&slot<ActorCount&&actors_[slot].active?actors_[slot].token:ActorToken{};
    }
    ActorToken Spawn(unsigned slot,const ActorRules& rules={}) noexcept {
        if(!running_||slot>=ActorCount||!rules.Valid())return {};
        const auto prior=actors_[slot].token.incarnation;
        if(prior==(std::numeric_limits<std::uint64_t>::max)())return {};
        if(actors_[slot].active)(void)Remove(actors_[slot].token);
        auto& actor=actors_[slot];actor={};actor.active=true;actor.rules=rules;
        actor.token={generation_,prior+1,slot};return actor.token;
    }
    bool Remove(ActorToken token) noexcept {
        if(!Matches(token))return false;
        auto& actor=actors_[token.slot];actor.active=false;actor.effects={};
        for(auto& action:actions_)if(action.active&&action.user==token)action.active=false;
        return true;
    }
    bool SetRules(ActorToken token,const ActorRules& rules) noexcept {
        if(!Matches(token)||!rules.Valid())return false;
        actors_[token.slot].rules=rules;return true;
    }
    ActionToken Begin(ActorToken user,bool consumesAction=true) noexcept {
        if(!Matches(user)||sequence_==(std::numeric_limits<std::uint64_t>::max)())return {};
        for(unsigned slot=0;slot<MaximumActions;++slot)if(!actions_[slot].active){
            auto& action=actions_[slot];action={};action.token={generation_,++sequence_,slot};
            action.user=user;action.active=true;action.consumesAction=consumesAction;
            action.snapshots=actors_;return action.token;
        }
        return {};
    }
    QueueResult Queue(ActionToken token,ActorToken target,const ElementEffect* effects,
                      unsigned count,unsigned roll,bool landed) noexcept {
        auto* action=Find(token);if(!action)return QueueResult::UnknownAction;
        if(!Matches(target)||!(action->snapshots[target.slot].token==target)||
           !action->snapshots[target.slot].active)return QueueResult::UnknownActor;
        if(!count||count>MaximumEffects||!effects||roll>=10000)return QueueResult::Invalid;
        for(unsigned i=0;i<count;++i){
            const auto& effect=effects[i];
            if(effect.element>=ElementLimit||!effect.stacks||effect.stacks>4||!effect.turns||effect.turns>255||
               effect.chanceBp>10000||static_cast<unsigned>(effect.kind)>static_cast<unsigned>(EffectKind::Cleanse))return QueueResult::Invalid;
            for(unsigned j=0;j<i;++j)if(effects[j].element==effect.element&&effects[j].kind==effect.kind)return QueueResult::Invalid;
        }
        if(!landed)return QueueResult::Miss;
        auto& pending=action->pending[target.slot];if(pending.queued)return QueueResult::Duplicate;
        pending.target=target;pending.count=count;pending.roll=roll;pending.queued=true;
        for(unsigned i=0;i<count;++i)pending.effects[i]=effects[i];
        return QueueResult::Queued;
    }
    TimedAffinity View(ActorToken token,unsigned element) const noexcept {
        return Matches(token)&&element<ElementLimit?ViewActor(actors_[token.slot],element):TimedAffinity{};
    }
    TimedAffinity Snapshot(ActionToken token,ActorToken target,unsigned element) const noexcept {
        const auto* action=Find(token);
        if(!action||!Matches(target)||element>=ElementLimit)return {};
        const auto& actor=action->snapshots[target.slot];
        return actor.active&&actor.token==target?ViewActor(actor,element):TimedAffinity{};
    }
    bool ConsumeNul(ActorToken token,unsigned element) noexcept {
        if(!Matches(token)||element>=ElementLimit)return false;
        auto& effect=actors_[token.slot].effects[element];if(!effect.nul)return false;
        --effect.nul;if(!effect.nul)effect.nulTurns=0;return true;
    }
    bool Cancel(ActionToken token) noexcept {
        auto* action=Find(token);if(!action)return false;
        action->active=false;return true;
    }
    bool Complete(ActionToken token) noexcept {
        auto* action=Find(token);if(!action||!Matches(action->user))return false;
        auto& user=actors_[action->user.slot];
        if(action->consumesAction&&user.completed==(std::numeric_limits<std::uint64_t>::max)())return false;
        // Expire the old snapshot before granting self-cast effects. A new Ward
        // therefore starts with its full duration, rather than losing one turn.
        if(action->consumesAction){
            ++user.completed;
            for(auto& effect:user.effects){
                Tick(effect.imperil,effect.imperilTurns);Tick(effect.ward,effect.wardTurns);Tick(effect.nul,effect.nulTurns);
            }
        }
        for(const auto& pending:action->pending){
            if(!pending.queued||!Matches(pending.target))continue;
            auto& target=actors_[pending.target.slot];
            const auto& rules=action->snapshots[pending.target.slot].rules;
            for(unsigned i=0;i<pending.count;++i)Apply(target,rules,pending.effects[i],pending.roll);
        }
        action->active=false;return true;
    }
private:
    bool Matches(ActorToken token) const noexcept {
        return running_&&token.Valid()&&token.generation==generation_&&
               actors_[token.slot].active&&actors_[token.slot].token==token;
    }
    Action* Find(ActionToken token) noexcept {
        if(!running_||!token.Valid()||token.generation!=generation_)return nullptr;
        auto& action=actions_[token.slot];
        return action.active&&action.token.sequence==token.sequence?&action:nullptr;
    }
    const Action* Find(ActionToken token) const noexcept {
        if(!running_||!token.Valid()||token.generation!=generation_)return nullptr;
        const auto& action=actions_[token.slot];
        return action.active&&action.token.sequence==token.sequence?&action:nullptr;
    }
    static TimedAffinity ViewActor(const Actor& actor,unsigned element) noexcept {
        auto result=actor.effects[element];result.valid=true;result.locked=actor.rules.locked[element];
        result.imperilImmune=actor.rules.imperilImmune[element];
        result.imperilResistanceBp=actor.rules.imperilResistanceBp[element];return result;
    }
    static void Tick(unsigned& stacks,unsigned& turns) noexcept {
        if(turns&&!--turns)stacks=0;
    }
    static void Grant(unsigned& stacks,unsigned& turns,unsigned add,unsigned duration,unsigned limit) noexcept {
        stacks=(std::min)(limit,stacks+add);if(stacks)turns=duration;
    }
    static void Apply(Actor& actor,const ActorRules& rules,const ElementEffect& effect,unsigned roll) noexcept {
        auto& live=actor.effects[effect.element];
        unsigned chance=effect.chanceBp;
        if(effect.kind==EffectKind::Imperil){
            if(rules.locked[effect.element]||rules.imperilImmune[effect.element]||!rules.imperilLimit)return;
            chance=chance*(10000-rules.imperilResistanceBp[effect.element])/10000;
        }
        if(effect.kind==EffectKind::Ward&&rules.locked[effect.element])return;
        if(roll>=chance)return;
        switch(effect.kind){
        case EffectKind::Imperil:Grant(live.imperil,live.imperilTurns,effect.stacks,effect.turns,rules.imperilLimit);break;
        case EffectKind::Ward:Grant(live.ward,live.wardTurns,effect.stacks,effect.turns,4);break;
        case EffectKind::Nul:Grant(live.nul,live.nulTurns,effect.stacks,effect.turns,4);break;
        case EffectKind::Cleanse:
        case EffectKind::RemoveImperil:live.imperil=live.imperilTurns=0;break;
        case EffectKind::RemoveWard:live.ward=live.wardTurns=0;break;
        case EffectKind::RemoveNul:live.nul=live.nulTurns=0;break;
        }
    }
    std::array<Actor,ActorCount> actors_{};
    std::array<Action,MaximumActions> actions_{};
    std::uint64_t generation_=0,sequence_=0;
    bool running_=false;
};
} // namespace FfxHooks::ElementalDominion
