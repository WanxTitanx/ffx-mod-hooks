#include <cstdio>
#include <cstdint>
#if __has_include("../hooks/ElementAffinity.h")
#include "../hooks/ElementAffinity.h"
namespace E=FfxHooks::ElementalDominion;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
int main(){
    for(int tier=-10000;tier<=25000;tier+=2500){
        E::AffinitySources s;s.base=tier;
        Check(E::ValidTier(tier)&&E::Effective(s).value==tier,"all 15 tiers");
        for(unsigned ward=0;ward<=4;++ward){s.ward=ward;
            const int wanted=tier<=0?tier:(tier<static_cast<int>(ward)*2500?0:tier-static_cast<int>(ward)*2500);
            Check(E::Effective(s).value==wanted,"Ward cannot create or deepen absorption");
        }
    }
    E::AffinitySources s;s.base=-10000;s.imperil=1;
    Check(E::Effective(s).value==-7500,"Imperil weakens absorption");
    s.base=0;Check(E::Effective(s).value==2500,"Imperil exposes unlocked immunity");
    s.locked=true;s.lockedValue=0;Check(E::Effective(s).value==0,"explicit affinity lock");
    E::AffinityPart parts[]={{15000,1},{-10000,1}};
    Check(E::Resolve(1000,parts,2,E::MixPolicy::HighestExposure).damage==1500,"highest exposure");
    Check(E::Resolve(1000,parts,2,E::MixPolicy::LowestExposure).damage==-1000,"lowest exposure");
    Check(E::Resolve(1000,parts,2,E::MixPolicy::SplitWeighted).damage==250,"signed weighted sum");
    parts[0].weight=3;Check(E::Resolve(1000,parts,2,E::MixPolicy::SplitWeighted).damage==875,"unequal weights");
    E::AffinityPart half[]={{5000,1}};
    Check(E::Resolve(3,half,1,E::MixPolicy::HighestExposure).damage==1&&E::Resolve(-3,half,1,E::MixPolicy::HighestExposure).damage==-1,"signed truncation");
    E::AffinityPart high[32];for(auto& p:high)p={25000,1000};
    Check(E::Resolve(INT32_MAX,high,32,E::MixPolicy::SplitWeighted).damage==INT32_MAX,"positive saturation");
    Check(E::Resolve(INT32_MIN,high,32,E::MixPolicy::SplitWeighted).damage==INT32_MIN,"negative saturation");
    Check(E::Resolve(123,nullptr,0,E::MixPolicy::HighestExposure).damage==123,"non-elemental empty list");
    Check(E::Resolve(123,high,33,E::MixPolicy::SplitWeighted).error==E::Error::Capacity,"capacity before pointer access");
    for(unsigned bit=1;bit<256;bit<<=1)for(unsigned f=0;f<16;++f){
        E::NativeMasks masks{static_cast<std::uint8_t>(f&1?bit:0),static_cast<std::uint8_t>(f&2?bit:0),static_cast<std::uint8_t>(f&4?bit:0),static_cast<std::uint8_t>(f&8?bit:0)};
        const int wanted=f&1?15000:f&8?-10000:f&4?0:f&2?5000:10000;
        Check(E::NativeBase(bit,masks).value==wanted,"native overlap precedence");
        Check(E::ResolveNativeExact(1000,bit,masks).damage==wanted/10,"native control vector");
    }
    E::NativeMasks masks;masks.weak=3;
    Check(E::ResolveNativeExact(3,3,masks).damage==6&&E::ResolveNativeExact(-3,3,masks).damage==-6,"native sequential rounding");
    Check(E::ResolveNativeExact(1000,256,masks).error!=E::Error::Ok,"no ninth native bit");
    std::printf("ELEMENT_AFFINITY_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production elemental affinity resolver is absent");return 1;}
#endif
