#pragma once
#include "ElementRegistry.h"
#include "ElementIdentity.h"
#include "ElementNameSettings.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace FfxHooks::ElementMenu {
inline constexpr unsigned NativeCount=ElementIdentity::NativeCount,HookCount=ElementIdentity::HookCount,Count=ElementIdentity::Count;
inline constexpr const auto& NativeBits=ElementIdentity::NativeBits;
struct Item {char key[65]{},label[65]{};unsigned nativeBit=0,rgb=0xFFFFFF;bool available=false;};
using Catalog=std::array<Item,Count>;
struct Provider {bool (*read)(Catalog&) noexcept=nullptr;};
inline std::atomic<const Provider*> provider{nullptr};
inline Catalog Defaults() noexcept {
    Catalog result{};
    for(unsigned i=0;i<Count;++i){const auto& item=ElementIdentity::Defaults[i];
        std::snprintf(result[i].label,sizeof(result[i].label),"%s",item.label);
        std::snprintf(result[i].key,sizeof(result[i].key),"%s",item.key);
        result[i].rgb=item.rgb;result[i].nativeBit=item.nativeBit;result[i].available=i<NativeCount;}
    return result;
}
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->read)return false;
    const Provider* empty=nullptr;
    return provider.compare_exchange_strong(empty,value)||empty==value;
}
inline void Unregister(const Provider* value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline Catalog Raw() noexcept {
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
inline Catalog Read() noexcept {
    auto result=Raw();
    try{ElementNames::Apply(result);}catch(...){return Raw();}
    return result;
}
inline unsigned Find(const Catalog& catalog,unsigned nativeBit,const char* key) noexcept {
    for(unsigned i=0;i<Count;++i){
        if(nativeBit?catalog[i].nativeBit==nativeBit:
           (!catalog[i].nativeBit&&key&&std::strcmp(catalog[i].key,key)==0))return i;
    }return Count;
}
inline ElementNames::Result SaveName(unsigned nativeBit,const char* key,std::string_view text,bool reset=false){
    using ElementNames::Result;
    if(!ElementNames::Editable(nativeBit))return Result::Unavailable;
    const auto current=Read();const unsigned target=Find(current,nativeBit,key);
    if(target==Count)return Result::Unavailable;
    char name[65]{};
    if(reset){const auto canonical=Raw();const unsigned original=Find(canonical,nativeBit,key);
        if(original==Count)return Result::Unavailable;
        std::snprintf(name,sizeof(name),"%s",canonical[original].label);
        if(const auto* previous=ElementNames::PromotionFallback(nativeBit,key,name)){
            for(unsigned i=0;i<Count;++i)if(i!=target&&ElementNames::Same(name,current[i].label)){
                std::snprintf(name,sizeof(name),"%s",previous);break;
            }
        }
    }else {const auto result=ElementNames::Normalize(text,name);if(result!=Result::Saved)return result;}
    for(unsigned i=0;i<Count;++i)if(i!=target&&ElementNames::Same(name,current[i].label))return Result::Duplicate;
    return ElementNames::Store(nativeBit,current[target].key,reset?"":name)?Result::Saved:Result::WriteFailed;
}
inline void DisplayName(unsigned nativeBit,const char* key,const char* canonical,char* out,std::size_t capacity) noexcept {
    if(!out||!capacity)return;
    const auto catalog=Read();const unsigned row=Find(catalog,nativeBit,key);
    std::snprintf(out,capacity,"%s",row<Count?catalog[row].label:canonical?canonical:"Element");
}
// Native affinity masks are bytes. External identities never become fictitious
// native bits; all persistence and gameplay lookups retain their stable keys.
inline unsigned NativeIndex(unsigned bit) noexcept {
    for(unsigned i=0;i<NativeCount;++i)if(NativeBits[i]==bit)return i;
    return Count;
}
}
