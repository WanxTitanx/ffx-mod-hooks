#pragma once
#include "../../../../research/equipment_workshop/include/workshop.h"
#include <atomic>

namespace FfxHooks::EquipmentWorkshop::CatalogBridge {
struct Provider {
    bool (*read)(workshop::Catalog&) noexcept;
    const char* (*name)(unsigned) noexcept;
};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->read||!value->name)return false;
    const Provider* expected=nullptr;
    return provider.compare_exchange_strong(expected,value)||expected==value;
}
inline void Unregister(const Provider* value) noexcept {
    const Provider* expected=value;(void)provider.compare_exchange_strong(expected,nullptr);
}
inline bool Read(workshop::Catalog& out) noexcept {
    out={};const auto* current=provider.load();
    if(current&&current->read(out)&&workshop::ValidCatalog(out))return true;
    out={};return false;
}
inline const char* Name(unsigned word) noexcept {
    const auto* current=provider.load();const char* value=current?current->name(word):nullptr;
    return value?value:"Unmapped ability";
}
}
