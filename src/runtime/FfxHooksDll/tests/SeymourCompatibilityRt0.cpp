// Offline contracts: no process attachment, saves or gameplay patching.
#include <array>
#include <cstdint>
#include <cstdio>
#if __has_include("../hooks/SeymourCompatibilityCore.h")
#include "../hooks/SeymourCompatibilityCore.h"
using namespace FfxHooks::SeymourCompatibility;
namespace {
unsigned checks=0,failures=0;
#define CHECK(...) do {++checks;if(!(__VA_ARGS__)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#__VA_ARGS__);}} while(false)
Scope Owned(){return {(std::uint64_t{3}<<32)|7u,11,7};}
struct Fake {
    Pair image{};unsigned reads=0,writes=0,attempts=0,failAt=0,readFailAt=0;
    bool readOk=true,current=true,revokeAfterFirst=false,foreignFirst=false,revokeAfterReadback=false;
    Fake(){image.scope=Owned();image.slots={12,13};
        for(unsigned i=0;i<2;++i){auto& g=image.gear[i];g.fill(0x5a);
            g[2]=1;g[3]=0xac;g[4]=7;g[5]=static_cast<std::uint8_t>(i);g[6]=7;}}
    static bool Read(void* p,Pair& out){auto& f=*static_cast<Fake*>(p);++f.reads;
        if(!f.readOk||f.reads==f.readFailAt)return false;
        out=f.image;if(f.reads>1&&f.revokeAfterReadback)f.current=false;return true;}
    static bool Current(void* p,const Pair& expected){auto& f=*static_cast<Fake*>(p);return f.current&&f.image.scope==expected.scope&&f.image.slots==expected.slots;}
    static bool Write(void* p,unsigned slot,const Gear& expected,std::uint8_t value){auto& f=*static_cast<Fake*>(p);++f.attempts;
        if(f.failAt==f.attempts)return false;
        if(slot>=2||f.image.gear[slot]!=expected)return false;
        f.image.gear[slot][3]=value;++f.writes;
        if(f.writes==1&&f.revokeAfterFirst)f.current=false;
        if(f.writes==1&&f.foreignFirst)f.image.gear[slot][4]=1;
        return true;}
    Io Operations(){return {this,Read,Current,Write};}
};
}
int main(){
    const auto scope=Owned();CHECK(scope.Valid());CHECK(!Scope{}.Valid());
    CHECK(!Scope{(std::uint64_t{UINT32_MAX}<<32)|7,11,7}.Valid());
    for(int actor=-2;actor<32;++actor)for(std::uint32_t command=0;command<0x4000;++command){
        const bool dangerous=command==0x3017||command==0x3024||command==0x3025||command==0x3026||command==0x302a;
        CHECK(FilterCommand(0,actor,command,true,scope,scope)==(actor==7&&dangerous?1:0));
    }
    for(auto command:{0x3017u,0x3024u,0x3025u,0x3026u,0x302au}){
        for(int original:{-4,-2,-1,1,2,255})CHECK(FilterCommand(original,7,command,true,scope,scope)==original);
        CHECK(FilterCommand(0,7,command,false,scope,scope)==0);
        CHECK(FilterCommand(0,7,command,true,{},scope)==0);
        auto changed=scope;++changed.commandGeneration;CHECK(FilterCommand(0,7,command,true,scope,changed)==0);
        changed=scope;changed.epoch+=(std::uint64_t{1}<<32);CHECK(FilterCommand(0,7,command,true,scope,changed)==0);
        CHECK(FilterCommand(0,263,command,true,scope,scope)==0);
        CHECK(FilterCommand(0,7,command|0x10000,true,scope,scope)==0);
    }
    for(unsigned flags=0;flags<256;++flags)for(unsigned input=0;input<256;++input){
        const auto expected=static_cast<std::uint8_t>((flags&~2u)|((input&1u)<<1));
        CHECK(VisibilityFlags(static_cast<std::uint8_t>(flags),static_cast<std::uint8_t>(input))==expected);
    }
    {Fake f;const auto before=f.image;CHECK(ApplyVisibility(false,7,1,f.Operations())==Outcome::Inactive);CHECK(f.reads==0&&f.image==before);}
    for(int actor:{-1,0,1,6,8,18,263}){Fake f;CHECK(ApplyVisibility(true,actor,1,f.Operations())==Outcome::Inactive);CHECK(f.reads==0);}
    {Fake f;auto before=f.image;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Applied);CHECK(f.writes==2);
        for(auto& g:before.gear){g[3]|=2;}CHECK(f.image==before);
        CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Unchanged);CHECK(f.writes==2);
        CHECK(ApplyVisibility(true,7,0,f.Operations())==Outcome::Applied);CHECK(f.writes==4);}
    for(unsigned invalid=0;invalid<10;++invalid){Fake f;
        if(invalid==0)f.image.slots[0]=200;
        if(invalid==1)f.image.slots[1]=12;
        if(invalid==2)f.image.gear[1][2]=0;
        if(invalid==3)f.image.gear[1][4]=6;
        if(invalid==4)f.image.gear[1][5]=0;
        if(invalid==5)f.image.gear[1][6]=255;
        if(invalid==6)f.image.scope={};
        if(invalid==7)f.image.scope.thread=8;
        if(invalid==8)f.readOk=false;
        if(invalid==9)f.current=false;
        const auto before=f.image;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Rejected);CHECK(f.writes==0&&f.image==before);}
    {Fake f;f.image.slots={255,255};CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Unchanged);CHECK(f.writes==0);}
    {Fake f;f.image.slots[0]=255;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Applied);CHECK(f.writes==1);}
    {Fake f;const auto before=f.image;f.failAt=1;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::RolledBack);CHECK(f.image==before);}
    {Fake f;const auto before=f.image;f.failAt=2;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::RolledBack);CHECK(f.image==before);}
    {Fake f;f.revokeAfterFirst=true;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Partial);CHECK(f.writes==1&&f.image.gear[1][3]==0xac);}
    {Fake f;f.foreignFirst=true;f.failAt=2;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Partial);CHECK(f.writes==1&&f.image.gear[0][4]==1);}
    {Fake f;f.foreignFirst=true;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Partial);CHECK(f.image.gear[0][4]==1);}
    {Fake f;f.readFailAt=2;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Partial);CHECK(f.writes==2&&f.reads==2);}
    {Fake f;f.revokeAfterReadback=true;CHECK(ApplyVisibility(true,7,1,f.Operations())==Outcome::Partial);CHECK(f.writes==2&&!f.current);}
    std::printf("SeymourCompatibilityRt0: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL: SeymourCompatibilityCore is not implemented");return 1;}
#endif
