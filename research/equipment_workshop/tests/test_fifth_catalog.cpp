#include "workshop.h"
#include "customize_recipes.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace workshop;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::printf("FAIL %s\n",message);}}
static State Fixture(unsigned kind){
    std::array<unsigned char,4400> gear{};std::array<std::uint16_t,112> items{};items.fill(255);
    gear[2]=1;gear[5]=static_cast<unsigned char>(kind);gear[6]=255;gear[11]=4;
    for(unsigned i=0;i<4;++i){gear[14+2*i]=98;gear[15+2*i]=128;}
    State state{};Check(Import(gear.data(),items.data(),123,state)==Error::Ok,"catalog fixture imports");
    state.pieces[0].fifthUnlocked=1;state.pieces[0].mode=2;state.pieces[0].ranks[0]=7;return state;
}
int main(){
    Economy economy{};economy.gil=1000000;economy.customizeUnlocked=1;
    for(unsigned kind=0;kind<2;++kind)for(unsigned id=0;id<131;++id){
        auto state=Fixture(kind);if(id==98)for(unsigned i=0;i<4;++i)state.pieces[0].native[14+2*i]=99;const auto before=state;const auto recipe=CustomizeRecipes[id];
        const auto word=static_cast<std::uint16_t>(0x8000+id);
        const bool admitted=recipe.quantity&&recipe.kind==kind+1;
        unsigned item=999,quantity=999;
        Check(FifthCost(state.pieces[0],word,economy.policy,item,quantity)==admitted,
              "fifth catalog includes every type-compatible native recipe without manufacturing native recipes");
        Request request{};request.op=Op::SetFifth;request.pieceId=state.pieces[0].id;request.revision=state.revision;request.value=word;
        Plan plan{};const auto result=Preview(state,request,plan,economy);
        Check(result==(admitted?Error::Ok:Error::UnsupportedAbility),"catalog admission is enforced by the paid transaction");
        if(admitted){
            const unsigned expected=recipe.quantity?(3*recipe.quantity+1)/2:45;
            Check(plan.costs[recipe.item]==expected&&plan.gilCost==200000&&plan.after.pieces[0].fifth==word,
                  "each fifth ability uses its actual native recipe plus the fifth Gil fee");
            Check(std::memcmp(plan.after.pieces[0].native,before.pieces[0].native,22)==0&&plan.after.pieces[0].ranks[0]==7&&
                  plan.after.pieces[0].ranks[4]==0&&Validate(plan.after)==Error::Ok,"catalog extension preserves native records and prior ranks");
        }
        const bool generic=!((id>=98&&id<=121)||id==84||id==85);
        Check(GenericRefinement(word)==generic,"broad fifth creation must not erase the existing generic refinement policy");
    }
    Check(!SupportedFifth(0x8083)&&!SupportedFifth(0xFFFF),"unknown abilities remain outside the bounded catalog");
    std::printf("WORKSHOP_FIFTH_CATALOG %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
