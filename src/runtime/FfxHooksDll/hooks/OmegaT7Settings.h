#pragma once
#include "../shared/Config.h"
namespace FfxHooks::OmegaT7::Settings {
inline bool Requested(){return Config::GetBool("spira.omega_t7",false);}
}
