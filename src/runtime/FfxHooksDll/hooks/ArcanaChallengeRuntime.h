#pragma once
#include "OmegaLastSealRuntime.h"
namespace FfxHooks::Arcana::Challenge {
bool Start(std::uintptr_t,OmegaLastSeal::Runtime::Reader);
void Stop() noexcept;
bool Active() noexcept;
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t,OmegaLastSeal::Runtime::Reader);
#endif
}
