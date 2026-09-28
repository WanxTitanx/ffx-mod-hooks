// Jarvis-HOOK: isolated real native clamp, equipment loops, turn edge and
// shared damage producer. Fixtures never load a game session or installed DLL.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "WorkshopFieldFixture.h"
#include "SpiraKernelFixture.h"
#include "WorkshopEconomyFixture.h"
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/AeonAscensionBridge.h"
#include "../hooks/CombatExtensionBus.h"
#include "../hooks/SharedTurnRuntime.h"
#include "../hooks/NovaSuperDamageHook.h"
#include "../shared/Config.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#if __has_include("../hooks/SpiraRuntime.h")
#include "../hooks/SpiraRuntime.h"
namespace S=FfxHooks::SpiraAbilities;
namespace W=FfxHooks::EquipmentWorkshop;
namespace B=FfxHooks::CombatExtensions;
static unsigned checks=0,failures=0;
static std::uintptr_t base=0;
static std::array<unsigned char,31*0xF90> actors{};
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void W16(unsigned char* p,unsigned v){SpiraKernelFixture::Word(p,v);}
static void W32(unsigned char* p,unsigned v){std::memcpy(p,&v,4);}
static int R32(const unsigned char* p){int v=0;std::memcpy(&v,p,4);return v;}
static unsigned char* Actor(unsigned id){return actors.data()+id*0xF90;}
static unsigned char* Ply(unsigned id){return reinterpret_cast<unsigned char*>(base+0xD3205C+id*0x94);}
static unsigned char* Gear(unsigned id,unsigned kind){return reinterpret_cast<unsigned char*>(base+0xD30F2C+(id*2+kind)*22);}
static void Equip(unsigned owner,unsigned kind,std::initializer_list<unsigned> words){
    auto* gear=Gear(owner,kind);std::memset(gear,0,22);gear[2]=1;gear[4]=gear[6]=static_cast<unsigned char>(owner);
    gear[5]=static_cast<unsigned char>(kind);gear[11]=4;
    for(unsigned i=0;i<4;++i)W16(gear+14+2*i,255);
    unsigned i=0;for(const auto word:words)W16(gear+14+2*i++,word);
    Ply(owner)[0x2D+kind]=Actor(owner)[0x592+kind]=static_cast<unsigned char>(owner*2+kind);
}
struct FieldLimits {
    std::array<unsigned char,5> first{},last{};
    unsigned char* memory=nullptr;
    bool armed=false;
    FieldLimits(){
        memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,1024,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!memory)return;
        std::vector<unsigned char> bytes{0x53,0x56,0x57};
        const auto imm=[&](std::uint32_t v){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(v>>(i*8)));};
        for(int n=-0x74;n<=-8;n+=4){bytes.insert(bytes.end(),{0xC7,0x85});imm(static_cast<std::uint32_t>(n));imm(n>=-0x3C?100:0);}
        bytes.insert(bytes.end(),{0x8B,0x75,0x08,0x89,0xB5});imm(static_cast<std::uint32_t>(-0x98));
        bytes.insert(bytes.end(),{0x8B,0xDE,0x69,0xDB});imm(0x94);
        bytes.insert(bytes.end(),{0x81,0xC3});imm(static_cast<std::uint32_t>(base+0xD3205C));
        bytes.insert(bytes.end(),{0xC7,0x45,0xE4});imm(600000);
        bytes.insert(bytes.end(),{0xC7,0x45,0xE8});imm(20000);
        bytes.push_back(0xE9);imm(static_cast<std::uint32_t>(base+0x386850-reinterpret_cast<std::uintptr_t>(memory)-bytes.size()-4));
        const auto tail=bytes.size();bytes.insert(bytes.end(),{0x8B,0x43,0x24,0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC3});
        std::memcpy(memory,bytes.data(),bytes.size());DWORD previous=0;
        if(!VirtualProtect(memory,1024,PAGE_EXECUTE_READ,&previous))return;
        FlushInstructionCache(GetCurrentProcess(),memory,bytes.size());
        std::memcpy(first.data(),reinterpret_cast<void*>(base+0x3861B9),5);
        std::memcpy(last.data(),reinterpret_cast<void*>(base+0x386988),5);
        unsigned char jump[5]={0xE9};
        auto relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory)-base-0x3861B9-5);std::memcpy(jump+1,&relative,4);
        if(!WorkshopFieldFixture::Write(base+0x3861B9,jump,5))return;
        relative=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory)+tail-base-0x386988-5);std::memcpy(jump+1,&relative,4);
        if(!WorkshopFieldFixture::Write(base+0x386988,jump,5)){WorkshopFieldFixture::Write(base+0x3861B9,first.data(),5);return;}
        armed=true;
    }
    ~FieldLimits(){if(armed){WorkshopFieldFixture::Write(base+0x3861B9,first.data(),5);WorkshopFieldFixture::Write(base+0x386988,last.data(),5);}if(memory)VirtualFree(memory,0,MEM_RELEASE);}
};
static int amount=450000;static unsigned component=3;
static unsigned __cdecl DamageEndpoint(unsigned user,void*,unsigned,void*,const void* command,unsigned id,void*,unsigned,unsigned,unsigned,unsigned){
    const auto* row=static_cast<const unsigned char*>(command);
    const bool bdl=user<18&&(S::Word(Actor(user)+0x6BE)&0x800)!=0;
    const int cap=FfxHooks::BattleDamage::NativeUpper(row[0x20],bdl);
    return static_cast<unsigned>(B::UpperDamage((std::max)(amount,-cap),cap,user,id,component,false));
}
#include "SpiraPaidCapsCases.inl"
#include "SpiraRewardCases.inl"
int main(int argc,char** argv){
    if(argc!=4&&argc!=5)return 2;const bool paidMode=argc==5;std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    base=reinterpret_cast<std::uintptr_t>(image);Check(PrivatePeFixture::NormalizeRelocations(image),"private PE relocates");
    std::ifstream input(argv[2],std::ios::binary);std::vector<unsigned char> source((std::istreambuf_iterator<char>(input)),{});
    auto kernel=SpiraKernelFixture::Build(source);
    const auto kp=reinterpret_cast<std::uintptr_t>(kernel.data()),ap=reinterpret_cast<std::uintptr_t>(actors.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A944),&kp,4);W16(reinterpret_cast<unsigned char*>(base+0xD2A970),static_cast<unsigned>(kernel.size()));
    std::memcpy(reinterpret_cast<void*>(base+0xD334CC),&ap,4);
    static std::array<unsigned,8> language{};const auto lp=reinterpret_cast<std::uintptr_t>(language.data());
    std::memcpy(reinterpret_cast<void*>(base+0x8DED48),&lp,4);language[1]=0;
    for(unsigned owner=0;owner<18;++owner){auto* actor=Actor(owner);W16(actor+0xC,owner);W16(actor+0xE,owner);actor[0xDC8]=1;
        W32(actor+0x594,1000);W32(actor+0x5D0,500);W32(actor+0x598,999);W32(actor+0x5D4,100);Equip(owner,0,{});Equip(owner,1,{});}
    FfxHooks::Config::LoadTextForTests("[spira]\nenabled=1\n[aeon_ascension]\nenabled=1\n","C:\\private-spira.ini");
    Check(!S::Prepare(base,{},false,nullptr),"default-OFF prepares no consumer");
    Check(S::Prepare(base,{true,true},false,nullptr),"Spira requests one shared equipment and combat pipeline");
    Check(FfxHooks::InstallNovaSuperDamageHook(base,false,false,false,nullptr).ok,"existing finite clamp owner installs with Nova OFF");
    const auto directory=std::filesystem::path(argv[3]).wstring();
    Check(W::StartForTests(base,paidMode,directory.c_str(),nullptr),"Spira equipment infrastructure does not require Workshop gameplay");
    Check(paidMode||(!W::Status().enabled&&!W::Requested()),"shared calculation does not advertise Workshop enabled");
    Check(S::Activate(),"Spira activates after its shared producers");
    S::TickMainThread();Check(S::Ready(),"authored loaded kernel is admitted by name payload and mapping");
    if(failures)return 1;
    FfxHooks::AeonAscension::Mapping paid{};
    Check(FfxHooks::AeonAscension::ReadMapping(paid)&&paid.proof&&paid.words[0]==0x8094&&paid.words[1]==0x8095,"real mapping provider exposes only admitted paid identities");
    Check(!W::AscensionEffect(8,0)&&!W::AscensionEffect(8,1),"an authored paid word has no receipt or free permission");
    if(paidMode){PaidCases(directory,argv[4]);S::RequestStop();W::RequestStop();
        std::printf("SPIRA_PAID_CAPS_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    SpiraRewardCases();
    Equip(2,1,{0x8099,0x8017});Equip(0,1,{0x8099,0x8017});
    {
        FieldLimits limits;Check(limits.armed,"private field bridge isolates growth while running actual max/current clamp sites");
        for(unsigned owner:{0u,2u}){W16(Ply(owner)+0x4C,0x600);W32(Ply(owner)+0x1C,12345);W32(Ply(owner)+0x20,123);
            reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x3861B0)(owner);
            Check(R32(Ply(owner)+0x24)==(owner==2?600000:99999)&&R32(Ply(owner)+0x28)==9999,"only Auron with Warden expands the original pre-clamp maximum");
            Check(R32(Ply(owner)+0x1C)==12345&&R32(Ply(owner)+0x20)==123,"maximum growth does not heal or refill current pools");}
        W16(Ply(2)+0x4C,0);reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x3861B0)(2);
        Check(R32(Ply(2)+0x24)==9999&&R32(Ply(2)+0x1C)==9999,"removing BHP closes Warden and clamps current downward");
    }
    Equip(0,1,{0x809E});
    Check(WorkshopFieldFixture::Percent(base,0,8)==110&&WorkshopFieldFixture::Percent(base,0,9)==110,"native HPMP row modifies both percentages once");
    Equip(0,1,{0x80A2});
    for(unsigned stat=0;stat<14;++stat)
        Check(WorkshopFieldFixture::Percent(base,0,stat)==(stat>=8?103:100),"AIO changes only HP MP STR MAG DEF MDEF at native indices8 through13");
    Equip(8,0,{0x8096});*reinterpret_cast<unsigned char*>(base+0xD2A8E0)=1;
    const auto edge=reinterpret_cast<void(__cdecl*)(unsigned,void*)>(base+0x3B13D0);
    edge(8,Actor(8));Check(R32(Actor(8)+0x5D4)==105,"Mana Spring grants exactly five MP at the real native turn edge");
    W32(Actor(8)+0x5D4,997);edge(8,Actor(8));Check(R32(Actor(8)+0x5D4)==999,"Mana Spring saturates at current maximum");
    W32(Actor(8)+0x5D4,100);W32(Actor(8)+0x5D0,0);edge(8,Actor(8));Check(R32(Actor(8)+0x5D4)==100,"dead actor receives no turn regeneration");W32(Actor(8)+0x5D0,500);
    W::DamageProducerForTests(reinterpret_cast<void*>(&DamageEndpoint));
    using Damage=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
    const auto damage=reinterpret_cast<Damage>(base+0x38E680);
    unsigned char row[96]{},info[128]{};row[0x20]=0x81;row[0x23]=1;row[0x28]=1;row[0x2A]=16;
    Equip(0,1,{0x8098});Equip(1,1,{});amount=1000;
    auto hit=[&](){return damage(0,Actor(0),1,Actor(1),row,0x3000,info,0,0,0,0);};
    Check(hit()==1500,"outgoing Devil's Bargain reaches the existing damage owner");
    Equip(1,1,{0x8098});Check(hit()==2250,"incoming and outgoing Bargain each apply once");
    row[0x20]|=0x10;Check(hit()==1000,"intentional healing is excluded from Bargain");row[0x20]=1;
    amount=-500;Check(static_cast<int>(hit())==-500,"signed absorption preserves its original value");amount=1000;
    row[0x23]=2;Check(hit()==1000,"MP-only outcomes are excluded");row[0x23]=1;
    const auto offset=20+152*108;kernel[offset+0x20]=1;
    Check(hit()==1000,"changed loaded binding bytes revoke Bargain immediately");kernel[offset+0x20]=0;
    const auto namePool=20+S::Word(kernel.data()+14);
    const auto nameAt=namePool+S::Word(kernel.data()+offset);
    const auto originalGlyph=kernel[nameAt];kernel[nameAt]^=1;
    Check(hit()==1000,"a changed identity string revokes the old admitted effect before the next tick");
    kernel[nameAt]=originalGlyph;
    const auto headerByte=kernel[0];kernel[0]=2;
    Check(hit()==1000,"a changed table layout cannot borrow a previously admitted binding");kernel[0]=headerByte;
    FfxHooks::Config::LoadTextForTests("[spira_ids]\nspira_devils_bargain=175\n","C:\\private-spira.ini");
    Check(hit()==1000,"changed ID configuration retires the old runtime binding immediately");
    FfxHooks::Config::LoadTextForTests("[spira]\nenabled=1\n[aeon_ascension]\nenabled=1\n","C:\\private-spira.ini");
    Check(hit()==2250,"restoring the exact admitted data and mapping restores only their original identities");
    S::RequestStop();Check(hit()==1000&&!FfxHooks::AeonAscension::ReadMapping(paid),"stop retires combat and paid mapping admission");
    const auto mp=R32(Actor(8)+0x5D4);edge(8,Actor(8));Check(R32(Actor(8)+0x5D4)==mp,"stopped Mana Spring leaves the turn owner usable");
    std::printf("SPIRA_RUNTIME_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production SpiraRuntime.h is missing");return 1;}
#endif
