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
// Restart-only opt-in for the 32-bit kernel table adapter; OFF by default.
inline constexpr char ExtendedKernelTablesKey[]="language.text_extended_kernel_tables";
inline bool ExtendedKernelTables(){return Config::GetBool(ExtendedKernelTablesKey,false);}
inline bool Save(int choice){
 return (choice==0||choice==1)&&Config::SetString(Key,choice==0?"native":"pt-BR");
}
}
