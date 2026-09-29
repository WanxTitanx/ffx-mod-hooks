// Eight-owner sorting must retain native item bytes, equipped indices and IDs.
#include <algorithm>
#include <cstdio>
#include <random>
#include <stdexcept>
#if __has_include("../hooks/SeymourGearSortCore.h")
#include "../hooks/SeymourGearSortCore.h"
namespace S=FfxHooks::SeymourGearSort;
namespace {
unsigned total=0,failed=0;
#define CHECK(x) do{++total;if(!(x)){if(++failed<20)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
S::Snapshot Make(unsigned count){
    S::Snapshot s{};s.generation=1;s.thread=17;s.count=count;s.equipped.fill(255);
    for(unsigned i=0;i<count;++i){
        s.rows[i]=0x15000u+i;auto& g=s.gear[i];g[0]=static_cast<unsigned char>(i);
        g[1]=static_cast<unsigned char>(i>>8);g[2]=1;g[3]=static_cast<unsigned char>(i*3);
        g[4]=static_cast<unsigned char>((count-1-i)%8);g[5]=static_cast<unsigned char>((i/8)%2);g[6]=255;
        for(unsigned j=7;j<22;++j)g[j]=static_cast<unsigned char>(i*13+j);
    }
    return s;
}
struct Fake {
    S::Snapshot state{};unsigned swaps=0,reads=0,failAt=0,revokeAt=0,driftAt=0;
    std::array<unsigned,200> workshopIds{};
    static bool Read(void* p,S::Snapshot& out){auto& f=*static_cast<Fake*>(p);++f.reads;out=f.state;return true;}
    static bool Current(void* p,const S::Snapshot& expected){
        auto& f=*static_cast<Fake*>(p);return f.state.generation==expected.generation&&f.state.thread==expected.thread;
    }
    static bool Swap(void* p,std::uint16_t a,std::uint16_t b){
        auto& f=*static_cast<Fake*>(p);++f.swaps;
        if(f.swaps==f.revokeAt){++f.state.generation;return false;}
        if(f.swaps==f.failAt)return false;
        const unsigned first=a&0xfff,second=b&0xfff;
        std::swap(f.state.gear[first],f.state.gear[second]);
        for(auto& index:f.state.equipped){if(index==first)index=static_cast<unsigned char>(second);else if(index==second)index=static_cast<unsigned char>(first);}
        std::swap(f.workshopIds[first],f.workshopIds[second]);
        if(f.swaps==f.driftAt)f.state.gear[first][7]^=1;
        return true;
    }
    S::Io Ops(){return {this,Read,Current,Swap};}
};
}
int main(){
    for(unsigned n=0;n<=200;++n){
        Fake f;f.state=Make(n);for(unsigned i=0;i<200;++i)f.workshopIds[i]=1000+i;
        auto before=f.state;S::Counts counts{};CHECK(S::Count(before,counts));
        unsigned sum=0;for(auto c:counts)sum+=c;CHECK(sum==n);
        S::Plan plan{};CHECK(S::Build(before,S::Order::OwnerAndType,plan));
        auto result=S::Apply(true,S::Order::OwnerAndType,f.Ops());
        CHECK(result==S::Result::Applied||result==S::Result::Unchanged);
        unsigned last=0;for(unsigned i=0;i<n;++i){
            const auto& g=f.state.gear[i];const unsigned key=g[4]*2+g[5];CHECK(i==0||key>=last);last=key;
            CHECK(g==before.gear[g[0]]);CHECK(f.workshopIds[i]==1000u+g[0]);
        }
        for(unsigned i=n;i<200;++i)CHECK(f.state.gear[i]==before.gear[i]);
        CHECK(f.state.rows==before.rows);const auto calls=f.swaps;
        CHECK(S::Apply(true,S::Order::OwnerAndType,f.Ops())==S::Result::Unchanged&&calls==f.swaps);
    }
    {Fake f;f.state=Make(200);
        for(unsigned c=0;c<8;++c)for(unsigned kind=0;kind<2;++kind){
            for(unsigned i=0;i<200;++i)if(f.state.gear[i][4]==c&&f.state.gear[i][5]==kind){
                f.state.equipped[c*2+kind]=static_cast<unsigned char>(i);f.state.gear[i][6]=static_cast<unsigned char>(c);break;
            }
        }
        const auto before=f.state;CHECK(S::Apply(true,S::Order::OwnerAndType,f.Ops())==S::Result::Applied);
        for(unsigned c=0;c<8;++c)for(unsigned k=0;k<2;++k){
            const auto old=before.equipped[c*2+k],now=f.state.equipped[c*2+k];
            CHECK(now<200&&f.state.gear[now]==before.gear[old]);
        }
    }
    for(unsigned variant=0;variant<7;++variant){
        Fake f;f.state=Make(20);
        if(variant==0)f.state.count=201;
        if(variant==1)f.state.rows[4]=f.state.rows[3];
        if(variant==2)f.state.rows[4]=0x5fff;
        if(variant==3)f.state.rows[4]=0x7000;
        if(variant==4)f.state.gear[4][2]=0;
        if(variant==5)f.state.gear[4][5]=2;
        if(variant==6)f.state.equipped[14]=201;
        const auto before=f.state;
        CHECK(S::Apply(true,S::Order::OwnerAndType,f.Ops())==S::Result::Rejected);
        CHECK(f.state==before&&f.swaps==0);
    }
    for(unsigned variant=0;variant<4;++variant){
        Fake f;f.state=Make(40);const auto before=f.state;
        if(variant==0)f.failAt=1;
        if(variant==1)f.failAt=2;
        if(variant==2)f.revokeAt=2;
        if(variant==3)f.driftAt=2;
        const auto result=S::Apply(true,S::Order::OwnerAndType,f.Ops());
        CHECK(result==S::Result::Rejected||result==S::Result::Partial);
        CHECK(f.swaps<=2);if(variant==0)CHECK(f.state==before);
    }
    {Fake f;f.state=Make(20);const auto before=f.state;
        CHECK(S::Apply(false,S::Order::OwnerAndType,f.Ops())==S::Result::Inactive);
        CHECK(f.state==before&&!f.reads&&!f.swaps);
    }
    // Deterministic random arrangements, including duplicate byte-identical pieces.
    std::mt19937 random(0x53794d);
    for(unsigned round=0;round<128;++round){
        Fake f;f.state=Make(200);std::shuffle(f.state.gear.begin(),f.state.gear.end(),random);
        auto expected=f.state.gear;
        std::stable_sort(expected.begin(),expected.end(),[](const auto& a,const auto& b){return a[4]*2+a[5]<b[4]*2+b[5];});
        CHECK(S::Apply(true,S::Order::OwnerAndType,f.Ops())==S::Result::Applied);
        CHECK(f.state.gear==expected);
    }
    std::printf("SeymourGearSortRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: eight-character gear sort core is not implemented");return 1;}
#endif
