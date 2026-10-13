// Isolated native special-buff setter plus the existing real turn-edge producer.
static unsigned __cdecl ItemMpEndpoint(unsigned,void*,unsigned target,void* actor,const void* command,unsigned id,void*,unsigned,unsigned,unsigned,unsigned){
    const auto* row=static_cast<const unsigned char*>(command);
    if((id>>12)==2&&(row[0x5A]&4)){
        const unsigned prior=static_cast<unsigned char*>(actor)[0x640];
        reinterpret_cast<int(__cdecl*)(unsigned,void*,int)>(base+::FfxHooks::ExecutableProfile::Rva<0x38D330>())(target,actor,static_cast<int>(prior|row[0x5A]));
    }
    return 0;
}
static void SpiraItemMpCases(bool enabled){
    W::DamageProducerForTests(reinterpret_cast<void*>(&ItemMpEndpoint));
    using Damage=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
    const auto damage=reinterpret_cast<Damage>(base+::FfxHooks::ExecutableProfile::Rva<0x38E680>());
    const auto turn=reinterpret_cast<void(__cdecl*)(unsigned,void*)>(base+::FfxHooks::ExecutableProfile::Rva<0x3B13D0>());
    unsigned char item[96]{},info[128]{};item[0x5A]=4;
    for(unsigned owner=0;owner<3;++owner){Actor(owner)[0x640]=0;W32(Actor(owner)+0x598,999);W32(Actor(owner)+0x5D4,100);W32(Actor(owner)+0x5D0,500);}
    auto use=[&](unsigned id,unsigned target){return damage(0,Actor(0),target,Actor(target),item,id,info,0,0,0,0);};
    use(0x2042,1);
    Check((Actor(1)[0x640]&4)==(enabled?0:4),"Twin Stars replacement changes the real native special-buff mask only when enabled");
    Check(item[0x5A]==4,"original item command bytes are never patched");
    turn(1,Actor(1));Check(R32(Actor(1)+0x5D4)==(enabled?109:100),"recipient regenerates one percent at its own native turn edge");
    use(0x2042,1);turn(1,Actor(1));Check(R32(Actor(1)+0x5D4)==(enabled?118:100),"repeated item uses do not stack regeneration");
    for(unsigned owner=0;owner<3;++owner){W32(Actor(owner)+0x5D4,100);use(0x2045,owner);turn(owner,Actor(owner));
        Check(R32(Actor(owner)+0x5D4)==(enabled?109:100),"Three Stars follows each original party target independently");}
    W32(Actor(1)+0x5D4,997);turn(1,Actor(1));Check(R32(Actor(1)+0x5D4)==(enabled?999:997),"item regeneration does not exceed maximum MP");
    Actor(1)[0x640]|=4;use(0x2042,1);Check((Actor(1)[0x640]&4)!=0,"another source of native zero-cost MP remains in place");
    W32(Actor(1)+0x5D0,0);use(0x3000,1);W32(Actor(1)+0x5D0,500);W32(Actor(1)+0x5D4,100);turn(1,Actor(1));
    Check(R32(Actor(1)+0x5D4)==100,"KO clears regeneration before a later revival");
    W::DamageProducerForTests(reinterpret_cast<void*>(&DamageEndpoint));
}
