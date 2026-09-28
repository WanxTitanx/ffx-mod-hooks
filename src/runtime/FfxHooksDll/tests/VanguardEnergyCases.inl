// Jarvis-HOOK: the existing native selection and scalar debit must not change
// Energy Boost/Burst halfway through one queued action. No save or game runs.
static int __cdecl EnergyNotice(unsigned){return 0;}
static void EnergyCases(unsigned char* gear,std::vector<unsigned char>& kernel){
    using Select=void(__cdecl*)(unsigned,unsigned char*,const unsigned char*,unsigned);
    using Debit=void(__cdecl*)(unsigned);
    using Percent=int(__cdecl*)(unsigned,unsigned,const unsigned char*,int);
    const auto select=reinterpret_cast<Select>(base+0x3B03F0);
    const auto debit=reinterpret_cast<Debit>(base+0x38E5F0);
    const auto percent=reinterpret_cast<Percent>(base+0x3892A0);
    std::vector<unsigned char> commands(20+320*96+1);
    W16(commands.data(),1);W16(commands.data()+10,319);
    W16(commands.data()+12,96);W16(commands.data()+14,320*96);W32(commands.data()+16,20);
    const auto commandAddress=reinterpret_cast<std::uintptr_t>(commands.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A92C),&commandAddress,4);
    for(unsigned id:{64u,65u}){
        auto* row=commands.data()+20+96*id;
        row[0x19]=255;row[0x20]=2;row[0x23]=1;row[0x26]=40;
        row[0x28]=6;row[0x2A]=16;row[0x2B]=2;row[0x2D]=1;
    }
    auto* command=commands.data()+20+96*64;
    std::memset(reinterpret_cast<void*>(base+0x1F11240),0,31*4);
    W16(gear+14,0x8088);W16(gear+16,0x8089);
    auto* actor=Actor(0);actor[0x5BD]=100;W32(actor+0x5D4,1000);
    Check(PatchJump(base+0x3B06C0,reinterpret_cast<void*>(&EnergyNotice))&&
          PatchJump(base+0x3B0CE0,reinterpret_cast<void*>(&EnergyNotice)),
          "only the isolated fixture's post-debit notifications are replaced");
    auto queue=[&](){
        auto* row=reinterpret_cast<unsigned char*>(base+0xD2AC70);
        std::memset(row,0,72);row[3]=2;
        for(unsigned i=0;i<2;++i){W16(row+8+16*i,0x3040+i);W16(row+10+16*i,255);W32(row+16+16*i,1<<18);}
        *reinterpret_cast<unsigned char*>(base+0xD2BDE1)=1;
        actor[0xDE5]=0;actor[0xDE7]=1;return row;
    };
    NewAction(2);auto* action=queue();actor[0x5BC]=100;
    Check(percent(0,18,command,1000)==1650,"full gauge initially enables both equipped energy bonuses");
    select(0,actor,action,0);
    Check(actor[0x6CD]==40&&actor[0x5BC]==100,"native selection stages its fee without spending the gauge");
    debit(0);
    Check(actor[0x5BC]==60&&!actor[0x6CD],"the sole native debit spends forty once");
    Check(percent(0,18,command,1000)==1650,"the first paid hit retains the pre-debit energy snapshot");
    select(0,actor,action,0);
    Check(percent(0,18,command,1000)==1650,"a repeated selection callback cannot refresh the same action's snapshot");
    action[2]=1;actor[0x5BC]=0;
    select(0,actor,action,1);
    Check(percent(0,18,commands.data()+20+96*65,1000)==1650,
          "later subactions cannot lose their starting energy bonuses when OD changes");
    Check(Complete(2)==1,"the actual native queue producer removes the completed energy action");
    action=queue();actor[0x5BC]=0;select(0,actor,action,0);
    Check(percent(0,18,command,1000)==1000,"an identical later action does not inherit the previous full-gauge snapshot");
    actor[0x5BC]=100;
    Check(percent(0,18,command,1000)==1000,"gaining OD during an action does not retroactively activate energy bonuses");
    Check(Complete(0)==1,"native cancellation removes the unfinished action");
    action=queue();actor[0x5BC]=100;select(0,actor,action,0);
    Check(percent(0,18,command,1000)==1650,"a fresh action after cancellation captures its own current gauge");
    kernel[20+136*108+0x20]=1;
    Check(percent(0,18,command,1000)==1000,"a changed loaded-kernel proof suspends the bound action's energy snapshot");
    kernel[20+136*108+0x20]=0;
    B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});
    actor[0x5BC]=60;
    Check(percent(0,18,command,1000)==1200,"a new native battle generation cannot reuse an old action snapshot");
    auto* defender=Actor(1);auto* armor=gear+22;
    std::memset(armor,0,22);armor[2]=armor[4]=armor[5]=armor[6]=1;armor[11]=4;
    for(unsigned i=0;i<4;++i)W16(armor+14+2*i,255);
    W16(armor+14,0x8092);W16(armor+16,0x8093);defender[0x593]=1;defender[0x5BD]=100;
    action=queue();actor[0x5BC]=defender[0x5BC]=100;select(0,actor,action,0);
    debit(0);defender[0x5BC]=0;
    Check(percent(0,1,command,1000)==825,"one action retains both offensive and defensive pre-hit energy thresholds");
    Check(percent(0,1,command,-1000)==-1650,"a defensive energy snapshot never reduces signed restoration");
    Complete(2);action=queue();actor[0x5BC]=0;defender[0x5BC]=60;select(0,actor,action,0);
    Check(percent(0,1,command,1000)==800,"the next action captures Wall without inheriting an old Barrier");
    defender[0x5BC]=100;
    Check(percent(0,1,command,1000)==800,"OD gained during the action cannot activate Barrier between its hits");
    Complete(2);action=queue();action[0]=18;Actor(18)[0xDE5]=0;Actor(18)[0xDE7]=1;
    select(18,Actor(18),action,0);defender[0x5BC]=0;
    Check(percent(18,1,command,1000)==500,"an incoming enemy action also keeps the defending ally's energy snapshot");
    V::RequestStop();
    Check(percent(0,18,command,1000)==1000,"stopped energy adapters leave the original percentage stage unchanged");
}
