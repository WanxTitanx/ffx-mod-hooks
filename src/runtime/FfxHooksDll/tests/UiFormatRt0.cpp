// Jarvis-HOOK: presentation arguments must not become configuration identities.
#include "../hooks/UiLanguageFormat.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    using namespace FfxHooks::UiLanguage;
    char label[256]{};
    {
        DisplayScope portuguese(Locale::Portuguese);
        assert(Format(label,sizeof(label),"%s: %s","Interface language","English"));
        assert(std::strcmp(label,u8"Idioma da interface: Inglês")==0);
        assert(Format(label,sizeof(label),"%s: %s",Raw{"Back"},"ON"));
        assert(std::strcmp(label,"Back: LIGADO")==0);
        assert(Format(label,sizeof(label),"%s: %04X",Raw{"vanguard.test"},0x30AFu));
        assert(std::strcmp(label,"vanguard.test: 30AF")==0);
    }
    assert(Format(label,sizeof(label),"%s: %s","Interface language","English"));
    assert(std::strcmp(label,"Interface language: English")==0);
    char small[8]{};
    assert(!Format(small,sizeof(small),"%s",Raw{u8"日本語"}));
    assert(ValidUtf8(small));
    assert(std::strcmp(small,u8"日本")==0);
    assert(!Format(nullptr,0,"%s","Back"));
    std::puts("UI format, raw data, locale scope and bounded UTF-8 regressions passed");
}
