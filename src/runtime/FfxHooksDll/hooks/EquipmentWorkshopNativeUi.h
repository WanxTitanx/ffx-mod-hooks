#pragma once
#include <cstdint>
namespace FfxHooks::EquipmentWorkshop::NativeUi {
bool Start(std::uintptr_t base,bool enabled,bool validateOnly,void(*log)(const char*));
void Stop() noexcept;
bool Active() noexcept;
using EquipmentLayout=float(*)(float);
void SetEquipmentLayout(EquipmentLayout) noexcept;
using StatusObserver=void(*)(void* nativeContext,unsigned nativeCapacity) noexcept;
bool StatusBridgeInstalled(std::uintptr_t base) noexcept;
void SetStatusObserver(StatusObserver) noexcept;
#ifdef FFXHOOKS_TESTING
void FrameEnvironmentForTests(void* frame);
#endif
}
