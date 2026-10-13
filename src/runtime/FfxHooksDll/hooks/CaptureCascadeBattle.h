#pragma once
#include <cstdint>
#include <filesystem>
namespace FfxHooks::CaptureCascade::Battle {
bool Initialize(const std::filesystem::path& game,void(*log)(const char*),void(*warning)()) noexcept;
bool Ready() noexcept;
bool AdmitNativeAssets(std::uintptr_t base) noexcept;
bool Run(std::uintptr_t base,void* context,bool(*original)(void*,int*),int* result) noexcept;
void Stop() noexcept;
}
