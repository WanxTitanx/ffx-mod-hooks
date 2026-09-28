#pragma once
#include "../shared/Config.h"
#include <cstring>
namespace FfxHooks::TextLanguage::Settings {
inline constexpr char Key[]="language.text_locale";
inline constexpr char BrazilianName[]="Portuguese (Brazil)";
inline int Selection(){
 const char* value=Config::GetString(Key,"native");
 if(std::strcmp(value,"native")==0)return 0;
 if(std::strcmp(value,"pt-BR")==0)return 1;
 return -1;
}
inline bool Save(int choice){
 return (choice==0||choice==1)&&Config::SetString(Key,choice==0?"native":"pt-BR");
}
}
