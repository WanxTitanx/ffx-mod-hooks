// Jarvis-HOOK: use the exact native status and complete damage calculators.
static void NativeSupportCases(){
    using Status=int(__cdecl*)(unsigned,unsigned char*,unsigned,unsigned char*,const unsigned char*,void*,void*,unsigned char*,int,unsigned*,unsigned*);
    using Compute=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
    // Native aftermath queues display records; supply its bounded pools without
    // replacing the status, damage, result or actor-writing functions.
    std::array<unsigned char,128*20> displayRecords{};
    std::array<unsigned,128> displayPointers{};
    const auto displayRoot=N::ReadOr<unsigned>(base+0xD360B8),displayEnd=N::ReadOr<unsigned>(base+0xD360C0);
    Put(reinterpret_cast<void*>(base+0xD360B8),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(displayRecords.data())));
    Put(reinterpret_cast<void*>(base+0xD360C0),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(displayPointers.data())));
    for(unsigned profile:{17u,18u,19u,20u,21u,22u,23u}){
        Battle(profile);Queue(20);Damage(20,0);StartExtra(20);
        const auto skill=O::Catalog[profile].skill;const auto& entry=bindings.Get(skill);
        const auto* queued=reinterpret_cast<const unsigned char*>(base+P::Rva<0xD2AC70>());
        const unsigned sub=Sub(20),mask=N::Dword(queued+16+16*sub),wanted=O::TargetFor(skill)==O::Target::Monsters?0x700000u:0x100000u;
        Check(mask==wanted,"native enqueue targets the caster or living monster group exactly");
        std::array<unsigned char,44> info{};std::array<unsigned,64> hits{},effects{};unsigned flags=0,changed=0;
        const auto actor=Actor(20);actor[0x5AA]=100;Put(actor+0x5A4,1000);Put(actor+0x6E4,1000);
        phase="raw support status";
        reinterpret_cast<Status>(base+0x38AE00)(20,actor,20,actor,entry.row.data(),hits.data(),effects.data(),info.data(),0,&flags,&changed);
        const unsigned buffs=O::BuffsFor(skill);
        for(unsigned bit=0;bit<9;++bit)if(buffs&(1u<<bit))Check(info[7+O::DurationIndex[bit]]==254,"raw native positive-status calculation accepts the authored payload");
        info={};hits={};effects={};unsigned landed=0,missed=0;
        B::DamageScope scope;
        const auto lp=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&landed)),mp=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&missed));
        Check(B::EnterDamage(scope,{20,actor,20,actor,entry.row.data(),entry.id,info.data(),0,lp,mp,0}),"owned support enters the production damage observer");
        phase="full support damage calculation";
        const auto result=reinterpret_cast<Compute>(base+0x38E630)(20,actor,20,actor,scope.forwardCommand,entry.id,info.data(),0,lp,mp,0);
        const auto forwarded=static_cast<const unsigned char*>(scope.forwardCommand);
        std::printf("SUPPORT profile=%u command=%04X statusHits=%u result=%u outcome=%u hp=%u reflectChance=%u reflectDuration=%u info=",profile,entry.id,landed,result,info[1],N::Dword(info.data()+32),forwarded[0x3F],forwarded[0x4C]);
        for(auto b:info)std::printf("%02X",unsigned(b));std::printf("\n");
        for(unsigned bit=0;bit<9;++bit)if(buffs&(1u<<bit))Check(info[7+O::DurationIndex[bit]]==254,"complete native support calculation retains the granted status");
        const auto stack=O::StackFor(skill);if(stack<4)Check(actor[0x65E+O::NativeStackIndex[stack]]==1,"complete native support calculation applies the authored stat stack");
        Check(!N::Dword(info.data()+32),"support calculation cannot damage its ally");
        B::LeaveDamage(scope,result,true);
        phase="native support aftermath";
        auto* group=actor+0x774;std::memset(group,0,728*2);group[1]=1;group[2]=20;group[3]=static_cast<unsigned char>(sub);
        std::memcpy(group+24,info.data(),info.size());
        const auto previous=A::originals[0];A::originals[0]=reinterpret_cast<void*>(base+0x38F060);
        int outcome=0;const auto applied=A::ResultShim(20,sub,20,&outcome,nullptr);A::originals[0]=previous;
        std::printf("SUPPORT_APPLIED profile=%u result=%d consumed=%u hp=%u reflect=%u\n",profile,applied,group[0],N::ReadOr<unsigned>(reinterpret_cast<std::uintptr_t>(actor)+0x5D0),unsigned(actor[0x60D]));
        for(unsigned bit=0;bit<9;++bit)if(buffs&(1u<<bit))Check(actor[O::BuffOffsets[bit]]==254,"native result consumption publishes the support status to its actor");
        Finish(20);
    }
    struct Debuff {unsigned profile,chance,offset,mask;};
    for(const auto& test:std::array<Debuff,7>{{{10,3,0x606,8},{11,13,0x609,255},{12,24,0x614,255},
        {13,14,0x60A,255},{14,1,0x606,2},{15,7,0x606,128},{16,6,0x606,64}}}){
        for(const bool immune:{false,true}){
            phase="native debuff setup";Battle(test.profile);Queue(20);Damage(20,0);StartExtra(20);const unsigned sub=Sub(20);
            const auto& entry=bindings.Get(O::Catalog[test.profile].skill);auto* target=Actor(0);
            Put(target+0x5D0,10000);Put(target+0x594,10000);Put(target+0x5A4,10000);Put(target+0x6E4,10000);
            target[0x641+test.chance]=immune?255:0;Actor(20)[0x5AA]=100;Actor(20)[0x5A8]=20;
            std::array<unsigned char,3> prior{};std::memcpy(prior.data(),reinterpret_cast<void*>(base+0x3988F0),3);
            const unsigned char zero[]={0x33,0xC0,0xC3};DWORD old=0,ignored=0;
            VirtualProtect(reinterpret_cast<void*>(base+0x3988F0),3,PAGE_EXECUTE_READWRITE,&old);
            std::memcpy(reinterpret_cast<void*>(base+0x3988F0),zero,3);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+0x3988F0),3);
            std::array<unsigned char,44> info{};unsigned landed=0,missed=0;B::DamageScope scope;
            const auto lp=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&landed)),mp=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&missed));
            B::EnterDamage(scope,{20,Actor(20),0,target,entry.row.data(),entry.id,info.data(),0,lp,mp,0});
            phase="native debuff damage calculation";
            const auto result=reinterpret_cast<Compute>(base+0x38E630)(20,Actor(20),0,target,scope.forwardCommand,entry.id,info.data(),0,lp,mp,0);
            B::LeaveDamage(scope,result,true);
            std::memcpy(reinterpret_cast<void*>(base+0x3988F0),prior.data(),3);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+0x3988F0),3);
            VirtualProtect(reinterpret_cast<void*>(base+0x3988F0),3,old,&ignored);
            auto* group=target+0x774;std::memset(group,0,728*2);group[1]=1;group[2]=20;group[3]=static_cast<unsigned char>(sub);std::memcpy(group+24,info.data(),info.size());
            phase="native debuff aftermath";
            const auto previous=A::originals[0];A::originals[0]=reinterpret_cast<void*>(base+0x38F060);
            int outcome=0;A::ResultShim(20,sub,0,&outcome,nullptr);A::originals[0]=previous;
            const bool applied=(N::ReadOr<unsigned short>(reinterpret_cast<std::uintptr_t>(target)+test.offset)&test.mask)!=0;
            std::printf("DEBUFF profile=%u immune=%u applied=%u hp=%u\n",test.profile,unsigned(immune),unsigned(applied),N::ReadOr<unsigned>(reinterpret_cast<std::uintptr_t>(target)+0x5D0));
            Check(applied!=immune,"complete native debuff path applies to susceptible targets and respects Proof");Finish(20);
        }
    }
    Put(reinterpret_cast<void*>(base+0xD360B8),displayRoot);Put(reinterpret_cast<void*>(base+0xD360C0),displayEnd);
    phase="native empty input record";Battle(17);
    *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=0;
    std::array<unsigned char,72> empty{};empty.fill(0xA5);empty[0]=21;empty[3]=0;Put(empty.data()+8,0x78667698);Put(empty.data()+16,0);
    using Append=int(__cdecl*)(unsigned,const unsigned char*,unsigned,unsigned,unsigned);
    const auto appended=reinterpret_cast<Append>(base+0x3B0B90)(21,empty.data(),0,0,0);
    const auto* actual=reinterpret_cast<const unsigned char*>(base+P::Rva<0xD2AC70>());
    Check(appended==-1&&Count()==1&&!actual[3]&&N::Dword(actual+8)==0x78667698,"native zero-command input records retain unused opaque command bytes");
    Actor(21)[0xDE5]=0;
    Check(A::FinishShim(21,0,0)==1&&Count()==0&&!Actor(21)[0xDE7],"native zero-command retirement never interprets the unused command payload");
}
