#include "../hooks/ArcanaCore.h"
#include <cstdio>
#include <cstring>
#include <limits>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
static bool Equal(const State& a,const State& b){return a.revision==b.revision&&a.mode==b.mode&&a.acquired==b.acquired&&a.slots==b.slots;}
static State Deck(Mode mode){State s;s.mode=mode;s.acquired.fill(1);return s;}
static Effects One(EffectKind kind,int value){Effects e;e.values[static_cast<unsigned>(kind)]=value;return e;}
int main(){
    State s;
    Check(Validate(s)==Error::None,"empty unacquired deck is a valid new session");
    Check(Equip(s,0,0,0,0)==Error::NotAcquired,"unacquired card cannot be equipped");
    Check(Award(s,0)==Error::None&&s.acquired[0]&&s.revision==1,"award adds one unique card");
    const auto acquired=s;
    Check(Award(s,0)==Error::None&&Equal(s,acquired),"award retry does not create a copy or revision");
    Check(Equip(s,1,0,0,0)==Error::None&&Owner(s,0)==0,"equipping binds a card to its actor");
    const auto before=s;
    Check(Equip(s,s.revision,1,0,0)==Error::TransferRequired&&Equal(s,before),"cross-owner preview needs explicit confirmation");
    Check(Equip(s,s.revision,1,0,0,true)==Error::None&&s.slots[0][0]==kEmpty&&s.slots[1][0]==0,"confirmed transfer moves rather than copies");
    Check(Equip(s,0,1,0,kEmpty)==Error::Stale,"old preview cannot unequip a newer state");
    auto full=Deck(Mode::Twin);
    Check(Equip(full,0,0,0,0)==Error::None&&Equip(full,1,0,1,21)==Error::None,"mode A allows two majors");
    Check(Equip(full,2,0,2,22)==Error::InvalidSlot,"mode A never exposes a third slot");
    auto b=Deck(Mode::Constellation);
    Check(Equip(b,0,0,0,0)==Error::None&&Equip(b,1,0,1,22)==Error::None&&Equip(b,2,0,2,36)==Error::None,"mode B allows one major and two minors");
    const auto bBefore=b;
    Check(Equip(b,b.revision,0,1,1)==Error::Capacity&&Equal(b,bBefore),"two majors plus a minor reject atomically");
    Check(ChangeMode(b,b.revision,Mode::Twin)==Error::ResolutionRequired&&Equal(b,bBefore),"B to A does not silently discard a third card");
    auto resolved=b.slots;resolved[0][2]=kEmpty;
    Check(ChangeMode(b,b.revision,Mode::Twin,&resolved)==Error::None&&b.slots[0][2]==kEmpty&&b.acquired[36],"explicit resolution releases but never destroys the third card");
    auto corrupt=Deck(Mode::Twin);corrupt.slots[0][0]=0;corrupt.slots[1][0]=0;
    Check(Validate(corrupt)!=Error::None,"one card cannot have two owners");
    full=Deck(Mode::Twin);full.revision=std::numeric_limits<std::uint64_t>::max();
    Check(Equip(full,full.revision,0,0,0)==Error::RevisionExhausted,"revision overflow cannot revive stale previews");
    Check(!FindCard(78)&&!FindCard(9999),"out of range catalog lookup is rejected");
    unsigned majors=0;
    for(unsigned id=0;id<78;++id){const auto* c=FindCard(id);Check(c&&c->id==id&&c->key&&c->name&&c->asset,"all 78 definitions have stable identity");if(c)majors+=c->major?1:0;}
    Check(majors==22,"catalog preserves 22 major arcana");
    const auto* strength=FindCard(8);Check(strength&&std::strstr(strength->name,"VIII - Strength - Ifrit"),"printed Tarot rank is separate from internal ID");
    const auto* page=FindCard(32);Check(page&&std::strncmp(page->name,"Page of Wands",13)==0,"court names have no invented number");
    auto emperor=Deck(Mode::Twin);emperor.slots[0][0]=4;emperor.slots[0][1]=49;
    const auto effects=Aggregate(emperor,0);
    Check(effects.Get(EffectKind::AutoProtect)==1&&effects.Get(EffectKind::AutoShell)==1,"boolean effects combine once across cards");
    Check(effects.Get(EffectKind::DefensePercent)==20,"native flags do not replace the card's numeric benefit");
    Check(BoostStat(1000,40,0,9999)==1400,"HP percentage applies once to native base");
    Check(BoostStat(250,25,20,255)==255,"ordinary stats clamp after all modifiers");
    Check(BoostStat(0,-200,-20,99999)==0,"negative arithmetic cannot underflow");
    DamageContext d;d.amount=9000;auto devil=One(EffectKind::OutgoingDamage,25);Effects none;
    Check(Damage(d,devil,none)==9999,"damage increases still respect the final shared cap");
    d.amount=20000;auto ward=One(EffectKind::IncomingDamage,-20);
    Check(Damage(d,none,ward)==9999,"incoming reduction precedes cap, not after an already capped result");
    d.amount=1000;d.fixed=true;Check(Damage(d,devil,ward)==1000,"fixed damage ignores generic dealt/received modifiers");
    d.fixed=false;d.elements=3;auto elements=One(EffectKind::ElementDamageFireIce,10);
    Check(Damage(d,elements,none)==1100,"matching two elements applies the card multiplier only once");
    d.healing=true;Check(Damage(d,devil,ward)==1000,"damage modifiers never turn healing into bonus damage");
    auto mp=One(EffectKind::HalfBlackMp,1);
    Check(MpCost(30,mp,true,false,false,false)==15&&MpCost(30,mp,false,true,false,false)==30,"conditional MP reduction respects command family");
    Check(MpCost(31,mp,true,false,false,false)==16,"fractional MP costs round up like native Half MP Cost");
    auto combined=mp;combined.values[static_cast<unsigned>(EffectKind::MpReduction)]=25;
    Check(MpCost(20,combined,true,false,false,false)==8&&MpCost(5,combined,true,false,false,false)==3,"card Half MP uses native rounding before one percentage-reduction layer");
    Check(MpCost(30,mp,true,false,true,true)==0&&MpCost(30,mp,true,false,false,true)==1,"native free/one-MP rules stay authoritative");
    auto quick=One(EffectKind::FirstCtbReduction,25);
    Check(CtbDelay(100,quick,true)==75&&CtbDelay(100,quick,false)==100,"first-action reduction is consumed by event ownership");
    auto worldState=Deck(Mode::Twin);Equip(worldState,0,0,0,21);const auto world=Aggregate(worldState,0);
    Check(MpCost(99,world,true,false,false,false)==99&&MpCost(99,world,true,false,false,true)==1,"World preserves native MP cost and native One MP");
    DamageContext ultimate;ultimate.amount=1000;ultimate.overdrive=true;
    Check(Damage(ultimate,world,none)==1500,"World strengthens eligible Overdrive HP damage by fifty percent");
    ultimate.overdrive=false;Check(Damage(ultimate,world,none)==1000,"World does not turn ordinary attacks into Overdrives");
    ultimate.overdrive=true;ultimate.fixed=true;Check(Damage(ultimate,world,none)==1000,"World preserves fixed Overdrive damage");
    ultimate.fixed=false;ultimate.fractional=true;Check(Damage(ultimate,world,none)==1000,"World preserves fractional Overdrive damage");
    Check(CtbDelay(100,world,false)==85&&CtbDelay(0,world,false)==0,"World reduces committed recovery while preserving native zero delay");
    DamageContext mortal;mortal.amount=1000;mortal.deathImmune=true;
    auto death=One(EffectKind::DeathImmuneDamage,20);
    Check(Damage(mortal,death,none)==1200,"Death remains useful against explicitly Death-immune targets");
    mortal.deathImmune=false;Check(Damage(mortal,death,none)==1000,"Death's extra damage does not affect ordinary targets");
    mortal.deathImmune=true;mortal.fixed=true;Check(Damage(mortal,death,none)==1000,"anti-immunity bonus preserves fixed damage semantics");
    mortal.fixed=false;mortal.healing=true;Check(Damage(mortal,death,none)==1000,"anti-immunity bonus never amplifies healing");
    mortal.healing=false;mortal.elements=12;
    Check(Damage(mortal,One(EffectKind::ElementDamageLightningWater,15),none)==1150,"combined Lightning/Water affiliation applies its matching bonus once");
    auto drop=One(EffectKind::DropMultiplier,2);
    Check(RewardRate(300,drop,EffectKind::DropMultiplier)==300,"Double Drop cannot multiply Triple Drop again");
    State development;
    Check(AwardAll(development,0)==Error::None&&development.revision==1,"explicit development grant fills the deck in one transaction");
    bool complete=true;for(auto owned:development.acquired)if(owned!=1)complete=false;
    Check(complete&&Owner(development,0)==-1,"development grant owns every card without auto-equipping any");
    const auto granted=development;
    Check(AwardAll(development,development.revision)==Error::None&&Equal(development,granted),"development grant is idempotent");
    Check(AwardAll(development,0)==Error::Stale,"old development command is rejected");
    auto triple=Deck(Mode::Constellation);
    for(int i=0;i<76;++i)for(int j=i+1;j<77;++j)for(int k=j+1;k<78;++k){
        triple.slots[0]={{static_cast<std::int16_t>(i),static_cast<std::int16_t>(j),static_cast<std::int16_t>(k)}};
        const bool permitted=(i<22?1:0)+(j<22?1:0)+(k<22?1:0)<=1;
        Check((Validate(triple)==Error::None)==permitted,"every three-card combination follows the declared B rule");
    }
    std::printf("ArcanaCoreRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
