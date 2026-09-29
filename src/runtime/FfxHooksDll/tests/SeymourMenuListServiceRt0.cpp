#include <cstdio>
#include <stdexcept>
#if __has_include("../hooks/SeymourMenuListService.h")
#include "../hooks/SeymourMenuListService.h"
namespace M=FfxHooks::SeymourMenuList;
unsigned total=0,failures=0;
#define CHECK(x) do{++total;if(!(x)){++failures;std::printf("FAIL line %u: %s\n",__LINE__,#x);}}while(false)
struct Fake {
 M::Roster roster{};M::List image{},baseline{};
 unsigned reads=0,writes=0,restores=0,mode=0,edge=0;
 bool active=true,same=true,throwRead=false;
 Fake(){roster.front.fill(255);roster.reserve.fill(255);roster.front[0]=7;roster.front[1]=0;roster.reserve[0]=1;roster.flags.fill(0x10);image.rows.fill(255);M::Build(false,0,roster,image,image);baseline=image;}
 static bool Current(void* p,bool cleanup){auto& f=*static_cast<Fake*>(p);return f.same&&(cleanup||f.active);}
 static bool Roster(void* p,M::Roster& out){out=static_cast<Fake*>(p)->roster;return true;}
 static bool Read(void* p,M::List& out){auto& f=*static_cast<Fake*>(p);++f.reads;
  if(f.edge&&f.reads==f.edge)f.active=false;
  if(f.throwRead){f.throwRead=false;throw std::runtime_error("read failure");}
  out=f.image;return true;}
 static bool Write(void* p,bool cleanup,const M::List& expected,const M::List& value){auto& f=*static_cast<Fake*>(p);
  if(!(f.image==expected))return false;
  if(cleanup){++f.restores;if(f.mode==8)return false;f.image=value;return true;}
  ++f.writes;
  if(f.mode==1)return false;
  if(f.mode==2){f.image.rows=value.rows;return false;}
  f.image=value;
  if(f.mode==3)f.active=false;
  if(f.mode==4){f.same=false;f.image.selection=123;}
  if(f.mode==5){f.image.rows[0]=9;return false;}
  if(f.mode==6)f.throwRead=true;
  if(f.mode==7){f.roster.flags[0]^=2;return false;}
  if(f.mode==8){f.active=false;return false;}
  if(f.mode==9)throw std::runtime_error("write failure");
  return true;}
 M::Io Io(){return {this,Current,Roster,Read,Write};}
};
int main(){
 {Fake f;CHECK(M::Extend(0,f.Io())==M::Result::Applied);CHECK(f.writes==1&&!f.restores&&f.image.total==8&&f.image.rows[0]==7&&M::ValidList(f.image));}
 {Fake f;f.active=false;CHECK(M::Extend(0,f.Io())==M::Result::Rejected&&!f.writes&&f.image==f.baseline);}
 {Fake f;f.roster.front[0]=255;f.roster.flags[7]=0;M::Build(false,0,f.roster,f.image,f.image);CHECK(M::Extend(0,f.Io())==M::Result::Unchanged&&!f.writes);}
 {Fake f;f.image.currentMask^=1;const auto bad=f.image;CHECK(M::Extend(0,f.Io())==M::Result::Rejected&&!f.writes&&f.image==bad);}
 for(unsigned mode:{1u,2u,3u,6u,9u}){Fake f;f.mode=mode;CHECK(M::Extend(0,f.Io())==M::Result::Restored);CHECK(f.image==f.baseline&&f.writes==1);}
 for(unsigned mode:{4u,5u,7u,8u}){Fake f;f.mode=mode;CHECK(M::Extend(0,f.Io())==M::Result::RestorePending);CHECK(f.writes==1);if(mode!=8)CHECK(!f.restores);}
 for(unsigned edge=1;edge<8;++edge){Fake f;f.edge=edge;const auto r=M::Extend(0,f.Io());CHECK(r!=M::Result::RestorePending);CHECK(M::ValidList(f.image));if(!f.active)CHECK(f.image==f.baseline);}
 {Fake f;auto io=f.Io();io.write=nullptr;CHECK(M::Extend(0,io)==M::Result::Rejected&&!f.writes);}
 {Fake f;f.roster.front[0]=9;CHECK(M::Extend(0,f.Io())==M::Result::Rejected&&!f.writes);}
 std::printf("SeymourMenuListServiceRt0: %u/%u passed\n",total-failures,total);return failures?1:0;
}
#else
int main(){std::puts("FAIL: menu list transaction and ownership restoration are missing");return 1;}
#endif
