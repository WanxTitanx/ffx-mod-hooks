#pragma once
#include <cstdint>
namespace FfxHooks::CaptureCascade::Capture {
bool Start(std::uintptr_t,void(*log)(const char*)) noexcept;
void Stop() noexcept;
}
