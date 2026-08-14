#pragma once

#include <stdint.h>

namespace FfxHooks {

typedef void (*SphereGridFullGridCompilerLogFn)(const char* line);

struct SphereGridFullGridCompilerInstallResult {
    bool ok;
    int extraNodeCount;
    int extraLinkCount;
    bool sidecarLoaded;
    bool hashMatched;
};

SphereGridFullGridCompilerInstallResult InstallSphereGridFullGridCompilerHook(
    uintptr_t base, SphereGridFullGridCompilerLogFn log);
bool RemoveSphereGridFullGridCompilerHook(SphereGridFullGridCompilerLogFn log);
bool IsSphereGridFullGridCompilerHookInstalled();

} // namespace FfxHooks
