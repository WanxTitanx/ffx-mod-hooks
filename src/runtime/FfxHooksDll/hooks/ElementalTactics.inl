// Jarvis-HOOK: one battle-thread ledger owns external status instances. Native
// queue/result entries, not frame counts or UI callbacks, commit and age them.
BattleState statusState;
struct StatusActor {
    ActorToken token{};std::uintptr_t address=0;
    std::uint32_t file=0;std::uint16_t identity=0;
};
std::array<StatusActor,ActorCount> statusActors{};
struct NativeAction {
    ActionToken token{};ActorToken actor{};unsigned queueIndex=0;
    std::array<Byte,72> row{};
    std::array<unsigned,4> bindings{UINT_MAX,UINT_MAX,UINT_MAX,UINT_MAX};
    std::array<bool,ActorCount> attempted{};
};
std::array<NativeAction,ActorCount> nativeActions{};
bool SyncingStatusActors=false;

void SyncStatusActors() noexcept {
    if(!options.tactics||!statusState.Generation()||SyncingStatusActors)return;
    SyncingStatusActors=true;std::uint32_t pointer=0;
    if(!Read(module+0xD334CC,pointer)||pointer<0x10000||pointer>UINT32_MAX-ActorCount*0xF90u){
        SyncingStatusActors=false;return;
    }
    for(unsigned slot=0;slot<ActorCount;++slot){
        const auto address=std::uintptr_t(pointer)+slot*0xF90u;
        std::uint16_t index=0xFFFF,identity=0xFFFF,status=1;std::uint32_t file=0;int hp=0;
        Byte exists=0,removed[2]{};
        const bool valid=!SharedActor::Busy(slot)&&Read(address+0xC,index)&&index==slot&&Read(address+0xE,identity)&&identity!=0xFFFF&&
            Read(address+0x48,file)&&Read(address+0x5D0,hp)&&hp>0&&Read(address+0x606,status)&&!(status&1)&&
            Read(address+0xDC8,exists)&&exists&&Copy(removed,reinterpret_cast<const void*>(address+0xDCD),2)&&!removed[0]&&!removed[1];
        auto& current=statusActors[slot];
        if(!valid){
            if(current.token.Valid())(void)statusState.Remove(current.token);
            current={};continue;
        }
        ActorRules rules{};
        bool rejected=false;
        if(const auto* profile=ProfileForActor(slot,reinterpret_cast<const void*>(address),&rejected)){
            rules.imperilLimit=profile->imperilLimit;rules.imperilResistanceBp.fill(profile->imperilResistBp);
            for(const auto& affinity:profile->affinities){
                rules.locked[affinity.element]=affinity.locked;
                rules.imperilImmune[affinity.element]=affinity.imperilImmune;
                if(affinity.imperilResistBp<=10000)rules.imperilResistanceBp[affinity.element]=affinity.imperilResistBp;
            }
        }
        if(rejected){if(current.token.Valid())(void)statusState.Remove(current.token);current={};continue;}
        if(current.token.Valid()&&current.address==address&&current.identity==identity&&current.file==file&&
           statusState.Current(slot)==current.token){(void)statusState.SetRules(current.token,rules);continue;}
        current={statusState.Spawn(slot,rules),address,file,identity};
    }
    SyncingStatusActors=false;
}
bool ResetStatusState() noexcept {
    statusActors={};nativeActions={};
    ClearNulReservations();
    if(!statusState.Start())return false;
    SyncStatusActors();return true;
}
TimedAffinity Timed(unsigned target,unsigned element,const ActionToken* action,bool live) noexcept {
    if(!options.tactics||target>=ActorCount||element>=pack.registry.Size())return {};
    const auto actor=statusActors[target].token;
    return action?statusState.Snapshot(*action,actor,element):live?statusState.View(actor,element):TimedAffinity{};
}
bool QueueRow(unsigned owner,std::array<Byte,72>& row,unsigned& index,unsigned& count) noexcept {
    if(owner>=ActorCount||!statusActors[owner].token.Valid())return false;
    std::int8_t length=0;Byte slot=255;
    if(!Read(module+0xD2BDE1,length)||length<1||length>62||
       !Read(statusActors[owner].address+0xDE5,slot)||slot>=static_cast<unsigned>(length)||
       !Copy(row.data(),reinterpret_cast<const void*>(module+0xD2AC70+72u*slot),row.size())||
       row[0]!=owner||!row[3]||row[3]>4||row[2]>row[3])return false;
    index=slot;count=static_cast<unsigned>(length);return true;
}
bool SameAction(const NativeAction& action,const std::array<Byte,72>& row,unsigned index,ActorToken actor) noexcept {
    if(!action.token.Valid()||!(action.actor==actor)||index!=action.queueIndex||row[1]!=action.row[1]||row[3]!=action.row[3])return false;
    for(unsigned sub=0;sub<row[3];++sub)if(std::memcmp(row.data()+8+16*sub,action.row.data()+8+16*sub,4))return false;
    return statusState.Snapshot(action.token,actor,0).valid;
}
NativeAction* TrackAction(unsigned owner) noexcept {
    if(!CurrentData()||!options.tactics||owner>=ActorCount)return nullptr;
    SyncStatusActors();std::array<Byte,72> row{};unsigned index=0,count=0;
    if(!QueueRow(owner,row,index,count))return nullptr;
    auto& action=nativeActions[owner];
    if(!SameAction(action,row,index,statusActors[owner].token)){
        ReleaseNulReservations(action.token);
        if(action.token.Valid())(void)statusState.Cancel(action.token);
        action={};action.actor=statusActors[owner].token;action.queueIndex=index;action.row=row;
        action.token=statusState.Begin(action.actor,row[1]==0);
    }
    return action.token.Valid()?&action:nullptr;
}
ActionToken TrackHit(const Bus::DamageCall& call,const CommandBinding& binding) noexcept {
    auto* action=TrackAction(call.user);if(!action||call.target>=ActorCount)return {};
    std::array<Byte,72> row{};unsigned index=0,count=0;
    if(!QueueRow(call.user,row,index,count)||row[2]>=row[3])return {};
    const unsigned sub=row[2];
    const unsigned first=row[8+16*sub]|(unsigned(row[9+16*sub])<<8);
    const unsigned second=row[10+16*sub]|(unsigned(row[11+16*sub])<<8);
    if(call.commandId!=first&&call.commandId!=second)return {};
    const auto bindingIndex=static_cast<std::size_t>(&binding-pack.commands.data());
    if(bindingIndex>=pack.commands.size())return {};
    action->bindings[sub]=static_cast<unsigned>(bindingIndex);return action->token;
}
#include "ElementalNul.inl"
struct ResultStatusContext {
    std::uint64_t generation=0;
    ActionToken action{};ActorToken target{};
    unsigned binding=UINT_MAX;
    std::array<std::array<Byte,4>,2> before{};
};
struct FinishStatusContext {
    std::uint64_t generation=0;
    ActionToken action{};ActorToken actor{};
    unsigned count=0;bool complete=false;
};
thread_local std::array<ResultStatusContext,SharedAction::MaximumDepth> resultContexts{};
thread_local std::array<FinishStatusContext,SharedAction::MaximumDepth> finishContexts{};
void* BeforeStatusResult(const SharedAction::ResultCall& call) noexcept {
    if(!CurrentData()||call.source>=ActorCount||call.target>=ActorCount||call.sub>=4||
       !SharedAction::resultDepth||SharedAction::resultDepth>resultContexts.size())return nullptr;
    auto* action=TrackAction(call.source);if(!action||action->bindings[call.sub]>=pack.commands.size())return nullptr;
    auto& context=resultContexts[SharedAction::resultDepth-1];context={};
    context.generation=generation.load();context.action=action->token;context.target=statusActors[call.target].token;
    context.binding=action->bindings[call.sub];if(!context.target.Valid())return nullptr;
    for(unsigned group=0;group<2;++group)if(!Copy(context.before[group].data(),
        reinterpret_cast<const void*>(statusActors[call.target].address+0x774+728*group),4))return nullptr;
    return &context;
}
unsigned StatusRoll(unsigned source,const CommandBinding& binding,ActorToken target,ActionToken action) noexcept {
    bool uncertain=false;
    for(const auto& effect:binding.effects){
        const auto before=statusState.Snapshot(action,target,effect.element);
        unsigned chance=effect.chanceBp;
        if(effect.kind==EffectKind::Imperil)chance=chance*(10000-before.imperilResistanceBp)/10000;
        if(chance&&chance<10000)uncertain=true;
    }
    if(!uncertain)return 0;
    __try {
        const auto stream=reinterpret_cast<unsigned(__cdecl*)(unsigned,unsigned)>(module+0x38D2D0)(source,0);
        return reinterpret_cast<unsigned(__cdecl*)(unsigned)>(module+0x398900)(stream)%10000;
    }__except(EXCEPTION_EXECUTE_HANDLER){return UINT_MAX;}
}
void AfterStatusResult(void* token,const SharedAction::ResultCall& call,int,bool completed) noexcept {
    const auto* context=static_cast<const ResultStatusContext*>(token);
    if(!completed||!context||context->generation!=generation.load()||!CurrentData()||call.source>=ActorCount||
       call.target>=ActorCount||context->binding>=pack.commands.size())return;
    auto& action=nativeActions[call.source];
    if(action.token.sequence!=context->action.sequence||action.token.generation!=context->action.generation||
       !(statusState.Current(call.target)==context->target))return;
    bool landed=false;
    for(unsigned group=0;group<2;++group){
        const auto address=statusActors[call.target].address+0x774+728*group;
        std::array<Byte,4> after{};const auto& before=context->before[group];
        if(!Copy(after.data(),reinterpret_cast<const void*>(address),4)||before[2]!=call.source||before[3]!=call.sub||
           after[2]!=call.source||after[3]!=call.sub||after[0]<=before[0]||after[0]>16||after[0]>before[1])continue;
        SettleNulReservations(call,context->action,context->target,group,before[0],after[0]);
        for(unsigned hit=before[0];hit<after[0];++hit){Byte result=1;
            if(Read(address+24+44*hit+1,result)&&result==0)landed=true;}
    }
    const auto& binding=pack.commands[context->binding];
    if(!landed||binding.effects.empty()||action.attempted[call.target])return;
    const auto* expected=admitted.ExpectedCommand(binding.encoded);std::array<Byte,108> row{};
    if(!expected||!Copy(row.data(),expected->address,expected->width)||
       !admitted.Command(binding.encoded,expected->address,row.data(),expected->width))return;
    action.attempted[call.target]=true;
    const auto roll=StatusRoll(call.source,binding,context->target,context->action);
    (void)statusState.Queue(context->action,context->target,binding.effects.data(),
        static_cast<unsigned>(binding.effects.size()),roll,true);
}
void* BeforeStatusFinish(const SharedAction::FinishCall& call) noexcept {
    if(!CurrentData()||call.owner>=ActorCount||!SharedAction::finishDepth||SharedAction::finishDepth>finishContexts.size())return nullptr;
    auto* action=TrackAction(call.owner);if(!action)return nullptr;
    std::array<Byte,72> row{};unsigned index=0,count=0;
    if(!QueueRow(call.owner,row,index,count)||index!=call.index)return nullptr;
    auto& context=finishContexts[SharedAction::finishDepth-1];
    context={generation.load(),action->token,action->actor,count,row[2]>=row[3]&&!call.preserve};return &context;
}
void AfterStatusFinish(void* token,const SharedAction::FinishCall& call,int result,bool completed) noexcept {
    const auto* context=static_cast<const FinishStatusContext*>(token);
    if(!context||context->generation!=generation.load()||!CurrentData()||call.owner>=ActorCount)return;
    std::int8_t count=0;
    if(!completed||result!=1||!Read(module+0xD2BDE1,count)||count<0||static_cast<unsigned>(count)+1!=context->count)return;
    auto& action=nativeActions[call.owner];
    if(action.token.sequence!=context->action.sequence||action.token.generation!=context->action.generation)return;
    ReleaseNulReservations(context->action);
    if(context->complete)(void)statusState.Complete(context->action);
    else (void)statusState.Cancel(context->action);
    action={};
}
const SharedAction::Observer statusObserver{BeforeStatusResult,AfterStatusResult,BeforeStatusFinish,AfterStatusFinish};
void NewStatusBattle() noexcept {ResetElementData(Bus::ResetReason::BattleStart);}
const SharedBattleRuntime::ActionObserver statusBattleObserver{NewStatusBattle};
bool StartTactics(){
    if(!SharedAction::Start(module)||!SharedAction::Subscribe(SharedAction::Slot::Elemental,&statusObserver))return false;
    if(!SharedBattleRuntime::RegisterActionObserver(SharedBattleRuntime::ActionConsumer::Elemental,&statusBattleObserver)){
        SharedAction::Unsubscribe(SharedAction::Slot::Elemental,&statusObserver);return false;
    }
    return true;
}
void StopTactics() noexcept {
    SharedAction::Unsubscribe(SharedAction::Slot::Elemental,&statusObserver);
    SharedBattleRuntime::UnregisterActionObserver(SharedBattleRuntime::ActionConsumer::Elemental,&statusBattleObserver);
}
