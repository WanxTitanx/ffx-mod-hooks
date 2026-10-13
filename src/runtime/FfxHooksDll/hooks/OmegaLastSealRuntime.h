#pragma once
#include <cstdint>
#include <filesystem>
namespace FfxHooks::OmegaLastSeal {struct State;}
namespace FfxHooks::OmegaLastSeal::Runtime {
using Log=void(*)(const char*);
struct Context {std::uint64_t battle=0;bool original=false;};
using Reader=bool(*)(Context&) noexcept;
bool Requested() noexcept;
void Prime(std::uintptr_t,const std::filesystem::path&,bool validateOnly,Log) noexcept;
bool Start(Reader);
bool UsesAuthoredStats(std::uintptr_t) noexcept;
void RequestStop() noexcept;
void Stop() noexcept;
bool Active() noexcept;
bool Prepared() noexcept;
const char* Detail() noexcept;
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t,const char*,Reader,Log);
State SnapshotForTests() noexcept;
#endif
}
