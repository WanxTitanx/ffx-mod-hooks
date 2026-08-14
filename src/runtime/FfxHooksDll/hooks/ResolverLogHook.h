// ResolverLogHook — Arena+ Multi Dark Aeon custom token resolver spike (Fase 4).
//
// Two-mode detour on FFX_Field_ResolveEncounterToken@0x7828B0 (PE RVA 0x3828B0):
//
//   1. LOGGER MODE (default once installed). Read-only: logs every call to
//      (token, returned-ptr, outField, outGroup, outEntry) so we can map the real token
//      traffic before designing the redirect path. NEVER modifies anything.
//
//   2. REDIRECT MODE (Opcao A in the RE doc, opt-in). When SetCustomTokenRedirects()
//      is given a table AND ArenaPlus_CustomTokenResolverEnabled() returned true at boot,
//      the shim consults the table BEFORE calling the trampoline. If the incoming token is
//      in the custom range 0xA001..0xAFFF AND the table has an entry, the shim substitutes
//      the alias (vanilla) token before calling the resolver, so the result is a fully
//      legitimate vanilla row pointer/field/group/entry tuple — downstream code never knows
//      it was redirected. The redirect is logged with a 'REDIRECT' tag.
//
// Gates:
//   arena_plus_resolver_log.flag           (file or env FFXHOOKS_ENABLE_ARENA_PLUS_RESOLVER_LOG=1)
//   arena_plus_custom_token_resolver.flag  (file or env FFXHOOKS_ENABLE_ARENA_PLUS_CUSTOM_TOKEN_RESOLVER=1)
//   FFXHOOKS_ARENAPLUS_RESOLVER_LOG_MAX    (env int, default 256; cap on logged calls)
//
// Plan: .cursor/plans/arena_plus_multi_dark_aeon_*.plan.md  (Fase 4 spike)
// RE doc: docs/reverse/FFX_ARENA_PLUS_CUSTOM_TOKEN_RESOLVER_HOOK_SPIKE.md
//
// Redirect safety: if the alias token resolves to a vanilla row, downstream paths
// see "vanilla everything" — the only side-effect is that the F7 launcher carrying a
// custom token in the future will land on the aliased battle. The hook is reversible:
// removing the flag or unloading the DLL restores vanilla behavior immediately.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace FfxHooks {

using ResolverLogFn = void (*)(const char*);

struct ResolverLogInstallResult {
    bool     ok;
    uint32_t reasonCode; // 0=ok, 1=disabled, 2=already_installed, 3=no_polyhook, 4=detour_failed
};

ResolverLogInstallResult InstallResolverLogHook(uintptr_t base, ResolverLogFn log);
void RemoveResolverLogHook();
bool IsResolverLogHookInstalled();

// Diagnostics for the parent DLL.
long ResolverLogHookCallCount();

// Custom token redirect (Opcao A). Pass an array of redirects; the hook keeps an internal
// copy. Pass count=0 (or table=nullptr) to clear the table. customToken must have its
// high word in 0xA001..0xAFFF; entries outside that range are ignored. The aliasToken
// should be a vanilla token that the resolver already knows how to resolve (e.g. 0x00DC0046
// for the Magus Sisters battle). Returns the number of entries accepted.
struct CustomTokenRedirect {
    uint32_t customToken;
    uint32_t aliasToken;
};
size_t SetCustomTokenRedirects(const CustomTokenRedirect* table, size_t count);

// Toggle the redirect path at runtime without uninstalling the hook. When disabled (default
// at boot), the shim still logs but never substitutes tokens. Returns previous state.
bool SetCustomTokenRedirectEnabled(bool enabled);
bool IsCustomTokenRedirectEnabled();
long ResolverRedirectHitCount();

} // namespace FfxHooks
