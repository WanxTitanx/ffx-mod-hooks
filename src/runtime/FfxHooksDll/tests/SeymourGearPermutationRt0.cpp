#include <cstdio>
#include <algorithm>
#include <random>
#include "../hooks/SeymourGearSortCore.h"
#ifdef FFX_SEYMOUR_GEAR_PERMUTATION
namespace S=FfxHooks::SeymourGearSort;
unsigned total=0,failed=0;
#define CHECK(x) do {++total;if(!(x)){if(++failed<20)std::printf("FAIL %u: %s\n",__LINE__,#x);}} while(false)
S::Snapshot Make(unsigned count){
    S::Snapshot s{};s.generation=2;s.thread=7;s.count=count;s.equipped.fill(255);
    for(unsigned i=0;i<count;++i){
        s.rows[i]=0x5000u+i;
        auto& g=s.gear[i];g[0]=static_cast<unsigned char>(i);g[2]=1;
        g[4]=7;g[5]=0;g[6]=255;g[11]=4;
    }
    return s;
}
void Swap(S::Snapshot& s,unsigned a,unsigned b){
    std::swap(s.gear[a],s.gear[b]);
    for(auto& index:s.equipped){if(index==a)index=static_cast<unsigned char>(b);else if(index==b)index=static_cast<unsigned char>(a);}
}
int main(){
    std::mt19937 random(0x53794d);
    for(unsigned count=0;count<=200;++count){
        auto before=Make(count),after=before;
        if(count){before.gear[0][6]=7;before.equipped[14]=0;after=before;}
        for(unsigned i=0;i<count;++i)Swap(after,i,random()%count);
        CHECK(S::ExactGroupPermutation(before,after,0,count));
        if(count){after.gear[count-1][7]^=1;CHECK(!S::ExactGroupPermutation(before,after,0,count));}
        CHECK(!S::ExactGroupPermutation(before,before,count+1,0));
        CHECK(!S::ExactGroupPermutation(before,before,0,201));
    }
    for(unsigned mode=0;mode<8;++mode){
        auto before=Make(20),after=before;
        if(mode==0)after.gear[19][7]=1;
        if(mode==1)after.rows[0]^=0x10000;
        if(mode==2)++after.generation;
        if(mode==3)++after.thread;
        if(mode==4)--after.count;
        if(mode==5)after.gear[0]=after.gear[1];
        if(mode==6)after.gear[0][4]=6;
        if(mode==7){after.gear[0][6]=7;after.equipped[14]=0;}
        CHECK(!S::ExactGroupPermutation(before,after,0,10));
    }
    {auto before=Make(20);before.gear[3]=before.gear[4];auto after=before;
     Swap(after,3,8);CHECK(S::ExactGroupPermutation(before,after,0,10));
     after.gear[3]=after.gear[4];CHECK(!S::ExactGroupPermutation(before,after,0,10));}
    {auto before=Make(20);before.gear[0][6]=7;before.equipped[14]=0;
     auto after=before;Swap(after,0,7);CHECK(S::ExactGroupPermutation(before,after,0,10));
     after.equipped[14]=0;CHECK(!S::ExactGroupPermutation(before,after,0,10));}
    {const auto before=Make(20);
     CHECK(!S::ExactGroupPermutation(before,before,0xffffffffu,1));
     CHECK(!S::ExactGroupPermutation(before,before,2,0xffffffffu));
     CHECK(S::ExactGroupPermutation(before,before,20,0));
     auto after=before;after.rows[199]=0x5001;
     CHECK(!S::ExactGroupPermutation(before,after,0,20));}
    // Row positions are not physical inventory slots. A permutation is confined
    // to the handles in this group, including when those handles are sparse.
    {auto before=Make(20);const auto original=before.gear;before.gear={};
     for(unsigned i=0;i<20;++i){before.rows[i]=0x110000u|0x5000u|(i*3+2);before.gear[i*3+2]=original[i];}
     auto after=before;Swap(after,S::Slot(before.rows[2]),S::Slot(before.rows[11]));
     CHECK(S::ExactGroupPermutation(before,after,2,10));
     after.gear[0][7]=1;CHECK(!S::ExactGroupPermutation(before,after,2,10));
     after=before;Swap(after,S::Slot(before.rows[1]),S::Slot(before.rows[2]));
     CHECK(!S::ExactGroupPermutation(before,after,2,10));}
    // Exercise every interval of a smaller inventory, not only a prefix.
    for(unsigned start=0;start<=32;++start)for(unsigned length=0;length<=32-start;++length){
        auto before=Make(32),after=before;
        for(unsigned i=0;i<length;++i)Swap(after,start+i,start+random()%length);
        CHECK(S::ExactGroupPermutation(before,after,start,length));
        if(length){after.gear[start][0]^=0x80;CHECK(!S::ExactGroupPermutation(before,after,start,length));}
        if(start){after=before;Swap(after,0,start);CHECK(!S::ExactGroupPermutation(before,after,start,length));}
    }
    std::printf("SeymourGearPermutationRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: native within-group permutation verification is missing");return 1;}
#endif
