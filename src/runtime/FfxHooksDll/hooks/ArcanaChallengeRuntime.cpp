#include "ArcanaChallengeRuntime.h"
#include "ArcanaChallengeBridge.h"
#include "ArcanaChallengeCatalog.generated.h"
#include "ArcanaRuntime.h"
#include "OmegaLastSealNative.h"
#include "OmegaLastSealConfig.h"
#include "OmegaLastSealEvidence.h"
#include "BattleEndHook.h"
#include "SharedActionRuntime.h"
#include "SharedBattleRuntime.h"
#include <atomic>
#include <cstring>

namespace FfxHooks::Arcana::Challenge {
namespace {
namespace N=OmegaLastSeal::Native::Common;namespace A=SharedAction;
std::uintptr_t image=0;OmegaLastSeal::Runtime::Reader reader=nullptr;std::atomic<bool> enabled{false};
DWORD owner=0;std::uint64_t generation=0,session=0,battle=0;const Formation* formation=nullptr;
struct Actor {std::uintptr_t address=0,resource=0,worker=0;unsigned monster=0;};
std::array<Actor,8> actors{};unsigned liveAtAdmission=0;std::array<unsigned char,8> flagsBefore{};
std::array<VictoryReceipt,10> pending{};
struct Completion {std::array<VictoryReceipt,10> receipts{};};
thread_local Completion completion{};
bool Own() noexcept {return owner==GetCurrentThreadId();}
unsigned Family(unsigned monster) noexcept {if(monster>=341)return 7;if(monster==339)return 6;if(monster==340)return 5;return monster-334;}
bool Witness(std::uintptr_t at,const Formation& row) noexcept {
    if(at<0x10000||at>UINT32_MAX-row.size)return false;
    __try{return OmegaLastSeal::Hash(reinterpret_cast<const void*>(at),row.size)==row.hash;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Clear() noexcept {generation=session=battle=0;formation=nullptr;actors={};liveAtAdmission=0;flagsBefore={};pending={};owner=0;}
bool Same(unsigned slot) noexcept {
    const auto& key=actors[slot];return key.address&&N::Actor(image,20+slot)==key.address&&N::ReadOr<std::uint32_t>(key.address+0x48)==key.resource&&
        N::ReadOr<std::uint32_t>(key.address+0xF7C)==key.worker&&N::ReadOr<std::uint16_t>(key.address+0xE)==0x1000+key.monster;
}
void Sync() noexcept {
    if(!enabled.load()||!reader||(owner&&!Own()))return;
    OmegaLastSeal::Runtime::Context current;if(!reader(current)||!current.battle||!current.original)return;
    const auto currentSession=Runtime::SaveSession(),currentBattle=Runtime::BattleGeneration();if(!currentSession||!currentBattle)return;
    if(generation==current.battle&&session==currentSession&&battle==currentBattle)return;
    Clear();std::array<char,10> name{};if(!N::Read(image+0xD2C25A,name))return;
    const Formation* match=nullptr;for(const auto& row:Formations)if(!std::memcmp(name.data(),row.name,9)){match=&row;break;}
    const auto buffer=N::ReadOr<std::uint32_t>(image+0xD2A9A8);
    if(!match||N::ReadOr<std::uint16_t>(image+0xD2A9A6)!=match->size||!Witness(buffer,*match))return;
    unsigned living=0;std::array<Actor,8> next{};
    for(unsigned slot=0;slot<8;++slot){const auto id=match->monsters[slot];if(id==65535)continue;
        const auto address=N::Actor(image,20+slot);if(!address||N::ReadOr<std::uint16_t>(address+0xE)!=0x1000+id)return;
        if(id<334||id>343)continue;
        next[slot]={address,N::ReadOr<std::uint32_t>(address+0x48),N::ReadOr<std::uint32_t>(address+0xF7C),id};
        if(!next[slot].resource||!next[slot].worker)return;
        if(N::Alive(address))living|=1u<<(id-334);
    }
    if(!living||!N::Read(image+ExecutableProfile::Rva<0xD2CA90>()+0xC89,flagsBefore))return;
    owner=GetCurrentThreadId();generation=current.battle;session=currentSession;battle=currentBattle;formation=match;actors=next;liveAtAdmission=living;
}
void* BeforeResult(const A::ResultCall&) noexcept {Sync();return nullptr;}
void NewBattle() noexcept {if(!owner||Own())Clear();}
void PumpField() noexcept {
    if(!enabled.load()||!Own()||!formation)return;
    if(session!=Runtime::SaveSession()||battle!=Runtime::BattleGeneration()){Clear();return;}
    for(auto& receipt:pending)if(receipt.monster){
        const auto flag=N::ReadOr<unsigned char>(image+ExecutableProfile::Rva<0xD2CA90>()+0xC89+Family(receipt.monster));
        if(!(flag&0x80))continue;
        const auto error=Runtime::GrantVictory(receipt);if(error==Error::None||error==Error::Stale)receipt={};
    }
}
void* BeforeTerminal(std::uint32_t) noexcept {
    if(!enabled.load()||!Own()||!formation||!reader)return nullptr;
    OmegaLastSeal::Runtime::Context current;
    if(!reader(current)||!current.original||current.battle!=generation||session!=Runtime::SaveSession()||battle!=Runtime::BattleGeneration())return nullptr;
    const auto outcome=OmegaLastSeal::Native::RawOutcome(image);if(outcome!=2&&outcome!=4)return nullptr;
    completion={};
    for(unsigned slot=0;slot<8;++slot){const auto& actor=actors[slot];if(!Same(slot)||actor.monster<334||actor.monster>343)continue;
        const unsigned card=actor.monster-334,family=Family(actor.monster);if(!(liveAtAdmission&(1u<<card))||(flagsBefore[family]&0x80))continue;
        if(N::ReadOr<unsigned>(actor.address+0x5D0)&&!(N::ReadOr<std::uint16_t>(actor.address+0x606)&1))continue;
        const unsigned stage=actor.monster==336||actor.monster==340?formation->stage:1;
        VictoryReceipt receipt{session,battle,0,0,actor.monster,stage,true,true};
        if(VictoryCard(receipt)<kCardCount)completion.receipts[card]=receipt;
    }
    return &completion;
}
void AfterTerminal(void* token,bool completed) noexcept {
    if(!completed||!enabled.load()||!Own())return;const auto frame=*static_cast<Completion*>(token);
    for(const auto& receipt:frame.receipts)if(receipt.monster){
        // The final Ixion/Yojimbo native event flag is written by the field
        // continuation after cleanup. It corroborates stage2/duel5; earlier
        // fights and loading a save cannot manufacture this pending receipt.
        if(receipt.monster==336||receipt.monster==340)pending[receipt.monster-334]=receipt;
        else (void)Runtime::GrantVictory(receipt);
    }
    liveAtAdmission=0;
}
const A::Observer actionObserver{BeforeResult,nullptr,nullptr,nullptr,nullptr};
const SharedBattleRuntime::ActionObserver battleObserver{NewBattle};
const BattleTerminal::Observer terminalObserver{BeforeTerminal,AfterTerminal};
bool Register(std::uintptr_t base,OmegaLastSeal::Runtime::Reader read,bool nativeHooks){
    if(enabled.load()||!read)return false;image=base;reader=read;
    if(nativeHooks){const EquipmentWorkshop::Evidence::Span evidence[]={OmegaLastSeal::Evidence::spans[3]};
        if(!ExecutableProfile::Steam20261001||!NativeUiSupport::Profile(base,evidence)||!IsBattleEndHookInstalled()||!A::Start(base))return false;}
    Pump empty=nullptr;if(!fieldPump.compare_exchange_strong(empty,&PumpField)&&empty!=&PumpField)return false;
    if(!A::Subscribe(A::Slot::ChallengeRewards,&actionObserver)||!SharedBattleRuntime::RegisterActionObserver(SharedBattleRuntime::ActionConsumer::ChallengeRewards,&battleObserver)||
       !BattleTerminal::Subscribe(BattleTerminal::Slot::ChallengeRewards,&terminalObserver)){Stop();return false;}
    enabled=true;return true;
}
}
bool Start(std::uintptr_t base,OmegaLastSeal::Runtime::Reader read){return Register(base,read,true);}
void Stop() noexcept {
    enabled=false;Pump value=&PumpField;(void)fieldPump.compare_exchange_strong(value,nullptr);
    A::Unsubscribe(A::Slot::ChallengeRewards,&actionObserver);SharedBattleRuntime::UnregisterActionObserver(SharedBattleRuntime::ActionConsumer::ChallengeRewards,&battleObserver);
    BattleTerminal::Unsubscribe(BattleTerminal::Slot::ChallengeRewards,&terminalObserver);if(!owner||Own())Clear();
}
bool Active() noexcept {return enabled.load();}
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t base,OmegaLastSeal::Runtime::Reader read){return Register(base,read,false);}
#endif
}
