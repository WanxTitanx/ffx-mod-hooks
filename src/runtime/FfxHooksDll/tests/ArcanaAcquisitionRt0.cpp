#include "../hooks/ArcanaAcquisition.h"
#include "../hooks/ArcanaWorldCore.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace FfxHooks::Arcana;
namespace {unsigned checks=0,failures=0;void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}}
int main(){
    using namespace Acquisition;
    std::array<unsigned char,kPayloadBytes> payload{};Facts facts;
    Check(!ReadFacts(nullptr,0,facts)&&!ReadFacts(payload.data(),payload.size()-1,facts),"partial native state cannot establish milestones");
    Check(ReadFacts(payload.data(),payload.size(),facts)&&!facts.freeTravel&&!facts.aeons&&!facts.darkAeons,"fresh save has no invented milestones");
    payload[0xBEC]=0x63;payload[0xBED]=0x0B; // 2915: partial airship destinations.
    ReadFacts(payload.data(),payload.size(),facts);Check(!facts.freeTravel,"first airship destination bit is not full travel");
    payload[0xBEC]=0xB8;payload[0xBED]=0x0B; // 3000: production free-travel event.
    ReadFacts(payload.data(),payload.size(),facts);Check(facts.freeTravel&&facts.story==3000,"native GameMoment opens free travel at the completed event");
    payload[0x3BE]=2;ReadFacts(payload.data(),payload.size(),facts);Check(!facts.omega,"Ultima Weapon is not Omega Weapon");
    payload[0x3BE]=4;payload[0x27B]=2;payload[0xC38]=1;
    for(unsigned actor=8;actor<=17;++actor)payload[0x55CC+actor*0x94+0x2C]=0x10;
    ReadFacts(payload.data(),payload.size(),facts);
    Check(facts.omega&&facts.wobbly&&facts.mirror&&facts.aeons==0x3FF00u,"independent native optional flags and all ten aeons decode without inventory writes");
    for(unsigned i=0;i<8;++i){payload[0x18F4+i]=255;payload[0xC89+i]=1;}
    ReadFacts(payload.data(),payload.size(),facts);Check(facts.darkAeons==0,"partial fights and legacy counters are not Dark completion");
    for(unsigned i=0;i<8;++i)payload[0xC89+i]=0x80;
    payload[0xC8E]=1; // Yojimbo has started, but the final sequence is unfinished.
    ReadFacts(payload.data(),payload.size(),facts);Check(facts.darkAeons==0xDF,"Dark Yojimbo progress cannot substitute for its final completion bit");
    payload[0xC8E]=0x80;const auto unchanged=payload;
    ReadFacts(payload.data(),payload.size(),facts);Check(facts.darkAeons==255&&payload==unchanged,"all eight terminal masks decode without changing native progress");
    for(unsigned id=0;id<78;++id)Check(std::strlen(Requirement(id))>20,"every locked card describes a concrete collection requirement");
    std::printf("ArcanaAcquisitionRt0: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
