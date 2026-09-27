#pragma once
#include <cstdint>
namespace FfxHooks::EquipmentWorkshop::NativeUi {
bool Start(std::uintptr_t base,bool enabled,bool validateOnly,void(*log)(const char*));
void Stop() noexcept;
bool Active() noexcept;
#ifdef FFXHOOKS_TESTING
void FrameEnvironmentForTests(void* frame);
#endif
}
