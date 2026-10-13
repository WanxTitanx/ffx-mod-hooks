#pragma once
#include <atomic>
#include <cstdint>
namespace FfxHooks::OmegaLastSeal::Bridge {
struct Provider {bool(*authoredStats)(std::uintptr_t) noexcept;void(*populated)() noexcept;};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {const Provider* empty=nullptr;return value&&value->authoredStats&&value->populated&&(provider.compare_exchange_strong(empty,value)||empty==value);}
inline void Unregister(const Provider* value) noexcept {(void)provider.compare_exchange_strong(value,nullptr);}
inline bool UsesAuthoredStats(std::uintptr_t base) noexcept {const auto* value=provider.load();return value&&value->authoredStats(base);}
inline void Populated() noexcept {const auto* value=provider.load();if(value)value->populated();}
}
