#include "../hooks/SeymourBattleCore.h"
#include <cstdio>
#include <algorithm>
#ifdef FFX_SEYMOUR_PERMANENT_BATTLE
namespace B=FfxHooks::SeymourBattle;
unsigned checks=0,failed=0;bool permit=true;
std::uint64_t Provider(){return permit?1:0;}
#define CHECK(x) do{++checks;if(!(x)){++failed;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
struct Fixture {
 B::RosterImage image{};unsigned assigns=0,entries=0,exits=0;bool revoke=false;
 Fixture(){image.party=0x11;image.state={0,1,2};image.ability.fill(255);for(unsigned i=0;i<5;++i)image.ability[i]=static_cast<unsigned char>(i+3);}
 static bool Read(void* p,B::RosterImage* out){*out=static_cast<Fixture*>(p)->image;return true;}
 static bool Assign(void* p,std::uint8_t,int){++static_cast<Fixture*>(p)->assigns;return false;}
 static bool Current(void*,const B::Command*){return true;}
 static int Entry(void* p){auto& f=*static_cast<Fixture*>(p);++f.entries;if(f.revoke)permit=false;return 71;}
 static void Exit(void* p){++static_cast<Fixture*>(p)->exits;}
 B::ServiceIo Io(){return {this,3,17,Read,Assign,Current};}
};
int main(){
 CHECK(B::RegisterPermanentRosterProvider(Provider));
 for(unsigned i=0;i<2048;++i){permit=true;Fixture f;std::swap(f.image.state[i%3],f.image.ability[i%5]);
  const auto before=f.image;B::Ownership owned{};B::Telemetry t;B::Command command{1,true,false};
  auto r=B::ServiceEntry(command,7,f.Io(),{&f,Fixture::Entry},&owned,&t);
  CHECK(r.outcome==B::ServiceOutcome::Applied&&r.originalResult==71&&f.entries==1);
  CHECK(owned.borrowedPermanent&&owned.battleRosterOwned&&f.assigns==0);
  CHECK(f.image.party==before.party&&f.image.state==before.state&&f.image.ability==before.ability);
  permit=false;auto exit=B::ServiceExit({2,false,true},f.Io(),{&f,Fixture::Exit},&owned,&t);
  CHECK(exit==B::ServiceOutcome::NoChange&&f.exits==1&&f.assigns==0);
  CHECK(!owned.borrowedPermanent&&!owned.battleRosterOwned);
 }
 {permit=false;Fixture f;B::Ownership o;B::Telemetry t;auto r=B::ServiceEntry({1,true,false},7,f.Io(),{&f,Fixture::Entry},&o,&t);
  CHECK(r.outcome==B::ServiceOutcome::RejectedPreflight&&!o.borrowedPermanent&&f.entries==1&&!f.assigns);}
 {permit=true;Fixture f;f.revoke=true;B::Ownership o;B::Telemetry t;auto r=B::ServiceEntry({1,true,false},7,f.Io(),{&f,Fixture::Entry},&o,&t);
  CHECK(r.outcome==B::ServiceOutcome::RestorePending&&o.borrowedPermanent&&!f.assigns);}
 std::printf("SeymourPermanentBattleRt0: %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
#else
int main(){std::puts("FAIL: permanent roster conflicts with temporary battle cleanup");return 1;}
#endif
