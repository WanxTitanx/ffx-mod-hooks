#pragma once
#include <atomic>
namespace FfxHooks::SpiraElemental::Bridge {
// Published at startup. Disabling this new rule requires a restart; logical stop
// must not silently reactivate the old elemental-conversion meaning of Touch.
inline std::atomic<bool> imperilTouchRequested{false};
}
