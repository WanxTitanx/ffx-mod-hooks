// Real service contracts with simulated native endpoints; no installed game.
#include <cstdio>
#if __has_include("../hooks/SeymourCompatibilityService.h")
#include "../hooks/SeymourCompatibilityService.h"
using namespace FfxHooks::SeymourCompatibility;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){ ++failures; std::printf("FAIL %u: %s\n",__LINE__,#x); } } while(false)
struct Fake {
    Pair pair{};
    unsigned originals=0,queries=0,writes=0,attempts=0,readCalls=0,failAt=0;
    bool admit=true,cleanup=true,changeScopeInOriginal=false,failRead=false;
    int result=0;
    Fake(){pair.scope={(std::uint64_t{9}<<32)|17u,3,17};pair.slots={10,11};
        for(unsigned i=0;i<2;++i){pair.gear[i].fill(0x5a);auto& g=pair.gear[i];
            g[2]=1;g[3]=0xac;g[4]=7;g[5]=static_cast<std::uint8_t>(i);g[6]=7;}}
    static bool Capture(void* p,Scope& out){auto& f=*static_cast<Fake*>(p);if(!f.admit)return false;out=f.pair.scope;return true;}
    static int Query(void* p,int,std::uint32_t){auto& f=*static_cast<Fake*>(p);++f.queries;
        if(f.changeScopeInOriginal)++f.pair.scope.commandGeneration;
        return f.result;}
    static void Original(void* p,int,std::uint8_t){++static_cast<Fake*>(p)->originals;}
    static bool Read(void* p,Pair& out){auto& f=*static_cast<Fake*>(p);++f.readCalls;
        if(f.failRead)return false;
        out=f.pair;return true;}
    static bool Current(void* p,const Pair& before){auto& f=*static_cast<Fake*>(p);
        return f.admit&&f.pair.scope==before.scope&&f.pair.slots==before.slots;}
    static bool CleanupCurrent(void* p,const Pair& before){auto& f=*static_cast<Fake*>(p);
        return f.cleanup&&f.pair.scope==before.scope&&f.pair.slots==before.slots;}
    static bool Compare(void* p,unsigned slot,const Gear& expected,std::uint8_t value){auto& f=*static_cast<Fake*>(p);
        if(++f.attempts==f.failAt||slot>=2||f.pair.gear[slot]!=expected)return false;
        f.pair.gear[slot][3]=value;++f.writes;return true;}
    Io Edit(){return {this,Read,Current,Compare};}
    Io Cleanup(){return {this,Read,CleanupCurrent,Compare};}
    QueryIo QueryOperations(){return {this,Query,Capture};}
    VisibilityOriginal Native(){return {this,Original};}
};
}
int main(){
    for(int actor:{0,6,7,8,263})for(auto command:{0x3017u,0x3024u,0x3025u,0x3026u,0x302au,0x3000u}){
        Fake f;CHECK(ServiceQuery(true,actor,command,f.QueryOperations())==(actor==7&&UnsafeCommand(command)?1:0));CHECK(f.queries==1);}
    for(int result:{-4,-2,-1,1,2,255}){Fake f;f.result=result;CHECK(ServiceQuery(true,7,0x3017,f.QueryOperations())==result);CHECK(f.queries==1);}
    {Fake f;CHECK(ServiceQuery(false,7,0x3017,f.QueryOperations())==0);CHECK(f.queries==1);}
    {Fake f;f.admit=false;CHECK(ServiceQuery(true,7,0x3017,f.QueryOperations())==0);CHECK(f.queries==1);}
    {Fake f;f.changeScopeInOriginal=true;CHECK(ServiceQuery(true,7,0x3017,f.QueryOperations())==0);CHECK(f.queries==1);}
    {Fake f;VisibilityLease lease;const auto before=f.pair;
        CHECK(lease.Apply(false,7,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::Inactive);
        CHECK(f.originals==1&&f.writes==0&&!lease.Pending()&&f.pair==before);}
    for(int actor:{-1,0,6,8,263}){Fake f;VisibilityLease lease;
        CHECK(lease.Apply(true,actor,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::Inactive);CHECK(f.originals==1&&f.readCalls==0);}
    {Fake f;VisibilityLease lease;const auto before=f.pair;
        CHECK(lease.Apply(true,7,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::Applied);
        CHECK(lease.Pending()&&f.originals==1);f.admit=false;
        CHECK(lease.Restore(f.Cleanup()));CHECK(!lease.Pending()&&f.pair==before);CHECK(lease.Restore(f.Cleanup()));}
    {Fake f;VisibilityLease lease;const auto before=f.pair;
        CHECK(lease.Apply(true,7,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::Applied);
        CHECK(lease.Apply(true,7,0,f.Native(),f.Edit(),f.Cleanup())==Outcome::Unchanged);
        CHECK(!lease.Pending()&&f.pair==before&&f.originals==2);}
    {Fake f;VisibilityLease lease;const auto before=f.pair;f.failAt=2;
        CHECK(lease.Apply(true,7,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::RolledBack);
        CHECK(!lease.Pending()&&f.pair==before&&f.originals==1);}
    {Fake f;VisibilityLease lease;CHECK(lease.Apply(true,7,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::Applied);
        ++f.pair.gear[0][4];const auto foreign=f.pair;CHECK(!lease.Restore(f.Cleanup()));
        CHECK(lease.Pending()&&f.pair==foreign);}
    {Fake f;VisibilityLease lease;CHECK(lease.Apply(true,7,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::Applied);
        f.cleanup=false;const auto before=f.pair;CHECK(!lease.Restore(f.Cleanup()));
        CHECK(lease.Apply(true,7,0,f.Native(),f.Edit(),f.Cleanup())==Outcome::Partial);CHECK(f.pair==before&&f.originals==2);}
    {Fake f;VisibilityLease lease;CHECK(lease.Apply(true,7,1,f.Native(),f.Edit(),f.Cleanup())==Outcome::Applied);
        f.failRead=true;CHECK(!lease.Restore(f.Cleanup()));CHECK(lease.Pending());f.failRead=false;
        CHECK(lease.Restore(f.Cleanup()));CHECK(!lease.Pending());}
    std::printf("SeymourCompatibilityServiceRt0: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL: SeymourCompatibilityService is not implemented");return 1;}
#endif
