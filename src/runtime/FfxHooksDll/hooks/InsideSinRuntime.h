#pragma once
#include "InsideSinConfig.h"
#include "SinAiHook.h"
namespace FfxHooks::InsideSin::Runtime {
using Log=void(*)(const char*);
bool Requested() noexcept;
void Prime(bool validateOnly) noexcept;
bool Start(std::uintptr_t image,const wchar_t* manifest,SinAi::ContextReader,Log=nullptr);
void Stop() noexcept;
void RequestStop() noexcept;
const char* Detail() noexcept;
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t,const Bindings&,SinAi::ContextReader,Log=nullptr);
void ResetForTests() noexcept;
#endif
}
