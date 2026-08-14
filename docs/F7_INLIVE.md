# F7 In-Live — functional core

Gate: `modules\config\f7_inlive.flag` or `modules\f7_inlive.flag` or env `FFXHOOKS_ENABLE_F7=1`
(OFF by default). Hotkey: F7 (override: `FFXHOOKS_NATIVE_MENU_HOTKEY`).

Principle: levers on live RAM, persisted to JSON when the user saves — never edits `.bin` files.
Fully reversible (next battle re-populates from vanilla).

## RAM offsets (source of truth: FFX Mod Studio `MemoryChr` struct)

- Enemy list: `*(u32*)(base + 0xD37634)` (fallback `0xD34460`); entry stride `0xF90`, 8 slots;
  slot occupied when `*(u16*)(entry + 0x0E) != 0xFFFF` (monster id).
- Per entry: `+0x594` Max_hp, `+0x598` Max_mp, `+0x5A4` Overkill, `+0x5A8..0x5AF` Str/Def/Mag/
  Mdf/Agi/Lck/Eva/Acc (bytes), `+0x5DA/5DC/5DD` Elem absorb/resist/weak (bitmask 0x01 Fire..0x10
  Holy), `+0x630..0x634` Status_innate_auto (3x u16), `+0x641` Status_resist (25 bytes),
  `+0x6E4/0x6E8` Current hp/mp.

## Hooks (MinHook, x86, main thread)

| RVA | Function | Shim | Effect |
|---|---|---|---|
| `0x3828B0` | `FFX_Field_ResolveEncounterToken` | read-only post-original | captures last natural encounter (field, group) |
| `0x383ED0` | `FFX_Battle_InitSystemSceneAndActorTable` | read-only post-original | marks "new battle ready for stats apply" |

`F7_TickMainThread()` runs 1x/frame from the menu pump hook: applies difficulty once enemy
stats are in RAM (`Max_hp != 0`) and arms battle music in the same window.

## Apply semantics

- Multipliers in permille with sanity clamp; element masks ORed (additive); status immunity =
  direct byte writes; auto-status = OR into 3x u16 bits 0..24.
- Difficulty presets: Off, Hunter, Sombra de Sin (2000/1500/1500/1300/1300/1400), True
  Nightmare (2500/1800/1800/1600/1600/1600). Config also supports per-area and per-monster
  presets (no UI yet — K-03).

## Force Last Battle

`MsBattleEncountExe` @ RVA `0x380DE0` (`int __cdecl(int field, int group, float walkedDelta)`)
called on the main thread with the captured encounter; `repeatCount` 1..9 with ~120 ms gaps
(see K-01). Precondition: in field, out of battle; `ret=-1` means "encounter queued".

## Music

Lock (override track + seq via `FFXHooksBlock_v1`), battle-entry override (single-consumption,
45 s expiry, no field leak), randomizer (playlist, `GetTickCount`), fade (min fade frames).

## Config

`modules\config\f7_inlive.json` — atomic writes (`.tmp` + `MoveFileEx`).
