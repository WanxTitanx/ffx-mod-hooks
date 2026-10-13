#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/ArcanaChallengeRuntime.h"
#include "../hooks/ArcanaChallengeBridge.h"
#include "../hooks/ArcanaChallengeCatalog.generated.h"
#include "../hooks/ArcanaRuntime.h"
#include "../hooks/OmegaLastSealNative.h"
#include "../hooks/BattleEndHook.h"
#include "../hooks/SharedActionRuntime.h"
#include "../hooks/SharedBattleRuntime.h"
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <string>
namespace C=FfxHooks::Arcana::Challenge;namespace A=FfxHooks::Arcana;namespace S=FfxHooks::SharedAction;namespace N=FfxHooks::OmegaLastSeal::Native::Common;
static std::uintptr_t base=0;static unsigned checks=0,failures=0;static std::uint64_t session=7;
static FfxHooks::OmegaLastSeal::Runtime::Context context{1,true};static A::State deck;
static std::vector<unsigned char> actors(31*0xF90),container;static const C::Formation* fixtureFormation=nullptr;
static const char* phase="load";
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void Put(void* p,unsigned value,unsigned size=4){std::memcpy(p,&value,size);}
static unsigned char* Actor(unsigned id){return actors.data()+id*0xF90;}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* e){std::printf("DARK_REWARD_FAULT phase=%s code=%08lX rva=%08lX address=%08lX\n",phase,e->ExceptionRecord->ExceptionCode,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-base),e->ExceptionRecord->NumberParameters>1?static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]):0);return EXCEPTION_CONTINUE_SEARCH;}
namespace FfxHooks {bool IsBattleEndHookInstalled(){return true;}}
namespace FfxHooks::Arcana::Runtime {
std::uint64_t SaveSession() noexcept{return session;}
std::uint64_t BattleGeneration() noexcept{return context.battle;}
Error GrantVictory(const VictoryReceipt& receipt) noexcept {
    if(receipt.session!=session||receipt.battle!=context.battle)return Error::Stale;
    const auto card=VictoryCard(receipt);return card>=80&&card<90?AwardDarkAeon(deck,deck.revision,card):Error::InvalidState;
}
}
static bool Context(FfxHooks::OmegaLastSeal::Runtime::Context& out) noexcept {out=context;return true;}
static unsigned Family(unsigned id){if(id>=341)return 7;if(id==339)return 6;if(id==340)return 5;return id-334;}
static auto* Flags(){return reinterpret_cast<unsigned char*>(base+FfxHooks::ExecutableProfile::Rva<0xD2CA90>()+0xC89);}
static void Observe(){const auto* observer=S::observers[static_cast<unsigned>(S::Slot::ChallengeRewards)].load();if(observer&&observer->beforeResult)(void)observer->beforeResult({0,0,20,nullptr,nullptr});}
static void Begin(const std::string& root,const char* name,bool original=true){
    FfxHooks::SharedBattleRuntime::NotifyActionConsumers();++context.battle;context.original=original;deck={};std::memset(actors.data(),0,actors.size());std::memset(Flags(),0,8);
    fixtureFormation=nullptr;for(const auto& row:C::Formations)if(!std::strcmp(row.name,name))fixtureFormation=&row;
    if(!fixtureFormation){Check(false,"fixture selects an authored original formation");return;}
    std::ifstream file(root+"/"+name+".bin",std::ios::binary);container={std::istreambuf_iterator<char>(file),{}};
    Put(reinterpret_cast<void*>(base+0xD2A9A8),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(container.data())));
    Put(reinterpret_cast<void*>(base+0xD2A9A6),static_cast<unsigned>(container.size()),2);std::memcpy(reinterpret_cast<void*>(base+0xD2C25A),name,10);
    for(unsigned slot=0;slot<8;++slot){const unsigned id=fixtureFormation->monsters[slot];if(id==65535)continue;auto* actor=Actor(20+slot);
        actor[0xDC8]=actor[0xDD2]=actor[0xDD3]=1;Put(actor+0xE,0x1000+id,2);Put(actor+0x5D0,100000);Put(actor+0x594,100000);Put(actor+0x48,0x12345000+slot*0x100);Put(actor+0xF7C,0x22345000+slot*0x100);}
    Actor(0)[0xDC8]=Actor(0)[0xDD2]=Actor(0)[0xDD3]=1;Put(Actor(0)+0x5D0,5000);Put(Actor(0)+0x594,5000);
    std::memset(reinterpret_cast<void*>(base+0xD2C895),255,7);*reinterpret_cast<unsigned char*>(base+0xD2C895)=0;
    *reinterpret_cast<unsigned char*>(base+0xD2C9EB)=0;*reinterpret_cast<unsigned char*>(base+0xD2C9F1)=0;
}
static void Dead(unsigned only=0){for(unsigned slot=0;slot<8;++slot){auto* actor=Actor(20+slot);if(fixtureFormation->monsters[slot]==65535)continue;
    if(!only||fixtureFormation->monsters[slot]==only){Put(actor+0x5D0,0);Put(actor+0x606,1,2);actor[0xDCC]=1;}else {actor[0xDC8]=0;actor[0xDCD]=1;}}}
static void End(bool complete=true){
    const auto* observer=FfxHooks::BattleTerminal::observers[static_cast<unsigned>(FfxHooks::BattleTerminal::Slot::ChallengeRewards)].load();
    if(observer){void* token=observer->before(1);if(token)observer->after(token,complete);}
}
static unsigned Owned(){unsigned total=0;for(unsigned card=80;card<90;++card)if(deck.acquired[card])++total;return total;}
int main(int argc,char** argv){
    if(argc!=3)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);AddVectoredExceptionHandler(1,Fault);
    const auto pe=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!pe)return 2;base=reinterpret_cast<std::uintptr_t>(pe);
    Check(PrivatePeFixture::NormalizeRelocations(pe),"private supported Steam image is relocated");
    Put(reinterpret_cast<void*>(base+0xD334D4),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors.data())));Put(reinterpret_cast<void*>(base+0xD34468),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(Actor(20))));
    Check(C::PrepareForTests(base,Context),"production challenge observer registers to the existing shared boundaries");
    phase="original wins";
    for(const auto& row:C::Formations){Begin(argv[2],row.name);Observe();Dead();End();
        for(unsigned id:row.monsters)if(id>=334&&id<=343){const unsigned card=80+id-334;
            if(id==336||id==340){Check(!deck.acquired[card],"Ixion/Yojimbo wait for the native terminal event flag");Flags()[Family(id)]=0x80;C::Tick();Check(deck.acquired[card]==((id==336&&row.stage==2)||(id==340&&row.stage==5)),"only stage2/duel5 can complete its pending reward");}
            else Check(deck.acquired[card],"confirmed original victory grants the corresponding card");}
        const auto revision=deck.revision;End();C::Tick();Check(deck.revision==revision,"duplicate terminal callbacks cannot repeat an acquisition");
    }
    phase="partial Sisters";Begin(argv[2],"kino01_70");Observe();Dead(341);End();Check(deck.acquired[87]&&!deck.acquired[88]&&!deck.acquired[89]&&Owned()==1,"partial original Sisters victory grants only the dead Cindy");
    phase="debug";Begin(argv[2],"bsil07_70",false);Observe();Dead();End();Check(!Owned(),"exact debug encounter is excluded");
    phase="modified container";Begin(argv[2],"bsil07_70");container.back()^=1;Observe();Dead();End();Check(!Owned(),"modified original-name container cannot impersonate a story victory");
    phase="native loss";Begin(argv[2],"bsil07_70");Observe();Dead();Put(Actor(0)+0x5D0,0);End();Check(!Owned(),"native defeat does not unlock a card");
    phase="unsettled death";Begin(argv[2],"bsil07_70");Observe();Put(Actor(20)+0x5D0,0);End();Check(!Owned(),"HP zero without the native terminal death mirror cannot grant a card");
    phase="native escape";Begin(argv[2],"bsil07_70");Observe();Actor(0)[0xDC8]=0;Actor(0)[0xDCD]=1;End();Check(!Owned(),"native party escape does not unlock a card");
    phase="aborted cleanup";Begin(argv[2],"bsil07_70");Observe();Dead();End(false);Check(!Owned(),"aborted native cleanup never grants");
    phase="old progress";Begin(argv[2],"bsil07_70");Flags()[0]=0x80;Observe();Dead();End();Check(!Owned(),"existing save flags are not retroactive victory receipts");
    phase="load invalidation";Begin(argv[2],"kami03_71");Observe();Dead();End();++session;Flags()[2]=0x80;C::Tick();Check(!Owned(),"loading another save invalidates the pending final-stage receipt");
    phase="actor replacement";Begin(argv[2],"bsil07_70");Observe();Dead();Put(Actor(20)+0x48,0x32345000);End();Check(!Owned(),"replacement actor resource cannot use an older admission");
    C::Stop();Check(!C::Active()&&!C::fieldPump.load(),"stop detaches challenge field progression");
    std::printf("ArcanaChallengeRuntimeRt1: %u checks, %u failures; native outcome leaf and exact formation bytes, controlled save/reader/recipient boundaries\n",checks,failures);return failures?1:0;
}
