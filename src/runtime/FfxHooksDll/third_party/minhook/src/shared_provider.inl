/* Jarvis-HOOK shared-provider adapter. Original integration code, 2026-09-29.
 * Compiled into this MinHook instance after its private lock/helpers.
 * The provider serializes patching with Fahrenheit; our registry contains only
 * targets successfully created by this client. No foreign ApplyQueued, ALL_HOOKS
 * or Uninitialize is ever invoked. */
typedef struct {
    HMODULE module;
    MH_STATUS (WINAPI *initialize)(VOID);
    MH_STATUS (WINAPI *create)(LPVOID, LPVOID, LPVOID*);
    MH_STATUS (WINAPI *enable)(LPVOID);
    MH_STATUS (WINAPI *disable)(LPVOID);
    MH_STATUS (WINAPI *remove)(LPVOID);
} SHARED_PROVIDER;
static SHARED_PROVIDER g_shared = {0};

MH_STATUS WINAPI MH_BindSharedProvider(HMODULE provider)
{
    SHARED_PROVIDER next = {0};
    MH_STATUS status = MH_OK;
    if (provider == NULL || sizeof(void*) != 4) return MH_ERROR_MODULE_NOT_FOUND;
    next.initialize = (MH_STATUS(WINAPI*)(VOID))GetProcAddress(provider, "MH_Initialize");
    next.create = (MH_STATUS(WINAPI*)(LPVOID,LPVOID,LPVOID*))GetProcAddress(provider, "MH_CreateHook");
    next.enable = (MH_STATUS(WINAPI*)(LPVOID))GetProcAddress(provider, "MH_EnableHook");
    next.disable = (MH_STATUS(WINAPI*)(LPVOID))GetProcAddress(provider, "MH_DisableHook");
    next.remove = (MH_STATUS(WINAPI*)(LPVOID))GetProcAddress(provider, "MH_RemoveHook");
    if (!next.initialize || !next.create || !next.enable || !next.disable ||
        !next.remove || next.create == MH_CreateHook) return MH_ERROR_FUNCTION_NOT_FOUND;
    EnterSpinLock();
    if (g_shared.module == provider) { LeaveSpinLock(); return MH_OK; }
    if (g_hHeap != NULL || g_shared.module != NULL) status = MH_ERROR_ALREADY_INITIALIZED;
    else if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)next.create, &next.module) ||
             next.module != provider) status = MH_ERROR_MODULE_NOT_FOUND;
    else g_shared = next;
    LeaveSpinLock();
    return status;
}
BOOL WINAPI MH_SharedProviderBound(VOID)
{
    BOOL bound;
    EnterSpinLock(); bound = g_shared.module != NULL; LeaveSpinLock();
    return bound;
}
static MH_STATUS SharedCreate(LPVOID target, LPVOID detour, LPVOID* original)
{
    UINT8 before[MEMORY_SLOT_SIZE];
    PHOOK_ENTRY hook;
    LPVOID trampoline = NULL;
    MH_STATUS status;
    if (g_hHeap == NULL) return MH_ERROR_NOT_INITIALIZED;
    if (!IsExecutableAddress(target) || !IsExecutableAddress(detour)) return MH_ERROR_NOT_EXECUTABLE;
    if (FindHookEntry(target) != INVALID_HOOK_POS) return MH_ERROR_ALREADY_CREATED;
    if (ForeignEntryJump(target) || !ReadCode(target, before, sizeof(before))) return MH_ERROR_PATCH_CONFLICT;
    hook = AddHookEntry();
    if (hook == NULL) return MH_ERROR_MEMORY_ALLOC;
    memset(hook, 0, sizeof(*hook));
    hook->pTarget = target; hook->pDetour = detour;
    memcpy(hook->backup, before, sizeof(JMP_REL));
    hook->sharedAboveReadable = ReadCode((LPBYTE)target-5, hook->sharedAbove, sizeof(hook->sharedAbove));
    status = g_shared.create(target, detour, &trampoline);
    if (status != MH_OK) { DeleteHookEntry(g_hooks.size-1); return status; }
    hook->pTrampoline = trampoline;
    if (original != NULL) *original = trampoline;
    // A provider may have changed the entry concurrently. Retain the unpublished
    // record rather than calling an unguarded foreign RemoveHook on changed bytes.
    if (!ReadCode(trampoline, hook->trampolineBackup, MEMORY_SLOT_SIZE) ||
        !CodeMatches(target, before, sizeof(before))) return MH_ERROR_PATCH_CONFLICT;
    return MH_OK;
}
static MH_STATUS SharedTransition(UINT pos, BOOL enable)
{
    PHOOK_ENTRY hook = &g_hooks.pItems[pos];
    MH_STATUS status;
    UINT8 entry[2], original[2];
    if (!PatchIntact(hook)) return MH_ERROR_PATCH_CONFLICT;
    if (hook->isEnabled == enable) return enable ? MH_ERROR_ENABLED : MH_ERROR_DISABLED;
    if (enable && !hook->patchAbove && hook->sharedAboveReadable &&
        !CodeMatches((LPBYTE)hook->pTarget-5, hook->sharedAbove, 5)) return MH_ERROR_PATCH_CONFLICT;
    memcpy(original, hook->backup, sizeof(original));
    status = enable ? g_shared.enable(hook->pTarget) : g_shared.disable(hook->pTarget);
    if (status != MH_OK) return status;
    hook->isEnabled = enable; hook->queueEnable = enable;
    if (enable && !hook->patchAbove) {
        if (!ReadCode(hook->pTarget, entry, sizeof(entry))) return MH_ERROR_PATCH_CONFLICT;
        if (entry[0] == 0xEB && entry[1] == 0xF9) {
            if (!hook->sharedAboveReadable) return MH_ERROR_PATCH_CONFLICT;
            hook->patchAbove = TRUE;
            memcpy(hook->backup, hook->sharedAbove, 5);
            memcpy(hook->backup+5, original, 2);
        }
    }
    return PatchIntact(hook) ? MH_OK : MH_ERROR_PATCH_CONFLICT;
}
static MH_STATUS SharedApply(BOOL queued, BOOL enable)
{
    UINT i;
    // Preflight every selected target before the first change. The provider's
    // exact calls may still fail partway; callers retain their may-have-run fence.
    for (i=0;i<g_hooks.size;++i) {
        PHOOK_ENTRY hook=&g_hooks.pItems[i];
        if (hook->isEnabled != (queued ? hook->queueEnable : enable) && !PatchIntact(hook))
            return MH_ERROR_PATCH_CONFLICT;
    }
    for (i=0;i<g_hooks.size;++i) {
        PHOOK_ENTRY hook=&g_hooks.pItems[i];
        const BOOL desired=queued ? hook->queueEnable : enable;
        if (hook->isEnabled != desired) {
            const MH_STATUS status=SharedTransition(i,desired);
            if (status != MH_OK) return status;
        }
    }
    return MH_OK;
}
static MH_STATUS SharedRemove(LPVOID target)
{
    UINT pos; MH_STATUS status;
    if (g_hHeap == NULL) return MH_ERROR_NOT_INITIALIZED;
    pos=FindHookEntry(target);
    if (pos==INVALID_HOOK_POS) return MH_ERROR_NOT_CREATED;
    if (!PatchIntact(&g_hooks.pItems[pos])) return MH_ERROR_PATCH_CONFLICT;
    if (g_hooks.pItems[pos].isEnabled) {
        status=SharedTransition(pos,FALSE);
        if (status!=MH_OK) return status;
    }
    status=g_shared.remove(target);
    if (status==MH_OK) DeleteHookEntry(pos);
    return status;
}
static MH_STATUS SharedRelease(VOID)
{
    if (g_hHeap==NULL) return MH_ERROR_NOT_INITIALIZED;
    while(g_hooks.size) {
        const MH_STATUS status=SharedRemove(g_hooks.pItems[g_hooks.size-1].pTarget);
        if(status!=MH_OK) return status;
    }
    HeapFree(g_hHeap,0,g_hooks.pItems); HeapDestroy(g_hHeap);
    g_hHeap=NULL; g_hooks.pItems=NULL; g_hooks.size=g_hooks.capacity=0;
    return MH_OK;
}
