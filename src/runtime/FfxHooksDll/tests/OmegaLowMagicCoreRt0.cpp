#include "../hooks/OmegaLowMagicCore.h"
#include <cstdio>
#include <initializer_list>
namespace O=FfxHooks::OmegaLowMagic;
int main(){unsigned n=0,bad=0;auto check=[&](bool ok,const char* text){++n;if(!ok){++bad;std::printf("FAIL %s\n",text);}};
 O::Context c{true,true,true,true,true,true,false,79,1,false};
 check(O::Select(c).field==0x29,"MAG79 qualifies after the runtime writer");
 for(unsigned mag:{80u,81u,255u}){c.magic=mag;check(!O::Select(c).field,"MAG80 and above excluded");}c.magic=79;
 constexpr unsigned fields[]={41,57,47,5,45,46,7},values[]={1,3,3,1,3,3,1};
 for(unsigned i=0;i<7;++i){c.nativeElements=O::Bits[i];const auto e=O::Select(c);
  check(e.field==fields[i]&&e.value==values[i],"one theme maps to the independently verified native property");
  check(e.field!=0x23&&e.field!=4&&e.field!=6&&e.field!=0x9D,"no sensor/Death/Petrify/Doom writes");
  check(O::Needed(e,0),"missing status can be granted");
  if(e.duration)for(unsigned duration:{3u,4u,254u,255u})check(!O::Needed(e,duration),"same or longer duration cannot be refreshed or shortened");
  else check(!O::Needed(e,e.mask),"existing boolean status does not repeat");
 }
 c.nativeElements=0;c.bio=true;check(O::Select(c).field==5,"Bio uses Poison explicitly");c.bio=false;check(O::Select(c).field==0x28,"neutral offense uses Mental Break");
 c.nativeElements=0xDF;check(O::Select(c).field==0x29,"multibit chooses the first theme without another roll");
 for(unsigned bit=0;bit<7;++bit){auto x=c;
  switch(bit){case 0:x.naturalOmega=false;break;case 1:x.monster=false;break;case 2:x.hostile=false;break;case 3:x.alive=false;break;case 4:x.offensiveMagic=false;break;case 5:x.landed=false;break;case 6:x.absorbed=true;break;}
  check(!O::Select(x).field,"every context/result gate fails closed");
 }
 std::printf("OmegaLowMagicCoreRt0 %u/%u passed\n",n-bad,n);return bad?1:0;
}
