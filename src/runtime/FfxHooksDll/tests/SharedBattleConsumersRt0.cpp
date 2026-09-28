// Jarvis-HOOK: independent lifecycle subscriptions do not replace Vanguard.
#include "../hooks/SharedBattleRuntime.h"
#include <cstdio>
#include <initializer_list>
namespace B=FfxHooks::SharedBattleRuntime;
static unsigned checks=0,failures=0,primaryCalls=0,secondaryCalls=0,originalCalls=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void Primary() noexcept {++primaryCalls;}
static void Secondary() noexcept {++secondaryCalls;}
static int Original(void*){++originalCalls;return 27;}
int main(){
    const B::ActionObserver primary{Primary},secondary{Secondary},other{Secondary};
    Check(B::RegisterActionObserver(&primary),"existing primary registers");
    Check(B::RegisterActionObserver(B::ActionConsumer::Elemental,&secondary),"Elemental can subscribe beside Vanguard");
    Check(!B::RegisterActionObserver(B::ActionConsumer::Elemental,&other),"a second descriptor cannot replace that slot");
    Check(B::RegisterActionObserver(B::ActionConsumer::Elemental,&secondary),"idempotent subscription is safe");
    for(const auto caller:{B::kBootstrapInitSceneReturnRva,B::kSphereGridStartupInitSceneReturnRva,std::uintptr_t(123)}){
        const auto result=B::RunInitScene(caller,{nullptr,Original},{},{});
        Check(result.originalResult==27&&!primaryCalls&&!secondaryCalls,"nonbattle callers remain original-only");
    }
    const auto before=originalCalls;
    B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Original},{},{});
    Check(originalCalls==before+1&&primaryCalls==1&&secondaryCalls==1,"both consumers receive one boundary around one native call");
    B::UnregisterActionObserver(&primary);
    Check(B::AnyConsumerRequiresRuntime({}),"secondary alone retains the required native infrastructure");
    B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Original},{},{});
    Check(primaryCalls==1&&secondaryCalls==2,"one consumer can stop independently");
    B::UnregisterActionObserver(B::ActionConsumer::Elemental,&other);
    Check(B::AnyConsumerRequiresRuntime({}),"only the registered owner can unsubscribe its slot");
    B::UnregisterActionObserver(B::ActionConsumer::Elemental,&secondary);
    Check(!B::AnyConsumerRequiresRuntime({}),"last subscriber releases its runtime request");
    std::printf("SHARED_BATTLE_CONSUMERS_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
