#pragma once
#include <cstdint>

namespace FfxHooks::WeaponStrikeVfx {
bool Start(std::uintptr_t module, bool enabled, bool validateOnly, void(*log)(const char*));
void TickMainThread() noexcept;
void RequestStop() noexcept;
bool Installed() noexcept;
const char* Detail() noexcept;
}
