#pragma once
// Independent Scan Expanded and Scan/Sensor extra-element presentation.
// No authored texture, persistent width patch or new elemental gameplay.

#include <stdint.h>

namespace FfxHooks {
    bool StartElementHook(uintptr_t base,bool extraElements,bool expandedStats,bool validateOnly,void(*log)(const char*));
#ifdef FFXHOOKS_TESTING
    void ElementScanFullEnvironmentForTests(void* rotated,void* text,void* numberR,void* numberL,void* glyph);
    void ElementScanEnvironmentForTests(void* info,void* panel,void* texture);
#endif
    void InstallElementHook(uintptr_t base,bool extraElements,bool expandedStats,void (*log)(const char* message));
    void RemoveElementHook();
    bool IsElementHookInstalled();
    bool IsScanExpandedInstalled();
}
