#include "ArcanaStore.h"
#include "ArcanaWorldCore.h"
#include "ArcanaAcquisition.h"
#include "ArcanaImageRouting.h"
#include "ArcanaCatalog.generated.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
using namespace FfxHooks::Arcana;
namespace {
unsigned checks=0;
void Check(bool good,const char* why){++checks;if(!good){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
std::vector<unsigned char> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Crc(std::vector<unsigned char>& bytes){unsigned crc=~0u;for(std::size_t i=0;i+4<bytes.size();++i){crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&unsigned(-int(crc&1)));}crc=~crc;for(unsigned i=0;i<4;++i)bytes[bytes.size()-4+i]=static_cast<unsigned char>(crc>>(i*8));}
void Write(const std::filesystem::path& p,const std::vector<unsigned char>& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());Check(bool(f),"fixture write");}
}
int main(int argc,char** argv){
    Check(argc==3,"fixture and scratch paths");
    const auto old=Read(argv[1]);Check(old.size()==372,"fixture produced by unmodified v2 encoder");
    Record r;Check(Decode(old.data(),old.size(),r),"decode old producer output");
    Check(r.state.revision==4&&r.state.slots[0][0]==21&&r.state.slots[0][1]==77&&r.state.slots[1][0]==0,"old revision and equipment preserved");
    Check(r.resources.valid==3&&r.resources.hp[0]==12345&&r.resources.mp[0]==456&&r.resources.hp[1]==999&&r.resources.mp[1]==22,"old HP/MP snapshot preserved");
    for(unsigned id=0;id<78;++id)Check(r.state.acquired[id]==1,"all original cards retained");
    Check(!r.state.acquired[78]&&!r.state.acquired[79],"new ownership not read from old equipment bytes");
    Check(r.presentation.count==78&&r.presentation.pending[78]==255&&r.presentation.pending[79]==255,"old notices preserved and new queue slots empty");
    Check(r.objectives==Acquisition::Progress{1,2,3,4,5,6,31,3,1,2,3,1},"authored objectives retained");
    State s;Check(Award(s,78)==Error::InvalidCard&&Award(s,79)==Error::InvalidCard,"ordinary grant excludes both Weapons");
    Check(AwardAll(s,0)==Error::None&&!s.acquired[78]&&!s.acquired[79],"full deck grants only standard cards");
    for(unsigned id=80;id<90;++id){
        Check(!s.acquired[id]&&Award(s,id)==Error::InvalidCard,"ordinary/full-deck excludes Dark Aeon cards");
        Check(FindCard(id)&&std::string(FindCard(id)->name).find("Dark ")==0,"Dark Aeon catalog identity");
    }
    State dark;
    Check(AwardDarkAeon(dark,0,78)==Error::InvalidCard&&!dark.acquired[78]&&!dark.acquired[79],"Dark grant cannot grant Weapon cards");
    Check(AwardDarkAeon(dark,1,80)==Error::Stale&&!dark.acquired[80],"Dark grant revision fence");
    for(unsigned id=80;id<90;++id){
        const auto before=dark.revision;
        Check(AwardDarkAeon(dark,before,id)==Error::None&&dark.acquired[id]&&dark.revision==before+1,"matching Dark card grant");
        Check(AwardDarkAeon(dark,dark.revision,id)==Error::None&&dark.revision==before+1,"Dark victory idempotence");
    }
    Check(!dark.acquired[78]&&!dark.acquired[79],"all Dark victories still cannot grant Weapons");
    const auto revision=s.revision;Check(AwardAll(s,revision)==Error::None&&s.revision==revision,"standard full-deck idempotence");
    Acquisition::Facts facts;facts.freeTravel=true;facts.omega=true;facts.story=3000;
    Check(Acquisition::Catalog().size()==78,"field catalog retains 78 sites");
    Check(!Acquisition::Eligible(s,{},facts,78)&&!Acquisition::Eligible(s,{},facts,79),"no field/old-omega-flag acquisition for Weapons");
    s.acquired[21]=0;Check(Acquisition::Eligible(s,{},facts,21),"The World still needs only the other77 standard cards");
    Check(std::string(Acquisition::Requirement(78)).find("Omega Weapon")!=std::string::npos&&std::string(Acquisition::Requirement(79)).find("Omega Weapon")!=std::string::npos,"both locked hints name Omega victory");
    Check(AwardOmegaWeapons(s,revision-1)==Error::Stale&&!s.acquired[78]&&!s.acquired[79],"privileged grant revision fence");
    Check(AwardOmegaWeapons(s,revision)==Error::None&&s.acquired[78]&&s.acquired[79]&&s.revision==revision+1,"one atomic two-card grant");
    Check(AwardOmegaWeapons(s,s.revision)==Error::None&&s.revision==revision+1,"repeat victory is idempotent");
    State exhausted;exhausted.revision=std::numeric_limits<std::uint64_t>::max();
    Check(AwardOmegaWeapons(exhausted,exhausted.revision)==Error::RevisionExhausted&&!exhausted.acquired[78]&&!exhausted.acquired[79],"no partial grant on exhausted revision");
    Check(std::string(FindCard(78)->name)=="Omega Weapon"&&std::string(FindCard(79)->name)=="Ultima Weapon","exact card names");
    Check(std::string(FindCard(78)->asset)=="omega-weapon.png"&&std::string(FindCard(79)->asset)=="ultima-weapon.png","exact installed asset names");
    Check(FindCard(78)->major&&FindCard(79)->major,"Weapon cards use major capacity weight");
    Check(Equip(s,s.revision,2,0,78)==Error::None&&Equip(s,s.revision,2,1,79)==Error::None,"equip both acquired Weapons");
    const auto e=Aggregate(s,2);
    Check(e.Get(EffectKind::OutgoingDamage)==55&&e.Get(EffectKind::OverdriveDamage)==60,"final halved damage effects");
    Check(e.Get(EffectKind::Sensor)==0&&e.Get(EffectKind::CtbReduction)==0,"no Sensor or CTB reduction");
    Check(e.Get(EffectKind::MpPerTurn)==3&&e.Get(EffectKind::WhiteHealing)==38&&e.Get(EffectKind::SurviveHeal)==25,"final recovery values");
    Check(e.Get(EffectKind::StrikeShadow)&&e.Get(EffectKind::WardShadow)&&e.Get(EffectKind::StrikeHoly)&&e.Get(EffectKind::WardHoly),"Dark and Holy identity effects");
    Check(e.Get(EffectKind::BreakDamage)&&e.Get(EffectKind::BreakHp)&&e.Get(EffectKind::AutoHaste)&&e.Get(EffectKind::Piercing)&&e.Get(EffectKind::SurviveOnce),"unchanged binary traits");
    DamageContext d;d.amount=1000;d.cap=99999;Check(Damage(d,e,{})==1550,"ordinary combined damage");d.overdrive=true;Check(Damage(d,e,{})==2150,"OD combined damage");
    Check(MpCost(90,e,false,false,false,false)==45&&MpCost(1,e,false,false,false,false)==1,"HalfMP and minimum remain native");
    Check(CtbDelay(100,e,false)==100,"no hidden CTB reduction");
    Check(BoostStat(100,e.Get(EffectKind::MagicPercent),0,255)==130&&BoostStat(100,e.Get(EffectKind::MpPercent),0,999)==150,"final magic and MP values");
    NativeUi::Images images;images.generation=1;images.count=1;
    for(unsigned id=78;id<=91;++id){images.quads[0]={id,0,0,1,1};Check(ImageRouting::Valid(images),"new card/icon/back IDs accepted");}
    images.quads[0].resource=92;Check(!ImageRouting::Valid(images),"image resource upper bound");
    Check(kTarotIconResource==90&&kCardBackResource==91&&kImageResourceCount==92,"shared images cannot collide with cards");
    auto before=r.state.acquired;Check(AwardOmegaWeapons(r.state,r.state.revision)==Error::None,"award migrated save after verified caller event");
    Check(Presentation::ObserveAwards(r.presentation,before,r.state.acquired)&&r.presentation.count==80,"two new notices fit full old queue");
    for(unsigned id=80;id<90;++id){auto oldOwned=r.state.acquired;Check(AwardDarkAeon(r.state,r.state.revision,id)==Error::None&&Presentation::ObserveAwards(r.presentation,oldOwned,r.state.acquired),"new Dark notice appended");}
    Check(r.presentation.count==90,"full ninety-card notification queue");
    RecordBytes encoded;Check(Encode(r,encoded)&&encoded.size()==396&&encoded[7]=='3'&&encoded[8]==3,"v3 encoder size/version");
    Record reread;Check(Decode(encoded.data(),encoded.size(),reread),"v3 round trip");
    Check(reread.state.acquired==r.state.acquired&&reread.state.slots==r.state.slots&&reread.resources.hp==r.resources.hp&&reread.resources.mp==r.resources.mp&&reread.objectives==r.objectives&&reread.presentation.pending==r.presentation.pending,"v3 fields do not overlap");
    auto bad=old;bad[174]=78;bad[175]=0;Crc(bad);Check(!Decode(bad.data(),bad.size(),reread),"old equipped ID cannot smuggle a Weapon");
    bad=old;bad[274]=78;Crc(bad);Check(!Decode(bad.data(),bad.size(),reread),"old first-card field cannot smuggle a Weapon");
    bad.assign(encoded.begin(),encoded.end());bad[379]=1;Crc(bad);Check(!Decode(bad.data(),bad.size(),reread),"v3 reserved byte rejected");
    Check(!Decode(encoded.data(),encoded.size()-1,reread),"truncated record rejected");
    std::vector<unsigned char> legacy(276,0);std::copy_n(old.begin(),272,legacy.begin());legacy[7]='1';legacy[8]=1;legacy[12]=0x14;legacy[13]=1;Crc(legacy);
    Record v1;Check(Decode(legacy.data(),legacy.size(),v1)&&!v1.state.acquired[78]&&!v1.state.acquired[79]&&v1.state.slots==r.state.slots,"v1 equipment/ownership migration");
    const std::filesystem::path dir=argv[2];Check(!std::filesystem::exists(dir),"fresh isolated store fixture");std::filesystem::create_directories(dir);
    const auto native=dir/"ffx_fixture",oldPath=Store::Extension(native,".arcana.v2");Write(oldPath,old);Store store;
    Record readOld;Hash oldPack{};oldPack.fill(0x22);Hash currentPack{};std::copy(std::begin(kPackHash),std::end(kPackHash),currentPack.begin());bool migrated=false;
    Check(store.ReadCompatible(native,r.nativeHash,currentPack,&oldPack,1,readOld,migrated)==StoreCode::Recovered&&migrated&&!readOld.state.acquired[78],"compatible old pack read without writes");
    Check(Read(oldPath)==old&&!std::filesystem::exists(Store::Extension(native,".arcana.v3")),"read-only migration preserves old file");
    r.packHash=currentPack;Check(store.Prepare(native,r)&&store.Commit(native,r.nativeHash),"native-save transaction writes v3 sidecar");
    Check(Read(oldPath)==old&&store.Read(native,r.nativeHash,currentPack,reread)==StoreCode::Found&&reread.state.acquired[78]&&reread.state.acquired[79],"new ownership persists without modifying v2");
    const auto final=Store::Extension(native,".arcana.v3");Write(final,std::vector<unsigned char>{'b','a','d'});
    Check(store.Read(native,r.nativeHash,currentPack,reread)==StoreCode::Invalid,"corrupt v3 cannot downgrade to older ownership");
    Check(Read(oldPath)==old,"old save sidecar still exact after negative checks");
    std::printf("WeaponCardsRt0: %u checks passed\n",checks);
}
