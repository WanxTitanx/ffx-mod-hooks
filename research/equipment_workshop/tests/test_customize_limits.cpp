#include "workshop.h"
#include "customize_recipes.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace workshop;
static unsigned checks=0,failed=0;
static void Check(bool value,const char* name){++checks;if(!value){++failed;std::printf("FAIL %s\n",name);}}
static State Fixture(unsigned kind,std::uint16_t first){
    std::array<std::uint8_t,4400> gear{};std::array<std::uint16_t,112> items{};items.fill(255);
    gear[2]=1;gear[5]=static_cast<std::uint8_t>(kind);gear[6]=255;gear[11]=4;
    const std::uint16_t words[]={first,static_cast<std::uint16_t>(kind?0x806A:0x8062),static_cast<std::uint16_t>(kind?0x806B:0x8063),static_cast<std::uint16_t>(kind?0x806C:0x8064)};
    std::memcpy(gear.data()+14,words,8);State s{};Import(gear.data(),items.data(),42,s);s.pieces[0].fifthUnlocked=1;return s;
}
static Request RequestFor(const State& s,Op op,std::uint16_t word){Request r{};r.op=op;r.revision=s.revision;r.pieceId=s.pieces[0].id;r.value=word;return r;}
static Economy Rich(){Economy e{};e.customizeUnlocked=1;e.gil=1000000;return e;}
int main(){
    for(unsigned free=0;free<2;++free){auto e=Rich();e.policy.devFreeMaterials=free;e.policy.devFreeGil=free;
        for(auto word:{0x800Bu,0x800Fu,0x8080u}){auto s=Fixture(word==0x8080,static_cast<std::uint16_t>(word));Plan p{};
            const auto result=Preview(s,RequestFor(s,Op::SetFifth,static_cast<std::uint16_t>(word)),p,e);
            Check(result!=Error::Ok,"fifth placement rejects an exact existing ability before spending, including DEV");
            Check(std::memcmp(&s,&p.after,sizeof(s))==0,"rejected duplicate preserves inventory, ranks, IDs and RNG");}
        for(auto pair:{std::array<unsigned,3>{0,15,14},{0,13,12},{0,124,125},{1,128,56},{1,33,31},{1,86,91}}){
            auto s=Fixture(pair[0],static_cast<std::uint16_t>(0x8000+pair[1]));Plan p{};
            Check(Preview(s,RequestFor(s,Op::SetFifth,static_cast<std::uint16_t>(0x8000+pair[2])),p,e)!=Error::Ok,"native stronger/group/variant/Ribbon restrictions reject the candidate");}
    }
    for(auto pair:{std::array<unsigned,3>{0,14,15},{0,12,13},{1,31,33},{1,56,128},{0,30,34}}){
        auto s=Fixture(pair[0],static_cast<std::uint16_t>(0x8000+pair[1]));Plan p{};
        Check(Preview(s,RequestFor(s,Op::SetFifth,static_cast<std::uint16_t>(0x8000+pair[2])),p,Rich())==Error::Ok,"native permitted directional upgrades and separate elements stay permitted");}
    for(unsigned id:{20u,83u,122u,123u,129u,130u}){auto s=Fixture(0,0x800B);Plan p{};
        Check(Preview(s,RequestFor(s,Op::SetFifth,static_cast<std::uint16_t>(0x8000+id)),p,Rich())!=Error::Ok,"new fifth creation requires a real native Customize recipe");}
    {auto s=Fixture(0,0x800B);auto create=RequestFor(s,Op::Create,0);create.slot=1;create.pieceId=0;
     std::memcpy(create.gearTemplate,s.pieces[0].native,22);Plan born{};
     Check(Preview(s,create,born,Rich())==Error::Ok,"native fusion donor fixture creates");s=born.after;
     auto fuse=RequestFor(s,Op::Fuse,0);fuse.other=1;fuse.otherId=s.pieces[1].id;fuse.count=1;fuse.to[0]=1;Plan p{};
     Check(Preview(s,fuse,p,Rich())!=Error::Ok,"Fusion cannot introduce a duplicate through a native slot");
     s.pieces[0].fifth=0x800B;s.pieces[0].abilities[4]=s.nextId++;s.pieces[0].native[14]=0;s.pieces[0].native[15]=128;
     Check(Preview(s,fuse,p,Rich())!=Error::Ok,"Fusion checks the existing fifth as well as native abilities");}
    for(unsigned old=0;old<131;++old){
        const bool numeric=old>=98&&old<121&&(old-98)%4<3;
        const bool touch=old>=47&&old<=75&&(old-47)%4==0;
        if(!numeric&&!touch)continue;
        const unsigned next=numeric?old+1:old-1;const auto recipe=CustomizeRecipes[next];
        auto s=Fixture(recipe.kind-1,static_cast<std::uint16_t>(0x8000+old));
        // Ensure the other slots cannot duplicate the chosen destination.
        const std::uint16_t rest[]={static_cast<std::uint16_t>(recipe.kind==1?0x8000:0x8008),static_cast<std::uint16_t>(recipe.kind==1?0x8001:0x8009),static_cast<std::uint16_t>(recipe.kind==1?0x8002:0x800A)};
        std::memcpy(s.pieces[0].native+16,rest,6);
        for(unsigned mode:{1u,2u}){auto a=s;a.pieces[0].mode=static_cast<std::uint8_t>(mode);a.pieces[0].rank=mode==1?5:0;if(mode==2)a.pieces[0].ranks[0]=5;
            Plan p{};const auto r=RequestFor(a,Op::Evolve,static_cast<std::uint16_t>(0x8000+next));
            Check(Preview(a,r,p,Rich())==Error::Ok,"existing evolution path remains available");
            unsigned sum=0;for(auto qty:p.costs)sum+=qty;
            Check(p.gilCost==25000&&p.costs[recipe.item]==(recipe.quantity+1u)/2u&&sum==(recipe.quantity+1u)/2u,"evolution costs only rounded half of the destination recipe plus 25000 Gil");
            Check(AbilityRank(p.after.pieces[0],0)==0&&AbilityRank(p.after.pieces[0],1)==AbilityRank(a.pieces[0],1),"evolution resets only the replaced instance and preserves other legacy ranks");
        }
    }
    std::printf("WORKSHOP_CUSTOMIZE_LIMITS %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
