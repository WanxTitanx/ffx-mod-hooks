#pragma once
#include <atomic>
namespace FfxHooks::AeroEquipmentBridge {
// Providers have process lifetime. Detach only closes their admission gates.
struct Provider {
    bool (*valefor)() noexcept;
    unsigned (*sos)(unsigned owner,const void* actor) noexcept;
};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->valefor||!value->sos)return false;
    const Provider* empty=nullptr;
    return provider.compare_exchange_strong(empty,value)||empty==value;
}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline bool Valefor() noexcept {const auto* p=provider.load();return p&&p->valefor();}
inline unsigned Sos(unsigned owner,const void* actor) noexcept {
    const auto* p=provider.load();return p?p->sos(owner,actor):0;
}
}
