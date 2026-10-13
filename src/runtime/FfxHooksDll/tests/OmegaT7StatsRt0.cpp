// Jarvis-HOOK: actual single-writer transactions against independently decoded DATA bases.
#include "../hooks/F7DifficultyCore.h"
#include "../hooks/SinNaturalCore.h"
#include "OmegaStatsFixtures.h"
#include <cstdio>
#include <cstring>
#include <array>
#include <vector>
#include <climits>
namespace D=FfxHooks::F7Difficulty;namespace S=FfxHooks::SinRam;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
struct Memory {
 static constexpr std::uintptr_t base=0x10000000;
 std::array<unsigned char,D::kActorSpan> bytes{};
 std::vector<std::pair<unsigned,unsigned>> writes;
 bool unreadable=false;
 template<class T>void Put(unsigned at,T v){std::memcpy(bytes.data()+at,&v,sizeof(v));}
 template<class T>T Get(unsigned at)const{T v{};std::memcpy(&v,bytes.data()+at,sizeof(v));return v;}
 void Init(const OmegaStatsFixture& f){bytes.fill(0xA5);writes.clear();unreadable=false;
  Put<std::uint16_t>(D::kFormationIdOffset,static_cast<std::uint16_t>(0x1000+f.model));Put<unsigned>(0x48,0x24000000);
  Put(D::kMaxHpOffset,f.hp);Put(D::kCurrentHpOffset,f.hp);Put(D::kOverkillOffset,f.overkill);Put(D::kMaxMpOffset,f.mp);Put(D::kCurrentMpOffset,f.mp);
  for(unsigned i=0;i<8;++i)Put<unsigned char>(D::kStatOffsets[i],f.stats[i]);}
 static bool Read(void* p,std::uintptr_t a,void* out,std::size_t n){auto& m=*static_cast<Memory*>(p);if(m.unreadable||a<base||a-base+n>m.bytes.size())return false;std::memcpy(out,m.bytes.data()+a-base,n);return true;}
 static bool Write(void* p,std::uintptr_t a,const void* in,std::size_t n){auto& m=*static_cast<Memory*>(p);if(a<base||a-base+n>m.bytes.size())return false;m.writes.emplace_back(static_cast<unsigned>(a-base),static_cast<unsigned>(n));std::memcpy(m.bytes.data()+a-base,in,n);return true;}
 D::MemoryIo Io(){return {this,Read,Write};}
};
static S::RuntimeRequest Request(){S::RuntimeRequest r;r.omegaT7=true;r.config.enabled=true;r.config.seeded=true;r.config.seed=123;r.scriptManaged=false;r.encounterToken=(590u<<16)|5;r.nativeFieldRow=88;r.origin=S::EncounterOrigin::Natural;r.transitionCallerRva=FfxHooks::SinNatural::kCallerRva;r.transitionRequestId=r.actorRequestId=4;r.transitionGeneration=7;return r;}
static void Apply(D::Runtime& runtime,Memory& m,const S::RuntimeRequest& r,int row=88){const D::ActorRef actor{Memory::base,0};runtime.UpdateComposed(m.Io(),D::MakeNeutralConfig(),true,row,r,&actor,1);}
static bool Allowed(unsigned offset,unsigned size){if(size==4&&(offset==D::kMaxHpOffset||offset==D::kCurrentHpOffset||offset==D::kOverkillOffset))return true;for(auto stat:D::kStatOffsets)if(size==1&&offset==stat)return true;return false;}
int main(){
 for(const auto& f:omegaStatsFixtures){
  Memory m;m.Init(f);D::Runtime runtime;runtime.BeginGeneration(7);auto request=Request();Apply(runtime,m,request);
  Check(m.Get<unsigned>(D::kMaxHpOffset)==f.t7hp&&m.Get<unsigned>(D::kCurrentHpOffset)==f.t7hp,"real base HP receives one T7 curve");
  Check(m.Get<unsigned>(D::kOverkillOffset)==f.t7overkill,"overkill receives the existing truncating T7 curve");
  for(unsigned i=0;i<8;++i)Check(m.Get<unsigned char>(D::kStatOffsets[i])==f.t7stats[i],"eight stats match independent DATA expected values including cap255");
  Check(m.Get<unsigned>(D::kMaxMpOffset)==f.mp&&m.Get<unsigned>(D::kCurrentMpOffset)==f.mp,"T7 never scales MP");
  if(f.model==910){
   const D::ActorRef actor{Memory::base,0};
   Check(runtime.OwnsOmegaMiniHp(7,actor,0x24000000,255000),"m910255000 has an exact original150000 writer witness");
   Check(!runtime.OwnsOmegaMiniHp(8,actor,0x24000000,255000)&&!runtime.OwnsOmegaMiniHp(7,actor,0x24000001,255000)&&
         !runtime.OwnsOmegaMiniHp(7,{Memory::base+16,0},0x24000000,255000)&&!runtime.OwnsOmegaMiniHp(7,actor,0x24000000,300000),"stale generation, replaced resource, foreign address or forged HP never inherits mini authority");
  }
  for(const auto& write:m.writes)Check(Allowed(write.first,write.second),"single writer changes only HP/overkill/eight stats, no MP/rewards/source bytes");
  const auto written=m.writes.size();m.Put<unsigned>(D::kCurrentHpOffset,f.t7hp-13);request.config.seed=999;request.areaVisit=25;Apply(runtime,m,request);
  Check(m.writes.size()==written&&m.Get<unsigned>(D::kCurrentHpOffset)==f.t7hp-13,"seed/visit repeat neither stacks nor heals current HP");
  request.config.enabled=false;Apply(runtime,m,request);
  if(f.model==910)Check(!runtime.OwnsOmegaMiniHp(7,{Memory::base,0},0x24000000,255000),"restoration retires the expanded mini HP witness");
  const auto expectedCurrent=static_cast<unsigned>((std::uint64_t(f.t7hp-13)*f.hp+f.t7hp/2)/f.t7hp);
  Check(m.Get<unsigned>(D::kMaxHpOffset)==f.hp&&m.Get<unsigned>(D::kCurrentHpOffset)==expectedCurrent,"source withdrawal restores base and one live HP ratio");
  Check(m.Get<unsigned>(D::kOverkillOffset)==f.overkill,"restore returns original overkill");
  for(unsigned i=0;i<8;++i)Check(m.Get<unsigned char>(D::kStatOffsets[i])==f.stats[i],"restore returns owned base stat");
  m.Init(f);runtime.BeginGeneration(8);request=Request();request.transitionGeneration=8;Apply(runtime,m,request);
  Check(m.Get<unsigned>(D::kMaxHpOffset)==f.t7hp,"native reentry captures fresh base rather than prior T7 result");
 }
 const auto f=omegaStatsFixtures[0];
 for(unsigned test=0;test<15;++test){Memory m;m.Init(f);const auto original=m.bytes;D::Runtime runtime;runtime.BeginGeneration(7);auto r=Request();int row=88;
  switch(test){case 0:r.omegaT7=false;break;case 1:r.config.enabled=false;break;case 2:r.origin=S::EncounterOrigin::CustomMix;break;case 3:r.origin=S::EncounterOrigin::Force;break;case 4:r.origin=S::EncounterOrigin::Arena;break;case 5:r.origin=S::EncounterOrigin::Ultra;break;case 6:r.transitionCallerRva++;break;case 7:r.actorRequestId++;break;case 8:r.transitionGeneration++;break;case 9:r.encounterToken=(591u<<16)|10;break;case 10:r.encounterToken=(590u<<16)|10;break;case 11:r.encounterToken=(1076u<<16)|5;break;case 12:row=89;break;case 13:r.transitionRequestId=r.actorRequestId=0;break;case 14:m.unreadable=true;break;}
  Apply(runtime,m,r,row);Check(m.writes.empty()&&m.bytes==original,"unadmitted/custom/Weapon/T9/old-arena/foreign-source cases write nothing");
 }
 for(unsigned model:{99u,100u,211u}){Memory m;m.Init(f);m.Put<std::uint16_t>(D::kFormationIdOffset,static_cast<std::uint16_t>(0x1000+model));D::Runtime runtime;runtime.BeginGeneration(7);Apply(runtime,m,Request());Check(m.writes.empty(),"bosses and treasure chests never receive Omega T7 stats");}
 {Memory m;m.Init(f);D::Runtime runtime;runtime.BeginGeneration(7);auto r=Request();r.encounterToken=(591u<<16)|8;r.nativeFieldRow=89;Apply(runtime,m,r,89);Check(m.Get<unsigned>(D::kMaxHpOffset)==f.t7hp,"second native Omega region receives the same T7 curve");}
 for(unsigned hp:{0u,0x80000000u}){Memory m;m.Init(f);m.Put<unsigned>(D::kMaxHpOffset,hp);m.Put<unsigned>(D::kCurrentHpOffset,hp);D::Runtime runtime;runtime.BeginGeneration(7);Apply(runtime,m,Request());Check(m.writes.empty(),"missing or signed-invalid source HP fails closed");}
 for(const auto& mini:omegaStatsFixtures)if(mini.model==910){Memory m;m.Init(mini);m.Put<unsigned>(D::kMaxHpOffset,255000);m.Put<unsigned>(D::kCurrentHpOffset,255000);D::Runtime runtime;runtime.BeginGeneration(7);Apply(runtime,m,Request());Check(m.writes.empty(),"m910 source must remain the original150000, never accept a forged255000 base");}
 {Memory m;m.Init(f);D::Runtime runtime;runtime.BeginGeneration(7);Apply(runtime,m,Request());m.Put<unsigned>(0x48,0x24001000);const auto prior=m.bytes;m.writes.clear();runtime.Restore(m.Io());Check(m.writes.empty()&&m.bytes==prior,"restore cannot write a replaced source resource even with same model/address");}
 std::printf("OmegaT7StatsRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
