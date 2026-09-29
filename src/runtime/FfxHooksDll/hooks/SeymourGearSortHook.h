#pragma once
#include <cstddef>
#include <cstdint>
namespace FfxHooks::SeymourGearSort {
void Start(std::uintptr_t,bool,void(*)(const char*));
void PresentTick();
void RequestStop() noexcept;
bool Remove();
void MenuLabel(char*,std::size_t);
bool MenuAction();
void Detail(char*,std::size_t);
}
