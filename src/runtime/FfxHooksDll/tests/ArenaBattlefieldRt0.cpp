#include "../hooks/CustomMixUltraCore.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
using namespace FfxHooks::CustomMixUltra;
namespace S = FfxHooks::ArenaScenery;
static int checks=0, failures=0;
static void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL: %s\n",why);}}
struct Context { std::uint16_t* field; std::uint16_t wanted; int calls=0; bool accepted=true; bool foreign=false; };
static bool Original(void* p){auto& c=*static_cast<Context*>(p);++c.calls;Check(*c.field==c.wanted,"native InitScene observes the requested scenery");if(c.foreign)*c.field=1099;return c.accepted;}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<std::uint8_t> source{std::istreambuf_iterator<char>(f),{}};
 if(source.size()!=kCarrierSize)return 2;
 Check(S::Count(3)==4 && S::Count(4)==3 && S::Count(5)==3,"restore four/three/three legacy arena options");
 Check(S::Default(3)==S::Choice::MacalaniaOpen2 && S::Default(4)==S::Choice::Cavern && S::Default(5)==S::Choice::CavernWide,"fixed mixes retain their former arena defaults");
 for(unsigned i=0;i<S::kChoiceCount;++i)for(std::uint8_t n=1;n<=8;++n)
  Check(FfxHooks::ArenaPositions::Validate(S::Generate(static_cast<S::Choice>(i),n),n)==FfxHooks::ArenaPositions::Issue::None,
        "every scenery Auto Arrange profile respects slot count, bounds and party exclusion");
 Check(S::Generate(S::Choice::MacalaniaOpen,3).points[0].x!=S::Generate(S::Choice::MacalaniaOpen2,3).points[0].x,
       "same-terrain variants retain distinct automatic spacing");
 for(unsigned i=1;i<S::kChoiceCount;++i){
  auto data=source;std::uint32_t field=0x00470460u;auto* low=reinterpret_cast<std::uint16_t*>(&field);
  PendingRequest request{};request.enabled=true;request.armed=true;request.generation=1;request.deadlineTick=100;
  request.selection.activationCount=3;request.selection.scenery=static_cast<S::Choice>(i);
  CarrierView carrier{data.data(),data.size(),kCarrierEncounterToken,"dome02_00",9,CarrierAccess::ReadWrite};carrier.battlefieldId=low;
  Context c{low,S::Get(request.selection.scenery)->battlefieldId};
  const auto out=ExecuteTransaction(&request,{1,1},carrier,&Original,&c);
  Check(out.result==TransactionResult::Restored&&out.battlefield.active&&c.calls==1&&data==source,"formation restoration retains scenery for the caller's later native read");
  Check((field>>16)==71&&*low==c.wanted,"scenery changes only the low uint16 and remains visible after InitScene");
  auto lease=out.battlefield;Check(RestoreBattlefield(&lease,low)&&field==0x00470460u,"normal cleanup restores owned scenery without changing field routing");
  request.armed=true;request.generation=2;c.accepted=false;c.calls=0;
  const auto failed=ExecuteTransaction(&request,{2,1},carrier,&Original,&c);
  Check(failed.result==TransactionResult::OriginalRejected&&!failed.battlefield.active&&field==0x00470460u,"failed initialization rolls back scenery with formation");
  request.armed=true;request.generation=3;c.accepted=true;c.foreign=true;
  const auto conflict=ExecuteTransaction(&request,{3,1},carrier,&Original,&c);
  Check(conflict.result==TransactionResult::RestoreConflict&&*low==1099,"a foreign scenery change is preserved and reported");
 }
 auto data=source;std::uint16_t low=1120;PendingRequest r{};r.enabled=true;r.armed=true;r.generation=1;r.deadlineTick=100;r.selection.activationCount=3;r.selection.scenery=S::Choice::Bikanel;
 CarrierView carrier{data.data(),data.size(),kCarrierEncounterToken,"dome02_00",9,CarrierAccess::ReadWrite};Context c{&low,1120};
 const auto blocked=ExecuteTransaction(&r,{1,1},carrier,&Original,&c);
 Check(blocked.result==TransactionResult::BattlefieldUnavailable&&data==source&&low==1120,"unproved battlefield memory rejects all Mix writes");
 std::printf("ArenaBattlefieldRt0: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
