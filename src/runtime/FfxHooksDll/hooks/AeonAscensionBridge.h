#pragma once
#include "AeonAscensionCore.h"
#include <atomic>

namespace FfxHooks::AeonAscension {
// The loaded-data owner publishes one process-lifetime provider. Workshop
// consumes a fresh proof at preview, confirmation and every effect query.
struct Provider {bool (*read)(Mapping&) noexcept;};
inline std::atomic<const Provider*> provider{nullptr};
inline bool RegisterProvider(const Provider* value) noexcept {
    if(!value||!value->read)return false;
    const Provider* empty=nullptr;
    return provider.compare_exchange_strong(empty,value)||empty==value;
}
inline void UnregisterProvider(const Provider* value) noexcept {
    const Provider* expected=value;(void)provider.compare_exchange_strong(expected,nullptr);
}
inline bool ReadMapping(Mapping& output) noexcept {
    output={};const auto* source=provider.load();
    if(source&&source->read(output)&&ValidMapping(output))return true;
    output={};return false;
}
} // namespace FfxHooks::AeonAscension
