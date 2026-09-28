// Jarvis-HOOK: action completion, identity reuse and isolated elemental timers.
#include <cstdio>
#include <cstdint>
#include <initializer_list>

#if __has_include("../hooks/ElementBattleState.h")
#include "../hooks/ElementBattleState.h"
namespace {
namespace E=FfxHooks::ElementalDominion;
unsigned checks=0,failures=0;
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
E::ElementEffect Effect(E::EffectKind kind,unsigned element=8){
    E::ElementEffect effect{};effect.kind=kind;effect.element=element;return effect;
}
bool Cast(E::BattleState& state,E::ActorToken user,E::ActorToken target,const E::ElementEffect& effect,unsigned roll=0){
    const auto action=state.Begin(user);
    return state.Queue(action,target,&effect,1,roll,true)==E::QueueResult::Queued&&state.Complete(action);
}
}
int main(){
    static E::BattleState state;
    Check(!state.Current(0).Valid(),"no actor can exist before a battle begins");
    Check(state.Start(),"new battle creates a nonzero generation");
    const auto user=state.Spawn(0),target=state.Spawn(18),twin=state.Spawn(19);
    Check(user.Valid()&&target.Valid()&&twin.Valid(),"player and identical monster instances have distinct tokens");
    Check(!state.Spawn(E::ActorCount).Valid(),"out-of-range actors never alias a real slot");
    auto imperil=Effect(E::EffectKind::Imperil);
    auto action=state.Begin(user);
    Check(state.Queue(action,target,&imperil,1,0,true)==E::QueueResult::Queued,"landed action queues one target attempt");
    Check(state.View(target,8).imperil==0&&state.Snapshot(action,target,8).imperil==0,"application waits for the causing action to complete");
    Check(state.Queue(action,target,&imperil,1,0,true)==E::QueueResult::Duplicate,"multiple hits never queue a second target attempt");
    Check(state.Complete(action)&&state.View(target,8).imperil==1&&state.View(target,8).imperilTurns==3,"completion applies one stack for three target actions");
    Check(!state.Complete(action)&&state.View(target,8).imperil==1,"replayed completion is inert");
    Check(state.View(twin,8).imperil==0,"same-template neighbor cannot inherit a stack");
    for(unsigned i=0;i<4;++i)Check(Cast(state,user,target,imperil),"reapplication is admitted up to the explicit stack bound");
    Check(state.View(target,8).imperil==4&&state.View(target,8).imperilTurns==3,"capped reapplication refreshes duration without overflow");
    const auto outer=state.Begin(target);
    Check(Cast(state,user,target,Effect(E::EffectKind::Ward)),"nested action may affect its own target snapshot");
    Check(state.Snapshot(outer,target,8).ward==0&&state.View(target,8).ward==1,"nested changes cannot retroactively alter an outer action snapshot");
    Check(state.Cancel(outer)&&state.View(target,8).imperilTurns==3,"cancelled actions do not expire statuses");
    const auto counter=state.Begin(target,false);
    Check(state.Complete(counter)&&state.View(target,8).imperilTurns==3,"a non-turn-consuming counter does not spend a native action duration");
    for(unsigned remaining:{2u,1u,0u}){
        const auto turn=state.Begin(target);
        Check(state.Complete(turn)&&state.View(target,8).imperilTurns==remaining,"only completed target turns decrement duration once");
        Check(state.View(target,8).imperil==(remaining?4u:0u),"stack and timer expire together");
    }
    E::ActorRules bossRules{};bossRules.imperilLimit=2;
    const auto boss=state.Spawn(20,bossRules);
    for(unsigned i=0;i<5;++i)Check(Cast(state,user,boss,imperil),"explicit boss profile accepts bounded applications");
    Check(state.View(boss,8).imperil==2,"boss cap does not use a name or HP heuristic");
    bossRules.imperilImmune[8]=true;bossRules.locked[9]=true;
    Check(state.SetRules(boss,bossRules),"profile update preserves actor identity");
    auto gravityImperil=imperil;gravityImperil.element=9;
    Check(Cast(state,user,boss,gravityImperil)&&state.View(boss,9).imperil==0,"an affinity lock prevents a hidden Imperil stack");
    const auto tick=state.Begin(boss);state.Complete(tick);
    Check(Cast(state,user,boss,imperil)&&state.View(boss,8).imperilTurns==2,"explicit immunity does not refresh an existing timer");
    E::ActorRules resistant{};resistant.imperilResistanceBp[8]=5000;
    const auto ally=state.Spawn(2,resistant);
    Check(Cast(state,user,ally,imperil,5000)&&state.View(ally,8).imperil==0,"resistance is separate from elemental exposure");
    Check(Cast(state,user,ally,imperil,4999)&&state.View(ally,8).imperil==1,"chance boundary is exact in integer basis points");
    action=state.Begin(user);
    Check(state.Queue(action,twin,&imperil,1,0,false)==E::QueueResult::Miss,"miss does not consume a status application attempt");
    Check(state.Queue(action,twin,&imperil,1,0,true)==E::QueueResult::Queued&&state.Complete(action),"a later landed hit can queue the sole attempt");
    Check(Cast(state,user,twin,Effect(E::EffectKind::Cleanse))&&state.View(twin,8).imperil==0,"Cleanse removes the bound Imperil, not other elements");
    Check(Cast(state,ally,ally,Effect(E::EffectKind::Ward))&&state.View(ally,8).wardTurns==3,"self Ward gets its full duration after the casting turn retires");
    Check(Cast(state,user,ally,Effect(E::EffectKind::RemoveWard))&&state.View(ally,8).ward==0,"Ward has its own explicit removal operation");
    Check(Cast(state,user,target,Effect(E::EffectKind::Nul))&&state.View(target,8).nul==1,"external Nul charges are not native Haste or Slow bytes");
    Check(state.ConsumeNul(target,8)&&state.View(target,8).nul==0&&!state.ConsumeNul(target,8),"one charge cannot be consumed twice");
    action=state.Begin(user);state.Queue(action,target,&imperil,1,0,true);
    Check(state.Remove(target)&&!state.View(target,8).valid,"KO or removal invalidates the old instance token");
    const auto replacement=state.Spawn(18);
    Check(replacement.incarnation!=target.incarnation&&state.Complete(action)&&state.View(replacement,8).imperil==0,"a reused actor slot rejects queued effects from the retired instance");
    const auto oldAction=state.Begin(user);state.Queue(oldAction,replacement,&imperil,1,0,true);
    Check(state.Start()&&!state.Complete(oldAction)&&!state.Current(0).Valid(),"new battle or save reset retires pending actions and all actors");
    const auto refreshed=state.Spawn(0);
    Check(refreshed.generation!=user.generation&&!state.Begin(user).Valid(),"old actor tokens cannot enter another battle");
    E::ActionToken actions[E::MaximumActions+1]{};
    for(unsigned i=0;i<E::MaximumActions;++i){actions[i]=state.Begin(refreshed);Check(actions[i].Valid(),"bounded concurrent action enters");}
    Check(!state.Begin(refreshed).Valid(),"action exhaustion rejects admission without allocating");
    for(unsigned i=0;i<E::MaximumActions;++i)Check(state.Cancel(actions[i]),"each bounded action retires independently");
    Check(state.Begin(refreshed).Valid(),"retired capacity becomes available with a fresh sequence");
    E::ActorRules invalidRules{};invalidRules.imperilResistanceBp[0]=10001;
    Check(!state.SetRules(refreshed,invalidRules),"invalid rules cannot partially update a live actor");
    auto invalidEffect=imperil;invalidEffect.turns=0;
    const auto malformed=state.Begin(refreshed);
    Check(state.Queue(malformed,refreshed,&invalidEffect,1,0,true)==E::QueueResult::Invalid,"invalid effect never enters the deferred action");
    Check(state.Queue(malformed,refreshed,&imperil,1,10000,true)==E::QueueResult::Invalid,"out-of-range random roll cannot imply success");
    Check(state.Complete(malformed)&&state.View(refreshed,8).imperil==0,"invalid queue leaves no latent effect");
    std::printf("ELEMENT_BATTLE_STATE_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production ElementBattleState.h is missing");return 1;}
#endif
