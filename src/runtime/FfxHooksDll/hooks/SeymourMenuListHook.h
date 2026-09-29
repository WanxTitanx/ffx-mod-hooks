#pragma once
#include <cstddef>
#include <cstdint>

namespace FfxHooks::SeymourMenuList {
void Start(std::uintptr_t base,bool validateOnly,void(*log)(const char*));
void PresentTick();
void PumpTick();
void RequestStop() noexcept;
bool Remove();
void MenuLabel(char*,std::size_t);
bool MenuAction();
void Detail(char*,std::size_t);
} // namespace FfxHooks::SeymourMenuList
