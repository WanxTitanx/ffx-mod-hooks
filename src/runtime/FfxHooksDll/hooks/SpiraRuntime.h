#pragma once
#include "SpiraAbilityCatalog.h"
#include <cstdint>

namespace FfxHooks::SpiraAbilities {
struct RuntimeOptions {bool spira=false,ascension=false;};
using LogFn=void(*)(const char*);
bool Prepare(std::uintptr_t image,RuntimeOptions,bool validateOnly,LogFn);
bool Prepare(std::uintptr_t image,bool validateOnly,LogFn);
bool Activate() noexcept;
void TickMainThread() noexcept;
// Loader-lock fallback: close admission only; normal teardown owns unsubscribe.
void RequestDetachStop() noexcept;
void RequestStop() noexcept;
bool Ready() noexcept;
bool HasEffect(unsigned owner,unsigned effect) noexcept;
unsigned PartyDropMultiplier() noexcept;
const char* RuntimeDetail() noexcept;
} // namespace FfxHooks::SpiraAbilities
