// Jarvis-HOOK: shared producer/upper-clamp composition and interrupted scopes.
#include <cstdio>
#include <cstdint>
#include <thread>

#if __has_include("../hooks/CombatExtensionBus.h")
#include "../hooks/CombatExtensionBus.h"
namespace {
namespace B=FfxHooks::CombatExtensions;
unsigned checks=0,failures=0,entered=0,left=0,aborted=0;
void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
int marker=0;
void* Enter(const B::DamageCall& call,const void*& command) noexcept {
    ++entered;if(call.commandId==0x3042)command=&marker;
    return reinterpret_cast<void*>(std::uintptr_t(call.commandId));
}
void Leave(void* token,const B::DamageCall& call,unsigned,bool completed) noexcept {
    ++left;if(!completed)++aborted;if(std::uintptr_t(token)!=call.commandId)++failures;
}
void Magic(void*,const B::DamageCall& call,B::CapRequests& requests) noexcept {
    requests.magicEligible=call.commandId==0x3042;
}
void Aeon(void*,const B::DamageCall& call,B::CapRequests& requests) noexcept {
    requests.aeonAuthorized=call.user==8;if(call.target==9)requests.Nonlethal(5000);
}
const B::Observer first{Enter,Leave,Magic};
const B::Observer second{nullptr,nullptr,Aeon};
#ifdef FFXHOOKS_COMBAT_AMOUNT_POLICY_V1
int Modify(void*,const B::DamageCall&,int amount) noexcept {return amount*2;}
const B::Observer modifier{nullptr,nullptr,nullptr,nullptr,Modify};
#endif
}
int main(){
    Check(!B::Required(),"empty bus requests no shared hooks");
    Check(!B::Subscribe(B::Slot::Elemental,nullptr),"null registration is rejected");
    Check(B::Subscribe(B::Slot::Elemental,&first),"first immutable observer registers");
    Check(B::Subscribe(B::Slot::Elemental,&first),"same observer registration is idempotent");
    Check(!B::Subscribe(B::Slot::Elemental,&second),"another module cannot replace an occupied slot");
    Check(B::Subscribe(B::Slot::Aeon,&second),"independent observer registers");
    B::DamageCall call{};call.user=8;call.target=1;call.commandId=0x3042;
    call.command=reinterpret_cast<const void*>(std::uintptr_t(1));
    B::DamageScope outer{};
    Check(B::EnterDamage(outer,call)&&outer.forwardCommand==&marker,"producer uses only the admitted private command view");
    Check(B::UpperDamage(1200000,99999,8,0x3042,3,false)==999999,"finite magic and Aeon limits compose once");
    Check(B::UpperDamage(450000,9999,8,0x3042,3,false)==9999,"observers cannot invent effective native BDL");
    Check(B::UpperDamage(450000,99999,8,0x3042,2,false)==99999,"MP remains outside finite HP requests");
    Check(B::UpperDamage(450000,99999,9,0x3042,3,false)==99999,"another actor cannot borrow the active scope");
    bool isolated=false;
    std::thread other([&]{isolated=B::UpperDamage(450000,99999,8,0x3042,3,false)==99999;});other.join();
    Check(isolated,"another thread cannot see the active damage scope");
    B::DamageCall nestedCall=call;nestedCall.user=3;nestedCall.target=9;nestedCall.commandId=0x3073;
    B::DamageScope nested{};Check(B::EnterDamage(nested,nestedCall),"nested counter enters");
    Check(B::UpperDamage(450000,99999,3,0x3073,3,true)==4999,"nested nonlethal target survives legacy Nova");
    Check(!B::LeaveDamage(outer,0,true),"out-of-order retirement preserves the inner context");
    Check(B::LeaveDamage(nested,0,false),"exception retirement restores the parent");
    Check(aborted==1&&B::UpperDamage(450000,99999,8,0x3042,3,false)==450000,"parent eligibility survives interrupted nesting");
    Check(!B::Unsubscribe(B::Slot::Elemental,&second),"only the slot owner can unsubscribe");
    Check(B::Unsubscribe(B::Slot::Elemental,&first),"stop immediately closes new policy admission");
    Check(B::Unsubscribe(B::Slot::Aeon,&second),"second owner stops independently");
    Check(B::UpperDamage(450000,99999,8,0x3042,3,false)==99999,"retired descriptor is only retained for cleanup");
    Check(B::LeaveDamage(outer,123,true)&&entered==left,"every callback token retires exactly once");
    Check(!B::LeaveDamage(outer,123,true),"duplicate leave is rejected");
    Check(!B::Required(),"last unsubscribe releases the runtime request");
    Check(B::UpperDamage(1200000,99999,3,0x3073,3,true)==1200000,"standalone Nova preserves its old opt-in behavior");
    B::CapRequests bounds{};bounds.Nonlethal(5000);bounds.Nonlethal(1000);bounds.Nonlethal(9000);
    Check(bounds.nonlethal&&bounds.targetCurrentHp==1000,"several nonlethal requests select the tightest bound");
    Check(B::Subscribe(B::Slot::Elemental,&first),"test descriptor registers again");
    B::DamageScope frames[B::MaximumDepth+1]{};const unsigned before=entered;
    for(unsigned i=0;i<B::MaximumDepth+1;++i)B::EnterDamage(frames[i],call);
    Check(entered-before==B::MaximumDepth,"bounded recursion cannot allocate another extension frame");
    Check(B::UpperDamage(450000,99999,8,0x3042,3,false)==99999,"overflow forwards native behavior without borrowing a parent");
    for(unsigned i=B::MaximumDepth+1;i>0;--i)Check(B::LeaveDamage(frames[i-1],0,true),"scopes retire in reverse order");
    Check(B::Unsubscribe(B::Slot::Elemental,&first)&&entered==left,"all nested tokens are retired");
#ifdef FFXHOOKS_COMBAT_AMOUNT_POLICY_V1
    Check(B::Subscribe(B::Slot::Reserved,&modifier),"precap modifier registers independently");
    B::DamageScope modified{};B::EnterDamage(modified,call);
    Check(B::UpperDamage(60000,99999,8,0x3042,3,false)==99999,"positive HP modifier executes before the upper clamp");
    Check(B::UpperDamage(60000,99999,8,0x3042,3,true)==120000,"Nova bypass retains the modifier");
    Check(B::UpperDamage(500,99999,8,0x3042,2,false)==500&&B::UpperDamage(500,99999,8,0x3042,1,false)==500,"MP and CTB bypass HP modifiers");
    Check(B::UpperDamage(-500,99999,8,0x3042,3,false)==-500,"absorption keeps native sign and amount");
    Check(B::UpperDamage(500,99999,7,0x3042,3,false)==500,"another actor cannot borrow the modifier");
    B::Unsubscribe(B::Slot::Reserved,&modifier);
    Check(B::UpperDamage(500,99999,8,0x3042,3,false)==500,"stop closes active modifier admission");
    Check(B::LeaveDamage(modified,0,true),"modifier retires normally");
#else
    Check(false,"shared positive-HP modifier interface is missing");
#endif
    std::printf("COMBAT_EXTENSION_BUS_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production CombatExtensionBus.h is missing");return 1;}
#endif
