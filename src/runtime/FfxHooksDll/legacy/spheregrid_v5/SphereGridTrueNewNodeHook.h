#pragma once

#include <stdint.h>

namespace FfxHooks {

typedef void (*SphereGridTrueNewNodeLogFn)(const char* line);

struct SphereGridTrueNewNodeInstallResult {
    bool ok;
    int nodeCount;
    int linkCount;
};

SphereGridTrueNewNodeInstallResult InstallSphereGridTrueNewNodeHook(uintptr_t base, SphereGridTrueNewNodeLogFn log);
bool RemoveSphereGridTrueNewNodeHook(SphereGridTrueNewNodeLogFn log);
bool IsSphereGridTrueNewNodeHookInstalled();

} // namespace FfxHooks
