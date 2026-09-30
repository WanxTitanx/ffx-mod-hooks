#pragma once
#include "UiLanguage.h"
#include "../shared/Config.h"
namespace FfxHooks::UiLanguage::Settings {
inline constexpr char Key[]="language.ui_locale";
inline Locale Current(){return Parse(Config::GetString(Key,"en"));}
inline bool Save(unsigned selection){
    return selection<LocaleCount&&Config::SetString(Key,Code(static_cast<Locale>(selection)));
}
}
