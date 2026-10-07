#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: active native command identity, actual HP loss and one settlement.
struct ActionBinding {
    std::uint64_t token=0;std::uintptr_t actor=0;std::uint16_t commands[8]{};
    Byte count=0,grants[4]{},offensive[4]{};bool applied=false,single=true;
    std::uint32_t targets=0;
};
ActionLedger actions;
ActionBinding actionBindings[31]{};
std::atomic<unsigned> actionEpoch{0};
unsigned observedActionEpoch=0;
std::uint64_t actionSerial=0;
unsigned actionTraceRemaining=0;
void TraceAction(const char* event,unsigned owner,std::uint64_t token,const Byte row[72],unsigned amount=0) noexcept {
    if(!logger||!actionTraceRemaining)return;
    --actionTraceRemaining;auto* actor=Actor(owner);char line[640]{};
    std::snprintf(line,sizeof(line),"[ffx-hooks] Vanguard action event=%s battle=%u token=%llu owner=%u reaction=%u cursor=%u/%u command=%04X targets=%08X queue=%d active=%u pending=%u plannedHeal=%u turnActor=%d actorGate=%u/%u/%u/%u rank=%u raw5A4=%d thread=%lu remaining=%u\n",
        event,observedActionEpoch,static_cast<unsigned long long>(token),owner,unsigned(row[1]),unsigned(row[2]),unsigned(row[3]),
        Word(row+8),Read<std::uint32_t>(row+16),int(Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())))),
        actor?unsigned(Read<Byte>(actor+0xDE5)):255u,actor?unsigned(Read<Byte>(actor+0xDE7)):255u,amount,
        int(Read<std::int8_t>(reinterpret_cast<void*>(module+::FfxHooks::ExecutableProfile::Rva<0xD2C9E4>()),-1)),
        actor?unsigned(Read<Byte>(actor+0x41C)):255u,actor?unsigned(Read<Byte>(actor+0x41D)):255u,
        actor?unsigned(Read<Byte>(actor+0x432)):255u,actor?unsigned(Read<Byte>(actor+0x508)):255u,
        actor?unsigned(Read<Byte>(actor+0xDE8)):255u,actor?Read<int>(actor+0x5A4):-1,
        GetCurrentThreadId(),actionTraceRemaining);
    logger(line);
}
void NewActionBattle() noexcept {InvalidateCastingActions();InvalidateFormationActions();InvalidateEnergyActions();if(actionEpoch.fetch_add(1)==UINT_MAX)running=false;}
const SharedBattleRuntime::ActionObserver actionObserver{NewActionBattle};
bool SyncActionEpoch() noexcept {
    const auto epoch=actionEpoch.load();if(!epoch)return false;
    if(epoch!=observedActionEpoch){actions.Reset();for(auto& value:actionBindings)value={};observedActionEpoch=epoch;actionTraceRemaining=128;}
    return true;
}
bool ActionRow(unsigned owner,Byte row[72],unsigned* index=nullptr) noexcept {
    auto* actor=Actor(owner);if(!actor)return false;
    const unsigned slot=Read<Byte>(actor+0xDE5,255);
    const int count=Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())));
    if(count<1||count>62||slot>=static_cast<unsigned>(count)||
       !Copy(row,reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2AC70>())+72*slot),72)||row[0]!=owner||!row[3]||row[3]>4||row[2]>row[3])return false;
    if(index)*index=slot;return true;
}
bool SameAction(unsigned owner,const Byte row[72]) noexcept {
    const auto& binding=actionBindings[owner];
    if(!binding.token||binding.actor!=reinterpret_cast<std::uintptr_t>(Actor(owner))||binding.count!=row[3])return false;
    for(unsigned i=0;i<row[3];++i)if(binding.commands[2*i]!=Word(row+8+16*i)||binding.commands[2*i+1]!=Word(row+10+16*i))return false;
    return actions.Active(owner,binding.token,binding.actor);
}
void BeforeActionHit(unsigned owner,void* source,unsigned target,void*,const void* command,unsigned commandId) noexcept {
    if(!Enter()||!(On(Feature::Vampirism)||On(Feature::TurnEndBuffs)||On(Feature::FollowUp))||!SyncActionEpoch()||owner>=18||owner==7||Actor(owner)!=source||!command)return;
    auto* actor=static_cast<Byte*>(source);Byte row[72]{},data[96]{};
    if(!Read<Byte>(actor+0xDC8)||Read<int>(actor+0x5D0)<=0||!ActionRow(owner,row)||row[2]>=row[3]||!Copy(data,command,96))return;
    const unsigned sub=row[2];
    if(commandId!=Word(row+8+16*sub)&&commandId!=Word(row+10+16*sub))return;
    auto& binding=actionBindings[owner];
    if(binding.token&&!SameAction(owner,row)){
        (void)actions.Finish(owner,binding.token,binding.actor,false,false,false,0,0);binding={};
    }
    if(!binding.token){
        if(actionSerial==UINT64_MAX){running=false;return;}
        Effects first{},second{};ReadEffects(actor,nullptr,first,second);if(!running.load())return;
        binding.actor=reinterpret_cast<std::uintptr_t>(actor);binding.token=++actionSerial;binding.count=row[3];
        for(unsigned i=0;i<row[3];++i){binding.commands[2*i]=static_cast<std::uint16_t>(Word(row+8+16*i));binding.commands[2*i+1]=static_cast<std::uint16_t>(Word(row+10+16*i));}
        const auto buffs=On(Feature::TurnEndBuffs)?Read<Byte>(actor+0x640):Byte(0);
        if(!actions.Begin(owner,binding.token,binding.actor,first[4],buffs)){binding={};return;}
        TraceAction("begin",owner,binding.token,row);
    }
    binding.grants[sub]=static_cast<Byte>(Word(data+0x5A)&0x14);
    binding.offensive[sub]=(data[0x23]&1)&&!(data[0x20]&0x10);
    const auto mask=Read<std::uint32_t>(row+16+16*sub);
    if(row[1]||!binding.offensive[sub]||(data[0x1A]&4)||(Read<std::uint32_t>(data+0x1C)&0x8000)||
       target<18||target>=31||mask!=(1u<<target))binding.single=false;
    if(target<31)binding.targets|=1u<<target;
    if(On(Feature::TurnEndBuffs))actions.Used(owner,binding.token,static_cast<Byte>(((data[0x20]&4)?0x10:0)|(data[0x25]?4:0)));
}
struct AftermathContext {unsigned source=31,target=31,sub=4;std::uint64_t token=0;std::uintptr_t actor=0;bool offensive=false;};
thread_local const AftermathContext* applyingAction=nullptr;
using AftermathFn=int(__cdecl*)(unsigned,unsigned,unsigned,int*,void*);
int CallAftermath(const AftermathContext* context,unsigned source,unsigned sub,unsigned target,int* output,void* descriptors){
    const auto* prior=applyingAction;applyingAction=context;int result=0;
    __try {result=reinterpret_cast<AftermathFn>(originals[AftermathHook])(source,sub,target,output,descriptors);}
    __finally {applyingAction=prior;}
    return result;
}
int BoundAftermath(unsigned source,unsigned sub,unsigned target,int* output,void* descriptors){
    const auto original=reinterpret_cast<AftermathFn>(originals[AftermathHook]);
    if(!Enter()||!SyncActionEpoch()||source>=18||target>=31||sub>=4)return original(source,sub,target,output,descriptors);
    Byte row[72]{};auto* actor=Actor(target);
    if(!actor||!ActionRow(source,row)||sub!=row[2]||!SameAction(source,row))return original(source,sub,target,output,descriptors);
    auto& binding=actionBindings[source];const auto token=binding.token;
    const AftermathContext context{source,target,sub,token,binding.actor,binding.offensive[sub]!=0};
    Byte before[2][4]{};for(unsigned i=0;i<2;++i)(void)Copy(before[i],actor+0x774+728*i,4);
    const int result=CallAftermath(&context,source,sub,target,output,descriptors);
    if(!running.load()||actionEpoch.load()!=observedActionEpoch||binding.token!=token)return result;
    for(unsigned i=0;i<2;++i){Byte after[4]{};const auto* group=actor+0x774+728*i;
        if(!Copy(after,group,4)||before[i][2]!=source||before[i][3]!=sub||after[2]!=source||after[3]!=sub||before[i][0]>=after[0]||after[0]>16||after[0]>before[i][1])continue;
        binding.applied=true;
        for(unsigned hit=before[i][0];hit<after[0];++hit){Byte info[7]{};
            if(Copy(info,group+24+44*hit,7)&&info[1]==0)actions.Reapplied(target,static_cast<Byte>(info[6]&binding.grants[sub]&Read<Byte>(actor+0x640)));
        }
    }
    return result;
}
int __cdecl AftermathShim(unsigned source,unsigned sub,unsigned target,int* output,void* descriptors){
    auto* actor=Actor(target);Byte before[2][4]{};
    const bool observe=Enter()&&On(Feature::SingleUseThreaten)&&source<31&&target>=18&&target<31&&sub<4&&actor&&
        !(Read<std::uint16_t>(actor+0x606)&0x800);
    const auto identity=observe?Read<std::uint16_t>(actor+0xE):std::uint16_t(0);
    if(observe)for(unsigned i=0;i<2;++i)(void)Copy(before[i],actor+0x774+728*i,4);
    const int result=BoundAftermath(source,sub,target,output,descriptors);
    PumpFormationReplacements();
    if(!observe||!running.load()||Actor(target)!=actor||Read<std::uint16_t>(actor+0xE)!=identity||
       !(Read<std::uint16_t>(actor+0x606)&0x800))return result;
    for(unsigned i=0;i<2;++i){Byte after[4]{};const auto* group=actor+0x774+728*i;
        if(!Copy(after,group,4)||before[i][2]!=source||before[i][3]!=sub||after[2]!=source||after[3]!=sub||
           before[i][0]>=after[0]||after[0]>16||after[0]>before[i][1])continue;
        for(unsigned hit=before[i][0];hit<after[0];++hit){Byte info[22]{};
            if(!Copy(info,group+24+44*hit,sizeof(info))||info[1]||!(Word(info+20)&0x800))continue;
            // Vanilla already owns a diminishing-success BYTE in EACH enemy's
            // stat block. Exhaust that budget after actual application instead
            // of keeping a slot-keyed cache that leaks into a respawn. New native
            // actor initialization supplies its own budget; cure/revive does not.
            const auto chance=Read<Byte>(actor+0x64C);
            __try {
                (void)_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(actor+0x64C),0,static_cast<char>(chance));
            }__except(EXCEPTION_EXECUTE_HANDLER){running=false;}
            return result;
        }
    }
    return result;
}
using HpFn=int(__cdecl*)(unsigned,Byte*,int,int,int,int,int);
int __cdecl HpShim(unsigned target,Byte* actor,int amount,int display,int resultCode,int hitFlags,int otherFlags){
    const auto original=reinterpret_cast<HpFn>(originals[HpHook]);const auto* context=applyingAction;
    if(!Enter()||!context||context->target!=target||Actor(target)!=actor||!SyncActionEpoch()||!actions.Active(context->source,context->token,context->actor))return original(target,actor,amount,display,resultCode,hitFlags,otherFlags);
    const int before=Read<int>(actor+0x5D0);
    const int result=original(target,actor,amount,display,resultCode,hitFlags,otherFlags);
    if(running.load()&&actionEpoch.load()==observedActionEpoch&&Actor(target)==actor)actions.Lost(context->source,context->token,target,before,Read<int>(actor+0x5D0),context->offensive&&amount>0);
    return result;
}
using FinishFn=int(__cdecl*)(unsigned,unsigned,unsigned);
#include "VanguardFollowUp.inl"

struct DeferredFollowUp {
    ActionBinding binding{};std::uintptr_t actor=0;unsigned epoch=0,owner=31,index=255;
    bool pending=false;
};
thread_local std::array<DeferredFollowUp,SharedAction::MaximumDepth> deferredFollowUps{};

void AfterActionObserversFinish(const SharedAction::FinishCall& call,int result,bool completed) noexcept {
    const auto depth=SharedAction::finishDepth;
    if(!depth||depth>deferredFollowUps.size())return;
    auto& pending=deferredFollowUps[depth-1];const auto value=pending;pending={};
    if(!value.pending||!completed||result!=1||value.owner!=call.owner||value.index!=call.index||
       !Enter()||!SyncActionEpoch()||value.epoch!=actionEpoch.load()||
       reinterpret_cast<std::uintptr_t>(Actor(value.owner))!=value.actor)return;
    auto* actor=Actor(value.owner);
    if(actor&&Read<int>(actor+0x5D0)>0&&!(Read<std::uint16_t>(actor+0x606)&1))
        QueueFollowUps(value.owner,value.binding);
}
int FinishBoundAction(unsigned owner,unsigned index,unsigned preserveCurrent){
    const auto original=&CallEnergyFinishOriginal;
    if(!Enter()||!SyncActionEpoch()||owner>=18)return original(owner,index,preserveCurrent);
    Byte row[72]{};unsigned active=255;
    if(!ActionRow(owner,row,&active)||active!=index||!SameAction(owner,row))return original(owner,index,preserveCurrent);
    const auto binding=actionBindings[owner];const int before=Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())));
    const int result=original(owner,index,preserveCurrent);
    if(result!=1||Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())))!=before-1){TraceAction("remove-rejected",owner,binding.token,row);return result;}
    auto* actor=Actor(owner);const int hp=actor?Read<int>(actor+0x5D0):0,maximum=actor?Read<int>(actor+0x594):0;
    const auto status=actor?Read<std::uint16_t>(actor+0x606):std::uint16_t(1);
    const bool complete=running.load()&&actionEpoch.load()==observedActionEpoch&&row[2]>=row[3]&&binding.applied;
    const auto settlement=actions.Finish(owner,binding.token,reinterpret_cast<std::uintptr_t>(actor),complete,hp>0&&!(status&1),(status&2)!=0,hp>0?static_cast<unsigned>(hp):0,maximum>0?static_cast<unsigned>(maximum):0);
    TraceAction(complete?"settle":"cancel",owner,binding.token,row,settlement.heal);
    actionBindings[owner]={};if(!settlement.accepted||!complete||!actor)return result;
    if(settlement.consume){const Byte old=Read<Byte>(actor+0x640),desired=old&static_cast<Byte>(~settlement.consume);
        _InterlockedCompareExchange8(reinterpret_cast<volatile char*>(actor+0x640),static_cast<char>(desired),static_cast<char>(old));}
    if(settlement.heal&&settlement.heal<=INT_MAX){
        Effects current{},unused{};ReadEffects(actor,nullptr,current,unused);
        // Revalidate the equipped effect now, never reuse an old kernel proof.
        if(running.load()&&current[4])reinterpret_cast<HpFn>(originals[HpHook])(owner,actor,-static_cast<int>(settlement.heal),0,0,0,0);
    }
    // Store only the admitted normal action. Status/Nul Ward retirement must
    // consume the native queue delta before this action can append reactions.
    const auto depth=SharedAction::finishDepth;
    if(running.load()&&hp>0&&!(status&1)&&binding.single&&depth&&depth<=deferredFollowUps.size())
        deferredFollowUps[depth-1]={binding,reinterpret_cast<std::uintptr_t>(actor),actionEpoch.load(),owner,index,true};
    return result;
}
int __cdecl FinishShim(unsigned owner,unsigned index,unsigned preserve){
    const auto depth=SharedAction::finishDepth;
    if(depth&&depth<=deferredFollowUps.size())deferredFollowUps[depth-1]={};
    return FinishCastAction(owner,index,preserve,&FinishBoundAction);
}
