#include "../hooks/NativeGameplayEvents.h"
#include <cstdio>
using namespace FfxHooks::NativeGameplayEvents;
static unsigned checks=0,failures=0,entries=0,exits=0,failed=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static void Enter(const Call& call) noexcept {if(call.kind==Kind::Field&&call.actor==3)++entries;}
static void Leave(const Call& call,bool ok) noexcept {if(call.kind==Kind::Field&&call.actor==3){++exits;if(!ok)++failed;}}
int main(){
    const Observer a{Enter,Leave},b{Enter,Leave};
    auto ticket=Begin({Kind::Field,3});End(ticket,true);Check(!entries&&!exits,"OFF producer has no consumer effects");
    Check(Subscribe(&a)&&Subscribe(&b)&&Subscribe(&a),"independent consumers register once each");
    ticket=Begin({Kind::Field,3});Unsubscribe(&a);Unsubscribe(&b);End(ticket,true);
    Check(entries==2&&exits==2,"entered scopes unwind after admission closes");
    End(ticket,false);Check(exits==2,"scope completion cannot run twice");
    Subscribe(&a);ticket=Begin({Kind::Field,3});End(ticket,false);
    Check(failed==1,"native failure is reported without pretending completed producer output");
    Unsubscribe(&a);
    std::printf("NativeGameplayEventsRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
