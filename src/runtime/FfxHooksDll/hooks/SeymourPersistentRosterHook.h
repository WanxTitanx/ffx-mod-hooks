#pragma once
#include <cstddef>
#include <cstdint>
namespace FfxHooks::SeymourPersistentRoster {
void Start(std::uintptr_t,bool,void(*)(const char*));
void PumpTick();
void PresentTick();
void RequestStop() noexcept;
bool Remove();
bool BattleReady() noexcept;
std::uint64_t BattleRevision() noexcept;
void MenuLabel(char*,std::size_t);
bool MenuAction();
void Detail(char*,std::size_t);
}
