#pragma once
#include <cstdint>
namespace FfxHooks::Arcana::Combat {
bool Start(std::uintptr_t,bool requested,bool validateOnly,void(*log)(const char*));
void Stop() noexcept;
bool Active() noexcept;
unsigned PartyDropMultiplier() noexcept;
}
