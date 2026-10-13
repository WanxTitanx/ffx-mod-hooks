#pragma once
#include "../shared/Config.h"
namespace FfxHooks::InsideSin::Settings {
// Restart-only; no missing manifest or enabled SIN setting implicitly opts in.
inline bool Requested() noexcept {return Config::GetBool("sin.inside_sin",false);}
}
