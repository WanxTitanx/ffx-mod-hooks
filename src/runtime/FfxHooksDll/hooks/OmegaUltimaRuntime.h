#pragma once
#include "OmegaUltimaConfig.h"
#include "OmegaUltimaBridge.h"
#include <filesystem>
namespace FfxHooks::OmegaUltima::Runtime {
using Log=void(*)(const char*);
bool Requested() noexcept;
bool Start(std::uintptr_t base,const std::filesystem::path& game,bool validateOnly,Log log);
void Stop() noexcept;
bool Active() noexcept;
const char* Detail() noexcept;
#ifdef FFXHOOKS_TESTING
using AssetReader=bool(*)(std::uintptr_t,unsigned char*,unsigned) noexcept;
void SetProvidersForTests(AssetReader reader,Random random) noexcept;
#endif
}
