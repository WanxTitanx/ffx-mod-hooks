#include "workshop.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace workshop;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* t){++checks;if(!ok){++failures;std::printf("FAIL %s\n",t);}}
static Economy Rich(){Economy e{};e.gil=1000000000;e.customizeUnlocked=1;return e;}
static State Make(unsigned owner=0,bool occupied=false){
 std::array<unsigned char,4400> gear{};std::array<std::uint16_t,112> items{};items.fill(255);
 for(unsigned j=0;j<2;++j){auto* n=gear.data()+22*j;n[2]=1;n[4]=static_cast<unsigned char>(owner);n[6]=255;n[11]=4;
  for(unsigned i=0;i<4;++i){unsigned w=occupied||j?0x8062:255;n[14+2*i]=static_cast<unsigned char>(w);n[15+2*i]=static_cast<unsigned char>(w>>8);}}
 State s{};Check(Import(gear.data(),items.data(),12345,s)==Error::Ok,"fixture imports");return s;
}
static Request Req(const State& s,Op op){Request r{};r.op=op;r.revision=s.revision;r.pieceId=s.pieces[0].id;return r;}
static void DeveloperCases();
int main(){
 const unsigned items[]={77,78,75,76,77,79,76};Plan p{};
 for(unsigned owner=0;owner<7;++owner){auto s=Make(owner);s.items[items[owner]]=7;s.items[80]=3;
  Check(Preview(s,Req(s,Op::UnlockFifth),p,Rich())==Error::Ok&&p.costs[items[owner]]==7&&p.costs[80]==3&&p.costs[84]==0,"unlock consumes seven owner spheres and three Master spheres");
  s.items[80]=2;Check(Preview(s,Req(s,Op::UnlockFifth),p,Rich())==Error::Materials&&std::memcmp(&p.after,&s,sizeof(s))==0,"nine spheres cannot unlock or consume partially");}
 auto s=Make(0,true);Check(Preview(s,Req(s,Op::UnlockFifth),p,Rich())==Error::Ok,"four occupied native slots permit fifth-slot unlock");
 s=Make(0,true);for(unsigned i=0;i<4;++i)s.pieces[0].native[14+2*i]=99;s.pieces[0].fifthUnlocked=1;auto r=Req(s,Op::SetFifth);r.value=0x8062;
 Check(Preview(s,r,p,Rich())==Error::Ok&&p.costs[70]==5&&p.gilCost==200000,"fifth charges ceil(3 times Q / 2) and 200000 Gil");
 r=Req(s,Op::Reforge);std::memcpy(r.gearTemplate,s.pieces[0].native,22);r.gearTemplate[4]=1;
 Check(Preview(s,r,p,Rich())==Error::Protected,"unlocked fifth binds equipment owner even while empty");
 s=Make(0,true);s.pieces[0].fifthUnlocked=1;r=Req(s,Op::Fuse);r.other=1;r.otherId=s.pieces[1].id;r.count=1;r.to[0]=4;
 Check(Preview(s,r,p,Rich())==Error::InvalidRequest,"fusion cannot populate the fifth slot");
 const unsigned ranks[]={1,10,11,20,21,30,31,40,41,50};const unsigned prices[]={1000,10000,11500,25000,27000,45000,47500,70000,73000,100000};
 for(unsigned k=0;k<10;++k){s=Make(0,true);auto& gear=s.pieces[0];gear.mode=2;gear.fifthUnlocked=1;gear.fifth=0x8062;gear.abilities[4]=s.nextId++;
  unsigned left=ranks[k]-1;for(unsigned i=0;i<5;++i){unsigned n=left>10?10:left;gear.ranks[i]=static_cast<unsigned char>(n);left-=n;}
  Check(Preview(s,Req(s,Op::Refine),p,Rich())==Error::Ok&&p.gilCost==prices[k],"refinement price uses cumulative total-rank bands");}
 DeveloperCases();
 s=Make(0,true);r=Req(s,Op::Refine);auto economy=Rich();Plan legacy{};legacy.gilCost=0xA5A5A5A5u;const auto canary=legacy;
 Check(ws_plan_abi()==5&&ws_plan_economy(&s,&r,&economy,&legacy)!=0&&std::memcmp(&legacy,&canary,sizeof(legacy))==0,"old ABI2 entrypoint refuses to overwrite a smaller host plan");
 std::printf("WORKSHOP_PROGRESSION %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
static void DeveloperCases(){
 auto s=Make(0,true);s.pieces[0].mode=2;s.pieces[0].ranks[0]=9;for(auto& n:s.items)n=1;
 auto e=Rich();e.gil=0;e.policy.devFreeMaterials=1;e.policy.devFreeGil=1;Plan p{};
 Check(Preview(s,Req(s,Op::Refine),p,e)==Error::Ok&&p.gilCost==10000&&p.gilDebit==0&&p.requirements[70]==11,"Dev quotes full price but waives Gil and material amounts");
 Check(std::memcmp(s.items,p.after.items,sizeof(s.items))==0&&p.after.rolls==s.rolls+1,"Dev improves once without consuming held ingredients");
 s.items[70]=0;Check(Preview(s,Req(s,Op::Refine),p,e)==Error::Materials,"Dev still requires one of every roulette ingredient");
 s=Make();s.items[77]=7;s.items[80]=0;
 Check(Preview(s,Req(s,Op::UnlockFifth),p,e)==Error::Ok&&p.costs[77]==7&&p.costs[80]==3&&p.after.items[77]==7,"Dev accepts a held owner sphere without manufacturing Masters");
 s.items[77]=0;Check(Preview(s,Req(s,Op::UnlockFifth),p,e)==Error::Materials,"Dev rejects an entirely absent wildcard group");
 s=Make(0,true);s.pieces[1].native[14]=99;auto r=Req(s,Op::Fuse);r.other=1;r.otherId=s.pieces[1].id;r.count=1;
 Check(Preview(s,r,p,e)==Error::Ok&&!p.after.pieces[1].id&&p.gilCost==10000&&p.gilDebit==0&&std::memcmp(s.items,p.after.items,sizeof(s.items))==0,"Dev fusion destroys donor while waiving resource debit");
 e=Rich();e.customizeUnlocked=0;
 Check(Preview(s,Req(s,Op::Refine),p,e)==Error::Locked&&std::memcmp(&s,&p.after,sizeof(s))==0,"normal Workshop requires native Customize admission");
 e.policy.devIgnoreProgression=1;
 Check(Preview(s,Req(s,Op::Refine),p,e)==Error::Ok&&p.gilCost==1000,"explicit Dev admission does not implicitly waive prices");
 e.policy.devFreeGil=2;Check(Preview(s,Req(s,Op::Refine),p,e)==Error::InvalidPolicy,"invalid Dev flags fail closed");
}
