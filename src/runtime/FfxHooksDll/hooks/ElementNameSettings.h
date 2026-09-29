#pragma once
#include "ElementRegistry.h"
#include "../shared/Config.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace FfxHooks::ElementNames {
inline constexpr unsigned MaximumLength=32;
enum class Result {Saved,Empty,TooLong,InvalidCharacter,Duplicate,Unavailable,WriteFailed};
inline bool Editable(unsigned bit) noexcept {return !bit||bit==0x10||bit==0x80||bit==0x20||bit==0x40;}
inline bool Character(unsigned char ch) noexcept {
    return (ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||
           ch==' '||ch=='-'||ch=='_'||ch=='\''||ch=='.'||ch=='('||ch==')'||ch=='/';
}
inline Result Normalize(std::string_view text,char (&out)[65]) noexcept {
    out[0]=0;
    while(!text.empty()&&text.front()==' ')text.remove_prefix(1);
    while(!text.empty()&&text.back()==' ')text.remove_suffix(1);
    if(text.empty())return Result::Empty;
    if(text.size()>MaximumLength)return Result::TooLong;
    for(unsigned char ch:text)if(!Character(ch))return Result::InvalidCharacter;
    std::memcpy(out,text.data(),text.size());out[text.size()]=0;return Result::Saved;
}
inline bool Same(std::string_view a,std::string_view b) noexcept {
    if(a.size()!=b.size())return false;
    for(unsigned i=0;i<a.size();++i){
        const char x=a[i]>='A'&&a[i]<='Z'?static_cast<char>(a[i]-'A'+'a'):a[i];
        const char y=b[i]>='A'&&b[i]<='Z'?static_cast<char>(b[i]-'A'+'a'):b[i];
        if(x!=y)return false;
    }return true;
}
inline bool SettingKey(unsigned nativeBit,const char* identity,char (&out)[128]) noexcept {
    if(!Editable(nativeBit))return false;
    if(nativeBit)std::snprintf(out,sizeof(out),"element_names.native_%02x",nativeBit);
    else {
        if(!identity||!ElementalDominion::ValidKey(identity))return false;
        std::snprintf(out,sizeof(out),"element_names.%s",identity);
    }return true;
}
inline bool Read(unsigned nativeBit,const char* identity,char (&out)[65]){
    char key[128]{};if(!SettingKey(nativeBit,identity,key))return false;
    const auto value=Config::GetString(key,"");
    return Normalize(value,out)==Result::Saved;
}
inline bool Store(unsigned nativeBit,const char* identity,const char* value){
    char key[128]{};if(!value||!SettingKey(nativeBit,identity,key))return false;
    // Empty is the explicit reset marker: inherit the current canonical label.
    return Config::SetString(key,value)&&std::strcmp(Config::GetString(key,""),value)==0;
}
inline const char* PromotionFallback(unsigned bit,const char* identity,std::string_view canonical) noexcept {
    // These four previously neutral defaults were given roles in this release.
    // A valid saved alias must not lose priority merely because a default changed.
    if(bit==0x20&&Same(canonical,"Earth"))return "Custom 01";
    if(bit==0x40&&Same(canonical,"Wind"))return "Custom 02";
    if(!bit&&identity){
        if(!std::strcmp(identity,"hook.custom03")&&Same(canonical,"Poison"))return "Custom 03";
        if(!std::strcmp(identity,"hook.custom04")&&Same(canonical,"Gravity"))return "Custom 04";
    }
    return nullptr;
}
template<class Item,std::size_t N> inline void Apply(std::array<Item,N>& catalog){
    std::array<std::array<char,65>,N> original{};std::array<bool,N> alias{};
    for(unsigned i=0;i<N;++i){
        std::snprintf(original[i].data(),original[i].size(),"%s",catalog[i].label);
        char name[65]{};
        if(Read(catalog[i].nativeBit,catalog[i].key,name)){
            std::snprintf(catalog[i].label,sizeof(catalog[i].label),"%s",name);alias[i]=true;
        }
    }
    // Resolve the complete set before comparing: valid saved swaps must survive
    // reload. Bad manually edited aliases cannot poison the registry/provider.
    for(unsigned pass=0;pass<N;++pass){
        std::array<bool,N> rejected{};bool changed=false;
        for(unsigned i=0;i<N;++i)if(!alias[i]){
            const char* inherited=original[i].data();
            if(const auto* previous=PromotionFallback(catalog[i].nativeBit,catalog[i].key,inherited)){
                for(unsigned j=0;j<N;++j)if(i!=j&&alias[j]&&Same(inherited,catalog[j].label)){
                    inherited=previous;break;
                }
            }
            if(std::strcmp(catalog[i].label,inherited)){
                std::snprintf(catalog[i].label,sizeof(catalog[i].label),"%.64s",inherited);changed=true;
            }
        }
        for(unsigned i=0;i<N;++i)for(unsigned j=i+1;j<N;++j){
            if(!Same(catalog[i].label,catalog[j].label))continue;
            rejected[i]=rejected[i]||alias[i];rejected[j]=rejected[j]||alias[j];
        }
        for(unsigned i=0;i<N;++i)if(rejected[i]){
            std::snprintf(catalog[i].label,sizeof(catalog[i].label),"%s",original[i].data());
            alias[i]=false;changed=true;
        }
        if(!changed)break;
    }
}
inline const char* Detail(Result result) noexcept {
    switch(result){
    case Result::Saved:return "Element name saved.";
    case Result::Empty:return "Enter a name or choose Restore default.";
    case Result::TooLong:return "Use at most 32 characters.";
    case Result::InvalidCharacter:return "Use letters, numbers, spaces or basic punctuation.";
    case Result::Duplicate:return "Another element already uses that name.";
    case Result::Unavailable:return "Element identity changed. Reopen its name editor.";
    case Result::WriteFailed:return "Unable to save. Previous name preserved.";
    }return "Name unavailable.";
}
}
