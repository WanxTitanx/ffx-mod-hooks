// Real native command lookup, append and compaction with a private command table.
static unsigned finishWitnesses=0,finishWitnessFailures=0;
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
}
static const FfxHooks::SharedAction::Observer followFinishWitness{nullptr,nullptr,BeforeFollowFinish,AfterFollowFinish};
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
    FfxHooks::SharedAction::Unsubscribe(FfxHooks::SharedAction::Slot::Reserved,&followFinishWitness);
}
