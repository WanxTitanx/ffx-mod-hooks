// Jarvis-HOOK: Phase Rotation observes the shared native CTB owner. Vanguard
// may subscribe independently; neither installs or retires the other's prologue.
#include "PhaseTurnEdgeHook.h"
#include "NativeGameplayEvents.h"
#include "../shared/ffx_addresses.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SharedTurnRuntime.h"
#include <atomic>
#include <cstdio>
#endif
namespace FfxHooks {
#ifdef FFXHOOKS_HAVE_POLYHOOK
namespace {
std::atomic<PhaseTurnEdgeLogFn> logFn{nullptr};
std::atomic<PhaseTurnEdgeCallback> callback{nullptr};
std::atomic<bool> installed{false};
std::atomic<long> fireCount{0};
std::atomic<std::uint32_t> lastActor{0};
std::atomic<std::uintptr_t> lastPointer{0};
std::uintptr_t imageBase=0;
void OnEdge(unsigned slot,void* actor,std::uint32_t) noexcept {
    if(!installed.load())return;
    const long sequence=fireCount.fetch_add(1)+1;
    lastActor.store(slot);lastPointer.store(reinterpret_cast<std::uintptr_t>(actor));
    if(const auto log=logFn.load()){
        char line[192]{};
        std::snprintf(line,sizeof(line),"[ffx-hooks] PhaseTurnEdge #%ld battleActive=1 n6=%u a2=0x%08X\n",
            sequence,slot,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actor)));
        log(line);
    }
    auto event=NativeGameplayEvents::Begin({NativeGameplayEvents::Kind::Turn,slot,actor,nullptr,static_cast<std::size_t>(sequence)});
    NativeGameplayEvents::End(event,true);
    if(const auto notify=callback.load()){
        const PhaseTurnEdgeEvent event{1,slot,reinterpret_cast<std::uintptr_t>(actor),sequence};
        notify(event);
    }
}
const SharedTurn::Observer observer{OnEdge};
}
PhaseTurnEdgeInstallResult InstallPhaseTurnEdgeHook(uintptr_t base,PhaseTurnEdgeLogFn log){
    if(installed.load())return {imageBase==base,2};
    if(!SharedTurn::Start(base)||!SharedTurn::Register(SharedTurn::Consumer::PhaseRotation,&observer))return {false,4};
    imageBase=base;logFn.store(log);installed.store(true);return {true,0};
}
void RemovePhaseTurnEdgeHook(){
    installed.store(false);SharedTurn::Unregister(SharedTurn::Consumer::PhaseRotation,&observer);
    callback.store(nullptr);logFn.store(nullptr);
    // Applied dispatchers/trampolines have process lifetime. A suspended native
    // entrant is safe even when the other subscriber remains active.
}
bool IsPhaseTurnEdgeHookInstalled(){return installed.load();}
long PhaseTurnEdgeHookFireCount(){return fireCount.load();}
uint32_t PhaseTurnEdgeLastActorIndex(){return lastActor.load();}
uintptr_t PhaseTurnEdgeLastActorPtr(){return lastPointer.load();}
void SetPhaseTurnEdgeCallback(PhaseTurnEdgeCallback value){callback.store(value);}
#else
PhaseTurnEdgeInstallResult InstallPhaseTurnEdgeHook(uintptr_t,PhaseTurnEdgeLogFn){return {false,3};}
void RemovePhaseTurnEdgeHook(){}
bool IsPhaseTurnEdgeHookInstalled(){return false;}
long PhaseTurnEdgeHookFireCount(){return 0;}
uint32_t PhaseTurnEdgeLastActorIndex(){return 0;}
uintptr_t PhaseTurnEdgeLastActorPtr(){return 0;}
void SetPhaseTurnEdgeCallback(PhaseTurnEdgeCallback){}
#endif
}
