#include "workshop.h"
#include <array>
#include <cstdio>
#include <cstring>

using namespace workshop;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static State Fixture(unsigned owner=8){
    std::array<unsigned char,GearCount*NativeBytes> records{};
    std::array<std::uint16_t,ItemCount> items{};items.fill(99);
    auto* gear=records.data();gear[2]=1;gear[3]=7;gear[4]=static_cast<unsigned char>(owner);
    gear[6]=static_cast<unsigned char>(owner);gear[11]=4;
    const std::uint16_t abilities[]={0x807B,0x8019,0x8062,0x00FF};
    std::memcpy(gear+14,abilities,8);
    State state{};Check(Import(records.data(),items.data(),123,state)==Error::Ok,"native Aeon records import without changing persisted layout");return state;
}
static Request Make(const State& state,Op op,unsigned value=0){
    Request request{};request.op=op;request.slot=0;request.revision=state.revision;
    request.pieceId=state.pieces[0].id;request.value=static_cast<std::uint16_t>(value);return request;
}
int main(){
    State state=Fixture();Plan plan{};Economy economy{};economy.gil=10000000;economy.customizeUnlocked=1;
    auto clear=Make(state,Op::Clear,2);
    Check(Preview(state,clear,plan,economy)==Error::Locked,"an Aeon without verified acquisition/Crest cannot be edited");
    Check(!std::memcmp(&state,&plan.after,sizeof(state)),"locked Aeon operation preserves all state and materials");
    auto create=Make(state,Op::Create);create.slot=1;create.pieceId=0;
    std::memcpy(create.gearTemplate,state.pieces[0].native,22);create.gearTemplate[6]=255;
    Check(Preview(state,create,plan,economy)==Error::Protected,"Workshop create cannot forge an Aeon record");
    for(unsigned owner:{8u,9u,10u,11u,14u})
        Check(std::strstr(AeonRequirement(owner),"Crest applied")!=nullptr,
              "Aeon requirements name the applied upgrade without version-specific planet names");
    for(unsigned owner=8;owner<18;++owner)for(unsigned position=0;position<4;++position){
        auto guarded=Fixture(owner);auto& piece=guarded.pieces[0];
        unsigned char original[2]{};std::memcpy(original,piece.native+14,2);
        std::memcpy(piece.native+14,piece.native+14+2*position,2);
        std::memcpy(piece.native+14+2*position,original,2);
        const auto firstId=piece.abilities[0];piece.abilities[0]=piece.abilities[position];piece.abilities[position]=firstId;
        Economy allowed{};allowed.gil=10000000;allowed.customizeUnlocked=1;
        allowed.aeons.obtained=1u<<owner;allowed.aeons.crests=127;allowed.aeons.gear[2*(owner-8)]=0;
        Check(Preview(guarded,Make(guarded,Op::Clear,position),plan,allowed)==Error::Protected&&
              !std::memcmp(&guarded,&plan.after,sizeof(guarded)),"every owner and native immunity position rejects removal without spending");
        allowed.policy.mode=1;
        Check(Preview(guarded,Make(guarded,Op::Refine),plan,allowed)==Error::Ok&&plan.gilCost==6000&&
              plan.after.pieces[0].abilities[position]==piece.abilities[position]&&plan.after.pieces[0].ranks[position]==0,
              "whole-equipment refinement excludes permanent immunity and doubles Gil once");
        allowed.policy.mode=2;allowed.gil=1999;
        Check(Preview(guarded,Make(guarded,Op::Refine),plan,allowed)==Error::Gil&&
              !std::memcmp(&guarded,&plan.after,sizeof(guarded)),"insufficient doubled Gil preserves RNG, resources and abilities");
    }
    std::printf("WORKSHOP_AEONS %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
