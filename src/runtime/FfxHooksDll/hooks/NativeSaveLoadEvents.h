#pragma once
#include "NativeSaveEvents.h"

namespace FfxHooks::NativeSaveEvents {
// Uses the existing immutable observer registry, not a second publisher/owner.
struct LoadDispatch {
    std::uint64_t cookie=0;
    DispatchSnapshot recipients{};
};
inline std::atomic<std::uint64_t> nativeLoadSerial{0};
inline LoadDispatch BeginLoad(void* destination,const void* source) noexcept {
    LoadDispatch frame{};
    // A failed load aimed at the active destination still retires the old
    // session. Observers validate the source; missing bytes are not a preview.
    if(!destination)return frame;
    const auto snapshot=CaptureObservers();bool any=false;
    for(std::size_t i=0;i<snapshot.size();++i){
        const auto* recipient=snapshot[i];
        if(recipient&&recipient->loadStarting&&recipient->loadCompleted){frame.recipients[i]=recipient;any=true;}
    }
    if(!any)return frame;
    auto previous=nativeLoadSerial.load(std::memory_order_acquire);
    for(;;){
        if(previous==UINT64_MAX)return {};
        if(nativeLoadSerial.compare_exchange_weak(previous,previous+1,std::memory_order_acq_rel,std::memory_order_acquire))break;
    }
    frame.cookie=previous+1;
    for(const auto* recipient:frame.recipients)if(recipient)recipient->loadStarting(frame.cookie,destination,source);
    return frame;
}
inline void EndLoad(LoadDispatch& frame,bool completed) noexcept {
    const auto cookie=frame.cookie;
    if(!cookie)return;
    frame.cookie=0; // Repeated finalization of the same call is inert.
    for(const auto* recipient:frame.recipients)if(recipient)recipient->loadCompleted(cookie,completed);
}
} // namespace FfxHooks::NativeSaveEvents
