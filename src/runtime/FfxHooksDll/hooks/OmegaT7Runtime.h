#pragma once
#define FFXHOOKS_OMEGA_AI_READY_V1 1
#include "SinAiHook.h"
#include <filesystem>
#include <array>
namespace FfxHooks::OmegaT7::Runtime {
using Log=void(*)(const char*);
bool Requested() noexcept;
void Prime(std::uintptr_t,const std::filesystem::path&,bool validateOnly,Log) noexcept;
bool Start(SinAi::ContextReader);
void RequestStop() noexcept;
void Stop() noexcept;
bool Active() noexcept;
bool Prepared() noexcept;
const char* Detail() noexcept;
struct PreviewEntry {unsigned model=0,profile=48;std::array<unsigned char,40> name{};};
struct PreviewSnapshot {unsigned field=0,formation=0,count=0;std::array<PreviewEntry,8> monsters{};};
bool Preview(PreviewSnapshot&) noexcept;
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t,const char* manifest,SinAi::ContextReader,std::uint64_t seed,Log);
bool EnableAiProducerForTests();
#endif
}
