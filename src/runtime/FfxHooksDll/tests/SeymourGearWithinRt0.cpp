#include <cstdio>
#include <algorithm>
#include <random>
#include <stdexcept>
#if __has_include("../hooks/SeymourGearWithinCore.h")
#include "../hooks/SeymourGearWithinCore.h"
namespace S=FfxHooks::SeymourGearSort;
unsigned total=0,failures=0;
#define CHECK(x) do{++total;if(!(x)){++failures;if(failures<24)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
struct Fake {
    S::Snapshot image;
    std::array<unsigned,200> ids{},originalIds{};
    std::array<S::Gear,200> originalGear{};
    std::array<std::uint16_t,14> nativeCounts{};
    unsigned reads=0,refreshes=0,counterReads=0,within=0,swaps=0,currentCalls=0;
    unsigned failRead=0,failCurrent=0;
    int fault=0;
    bool current=true;
    explicit Fake(unsigned count=32){
        image.generation=5;image.thread=7;image.count=count;image.equipped.fill(255);
        for(unsigned i=0;i<200;++i)ids[i]=i;
        for(unsigned i=0;i<count;++i){
            // Scatter handles in physical inventory while groups are contiguous.
            const unsigned slot=(i*73u)%200u;image.rows[i]=0xABCD5000u|slot;
            auto& g=image.gear[slot];g[0]=static_cast<std::uint8_t>(i);g[2]=1;
            const unsigned key=count?(i*16/count):0;
            g[4]=static_cast<std::uint8_t>(key/2);g[5]=static_cast<std::uint8_t>(key%2);g[6]=255;g[11]=4;
        }
        for(unsigned i=0;i<count;++i){
            const auto slot=S::Slot(image.rows[i]);auto& g=image.gear[slot];
            const unsigned pair=g[4]*2+g[5];
            if(image.equipped[pair]==255){image.equipped[pair]=static_cast<std::uint8_t>(slot);g[6]=g[4];}
        }
        Seal();
    }
    void Seal(){originalGear=image.gear;originalIds=ids;}
    static bool Read(void* p,S::Snapshot& out){auto& f=*static_cast<Fake*>(p);++f.reads;
        if(f.failRead==f.reads)return false;
        out=f.image;return true;}
    static bool Current(void* p,const S::Snapshot& value){auto& f=*static_cast<Fake*>(p);++f.currentCalls;
        return f.current&&f.failCurrent!=f.currentCalls&&value.generation==f.image.generation&&value.thread==f.image.thread;}
    static bool Refresh(void* p){auto& f=*static_cast<Fake*>(p);++f.refreshes;
        S::Counts all{};CHECK(S::Count(f.image,all));std::copy_n(all.begin(),14,f.nativeCounts.begin());
        if(f.fault==1)++f.nativeCounts[0];
        if(f.fault==2)f.image.gear[199][7]^=1;
        if(f.fault==3)f.current=false;
        if(f.fault==4)return false;
        if(f.fault==15)throw std::runtime_error("native refresh exception");
        return true;
    }
    static bool Counts(void* p,std::array<std::uint16_t,14>& out){auto& f=*static_cast<Fake*>(p);++f.counterReads;
        if(f.fault==5)return false;
        out=f.nativeCounts;return true;}
    void Swap(unsigned rowA,unsigned rowB){
        ++swaps;const unsigned a=S::Slot(image.rows[rowA]),b=S::Slot(image.rows[rowB]);
        std::swap(image.gear[a],image.gear[b]);std::swap(ids[a],ids[b]);
        for(auto& slot:image.equipped){if(slot==a)slot=static_cast<std::uint8_t>(b);else if(slot==b)slot=static_cast<std::uint8_t>(a);}
    }
    static bool Within(void* p,unsigned start,unsigned count){auto& f=*static_cast<Fake*>(p);++f.within;
        CHECK(count>1&&start<f.image.count&&count<=f.image.count-start);
        const auto owner=f.image.gear[S::Slot(f.image.rows[start])][4];
        const auto type=f.image.gear[S::Slot(f.image.rows[start])][5];
        for(unsigned i=0;i<count;++i){const auto& g=f.image.gear[S::Slot(f.image.rows[start+i])];CHECK(g[4]==owner&&g[5]==type);}
        f.Swap(start,start+count-1);
        if(f.fault==6)f.image.gear[S::Slot(f.image.rows[start])][7]^=1;
        if(f.fault==7)f.image.gear[199][7]^=1;
        if(f.fault==8)f.image.rows[0]^=0x10000;
        if(f.fault==9)++f.image.generation;
        if(f.fault==10)f.current=false;
        if(f.fault==11)return false;
        if(f.fault==12)++f.nativeCounts[0];
        if(f.fault==13)throw std::runtime_error("native within exception");
        if(f.fault==14)f.image.equipped[owner*2+type]=255;
        return true;
    }
    S::WithinIo Io(){return {this,Read,Current,Refresh,Counts,Within};}
    void Identity(){for(unsigned i=0;i<200;++i)CHECK(image.gear[i]==originalGear[ids[i]]);}
};
int main(){
    {Fake f;CHECK(S::SortWithin(false,{})==S::Result::Inactive);CHECK(!f.refreshes);CHECK(S::SortWithin(true,{})==S::Result::Rejected);}
    for(unsigned count=0;count<=200;++count){Fake f(count);const auto before=f.image;
        const auto result=S::SortWithin(true,f.Io());CHECK(result==S::Result::Applied||result==S::Result::Unchanged);
        CHECK(f.refreshes==1);S::Counts expected{};CHECK(S::Count(before,expected));
        CHECK(std::equal(f.nativeCounts.begin(),f.nativeCounts.end(),expected.begin()));
        unsigned calls=0;for(auto n:expected)if(n>1)++calls;CHECK(calls==f.within);f.Identity();CHECK(S::Valid(f.image));
    }
    for(int fault=1;fault<=15;++fault){Fake f;f.fault=fault;bool caught=false;S::Result result=S::Result::Inactive;
        try{result=S::SortWithin(true,f.Io());}catch(const std::runtime_error&){caught=true;}
        if(fault==13||fault==15)CHECK(caught);else CHECK(result==S::Result::Partial);
        if(fault<=5||fault==15)CHECK(f.within==0);else CHECK(f.within==1);
    }
    for(unsigned mode=0;mode<8;++mode){Fake f;
        if(mode==0)f.image.gear[S::Slot(f.image.rows[0])][11]=5;
        if(mode==1)f.Swap(0,31);
        if(mode==2)f.image.count=201;
        if(mode==3)f.image.rows[1]=f.image.rows[0];
        if(mode==4)f.image.equipped[0]=201;
        if(mode==5)f.image.thread=0;
        if(mode==6)f.failRead=1;
        if(mode==7)f.current=false;
        CHECK(S::SortWithin(true,f.Io())==S::Result::Rejected);CHECK(!f.refreshes&&!f.within);
    }
    {Fake f(40);const unsigned a=S::Slot(f.image.rows[36]),b=S::Slot(f.image.rows[37]);
        f.image.gear[b]=f.image.gear[a];f.image.gear[b][6]=255;f.image.gear[a][6]=255;f.Seal();
        CHECK(S::SortWithin(true,f.Io())==S::Result::Applied);f.Identity();}
    // A tail of unsupported owners remains untouched and is never a native group.
    {Fake f(40);for(unsigned i=38;i<40;++i){auto& g=f.image.gear[S::Slot(f.image.rows[i])];g[4]=17;g[6]=255;}
        f.image.equipped[15]=255;f.Seal();const auto before=f.image;
        CHECK(S::SortWithin(true,f.Io())==S::Result::Applied);
        for(unsigned i=38;i<40;++i)CHECK(f.image.gear[S::Slot(f.image.rows[i])]==before.gear[S::Slot(before.rows[i])] );
        f.Identity();}
    // All failures after a mutating helper begins are explicit partial operations.
    {Fake baseline;CHECK(S::SortWithin(true,baseline.Io())==S::Result::Applied);
        for(unsigned at=1;at<=baseline.reads;++at){Fake f;f.failRead=at;const auto r=S::SortWithin(true,f.Io());
            CHECK(r==(f.refreshes?S::Result::Partial:S::Result::Rejected));}
        for(unsigned at=1;at<=baseline.currentCalls;++at){Fake f;f.failCurrent=at;const auto r=S::SortWithin(true,f.Io());
            CHECK(r==(f.refreshes?S::Result::Partial:S::Result::Rejected));}}
    std::printf("SeymourGearWithinRt0: %u/%u passed\n",total-failures,total);return failures?1:0;
}
#else
int main(){std::puts("FAIL: verified native within-group pipeline is missing");return 1;}
#endif
