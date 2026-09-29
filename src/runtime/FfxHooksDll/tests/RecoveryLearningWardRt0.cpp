#include "../hooks/GridLearnedCore.h"
#include "../hooks/NulWardCore.h"
#include <cstdio>
#include <limits>

namespace G=FfxHooks::GridLearned;
namespace W=FfxHooks::NulWard;
namespace {
int checks=0,failed=0;
void Check(bool value,const char* label){++checks;if(!value){++failed;std::fprintf(stderr,"FAIL %s\n",label);}}
G::Identity Id(unsigned path,unsigned disk){G::Identity id{};id.path[0]=static_cast<unsigned char>(path);id.image[0]=static_cast<unsigned char>(disk);return id;}
}
int main(){
    G::State state;
    Check(!state.Ready(),"learning starts unbound");
    Check(state.Set(3,323,true)==G::Change::Unbound,"unbound learning rejected");
    Check(!state.Bind({}),"empty identity rejected");
    const auto saveA=Id(1,2),saveB=Id(3,2);
    Check(state.Bind(saveA),"save A binds");
    Check(state.Set(3,323,true)==G::Change::Changed,"Kimahri learns blue command");
    Check(state.Has(3,323)&&!state.Has(1,323),"learned commands are character-local");
    Check(state.Set(3,323,true)==G::Change::Unchanged,"duplicate learn is idempotent");
    Check(state.Set(7,323,true)==G::Change::Invalid,"outside playable roster rejected");
    Check(state.Set(3,384,true)==G::Change::Invalid,"outside command extension rejected");
    Check(state.Set(3,95,true)==G::Change::Invalid,"native small-command bank remains native");
    Check(state.Set(1,366,true)==G::Change::Changed,"Yuna learns shadow-bank command");
    G::Record record{};Check(G::Encode(state,record),"bounded record serializes");
    G::Learned learned{};Check(G::Decode(record,saveA,learned),"matching identity and integrity roundtrip");
    G::State restored;Check(restored.Bind(saveA,&learned)&&restored.Has(3,323)&&restored.Has(1,366),"learned bits restored exactly");
    Check(!G::Decode(record,saveB,learned),"identical save bytes under a different slot cannot import learning");
    Check(!G::Decode(record,Id(1,4),learned),"different save bytes under same path cannot import learning");
    for(std::size_t i=0;i<record.size();++i){auto damaged=record;damaged[i]^=1;Check(!G::Decode(damaged,saveA,learned),"every record byte is integrity-bound");}
    Check(state.Bind(saveB),"switch to save B");
    Check(!state.Has(3,323)&&!state.Has(1,366),"save switch clears old commands before publication");
    Check(state.Set(1,352,true)==G::Change::Changed,"shadow first command is supported");
    Check(state.Set(1,383,true)==G::Change::Changed,"shadow final command is supported");
    Check(state.Set(1,383,false)==G::Change::Changed&&!state.Has(1,383),"explicit revocation stays character-local");
    state.Clear();Check(!state.Ready()&&!state.Has(1,352),"reset closes learning admission");
    Check(!G::Encode(state,record),"unbound state never persists");
    state.BeginSession();
    Check(state.Ready()&&state.Set(3,323,true)==G::Change::Changed,"new game owns empty transient learning");
    Check(!G::Encode(state,record),"transient learning cannot persist before native save");
    const auto firstSaveWords=state.Words();
    Check(state.Bind(saveA,&firstSaveWords)&&G::Encode(state,record),"first real save binds transient learning");
    state.BeginSession();
    Check(!state.Has(3,323),"new game reset cannot inherit previous learning");

    W::Bank wards;
    const W::Actor a{2,0x1000,17},replacement{2,0x1000,18},other{3,0x2000,19};
    Check(!wards.Cast(0,a,W::kHoly),"no ward outside a known battle generation");
    Check(wards.Cast(1,a,W::kHoly),"cast Holy ward");
    Check(wards.Cast(1,a,W::kDark),"cast independent Dark ward");
    Check(wards.Blocks(1,a)==(W::kHoly|W::kDark),"two independent protections coexist");
    Check(wards.Filter(1,a,W::kHoly,100,0,true)==0,"matching HP damage blocked");
    Check(wards.Blocks(1,a)==W::kDark,"Holy hit preserves unrelated Dark ward");
    Check(wards.Filter(1,a,W::kHoly,100,0,true)==100,"consumed ward cannot block again");
    Check(wards.Filter(1,a,W::kDark,-100,0,true)==-100&&wards.Blocks(1,a)==W::kDark,"healing does not consume a ward");
    Check(wards.Filter(1,a,W::kDark,100,1,true)==100&&wards.Blocks(1,a)==W::kDark,"MP component is not a second ward consumer");
    Check(wards.Filter(1,a,W::kDark,100,0,false)==100&&wards.Blocks(1,a)==W::kDark,"observe mode never consumes or changes damage");
    Check(wards.Filter(1,other,W::kDark,100,0,true)==100,"another actor cannot borrow a ward");
    Check(wards.Filter(1,replacement,W::kDark,100,0,true)==100,"reused actor address with changed identity loses protection");
    Check(wards.Blocks(1,a)==0,"identity mismatch retires stale slot state");
    Check(wards.Cast(1,a,W::kHoly|W::kDark),"cast both supported protections");
    Check(wards.Filter(1,a,W::kHoly|W::kDark,1,0,true)==0&&wards.Blocks(1,a)==0,"multi-element hit consumes only matching available wards");
    Check(wards.Cast(1,a,W::kHoly),"cast before battle transition");
    Check(wards.Filter(2,a,W::kHoly,100,0,true)==100,"next battle does not inherit pointer-keyed protection");
    Check(!wards.Cast(2,{64,0x1000,1},W::kHoly),"out-of-range actor slot rejected");
    Check(!wards.Cast(2,{0,0,1},W::kHoly),"null actor rejected");
    Check(!wards.Cast(2,a,1),"unsupported element cannot create a ward");
    wards.Clear();Check(wards.Blocks(2,a)==0,"explicit retirement clears protections");
    const auto code=W::BuildWritebackGateway(0x10000000,0x20000000,0x30000000);
    Check(code.size>40&&code.size<code.bytes.size(),"bounded native gateway emitted");
    Check(code.bytes[0]==0x9c&&code.bytes[1]==0x60,"gateway snapshots native flags and all GPRs");
    Check(code.bytes[code.size-6]==0xff&&code.bytes[code.size-5]==0x25,"gateway uses retained original-pointer storage");
    std::printf("RecoveryLearningWardRt0: %d/%d passed\n",checks-failed,checks);
    return failed?1:0;
}
