#include "../hooks/TextLanguageSettings.h"
#include <iostream>
#include <string>
namespace C=FfxHooks::Config;
namespace S=FfxHooks::TextLanguage::Settings;
static unsigned checks=0,failures=0,writes=0;
static bool permitted=true;
static std::string saved;
static bool Persist(void*,const char*,const char* bytes){++writes;if(!permitted)return false;saved=bytes;return true;}
static void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
int main(){
 C::ResetForTests();C::LoadTextForTests("[language]\nvoice=2\nsfx=1\nvideo=0\n","C:\\mod006-settings.ini");
 C::SetProvidersForTests({nullptr,nullptr,nullptr,Persist});
 Check(S::Selection()==0&&writes==0,"missing selection remains original without writing");
 Check(S::Save(1)&&S::Selection()==1&&writes==1,"PT-BR is a persistent opt-in virtual locale");
 Check(C::GetInt("language.voice",-1)==2&&C::GetInt("language.sfx",-1)==1&&C::GetInt("language.video",-1)==0,"text selection does not modify any audio choice");
 const auto first=saved;C::LoadTextForTests(first.c_str(),"C:\\mod006-settings.ini");
 Check(S::Selection()==1,"PT-BR survives a configuration reload");
 permitted=false;Check(!S::Save(0)&&S::Selection()==1,"failed persistence preserves the prior text choice");
 permitted=true;Check(S::Save(0)&&S::Selection()==0,"original text remains a separate selectable option");
 const auto count=writes;Check(!S::Save(-1)&&!S::Save(2)&&writes==count,"invalid cursor value never writes configuration");
 C::LoadTextForTests("[language]\ntext_locale=../escape\n","C:\\mod006-settings.ini");
 Check(S::Selection()==-1,"unknown config is diagnosed rather than mistaken for PT-BR");
 C::ResetForTests();std::cout<<"TextLanguageSettings RT0: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
