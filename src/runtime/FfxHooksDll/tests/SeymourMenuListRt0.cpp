#include <algorithm>
#include <array>
#include <cstdio>
#include <random>
#include <vector>
#if __has_include("../hooks/SeymourMenuListCore.h")
#include "../hooks/SeymourMenuListCore.h"
namespace M=FfxHooks::SeymourMenuList;
unsigned checks=0,failed=0;
#define CHECK(x) do{++checks;if(!(x)){if(++failed<16)std::printf("FAIL line %u: %s\n",__LINE__,#x);}}while(false)
M::Roster Empty(){M::Roster r{};r.front.fill(255);r.reserve.fill(255);return r;}
M::List Previous(){M::List p{};for(unsigned i=0;i<8;++i)p.rows[i]=static_cast<unsigned char>(0xa0u+i);p.current=100;p.total=101;p.frontline=102;p.selection=103;p.frontMask=104;p.currentMask=105;p.totalMask=106;return p;}
int main(){
    std::mt19937 rng(0x53594d38);std::array<unsigned char,8> order{0,1,2,3,4,5,6,7};
    for(unsigned n=0;n<=8;++n){
        std::shuffle(order.begin(),order.end(),rng);
        for(unsigned joined=0;joined<256;++joined)for(bool extend:{false,true})
        for(unsigned mode:{0u,7u,0x10000u,0x1001fu,0x20000u,0xffffffffu}){
            auto input=Empty();const unsigned front=(std::min)(n,3u);
            for(unsigned i=0;i<n;++i){if(i<front)input.front[i]=order[i];else input.reserve[i-front]=order[i];}
            for(unsigned i=0;i<8;++i)input.flags[i]=static_cast<unsigned char>((joined&(1u<<i))?0x91:0x81);
            const auto saved=input;auto previous=Previous(),result=previous;
            CHECK(M::Build(extend,mode,input,previous,result));CHECK(input==saved);
            std::vector<unsigned char> expected;unsigned fm=0,cm=0,tm=0,fc=0,cc=0;
            if((mode&0xffff0000u)!=0x10000u){
                for(unsigned i=0;i<n;++i)if(extend||order[i]!=7){expected.push_back(order[i]);cm|=1u<<order[i];if(i<front){fm|=1u<<order[i];++fc;}}
                cc=static_cast<unsigned>(expected.size());tm=cm;
                for(unsigned i=0;i<8;++i)if((extend||i!=7)&&(joined&(1u<<i))){tm|=1u<<i;if(!(cm&(1u<<i)))expected.push_back(static_cast<unsigned char>(i));}
            }
            CHECK(result.current==cc&&result.total==expected.size()&&result.frontline==fc&&result.selection==0);
            CHECK(result.frontMask==fm&&result.currentMask==cm&&result.totalMask==tm);
            for(unsigned i=0;i<8;++i)CHECK(result.rows[i]==(i<expected.size()?expected[i]:previous.rows[i]));
            CHECK(M::ValidList(result));
            auto alias=previous;CHECK(M::Build(extend,mode,input,alias,alias)&&alias==result);
        }
    }
    for(unsigned bad=8;bad<255;++bad){auto input=Empty();input.front[0]=static_cast<unsigned char>(bad);auto out=Previous(),saved=out;
        CHECK(!M::Build(true,0,input,out,out)&&out==saved);}
    {auto input=Empty();input.front[0]=7;input.reserve[16]=7;auto out=Previous(),saved=out;
     CHECK(!M::Build(true,0,input,out,out)&&out==saved);}
    {auto input=Empty();for(unsigned i=0;i<8;++i)input.reserve[i*2]=static_cast<unsigned char>(7-i);auto out=Previous();
     CHECK(M::Build(true,0,input,out,out)&&out.total==8&&out.rows[0]==7&&out.rows[7]==0);}
    for(unsigned field=0;field<9;++field){M::List out{};out.rows.fill(255);
        if(field==0)out.total=9;
        if(field==1)out.current=1;
        if(field==2)out.frontline=1;
        if(field==3){out.total=1;out.rows[0]=8;}
        if(field==4){out.total=2;out.rows[0]=out.rows[1]=0;}
        if(field==5)out.totalMask=0x100;
        if(field==6)out.currentMask=1;
        if(field==7)out.frontMask=1;
        if(field==8)out.selection=1;
        CHECK(!M::ValidList(out));
    }
    std::printf("SeymourMenuListRt0: %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
#else
int main(){std::puts("FAIL: bounded native eight-character menu list implementation is missing");return 1;}
#endif
