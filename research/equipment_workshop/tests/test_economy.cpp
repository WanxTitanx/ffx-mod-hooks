#include "workshop.h"
#include "customize_recipes.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace workshop;
static unsigned checks=0,failures=0;
static Economy Funding(){Economy e{};e.gil=1000000000;e.customizeUnlocked=1;return e;}
static Error Quote(const State& s,const Request& r,Plan& p,Economy e=Funding()){e.customizeUnlocked=1;return Preview(s,r,p,e);}

static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static State Make(){
    std::array<unsigned char,GearCount*NativeBytes> records{};
    std::array<std::uint16_t,ItemCount> items{};items.fill(255);
    const unsigned words[]={0x8000,0x8064,0x8055,0x8075};
    for(unsigned slot=0;slot<2;++slot){auto* p=records.data()+slot*22;p[2]=1;p[6]=255;p[11]=4;
        for(unsigned i=0;i<4;++i){p[14+2*i]=static_cast<unsigned char>(words[i]);p[15+2*i]=static_cast<unsigned char>(words[i]>>8);}}
    State s{};Check(Import(records.data(),items.data(),12345,s)==Error::Ok,"import economy fixture");
    s.pieces[0].mode=s.pieces[1].mode=2;return s;
}
static Request RequestFor(const State& s,Op op){Request r{};r.op=op;r.revision=s.revision;r.pieceId=s.pieces[0].id;return r;}
static void SetWord(Piece& p,unsigned slot,unsigned word){p.native[14+2*slot]=static_cast<unsigned char>(word);p.native[15+2*slot]=static_cast<unsigned char>(word>>8);}
static void CatalogAndRanks(){
    Policy p{};unsigned nativeCount=0,modCount=0;
    for(unsigned id=0;id<131;++id){
        unsigned item=0,quantity=0;bool native=false;
        Check(CustomizeCost(static_cast<std::uint16_t>(0x8000+id),p,item,quantity,native),"every admitted ability has an explicit economy recipe");
        if(native)++nativeCount;else ++modCount;
        Check(native?(item==CustomizeRecipes[id].item&&quantity==CustomizeRecipes[id].quantity):(item==73&&quantity==30),"native recipe or clearly separate mod-only fallback");
        unsigned last=0;
        for(unsigned rank=1;rank<=10;++rank){unsigned actualItem=0,amount=0;
            Check(RefinementCost(static_cast<std::uint16_t>(0x8000+id),rank,p,actualItem,amount)&&actualItem==item&&amount==(quantity*rank+59)/60&&amount>=last,"the reduced rank price rounds once and keeps the same Customize material");last=amount;}
    }
    Check(nativeCount==125&&modCount==6,"125 native recipes and six explicitly non-native entries");
    const unsigned ids[]={0,1,19,85,86,98,100,117,128};
    const unsigned items[]={73,96,108,57,55,70,77,67,53},amounts[]={2,1,50,70,80,3,1,1,99};
    for(unsigned i=0;i<9;++i){unsigned item=0,qty=0;bool native=false;
        Check(CustomizeCost(static_cast<std::uint16_t>(0x8000+ids[i]),p,item,qty,native)&&native&&item==items[i]&&qty==amounts[i],"native Customize golden recipe");}
    for(unsigned bad:{0u,255u,0x8083u,0xFFFFu}){unsigned i=0,q=0;bool n=false;Check(!CustomizeCost(static_cast<std::uint16_t>(bad),p,i,q,n),"unknown or empty ability has no manufactured recipe");}
    for(unsigned field=0;field<7;++field){auto bad=p;
        switch(field){case 0:bad.mode=3;break;case 1:bad.baseItem=69;break;case 2:bad.baseAmount=0;break;case 3:bad.refinementDivisor=0;break;case 4:bad.fusionDivisor=0;break;case 5:bad.fusionGilPerAbility=0;break;default:bad.modRecipeQuantity=0;}
        auto s=Make();auto r=RequestFor(s,Op::Refine);Plan plan{};Economy e=Funding();e.policy=bad;
        Check(!ValidPolicy(bad)&&Quote(s,r,plan,e)==Error::InvalidPolicy&&std::memcmp(&s,&plan.after,sizeof(s))==0,"invalid policy rejects without altering saved ranks");
    }
}
static void AlternativeRequirements(){
    auto s=Make();auto& piece=s.pieces[0];
    SetWord(piece,0,0x8062);SetWord(piece,2,0x806A);
    piece.ranks[0]=4;piece.ranks[1]=10;piece.ranks[2]=2;piece.ranks[3]=10;
    s.items[70]=6;s.items[77]=s.items[67]=0;
    Economy pricing=Funding();pricing.policy.refinementDivisor=3;
    auto r=RequestFor(s,Op::Refine);Plan p{};
    Check(Quote(s,r,p,pricing)==Error::Ok&&p.requirements[70]==6,"same-item alternatives require max(5,3)+base1, not sum");
    Check(p.requirements[77]==0&&p.requirements[67]==0,"maxed abilities require no ingredients and leave roulette");
    auto poor=s;poor.items[70]=5;
    Check(Quote(poor,r,p,pricing)==Error::Materials&&p.requirements[70]==6&&!p.chosenAbility&&std::memcmp(&p.after,&poor,sizeof(poor))==0,"overlapping base and specific material cannot underflow or bias the pool");
    unsigned selected[2]={};
    for(unsigned seed=0;seed<256;++seed){s.rng=seed;Plan a{},b{};
        Check(Quote(s,r,a,pricing)==Error::Ok,"sufficient prerequisites admit every roulette seed");
        const bool first=a.chosenAbility==piece.abilities[0];++selected[first?0:1];
        Check((first||a.chosenAbility==piece.abilities[2])&&a.costs[70]==(first?6:4)&&a.after.items[70]==(first?0:2),"only winner plus base is charged, including same-item overlap");
        auto rich=s;rich.items[70]=255;
        Check(Quote(rich,r,b,pricing)==Error::Ok&&a.chosenAbility==b.chosenAbility,"extra inventory never changes candidate probabilities or result");
        Check(a.after.rolls==s.rolls+1&&std::memcmp(&s,&b.after,sizeof(s))!=0,"one paid draw advances the generator once");
    }
    Check(selected[0]>0&&selected[1]>0,"both eligible instances remain reachable");
    Economy e=pricing;e.policy.mode=1;s.items[70]=10;
    Check(Quote(s,r,p,e)==Error::Ok&&p.costs[70]==10&&p.requirements[70]==10&&p.after.pieces[0].ranks[0]==5&&p.after.pieces[0].ranks[2]==3&&p.after.rolls==s.rolls,"whole-equipment mode sums simultaneous costs and does not roll RNG");
}
static void PrepareArmorFusion(State& s){
    s.pieces[0].native[5]=s.pieces[1].native[5]=1;
    SetWord(s.pieces[0],0,0x8009);SetWord(s.pieces[0],2,0x8054);SetWord(s.pieces[1],0,0x8008);
}
static void MigrationAndFusion(){
    auto s=Make();auto& target=s.pieces[0];target.mode=1;target.rank=6;
    auto r=RequestFor(s,Op::Refine);Plan plan{};
    const auto before=s;
    Check(Quote(s,r,plan)==Error::Ok&&std::memcmp(&s,&before,sizeof(s))==0,"legacy A read and preview do not mutate the sidecar");
    unsigned total=0;for(unsigned i=0;i<4;++i){const auto rank=AbilityRank(plan.after.pieces[0],i);total+=rank;Check(rank==6||rank==7,"legacy A expands without loss or free catch-up");}
    Check(total==25&&plan.after.pieces[0].mode==2&&sizeof(State)==16260,"paid B migrates A ranks inside unchanged v1 layout");
    s=plan.after;r=RequestFor(s,Op::Refine);Economy e=Funding();e.policy.mode=1;
    Check(Quote(s,r,plan,e)==Error::Ok,"A can operate on uneven B ranks");
    for(unsigned i=0;i<4;++i)Check(AbilityRank(plan.after.pieces[0],i)==AbilityRank(s.pieces[0],i)+1,"A advances each rank once, not to the highest existing rank");
    s=Make();PrepareArmorFusion(s);s.pieces[1].ranks[2]=8;
    r=RequestFor(s,Op::Fuse);r.other=1;r.otherId=s.pieces[1].id;r.count=1;r.from[0]=2;r.to[0]=3;
    e={};e.gil=9999;
    Check(Quote(s,r,plan,e)==Error::Gil&&plan.gilCost==10000&&std::memcmp(&plan.after,&s,sizeof(s))==0,"insufficient Gil preserves donor, materials, target and RNG");
    e.gil=10000;
    Check(Quote(s,r,plan,e)==Error::Ok&&plan.costs[57]==24&&plan.gilCost==10000&&plan.gilBefore==10000,"fusion charges ceil(70/3) Light Curtains and explicit Gil");
    Check(!plan.after.pieces[1].id&&!plan.after.pieces[1].native[2]&&AbilityRank(plan.after.pieces[0],3)==8,"priced fusion consumes donor and moves the developed ability rank");
    r.count=2;r.from[1]=0;r.to[1]=1;e.gil=20000;
    Check(Quote(s,r,plan,e)==Error::Ok&&plan.gilCost==20000&&plan.costs[64]==2&&plan.costs[57]==24,"two transfers aggregate both rounded recipes and both Gil fees");
    auto paid=plan.after;r.revision=paid.revision;
    Check(Quote(paid,r,plan,e)==Error::Stale&&std::memcmp(&paid,&plan.after,sizeof(paid))==0,"consumed donor cannot be charged or fused again");
    s=Make();PrepareArmorFusion(s);r=RequestFor(s,Op::Fuse);r.other=1;r.otherId=s.pieces[1].id;r.count=1;r.from[0]=2;e.policy.fusionGilPerAbility=100000000;e.gil=0xFFFFFFFF;
    Check(Quote(s,r,plan,e)==Error::Ok&&plan.gilCost==100000000,"large allowed Gil cost is computed without signed overflow");
    unsigned char buffer[32];std::memset(buffer,0xA5,sizeof(buffer));
    Check(ws_plan(&s,&r,reinterpret_cast<Plan*>(buffer))!=0&&buffer[0]==0xA5&&buffer[31]==0xA5&&ws_plan_abi()==5,"legacy C ABI never writes a larger plan into an old host buffer");
    Check(ws_plan_economy_v4(&s,&r,&e,reinterpret_cast<Plan*>(buffer))!=0&&buffer[0]==0xA5&&buffer[31]==0xA5,
          "ABI4 refuses to write the larger catalogue-bearing ABI5 plan into a legacy buffer");
}
int main(){
    CatalogAndRanks();AlternativeRequirements();MigrationAndFusion();
    auto state=Make();auto request=RequestFor(state,Op::Refine);Plan plan{};
    auto poor=state;poor.items[57]=0;const auto before=poor;
    Check(Quote(poor,request,plan)==Error::Materials,"roulette requires the Auto-Protect material even if another ability would win");
    Check(std::memcmp(&poor,&before,sizeof(poor))==0,"rejected roulette changes no inventory or RNG");
    poor=state;poor.items[70]=0;
    Check(Quote(poor,request,plan)==Error::Materials,"roulette needs its base Power Sphere");
    Check(Quote(state,request,plan)==Error::Ok&&plan.costs[70]==1,"one base Power Sphere is charged for accepted roulette");
    unsigned winner=5;for(unsigned i=0;i<4;++i)if(plan.after.pieces[0].ranks[i]!=state.pieces[0].ranks[i])winner=i;
    const unsigned materials[]={73,77,57,67},costs[]={1,1,2,1};
    Check(winner<4&&plan.costs[materials[winner]]==costs[winner],"only the winning ability pays its next-rank Customize ingredient");
    if(winner<4)for(unsigned i=0;i<4;++i)if(i!=winner)Check(plan.costs[materials[i]]==0,"unselected outcome ingredients are prerequisites, not charges");
    std::printf("WORKSHOP_ECONOMY %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
