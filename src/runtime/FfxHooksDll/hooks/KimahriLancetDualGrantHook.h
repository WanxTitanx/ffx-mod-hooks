#pragma once
// Kimahri Lancet dual grant — Ronso Rage learn (104–115) also grants Blue Mage clone (323–334).
// Chains through GridTeach grant shim when installed (sidecar + party bank).

#include <stdint.h>

namespace FfxHooks {

    typedef void (*KimahriLancetDualGrantLogFn)(const char* message);
    typedef int(__cdecl* GrantCommandFn)(int charIdx, int cmdId, int on);

    struct KimahriLancetDualGrantInstallResult {
        bool ok;
        bool armed;
    };

    KimahriLancetDualGrantInstallResult InstallKimahriLancetDualGrantHook(
        uintptr_t base,
        bool armed,
        GrantCommandFn grantThrough,
        KimahriLancetDualGrantLogFn log);

    bool RemoveKimahriLancetDualGrantHook(KimahriLancetDualGrantLogFn log);
    bool IsKimahriLancetDualGrantHookInstalled();

    // Called from GridTeach GrantCommand shim after a successful Ronso rage grant.
    void KimahriLancetDualGrantOnRonsoLearn(
        int charIdx,
        int rageId,
        int grantOk,
        void* retAddr,
        GrantCommandFn grantThrough);

} // namespace FfxHooks
