#include "../hooks/SeymourBattleCore.h"
#include <cstdio>
#include <stdexcept>
#if FFX_SEYMOUR_PERMANENT_BATTLE >= 2
namespace B=FfxHooks::SeymourBattle;
unsigned total=0,failed=0;
std::uint64_t revision=1;
bool throwProvider=false;
std::uint64_t Provider(){if(throwProvider)throw std::runtime_error("provider");return revision;}
std::uint64_t Foreign(){return 42;}
#define CHECK(x) do{++total;if(!(x)){++failed;std::printf("FAIL %u %s\n",__LINE__,#x);}}while(false)
struct Fixture {
 B::RosterImage image{};unsigned calls=0,assigns=0;bool swapSession=false,corrupt=false;
 Fixture(){image.party=0x11;image.state={0,1,2};image.ability.fill(255);for(unsigned i=0;i<5;++i)image.ability[i]=static_cast<unsigned char>(i+3);}
 static bool Read(void* p,B::RosterImage* out){*out=static_cast<Fixture*>(p)->image;return true;}
 static bool Assign(void* p,std::uint8_t,int){++static_cast<Fixture*>(p)->assigns;return false;}
 static bool Current(void*,const B::Command*){return true;}
 static int Entry(void* p){auto& f=*static_cast<Fixture*>(p);++f.calls;if(f.swapSession)++revision;if(f.corrupt)f.image.ability[0]=7;return 19;}
 static void Exit(void* p){++static_cast<Fixture*>(p)->calls;}
 B::ServiceIo Io(){return {this,3,17,Read,Assign,Current};}
};
int main(){
 CHECK(!B::RegisterPermanentRosterProvider(nullptr));CHECK(B::RegisterPermanentRosterProvider(Provider));
 CHECK(B::RegisterPermanentRosterProvider(Provider));CHECK(!B::RegisterPermanentRosterProvider(Foreign));
 for(unsigned mode=0;mode<4;++mode){
  revision=1;throwProvider=false;Fixture f;B::Ownership o;B::Telemetry t;
  if(mode==0)f.swapSession=true;
  if(mode==1)f.corrupt=true;
  if(mode==2)throwProvider=true;
  if(mode==3)revision=0;
  auto result=B::ServiceEntry({1,true,false},7,f.Io(),{&f,Fixture::Entry},&o,&t);
  CHECK(f.calls==1&&f.assigns==0&&result.originalResult==19);
  CHECK(result.outcome!=B::ServiceOutcome::Applied);
  CHECK(mode>=2||o.borrowedPermanent);
  if(o.borrowedPermanent){
   auto before=f.image;
   CHECK(B::ServiceExit({2,false,true},f.Io(),{&f,Fixture::Exit},&o,&t)==B::ServiceOutcome::NoChange);
   CHECK(f.calls==2&&!f.assigns&&f.image.party==before.party&&f.image.ability==before.ability);
  }
 }
 std::printf("SeymourPermanentSessionRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: permanent battle admission does not capture the active save revision");return 1;}
#endif
