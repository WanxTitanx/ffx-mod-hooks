#include "../hooks/ElementMenuCatalog.h"
#include <cstdio>
#include <map>
static std::map<std::string,std::string> settings;
namespace FfxHooks::Config {
const char* GetString(const char* key,const char* fallback){const auto at=settings.find(key);return at==settings.end()?fallback:at->second.c_str();}
bool SetString(const char* key,const char* value){settings[key]=value;return true;}
}
namespace E=FfxHooks::ElementMenu;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
    const char* names[]={"Earth","Wind","Poison","Gravity"};
    const char* previous[]={"Custom 01","Custom 02","Custom 03","Custom 04"};
    for(unsigned n=0;n<4;++n){
        settings.clear();settings["element_names.native_10"]=names[n];
        const auto current=E::Read();const unsigned index=6+n;
        Check(!std::strcmp(current[4].label,names[n])&&!std::strcmp(current[index].label,previous[n]),
              "a saved alias wins over a newly promoted default without changing either identity");
        Check(settings["element_names.native_10"]==names[n]&&!std::strcmp(E::Raw()[index].label,names[n]),
              "collision resolution only changes presentation, not preferences or canonical descriptors");
        Check(E::SaveName(current[index].nativeBit,current[index].key,"",true)==FfxHooks::ElementNames::Result::Saved&&
              !std::strcmp(E::Read()[index].label,previous[n]),"Restore default uses the collision-safe inherited presentation");
    }
    settings={{"element_names.native_10","Poison"},{"element_names.native_80","Gravity"}};
    auto current=E::Read();
    Check(!std::strcmp(current[4].label,"Poison")&&!std::strcmp(current[5].label,"Gravity")&&
          !std::strcmp(current[8].label,"Custom 03")&&!std::strcmp(current[9].label,"Custom 04"),
          "independent saved aliases reserve both new external defaults");
    settings={{"element_names.native_10","Poison"},{"element_names.native_80","Poison"}};current=E::Read();
    Check(!std::strcmp(current[4].label,"Holy")&&!std::strcmp(current[5].label,"Darkness")&&!std::strcmp(current[8].label,"Poison"),
          "invalid duplicate manual aliases still fall back and release the canonical default");
    settings={{"element_names.native_20","Wind"},{"element_names.native_40","Earth"}};current=E::Read();
    Check(!std::strcmp(current[6].label,"Wind")&&!std::strcmp(current[7].label,"Earth"),"valid saved swaps still resolve as a complete set");
    settings.clear();current=E::Read();
    Check(!std::strcmp(current[6].label,"Earth")&&!std::strcmp(current[8].label,"Poison"),"without aliases the user-approved new defaults remain visible");
    std::printf("ElementNameDefaultsRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
