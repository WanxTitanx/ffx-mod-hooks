// Jarvis-HOOK: local cast pricing and executed-action rank; no kernel edits.
struct CastPriceContext {unsigned owner=31;const Byte* command=nullptr;};
thread_local const CastPriceContext* castPrice=nullptr;
thread_local int buildingCastWindow=-1;
thread_local unsigned buildingCastOwner=31;
bool CastMenuOwner(unsigned owner,unsigned* active=nullptr) noexcept {
    if(owner>=18||owner==7||!Read<Byte>(reinterpret_cast<void*>(module+0xD2A8E0)))return false;
    int current=Read<std::int8_t>(reinterpret_cast<void*>(module+0x1FCC092),-1);
    const bool constructing=buildingCastWindow>=0&&buildingCastOwner==owner;
    if(constructing)current=buildingCastWindow;
    if(current<0||current>=8)return false;
    const auto* window=reinterpret_cast<const Byte*>(module+0xF3C910+0xF0*current);
    if(Word(window+8)!=owner||(!constructing&&(Read<Byte>(window+1)<3||Read<Byte>(window+1)>6)))return false;
    for(int i=current;i>=0;--i){const auto* parent=reinterpret_cast<const Byte*>(module+0xF3C910+0xF0*i);
        const unsigned phase=Read<Byte>(parent+1);
        // Closed windows retain header/owner bytes. Only the constructor's own
        // frame may be unstaged; a stale parent cannot claim a later spell menu.
        const bool live=(constructing&&i==current)||(phase>0&&phase<=6);
        if(live&&Word(parent+8)==owner&&Word(parent+0xC)==0x3029){if(active)*active=static_cast<unsigned>(current);return true;}}
    return false;
}
struct CastExecution {
    std::uintptr_t actor=0;std::uint32_t epoch=0;std::uint16_t pending=0;
    Byte pair[4]{};bool approved=false,executed=false;
};
CastExecution castExecutions[18]{};
std::atomic<std::uint32_t> castEpoch{0};
void InvalidateCastingActions() noexcept {if(castEpoch.fetch_add(1)==UINT32_MAX)running=false;}
int AdjustCastMpCost(unsigned owner,const Byte* command,int cost) noexcept {
    if(cost<=0||!Enter()||!On(Feature::Quickcast)||!command)return cost;
    if(castPrice){if(castPrice->owner!=owner||castPrice->command!=command)return cost;}
    else {
        if(!CastMenuOwner(owner))return cost;
        unsigned id=0;Byte row[96]{};if(!CommandIdentity(command,id,row))return cost;
        const unsigned category=row[0x17],menu=row[0x16]&0xF8;
        if(menu==8||menu==16||(category!=1&&!(On(Feature::WhiteMagic)&&category==2)))return cost;
    }
    return cost>INT_MAX/2?INT_MAX:cost*2;
}
bool ReadCastSelection(const Byte* source,int sub,Byte first[4],Byte pair[4],const Byte*& command) noexcept {
    command=nullptr;
    if(!source||sub<0||sub>3||!Copy(first,source,4)||first[0]>=18||first[0]==7||
       !Copy(pair,source+8+16*sub,4)||Word(pair)!=0x3029)return false;
    if(first[1]||!first[3]||first[3]>4||static_cast<unsigned>(sub)>=first[3])return true;
    CommandTableView table{};const unsigned child=Word(pair+2);
    if(!ReadCommandTable(table)||child<0x3000||child>0x313F||(child&0xFFF)>table.last)return true;
    command=reinterpret_cast<const Byte*>(table.rows+96*(child&0xFFF));return true;
}
int CallCastCommit(const CastPriceContext* context,const Byte* source,int sub){
    const auto* prior=castPrice;castPrice=context;int result=0;
    __try {result=BoundCommitCost(source,sub);}
    __finally {castPrice=prior;}
    return result;
}
int __cdecl CommitCostShim(const Byte* source,int sub){
    if(!Enter()||!(On(Feature::Quickcast)||On(Feature::WhiteMagic)))return BoundCommitCost(source,sub);
    Byte first[4]{},pair[4]{};const Byte* command=nullptr;
    if(!ReadCastSelection(source,sub,first,pair,command))return BoundCommitCost(source,sub);
    const unsigned owner=first[0];castExecutions[owner]={};
    Byte* actor=nullptr;Byte data[96]{};
    if(!NativeCostActor(owner,actor)||!command||!Copy(data,command,96)||!LearnedCommand(actor,41)||
       !LearnedCommand(actor,Word(pair+2)&0xFFF)||(data[0x19]!=255&&data[0x19]!=owner)||
       (data[0x16]&0xF8)==8||(data[0x16]&0xF8)==16||
       (data[0x17]!=1&&!(On(Feature::WhiteMagic)&&data[0x17]==2))||
       (On(Feature::Quickcast)&&(first[3]!=1||sub!=0)))return 0;
    const CastPriceContext context{owner,command};const auto epoch=castEpoch.load();
    const int baseCost=NativeModifiedMpCost(owner,command);
    // Reject an unrepresentable native BYTE before calling the staging producer.
    if(baseCost<0||baseCost>(On(Feature::Quickcast)?127:255))return 0;
    const int result=CallCastCommit(&context,source,sub);
    if(result==-1&&On(Feature::Quickcast)&&running.load()&&castEpoch.load()==epoch){
        auto& record=castExecutions[owner];record.actor=reinterpret_cast<std::uintptr_t>(actor);
        record.epoch=epoch;record.pending=Read<std::uint16_t>(actor+0x6CC);
        std::memcpy(record.pair,pair,4);record.approved=true;
    }
    return result;
}
void CastSelected(unsigned owner,Byte* actor,const Byte* source,unsigned sub) noexcept {
    if(!Enter()||!On(Feature::Quickcast)||owner>=18||Actor(owner)!=actor||!source)return;
    auto& record=castExecutions[owner];Byte first[4]{},pair[4]{};
    const bool matched=record.approved&&record.actor==reinterpret_cast<std::uintptr_t>(actor)&&
        record.epoch==castEpoch.load()&&sub==0&&Copy(first,source,4)&&first[0]==owner&&!first[1]&&first[3]==1&&
        Copy(pair,source+8,4)&&!std::memcmp(pair,record.pair,4)&&Read<std::uint16_t>(actor+0x6CC)==record.pending;
    record.approved=false;record.executed=matched;
}
void __cdecl SelectedCostShim(unsigned owner,Byte* actor,const Byte* source,unsigned sub){
    BoundSelectedCost(owner,actor,source,sub);CastSelected(owner,actor,source,sub);
    CaptureEnergySelection(owner,actor,source,sub);
}
using CastFinishFn=int(__cdecl*)(unsigned,unsigned,unsigned);
int FinishCastAction(unsigned owner,unsigned index,unsigned preserve,CastFinishFn original){
    if(!Enter()||!On(Feature::Quickcast)||owner>=18)return original(owner,index,preserve);
    auto* actor=Actor(owner);const auto record=castExecutions[owner];Byte action[72]{};
    const int before=Read<std::int8_t>(reinterpret_cast<void*>(module+0xD2BDE1));
    const bool matched=record.executed&&record.epoch==castEpoch.load()&&actor&&
        record.actor==reinterpret_cast<std::uintptr_t>(actor)&&Read<Byte>(actor+0xDE5,255)==index&&
        before>0&&before<=62&&index<static_cast<unsigned>(before)&&!preserve&&
        Copy(action,reinterpret_cast<void*>(module+0xD2AC70+72*index),72)&&action[0]==owner&&!action[1]&&
        action[3]==1&&action[2]==1&&!std::memcmp(action+8,record.pair,4);
    const int result=original(owner,index,preserve);
    if(result==1)castExecutions[owner]={};
    if(matched&&result==1&&running.load()&&castEpoch.load()==record.epoch&&Actor(owner)==actor&&
       Read<std::int8_t>(reinterpret_cast<void*>(module+0xD2BDE1))==before-1){
        const auto rank=Read<Byte>(actor+0xDE8);
        __try {(void)_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(actor+0xDE8),2,static_cast<char>(rank));}
        __except(EXCEPTION_EXECUTE_HANDLER){running=false;}
    }
    return result;
}
using CastMenuFn=int(__cdecl*)(unsigned,unsigned,unsigned);
int CallCastMenu(unsigned window,unsigned owner,unsigned header){
    const int previousWindow=buildingCastWindow;const unsigned previousOwner=buildingCastOwner;
    buildingCastWindow=window<8?static_cast<int>(window):-1;buildingCastOwner=owner;int result=0;
    __try {result=reinterpret_cast<CastMenuFn>(originals[CastMenuHook])(window,owner,header);}
    __finally {buildingCastWindow=previousWindow;buildingCastOwner=previousOwner;}
    return result;
}
int __cdecl CastMenuShim(unsigned window,unsigned owner,unsigned header){
    const int result=CallCastMenu(window,owner,header);
    if(!Enter()||!On(Feature::Quickcast)||header!=0x3029||window>=8||owner>=18||owner==7)return result;
    auto* entry=reinterpret_cast<Byte*>(module+0xF3C910+0xF0*window);
    if(Read<std::int8_t>(reinterpret_cast<void*>(module+0x1FCC092),-1)!=static_cast<int>(window)||
       Word(entry+8)!=owner||Word(entry+0xC)!=header)return result;
    __try {(void)_InterlockedCompareExchange16(reinterpret_cast<volatile short*>(entry+0xA4),1,2);}
    __except(EXCEPTION_EXECUTE_HANDLER){running=false;}
    return result;
}
using CastListFn=const std::uint16_t*(__cdecl*)(unsigned,unsigned,int*);
std::uint16_t castLists[8][25]{};
const std::uint16_t* __cdecl CastListShim(unsigned owner,unsigned category,int* count){
    const auto original=reinterpret_cast<CastListFn>(originals[CastListHook]);
    const auto* native=original(owner,category,count);unsigned window=0;
    if(!Enter()||!On(Feature::WhiteMagic)||category!=1||!native||!count||*count<0||*count>24||!CastMenuOwner(owner,&window))return native;
    int whiteCount=0;const auto* whites=original(owner,2,&whiteCount);
    if(!whites||whiteCount<1||whiteCount>24)return native;
    CommandTableView table{};Byte header[96]{};
    if(!ReadCommandTable(table)||table.last<278||!Copy(header,reinterpret_cast<void*>(table.rows+96*278),96)||
       (header[0x16]&0xF8)!=8||header[0x17]!=2||(header[0x19]!=255&&header[0x19]!=owner))return native;
    auto* view=castLists[window];
    if(!Copy(view,native,static_cast<std::size_t>(*count)*2))return native;
    for(int i=0;i<*count;++i)if(view[i]==0x3116)return native;
    view[*count]=0x3116;++*count;return view;
}
