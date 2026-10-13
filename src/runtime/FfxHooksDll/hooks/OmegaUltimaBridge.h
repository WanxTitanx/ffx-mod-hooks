#pragma once
#include "SinNaturalCore.h"
#include <atomic>
namespace FfxHooks::OmegaUltima::Bridge {
using Original=bool(*)(void*,int*);
using CurrentRequest=bool(*)(std::uint32_t,std::int32_t) noexcept;
struct Provider {
    void(*natural)(const SinNatural::Evidence&,std::uint32_t,CurrentRequest) noexcept;
    bool(*battle)(std::uintptr_t,void*,Original,int*) noexcept;
};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->natural||!value->battle)return false;
    const Provider* empty=nullptr;return provider.compare_exchange_strong(empty,value)||empty==value;
}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline void Natural(const SinNatural::Evidence& evidence,std::uint32_t request,CurrentRequest current) noexcept {
    const auto* p=provider.load();if(p)p->natural(evidence,request,current);
}
inline bool Battle(std::uintptr_t base,void* context,Original original,int* result) noexcept {
    const auto* p=provider.load();return p?p->battle(base,context,original,result):(original&&result&&original(context,result));
}
}
