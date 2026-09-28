// Real Ronso cost owner, temporary equipment commands and native scalar debit.
static void EquipmentCommandCases(unsigned char* actor,unsigned char* gear,std::vector<unsigned char>& commands){
    using Availability=int(__cdecl*)(unsigned,unsigned);using Commit=int(__cdecl*)(const unsigned char*,int);using Debit=void(__cdecl*)(unsigned);
    const auto available=reinterpret_cast<Availability>(base+0x39AD40);const auto commit=reinterpret_cast<Commit>(base+0x38ABE0);const auto debit=reinterpret_cast<Debit>(base+0x38E5F0);
    auto* command=commands.data()+20+65*96;std::memcpy(command,commands.data()+20+64*96,96);W32(command+0x1C,2);W16(actor+0x6BC,0);
    W32(actor+0x5D4,1000);W16(gear+16,0x8087);actor[0x5BC]=9;
    const auto learnedBefore=std::array<unsigned char,2>{actor[0x66C],actor[0x66D]};
    V::BindingState bindings{};
    Check(V::ReadBindings(bindings)&&bindings.entries[0].code==V::BindingCode::Valid&&bindings.entries[0].command==0x3041&&bindings.entries[0].cost==12,
          "equipment binding has current kernel/mapping proof and an explicit OD fee");
    Check(available(0,0x3041)==1&&actor[0x66C]==learnedBefore[0]&&actor[0x66D]==learnedBefore[1],
          "equipped ability grants a command without teaching native or saved banks");
    Check(available(0,65)==1,"native menu builders may query the same command by unencoded table index");
    Check(available(0,0x2041)==0,"an item-table ID cannot borrow a same-index equipment command");
    std::array<unsigned char,72> action{};action[3]=1;W16(action.data()+8,0x3041);W16(action.data()+10,255);
    R::CommandCosts::Quote quote{};
    Check(R::CommandCosts::Read(0,command,quote)&&quote.allowed&&quote.cost==9,"equipment fee12 receives Efficiency exactly once to become9");
    Check(commit(action.data(),0)==-1&&actor[0x6CD]==9&&actor[0x6CC]==15,"temporary command stages its validated OD and native MP fees");
    using ResolveCost=void(__cdecl*)(unsigned,unsigned char*,const unsigned char*,unsigned);
    reinterpret_cast<ResolveCost>(base+0x3B03F0)(0,actor,action.data(),0);
    Check(actor[0x6CD]==9&&actor[0x6CC]==15,"the subsequent native selected-command producer must not overwrite the approved equipment price");
    debit(0);Check(actor[0x5BC]==0&&*reinterpret_cast<int*>(actor+0x5D4)==985,"equipment command uses the same one-time native debit");
    W16(gear+16,255);actor[0x5BC]=100;
    Check(available(0,0x3041)==0&&commit(action.data(),0)==0&&!actor[0x6CD],"unequipping removes the transient command and rejects stale selection");
    W16(actor+0x66C,3);
    Check(available(0,0x3041)==1&&commit(action.data(),0)==-1&&actor[0x6CD]==30,"native learning survives unequip without retaining the absent equipment fee");debit(0);
    W16(actor+0x66C,1);W16(gear+16,0x8087);actor[0x5BC]=9;W16(actor+0x698,2);
    Check((available(0,0x3041)&2)!=0&&commit(action.data(),0)==0,"equipment cannot override native disabled-command restrictions");W16(actor+0x698,0);
    command[0x19]=1;
    Check(available(0,0x3041)==0&&commit(action.data(),0)==0,
          "a binding cannot expose another character's exclusive command");
    command[0x19]=255;
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=0;
    C::SetProvidersForTests({nullptr,nullptr,nullptr,[](void*,const char*,const char*){return true;}});
    Check(!V::SaveBinding(1,(13u<<16)|0x3041,bindings.stamp),"two abilities cannot disagree about one command's owner or price");
    Check(!V::SaveBinding(0,UINT_MAX,bindings.stamp),"malformed packed bindings cannot wrap IDs or costs");
    Check(V::SaveBinding(0,0,bindings.stamp),"an explicit disabled binding is persisted through the authoritative writer");
    Check(!V::SaveBinding(0,(13u<<16)|0x3041,bindings.stamp),"old editor proof cannot overwrite a newer configuration");
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=1;
    Check(available(0,0x3041)==0&&commit(action.data(),0)==0,"deleted binding leaves no usable unlearned command in a stale menu");
}
