#include "../hooks/ArcanaAcquisition.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
int main(){
    std::array<unsigned char,Acquisition::kPayloadBytes> payload{};State state;
    Check(Acquisition::Reconcile(state,nullptr,0)==0&&state.revision==0,"unobserved native state grants nothing");
    Check(Acquisition::Reconcile(state,payload.data(),payload.size()-1)==0,"partial native snapshot grants nothing");
    Check(Acquisition::Reconcile(state,payload.data(),payload.size())==1&&state.acquired[0],"pilgrimage starts with The Fool only");
    auto revision=state.revision;
    Check(Acquisition::Reconcile(state,payload.data(),payload.size())==0&&state.revision==revision,"repeated milestone has one permanent receipt");
    for(unsigned actor=8;actor<=12;++actor){
        payload[0x55CC+actor*0x94+0x2C]=0x10;
        Check(Acquisition::Reconcile(state,payload.data(),payload.size())>0,"obtained story Aeon reconciles temple rewards");
    }
    Check(!state.acquired[15]&&!state.acquired[21],"story completion does not grant challenge cards or World");
    payload[0xC38]=1;for(unsigned actor=13;actor<18;++actor)payload[0x55CC+actor*0x94+0x2C]=0x10;
    payload[0x3D10]=payload[0x3D11]=payload[0x3D12]=255;payload[0x3D13]=3;
    Acquisition::Reconcile(state,payload.data(),payload.size());
    unsigned owned=0;for(auto value:state.acquired)owned+=value;
    Check(owned==73,"temples and optional collection milestones account for all 73 nonchallenge cards");
    payload[0x400]=199;Acquisition::Reconcile(state,payload.data(),payload.size());Check(!state.acquired[19],"Sun requires 200 consecutive dodges");
    payload[0x400]=200;Acquisition::Reconcile(state,payload.data(),payload.size());Check(state.acquired[19],"Sun records the lightning challenge");
    payload[0xC89+4]=0x80;Acquisition::Reconcile(state,payload.data(),payload.size());Check(state.acquired[16]&&!state.acquired[20],"one Dark victory grants only its named challenge");
    for(unsigned i=0;i<8;++i)payload[0x18F4+i]=1;
    const auto snapshot=payload;Acquisition::Reconcile(state,payload.data(),payload.size());
    owned=0;for(auto value:state.acquired)owned+=value;
    Check(owned==78&&state.acquired[21]&&Validate(state)==Error::None,"old completed save reconciles all cards and noncircular World");
    Check(payload==snapshot,"reconciliation cannot write native progress");
    revision=state.revision;payload={};Check(Acquisition::Reconcile(state,payload.data(),payload.size())==0&&state.revision==revision,"already earned cards survive a missing or reset event flag");
    for(unsigned id=0;id<78;++id)Check(std::strlen(Acquisition::Requirement(id))>8,"every locked card exposes a concrete acquisition requirement");
    std::printf("ArcanaAcquisitionRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
