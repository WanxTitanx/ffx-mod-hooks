// Jarvis-HOOK: immutable companion publication leases; no native or file I/O.
#include <cstdio>
#if !__has_include("../hooks/SphereGridProgress8CommitCore.h")
int main(){std::puts("FAIL: Grid8 verified-save consumer is missing");return 1;}
#else
#include "../hooks/SphereGridProgress8CommitCore.h"
#include <atomic>
#include <thread>
#include <vector>
namespace G=FfxHooks::SphereGridProgress8;
namespace C=FfxHooks::SphereGridProgress8Commit;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* reason){
    ++checks;if(!value){++failures;std::printf("FAIL: %s\n",reason);}
}
static C::Scope Scope(){C::Scope s{};s.session=11;s.thread=7;s.layoutGeneration=13;s.request=17;return s;}
static G::Key Key(unsigned image=2){
    G::Key k{};k.path.fill(1);k.image.fill(static_cast<unsigned char>(image));
    k.layout.fill(3);k.contents.fill(4);return k;
}
static G::Snapshot State(){
    G::Snapshot s;s.nodes.resize(1003,{1,0x81});s.links.resize(1024,0x80);
    s.cursors.fill(860);s.cursors[7]=1002;s.tilt=2;s.zoom=3;return s;
}
static void Normal(){
    C::Queue q;const auto scope=Scope();const auto key=Key();auto source=State();const auto before=source;
    Check(q.Stage(1,scope,key,source),"stage accepts one valid scope-bound snapshot");
    source.nodes[0].mask=0;source.cursors[7]=0;
    C::Publication publication{};
    Check(q.Verify(1,scope,key,publication),"matching verification issues publication once");
    G::Snapshot decoded;
    Check(publication.bytes&&G::Decode(*publication.bytes,key,decoded)&&decoded==before,
          "staged bytes are immutable and preserve Seymour's high cursor and masks");
    Check(q.Current(publication,scope),"publication remains usable during disk admission checks");
    C::Publication replay{};Check(!q.Verify(1,scope,key,replay),"verified callback replay cannot issue a second publication");
    Check(q.Current(publication,scope),"replayed verification does not revoke the already-issued lease");
    auto wrong=publication;wrong.key.image[0]^=1;
    Check(!q.Current(wrong,scope),"mutated key is not a publication lease");
    wrong=publication;wrong.bytes=std::make_shared<const G::Bytes>(*publication.bytes);
    Check(!q.Current(wrong,scope),"an equal but independently supplied byte buffer does not own the lease");
    q.Finish(wrong);Check(q.Current(publication,scope),"foreign finish cannot retire the real lease");
    q.Finish(publication);Check(!q.Current(publication,scope),"finish retires the exact publication");
    Check(!q.Stage(1,scope,key,before),"finished tickets cannot be replayed");
}
static void Rejections(){
    for(unsigned mode=0;mode<10;++mode){
        C::Queue q;auto scope=Scope();auto key=Key();const auto state=State();
        Check(q.Stage(100,scope,key,state),"negative fixture stages a real candidate");
        auto otherScope=scope;auto otherKey=key;
        if(mode==0)++otherScope.session;
        if(mode==1)++otherScope.thread;
        if(mode==2)++otherScope.layoutGeneration;
        if(mode==3)++otherScope.request;
        if(mode==4)otherKey.path[0]^=1;
        if(mode==5)otherKey.image[0]^=1;
        if(mode==6)otherKey.layout[0]^=1;
        if(mode==7)otherKey.contents[0]^=1;
        if(mode==8)q.Abort(100);
        if(mode==9)q.Invalidate();
        C::Publication result{};
        Check(!q.Verify(100,otherScope,otherKey,result)&&!result.bytes,
              "mismatched session/thread/layout/control/key or cancellation is rejected");
        Check(!q.Verify(100,scope,key,result),"failed verification cannot later revive the candidate");
    }
    for(unsigned mode=0;mode<8;++mode){
        C::Queue q;auto scope=Scope();auto key=Key();auto state=State();
        if(mode==0)scope.session=0;
        if(mode==1)scope.thread=0;
        if(mode==2)scope.layoutGeneration=0;
        if(mode==3)scope.request=0;
        if(mode==4)key.image={};
        if(mode==5)state.cursors[7]=1003;
        if(mode==6)state.nodes[0]={255,0x80};
        if(mode==7)state.nodes.clear();
        Check(!q.Stage(1,scope,key,state),"invalid snapshot or scope never stages");
    }
    C::Queue q;const auto scope=Scope();const auto key=Key();const auto state=State();
    Check(!q.Stage(0,scope,key,state),"zero native ticket is invalid");
    Check(q.Stage(5,scope,key,state),"duplicate fixture stages");
    Check(!q.Stage(5,scope,key,state),"duplicate staging invalidates rather than replacing provenance");
    C::Publication p{};Check(!q.Verify(5,scope,key,p),"duplicate staging cannot leave old provenance eligible");
}
static void Revocation(){
    for(unsigned mode=0;mode<6;++mode){
        C::Queue q;auto scope=Scope();const auto key=Key();const auto state=State();C::Publication p{};
        Check(q.Stage(1,scope,key,state)&&q.Verify(1,scope,key,p),"revocation fixture obtains a real lease");
        if(mode==0)q.Abort(1);
        if(mode==1)q.Invalidate();
        if(mode==2)q.RequestStop();
        if(mode==3)++scope.session;
        if(mode==4)++scope.layoutGeneration;
        if(mode==5)++scope.request;
        Check(!q.Current(p,scope),"cancellation or scope change closes disk publication admission");
    }
    C::Queue q;const auto scope=Scope();const auto state=State();C::Publication old{},fresh{};
    Check(q.Stage(1,scope,Key(),state)&&q.Verify(1,scope,Key(),old),"old lease exists");
    q.Invalidate();auto next=scope;++next.session;
    Check(q.Stage(2,next,Key(9),state)&&q.Verify(2,next,Key(9),fresh),"new save receives its own lease");
    q.Abort(1);q.Finish(old);
    Check(q.Current(fresh,next)&&!q.Current(old,next),"old cleanup cannot retire a later save");
    q.RequestStop();
    Check(!q.Stage(3,next,Key(),state),"nonblocking stop prevents future staging");
    q.Invalidate();Check(!q.Stage(4,next,Key(),state),"normal invalidation cannot undo permanent stop");
}
static void CapacityAndConcurrency(){
    C::Queue q;const auto scope=Scope();const auto key=Key();const auto state=State();
    std::array<C::Publication,C::Queue::Capacity> publications{};
    for(std::size_t i=0;i<publications.size();++i)
        Check(q.Stage(i+1,scope,key,state),"bounded slot available");
    Check(!q.Stage(100,scope,key,state),"capacity overflow rejects instead of evicting another save");
    for(std::size_t i=0;i<publications.size();++i)
        Check(q.Verify(i+1,scope,key,publications[i]),"overflow preserves each original candidate");
    Check(!q.Stage(101,scope,key,state),"disk publication leases also hold bounded capacity");
    q.Finish(publications[0]);Check(q.Stage(102,scope,key,state),"finished lease releases one slot");
    Check(!q.Stage(100,scope,key,state),"a rejected earlier serial cannot be retried as a new write");
    C::Queue concurrent;Check(concurrent.Stage(1,scope,key,state),"concurrent verification fixture");
    std::atomic<unsigned> accepted{0};std::array<C::Publication,8> claims{};
    std::vector<std::thread> threads;
    for(unsigned i=0;i<claims.size();++i)threads.emplace_back([&,i]{
        if(concurrent.Verify(1,scope,key,claims[i]))++accepted;
    });
    for(auto& thread:threads)thread.join();
    Check(accepted==1,"concurrent duplicate confirmation grants exactly one publication");
    concurrent.RequestStop();for(const auto& p:claims)Check(!concurrent.Current(p,scope),"stop closes every concurrent claim");
}
static void RoundTrips(){
    C::Queue q;auto scope=Scope();auto state=State();
    for(unsigned n=0;n<512;++n){
        const auto key=Key(n%250+1);state.cursors[7]=static_cast<std::uint16_t>(n%state.nodes.size());
        state.nodes[n%state.nodes.size()].mask=static_cast<unsigned char>(n|0x80);
        C::Publication p{};
        Check(q.Stage(n+1,scope,key,state)&&q.Verify(n+1,scope,key,p),"sequential verified snapshot");
        G::Snapshot read;Check(G::Decode(*p.bytes,key,read)&&read==state,"sequential immutable snapshot matches all eight characters");
        q.Finish(p);Check(!q.Current(p,scope),"sequential lease finishes once");
    }
}
int main(){Normal();Rejections();Revocation();CapacityAndConcurrency();RoundTrips();
    std::printf("SphereGridProgress8CommitRt0: %u/%u passed\n",checks-failures,checks);return failures?1:0;}
#endif
