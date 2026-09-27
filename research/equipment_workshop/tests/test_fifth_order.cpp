#include "workshop.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace workshop;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
static Economy Rich(){Economy e{};e.gil=1000000;e.customizeUnlocked=1;return e;}
static State Make(unsigned mask){
    std::array<unsigned char,4400> gear{};std::array<std::uint16_t,112> items{};items.fill(255);
    gear[2]=1;gear[6]=255;gear[11]=4;
    for(unsigned i=0;i<4;++i){const unsigned word=(mask&(1u<<i))?0x8062:(i%2?255:0);gear[14+2*i]=static_cast<unsigned char>(word);gear[15+2*i]=static_cast<unsigned char>(word>>8);}
    State state{};Check(Import(gear.data(),items.data(),12345,state)==Error::Ok,"order fixture imports");
    state.pieces[0].mode=2;for(unsigned i=0;i<4;++i)if(mask&(1u<<i))state.pieces[0].ranks[i]=static_cast<unsigned char>(i+1);
    return state;
}
static Request Req(const State& state,Op op){Request r{};r.op=op;r.pieceId=state.pieces[0].id;r.revision=state.revision;return r;}
int main(){
    for(unsigned mask=0;mask<16;++mask){
        auto state=Make(mask);const auto before=state;Plan plan{};
        Check(Preview(state,Req(state,Op::UnlockFifth),plan,Rich())==Error::Ok&&plan.after.pieces[0].fifthUnlocked,
              "four open native slots admit unlock for every occupied/empty combination");
        Check(std::memcmp(plan.after.pieces[0].native,before.pieces[0].native,22)==0&&
              std::memcmp(plan.after.pieces[0].ranks,before.pieces[0].ranks,5)==0&&plan.after.pieces[0].id==before.pieces[0].id,
              "unlock preserves native abilities, ranks and identity");
        state.pieces[0].fifthUnlocked=1;auto set=Req(state,Op::SetFifth);set.value=0x8063;
        Check(Preview(state,set,plan,Rich())==(mask==15?Error::Ok:Error::InvalidRequest),
              "fifth customization requires all four native abilities including zero-empty normalization");
        if(mask!=15){
            Check(std::memcmp(&plan.after,&state,sizeof(state))==0&&plan.gilCost==0,
                  "incomplete native abilities reject before charging or changing metadata");
            auto dev=Rich();dev.policy.devFreeMaterials=dev.policy.devFreeGil=dev.policy.devIgnoreProgression=1;
            Check(Preview(state,set,plan,dev)==Error::InvalidRequest,"development exemptions cannot bypass fifth-slot order");
        }
        Check(before.rng==state.rng&&before.rolls==state.rolls,"slot-order previews never advance RNG");
    }
    auto state=Make(0);state.pieces[0].native[11]=3;Plan plan{};
    Check(Preview(state,Req(state,Op::UnlockFifth),plan,Rich())==Error::InvalidRequest,"three open slots cannot unlock the fifth");
    state=Make(15);state.pieces[0].fifthUnlocked=1;state.pieces[0].fifth=0x8062;state.pieces[0].abilities[4]=state.nextId++;
    auto clear=Req(state,Op::Clear);clear.value=2;
    Check(Preview(state,clear,plan,Rich())==Error::Ok,"native removal remains permitted on unlocked equipment");
    state=plan.after;auto set=Req(state,Op::SetFifth);set.value=0x8063;
    Check(Preview(state,set,plan,Rich())==Error::InvalidRequest,"replacement of an existing fifth also requires four filled native slots");
    Check(state.pieces[0].fifthUnlocked&&state.pieces[0].fifth==0x8062&&Validate(state)==Error::Ok,
          "clearing a native ability never erases existing fifth data or invalidates legacy saves");
    std::printf("WORKSHOP_FIFTH_ORDER %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
