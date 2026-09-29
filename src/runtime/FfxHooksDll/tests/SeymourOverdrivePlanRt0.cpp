// Pure relocation contracts; exact-profile source bytes, no game process.
#include <cstdio>
#include <array>
#include <vector>
#include <cstring>
#if __has_include("../hooks/SeymourOverdrivePlan.h")
#include "../hooks/SeymourOverdrivePlan.h"
using namespace FfxHooks::SeymourOverdrive;
static unsigned total=0,failed=0;
#define CHECK(x) do {++total;if(!(x)){++failed;std::printf("FAIL %u: %s\n",__LINE__,#x);}} while(false)
int main(){
 for(auto base:{0x400000u,0x500000u,0x10000000u})for(unsigned f=0;f<FunctionCount;++f){
  const auto& spec=Functions[f];std::vector<unsigned char> source(spec.size),dest(spec.size+16,0xCD);
  CHECK(Reference(f,base,source.data(),source.size()));
  for(auto allocation:{0x30000000u,0x70000000u,0xf0000000u}){
   const Targets routed{0x31001000u,0x31002000u};
   CHECK(Relocate(f,base,allocation,routed,source.data(),source.size(),dest.data(),spec.size));
   CHECK(dest[spec.boundOffset]==spec.extendedBound);
   for(unsigned i=spec.size;i<dest.size();++i)CHECK(dest[i]==0xCD);
   for(unsigned i=0;i<spec.callCount;++i){const auto& c=spec.calls[i];
    std::uint32_t displacement=0;std::memcpy(&displacement,dest.data()+c.offset+1,4);
    const auto target=allocation+c.offset+5+displacement;
    const auto expected=c.targetRva==CounterRva?routed.counter:c.targetRva==GaugeRva?routed.gauge:base+c.targetRva;
    CHECK(target==expected);
   }
   for(unsigned i=0;i<spec.absoluteCount;++i){std::uint32_t value=0;std::memcpy(&value,dest.data()+spec.absolutes[i],4);
    std::uint32_t native=0;std::memcpy(&native,spec.bytes+spec.absolutes[i],4);CHECK(value==native+(base-0x400000u));}
  }
  for(unsigned i=0;i<source.size();++i){auto bad=source;bad[i]^=0x80;auto before=dest;
   CHECK(!Relocate(f,base,0x30000000,{0x31001000,0x31002000},bad.data(),bad.size(),dest.data(),spec.size));CHECK(dest==before);}
  auto before=dest;
  CHECK(!Relocate(f,base,0x30000000,{0,0},source.data(),source.size(),dest.data(),spec.size));CHECK(dest==before);
  CHECK(!Relocate(f,base,0xfffffff0u,{0x31001000,0x31002000},source.data(),source.size(),dest.data(),spec.size));CHECK(dest==before);
  CHECK(!Relocate(f,base,0x30000000,{0x31001000,0x31002000},source.data(),source.size()-1,dest.data(),spec.size));CHECK(dest==before);
  CHECK(!Relocate(f,base,0x30000000,{0x31001000,0x31002000},source.data(),source.size(),dest.data(),spec.size-1));CHECK(dest==before);
 }
 std::array<unsigned char,4096> unchanged{};auto before=unchanged;
 CHECK(!Reference(FunctionCount,0x400000,unchanged.data(),unchanged.size()));CHECK(unchanged==before);
 std::printf("SeymourOverdrivePlanRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: SeymourOverdrivePlan is missing");return 1;}
#endif
