#pragma once
#include <atomic>
#include <cstdint>
namespace FfxHooks::OmegaT7::Bridge {
struct Provider {bool(*available)(unsigned,unsigned) noexcept;void(*populated)() noexcept;void(*review)() noexcept;};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->available||!value->populated||!value->review)return false;
    const Provider* empty=nullptr;return provider.compare_exchange_strong(empty,value)||empty==value;
}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline bool Available(unsigned field,unsigned formation) noexcept {const auto* p=provider.load();return p&&p->available(field,formation);}
inline void Populated() noexcept {const auto* p=provider.load();if(p)p->populated();}
inline void Review() noexcept {const auto* p=provider.load();if(p)p->review();}
// The actor writer supplies evidence only. This callback cannot grant writes.
struct ScaleProvider {bool(*ownsMiniHp)(unsigned,std::uintptr_t,std::uint32_t,std::uint32_t) noexcept;};
inline std::atomic<const ScaleProvider*> scaleProvider{nullptr};
inline bool RegisterScale(const ScaleProvider* value) noexcept {
    if(!value||!value->ownsMiniHp)return false;
    const ScaleProvider* empty=nullptr;return scaleProvider.compare_exchange_strong(empty,value)||empty==value;
}
inline void UnregisterScale(const ScaleProvider* value) noexcept {scaleProvider.compare_exchange_strong(value,nullptr);}
inline bool OwnsMiniHp(unsigned id,std::uintptr_t actor,std::uint32_t resource,std::uint32_t hp) noexcept {
    const auto* p=scaleProvider.load();return p&&p->ownsMiniHp(id,actor,resource,hp)&&scaleProvider.load()==p;
}
}
