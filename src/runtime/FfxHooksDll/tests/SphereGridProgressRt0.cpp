#include <cstdio>
#if __has_include("../hooks/SphereGridProgressCore.h")
#include "../hooks/SphereGridProgressCore.h"
using namespace FfxHooks::SphereGridProgress;
static int checks=0,failed=0;
#define CHECK(x) do { ++checks; if(!(x)){++failed;std::printf("FAIL line %d: %s\n",__LINE__,#x);} } while(false)
static Key KeyFor(unsigned n=1){Key k{};k.path.fill(static_cast<unsigned char>(n));k.image.fill(2);k.layout.fill(3);k.contents.fill(4);return k;}
static Snapshot State(unsigned nodes=861,unsigned links=881){
    Snapshot s;s.nodes.resize(nodes);s.links.resize(links);
    for(unsigned i=0;i<nodes;++i)s.nodes[i]={35,static_cast<unsigned char>(i%128)};
    for(unsigned i=0;i<7;++i)s.cursors[i]=static_cast<unsigned short>(nodes-1-i);
    s.tilt=2;s.zoom=3;return s;
}
int main(){
    const auto key=KeyFor();const auto state=State();Bytes encoded;
    CHECK(Encode(key,state,encoded));Snapshot restored;CHECK(Decode(encoded,key,restored));CHECK(restored==state);
    CHECK(encoded.size()==160+861*2+881+16+4);
    CHECK(encoded[160+861*2+881]==0x5c && encoded[160+861*2+881+1]==3);
    for(unsigned n:{1024u,1025u,4096u,16384u}){auto large=State(n,n-1);CHECK(Encode(key,large,encoded));CHECK(Decode(encoded,key,restored));CHECK(restored==large);}
    CHECK(Encode(key,state,encoded));const auto original=encoded;
    for(unsigned offset:{0u,8u,12u,16u,20u,24u,28u,32u,64u,96u,128u,160u}){
        auto bad=original;bad[offset]^=1;auto before=restored;CHECK(!Decode(bad,key,restored));CHECK(restored==before);}
    auto other=key;other.path[0]^=1;CHECK(!Decode(original,other,restored));
    other=key;other.image[0]^=1;CHECK(!Decode(original,other,restored));
    other=key;other.layout[0]^=1;CHECK(!Decode(original,other,restored));
    other=key;other.contents[0]^=1;CHECK(!Decode(original,other,restored));
    for(unsigned field=0;field<6;++field){auto bad=state;
        if(field==0)bad.nodes[860].content=130;
        if(field==1)bad.nodes[860].mask=128;
        if(field==2)bad.links[880]=128;
        if(field==3)bad.cursors[6]=861;
        if(field==4)bad.tilt=3;
        if(field==5)bad.zoom=4;
        auto bytes=original;CHECK(!Encode(key,bad,bytes));CHECK(bytes==original);}
    auto null=state;null.nodes[860]={255,0};CHECK(Encode(key,null,encoded));CHECK(Decode(encoded,key,restored));CHECK(restored==null);
    auto tooBig=State(16385,0);CHECK(!Encode(key,tooBig,encoded));CHECK(!Encode(Key{},state,encoded));
    Pending pending;Bytes out;CHECK(!pending.Stage(1,key,state,1,7));CHECK(pending.Begin(1,7));
    CHECK(pending.Stage(1,key,state,1,7));auto changed=state;changed.nodes[860].mask=127;
    CHECK(pending.Verify(1,key,1,7,out));CHECK(Decode(out,key,restored));CHECK(restored==state);
    CHECK(!pending.Verify(1,key,1,7,out));CHECK(!pending.Stage(2,key,state,1,8));
    CHECK(pending.Stage(2,key,state,1,7));CHECK(!pending.Verify(2,KeyFor(2),1,7,out));CHECK(!pending.Verify(2,key,1,7,out));
    CHECK(pending.Stage(3,key,state,1,7));pending.Abort(3);CHECK(!pending.Verify(3,key,1,7,out));
    CHECK(pending.Stage(4,key,state,1,7));CHECK(pending.Begin(2,7));CHECK(!pending.Verify(4,key,2,7,out));
    CHECK(pending.Stage(5,key,state,2,7));CHECK(!pending.Stage(5,key,changed,2,7));CHECK(!pending.Verify(5,key,2,7,out));
    CHECK(pending.Stage(6,key,state,2,7));CHECK(!pending.Verify(6,key,2,8,out));
    CHECK(pending.Stage(7,key,state,2,7));pending.RequestStop();CHECK(!pending.Verify(7,key,2,7,out));CHECK(!pending.Begin(3,7));
    std::printf("SphereGridProgressRt0: %d/%d checks passed\n",checks-failed,checks);return failed?1:0;
}
#else
int main(){std::puts("FAIL: SphereGridProgressCore is not implemented");return 1;}
#endif
