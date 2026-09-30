#pragma once
// Jarvis-HOOK: DLL-owned presentation only. Canonical keys and game text are untouched.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::UiLanguage {
enum class Locale : unsigned { English, Portuguese, Spanish, French, Italian, German, Japanese, Korean, Chinese };
inline constexpr unsigned LocaleCount = 9;
inline constexpr const char* Codes[] = {"en","pt","es","fr","it","de","ja","ko","zh"};
inline constexpr const char* Names[] = {"English",u8"Português (Brasil)",u8"Español",u8"Français","Italiano","Deutsch",u8"日本語",u8"한국어",u8"简体中文"};
inline constexpr const char* EnglishNames[] = {"English","Portuguese (Brazil)","Spanish","French","Italian","German","Japanese","Korean","Chinese (Simplified)"};
inline unsigned Index(Locale value) noexcept { return static_cast<unsigned>(value)<LocaleCount?static_cast<unsigned>(value):0; }
inline const char* Code(Locale value) noexcept { return Codes[Index(value)]; }
inline const char* Name(Locale value) noexcept { return Names[Index(value)]; }
inline Locale Parse(const char* value) noexcept {
    if(!value)return Locale::English;
    char normalized[24]{};std::size_t length=0;
    while(value[length]) {
        if(length+1>=sizeof(normalized))return Locale::English;
        const unsigned char c=static_cast<unsigned char>(value[length]);
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_'))return Locale::English;
        normalized[length]=c=='_'?'-':c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);++length;
    }
    for(unsigned i=0;i<LocaleCount;++i)if(std::strcmp(normalized,Codes[i])==0)return static_cast<Locale>(i);
    struct Alias {const char* code;Locale value;};
    constexpr Alias aliases[]={{"en-us",Locale::English},{"en-gb",Locale::English},{"pt-br",Locale::Portuguese},
        {"es-es",Locale::Spanish},{"es-mx",Locale::Spanish},{"fr-fr",Locale::French},{"it-it",Locale::Italian},
        {"de-de",Locale::German},{"ja-jp",Locale::Japanese},{"ko-kr",Locale::Korean},{"zh-cn",Locale::Chinese},
        {"zh-hans",Locale::Chinese},{"zh-hans-cn",Locale::Chinese}};
    for(const auto& alias:aliases)if(std::strcmp(normalized,alias.code)==0)return alias.value;
    return Locale::English;
}

// Strict scalar decoding rejects overlong forms, surrogate halves and out-of-range values.
inline std::size_t ScalarBytes(const unsigned char* p) noexcept {
    if(!p||!p[0])return 0;
    if(p[0]<0x80)return 1;
    unsigned n=0;std::uint32_t cp=0,minimum=0;
    if(p[0]>=0xC2&&p[0]<=0xDF){n=2;cp=p[0]&31;minimum=0x80;}
    else if(p[0]>=0xE0&&p[0]<=0xEF){n=3;cp=p[0]&15;minimum=0x800;}
    else if(p[0]>=0xF0&&p[0]<=0xF4){n=4;cp=p[0]&7;minimum=0x10000;}
    else return 0;
    for(unsigned i=1;i<n;++i){if(!p[i]||(p[i]&0xC0)!=0x80)return 0;cp=(cp<<6)|(p[i]&63);}
    return cp<minimum||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF)?0:n;
}
inline bool ValidUtf8(const char* text) noexcept {
    if(!text)return false;
    const auto* p=reinterpret_cast<const unsigned char*>(text);
    while(*p){const auto n=ScalarBytes(p);if(!n)return false;p+=n;}return true;
}
inline std::size_t CopyUtf8(char* out,std::size_t capacity,const char* text) noexcept {
    if(!out||!capacity)return 0;
    out[0]=0;
    if(!ValidUtf8(text))return 0;
    std::size_t used=0;
    while(text[used]){const auto n=ScalarBytes(reinterpret_cast<const unsigned char*>(text+used));
        if(n>=capacity-used)break;
        std::memcpy(out+used,text+used,n);used+=n;}
    out[used]=0;return used;
}
struct Entry {const char* values[LocaleCount];};
#include "UiLanguageCatalog.inl"
inline std::size_t EntryCount() noexcept {return sizeof(Catalog)/sizeof(Catalog[0]);}
inline const char* EntryKey(std::size_t row) noexcept {return row<EntryCount()?Catalog[row].values[0]:"";}
inline const char* Text(const char* source,Locale locale) noexcept {
    if(!source)return "";
    if(Index(locale)==0)return source;
    std::size_t first=0,last=EntryCount();
    while(first<last){const auto middle=first+(last-first)/2;const int order=std::strcmp(source,Catalog[middle].values[0]);
        if(order==0){const auto* translated=Catalog[middle].values[Index(locale)];return translated&&translated[0]?translated:source;}
        if(order<0)last=middle;else first=middle+1;}
    return source;
}
// Each menu draw selects one locale for its whole snapshot. No mutable global locale.
inline thread_local Locale DisplayLocale=Locale::English;
inline const char* Text(const char* source) noexcept {return Text(source,DisplayLocale);}
class DisplayScope {
    Locale previous_;
public:
    explicit DisplayScope(Locale locale) noexcept:previous_(DisplayLocale){DisplayLocale=locale;}
    ~DisplayScope() noexcept {DisplayLocale=previous_;}
    DisplayScope(const DisplayScope&)=delete;DisplayScope& operator=(const DisplayScope&)=delete;
};
}
