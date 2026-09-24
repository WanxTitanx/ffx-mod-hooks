#include "../hooks/RonsoPoolCore.h"
#include <cstdio>
using namespace FfxHooks::RonsoPool;
namespace {
int checks=0,failures=0;
void Expect(bool ok,const char* why) {
    ++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}
}
Facts Kimahri(uint16_t command=115,uint8_t charge=199,uint8_t cost=200) {
    return {true,false,true,true,true,3,command,charge,cost};
}
void ScopeAndThresholds() {
    for(uint16_t command=104;command<=115;++command)
        for(uint8_t cost:{uint8_t(20),uint8_t(100),uint8_t(200)})
            for(uint16_t charge=0;charge<=255;++charge) {
                auto f=Kimahri(command,static_cast<uint8_t>(charge),cost);
                const auto expected=charge>=cost && charge<=200?Availability::Allow:Availability::Block;
                auto decision=Evaluate(f);
                Expect(decision.availability==expected&&decision.useCapacity200,
                       "learned Ronso uses its actual cost within the fixed capacity");
                f.commandId=static_cast<uint16_t>(command|0x3000);
                Expect(Evaluate(f).availability==expected,"raw and exact player encoding agree");
            }
    for(uint16_t id=0;id<32;++id)if(id!=3) {
        auto f=Kimahri();f.characterId=id;
        Expect(Evaluate(f).availability==Availability::Native&&!Evaluate(f).useCapacity200,
               "other characters and aeons are untouched, including Auron2");
    }
    for(int off=0;off<3;++off) {
        auto f=Kimahri();if(off==0)f.enabled=false;if(off==1)f.stopping=true;if(off==2)f.ownedPartyActor=false;
        Expect(Evaluate(f).availability==Availability::Native&&!Evaluate(f).useCapacity200,
               "OFF, stop and unowned actor are inert");
    }
    for(uint16_t command:{uint16_t(0),uint16_t(103),uint16_t(116),uint16_t(282),uint16_t(334),uint16_t(0x4073),uint16_t(0xF073)}) {
        auto f=Kimahri(command,200,20);
        Expect(Evaluate(f).availability==Availability::Native&&Evaluate(f).useCapacity200,
               "ordinary turns retain owned capacity without changing command behavior");
    }
    for(int blocked=0;blocked<2;++blocked) {
        auto f=Kimahri(104,200,20);if(blocked==0)f.learned=false;else f.nativeRestrictionsAllow=false;
        Expect(Evaluate(f).availability==Availability::Block,"affordable cannot grant unlearned or disabled commands");
    }
    for(uint8_t cost:{uint8_t(0),uint8_t(201),uint8_t(250),uint8_t(255)}) {
        auto f=Kimahri(115,200,cost);
        const auto expected=(cost==0||cost==255)?Availability::Native:Availability::Block;
        Expect(Evaluate(f).availability==expected,"sentinels retain native classification; above-cap cost is blocked");
    }
}
void HeaderAndOwnership() {
    auto header=Kimahri(282,20,100);std::array<Facts,12> children{};
    for(size_t i=0;i<children.size();++i) {
        children[i]=Kimahri(static_cast<uint16_t>(104+i),20,100);children[i].learned=false;
    }
    Expect(EvaluateHeader(header,children)==Availability::Block,"no affordable learned child blocks header");
    children[0]=Kimahri(104,20,20);
    Expect(EvaluateHeader(header,children)==Availability::Allow,"Jump20 opens header without paying its metadata cost100");
    header.characterId=2;
    Expect(EvaluateHeader(header,children)==Availability::Native,"other actor header is not overridden");
    header=Kimahri(282,20,100);header.nativeRestrictionsAllow=false;
    Expect(EvaluateHeader(header,children)==Availability::Block,"header respects native restrictions");
    header=Kimahri(282,20,100);children[0].characterId=2;
    Expect(EvaluateHeader(header,children)==Availability::Block,"foreign child's affordability cannot open Kimahri header");
    children[0]=Kimahri(104,200,20);
    Expect(EvaluateHeader(header,children)==Availability::Block,"mismatched charge snapshot is rejected");
    Owner owner{17,0x1234,100,true,false};
    Expect(CanRestore(owner,17,0x1234,200),"matching owned generation can restore fixed capacity");
    Expect(!CanRestore(owner,18,0x1234,200)&&!CanRestore(owner,17,0x1235,200),"generation and actor reuse close restoration");
    Expect(!CanRestore(owner,17,0x1234,255),"foreign max write is not overwritten");
    owner.conflict=true;Expect(!CanRestore(owner,17,0x1234,200),"conflict is terminal for ownership");
    owner.conflict=false;owner.active=false;Expect(!CanRestore(owner,17,0x1234,200),"inactive owner is inert");
}
}
int main() {
    ScopeAndThresholds();HeaderAndOwnership();
    std::printf("RonsoPoolRt0: %s (%d checks, %d failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
