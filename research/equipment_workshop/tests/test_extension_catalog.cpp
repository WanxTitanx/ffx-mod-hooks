// Jarvis-HOOK: explicit host validation inputs; no native kernel or player files.
#include "workshop.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace workshop;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static State Fixture(unsigned kind){
    std::array<unsigned char,4400> gear{};std::array<std::uint16_t,112> items{};items.fill(255);
    gear[2]=1;gear[5]=static_cast<unsigned char>(kind);gear[6]=255;gear[11]=4;
    for(unsigned i=0;i<4;++i){gear[14+2*i]=98;gear[15+2*i]=128;}
    State state{};Check(Import(gear.data(),items.data(),123,state)==Error::Ok,"extension fixture imports");
    state.pieces[0].fifthUnlocked=1;state.pieces[0].mode=2;state.pieces[0].ranks[0]=7;return state;
}
static Request Fifth(const State& state,unsigned word){Request r{};r.op=Op::SetFifth;r.pieceId=state.pieces[0].id;r.revision=state.revision;r.value=static_cast<std::uint16_t>(word);return r;}
int main(){
    Economy economy{};economy.gil=1000000;economy.customizeUnlocked=1;economy.catalog.proof=0x12345678;
    for(unsigned effect=0;effect<13;++effect) economy.catalog.entries[effect]={static_cast<std::uint16_t>(0x8087+effect),static_cast<std::uint16_t>(effect<6?1:2),73,0};
    for(unsigned effect=0;effect<13;++effect)for(unsigned kind=0;kind<2;++kind){
        const auto state=Fixture(kind);const auto before=state;const auto word=economy.catalog.entries[effect].word;
        Plan plan{};const auto result=Preview(state,Fifth(state,word),plan,economy);const bool compatible=(effect<6?0u:1u)==kind;
        Check(result==(compatible?Error::Ok:Error::UnsupportedAbility),"only a currently validated type-compatible extension can enter the fifth slot");
        if(!compatible||result!=Error::Ok)continue;
        Check(plan.catalog.proof==economy.catalog.proof&&plan.costs[73]==45&&plan.gilDebit==200000,"extension quote binds its proof and charges the declared mod recipe plus fifth fee");
        Check(plan.after.pieces[0].fifth==word&&plan.after.pieces[0].ranks[0]==7&&plan.after.pieces[0].ranks[4]==0&&Validate(plan.after)==Error::Ok,"extension fifth preserves native bytes, identities and prior refinements");
        Check(std::memcmp(state.pieces[0].native,plan.after.pieces[0].native,22)==0,"logical fifth never overwrites a native slot");
        auto refined=plan.after;Request r{};r.op=Op::Refine;r.pieceId=refined.pieces[0].id;r.revision=refined.revision;
        Plan quote{};Check(Preview(refined,r,quote,economy)==Error::Ok,"new equipped abilities participate in the shared paid refinement pipeline");
        Check(std::memcmp(&state,&before,sizeof(state))==0,"preview is immutable");
    }
    auto state=Fixture(0);Plan plan{};
    auto remapped=economy;remapped.catalog.entries[0].word=0x81F4;remapped.catalog.proof++;
    Check(Preview(state,Fifth(state,0x81F4),plan,remapped)==Error::Ok,"mapping is not a hardcoded 147 upper boundary");
    Check(Preview(state,Fifth(state,0x8087),plan,remapped)==Error::UnsupportedAbility,"old ID stops being authorable after remap");
    auto invalid=economy;invalid.catalog.entries[1].word=invalid.catalog.entries[0].word;
    Check(Preview(state,Fifth(state,0x8087),plan,invalid)!=Error::Ok,"duplicate extension identities fail before mutation");
    invalid=economy;invalid.catalog.proof=0;
    Check(Preview(state,Fifth(state,0x8087),plan,invalid)!=Error::Ok,"an unproven extension catalog is not trusted");
    for(unsigned id:{130u,131u,132u,133u,134u}){invalid=economy;invalid.catalog.entries[0].word=static_cast<std::uint16_t>(0x8000+id);
        Check(Preview(state,Fifth(state,invalid.catalog.entries[0].word),plan,invalid)!=Error::Ok,"catalog cannot claim vanilla or reserved identities");}
    std::printf("WORKSHOP_EXTENSION_CATALOG %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
