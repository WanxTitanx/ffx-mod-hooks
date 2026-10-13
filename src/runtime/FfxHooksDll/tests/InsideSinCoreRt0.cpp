// Jarvis-HOOK: approved Inside Sin policy, independent of the game process.
#include <cstdio>
#if !__has_include("../hooks/InsideSinCore.h")
int main(){std::puts("FAIL: Inside Sin seven-preset policy is not implemented");return 1;}
#else
#include "../hooks/InsideSinCore.h"
#if __has_include("../hooks/InsideSinConfig.h")
#include "../hooks/InsideSinConfig.h"
#endif
using namespace FfxHooks::InsideSin;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",why);}}
int main(){
    unsigned counts[8]{};
    for(unsigned ticket=0;ticket<100;++ticket)++counts[static_cast<unsigned>(Choose(ticket))];
    for(unsigned i=1;i<=7;++i)Check(counts[i]==(i<=5?18u:5u),"individual weights are 18/18/18/18/18/5/5");
    Check(Choose(100)==Preset::None,"out-of-domain tickets cannot select a preset");
    Check(EligibleField(1073,0)&&EligibleField(1073,4)&&EligibleField(1074,0)&&EligibleField(1074,7)&&EligibleField(1075,7),"all twenty-one natural formations are supported");
    for(unsigned field:{0u,515u,583u,586u,600u,2097u})Check(!EligibleField(field,0),"outside/aliased/story/Omega fields are rejected");
    Check(!EligibleField(1073,5)&&!EligibleField(1074,8),"unreviewed formation suffixes are rejected");
    for(unsigned distribution:{0u,20u,50u,80u,100u})for(unsigned seed=0;seed<300;++seed){
        const auto selected=Infections(seed,3,distribution);
        const unsigned count=Count(selected);
        const unsigned expected=distribution?FfxHooks::SinSpread::Quota(Roster.size(),static_cast<FfxHooks::SinSpread::Distribution>(distribution)):count;
        Check(count==expected&&count<=(distribution==100?Roster.size():Roster.size()*80/100),"infection quota is unchanged");
    }
    Check(Infections(1,2,13)==0,"invalid distribution is inert");
    Check(ModelAllowed(1073,220)&&!ModelAllowed(1073,95)&&ModelAllowed(1074,95),"shared species obey exact regional membership");
    Check(!ModelAllowed(1073,38)&&!ModelAllowed(1074,38)&&!ModelAllowed(1075,38)&&ModelAllowed(1075,47)&&ModelAllowed(1075,188),"sins05 admits only its actual native roster");
    bool seen[8]{};for(unsigned seed=0;seed<1000;++seed)seen[static_cast<unsigned>(ChooseForActor(seed,1,1,220,20))]=true;
    for(unsigned i=1;i<=7;++i)Check(seen[i],"one species can receive every regional preset");

    Ledger ledger;
    const Identity a{1,1,0x10000,0x20000,38},b{1,1,0x12000,0x21000,38};
    Check(ledger.Bind(20,a,Preset::Ascendant)&&ledger.Bind(21,b,Preset::Ascendant),"two identical species receive independent instances");
    Vitals v{501,1000,0,false};
    Check(!ledger.ReceivedAction(20,a,v).triggered,"Ascendant waits above half HP");
    v.hp=500;v.buffs=Shell;
    auto reaction=ledger.ReceivedAction(20,a,v);
    Check(reaction.triggered&&reaction.buffs==Haste&&!reaction.heal,"Ascendant grants only missing buffs at exact half HP");
    v.buffs=0;Check(!ledger.ReceivedAction(20,a,v).triggered,"Dispel cannot rearm the consumed latch");
    Check(ledger.ReceivedAction(21,b,v).triggered,"another actor does not inherit the latch");
    Identity reused=a;reused.incarnation=2;
    Check(!ledger.ReceivedAction(20,reused,v).triggered,"stale slot identity cannot fire");
    Check(ledger.Bind(20,reused,Preset::Ascendant)&&ledger.ReceivedAction(20,reused,v).triggered,"reused slot starts fresh");
    ledger.Reset();Check(!ledger.ReceivedAction(20,reused,v).triggered,"battle reset retires prior identities");
    Check(ledger.Bind(20,a,Preset::Sentinel),"Sentinel binds");
    v={800,1000,0,false};
    for(unsigned i=0;i<5;++i)Check(!ledger.Hit(20,a,900,v,true).triggered,"separate small hits do not accumulate");
    v.hp=650;reaction=ledger.Hit(20,a,900,v,true);
    Check(reaction.triggered&&reaction.heal==200&&reaction.buffs==Haste,"one exact quarter hit heals twenty percent and grants missing Haste");
    Check(!ledger.Hit(20,a,900,v,true).triggered,"Sentinel only triggers once");
    ledger.Reset();ledger.Bind(20,a,Preset::Sentinel);v.hp=0;
    Check(!ledger.Hit(20,a,900,v,true).triggered,"lethal hit cannot trigger Sentinel");
    v={600,1001,Haste,false};reaction=ledger.Hit(20,a,851,v,true);
    Check(reaction.triggered&&reaction.heal==200&&!reaction.buffs,"fractional thresholds and existing Haste are handled exactly");
    Check(HealingAmount(200,false)==-200&&HealingAmount(200,true)==200,"native HP request keeps Zombie sign semantics");
    Identity geminiA=a,geminiB=b;geminiA.model=182;geminiB.model=184;
    GeminiLatch gemini;
    Check(!gemini.Bind(geminiA,a)&&gemini.Bind(geminiA,geminiB),"Gemini trait requires the actual opposite species");
    Check(!gemini.Death(a,true,v).triggered,"death of a different ally cannot trigger Gemini Haste");
    Check(!gemini.Death(geminiB,false,v).triggered,"a living Gemini partner cannot trigger");
    v={300,1000,0,false};reaction=gemini.Death(geminiB,true,v);
    Check(reaction.triggered&&reaction.buffs==Haste&&!reaction.heal,"partner death grants one missing Haste even with other allies");
    Check(!gemini.Death(geminiB,true,v).triggered,"Dispel does not rearm Gemini Haste");
    auto replacement=geminiB;++replacement.incarnation;gemini.Bind(geminiA,replacement);
    Check(!gemini.Death(replacement,true,v).triggered,"partner slot reuse does not rearm an already consumed survivor");
    gemini.Reset();gemini.Bind(geminiA,geminiB);v.buffs=Haste;
    reaction=gemini.Death(geminiB,true,v);
    Check(reaction.triggered&&!reaction.buffs,"existing Haste is not renewed");
    gemini.Reset();gemini.Bind(geminiA,geminiB);v.hp=0;
    Check(!gemini.Death(geminiB,true,v).triggered,"dead Gemini cannot receive Haste");
    Check(Extra(Preset::Guardian,Shell|Protect|Regen,0,0).preset==Preset::None,"saturated Guardian skips without offensive fallback");
    Check(Extra(Preset::Sentinel,Shell|Protect|Reflect|Regen,0,0).preset==Preset::None,"saturated Sentinel skips");
    auto extra=Extra(Preset::Guardian,Shell,0,0);
    Check(extra.buffs==(Protect|Regen)&&extra.self,"Guardian applies only missing defense");
    extra=Extra(Preset::Devastator,0,0x25,1);
    Check(extra.targets==0x25&&!extra.self&&!extra.buffs,"Devastator targets each living party actor once");
    extra=Extra(Preset::Oppressor,0,0x25,1);
    Check(extra.targets==4&&!extra.self,"random single target is selected only among living actors");
    Check(Extra(Preset::Ascendant,0,0,0).preset==Preset::None,"empty party produces no attack");
    Check(EligibleAction(true,true,false,false,false)&&!EligibleAction(true,true,true,false,false)&&
          !EligibleAction(true,true,false,true,false)&&!EligibleAction(true,true,false,false,true)&&
          !EligibleAction(false,true,false,false,false),"only completed living native actions produce one extra");
#if !__has_include("../hooks/InsideSinConfig.h")
    Check(false,"unresolved command bindings must gate the whole regional pool");
#else
    Bindings bindings{};
    Check(!bindings.Ready(),"empty bindings cannot activate any preset");
    for(unsigned i=0;i<Definitions.size();++i){
        const auto& d=Definitions[i];auto& binding=bindings.commands[i];
        binding.id=static_cast<std::uint16_t>(0x6000+i); // synthetic fixture IDs only
        auto& row=binding.row;row[16]=1;row[18]=1;row[0x19]=255;row[0x1A]=d.self?0xB1:d.all?0x37:0x33;
        row[0x1C]=6;row[0x20]=d.physical?1:2;row[0x23]=1;row[0x28]=d.physical?1:3;
        row[0x2A]=static_cast<std::uint8_t>(d.power);row[0x2B]=1;row[0x2D]=static_cast<std::uint8_t>(d.element);
        if(d.preset==Preset::Oppressor){row[0x2Eu+24]=75;row[0x47+12]=3;}
        if(d.preset==Preset::Tormentor){row[0x2Eu+3]=75;row[0x2Eu+13]=50;row[0x47+1]=3;}
        if(d.preset==Preset::Breaker)row[0x2Eu+6]=75;
        for(unsigned n=0;n<5;++n)if(d.buffs&(1u<<n)){row[0x2Eu+ChanceIndices[n]]=255;row[0x47+DurationIndices[n]]=254;}
        Check(Semantics(d.preset,row),"native row implements its exact preset contract");
        auto invalid=row;invalid[0x2B]=2;Check(!Semantics(d.preset,invalid),"multi-impact substitute is rejected");
        invalid=row;invalid[0x25]=1;Check(!Semantics(d.preset,invalid),"extra cannot consume native MP");
        if(!d.self){invalid=row;invalid[0x20]|=0x10;Check(!Semantics(d.preset,invalid),"offense cannot heal");
            invalid=row;invalid[0x2Eu+16]=255;Check(!Semantics(d.preset,invalid),"offense cannot grant Protect");}
        else {invalid=row;invalid[0x2A]=1;Check(!Semantics(d.preset,invalid),"defense cannot damage");
            invalid=row;invalid[0x47+3]=255;Check(!Semantics(d.preset,invalid),"permanent Shell is rejected");}
    }
    Check(bindings.Ready(),"complete seven-command set is admitted atomically");
    auto absent=bindings;absent.commands[0].id=0;Check(!absent.Ready(),"one missing reference disables the whole pool without changing weights");
    absent=bindings;absent.commands[1].id=absent.commands[0].id;Check(!absent.Ready(),"duplicate commands are rejected");
    auto row=bindings.commands[2].row;FilterExistingBuffs(row,Shell|Regen);
    Check(row[0x2Eu+15]==0&&row[0x2Eu+22]==0&&row[0x2Eu+16]==255,"defensive command shadow contains only missing buffs");
    auto translated=bindings.commands[0].row;translated[0]=42;
    Check(bindings.Matches(Preset::Devastator,translated),"text offsets may change without invalidating gameplay binding");
    translated[0x2A]=25;Check(!bindings.Matches(Preset::Devastator,translated),"changed gameplay bytes close admission");
    Bindings parsed{};bool enabled=true;
    Check(ParseBindings("{\"schema\":\"ffx.inside-sin.v1\",\"enabled\":false,\"commands\":[]}",parsed,enabled)==BindingCode::Disabled&&!enabled,"disabled configuration is inert");
    Check(ParseBindings("{\"schema\":\"ffx.inside-sin.v1\",\"enabled\":true,\"commands\":[]}",parsed,enabled)==BindingCode::Unresolved&&!parsed.Ready(),"missing references cannot activate");
#endif
    std::printf("Inside Sin core: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
#endif
