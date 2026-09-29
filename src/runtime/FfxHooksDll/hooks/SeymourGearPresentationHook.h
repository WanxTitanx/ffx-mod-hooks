#pragma once
#include <cstdint>
#include <cstddef>
namespace FfxHooks::SeymourGearPresentation {
enum class State:unsigned {NotStarted,Off,ValidateOnly,Unavailable,Installed,StopPending,Stopped};
void Start(std::uintptr_t,bool,void (*)(const char*));
void PresentTick();
void RequestStop() noexcept;
bool Remove();
void MenuLabel(char*,std::size_t);
bool MenuAction();
void Detail(char*,std::size_t);
}
