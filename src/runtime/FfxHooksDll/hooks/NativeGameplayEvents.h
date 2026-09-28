#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace FfxHooks::NativeGameplayEvents {
enum class Kind {Load,Field,Aggregate,Battle,Turn,Damage};
struct Damage {unsigned user=255,target=255,commandId=0;const void* userPointer=nullptr;const void* targetPointer=nullptr;const void* command=nullptr;void* information=nullptr;};
enum class Provider {None,Workshop,Arcana};
struct Call {Kind kind=Kind::Field;unsigned actor=255;const void* source=nullptr;void* destination=nullptr;std::size_t size=0;};
struct Observer {void (*enter)(const Call&) noexcept=nullptr;void (*leave)(const Call&,bool) noexcept=nullptr;};
struct Ticket {Call call;std::array<const Observer*,4> listeners{};unsigned count=0;bool active=false;};
inline std::array<std::atomic<const Observer*>,4> observers{};
inline std::atomic<Provider> provider{Provider::None};
inline std::mutex registration;
inline bool Requested() noexcept {
    for(const auto& slot:observers)if(slot.load())return true;
    return false;
}
inline bool Subscribe(const Observer* value) noexcept {
    if(!value||!value->enter||!value->leave)return false;
    try {std::lock_guard<std::mutex> lock(registration);
        for(auto& slot:observers)if(slot.load()==value)return true;
        for(auto& slot:observers){const Observer* expected=nullptr;if(slot.compare_exchange_strong(expected,value))return true;}
    }catch(...){return false;}
    return false;
}
inline void Unsubscribe(const Observer* owner) noexcept {
    for(auto& slot:observers){const Observer* expected=owner;slot.compare_exchange_strong(expected,nullptr);}
}
inline Ticket Begin(const Call& call) noexcept {
    Ticket ticket;ticket.call=call;ticket.active=true;
    for(auto& slot:observers){const auto* value=slot.load();if(!value)continue;
        bool duplicate=false;for(unsigned i=0;i<ticket.count;++i)if(ticket.listeners[i]==value)duplicate=true;
        if(!duplicate)ticket.listeners[ticket.count++]=value;
    }
    for(unsigned i=0;i<ticket.count;++i)ticket.listeners[i]->enter(call);
    return ticket;
}
inline void End(Ticket& ticket,bool completed) noexcept {
    if(!ticket.active)return;
    ticket.active=false;
    // An entered scope closes in reverse order even after unsubscription; each
    // permanent observer can then unwind its own thread-local producer context.
    for(unsigned i=ticket.count;i>0;--i)ticket.listeners[i-1]->leave(ticket.call,completed);
}
}
