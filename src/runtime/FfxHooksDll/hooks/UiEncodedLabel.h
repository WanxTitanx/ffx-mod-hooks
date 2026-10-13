#pragma once
// Jarvis-HOOK: only plain DLL-owned labels, never opaque game text/control streams.
#include "UiLanguage.h"
#include <cstdio>
#define FFXHOOKS_UI_ENCODED_LABEL_V1 1
#define FFXHOOKS_UI_RAW_LABEL_V1 1
namespace FfxHooks::UiLanguage {
inline constexpr char EncodedAtlas[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
inline bool DecodeOwnedLabel(const unsigned char* bytes,char* out,std::size_t capacity) noexcept {
    if(!bytes||!out||capacity<2)return false;
    for(std::size_t i=0;i<capacity;++i){
        const unsigned value=bytes[i];
        if(!value){out[i]=0;return i!=0;}
        if(i+1==capacity||value<0x30||value-0x30>=sizeof(EncodedAtlas)-1)return false;
        out[i]=EncodedAtlas[value-0x30];
    }
    return false;
}
inline bool TranslateOwnedLabel(const unsigned char* bytes,char* out,std::size_t capacity,bool raw=false) noexcept {
    if(raw||DisplayLocale==Locale::English||!out||!capacity)return false;
    char source[512]{};if(!DecodeOwnedLabel(bytes,source,sizeof(source)))return false;
    const auto* exact=Text(source);
    if(std::strcmp(exact,source)){CopyUtf8(out,capacity,exact);return true;}
    // Numeric values and native monster identities remain byte-for-byte data;
    // only the known UI label or authored curse name is localized.
    if(auto* separator=std::strstr(source,": ")){
        *separator=0;const auto* value=separator+2;const auto* label=Text(source);const auto* translated=Text(value);
        if(std::strcmp(label,source)||std::strcmp(value,translated)){
            char combined[2048]{};std::snprintf(combined,sizeof(combined),"%s: %s",label,translated);
            CopyUtf8(out,capacity,combined);return true;
        }
        *separator=':';
    }
    if(const auto* marker=std::strstr(source," - T")){
        auto* curse=marker+4;while(*curse>='0'&&*curse<='9')++curse;
        if(curse>marker+4&&*curse==' '){++curse;const auto* translated=Text(curse);
            if(std::strcmp(curse,translated)){
                char combined[2048]{};std::snprintf(combined,sizeof(combined),"%.*s%s",static_cast<int>(curse-source),source,translated);
                CopyUtf8(out,capacity,combined);return true;
            }
        }
    }
    return false;
}
inline unsigned DisplayUnits(const char* text) noexcept {
    unsigned count=0;const auto* p=reinterpret_cast<const unsigned char*>(text);
    while(p&&*p){const auto size=ScalarBytes(p);if(!size)return count;count+=size>=3?2u:1u;p+=size;}return count;
}
}
