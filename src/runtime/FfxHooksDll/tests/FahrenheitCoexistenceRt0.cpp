// Jarvis-HOOK: bootstrap and capability decisions are tested without Windows or the game.
#include <cstdio>
#if __has_include("../hooks/FahrenheitCoexistenceCore.h")
#include "../hooks/FahrenheitCoexistenceCore.h"
#include "../hooks/NativeSaveEvents.h"
#include <atomic>
#include <thread>
#include <vector>
using namespace FfxHooks::Coexistence;
static int checks=0, failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static void ReadSave(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
static void WriteSave(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
int main(){
    State standalone;
    Check(!standalone.TryStart(),"unobserved runtime cannot start");
    standalone.Observe(false);
    Check(standalone.NativeSaveIoAllowed() && standalone.NativeRenderAllowed(),"standalone capabilities are preserved");
    Check(standalone.TryStart() && !standalone.TryStart(),"standalone startup has one owner");
    Check(standalone.Finish(true) && !standalone.FrameAllowed(),"standalone has no foreign frame owner");
    standalone.Observe(true);
    Check(standalone.Read()==Phase::LatePeer,"late peer arrival rejects provider switching");
    Check(!standalone.Ready(1,3) && !standalone.NativeSaveIoAllowed(),"late arrival cannot reopen persistent writes");
    State peer; peer.Observe(true);
    Check(peer.PeerPresent() && !peer.TryStart(),"peer discovery waits for its committed bootstrap");
    Check(!peer.NativeSaveIoAllowed() && !peer.NativeRenderAllowed(),"managed saves and render ownership cannot be inferred from detection");
    Check(!peer.Ready(0,3) && !peer.Ready(2,3) && !peer.Ready(1,7),"wrong ABI and unimplemented save capability are rejected");
    Check(!peer.Ready(1,1) && !peer.Ready(1,2),"partial bridge cannot admit dependent runtime");
    Check(peer.Ready(1,3) && peer.Ready(1,3),"validated handshake is idempotent");
    peer.Observe(false);
    Check(peer.PeerPresent(),"a missing later module observation cannot forget a peer");
    std::atomic<unsigned> winners{0}; std::vector<std::thread> threads;
    for(unsigned i=0;i<16;++i)threads.emplace_back([&]{if(peer.TryStart())++winners;});
    for(auto& thread:threads)thread.join();
    Check(winners==1,"simultaneous loader and bridge workers initialize once");
    Check(!peer.FrameAllowed() && peer.Finish(true) && peer.FrameAllowed(),"frame callbacks wait for complete native startup");
    Check(!peer.NativeSaveIoAllowed(),"render readiness does not grant save projection authority");
    for(const char* key:{"labs.equipment_workshop","labs.kimahri_ronso_mana","arcana.enabled","development.fastload_autosave","labs.grid_teach","aeon_ascension.enabled","vanguard.enabled","elemental.nul_spells"})
        Check(FeatureBlock(peer,key)!=Block::None,"persistent or boot-dependent feature is visibly unavailable");
    for(const char* key:{"weapon_strike_vfx.enabled","elemental.core","boosters.ap","input.dialog_skip"})
        Check(FeatureBlock(peer,key)==Block::None,"independent feature is not globally disabled");
    peer.Stop(); peer.Stop();
    Check(!peer.FrameAllowed() && !peer.TryStart() && !peer.Ready(1,3),"stop is terminal and repeated callbacks are inert");
    State failed; failed.Observe(true);failed.Ready(1,3);failed.TryStart();failed.Finish(false);
    Check(!failed.FrameAllowed() && !failed.TryStart() && !failed.Ready(1,3),"failed native startup cannot become ready on a later frame");
    State failedStandalone; failedStandalone.Observe(false);
    Check(failedStandalone.TryStart() && failedStandalone.Finish(false),"standalone failure is recorded");
    Check(!failedStandalone.NativeSaveIoAllowed() && !failedStandalone.NativeRenderAllowed(),
        "failed standalone startup cannot admit new I/O or render owners");
    State stoppedStandalone; stoppedStandalone.Observe(false); stoppedStandalone.Stop();
    Check(!stoppedStandalone.NativeSaveIoAllowed() && !stoppedStandalone.NativeRenderAllowed(),
        "terminal standalone stop revokes native capabilities");
    stoppedStandalone.Observe(true);
    Check(stoppedStandalone.Read()==Phase::PeerStopped && !stoppedStandalone.Ready(1,3),
        "peer detection after stop preserves terminal state");
    static const FfxHooks::NativeSaveEvents::Observer observer{ReadSave,WriteSave};
    Check(FfxHooks::NativeSaveEvents::Subscribe(&observer),"standalone native save subscriptions remain available");
    FfxHooks::NativeSaveEvents::Unsubscribe(&observer);
    runtime.Observe(true);
    Check(!FfxHooks::NativeSaveEvents::Subscribe(&observer)&&!FfxHooks::NativeSaveEvents::Requested(),
        "actual native save registry rejects subscribers while Fahrenheit owns persistence");
    Check(runtime.Ready(1,3)&&runtime.TryStart()&&runtime.Finish(true),"shared hook/render bridge can become active");
    Check(!FfxHooks::NativeSaveEvents::Subscribe(&observer)&&!FfxHooks::NativeSaveEvents::Requested(),
        "successful graphics handshake cannot enable unimplemented managed save delivery");
    std::printf("Fahrenheit coexistence RT0: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
#else
int main(){std::puts("FAIL: Fahrenheit bootstrap/capability policy is missing");return 1;}
#endif
