// Jarvis-HOOK: current quotes reuse RonsoPool. The native scalar debit pays once.
namespace CostOwner=RonsoPool::CommandCosts;
struct CommandTableView {unsigned last=0;std::uintptr_t rows=0;};
bool ReadCommandTable(CommandTableView& out) noexcept {
    out={};const auto table=Read<std::uint32_t>(reinterpret_cast<void*>(module+0xD2A92C));Byte h[20]{};
    if(table<0x10000||table>UINT32_MAX-20||!Copy(h,reinterpret_cast<void*>(table),20))return false;
    const auto last=Word(h+10),bytes=Word(h+14),offset=Read<std::uint32_t>(h+16);
    if(Word(h)!=1||Word(h+8)||last>4095||Word(h+12)!=96||bytes!=(last+1)*96||offset!=20||table>UINT32_MAX-offset-bytes)return false;
    out={last,table+offset};return true;
}
bool CommandIdentity(const Byte* row,unsigned& id,Byte data[96]) noexcept {
    CommandTableView table{};if(!row||!ReadCommandTable(table))return false;
    const auto address=reinterpret_cast<std::uintptr_t>(row);
    if(address<table.rows||(address-table.rows)%96)return false;
    id=static_cast<unsigned>((address-table.rows)/96);return id<=table.last&&Copy(data,row,96);
}
bool LearnedCommand(const Byte* actor,unsigned id) noexcept {
    if(id>=320)return false;const auto mask=static_cast<std::uint16_t>(1u<<(id%16));
    return (Read<std::uint16_t>(actor+0x664+(id/16)*2)&mask)&&!(Read<std::uint16_t>(actor+0x690+(id/16)*2)&mask);
}
unsigned DiscountOverdrive(unsigned raw,bool efficiency) noexcept {return efficiency?(raw*75u+99u)/100u:raw;}
bool NativeCostActor(unsigned owner,Byte*& actor) noexcept {
    actor=Actor(owner);
    return owner<18&&owner!=7&&actor&&Read<std::uint16_t>(actor+0xE,0xFFFF)==owner&&Read<Byte>(actor+0xDC8)&&Read<int>(actor+0x5D0)>0&&!Read<Byte>(actor+0xDCC)&&!Read<Byte>(actor+0xDCE);
}
#include "VanguardEquipmentCommands.inl"
bool QuoteOverdrive(unsigned owner,const Byte* row,CostOwner::Quote& quote) noexcept {
    quote={};if(!Enter()||!(On(Feature::PartialOverdriveCosts)||On(Feature::Efficiency)))return false;
    unsigned id=0;Byte command[96]{};if(!CommandIdentity(row,id,command))return false;
    const bool header=(command[0x16]&0xF8)==8||(command[0x16]&0xF8)==16;
    BindingContext bindings{};LoadBindingContext(owner,bindings);const auto binding=FindBinding(bindings,id);
    if(!command[0x26]&&!header&&!(binding.granted&&binding.cost<256&&On(Feature::PartialOverdriveCosts)))return false;
    Byte* actor=nullptr;quote.command=0x3000+id;if(!NativeCostActor(owner,actor))return true;
    quote.charge=Read<Byte>(actor+0x5BC);quote.maximum=Read<Byte>(actor+0x5BD);
    Effects effects{},unused{};ReadEffects(actor,nullptr,effects,unused);if(!running.load())return true;
    const bool efficiency=effects[3];if(!On(Feature::PartialOverdriveCosts)&&!efficiency)return false;
    const bool free=Read<Byte>(reinterpret_cast<void*>(module+0xD2A90C))!=0;
    const unsigned raw=binding.granted&&binding.cost<256&&On(Feature::PartialOverdriveCosts)?binding.cost:command[0x26];
    quote.cost=free?0:DiscountOverdrive(raw,efficiency);
    if(!quote.maximum||quote.charge>quote.maximum||((Read<std::uint16_t>(actor+0x616)&0x400)&&!free)||
       (command[0x19]!=255&&command[0x19]!=owner)||!AllowedCommand(actor,id,binding))return true;
    if(header){
        CommandTableView table{};if(!ReadCommandTable(table))return true;unsigned best=UINT_MAX;
        for(unsigned child=0;child<=table.last&&child<320;++child){
            const auto* entry=reinterpret_cast<const Byte*>(table.rows+96*child);Byte data[96]{};
            if(!Copy(data,entry,96)||(data[0x16]&7)||data[0x18]!=command[0x17]||(data[0x19]!=255&&data[0x19]!=owner)||!AllowedCommand(actor,child,FindBinding(bindings,child)))continue;
            if((Read<std::uint32_t>(data+0x1C)&0x20000)&&Read<Byte>(actor+0x609))continue;
            const auto childBinding=FindBinding(bindings,child);
            const auto childRaw=childBinding.granted&&childBinding.cost<256&&On(Feature::PartialOverdriveCosts)?childBinding.cost:data[0x26];
            const auto price=free?0:DiscountOverdrive(childRaw,efficiency);const int mp=MpCostShim(owner,entry);
            if(mp>=0&&Read<int>(actor+0x5D4)>=mp&&price<=quote.charge)best=(std::min)(best,price);
        }
        if(best!=UINT_MAX){quote.cost=best;quote.allowed=true;}return true;
    }
    quote.allowed=quote.cost<=quote.charge;return true;
}
const CostOwner::Provider overdriveProvider{QuoteOverdrive};
bool CommitStagedCost(Byte* actor,std::uint16_t expected,std::uint16_t desired,Byte odFlag) noexcept {
    __try {
        const Byte prior=actor[0x5C0];
        if(static_cast<std::uint16_t>(_InterlockedCompareExchange16(reinterpret_cast<volatile short*>(actor+0x6CC),static_cast<short>(desired),static_cast<short>(expected)))!=expected)return false;
        if(static_cast<Byte>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(actor+0x5C0),static_cast<char>(odFlag),static_cast<char>(prior)))==prior)return true;
        (void)_InterlockedCompareExchange16(reinterpret_cast<volatile short*>(actor+0x6CC),static_cast<short>(expected),static_cast<short>(desired));return false;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
using CommitCostFn=int(__cdecl*)(const Byte*,int);
struct ApprovedCommandCost {
    std::uintptr_t actor=0;std::uint16_t pending=0;Byte pair[4]{};
    unsigned sub=0;bool armed=false;
};
ApprovedCommandCost approvedCommandCosts[18]{};
int BoundCommitCost(const Byte* source,int subAction){
    const auto original=reinterpret_cast<CommitCostFn>(originals[CommitCostHook]);
    if(!Enter()||!(On(Feature::Efficiency)||On(Feature::PartialOverdriveCosts)||On(Feature::EquipmentCommands))||!source||subAction<0||subAction>3)return original(source,subAction);
    Byte first[4]{};if(!Copy(first,source,4))return original(source,subAction);
    const unsigned owner=first[0];Byte* actor=Actor(owner);
    if(owner<18)approvedCommandCosts[owner]={};
    if(!actor||Read<Byte>(actor+0x6DE))return original(source,subAction);
    Byte ids[4]{};if(!Copy(ids,source+8+16*subAction,4))return 0;
    using Resolve=const Byte*(__cdecl*)(unsigned,unsigned,int,const Byte*,int*);
    int resolved=0;const auto* row=reinterpret_cast<Resolve>(module+0x38CF10)(owner,0,0x305F,ids,&resolved);
    if(On(Feature::EquipmentCommands)&&resolved>=0x3000&&resolved<=0x313F){
        BindingContext context{};LoadBindingContext(owner,context);const unsigned id=static_cast<unsigned>(resolved)&0xFFF;
        const auto binding=FindBinding(context,id);
        if((binding.configured||binding.previouslyGranted)&&!AllowedCommand(actor,id,binding))return 0;
    }
    CostOwner::Quote quote{};if(!QuoteOverdrive(owner,row,quote))return original(source,subAction);
    if(!quote.allowed||quote.command!=static_cast<unsigned>(resolved)||!CostOwner::nativeReady.load())return 0;
    const auto pending=Read<std::uint16_t>(actor+0x6CC);const int mp=CostOwner::EvaluateNative(owner,row,0);
    // Wide fees are rejected, never silently truncated into the native BYTE.
    if(mp<0||mp>255||Read<int>(actor+0x5D4)<mp||quote.cost>255)return 0;
    CostOwner::Quote current{};
    if(!QuoteOverdrive(owner,row,current)||!current.allowed||current.command!=quote.command||current.cost!=quote.cost||current.charge!=quote.charge||current.maximum!=quote.maximum)return 0;
    const auto desired=static_cast<std::uint16_t>(unsigned(mp)|(current.cost<<8));
    if(!CommitStagedCost(actor,pending,desired,Read<Byte>(row+0x26)?1:0)){
        running=false;if(logger)logger("[ffx-hooks] Vanguard: command cost staging conflict; admission closed\n");return 0;
    }
    auto& approved=approvedCommandCosts[owner];approved.actor=reinterpret_cast<std::uintptr_t>(actor);
    approved.pending=desired;approved.sub=static_cast<unsigned>(subAction);
    std::memcpy(approved.pair,ids,4);approved.armed=true;
    return -1;
}
using SelectedCostFn=void(__cdecl*)(unsigned,Byte*,const Byte*,unsigned);
void BoundSelectedCost(unsigned owner,Byte* actor,const Byte* source,unsigned sub){
    const auto original=reinterpret_cast<SelectedCostFn>(originals[SelectedCostHook]);
    if(!Enter()||owner>=18||Actor(owner)!=actor||!source||sub>=4){original(owner,actor,source,sub);return;}
    Byte pair[4]{};const auto approved=approvedCommandCosts[owner];
    const bool own=approved.armed&&approved.actor==reinterpret_cast<std::uintptr_t>(actor)&&approved.sub==sub&&
        Read<Byte>(source,255)==owner&&Copy(pair,source+8+16*sub,4)&&!std::memcmp(pair,approved.pair,4)&&
        Read<std::uint16_t>(actor+0x6CC)==approved.pending&&!Read<Byte>(actor+0x6CF);
    approvedCommandCosts[owner]={};
    // Preserve native selection flags and Grand Summon/full-gauge semantics. Its
    // ordinary row-cost rewrite is replaced only by THIS accepted one-shot fee.
    original(owner,actor,source,sub);
    if(!own||!running.load())return;
    const auto after=Read<std::uint16_t>(actor+0x6CC);
    if((after&255)!=(approved.pending&255))return;
    __try {
        if(static_cast<std::uint16_t>(_InterlockedCompareExchange16(reinterpret_cast<volatile short*>(actor+0x6CC),
            static_cast<short>(approved.pending),static_cast<short>(after)))!=after)running=false;
    }__except(EXCEPTION_EXECUTE_HANDLER){running=false;}
}
bool CommitCostProfile(std::uintptr_t base) noexcept {
    // Both original command branches must still reach Ronso's exact cost owner.
    const Byte first[]={0xE8,0x20,0x1B,0,0},second[]={0xE8,0xEB,0x1A,0,0};Byte actual[5]{};
    return Copy(actual,reinterpret_cast<void*>(base+0x38AC2B),5)&&!std::memcmp(actual,first,5)&&Copy(actual,reinterpret_cast<void*>(base+0x38AC60),5)&&!std::memcmp(actual,second,5);
}
