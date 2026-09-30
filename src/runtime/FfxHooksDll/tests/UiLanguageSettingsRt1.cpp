// Jarvis-HOOK: real Config publication with a bounded persistence provider.
#include "../hooks/UiLanguageSettings.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
struct Store {bool accept=true;unsigned calls=0;std::string text;};
static bool Persist(void* raw,const char*,const char* text){
    auto& store=*static_cast<Store*>(raw);++store.calls;
    if(!store.accept)return false;store.text=text;return true;
}
int main(){
    using namespace FfxHooks;
    Store store;
    Config::SetProvidersForTests({&store,nullptr,nullptr,Persist});
    assert(Config::LoadTextForTests("[language]\nvoice=2\nsfx=1\nvideo=0\ntext_locale=pt-BR\nui_locale=de\n[custom]\nkeep=123\n","ui-test.ini"));
    assert(UiLanguage::Settings::Current()==UiLanguage::Locale::German);
    for(unsigned i=0;i<UiLanguage::LocaleCount;++i){
        assert(UiLanguage::Settings::Save(i));
        assert(UiLanguage::Index(UiLanguage::Settings::Current())==i);
        assert(Config::GetInt("language.voice",-1)==2);
        assert(Config::GetInt("language.sfx",-1)==1);
        assert(Config::GetInt("language.video",-1)==0);
        assert(std::strcmp(Config::GetString("language.text_locale",""),"pt-BR")==0);
        assert(Config::GetInt("custom.keep",-1)==123);
    }
    unsigned calls=store.calls;assert(!UiLanguage::Settings::Save(9));assert(store.calls==calls);
    store.accept=false;assert(!UiLanguage::Settings::Save(1));
    assert(UiLanguage::Settings::Current()==UiLanguage::Locale::Chinese);
    assert(Config::LoadTextForTests("[language]\nui_locale=unknown\n","ui-test.ini"));
    assert(UiLanguage::Settings::Current()==UiLanguage::Locale::English);
    assert(Config::LoadTextForTests("[custom]\nkeep=123\n","ui-test.ini"));
    assert(UiLanguage::Settings::Current()==UiLanguage::Locale::English);
    Config::ResetForTests();std::puts("UI settings: nine locales, independent keys, failed writes and defaults passed");
}
