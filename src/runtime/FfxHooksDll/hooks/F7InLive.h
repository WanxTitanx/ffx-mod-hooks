#pragma once
// F7InLive.h — "FFX Editor - In-Live" (F7): Difficulty (RAM), Force Last Battle, Music.
//
// Lane: Jarvis-HOOK. Gate: modules\config\f7_inlive.flag OU FFXHOOKS_ENABLE_F7=1.
// Padrao do projeto: hook C++ MinHook + config sidecar JSON atomico (tmp + MoveFileEx)
// + block MMF (FFXHooksBlock_v1) para override de musica (mesmo contrato do editor C#).
//
// RAM Offsets (MemoryChr / FFXBattleActorRecord — fonte: FFXProjectEditor/FfxLib/Memory/MemoryChr.cs,
// validado no decompile de FFX_Battle_InitActorTable_structural 0x79C130 na COPY):
//   +0x594 Max_hp (i32) +0x598 Max_mp +0x5A8..0x5AF stats bytes
//   +0x5DA Elem_absorb +0x5DC Elem_resist +0x5DD Elem_weak (bitmask 0x01 Fire..0x10 Holy)
//   +0x630..0x634 Status_innate_auto (3 x u16; bit i = status i da StatusByteList: 0 Death .. 24 Slow)
//   +0x641 Status_resist (25 bytes, 1 = imune)
//   +0x6E4 Current_hp
// Enemy list: *(u32*)(base + 0xD37634) = g_BattleEnemyList; entry = list + 0xF90*slot;
//   slot occupied if *(u16*)(entry+0x0E) != 0xFFFF (monster id — decompile 0x79C130).
#include <stdint.h>
#include "../shared/ffx_hooks_block.h"

namespace FfxHooks {

// ── Persisted config (modules\config\f7_inlive.json) ─────────────────────
#define F7_AREA_RULES_MAX   16
#define F7_PLAYLIST_MAX     8
#define F7_STATUS_COUNT     25

struct F7DifficultyPreset {
    bool     enabled;
    int      hpMul;        // permille: 1000 = x1.00 · 1500 = x1.50 · 500 = x0.50
    int      mpMul;
    int      strMul, defMul, magMul, mdfMul, agiMul, accMul, evaMul, lckMul;
    int      overkillMul;
    uint32_t autoStatusMask;   // bits 0..24 = StatusByteList (Death=0 .. Slow=24) — aplica em innate_auto
    uint8_t  elemWeak;         // bitmask 0x01 Fire 0x02 Ice 0x04 Thunder 0x08 Water 0x10 Holy (OR)
    uint8_t  elemResist;       // idem (OR)
    uint8_t  elemAbsorb;       // idem (OR)
    uint8_t  statusResist[F7_STATUS_COUNT];  // 1 = imune
};

struct F7AreaRule {           // N2: regra por area (field row key do encounter)
    bool     enabled;
    int      fieldRow;        // -1 = qualquer area (fallback)
    int      hpMul, strMul, defMul, magMul, mdfMul, agiMul;
    uint32_t autoStatusMask;
    uint8_t  elemWeak, elemResist;
};

struct F7MusicConfig {
    int  lockTrack;        // -1 = none; senao 0..0xB5 (FMOD runtime id)
    int  battleTrack;      // -1 = none (muda a musica de ENTRADA da batalha)
    bool randomizer;       // sorteia da playlist a cada batalha
    int  fadeFrames;       // 0..600 (0 = default do jogo)
    int  playlist[F7_PLAYLIST_MAX];
    int  playlistCount;
};

struct F7ForceConfig {
    int  lastField;        // field row key (ResolveEncounterToken *a2)
    int  lastGroup;        // group index (*a3)
    int  lastFormation;
    bool hasLast;
    int  repeatCount;      // 1..9 (quantas vezes encadear o force)
};

struct F7Config {
    F7DifficultyPreset diffGlobal;
    bool diffByArea;                    // N2 ligado/desligado
    F7AreaRule areas[F7_AREA_RULES_MAX];
    int  areaCount;
    F7MusicConfig music;
    F7ForceConfig force;
};

// ── API (used by dllmain.cpp / menus) ──────────────────────────────────
bool F7_IsEnabled();
bool F7_InstallHooks(uintptr_t base, FFXHooksBlock* block, void (*log)(const char*));
void F7_RemoveHooks();
void F7_TickMainThread();          // chamado do pump hook (main thread): auto-apply difficulty + music battle
const F7Config& F7_GetConfig();
bool F7_SaveConfig();              // atomico (.tmp + MoveFileEx)
void F7_Log(const char* fmt, ...);

// Music
void F7_SetMusicLock(int track);      // -1 = none
void F7_SetMusicBattleTrack(int track);
void F7_SetMusicRandomizer(bool on);
void F7_SetMusicFade(int frames);
void F7_SetDifficultyLevel(int level);        // KEYSTONE B (2026-08-02): lever do F7 — 0..5 -> hpMul 1000..2000
void F7_MusicApplyLock();             // aplica override no block agora
void F7_MusicClearOverride();
void F7_MusicPreview(int track);      // toca a faixa agora (override + soundcmd) sem persistir
void F7_ResetMusic();                 // defaults: sem lock/battle/randomizer/fade + salva
const char* F7_StatusName(int i);     // nome do status 0..24 (coluna AUTO do DIFF)

// Force
void F7_ForceLastBattle();            // 1 click: MsBattleEncountExe(field, group, 0.0f) na main thread
void F7_ForceFieldBattle(int field, int group);  // KEYSTONE B: force com field/group arbitrario
void F7_SetRepeatCount(int n);
int  F7_LastEncounterField();
int  F7_LastEncounterGroup();
bool F7_HasLastEncounter();

// Difficulty
void F7_DifficultyApplyNow();         // aplica preset global ativo nos inimigos da RAM (batalha atual)
bool F7_DifficultyTryAutoApply();     // true se aplicou (stats prontos)
bool F7_DifficultyInBattle();         // true entre InitSystemScene e proxima cena
int  F7_DifficultyAppliedCount();     // inimigos modificados no ultimo apply
void F7_SetSkipForceCapture(bool v);  // dllmain chama antes de launch nao-natural (Dark Aeon/Combo/Custom/Arena)

} // namespace FfxHooks
