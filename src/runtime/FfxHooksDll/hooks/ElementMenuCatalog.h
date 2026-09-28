#pragma once
#include "ElementRegistry.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace FfxHooks::ElementMenu {
inline constexpr unsigned NativeCount=8,HookCount=2,Count=NativeCount+HookCount;
inline constexpr unsigned NativeBits[NativeCount]={1,2,4,8,0x10,0x80,0x20,0x40};
struct Item {char key[65]{},label[65]{};unsigned nativeBit=0,rgb=0xFFFFFF;bool available=false;};
using Catalog=std::array<Item,Count>;
struct Provider {bool (*read)(Catalog&) noexcept=nullptr;};
inline std::atomic<const Provider*> provider{nullptr};
inline Catalog Defaults() noexcept {
    Catalog result{};
    const char* names[]={"Fire","Ice","Thunder","Water","Holy","Darkness","Custom 1","Custom 2","Hook element 9","Hook element 10"};
    for(unsigned i=0;i<Count;++i){std::snprintf(result[i].label,sizeof(result[i].label),"%s",names[i]);
        if(i<NativeCount){result[i].nativeBit=NativeBits[i];result[i].available=true;}}
    return result;
}
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->read)return false;const Provider* empty=nullptr;
    return provider.compare_exchange_strong(empty,value)||empty==value;
}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline Catalog Read() noexcept {
    auto result=Defaults();const auto* source=provider.load(std::memory_order_acquire);Catalog candidate=result;
    if(!source||!source->read||!source->read(candidate)||provider.load()!=source)return result;
    for(unsigned i=0;i<Count;++i){const auto& item=candidate[i];
        if(!std::memchr(item.label,0,sizeof(item.label))||!std::memchr(item.key,0,sizeof(item.key))||
           !ElementalDominion::ValidLabel(item.label)||item.rgb>0xFFFFFF||
           item.nativeBit!=(i<NativeCount?NativeBits[i]:0)||
           (item.available&&i>=NativeCount&&!ElementalDominion::ValidKey(item.key)))return result;
        for(unsigned j=NativeCount;j<i;++j)if(item.key[0]&&std::strcmp(item.key,candidate[j].key)==0)return result;
    }
    return candidate;
}
// Native affinity masks are bytes. External identities never become fictitious
// native bits; all persistence and gameplay lookups retain their stable keys.
inline unsigned NativeIndex(unsigned bit) noexcept {
    for(unsigned i=0;i<NativeCount;++i)if(NativeBits[i]==bit)return i;return Count;
}
}
