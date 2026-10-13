// Jarvis-HOOK: own only exact follow-up rows until native dispatch. Never infer
// ownership from reaction=1: native counters and other mods use the same packet.
constexpr unsigned FollowQueueRva=ExecutableProfile::Rva<0xD2AC70>();
constexpr unsigned FollowCountRva=ExecutableProfile::Rva<0xD2BDE1>();
using FollowRow=std::array<Byte,72>;
struct FollowTicket {
    FollowRow row{};std::uintptr_t actor=0,target=0;
    std::uint64_t trigger=0;unsigned targetId=31,resource=0;
    std::uint16_t model=0;
};
std::array<FollowTicket,62> followTickets{};
bool followInputDeferred=false;
void ResetFollowUps() noexcept {for(auto& ticket:followTickets)ticket={};followInputDeferred=false;}
int FollowQueueCount() noexcept {return Read<std::int8_t>(reinterpret_cast<void*>(module+FollowCountRva),-1);}
bool FollowTargetAlive(const Byte* actor) noexcept {
    return actor&&Read<Byte>(actor+0xDC8)&&!Read<Byte>(actor+0xDCC,1)&&
        !Read<Byte>(actor+0xDCD,1)&&!Read<Byte>(actor+0xDCE,1)&&
        Read<int>(actor+0x5D0)>0&&!(Read<std::uint16_t>(actor+0x606,5)&5);
}
void RememberFollowUp(unsigned index,unsigned ally,unsigned target,std::uint64_t trigger,const Byte row[72]) noexcept {
    auto& ticket=followTickets[index];ticket={};std::memcpy(ticket.row.data(),row,72);
    ticket.actor=reinterpret_cast<std::uintptr_t>(Actor(ally));ticket.target=reinterpret_cast<std::uintptr_t>(Actor(target));
    ticket.trigger=trigger;ticket.targetId=target;
    ticket.model=Read<std::uint16_t>(Actor(target)+0xE);
    ticket.resource=Read<unsigned>(Actor(target)+0x48);
}
void FollowRetired(unsigned index,int before,int result) noexcept {
    const int after=FollowQueueCount();
    if(result==1&&before>0&&before<=62&&index<static_cast<unsigned>(before)&&after==before-1){
        for(unsigned i=index;i+1<followTickets.size();++i)followTickets[i]=followTickets[i+1];
        followTickets.back()={};
    }else if(after!=before||result==1)ResetFollowUps();
}
using FollowHeadFn=Byte*(__cdecl*)();
bool StartedFollow(const FollowTicket& ticket,const FollowRow& row,unsigned index) noexcept {
    auto* actor=Actor(row[0]);
    return ticket.trigger&&actor&&reinterpret_cast<std::uintptr_t>(actor)==ticket.actor&&
        row[0]==ticket.row[0]&&row[1]==1&&row[3]==1&&row[2]<=1&&
        Word(row.data()+8)==Word(ticket.row.data()+8)&&Word(row.data()+10)==Word(ticket.row.data()+10)&&
        Read<Byte>(actor+0xDE5,255)==index;
}
void __cdecl FollowInputShim(){
    const auto original=reinterpret_cast<void(__cdecl*)()>(originals[FollowInputHook]);
    if(!On(Feature::FollowUp)||!Enter()||!SyncActionEpoch()){original();return;}
    const int inputs=Read<std::int8_t>(reinterpret_cast<void*>(module+ExecutableProfile::Rva<0xD2BDE0>()),-1);
    Byte input[8]{};const int count=FollowQueueCount();
    if(inputs>0&&inputs<=62&&count>0&&count<=62&&
       Copy(input,reinterpret_cast<void*>(module+ExecutableProfile::Rva<0xD2AA80>()),8)&&
       input[0]<31&&input[1]==0&&input[3]==0){
        for(unsigned i=0;i<static_cast<unsigned>(count);++i){
            const auto& ticket=followTickets[i];if(!ticket.trigger)continue;
            FollowRow row{};if(!Copy(row.data(),reinterpret_cast<void*>(module+FollowQueueRva+72*i),72))break;
            const bool pending=row==ticket.row&&reinterpret_cast<std::uintptr_t>(Actor(row[0]))==ticket.actor;
            if(!pending&&!StartedFollow(ticket,row,i))continue;
            // Native counters reserve an input entry as well as an action.
            // Our injected action has no input entry: hold the next unstarted
            // ordinary request until the assist retires. Otherwise native marks
            // it started before menu-open rejects a still-busy prior menu.
            if(!followInputDeferred)TraceAction("input-waits-follow",row[0],ticket.trigger,row.data());
            followInputDeferred=true;return;
        }
    }
    followInputDeferred=false;original();
}
Byte* __cdecl FollowHeadShim(){
    const auto original=reinterpret_cast<FollowHeadFn>(originals[FollowHeadHook]);
    if(!On(Feature::FollowUp)||!Enter()||!SyncActionEpoch())return original();
    const auto epoch=observedActionEpoch;
    // The native consumer has not set DE5 or started its target/animation state
    // at this accessor. Once started, native completion exclusively owns it.
    for(unsigned remaining=62;remaining&&Enter()&&actionEpoch.load()==epoch;--remaining){
        auto* head=original();const auto ticket=followTickets[0];
        if(!head||!ticket.trigger)return head;
        FollowRow row{};const int before=FollowQueueCount();
        if(before<1||before>62||head!=reinterpret_cast<Byte*>(module+FollowQueueRva)||
           !Copy(row.data(),head,row.size())){followTickets[0]={};return head;}
        if(row!=ticket.row){
            // Keep the input barrier while native owns a started assist, but
            // never cancel or edit its in-flight target/animation packet.
            if(!StartedFollow(ticket,row,0))followTickets[0]={};
            return head;
        }
        const unsigned owner=row[0];auto* actor=Actor(owner);
        if(owner>=18||owner==7||!actor||reinterpret_cast<std::uintptr_t>(actor)!=ticket.actor){followTickets[0]={};return head;}
        for(unsigned id=0;id<31;++id){auto* active=Actor(id);
            if(active&&Read<Byte>(active+0xDE5,255)!=255)return head;}
        auto* target=Actor(ticket.targetId);
        const bool valid=reinterpret_cast<std::uintptr_t>(target)==ticket.target&&
            target&&Read<std::uint16_t>(target+0xE)==ticket.model&&Read<unsigned>(target+0x48)==ticket.resource&&FollowTargetAlive(target);
        if(valid&&FollowTargetAlive(actor))return head;
        if(!Read<Byte>(actor+0xDE7)){followTickets[0]={};return head;}
        // Use the shared retirement entry, not the trampoline: status observers
        // must see cancellation and the exact count decrement before appenders.
        using Finish=int(__cdecl*)(unsigned,unsigned,unsigned);
        const int result=reinterpret_cast<Finish>(module+ExecutableProfile::Rva<0x3B0870>())(owner,0,0);
        // A retirement observer can load/start another battle and reuse the
        // same actor/row bytes. This drain owns only its original generation.
        if(!Enter()||actionEpoch.load()!=epoch)return original();
        if(result!=1||FollowQueueCount()!=before-1){ResetFollowUps();return original();}
        TraceAction(valid?"follow-skipped-owner":"follow-skipped-no-target",owner,ticket.trigger,row.data());
    }
    return original();
}
using FollowInsertFn=int(__cdecl*)(unsigned,const Byte*,unsigned,unsigned,unsigned,unsigned,unsigned);
int __cdecl FollowInsertShim(unsigned owner,const Byte* row,unsigned priority,unsigned mode,unsigned reaction,unsigned state,unsigned afterOwner){
    const auto original=reinterpret_cast<FollowInsertFn>(originals[FollowInsertHook]);
    if(!On(Feature::FollowUp)||!Enter()||!SyncActionEpoch())return original(owner,row,priority,mode,reaction,state,afterOwner);
    bool pending=false;for(const auto& ticket:followTickets)pending|=ticket.trigger!=0;
    if(!pending)return original(owner,row,priority,mode,reaction,state,afterOwner);
    std::array<FollowRow,62> beforeRows{},afterRows{};const int before=FollowQueueCount();
    const auto epoch=actionEpoch.load();
    const bool copied=before>=0&&before<62&&Copy(beforeRows.data(),reinterpret_cast<void*>(module+FollowQueueRva),72u*before);
    const int result=original(owner,row,priority,mode,reaction,state,afterOwner);
    const int after=FollowQueueCount();
    if(!running.load()||epoch!=actionEpoch.load())return result;
    if(result==0&&after==before)return result;
    if(!copied||result!=-1||after!=before+1||!Copy(afterRows.data(),reinterpret_cast<void*>(module+FollowQueueRva),72u*after)){
        ResetFollowUps();return result;
    }
    // Native counters can insert ahead of a pending assist. Prove the unique
    // insertion rather than shifting someone else's identical reaction ticket.
    unsigned insertion=62,matches=0;
    for(unsigned candidate=0;candidate<static_cast<unsigned>(after);++candidate){
        bool match=true;for(unsigned i=0;i<static_cast<unsigned>(before)&&match;++i)
            match=beforeRows[i]==afterRows[i+(i>=candidate?1u:0u)];
        if(match){insertion=candidate;++matches;}
    }
    if(matches!=1){ResetFollowUps();return result;}
    for(unsigned i=static_cast<unsigned>(before);i>insertion;--i)followTickets[i]=followTickets[i-1];
    followTickets[insertion]={};return result;
}
