#pragma once
// Jarvis-HOOK: opt-in extension of RonsoPool's existing native cost/entry owner.
// No second cost-gate hook, pool-capacity override or save writer is introduced.
#include <atomic>
#include <cstdint>
namespace FfxHooks::RonsoPool::CommandCosts {
struct Quote {
    unsigned command=0,cost=0,charge=0,maximum=0;
    bool allowed=false;
};
struct Provider {bool (*quote)(unsigned,const unsigned char*,Quote&) noexcept;};
inline std::atomic<const Provider*> provider{nullptr};
inline std::atomic<bool> requested{false},nativeReady{false};
inline void Request(bool value) noexcept {requested.store(value);}
inline bool Requested() noexcept {return requested.load();}
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->quote)return false;const Provider* expected=nullptr;
    return provider.compare_exchange_strong(expected,value)||expected==value;
}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline bool Read(unsigned actor,const unsigned char* command,Quote& out) noexcept {
    out={};const auto* current=provider.load();
    if(!nativeReady.load()||!current||!command||!current->quote(actor,command,out))return false;
    if(out.command<0x3000||out.command>0x3FFF||out.maximum==0||out.maximum>255||
       out.charge>out.maximum||out.cost>255)out.allowed=false;
    return true;
}
using NativeGate=int(__cdecl*)(int,const unsigned char*,int);
inline std::atomic<NativeGate> gate{nullptr};
inline int EvaluateNative(unsigned actor,const unsigned char* command,int extraMp) noexcept {
    const auto call=gate.load();return nativeReady.load()&&call?call(static_cast<int>(actor),command,extraMp):-1;
}
}
