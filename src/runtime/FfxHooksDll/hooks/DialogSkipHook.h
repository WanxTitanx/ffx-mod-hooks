#pragma once
#include <stdint.h>
/* DialogSkipHook.h — Dialog/cutscene voice skip (Onda 3, Operacao Demonio 2026-08-02).
 * Porte SEGURO do patch hardcoded `ret 8` do UnX legado (RVA 0x30B040):
 * hook de comportamento em FFX_FmodVoice_ReadEventData; gate input.dialog_skip. */
namespace FfxHooks { bool InstallDialogSkipHook(uintptr_t moduleBase, void* logFn); }