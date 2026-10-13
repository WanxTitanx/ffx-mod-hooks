#pragma once
#include "SinNaturalCore.h"
#include "SinRamScalingCore.h"
#include <atomic>
namespace FfxHooks::CaptureCascade::Bridge {
using Original=bool(*)(void*,int*);
using CurrentRequest=bool(*)(std::uint32_t,std::int32_t) noexcept;
struct Provider {
    bool (*natural)(const SinNatural::Evidence&,std::uint32_t,CurrentRequest) noexcept=nullptr;
    SinRam::Config (*effective)(unsigned,const SinRam::Config&) noexcept=nullptr;
    bool (*battle)(std::uintptr_t,void*,Original,int*) noexcept=nullptr;
};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->natural||!value->effective||!value->battle)return false;
    const Provider* expected=nullptr;return provider.compare_exchange_strong(expected,value)||expected==value;
}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline void Natural(const SinNatural::Evidence& evidence,std::uint32_t request,CurrentRequest current) noexcept {const auto* p=provider.load();if(p)p->natural(evidence,request,current);}
inline SinRam::Config Effective(unsigned field,const SinRam::Config& config) noexcept {const auto* p=provider.load();return p?p->effective(field,config):config;}
inline bool Battle(std::uintptr_t base,void* context,Original original,int* result) noexcept {
    const auto* p=provider.load();return p?p->battle(base,context,original,result):(original&&result&&original(context,result));
}
}
