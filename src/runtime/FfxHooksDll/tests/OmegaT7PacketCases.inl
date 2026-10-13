static unsigned unrelatedQueueCalls=0;
static void* ObserveUnrelatedQueue(const FfxHooks::SharedQueue::Call&,const unsigned char*&) noexcept {++unrelatedQueueCalls;return nullptr;}
static void AfterUnrelatedQueue(void*,const FfxHooks::SharedQueue::Call&,int,bool) noexcept {}
static void AtomicPacketCases(){
    namespace Q=FfxHooks::SharedQueue;
    if(!Q::installed.load())Q::original=reinterpret_cast<void*>(base+0x3B0B90);
    static const Q::Observer unrelated{ObserveUnrelatedQueue,AfterUnrelatedQueue};
    Check(Q::Register(&unrelated),"another queue consumer can register before Omega T7");
#ifdef FFXHOOKS_HAVE_POLYHOOK
    auto* site=reinterpret_cast<unsigned char*>(base+Q::AppendRva);const auto saved=site[31];DWORD protection=0,ignored=0;
    VirtualProtect(site+31,1,PAGE_EXECUTE_READWRITE,&protection);site[31]^=1;
    Check(!Q::Start(base),"the full native queue witness rejects a changed byte beyond the old sixteen-byte prefix");
    site[31]=saved;VirtualProtect(site+31,1,protection,&ignored);
    Check(Q::Start(base)&&O::Evidence::Profile(base),"Omega admits the signature-proven shared queue owner installed first");
    VirtualProtect(site+31,1,PAGE_EXECUTE_READWRITE,&protection);site[31]^=1;
    Check(!Q::Start(base)&&!O::Evidence::Profile(base),"changed owned queue bytes cannot reuse another consumer's signature proof");
    site[31]=saved;VirtualProtect(site+31,1,protection,&ignored);
    Check(Q::Start(base)&&O::Evidence::Profile(base),"restored shared queue entry regains exact admission");
#endif
    for(unsigned profile:{15u,16u}){
        Battle(profile);
        for(unsigned repeat=0;repeat<2;++repeat){
        *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=0;
        std::array<unsigned char,72> original{};original[0]=20;original[3]=1;Put(original.data()+8,0x6000,2);Put(original.data()+10,255,2);Put(original.data()+16,1);
        Check(Q::Shim(20,original.data(),0,0,0)==-1,"native ordinary monster packet appends once");
        auto* q=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());
        Check(Count()==1&&q[0]==20&&q[1]==0&&q[3]==2&&N::Word(q+8)==0x6000&&N::Word(q+24)==bindings.Get(O::Catalog[profile].skill).id,
            "normal command and Omega Break occupy one ordinary native packet before any command executes");
        Check(original[3]==1&&N::Word(original.data()+24)==0,"composition preserves the caller-owned original packet");
        using Rank=unsigned(__cdecl*)(const unsigned char*);
        using Cost=int(__cdecl*)(unsigned,const unsigned char*,int);
        Check(reinterpret_cast<Rank>(base+0x3895B0)(q)==reinterpret_cast<Rank>(base+0x3895B0)(original.data()),"native CTB rank remains that of the original first command");
        Check(reinterpret_cast<Cost>(base+0x3B0C20)(20,q,-1)==reinterpret_cast<Cost>(base+0x3B0C20)(20,original.data(),-1),"the complete packet retains the original native MP cost");
        Actor(20)[0xDE5]=0;q[4]=1;
        std::array<unsigned char,72> counter{};counter[0]=0;counter[1]=counter[3]=1;Put(counter.data()+8,0x3000,2);Put(counter.data()+10,255,2);Put(counter.data()+16,1u<<20);
        using Insert=int(__cdecl*)(unsigned,const unsigned char*,unsigned,unsigned,unsigned,unsigned,unsigned);
        Check(reinterpret_cast<Insert>(base+0x3B0A30)(0,counter.data(),0,0,1,0,20)==-1,"actual native counter inserts after the active monster packet");
        Check(Count()==2&&q[0]==20&&q[3]==2&&q[72]==0&&q[73]==1,"the counter cannot split the normal command from its Omega Break");
        q[2]=1;
        using Head=unsigned char*(__cdecl*)();
        Check(reinterpret_cast<Head>(base+0x3B0A10)()==q&&q[2]<q[3],"after the first command native head still owns the pending extra");
        q[2]=q[3];Check(A::FinishShim(20,0,0)==1&&Count()==1&&q[0]==0,"one final retirement releases the native counter with no new Omega reaction appended");
        Actor(0)[0xDE5]=0;q[2]=q[3];A::FinishShim(0,0,0);
        }
    }
    for(unsigned count=1;count<=4;++count){
        Battle(15);*reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=0;
        std::array<unsigned char,72> original{};original[0]=20;original[3]=static_cast<unsigned char>(count);
        for(unsigned sub=0;sub<count;++sub){Put(original.data()+8+16*sub,0x6000,2);Put(original.data()+10+16*sub,255,2);Put(original.data()+16+16*sub,1u<<sub);}
        using Append=Q::Function;
        Check(reinterpret_cast<Append>(base+Q::AppendRva)(20,original.data(),0,0,0)==-1,"the actual native append entry accepts an authored multi-command packet");
        auto* q=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());
        Check(q[3]==(count<4?count+1:count)&&!std::memcmp(q+8,original.data()+8,16*count),"one extra uses only a free native slot and preserves every original subcommand");
        if(count==4)Check(!std::memcmp(q,original.data(),72),"a full four-command packet remains byte-identical instead of spawning a separate action");
        Actor(20)[0xDE5]=0;Finish(20);
    }
    // A priority counter can enter ahead of an unstarted packet, reusing its
    // old index. Retiring that other actor must not erase the extra's lease.
    Battle(2);Queue(0,0x3000);Damage(0,20,0,1);Finish(0);Queue(20);
    std::array<unsigned char,72> counter{};counter[0]=1;counter[1]=counter[3]=1;Put(counter.data()+8,0x3000,2);Put(counter.data()+10,255,2);Put(counter.data()+16,1u<<21);
    using Insert=int(__cdecl*)(unsigned,const unsigned char*,unsigned,unsigned,unsigned,unsigned,unsigned);
    Check(reinterpret_cast<Insert>(base+0x3B0A30)(1,counter.data(),0,0,1,0,21)==-1,"priority counter enters ahead of an unstarted combined packet");
    auto* q=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());
    Check(Count()==2&&q[0]==1&&q[72]==20,"native insertion really moves the owned packet off its original index");
    Actor(1)[0xDE5]=0;Finish(1);Actor(20)[0xDE5]=0;StartExtra(20);
    const auto skill=bindings.Get(O::Skill::Aeroja);B::DamageScope scope;
    B::EnterDamage(scope,{20,Actor(20),0,Actor(0),skill.row.data(),skill.id});
    Check(B::UpperDamage(100,9999,20,skill.id,3,false)==130,"retiring the counter preserves the moved extra's charge and payload ownership");
    B::LeaveDamage(scope,0,true);Damage(20,0,130);Finish(20);
    Battle(15);Queue(20);const auto savedCount=q[3];
    std::array<unsigned char,72> duplicate{};duplicate[0]=20;duplicate[3]=1;Put(duplicate.data()+8,0x6000,2);Put(duplicate.data()+10,255,2);Put(duplicate.data()+16,1);
    Check(Q::Shim(20,duplicate.data(),0,0,0)==-1&&q[3]==savedCount&&q[72+3]==1,"a second pending ordinary packet cannot steal the same actor's live extra ticket");
    R::Stop();*reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=0;
    Check(Q::Shim(20,duplicate.data(),0,0,0)==-1&&q[3]==1,"stop leaves the retained native detour transparent with no Omega composition");
    Battle(15);
    Check(unrelatedQueueCalls!=0,"both registered consumers see the native append without replacing one another");
    Q::Unregister(&unrelated);Check(Q::Required(),"removing the other consumer preserves Omega's queue registration");
}
