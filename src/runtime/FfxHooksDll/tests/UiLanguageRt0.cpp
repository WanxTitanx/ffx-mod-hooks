// Jarvis-HOOK: portable locale and Unicode boundary regressions.
#include "../hooks/UiLanguage.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    using namespace FfxHooks::UiLanguage;
    assert(LocaleCount == 9);
    assert(Parse(nullptr) == Locale::English);
    assert(Parse("") == Locale::English);
    assert(Parse("pt-BR") == Locale::Portuguese);
    assert(Parse("PT_br") == Locale::Portuguese);
    assert(Parse("zh-Hans") == Locale::Chinese);
    assert(Parse("zh-TW") == Locale::English);
    assert(Parse("ja-JP") == Locale::Japanese);
    assert(Parse("../../pt") == Locale::English);
    assert(Parse("pt-BR extra") == Locale::English);
    for (unsigned i=0;i<LocaleCount;++i) {
        const auto locale=static_cast<Locale>(i);
        assert(Parse(Code(locale)) == locale);
        assert(Name(locale)[0]);
        assert(Text("Back",locale)[0]);
        assert(std::strcmp(Text("Unregistered technical ID",locale),"Unregistered technical ID")==0);
    }
    assert(std::strcmp(Text("Back",Locale::Portuguese),u8"Voltar")==0);
    assert(std::strcmp(Text("Back",Locale::Japanese),u8"戻る")==0);
    assert(std::strcmp(Text("Back",Locale::English),"Back")==0);
    char out[8]{};
    assert(CopyUtf8(out,sizeof(out),u8"日本語")==6);
    assert(std::strcmp(out,u8"日本")==0);
    assert(CopyUtf8(out,1,u8"é")==0 && out[0]==0);
    assert(CopyUtf8(nullptr,0,"text")==0);
    assert(!ValidUtf8("\xC0\xAF"));
    assert(!ValidUtf8("\xED\xA0\x80"));
    assert(!ValidUtf8("\xF4\x90\x80\x80"));
    assert(!ValidUtf8("\xE3\x81"));
    assert(ValidUtf8(u8"Português 日本語 한국어 简体中文"));
    assert(CopyUtf8(out,sizeof(out),"\xC0\xAF")==0 && out[0]==0);
    for (std::size_t row=0;row<EntryCount();++row) {
        const char* key=EntryKey(row);
        for(unsigned i=0;i<LocaleCount;++i)
            assert(ValidUtf8(Text(key,static_cast<Locale>(i))));
    }
    std::printf("UI locale/UTF-8 regressions passed; %zu catalog rows x %u locales\n",EntryCount(),LocaleCount);
}
