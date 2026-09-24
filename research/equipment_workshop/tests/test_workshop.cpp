#include "workshop.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace workshop;
static unsigned checks,failures;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
static void Put(std::uint8_t* p,unsigned n,unsigned value){p[n]=static_cast<std::uint8_t>(value);p[n+1]=static_cast<std::uint8_t>(value>>8);}
static State Make(){
    std::array<std::uint8_t,GearCount*NativeBytes> bytes{};
    std::array<std::uint16_t,ItemCount> items{};items.fill(255);
    for(unsigned slot=0;slot<4;++slot){auto* p=bytes.data()+slot*NativeBytes;p[2]=1;p[4]=0;p[5]=0;p[6]=0xFF;p[11]=4;
        for(unsigned i=0;i<4;++i)Put(p,14+2*i,0x8064);}
    State s{};Check(Import(bytes.data(),items.data(),12345,s)==Error::Ok,"import packed inventory");return s;
}
static Request Req(const State& s,Op op,unsigned slot=0){Request r{};r.op=op;r.revision=s.revision;r.slot=static_cast<std::uint16_t>(slot);r.pieceId=s.pieces[slot].id;return r;}
static Error Step(State& s,Request r,Plan* receipt=nullptr){Plan p{};auto e=Preview(s,r,p);if(e==Error::Ok)s=p.after;if(receipt)*receipt=p;return e;}
static void Mode(State& s,unsigned slot,unsigned mode){auto r=Req(s,Op::Mode,slot);r.value=static_cast<std::uint16_t>(mode);Check(Step(s,r)==Error::Ok,"choose refinement mode");}
int main(){
    auto s=Make();Check(sizeof(State)==16260 && sizeof(Piece)==80,"wire sizes stable");
    Check(s.pieces[0].id!=s.pieces[1].id && std::memcmp(s.pieces[0].native,s.pieces[1].native,22)==0,"identical records have different identities");
    Mode(s,0,1);Plan receipt{};
    Check(Step(s,Req(s,Op::Refine),&receipt)==Error::Ok && s.pieces[0].rank==1 && receipt.costs[87]==4,"A costs aggregate all occupied abilities");
    const auto originalId=s.pieces[0].id;auto r=Req(s,Op::Swap);r.other=1;r.otherId=s.pieces[1].id;
    Check(Step(s,r)==Error::Ok && s.pieces[1].id==originalId && s.pieces[1].rank==1 && s.pieces[0].rank==0,"swap moves metadata with identical pieces");
    r=Req(s,Op::Retire,1);Check(Step(s,r)==Error::Ok && s.pieces[1].id==0,"sale retires identity");
    r=Req(s,Op::Create,1);std::memcpy(r.gearTemplate,s.pieces[0].native,22);Check(Step(s,r)==Error::Ok && s.pieces[1].id!=originalId && s.pieces[1].rank==0,"recreated identical piece gets fresh identity");
    auto b=Make();Mode(b,0,2);auto unlock=Req(b,Op::UnlockFifth);Check(Step(b,unlock)==Error::Ok,"fifth unlock is logical");
    auto fifth=Req(b,Op::SetFifth);fifth.value=0x8055;Check(Step(b,fifth)==Error::Ok,"fifth auto-protect admitted");
    Check(b.pieces[0].native[11]==4 && Ability(b.pieces[0],4)==0x8055 && b.pieces[1].native[0]==0,"fifth never touches native capacity or neighbor");
    for(unsigned i=0;i<50;++i){b.items[72]=b.items[73]=255;Check(Step(b,Req(b,Op::Refine))==Error::Ok,"B eligible selection reaches fifty");}
    unsigned total=0;for(auto rank:b.pieces[0].ranks)total+=rank;
    Check(total==50 && b.rolls==50,"B max is fifty with five abilities");auto saved=b;
    Check(Step(b,Req(b,Op::Refine))==Error::Maximum && std::memcmp(&b,&saved,sizeof(b))==0,"max attempt consumes no materials or RNG");
    auto poor=Make();Mode(poor,0,2);poor.items[72]=0;auto before=poor;
    Check(Step(poor,Req(poor,Op::Refine))==Error::Materials && std::memcmp(&poor,&before,sizeof(poor))==0,"failed attempt is exact no-op including RNG");
    auto duplicate=Make();Mode(duplicate,0,2);Mode(duplicate,1,2);auto sourceAbility=duplicate.pieces[1].abilities[0];
    auto fuse=Req(duplicate,Op::Fuse);fuse.other=1;fuse.otherId=duplicate.pieces[1].id;fuse.count=1;fuse.from[0]=0;fuse.to[0]=0;
    Check(Step(duplicate,fuse)==Error::Ok && duplicate.pieces[0].abilities[0]==sourceAbility && duplicate.pieces[1].id==0,"fusion transfers ability instance and consumes donor");
    auto stale=Req(duplicate,Op::Refine);stale.pieceId=duplicate.nextId;Check(Step(duplicate,stale)==Error::Stale,"stale identity cannot refine replacement");
    auto invalid=Make();invalid.pieces[0].native[11]=5;Check(Validate(invalid)==Error::InvalidState,"native capacity five rejected");
    invalid=Make();invalid.pieces[1].id=invalid.pieces[0].id;Check(Validate(invalid)==Error::InvalidState,"duplicate identity rejected");
    for(auto bad:{20u,29u,123u}){auto x=Make();Put(x.pieces[0].native,14,0x8000+bad);Mode(x,0,1);Check(Step(x,Req(x,Op::Refine))==Error::UnsupportedAbility,"undefined effects explicitly block entire piece");}
    auto expansion=Make();
    for(unsigned i=0;i<4;++i){Put(expansion.pieces[0].native,14+2*i,Empty);expansion.pieces[0].abilities[i]=0;}
    expansion.pieces[0].native[11]=0;
    for(unsigned policy=0;policy<2;++policy){auto x=expansion;auto request=Req(x,Op::Expand);request.value=4;request.policy=static_cast<std::uint8_t>(policy);
        Check(Step(x,request,&receipt)==Error::Ok && x.pieces[0].native[11]==4,"both expansion recipes reach only four native slots");
        bool costs=true;for(unsigned i=1;i<=4;++i)costs=costs&&receipt.costs[80+i]==(policy?i:1);
        Check(costs && std::memcmp(x.pieces[1].native,expansion.pieces[1].native,22)==0,"expansion costs match policy and neighbor stays exact");}
    auto reforge=Make();Mode(reforge,0,2);reforge.pieces[0].ranks[0]=7;const auto keptId=reforge.pieces[0].id;
    r=Req(reforge,Op::Reforge);std::memcpy(r.gearTemplate,reforge.pieces[1].native,22);r.gearTemplate[4]=3;r.gearTemplate[5]=1;r.gearTemplate[12]=0x43;
    Check(Step(reforge,r,&receipt)==Error::Ok && reforge.pieces[0].id==keptId && reforge.pieces[0].ranks[0]==7 && reforge.pieces[0].native[4]==3 && receipt.costs[80]==1,"reforge preserves piece/ability identity and changes verified presentation");
    auto kimahri=reforge;Check(Step(kimahri,Req(kimahri,Op::Refine),&receipt)==Error::Ok && receipt.costs[73]==2,"same-ID catalyst and sphere aggregate for Kimahri");
    auto clear=Make();Mode(clear,0,2);clear.pieces[0].ranks[0]=8;r=Req(clear,Op::Clear);r.value=0;
    Check(Step(clear,r,&receipt)==Error::Ok && Ability(clear.pieces[0],0)==Empty && clear.pieces[0].abilities[0]==0 && clear.pieces[0].ranks[0]==0 && receipt.costs[95]==1,"clear retires the ability instance and its rank");
    auto evolve=Make();Mode(evolve,0,2);evolve.pieces[0].ranks[0]=9;const auto retired=evolve.pieces[0].abilities[0];r=Req(evolve,Op::Evolve);r.value=0x8065;r.to[0]=0;
    Check(Step(evolve,r)==Error::Ok && evolve.pieces[0].abilities[0]!=retired && evolve.pieces[0].ranks[0]==0,"evolution cannot inherit the replaced B slot rank");
    auto catchup=Make();Mode(catchup,0,1);Mode(catchup,1,1);catchup.pieces[0].rank=10;
    r=Req(catchup,Op::Fuse);r.other=1;r.otherId=catchup.pieces[1].id;r.count=1;r.from[0]=0;r.to[0]=0;
    Check(Step(catchup,r,&receipt)==Error::Ok && receipt.costs[87]==22 && receipt.costs[75]==3 && receipt.costs[73]==1,"A fusion charges all ten catch-up ranks and its fusion ingredient");
    auto transfer=Make();Mode(transfer,0,2);Mode(transfer,1,2);transfer.pieces[1].ranks[0]=7;transfer.pieces[1].ranks[1]=8;
    const auto firstId=transfer.pieces[1].abilities[0],secondId=transfer.pieces[1].abilities[1];
    r=Req(transfer,Op::Fuse);r.other=1;r.otherId=transfer.pieces[1].id;r.count=2;r.from[0]=0;r.to[0]=2;r.from[1]=1;r.to[1]=3;
    Check(Step(transfer,r)==Error::Ok && transfer.pieces[0].abilities[2]==firstId && transfer.pieces[0].abilities[3]==secondId && transfer.pieces[0].ranks[2]==7 && transfer.pieces[0].ranks[3]==8,"two transferred ability identities retain only their own B ranks");
    auto protectedPiece=Make();protectedPiece.pieces[1].native[3]=4;r=Req(protectedPiece,Op::Fuse);r.other=1;r.otherId=protectedPiece.pieces[1].id;r.count=1;
    Check(Step(protectedPiece,r)==Error::Protected,"Celestial donor cannot be consumed");
    protectedPiece=Make();protectedPiece.pieces[0].native[3]=8;r=Req(protectedPiece,Op::Fuse);r.other=1;r.otherId=protectedPiece.pieces[1].id;r.count=1;
    Check(Step(protectedPiece,r)==Error::Ok && protectedPiece.pieces[0].native[3]==8,"Brotherhood may receive without losing native special flags");
    auto equipped=Make();equipped.pieces[0].native[6]=0;r=Req(equipped,Op::Clear);r.value=0;Check(Step(equipped,r)==Error::Equipped,"equipped gear cannot be edited mid-context");
    auto random=Make();Mode(random,0,2);Plan preview1{},preview2{};r=Req(random,Op::Refine);
    Check(Preview(random,r,preview1)==Error::Ok && Preview(random,r,preview2)==Error::Ok && std::memcmp(&preview1,&preview2,sizeof(Plan))==0,"cancel and repeat preview do not advance the generator");
    for(unsigned i=0;i<40;++i){random.items[72]=random.items[73]=255;Check(Step(random,Req(random,Op::Refine))==Error::Ok,"four-slot B has forty eligible successes");}
    Check(Step(random,Req(random,Op::Refine))==Error::Maximum && random.rolls==40,"four-slot maximum is forty");
    auto exhausted=Make();exhausted.nextId=std::numeric_limits<std::uint64_t>::max();Check(Validate(exhausted)==Error::InvalidState,"identity counter cannot wrap");
    std::array<std::uint8_t,4400> zeroNative{};zeroNative[2]=1;zeroNative[6]=255;
    std::array<std::uint16_t,112> zeroItems{};State zeroState{};
    Check(Import(zeroNative.data(),zeroItems.data(),1,zeroState)==Error::Ok && Ability(zeroState.pieces[0],0)==Empty && zeroState.pieces[0].abilities[0]==0,"native zero ability words remain empty without manufactured instances");
    Check(std::memcmp(zeroState.pieces[0].native,zeroNative.data(),22)==0,"empty normalization never rewrites native record bytes");
    auto hidden=Make();hidden.pieces[0].native[11]=1;
    Check(Validate(hidden)==Error::InvalidState,"hidden occupied native words cannot obtain unpaid global-rank effects");
    std::printf("WORKSHOP_CORE %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
