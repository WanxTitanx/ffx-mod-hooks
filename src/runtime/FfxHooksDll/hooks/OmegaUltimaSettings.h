#pragma once
#include "../shared/Config.h"
namespace FfxHooks::OmegaUltima::Settings {
inline bool Requested(){return Config::GetBool("spira.omega_ultima",false);}
}
