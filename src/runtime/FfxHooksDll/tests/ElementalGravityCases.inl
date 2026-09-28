// Jarvis-HOOK: real native percentage formula and immunity entry, followed by
// the existing cap policy. The fixture multiplier represents a later amplifier.
#include "../hooks/ElementAffinity.h"
static unsigned gravityChannel=1,gravityFlags=0,gravityBlocked=0;
static int gravityAmplifier=1,gravityNativeCap=99999;
static std::string GravityPack(const std::vector<unsigned char>& bank,
                               const std::vector<unsigned char>& file,bool overrideImmunity){
    auto json=MonsterPack(bank,file);
    const std::string requirement="\"mod007.context.v1\"";
    json.replace(json.find(requirement),requirement.size(),requirement+",\"mod007.gravity.v1\",\"mod007.spell-cap.v1\"");
    const std::string kind="\"imperil_limit\":2";
    json.replace(json.find(kind),kind.size(),kind+",\"gravity\":{\"maximum_hp_divisor\":16,\"nonlethal\":true,\"override_native_immunity\":"+
        std::string(overrideImmunity?"true":"false")+"}");
    std::string bindings;
    for(unsigned id=110;id<114;++id){
        bindings+=",{\"key\":\"gravity.test"+std::to_string(id)+"\",\"bank\":\"table.command\",\"index\":"+
            std::to_string(id)+",\"row_sha256\":\""+Hash(bank.data()+20+96*id,96)+
            "\",\"elements\":[{\"key\":\"tests.e9\",\"weight\":1}],\"gravity\":"+(id==112?"false":"true")+
            ",\"spell\":\""+(id==111?"fury":"native_magic")+"\"}";
    }
    json.insert(json.find("],\"profiles\""),bindings);return json;
}
static unsigned __cdecl GravityEndpoint(unsigned user,void* source,unsigned,void* target,
                                        const void* command,unsigned id,void*,unsigned,unsigned,unsigned,unsigned){
    const auto* row=static_cast<const unsigned char*>(command);
    using Formula=int(__cdecl*)(const void*,const void*,const void*,int,int,unsigned,unsigned,int,int*,int*,int);
    using Immunity=int(__cdecl*)(const void*,unsigned,unsigned*,unsigned,unsigned*,int);
    int defense=0,magicDefense=0;
    int value=reinterpret_cast<Formula>(coreImage+0x389CB0)(source,target,row,row[0x28],row[0x2A],0,
                                                           gravityChannel,0,&defense,&magicDefense,0);
    gravityFlags=gravityChannel;gravityBlocked=0;
    value=reinterpret_cast<Immunity>(coreImage+0x38AE40)(target,row[0x28],&gravityFlags,gravityChannel,&gravityBlocked,value);
    using Affinity=int(__cdecl*)(const unsigned char*,const unsigned char*,unsigned,int);
    value=reinterpret_cast<Affinity>(coreImage+0x38A420)(static_cast<const unsigned char*>(target),row,row[0x2D],value);
    value=E::BoundedDamage(std::int64_t(value)*gravityAmplifier).damage;
    const auto pass=gravityChannel==1?3u:gravityChannel==2?2u:1u;
    return static_cast<unsigned>(B::UpperDamage(value,gravityNativeCap,user,id,pass,false));
}
static void GravityCases(std::uintptr_t base,std::vector<unsigned char>& actors,
                         std::vector<unsigned char>& bank,bool overrideImmunity){
    coreImage=base;W::DamageProducerForTests(reinterpret_cast<void*>(&GravityEndpoint));
    auto* target=actors.data()+18*0xF90;
    for(unsigned slot:{18u,19u}){auto* actor=actors.data()+slot*0xF90;
        W32(actor+0x594,160000);W32(actor+0x5D0,40000);W32(actor+0x6E4,40000);
        W32(actor+0x598,2000);W32(actor+0x5D4,400);W32(actor+0x6E8,400);
    }
    std::array<unsigned char,44> info{};
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+0x38E680);
    const auto hit=[&](unsigned id,unsigned slot=18){return static_cast<int>(producer(0,actors.data(),slot,
        actors.data()+slot*0xF90,bank.data()+20+96*id,0x3000+id,info.data(),0,0,0,0));};
    Check(hit(110)==10000,"a bound boss uses the native maximum-HP formula with an exact one-sixteenth base");
    W32(target+0x5D0,8000);W32(target+0x6E4,8000);
    Check(hit(110)==7999,"final nonlethal clipping follows the current HP after the maximum-HP base");
    W32(target+0x5D0,40000);W32(target+0x6E4,40000);
    Check(hit(111)==10000,"explicit Fury receives the same Gravity contract without the ordinary magic category");
    W32(target+0x5D0,80000);W32(target+0x6E4,80000);
    Check(hit(112)==20000,"an unbound command retains native current-HP percentage behavior");
    Check(hit(110,19)==10000,"an unlisted enemy retains its original native formula");
    gravityAmplifier=20;
    Check(hit(110)==79999,"later positive amplifiers cannot turn a bound Gravity hit lethal");gravityAmplifier=1;
    gravityNativeCap=9999;
    Check(hit(110)==9999,"the nonlethal policy does not grant equipment BDL");gravityNativeCap=99999;
    W32(target+0x5D0,1);W32(target+0x6E4,1);
    Check(hit(110)==0,"one remaining HP is preserved");
    W32(target+0x5D0,40000);W32(target+0x6E4,101);
    Check(hit(110)==100,"precomputed earlier hits reserve HP through the native scratch balance");
    W32(target+0x6E4,40000);target[0x5B8]|=2;
    Check(hit(110)==(overrideImmunity?10000:0)&&gravityFlags==(overrideImmunity?1u:0u)&&gravityBlocked==(overrideImmunity?0u:1u),
          "only the explicit profile can override native Gravity immunity and its result counters");
    Check(hit(112)==0&&gravityFlags==0&&gravityBlocked==1,"unbound commands retain immunity even in an override-enabled profile");
    target[0x5B8]&=static_cast<unsigned char>(~2u);
    gravityChannel=2;
    Check(hit(113)==100,"MP-only commands retain their native current-MP fraction");gravityChannel=1;
    const auto original=bank;
    bank[20+96*110+0x2A]=8;
    Check(hit(110)==20000,"changed command data cannot borrow the admitted Gravity binding");bank=original;
    E::RequestStop();W32(target+0x5D0,80000);W32(target+0x6E4,80000);
    Check(hit(110)==20000,"stop restores the unmodified native current-HP formula");
}
