#include "ArenaBattleProgram.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>

namespace FfxHooks::ArenaBattleProgram {
namespace {
constexpr unsigned kProfiles=ArenaScenery::kChoiceCount*2u;
constexpr std::array<unsigned char,32> kDigest={{0x9b,0xb1,0x2c,0x51,0xf8,0x86,0xdf,0x68,0xc0,0x59,0xb2,0x83,0x4c,0x6b,0x70,0x53,0xe9,0x7e,0x60,0xfd,0xfd,0x9d,0x3c,0x74,0x29,0x05,0xbd,0xaa,0x27,0x8c,0x1c,0x2a}};
struct Profile {std::vector<std::uint8_t> bytes;Geometry geometry{};std::array<char,16> name{};};
struct Store {std::array<Profile,kProfiles> profiles{};std::array<Frame*,kMaximumFrames> frames{};std::vector<Encounter> encounters;};
// Intentionally process-lifetime: native ATEL and position readers borrow frames.
// Dynamic unload is unsupported and the production loader pins this module.
std::atomic<Store*> g_store{nullptr};
SRWLOCK g_mutex=SRWLOCK_INIT;
struct Lock {Lock(){AcquireSRWLockExclusive(&g_mutex);}~Lock(){ReleaseSRWLockExclusive(&g_mutex);}};
std::uint16_t U16(const std::uint8_t* p){std::uint16_t v;std::memcpy(&v,p,2);return v;}
std::uint32_t U32(const std::uint8_t* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
const Profile* Get(ArenaScenery::Choice scenery,ArenaScenery::Camera camera) noexcept {
    const auto* store=g_store.load(std::memory_order_acquire);
    if(!store||!ArenaScenery::Get(scenery)||!ArenaScenery::ValidCamera(camera))return nullptr;
    return &store->profiles[static_cast<unsigned>(scenery)*2u+static_cast<unsigned>(camera)];
}
bool ValidProfile(Profile& p){
    const auto& b=p.bytes;
    if(b.size()<0x100u||b.size()>=65536u||U32(b.data())!=8u||U32(b.data()+32)!=b.size())return false;
    const auto script=U32(b.data()+4),map=U32(b.data()+8),formation=U32(b.data()+12),area=U32(b.data()+16);
    if(script<0x24u||script>=map||map>=formation||formation+28u>area||area+0x70u>=b.size())return false;
    if(b[map]!=2u||b[map+1]!=138u||b[area]!=0u||b[area+1]!=1u||b[area+6]!=8u)return false;
    for(unsigned slot=0;slot<138;++slot)
        if(b[map+2+slot]!=(slot==0?0u:slot==63?1u:255u))return false;
    const unsigned records=map+140u;
    if(records+8u>formation||b[records+1]!=0u||b[records+5]!=4u)return false;
    const auto workers=U16(b.data()+script+0x36u);
    if(workers==0u||workers>64u||script+0x38u+workers*4u>map)return false;
    for(unsigned row=0;row<2;++row){
        const auto r=records+row*4u;const auto worker=b[r];const auto tags=map+U16(b.data()+r+2u);
        if(worker>=workers||tags+2u>formation)return false;
        const auto d=script+U32(b.data()+script+0x38u+worker*4u);
        if(d+0x28u>map)return false;
        const auto functions=U16(b.data()+d+8u),count=U16(b.data()+tags);
        if(tags+2u+count*2u>formation)return false;
        for(unsigned j=0;j<count;++j){const auto value=U16(b.data()+tags+2u+j*2u);if(value!=65535u&&value>=functions)return false;}
    }
    const auto positions=area+U32(b.data()+area+0x20u),stage=area+U32(b.data()+area+0x24u);
    if(positions<area+0x60u||positions+128u>stage||stage+128u>b.size())return false;
    p.geometry.slotsOffset=formation+12u;p.geometry.areaOffset=area;p.geometry.positionsOffset=positions;
    return p.geometry.nativePositions>0u&&p.geometry.nativePositions<=8u&&
        std::isfinite(p.geometry.originX)&&std::isfinite(p.geometry.originZ)&&
        std::fabs(p.geometry.forwardX*p.geometry.forwardX+p.geometry.forwardZ*p.geometry.forwardZ-1.0f)<0.001f;
}
}
bool Ready() noexcept {return g_store.load(std::memory_order_acquire)!=nullptr;}
bool Load(const char* path,std::string* error){
    Lock lock;
    if(Ready())return true;
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)return Fail(error,"Normal battle profiles are missing");
    const auto size=file.tellg();if(size<=0||size>8*1024*1024)return Fail(error,"Invalid battle profile size");
    file.seekg(0);std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if(!file.read(reinterpret_cast<char*>(bytes.data()),size))return Fail(error,"Cannot read battle profiles");
    BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char digest[32]={};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return Fail(error,"Cannot verify battle profiles");
    const bool hashed=BCryptHash(algorithm,nullptr,0,bytes.data(),static_cast<ULONG>(bytes.size()),digest,32)>=0;
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(!hashed||std::memcmp(digest,kDigest.data(),32)!=0)return Fail(error,"Battle profile checksum mismatch");
    if(bytes.size()<16||std::memcmp(bytes.data(),"ARPROG02",8)!=0||U32(bytes.data()+8)!=kProfiles||U32(bytes.data()+12)!=0x4428u)return Fail(error,"Invalid battle profile header");
    std::unique_ptr<Store> store(new(std::nothrow) Store);if(!store)return Fail(error,"Cannot allocate battle profiles");
    size_t at=16u+0x4428u;
    for(unsigned i=0;i<kProfiles;++i){
        if(at+48u>bytes.size())return Fail(error,"Truncated battle profile");
        const auto key=U32(bytes.data()+at),camera=U32(bytes.data()+at+4),n=U32(bytes.data()+at+8);
        if(key!=i/2u||camera!=i%2u||n>=65536u||at+48u+n>bytes.size())return Fail(error,"Invalid battle profile entry");
        auto& p=store->profiles[i];p.geometry.nativePositions=U32(bytes.data()+at+12);
        std::memcpy(&p.geometry.originX,bytes.data()+at+16,16);std::memcpy(p.name.data(),bytes.data()+at+32,16);p.name.back()=0;
        p.bytes.assign(bytes.begin()+at+48u,bytes.begin()+at+48u+n);at+=48u+n;
        if(!ValidProfile(p))return Fail(error,"Unproved battle profile layout");
        p.geometry.sourceName=p.name.data();
    }
    if(at+12u>bytes.size() || std::memcmp(bytes.data()+at,"ARENC001",8)!=0)return Fail(error,"Battle catalog is missing");
    const unsigned count=U32(bytes.data()+at+8u);at+=12u;
    if(count==0u || count>1024u || bytes.size()-at!=count*36u)return Fail(error,"Invalid battle catalog size");
    store->encounters.reserve(count);
    for(unsigned i=0;i<count;++i){
        Encounter entry{};std::memcpy(entry.name.data(),bytes.data()+at,16u);
        if(entry.name.back()!=0 || entry.name.front()==0)return Fail(error,"Invalid battle catalog name");
        for(char ch:entry.name){if(!ch)break;if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'))return Fail(error,"Invalid battle catalog name");}
        entry.scenery=U16(bytes.data()+at+16u);entry.count=bytes[at+18u];entry.flags=bytes[at+19u];
        if(entry.count>8u || (entry.scenery!=0xffffu && entry.scenery>=ArenaScenery::kChoiceCount))return Fail(error,"Invalid battle catalog entry");
        std::memcpy(entry.monsters.data(),bytes.data()+at+20u,16u);at+=36u;
        store->encounters.push_back(entry);
    }
    if(at!=bytes.size())return Fail(error,"Trailing battle profile data");
    g_store.store(store.release(),std::memory_order_release);return true;
}
const std::vector<Encounter>& Encounters() noexcept {
    static const std::vector<Encounter> empty;
    const auto* store=g_store.load(std::memory_order_acquire);
    return store?store->encounters:empty;
}
bool UseEncounter(const Encounter& entry,const CustomMixUltra::SelectionInput& current,
                  CustomMixUltra::SelectionInput* output) noexcept {
    if(!output || !entry.count || entry.count>8)return false;
    auto selection=current;selection.activationCount=0;selection.activations={};
    if(entry.scenery!=0xffffu){
        const auto scene=static_cast<ArenaScenery::Choice>(entry.scenery);
        if(!ArenaScenery::Get(scene))return false;
        selection.scenery=scene;
    }
    for(unsigned i=0;i<entry.count;){
        const auto* choice=ArenaMonsters::FromNative(entry.monsters[i]);
        if(!choice || i+choice->count>entry.count)return false;
        for(unsigned j=0;j<choice->count;++j)if(entry.monsters[i+j]!=choice->monsterIds[j])return false;
        selection.activations[selection.activationCount++]=choice->choice;i+=choice->count;
    }
    const auto expanded=CustomMixUltra::BuildSelection(selection);
    if(expanded.result!=CustomMixUltra::SelectionResult::Ready)return false;
    selection.positions=ArenaScenery::Generate(selection.scenery,expanded.expanded.monsterCount);
    *output=selection;return true;
}

bool LoadForCurrentModule(std::string* error){
    HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCSTR>(&LoadForCurrentModule),&module))return Fail(error,"Cannot pin the battle profile owner");
    char path[MAX_PATH]={};const DWORD n=GetModuleFileNameA(module,path,MAX_PATH);
    if(!n||n>=MAX_PATH)return Fail(error,"Cannot locate the battle profile folder");
    char* slash=std::strrchr(path,'\\');if(!slash)return Fail(error,"Invalid module folder");
    slash[1]=0;std::string full(path);full+="config\\arena-mixes\\_battle-profiles-v1.bin";
    return Load(full.c_str(),error);
}
bool Describe(ArenaScenery::Choice scenery,ArenaScenery::Camera camera,Geometry* out) noexcept {
    const auto* p=Get(scenery,camera);if(!p||!out)return false;*out=p->geometry;return true;
}
void ToWorld(const Geometry& g,float x,float z,float* wx,float* wz) noexcept {
    *wx=g.originX+g.forwardZ*x+g.forwardX*z;*wz=g.originZ-g.forwardX*x+g.forwardZ*z;
}
void ToRelative(const Geometry& g,float wx,float wz,float* x,float* z) noexcept {
    const float dx=wx-g.originX,dz=wz-g.originZ;*x=g.forwardZ*dx-g.forwardX*dz;*z=g.forwardX*dx+g.forwardZ*dz;
}
unsigned PartyPreview(ArenaScenery::Choice scenery,ArenaScenery::Camera camera,std::array<ArenaPositions::Point,7>* points) noexcept {
    const auto* p=Get(scenery,camera);if(!p||!points)return 0;
    const auto area=p->geometry.areaOffset;const auto count=p->bytes[area+4];
    const auto start=area+U32(p->bytes.data()+area+0x10);
    if(count>7||start+count*16u>p->bytes.size())return 0;
    for(unsigned i=0;i<count;++i){float x,z;std::memcpy(&x,p->bytes.data()+start+i*16u,4);std::memcpy(&z,p->bytes.data()+start+i*16u+8u,4);ToRelative(p->geometry,x,z,&(*points)[i].x,&(*points)[i].z);}
    return count;
}
unsigned MonsterPreview(const CustomMixUltra::SelectionInput& selection,std::array<ArenaPositions::Point,8>* points) noexcept {
    if(!points)return 0;
    const auto* profile=Get(selection.scenery,selection.camera);
    const auto result=CustomMixUltra::BuildSelection(selection);
    if(!profile || result.result!=CustomMixUltra::SelectionResult::Ready)return 0;
    auto layout=selection.positions;
    if(!layout.enabled && result.expanded.monsterCount>profile->geometry.nativePositions)
        layout=ArenaScenery::Generate(selection.scenery,result.expanded.monsterCount);
    for(unsigned i=0;i<result.expanded.monsterCount;++i){
        if(layout.enabled)(*points)[i]=layout.points[i];
        else {float x=0,z=0;std::memcpy(&x,profile->bytes.data()+profile->geometry.positionsOffset+i*16u,4);std::memcpy(&z,profile->bytes.data()+profile->geometry.positionsOffset+i*16u+8u,4);
            ToRelative(profile->geometry,x,z,&(*points)[i].x,&(*points)[i].z);}
        if(!std::isfinite((*points)[i].x)||!std::isfinite((*points)[i].z))return 0;
    }
    return result.expanded.monsterCount;
}

bool Build(const CustomMixUltra::SelectionInput& s,std::vector<std::uint8_t>* output,std::string* error){
    using namespace CustomMixUltra;
    const auto* p=Get(s.scenery,s.camera);const auto expanded=BuildSelection(s);
    if(!p||!output)return Fail(error,"Normal battle profiles are not ready");
    if(expanded.result!=SelectionResult::Ready||ArenaPositions::Validate(s.positions,expanded.expanded.monsterCount)!=ArenaPositions::Issue::None)return Fail(error,"Invalid battle formation");
    *output=p->bytes;auto& b=*output;
    for(unsigned i=0;i<8;++i){const std::uint16_t id=i<expanded.expanded.monsterCount?expanded.expanded.monsterIds[i]:0xFFFFu;std::memcpy(b.data()+p->geometry.slotsOffset+i*2u,&id,2);}
    auto layout=s.positions;
    if(!layout.enabled&&expanded.expanded.monsterCount>p->geometry.nativePositions)
        layout=ArenaScenery::Generate(s.scenery,expanded.expanded.monsterCount);
    if(layout.enabled)for(unsigned i=0;i<layout.count;++i){
        float x,z;ToWorld(p->geometry,layout.points[i].x,layout.points[i].z,&x,&z);
        std::memcpy(b.data()+p->geometry.positionsOffset+i*16u,&x,4);std::memcpy(b.data()+p->geometry.positionsOffset+i*16u+8u,&z,4);
    }
    return true;
}
bool Matches(const Frame* f,const CustomMixUltra::SelectionInput& s) noexcept {
    if(!f)return false;const auto& a=f->selection;
    if(a.activationCount!=s.activationCount||a.scenery!=s.scenery||a.camera!=s.camera||a.musicTrack!=s.musicTrack||a.activations!=s.activations||
       a.positions.enabled!=s.positions.enabled||a.positions.automatic!=s.positions.automatic||a.positions.count!=s.positions.count)return false;
    for(unsigned i=0;i<8;++i)if(a.positions.points[i].x!=s.positions.points[i].x||a.positions.points[i].z!=s.positions.points[i].z)return false;
    return true;
}
Frame* Reserve(const CustomMixUltra::SelectionInput& selection) noexcept {
    try{
        Lock lock;auto* store=g_store.load(std::memory_order_acquire);if(!store)return nullptr;
        for(auto*& f:store->frames)if(!f){
            std::unique_ptr<Frame> frame(new(std::nothrow) Frame);if(!frame)return nullptr;
            if(!Build(selection,&frame->bytes)||!Describe(selection.scenery,selection.camera,&frame->geometry))return nullptr;
            frame->selection=selection;f=frame.release();return f;
        }
    }catch(...){return nullptr;}
    return nullptr;
}
void ReleaseUnpublished(Frame* frame) noexcept {
    if(!frame||frame->published)return;
    Lock lock;auto* store=g_store.load(std::memory_order_acquire);if(!store)return;
    for(auto*& f:store->frames)if(f==frame){delete f;f=nullptr;return;}
}
} // namespace FfxHooks::ArenaBattleProgram
