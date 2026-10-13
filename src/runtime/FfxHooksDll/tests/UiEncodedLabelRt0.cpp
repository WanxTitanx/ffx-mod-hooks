#include "../hooks/UiLanguage.h"
#include <array>
#include <cstdio>
#include <cstring>
#if __has_include("../hooks/UiEncodedLabel.h")
#include "../hooks/UiEncodedLabel.h"
#endif
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
static bool Translate(const char* text,char* output,bool raw=false){
#ifdef FFXHOOKS_UI_ENCODED_LABEL_V1
    std::array<unsigned char,256> bytes{};
    for(unsigned n=0;text[n];++n){const auto* found=std::strchr(FfxHooks::UiLanguage::EncodedAtlas,text[n]);if(!found)return false;bytes[n]=static_cast<unsigned char>(0x30+found-FfxHooks::UiLanguage::EncodedAtlas);}
#ifdef FFXHOOKS_UI_RAW_LABEL_V1
    return FfxHooks::UiLanguage::TranslateOwnedLabel(bytes.data(),output,1024,raw);
#else
    (void)raw;return FfxHooks::UiLanguage::TranslateOwnedLabel(bytes.data(),output,1024);
#endif
#else
    (void)text;(void)raw;output[0]=0;return false;
#endif
}
int main(){
    using namespace FfxHooks::UiLanguage;char output[1024]{};
    DisplayScope language(Locale::Portuguese);
    Check(Translate("Back",output)&&!std::strcmp(output,u8"Voltar"),"F7 native encoded navigation uses the selected UI locale");
    Check(Translate("Seed: 2461293065",output)&&!std::strcmp(output,u8"Semente: 2461293065"),"dynamic seed retains its exact value");
    Check(Translate("Curses: ON",output)&&!std::strcmp(output,u8"Maldições: LIGADO"),"dynamic labels and state values both translate");
    Check(Translate("Varuna - T7 Abyss",output)&&!std::strcmp(output,u8"Varuna - T7 Abismo"),"curse preview preserves monster identity and tier");
    Check(!Translate("User-defined monster",output),"unknown/custom labels retain native rendering");
    for(const auto* name:{"Back","Search","Music","Seed: Back"})
        Check(!Translate(name,output,true),"raw user identity never enters exact-key or label-value translation");
    Check(Translate("Back",output)&&!std::strcmp(output,u8"Voltar"),"navigation still translates beside raw preset names");
#ifdef FFXHOOKS_UI_ENCODED_LABEL_V1
    const unsigned char control[]={1,0x50,0};
    Check(!TranslateOwnedLabel(control,output,sizeof(output)),"opaque native control sequences are never reinterpreted as plain labels");
    {DisplayScope english(Locale::English);Check(!Translate("Back",output),"English leaves the native path unchanged");}
#endif
    std::printf("UI_ENCODED_LABEL_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
