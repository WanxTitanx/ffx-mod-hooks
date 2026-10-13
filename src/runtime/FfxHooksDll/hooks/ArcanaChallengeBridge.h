#pragma once
#include <atomic>
namespace FfxHooks::Arcana::Challenge {
using Pump=void(*)() noexcept;
inline std::atomic<Pump> fieldPump{nullptr};
inline void Tick() noexcept {if(const auto value=fieldPump.load())value();}
}
