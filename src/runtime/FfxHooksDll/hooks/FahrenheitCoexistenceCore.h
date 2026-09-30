#pragma once
// Jarvis-HOOK: no OS calls, configuration writes or foreign pointers in admission decisions.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace FfxHooks::Coexistence {
enum class Phase : std::uint32_t {
    Cold=0, StandaloneReady, PeerWaiting, PeerReady, StandaloneInstalling,
    PeerInstalling, StandaloneActive, PeerActive, Failed, PeerFailed,
    LatePeer, Stopped, PeerStopped, PeerConfiguringServices
};
enum class Block : std::uint32_t { None=0, ManagedSaveProtocol, PeerFileLoader, PeerBootstrap };
inline constexpr std::uint32_t kBridgeAbi=1, kBridgeCapabilities=3; // shared hooks + frame pump; no save authority
inline constexpr std::uint32_t kServicesAbi=2,kSaveService=4,kResourceService=8;
class State {
    static_assert(std::atomic<Phase>::is_always_lock_free,"Detach state must remain lock-free");
    std::atomic<Phase> phase_{Phase::Cold};
    std::atomic<std::uint32_t> services_{0};
    static bool Peer(Phase value) noexcept {
        return value==Phase::PeerWaiting || value==Phase::PeerReady ||
            value==Phase::PeerInstalling || value==Phase::PeerActive ||
            value==Phase::PeerFailed || value==Phase::LatePeer || value==Phase::PeerStopped ||
            value==Phase::PeerConfiguringServices;
    }
public:
    Phase Read() const noexcept {return phase_.load(std::memory_order_acquire);}
    bool PeerPresent() const noexcept {return Peer(Read());}
    bool ConfigureServices(std::uint32_t abi,std::uint32_t capabilities) noexcept {
        if(abi!=kServicesAbi||!capabilities||(capabilities&~(kSaveService|kResourceService)))return false;
        auto waiting=Phase::PeerWaiting;
        if(!phase_.compare_exchange_strong(waiting,Phase::PeerConfiguringServices,std::memory_order_acq_rel))return false;
        const auto previous=services_.load(std::memory_order_acquire);
        const bool accepted=!previous||previous==capabilities;
        if(accepted)services_.store(capabilities,std::memory_order_release);
        auto configuring=Phase::PeerConfiguringServices;
        // Stop may win while the contract is being installed. Never reopen it.
        return phase_.compare_exchange_strong(configuring,Phase::PeerWaiting,std::memory_order_acq_rel)&&accepted;
    }
    bool ServiceAllowed(std::uint32_t capability) const noexcept {
        const auto phase=Read();
        return (phase==Phase::PeerWaiting||phase==Phase::PeerReady||phase==Phase::PeerInstalling||phase==Phase::PeerActive)&&
            (services_.load(std::memory_order_acquire)&capability)==capability;
    }
    bool SaveServicesAllowed() const noexcept {return ServiceAllowed(kSaveService);}
    bool FileServicesAllowed() const noexcept {return ServiceAllowed(kResourceService);}
    void Observe(bool peer) noexcept {
        auto prior=Read();
        for(;;){
            auto next=prior;
            if(peer && !Peer(prior)){
                switch(prior){
                case Phase::Cold: case Phase::StandaloneReady: next=Phase::PeerWaiting;break;
                case Phase::Failed: next=Phase::PeerFailed;break;
                case Phase::Stopped: next=Phase::PeerStopped;break;
                default: next=Phase::LatePeer;break;
                }
            }else if(prior==Phase::Cold)next=Phase::StandaloneReady;
            if(next==prior || phase_.compare_exchange_weak(prior,next,std::memory_order_acq_rel))return;
        }
    }
    bool Ready(std::uint32_t abi,std::uint32_t capabilities) noexcept {
        if(abi!=kBridgeAbi || capabilities!=kBridgeCapabilities)return false;
        auto expected=Phase::PeerWaiting;
        if(phase_.compare_exchange_strong(expected,Phase::PeerReady,std::memory_order_acq_rel))return true;
        return expected==Phase::PeerReady || expected==Phase::PeerInstalling || expected==Phase::PeerActive;
    }
    bool TryStart() noexcept {
        auto prior=Read();
        for(;;){
            const auto next=prior==Phase::StandaloneReady?Phase::StandaloneInstalling:Phase::PeerInstalling;
            if(prior!=Phase::StandaloneReady && prior!=Phase::PeerReady)return false;
            if(phase_.compare_exchange_weak(prior,next,std::memory_order_acq_rel))return true;
        }
    }
    bool Finish(bool success) noexcept {
        auto prior=Read();
        if(prior!=Phase::StandaloneInstalling && prior!=Phase::PeerInstalling)return false;
        const bool peer=Peer(prior);
        const auto next=success?(peer?Phase::PeerActive:Phase::StandaloneActive):(peer?Phase::PeerFailed:Phase::Failed);
        return phase_.compare_exchange_strong(prior,next,std::memory_order_acq_rel);
    }
    void Stop() noexcept {
        auto prior=Read();
        for(unsigned attempt=0;attempt<16;++attempt){
            if(phase_.compare_exchange_strong(prior,Peer(prior)?Phase::PeerStopped:Phase::Stopped,std::memory_order_acq_rel))return;
        }
        phase_.store(Phase::PeerStopped,std::memory_order_release);
    }
    bool FrameAllowed() const noexcept {return Read()==Phase::PeerActive;}
    bool NativeSaveIoAllowed() const noexcept {
        const auto phase=Read();
        return phase==Phase::Cold || phase==Phase::StandaloneReady ||
            phase==Phase::StandaloneInstalling || phase==Phase::StandaloneActive;
    }
    bool NativeRenderAllowed() const noexcept {return NativeSaveIoAllowed();}
    bool SavePipelineAllowed() const noexcept {return NativeSaveIoAllowed()||SaveServicesAllowed();}
};
// One inline object is shared by production translation units. Isolated tests construct their own State.
inline State runtime;
// Installed only after the exact shared provider is pinned and initialized.
struct DetourApi {
    bool (*create)(std::uintptr_t,std::uintptr_t,std::uintptr_t*) noexcept;
    bool (*enable)(std::uintptr_t) noexcept;
    bool (*disable)(std::uintptr_t) noexcept;
    bool (*remove)(std::uintptr_t) noexcept;
};
inline std::atomic<const DetourApi*> detourApi{nullptr};
inline Block FeatureBlock(const State& state,const char* key) noexcept {
    if(!state.PeerPresent() || !key)return Block::None;
    if(std::strcmp(key,"development.fastload_autosave")==0)return Block::PeerBootstrap;
    if(std::strncmp(key,"language.text",13)==0)return state.FileServicesAllowed()?Block::None:Block::PeerFileLoader;
    if(state.SaveServicesAllowed())return Block::None;
    if(std::strncmp(key,"vanguard.",9)==0 || std::strncmp(key,"seymour.",8)==0 ||
       std::strncmp(key,"sphere_grid.",12)==0)return Block::ManagedSaveProtocol;
    for(const char* blocked:{"labs.equipment_workshop","labs.equipment_workshop_native_ui",
        "labs.kimahri_ronso_mana","labs.nova_super_damage","arcana.enabled",
        "development.arcana_full_deck","labs.grid_teach","labs.kimahri_lancet_dual_grant",
        "aeon_ascension.enabled","elemental.nul_spells"})
        if(std::strcmp(key,blocked)==0)return Block::ManagedSaveProtocol;
    return Block::None;
}
inline bool FeatureAllowed(const char* key) noexcept {return FeatureBlock(runtime,key)==Block::None;}
inline const char* BlockName(Block block) noexcept {
    switch(block){
    case Block::ManagedSaveProtocol:return "Fahrenheit managed-save adapter required";
    case Block::PeerFileLoader:return "Fahrenheit owns file redirection";
    case Block::PeerBootstrap:return "Fahrenheit owns startup and autosave selection";
    default:return "none";
    }
}
}
