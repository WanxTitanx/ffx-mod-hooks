// Real native command lookup, append and compaction with a private command table.
static unsigned finishWitnesses=0,finishWitnessFailures=0;
static bool epochDuringFollowFinish=false;
static std::array<int,FfxHooks::SharedAction::MaximumDepth> finishWitnessCounts{};
static void* BeforeFollowFinish(const FfxHooks::SharedAction::FinishCall&) noexcept {
    const auto depth=FfxHooks::SharedAction::finishDepth;
    if(!depth||depth>finishWitnessCounts.size())return nullptr;
    auto& count=finishWitnessCounts[depth-1];
    count=*reinterpret_cast<std::int8_t*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>());
    return &count;
}
static void AfterFollowFinish(void* token,const FfxHooks::SharedAction::FinishCall&,int result,bool completed) noexcept {
    if(!token||!completed||result!=1)return;
    ++finishWitnesses;
    const auto count=*reinterpret_cast<std::int8_t*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>());
    if(count!=*static_cast<int*>(token)-1)++finishWitnessFailures;
    if(epochDuringFollowFinish){
        epochDuringFollowFinish=false;
        // Reuse the remaining native row/actor allocation as byte-identical
        // foreign work in the new epoch. No second guard call occurs here.
        B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});
    }
}
static const FfxHooks::SharedAction::Observer followFinishWitness{nullptr,nullptr,BeforeFollowFinish,AfterFollowFinish};
static DWORD WINAPI ForeignFollowHead(void* output){
    constexpr unsigned rva=FfxHooks::ExecutableProfile::Steam20261001?0x3B0A10u:0x3B0A20u;
    *static_cast<unsigned char**>(output)=reinterpret_cast<unsigned char*(__cdecl*)()>(base+rva)();return 0;
}
#include "VanguardFollowInputCases.inl"
static void FollowUpCases(unsigned char* gear,std::vector<unsigned char>& kernel,bool combined=false){
    finishWitnesses=finishWitnessFailures=0;
    Check(FfxHooks::SharedAction::Subscribe(FfxHooks::SharedAction::Slot::Reserved,&followFinishWitness),
          "independent action retirement witness observes the shared producer");
    if(combined)W16(gear+14,255);
    std::array<unsigned char,116> commands{};W16(commands.data(),1);W16(commands.data()+12,96);W16(commands.data()+14,96);W32(commands.data()+16,20);
    auto* attack=commands.data()+20;attack[0x1A]=3;attack[0x20]=5;attack[0x23]=1;
    attack[0x24]=3;attack[0x28]=1;attack[0x29]=100;attack[0x2A]=16;attack[0x2B]=1;W32(attack+0x1C,0x40002);
    const auto table=reinterpret_cast<std::uintptr_t>(commands.data());std::memcpy(reinterpret_cast<void*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2A92C>())),&table,4);
    using Entry=const unsigned char*(__cdecl*)(unsigned,unsigned);
    Check(reinterpret_cast<Entry>(base+(::FfxHooks::ExecutableProfile::Rva<0x390AE0>()))(0x3000,0)==attack,"native Attack lookup uses the bounded command fixture");
    auto* queue=reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2AC70>()));auto* count=reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>()));
    auto reset=[&](unsigned steps=1){
        NewAction(steps);W32(Actor(18)+0x5D0,1000);W32(Actor(19)+0x5D0,1000);attack[0x25]=attack[0x26]=0;kernel[20+140*108+0x20]=0;
        for(unsigned owner=1;owner<18;++owner){auto* actor=Actor(owner);auto* record=gear+22*owner;
            std::memset(record,0,22);record[2]=1;record[4]=record[6]=static_cast<unsigned char>(owner);record[11]=4;
            for(unsigned i=0;i<4;++i)W16(record+14+2*i,255);if(owner==1)W16(record+14,0x808C);
            actor[0x592]=static_cast<unsigned char>(owner);actor[0xDC8]=actor[0xDD4]=1;actor[0xDCD]=actor[0x6D8]=actor[0x608]=0;
            actor[0xDE5]=255;actor[0xDE7]=0;W16(actor+0x606,0);W16(actor+0x664,1);W16(actor+0x690,0);W32(actor+0x5D0,1000);
        }
    };
    reset();const int mp=R32(Actor(1)+0x5D4),ctb=R32(Actor(1)+0x5A4);Hit(0,18,100);Complete(1);
    Check(*count==1&&queue[0]==1&&queue[1]==1&&queue[3]==1&&queue[8]==0&&queue[9]==0x30&&queue[10]==255&&queue[11]==0&&R32(queue+16)==(1<<18)&&Actor(1)[0xDE7]==1,
          "one eligible equipped ally queues one native reaction Attack against the original target");
    Check(R32(Actor(1)+0x5D4)==mp&&R32(Actor(1)+0x5A4)==ctb&&R32(Actor(0)+0x5D0)==1000,"enqueue never debits MP, rewrites CTB, or implicitly enables Vampirism");
    Check(finishWitnesses==1&&finishWitnessFailures==0,
          "shared status consumers see native retirement before a follow-up changes the queue count");
    Complete(1);Check(*count==1&&Actor(1)[0xDE7]==1,"duplicate root completion cannot append another follow-up");
    reset();queue[1]=1;Hit(0,18,100);Complete(1);Check(*count==0,"reaction and counter actions cannot start a follow-up chain");
    reset(2);Hit(0,18,100);Hit(1,18,100);Complete(2);Check(*count==1,"multiple hits on the same target trigger only one assist per ally");
    reset(2);Hit(0,18,100);Hit(1,19,100);Complete(2);Check(*count==0,"a multi-target action never becomes a single-target assist trigger");
    reset();W32(Actor(18)+0x5D0,1);Hit(0,18,100);Complete(1);Check(*count==0,"a dead original target is not queued for a follow-up");
    reset();Actor(1)[0xDC8]=0;Hit(0,18,100);Complete(1);Check(*count==0,"reserve or removed allies cannot assist");
    reset();Actor(1)[0xDD4]=0;Hit(0,18,100);Complete(1);Check(*count==0,"native cannot-act eligibility is preserved");
    reset();W16(Actor(1)+0x690,1);Hit(0,18,100);Complete(1);Check(*count==0,"a natively disabled Attack cannot be borrowed for an assist");
    reset();Hit(0,18,100);kernel[20+140*108+0x20]=1;Complete(1);Check(*count==0,"follow-up rechecks current loaded-kernel ownership before enqueue");
    reset();attack[0x25]=1;Hit(0,18,100);Complete(1);Check(*count==0,"modified resource-priced Attack cannot be issued free through Follow Up");
    reset();W16(gear+22*2+14,0x808C);
    for(unsigned index=1;index<62;++index){std::memset(queue+72*index,0,72);queue[72*index]=18;queue[72*index+3]=1;}
    *count=62;Hit(0,18,100);Complete(1);
    Check(*count==62&&queue[61*72]==1&&Actor(1)[0xDE7]==1&&Actor(2)[0xDE7]==0,"a single freed queue slot admits only one ally without overflow or failed-append turn side effects");
    if(combined){
        // Wakka is native party actor 4. Complete both his already queued turn
        // and his reaction with the real queue compactor and HP consumer.
        reset();W16(gear+14,0x808B);W16(gear+22+14,255);
        W16(gear+22*4+14,0x808C);W16(gear+22*4+16,0x808B);
        std::memcpy(queue+72,queue,72);queue[72]=4;
        *count=2;Actor(4)[0xDE5]=255;Actor(4)[0xDE7]=1;
        Hit(0,18,100);Complete(1);
        Check(R32(Actor(0)+0x5D0)==1002&&*count==2&&queue[0]==4&&queue[72]==4&&queue[73]==1&&Actor(4)[0xDE5]==255&&Actor(4)[0xDE7]==2,
              "Vampirism settles once while Wakka keeps his normal queued turn and one separate assist");
        using Finish=int(__cdecl*)(unsigned,unsigned,unsigned);
        const auto finish=reinterpret_cast<Finish>(base+(::FfxHooks::ExecutableProfile::Rva<0x3B0870>()));
        // DE5 identifies the executing row, not a future row's position. Native
        // removal compacts the queue but the scheduler selects the next actor.
        Actor(4)[0xDE5]=0;
        Hit(0,18,100,0,true,0,4);queue[2]=1;
        const auto normalFinished=finish(4,0,0);
        Check(normalFinished==1&&R32(Actor(4)+0x5D0)==1002&&*count==1&&queue[1]==1&&Actor(4)[0xDE7]==1,
              "Wakka's normal turn settles without losing the queued reaction");
        Actor(4)[0xDE5]=0;
        Hit(0,18,100,0,true,0,4);queue[2]=1;
        Check(finish(4,0,0)==1&&R32(Actor(4)+0x5D0)==1004&&*count==0&&Actor(4)[0xDE7]==0,
              "Wakka's follow-up heals once and retires without recursive assists or a stuck queue count");
        Check(finish(4,0,0)==0&&R32(Actor(4)+0x5D0)==1004,
              "duplicate Wakka reaction completion cannot heal or consume a later turn");
    }
    // Reproduce the reported reciprocal owner sequence within one battle epoch.
    // Selecting a queued row below is a fixture operation, not a scheduler claim.
    reset();W16(gear+22+14,255);W16(gear+14,0x808C);W16(gear+22*6+14,0x808C);
    Actor(0)[0xDE5]=255;Actor(0)[0xDE7]=0;Actor(6)[0xDE5]=0;Actor(6)[0xDE7]=1;
    Actor(0)[0xDD4]=1;W16(Actor(0)+0x664,1);W16(Actor(0)+0x690,0);
    using Learned=int(__cdecl*)(unsigned,unsigned);
    Check(reinterpret_cast<Learned>(base+::FfxHooks::ExecutableProfile::Rva<0x39AD40>())(0,0x3000)==1,
          "the reciprocal Tidus fixture satisfies actual native Attack learning eligibility");
    W32(Actor(0)+0x5D0,10000);W32(Actor(6)+0x5D0,10000);W32(Actor(20)+0x5D0,10000);
    queue[0]=6;W16(queue+8,0x3115);W32(queue+16,1u<<20);
    using Finish=int(__cdecl*)(unsigned,unsigned,unsigned);
    using Append=int(__cdecl*)(unsigned,const unsigned char*,unsigned,unsigned,unsigned);
    const auto finish=reinterpret_cast<Finish>(base+::FfxHooks::ExecutableProfile::Rva<0x3B0870>());
    const auto append=reinterpret_cast<Append>(base+::FfxHooks::ExecutableProfile::Rva<0x3B0BA0>());
    Hit(0,20,100,0,true,0,6,0x3115);queue[2]=1;finish(6,0,0);
    Check(*count==1&&queue[0]==0&&queue[1]==1&&Actor(6)[0xDE7]==0&&Actor(0)[0xDE7]==1,
          "Rikku's normal action appends one Tidus reaction after native retirement");
    Actor(0)[0xDE5]=0;Hit(0,20,100,0,true,0,0);queue[2]=1;finish(0,0,0);
    Check(*count==0&&Actor(0)[0xDE7]==0&&Actor(0)[0xDE5]==255,
          "Tidus's first reaction leaves no queued action or active row");
    unsigned char next[72]{};next[0]=0;next[3]=1;W16(next+8,0x3115);W16(next+10,255);W32(next+16,1u<<20);
    Check(append(0,next,0,0,0)==-1,"Tidus's later ordinary command uses the actual native append ABI");
    Actor(0)[0xDE5]=0;Hit(0,20,100,0,true,0,0,0x3115);queue[2]=1;finish(0,0,0);
    Check(*count==1&&queue[0]==6&&queue[1]==1&&Actor(0)[0xDE7]==0&&Actor(6)[0xDE7]==1,
          "Tidus's later normal action appends only Rikku's reciprocal reaction");
    Actor(6)[0xDE5]=0;Hit(0,20,100,0,true,0,6);queue[2]=1;finish(6,0,0);
    Check(*count==0&&Actor(0)[0xDE7]==0&&Actor(6)[0xDE7]==0&&Actor(0)[0xDE5]==255&&Actor(6)[0xDE5]==255,
          "both reciprocal reactions retire without recursion or residual native pending counts");
    next[0]=4;Check(append(4,next,0,0,0)==-1,"the next ordinary Wakka row can be admitted after reciprocal reactions");
    Actor(4)[0xDE5]=0;const unsigned previousWakka=wakkaBegins;Hit(0,20,100,0,true,0,4,0x3115);
    Check(wakkaBegins==previousWakka+1,"the next Wakka damage callback remains reachable in the private native queue harness");
    Check(finishWitnessFailures==0,"every status-retirement witness retains the native removal boundary across reciprocal actions");
    // The real dispatcher asks this native accessor for the head BEFORE it
    // starts target/animation work. A target may die after both assists queued.
    constexpr unsigned headRva=FfxHooks::ExecutableProfile::Steam20261001?0x3B0A10u:0x3B0A20u;
    const auto head=reinterpret_cast<unsigned char*(__cdecl*)()>(base+headRva);
    reset();W16(gear+14,255);W16(gear+22*6+14,0x808C);
    Actor(23)[0xDC8]=1;Actor(23)[0xDCC]=Actor(23)[0xDCD]=Actor(23)[0xDCE]=0;
    W16(Actor(23)+0x606,0);W32(Actor(23)+0x5D0,200);W32(queue+16,1u<<23);
    Hit(0,23,50);Complete(1);
    Check(*count==2&&queue[0]==1&&queue[72]==6,"two assists retain the original Omega actor 23 before either executes");
    Actor(1)[0xDE5]=0;Hit(0,23,500,0,true,0,1);queue[2]=1;finish(1,0,0);
    Check(R32(Actor(23)+0x5D0)==0&&R32(Actor(20)+0x5D0)>0,"first assist kills the original target while another enemy remains alive");
    Check(head()==nullptr&&*count==0&&Actor(6)[0xDE7]==0&&Actor(6)[0xDE5]==255,
          "a pending follow-up to the now-dead target retires before native target/animation dispatch");
    auto pending=[&](){
        reset();W16(gear+14,255);Actor(18)[0xDC8]=1;
        Actor(18)[0xDCC]=Actor(18)[0xDCD]=Actor(18)[0xDCE]=0;W16(Actor(18)+0x606,0);
        Hit(0,18,1);Complete(1);
        Check(*count==1&&queue[0]==1&&Actor(1)[0xDE7]==1,"pending assist is produced by a completed ordinary action");
    };
    pending();std::array<unsigned char,72> healthy{};std::memcpy(healthy.data(),queue,72);
    Check(head()==queue&&*count==1&&!std::memcmp(healthy.data(),queue,72),"a living target keeps its original assist packet unchanged");
    for(unsigned invalid=0;invalid<8;++invalid){
        pending();const auto model=Word(Actor(18)+0xE);const int resource=R32(Actor(18)+0x48);
        switch(invalid){
            case 0:W32(Actor(18)+0x5D0,0);break;
            case 1:Actor(18)[0xDC8]=0;break;
            case 2:Actor(18)[0xDCC]=1;break;
            case 3:Actor(18)[0xDCD]=1;break;
            case 4:Actor(18)[0xDCE]=1;break;
            case 5:W16(Actor(18)+0x606,5);break;
            case 6:W16(Actor(18)+0xE,model+1);break;
            case 7:W32(Actor(18)+0x48,resource+1);break;
        }
        Check(head()==nullptr&&*count==0&&Actor(1)[0xDE7]==0,"dead, absent, removed, petrified or replaced original targets are never dispatched");
        W16(Actor(18)+0xE,model);W32(Actor(18)+0x48,resource);
    }
    pending();W32(Actor(1)+0x5D0,0);
    Check(head()==nullptr&&*count==0,"a dead assisting actor does not keep an orphan reaction");
    pending();std::array<unsigned char,72> ordinary=healthy,foreign=healthy;ordinary[1]=0;W32(ordinary.data()+16,1u<<20);
    Check(append(1,ordinary.data(),0,0,0)==-1&&append(1,foreign.data(),0,0,1)==-1,"native appends retain a normal turn and an identical foreign reaction behind the owned assist");
    const auto inputCount=*reinterpret_cast<unsigned char*>(base+FfxHooks::ExecutableProfile::Rva<0xD2BDE0>());
    const auto pendingInput=Actor(1)[0xDE6],wait=Actor(1)[0x65C];const int originalMp=R32(Actor(1)+0x5D4),originalHp=R32(Actor(1)+0x5D0);
    W32(Actor(18)+0x5D0,0);
    Check(head()==queue&&*count==2&&Actor(1)[0xDE7]==2&&!std::memcmp(queue,ordinary.data(),72)&&!std::memcmp(queue+72,foreign.data(),72),
          "skip removes only the owned assist, preserving the normal turn and identical foreign counter");
    Check(Actor(1)[0xDE6]==pendingInput&&Actor(1)[0x65C]==wait&&R32(Actor(1)+0x5D4)==originalMp&&R32(Actor(1)+0x5D0)==originalHp&&
          *reinterpret_cast<unsigned char*>(base+FfxHooks::ExecutableProfile::Rva<0xD2BDE0>())==inputCount,
          "cancel does not spend the ordinary input turn, CTB wait, MP, HP or Vampirism healing");
    Actor(1)[0xDE5]=0;const int nextTargetHp=R32(Actor(20)+0x5D0);Hit(0,20,10,0,true,0,1);queue[2]=1;
    Check(R32(Actor(20)+0x5D0)==nextTargetHp-10&&finish(1,0,0)==1&&head()==queue&&*count==1,
          "the preserved ordinary turn reaches native damage and retirement while the foreign reaction stays unowned");
    pending();constexpr unsigned insertRva=FfxHooks::ExecutableProfile::Steam20261001?0x3B0A30u:0x3B0A40u;
    using Insert=int(__cdecl*)(unsigned,const unsigned char*,unsigned,unsigned,unsigned,unsigned,unsigned);
    const auto insert=reinterpret_cast<Insert>(base+insertRva);foreign[0]=4;W32(foreign.data()+16,1u<<20);
    Check(insert(4,foreign.data(),0,0,1,0,1)==-1&&queue[0]==4&&queue[72]==1,"native priority insertion moves the pending assist to a later row");
    W32(Actor(18)+0x5D0,0);Check(head()==queue&&*count==2,"a native inserted counter dispatches before any owned assist");
    Check(finish(4,0,0)==1&&head()==nullptr&&*count==0&&Actor(1)[0xDE7]==0,"ownership follows native insertion and retirement without skipping the wrong row");
    pending();foreign=healthy;
    Check(insert(1,foreign.data(),0,0,1,0,1)==-1,"native insertion can create two byte-identical reaction rows");
    W32(Actor(18)+0x5D0,0);
    Check(head()==queue&&*count==2,"ambiguous byte-identical insertion relinquishes ownership instead of deleting a foreign row");
    pending();W32(Actor(18)+0x5D0,0);Actor(1)[0xDE5]=0;queue[4]=1;
    Check(head()==queue&&*count==1,"an already selected animation/action remains exclusively owned by native completion");
    pending();W32(Actor(18)+0x5D0,0);W32(queue+16,1u<<20);
    Check(head()==queue&&*count==1&&R32(queue+16)==(1<<20),"an externally changed packet is not rewritten or removed");
    pending();W32(Actor(18)+0x5D0,0);unsigned char* foreignHead=nullptr;
    HANDLE thread=CreateThread(nullptr,0,ForeignFollowHead,&foreignHead,0,nullptr);
    const bool joined=thread&&WaitForSingleObject(thread,5000)==WAIT_OBJECT_0;if(thread)CloseHandle(thread);
    Check(joined&&foreignHead==queue&&*count==1,"a foreign thread can read the queue but cannot cancel owned assists");
    Check(head()==nullptr&&*count==0,"the owner thread cancels the same invalid assist exactly once");
    reset();W16(gear+14,255);W16(gear+22*6+14,0x808C);Hit(0,18,1);Complete(1);
    Check(*count==2&&queue[0]==1&&queue[72]==6,"two distinct owned assists prepare a generation change during cancellation");
    W32(Actor(18)+0x5D0,0);const auto beforeEpochWitness=finishWitnesses;epochDuringFollowFinish=true;
    Check(head()==queue&&*count==1&&queue[0]==6&&Actor(6)[0xDE7]==1&&finishWitnesses==beforeEpochWitness+1,
          "a battle epoch changed inside retirement stops the old drain before it cancels new byte-identical work");
    pending();W32(Actor(18)+0x5D0,0);
    B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});
    Check(head()==queue&&*count==1,"a new battle epoch cannot cancel an old identical row using stale ownership");
    FollowInputCases(pending,queue,count);
    pending();W32(Actor(18)+0x5D0,0);V::RequestStop();
    Check(head()==queue&&*count==1,"stop leaves both added native hooks inert and performs no queue writes");
    Check(finishWitnessFailures==0,"skipping an invalid assist preserves shared native-retirement observations");
    FfxHooks::SharedAction::Unsubscribe(FfxHooks::SharedAction::Slot::Reserved,&followFinishWitness);
}
