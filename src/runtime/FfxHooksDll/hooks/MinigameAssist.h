#pragma once
#include "MinigameAssistCore.h"

namespace FfxHooks::MinigameAssist {
enum class Code {Off,Ready,Unsupported,ScriptMismatch,Stopped};
bool Profile(std::uintptr_t base) noexcept;
using Feedback=void(*)(unsigned option,Code) noexcept;
bool Start(std::uintptr_t base,unsigned options,bool validateOnly,void(*log)(const char*),Feedback feedback=nullptr);
void Stop() noexcept;
bool Active() noexcept;
Code Status() noexcept;
}
