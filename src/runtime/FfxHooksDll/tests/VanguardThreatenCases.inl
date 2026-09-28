// Native Threaten calculation and the existing consumed-result boundary. The
// instance-owned resistance BYTE is also vanilla's diminishing-success budget.
static void ThreatenCases(){
    // The original Threaten resolver calls the original CTB Excel reader. Give
    // it a complete bounded native-format table rather than bypassing that call.
    std::array<unsigned char,20+256*2> ctb{};
    W16(ctb.data(),1);W16(ctb.data()+10,255);W16(ctb.data()+12,2);
    W16(ctb.data()+14,512);W32(ctb.data()+16,20);
    for(unsigned i=0;i<256;++i)ctb[20+2*i]=5;
    const auto ctbPointer=reinterpret_cast<std::uintptr_t>(ctb.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A94C),&ctbPointer,4);
    using Status=int(__cdecl*)(unsigned,unsigned char*,unsigned,unsigned char*,const unsigned char*,unsigned*,unsigned*,unsigned char*,int,int*,int*);
    const auto calculate=reinterpret_cast<Status>(base+0x38AEC0);
    using Apply=int(__cdecl*)(unsigned,unsigned,unsigned,int*,void*);
    const auto apply=reinterpret_cast<Apply>(base+0x38F0B0);
    auto* source=Actor(0);auto* target=Actor(18);
    std::array<unsigned char,96> command{};command[0x2E+11]=254;
    W32(source+0x5D0,1000);source[0x65D]=5;source[0xDE8]=3;
    W16(target+0x606,0);W16(target+0x62A,0);target[0x613]=target[0x614]=0;
    target[0x641+11]=100;
    const auto pristine=std::vector<unsigned char>(target,target+0xF90);
    auto compute=[&](){
        std::array<unsigned char,44> info{};unsigned hits[8]{},effects[8]{};int amounts[3]{},out=0;
        calculate(0,source,18,target,command.data(),hits,effects,info.data(),0,amounts,&out);
        return info;
    };
    auto commit=[&](const std::array<unsigned char,44>& info){
        auto* group=target+0x774;std::memset(group,0,68);group[1]=1;group[2]=0;group[3]=0;
        std::memcpy(group+24,info.data(),44);requestedDamage[18]=0;int out=0;
        apply(0,0,18,&out,nullptr);
    };
    auto result=compute();
    Check((Word(result.data()+20)&0x800)!=0&&target[0x64C]==70,
          "native successful Threaten calculation preserves its ordinary chance update before application");
    Check((Word(target+0x606)&0x800)==0,"calculating or abandoning a result has not applied Threaten");
    commit(result);
    Check((Word(target+0x606)&0x800)!=0&&target[0x64C]==0,
          "one consumed successful Threaten exhausts this native enemy instance's chance budget");
    int out=0;apply(0,0,18,&out,nullptr);
    Check(target[0x64C]==0,"duplicate aftermath delivery does not create or refill a use");
    W16(target+0x606,0);result=compute();
    Check((Word(result.data()+20)&0x800)==0&&target[0x64C]==0,
          "cleansing Threaten does not permit a second success against the same enemy");
    W32(target+0x5D0,0);W16(target+0x606,1);W32(target+0x5D0,1000);W16(target+0x606,0);
    result=compute();Check((Word(result.data()+20)&0x800)==0,
          "reviving the same enemy does not refill its instance-owned Threaten budget");
    // A new native actor record can reuse the exact slot, address and species.
    // No Vanguard epoch/reset notification is supplied to hide an identity bug.
    std::memcpy(target,pristine.data(),0xF90);result=compute();
    Check((Word(result.data()+20)&0x800)!=0,"a freshly initialized identical enemy in the reused slot has its own use");
    commit(result);Check(target[0x64C]==0,"the replacement enemy can spend its own use only once");
    std::memcpy(target,pristine.data(),0xF90);target[0x64C]=0;result=compute();
    Check((Word(result.data()+20)&0x800)==0,"a natively immune enemy is not made vulnerable by the one-use rule");
}
