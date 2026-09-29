#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace FfxHooks::RonsoPool {
using LogFn=void(*)(const char*);
struct HookTarget {uintptr_t address=0;void* replacement=nullptr;void** original=nullptr;};
struct PreparedRuntime {
    std::array<HookTarget,5> hooks{};
    size_t count=0;
    bool ioRequired=false;
};
bool HasPersistentOwnership() noexcept;
bool PrepareRuntime(uintptr_t base,bool gameplay,LogFn,PreparedRuntime*,
                    const wchar_t* storageOverride=nullptr);
bool InstallIoImports() noexcept;
bool RestoreIoImports() noexcept;
void ActivateRuntime() noexcept;
// Published only after the existing owner installed native save/reset producers.
// This is infrastructure readiness, not Ronso gameplay or an active save identity.
bool IsSaveIoReady() noexcept;
// Optional post-close/readback publication, not the buffered fwrite event.
bool IsVerifiedSaveIoReady() noexcept;
void RequestStop() noexcept;
void DiscardUnpublishedRuntime() noexcept;
#ifdef FFXHOOKS_TESTING
using ImportProtectFn=bool(*)(uintptr_t,size_t,uint32_t,uint32_t*) noexcept;
void SetImportProtectionForFixture(ImportProtectFn) noexcept;
#endif
}
