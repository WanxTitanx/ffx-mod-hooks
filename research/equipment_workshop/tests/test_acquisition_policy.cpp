#include "workshop.h"
#include "customize_recipes.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#if __has_include("acquisition.h")
#include "acquisition.h"
#define HAS_ACQUISITION 1
#else
namespace workshop {struct AcquisitionCatalog {};}
#endif
using namespace workshop;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
static void Word(std::uint8_t* p,unsigned v){p[0]=static_cast<std::uint8_t>(v);p[1]=static_cast<std::uint8_t>(v>>8);}
static std::vector<std::uint8_t> Recipes(bool spira){
    std::vector<std::uint8_t> bytes(20);unsigned count=0;
    for(unsigned id=0;id<131;++id){const auto& r=CustomizeRecipes[id];if(!r.quantity)continue;
        const auto at=bytes.size();bytes.resize(at+8);Word(bytes.data()+at,r.kind);Word(bytes.data()+at+2,spira&&(id==13||id==17)?0:0x8000+(spira&&id==24?23:id));
        Word(bytes.data()+at+4,0x2000+r.item);Word(bytes.data()+at+6,r.quantity);++count;}
    Word(bytes.data(),1);Word(bytes.data()+10,count-1);Word(bytes.data()+12,8);Word(bytes.data()+14,count*8);Word(bytes.data()+16,20);return bytes;
}
static AcquisitionCatalog Rules(const std::vector<std::uint8_t>& data){AcquisitionCatalog out{};
#ifdef HAS_ACQUISITION
    Check(ReadAcquisition(data.data(),data.size(),out),"native customization snapshot is structurally valid");
#else
    (void)data;
#endif
    return out;
}
static State Gear(unsigned kind,unsigned donorWord){std::array<std::uint8_t,4400> gear{};std::array<std::uint16_t,112> items{};items.fill(255);
    for(unsigned slot=0;slot<2;++slot){auto* p=gear.data()+slot*22;p[2]=1;p[5]=static_cast<std::uint8_t>(kind);p[6]=255;p[11]=4;
        for(unsigned i=0;i<4;++i)Word(p+14+2*i,slot&&i==0?donorWord:0x8062+i);}
    State state{};Check(Import(gear.data(),items.data(),5,state)==Error::Ok,"existing native gear imports intact");state.pieces[0].fifthUnlocked=1;return state;
}
static Error Quote(const State& state,Request request,Plan& plan,Economy economy,const AcquisitionCatalog* rules){
#ifdef HAS_ACQUISITION
    return Preview(state,request,plan,economy,rules);
#else
    (void)rules;return Preview(state,request,plan,economy);
#endif
}
static bool Price(const Piece& piece,unsigned word,const AcquisitionCatalog* rules){unsigned item=0,quantity=0;
#ifdef HAS_ACQUISITION
    return FifthCost(piece,static_cast<std::uint16_t>(word),{},item,quantity,nullptr,rules);
#else
    (void)rules;return FifthCost(piece,static_cast<std::uint16_t>(word),{},item,quantity);
#endif
}
int main(){const auto native=Recipes(false),spira=Recipes(true);auto vanilla=Rules(native),retired=Rules(spira);Economy economy{};economy.gil=100000000;economy.customizeUnlocked=1;
    for(unsigned word:{0x800Du,0x8011u,0x8018u}){const unsigned kind=word==0x8018?1:0;auto state=Gear(kind,word);const auto before=state;Plan p{};Request r{};r.op=Op::SetFifth;r.slot=0;r.pieceId=state.pieces[0].id;r.revision=state.revision;r.value=static_cast<std::uint16_t>(word);
        Check(Quote(state,r,p,economy,&vanilla)==Error::Ok,"vanilla retains the native fifth-slot acquisition");
        Check(Quote(state,r,p,economy,&retired)==Error::UnsupportedAbility,"Spira direct fifth request rejects a retired target");
        Check(!Price(state.pieces[0],word,&retired)&&Price(state.pieces[0],word,&vanilla),"fifth-slot list/cost uses the same profile-specific admission");
        r.op=Op::Fuse;r.other=1;r.otherId=state.pieces[1].id;r.count=1;r.from[0]=0;r.to[0]=0;
        Check(Quote(state,r,p,economy,&vanilla)==Error::Ok,"vanilla fusion still transfers the native ability");
        Check(Quote(state,r,p,economy,&retired)==Error::UnsupportedAbility,"Spira direct fusion cannot transfer a retired ability from legacy gear");
        Check(std::memcmp(&state,&before,sizeof(state))==0,"rejected grants preserve gear, inventory, currency and RNG");
        r.op=Op::Clear;r.slot=1;r.pieceId=state.pieces[1].id;r.value=0;
        Check(Quote(state,r,p,economy,&retired)==Error::Ok&&Ability(p.after.pieces[1],0)==Empty,"legacy gear remains removable under the current profile");
        r.op=Op::Refine;Check(Quote(state,r,p,economy,&retired)==Error::Ok,"refinement of already owned words does not become a new grant or erase metadata");
        Check(Validate(state)==Error::Ok&&Ability(state.pieces[1],0)==word,"state validation and recovery do not blacklist old gear");
    }
    for(unsigned word:{0x800Cu,0x8017u,0x8019u}){auto state=Gear(word==0x8017?1:0,word);Request r{};r.op=Op::SetFifth;r.pieceId=state.pieces[0].id;r.value=static_cast<std::uint16_t>(word);Plan p{};
        Check(Quote(state,r,p,economy,&retired)==Error::Ok,"Half MP, Break Limits and BDL remain obtainable");}
#ifdef HAS_ACQUISITION
    Check(AcquisitionAllowed(0x807A,&retired),"the global Capture identity is preserved despite having no vanilla Customize recipe");
    auto other=native;for(std::size_t at=20;at<other.size();at+=8)if(other[at+2]==25)Word(other.data()+at+2,0);auto different=Rules(other);
    Check(AcquisitionAllowed(0x800D,&different)&&AcquisitionAllowed(0x8011,&different)&&AcquisitionAllowed(0x8018,&different)&&!AcquisitionAllowed(0x8019,&different),"another profile follows its own actual targets, not the Spira ID list");
    auto freeRecipe=native;for(std::size_t at=20;at<freeRecipe.size();at+=8)if(freeRecipe[at+2]==13)Word(freeRecipe.data()+at+6,0);
    AcquisitionCatalog freeProfile{};Check(ReadAcquisition(freeRecipe.data(),freeRecipe.size(),freeProfile)&&AcquisitionAllowed(0x800D,&freeProfile),"a free recipe in another profile is still an available target; admission does not redefine pricing");
    AcquisitionCatalog unavailable{};Check(!AcquisitionAllowed(0x800D,&unavailable),"unavailable recipe data cannot silently restore a retired grant");
    for(unsigned offset:{0u,12u,14u,16u}){auto bad=spira;bad[offset]^=1;AcquisitionCatalog rejected{};Check(!ReadAcquisition(bad.data(),bad.size(),rejected),"corrupt native table refuses acquisition admission");}
#endif
    std::printf("WORKSHOP_ACQUISITION %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
