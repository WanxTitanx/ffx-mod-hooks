#pragma once
// F7AiSwap.h — "F7 Monster AI Swap" (Jarvis-HOOK): per-monster/per-ability status injection.
//
// Lane: Jarvis-HOOK. Gate: modules\config\f7_aiswap.flag OU FFXHOOKS_ENABLE_F7_AISWAP=1.
//
// CONCEITO: o jogador configura, por monstro e por ability (anim1 id), status effects adicionais
// que sao aplicados AO ALVO quando o monstro usa aquela ability. NAO edita bins — so RAM em runtime.
//
// OFFSETS (fonte de verdade: FFXProjectEditor/FfxLib/Memory/MemoryChr.cs — nao o plano):
//   +0x0E  u16  monster id (!= 0xFFFF = slot ocupado)
//   +0x438 u8   Seck_target_id   -> HINT forte de alvo (NAO target final resolvido) [guarded]
//   +0xDD6 u8   Stat_action      -> HINT forte de "acao em andamento" (NAO linha executada) [guarded]
//   +0x606 u16  Status_suffer    -> flags dos status que o actor SOFRE agora (bit = StatusByteList idx)
//   +0x608 StatusDurationByteList -> turns restantes por status "com duracao" (13 bytes)
//   +0x616 u16  Status_suffer_extra -> extra flags (Curse, Doom, Shield, ...)
//
// NOTA HONESTA: Stat_action e Seck_target_id sao HINTS (nao verdades causais). O write so acontece
// com o gate ON e aplica status no campo Status_suffer do alvo. Requer RT2 para confirmar a semantica
// da janela de acao. Por padrao, os hints leem mas o apply e governado por flag.
#include <stdint.h>

namespace FfxHooks {

#define F7_AISWAP_ENTRIES_MAX   16
#define F7_AISWAP_ABILITIES_MAX 16
#define F7_AISWAP_STATUS_COUNT  25    // StatusByteList (Death=0 .. Slow=24)

// statusOnHit[i] = duracao (0-255) do status i da StatusByteList; 0 = sem efeito.
struct F7AiSwapAbility {
    uint16_t abilityId;                  // anim1 id da skill
    uint8_t  statusOnHit[F7_AISWAP_STATUS_COUNT];
};

struct F7AiSwapEntry {
    uint16_t monsterId;
    int      abilityCount;
    F7AiSwapAbility abilities[F7_AISWAP_ABILITIES_MAX];
};

struct F7AiSwapConfig {
    bool             enabled;
    int              entryCount;
    F7AiSwapEntry    entries[F7_AISWAP_ENTRIES_MAX];
};

// ── API ────────────────────────────────────────────────────────────────
bool F7AiSwap_IsEnabled();
bool F7AiSwap_Install(uintptr_t base, void (*log)(const char*));
void F7AiSwap_Remove();
void F7AiSwap_Tick();                    // chamado do F7_TickMainThread (1x/frame, main thread)
const F7AiSwapConfig& F7AiSwap_GetConfig();
bool F7AiSwap_Reload();                  // recarrega do JSON em disco (usado pelo menu)
bool F7AiSwap_SaveConfig();              // atomico (.tmp + MoveFileEx)
void F7AiSwap_Log(const char* fmt, ...);
// Conveniencia p/ menu: aplica status manual a um entry/slot (retorna count de status aplicados).
int  F7AiSwap_ApplyAbilityNow(uint16_t monsterId, uint16_t abilityId);

} // namespace FfxHooks
