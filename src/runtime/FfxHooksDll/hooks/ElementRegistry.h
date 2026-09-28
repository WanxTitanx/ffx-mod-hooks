#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace FfxHooks::ElementalDominion {
inline constexpr unsigned ElementLimit=32,InvalidElement=32;
enum class Error : std::uint8_t {Ok,Capacity,InvalidKey,InvalidDescriptor,DuplicateKey,NativeBitCollision,MissingReference,InvalidInput,InvalidTier,InvalidWeight,UnsupportedPolicy};
inline bool ValidKey(std::string_view key,unsigned limit=64) noexcept {
    if(key.empty()||key.size()>limit||key.front()<'a'||key.front()>'z')return false;
    bool namespaced=false,segmentStart=false;
    for(char ch:key){
        if(ch=='.'){if(segmentStart)return false;namespaced=true;segmentStart=true;continue;}
        const bool alphanumeric=(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9');
        if(!alphanumeric&&(segmentStart||(ch!='_'&&ch!='-')))return false;
        segmentStart=false;
    }
    return namespaced&&!segmentStart;
}
inline bool ValidLabel(std::string_view value) noexcept {
    if(value.empty()||value.size()>64)return false;
    for(unsigned char ch:value)if(ch<32||ch>126)return false;
    return true;
}
struct Element {std::string key,labelKey,label;std::uint32_t rgb=0xFFFFFF;unsigned nativeBit=0;};
class Registry {
public:
    Error Add(const Element& element){
        if(count_==ElementLimit)return Error::Capacity;
        if(!ValidKey(element.key))return Error::InvalidKey;
        if(!ValidKey(element.labelKey,128)||!ValidLabel(element.label)||element.rgb>0xFFFFFF)return Error::InvalidDescriptor;
        const unsigned bit=element.nativeBit;
        if(bit>0x80||(bit&&(bit&(bit-1))))return Error::InvalidDescriptor;
        for(unsigned i=0;i<count_;++i){
            if(entries_[i].key==element.key)return Error::DuplicateKey;
            if(bit&&entries_[i].nativeBit==bit)return Error::NativeBitCollision;
        }
        // Pack construction allocates; publication happens only after every copy succeeds.
        entries_[count_]=element;++count_;return Error::Ok;
    }
    unsigned Size() const noexcept {return count_;}
    const Element* At(unsigned index) const noexcept {return index<count_?&entries_[index]:nullptr;}
    unsigned Index(std::string_view key) const noexcept {
        for(unsigned i=0;i<count_;++i)if(entries_[i].key==key)return i;
        return InvalidElement;
    }
    const Element* Find(std::string_view key) const noexcept {return At(Index(key));}
    const Element* Native(unsigned bit) const noexcept {
        if(!bit||bit>0x80||(bit&(bit-1)))return nullptr;
        for(unsigned i=0;i<count_;++i)if(entries_[i].nativeBit==bit)return &entries_[i];
        return nullptr;
    }
private:
    // Dense indices are scoped to this pack. Persisted bindings always retain keys.
    std::array<Element,ElementLimit> entries_{};
    unsigned count_=0;
};
}
