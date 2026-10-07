// Jarvis-HOOK: real producer entry, eleven-argument ABI and scoped cleanup.
#include "../hooks/CombatExtensionBus.h"
namespace CombatProducerFixture {
namespace B=FfxHooks::CombatExtensions;
using Producer=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
Producer entry=nullptr;
unsigned enters=0,leaves=0,aborts=0,calls=0;
unsigned char originalCommand[96]{},privateCommand[96]{};
bool nested=false,raiseFault=false,expectPrivate=true;
void* Enter(const B::DamageCall& call,const void*& forward) noexcept {
    ++enters;
    if(call.commandId==0x3042){Check(call.command==originalCommand,"observer retains the original command identity");forward=privateCommand;}
    return reinterpret_cast<void*>(std::uintptr_t(call.commandId));
}
void Leave(void* token,const B::DamageCall& call,unsigned,bool completed) noexcept {
    ++leaves;if(!completed)++aborts;
    Check(std::uintptr_t(token)==call.commandId,"producer retires the matching observer token");
}
void Cap(void*,const B::DamageCall& call,B::CapRequests& out) noexcept {out.magicEligible=call.commandId==0x3042;}
const B::Observer observer{Enter,Leave,Cap};
unsigned __cdecl Endpoint(unsigned user,void* userPtr,unsigned target,void* targetPtr,const void* command,
                         unsigned commandId,void* info,unsigned a8,unsigned a9,unsigned a10,unsigned a11){
    ++calls;
    Check(user==8&&userPtr==reinterpret_cast<void*>(0x1110)&&target==20&&targetPtr==reinterpret_cast<void*>(0x2220)&&
          info==reinterpret_cast<void*>(0x3330)&&a8==0x4444&&a9==0x5555&&a10==0x6666&&a11==0x7777,
          "producer arguments retain their exact width and order");
    Check(command==(expectPrivate&&commandId==0x3042?privateCommand:originalCommand),"only the admitted private command pointer changes");
    const int before=B::UpperDamage(450000,99999,user,commandId,3,false);
    Check(before==(expectPrivate&&commandId==0x3042?450000:99999),"shared cap receives the real producer context");
    if(raiseFault)RaiseException(0xE0420707,0,0,nullptr);
    if(nested&&commandId==0x3042){
        Check(entry(user,userPtr,target,targetPtr,originalCommand,0x3000,info,a8,a9,a10,a11)==0xABCD1234,"nested native producer returns once");
        Check(B::UpperDamage(450000,99999,user,commandId,3,false)==before,"nested return restores the parent policy");
    }
    return 0xABCD1234;
}
bool Invoke() {
    __try{return entry(8,reinterpret_cast<void*>(0x1110),20,reinterpret_cast<void*>(0x2220),originalCommand,
                       0x3042,reinterpret_cast<void*>(0x3330),0x4444,0x5555,0x6666,0x7777)==0xABCD1234;}
    __except(GetExceptionCode()==0xE0420707?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void Run(std::uintptr_t base){
    entry=reinterpret_cast<Producer>(base+(::FfxHooks::ExecutableProfile::Rva<0x38E680>()));
    Check(B::Subscribe(B::Slot::Elemental,&observer),"producer fixture registers its observer");
    DamageProducerForTests(reinterpret_cast<void*>(&Endpoint));nested=true;
    Check(Invoke()&&calls==2&&enters==2&&leaves==2,"outer and nested calls dispatch exactly once");
    Check(B::currentDamage==nullptr,"completed producer leaves no stale frame");
    nested=false;raiseFault=true;
    Check(!Invoke()&&aborts==1&&enters==leaves&&B::currentDamage==nullptr,"native exception retires the admitted frame");
    raiseFault=false;expectPrivate=false;const unsigned before=enters;
    Check(B::Unsubscribe(B::Slot::Elemental,&observer),"observer admission closes independently");
    Check(Invoke()&&enters==before,"unsubscribed producer forwards vanilla arguments and cap");
    Check(B::Subscribe(B::Slot::Elemental,&observer),"stop test retains a published descriptor");RequestStop();
    Check(Invoke()&&enters==before&&B::currentDamage==nullptr,"stopped detour cannot dispatch or inherit extension state");
    Check(B::Unsubscribe(B::Slot::Elemental,&observer),"fixture releases its descriptor");
}
}
