# Arcana elemental strikes implementation plan — Jarvis-HOOK

**Goal:** add weapon-element bonuses to major Arcana without removing existing
effects, combine them with the Holy/Shadow VFX branch, then integrate by PR into
main as explicitly requested by the user.

**Architecture:** retain the existing native actor overlay for Holy, Darkness,
Earth and Wind. Publish only the two hook-owned card effects through a bounded,
owner-thread provider to Elemental Dominion's existing damage/affinity pipeline.
No new detour, saved equipment word, command-table writer or item name is needed.

**Tech stack:** C++17, existing native x86/MSVC adapters, portable GCC regression
tests, Python catalog generators, existing Windows and Proton RT1 harnesses.

## Accepted contract

- Source base is `codex/holy-shadow-vfx-20260929` at `2ce1f3d`, including main
  `5cfe3c2`. The new work is isolated on `codex/arcana-elemental-strikes-20260929`.
- The user explicitly selected default labels Earth, Wind, Poison and Gravity.
  Keep `native.custom01`, `native.custom02`, `hook.custom03`, `hook.custom04`,
  native bits and every saved display override unchanged.
- The Sun already has Holystrike and Holy Ward; preserve them. Add Shadowstrike
  and Shadow Ward to The Moon, Earthstrike/Earth Ward to The Empress,
  Aerostrike/Wind Ward to The Chariot, Biostrike/Poison Ward to Death, and
  Gravitystrike/Gravity Ward to The Hanged Man. Preserve every original effect.
- Raise the bounded per-card capacity from eight to ten. Card IDs, keys, assets,
  ownership, acquisition state, save sidecar identity and slot counts remain.
- Shadow is Darkness element `0x80`, never blindness/Darkstrike. Earth is `0x20`
  and Wind is `0x40`. Poison is an element, never the Poison status. Gravitystrike
  uses the weapon's normal damage formula, never Demi, fractional HP or a cap bypass.
- The two extra strikes require active Arcana and Elemental Dominion Core.
  Unbound native weapon commands must have a validated kernel-row address and
  bounded live row, HP weapon flags and an eligible normal weapon formula.
  Revalidate the original row and provider at consumption; stop, mutation,
  foreign actor/thread, non-weapon commands and invalid packs fall back safely.
- Authored command bindings remain authoritative; augmenting non-NativeExact
  bindings can include card strikes. Match semantic external keys `spira.poison`
  / `spira.gravity` or stable built-in keys, never an arbitrary registry index.
- External card Wards halve positive resolved exposure, preserve immunity and
  absorption, respect locked profiles, and do not manufacture timed Ward stacks.
  Native card Wards continue using their existing native resistance flags.
- The existing shared affinity/Nul owner resolves mixed attacks once. A native
  Nul charge cannot nullify an uncovered external element or be consumed twice.
- New effects inherit OFF-by-default feature gates. The VFX gate remains
  separate. No game launch, DLL deployment or public release is part of this PR.

## Review focus

- A Moon with ten bonuses retains its existing eight, including sleep/confusion
  and damage mitigation; native blindness and elemental Shadow remain independent.
- Existing card acquisitions, item identities and user-defined element aliases
  survive the change; custom packs cannot accidentally bind an unrelated ninth slot.
- Poison/Gravity strikes honor F7 weakness/resistance/absorption and numeric Scan,
  coexist with native weapon elements, and preserve native formulas/caps.
- Invalid command pointers/headers, edited rows, retired providers, nested hits,
  foreign threads and feature stop cannot leak an elemental override.
- Combined VFX/native-element regression checks use the exact merged source;
  offline and isolated execution are not reported as live appearance validation.

## Steps and validation ledger

- [x] Baseline: portable Arcana suite and clean base identities.
- [x] Add failing card-overlay and default-label regressions.
- [x] Extend catalog capacity, add effects and regenerate typed catalog.
- [x] Implement the bounded extra-element provider and affinity integration.
- [x] Cover native/non-native separation, mixed hits, Ward semantics, fallback,
      stop/reload and alias persistence with portable and isolated native tests.
- [x] Validate complete Windows DLL, combined Holy/Shadow VFX, Arcana,
      Elemental/F7/F8/name regressions and same-binary Proton where available.
- [x] Update README, checked roadmap and Editor integration notes; exact staging,
      commit/push, PR into Holy/Shadow branch, then PR/merge into main.

Evidence and command receipts belong in the ignored
`work/arcana-elemental-strikes-20260929/` of this worktree. Build transport must
include every tracked runtime input and any new source/header before compiling.
Preserve all unrelated work, installed game files and public release artifacts.


## Review rulings and regression evidence

- The typed pack fingerprint necessarily changes when effects change. Card IDs,
  keys, major/minor identity and native-save hash still match. The deployed v4
  fingerprint `c6fe69e9f96cf776a943ebeba725ed65dc1872cc6e8e0e1539963c164df2453e`
  is explicitly added to the compiled compatibility list; no arbitrary pack is
  accepted. A real stored v4 extension is migrated in memory while the original
  sidecar remains untouched until the next completed native save. The new
  fingerprint is `9ffaa68fd969de9598684e813e570afd5c8ef30e702522f477d4e4cf78ec6ca7`.
- Review found that source-strike retirement was guarded, but a recipient-only
  Ward needed its own provider/snapshot guard. The preceding source snapshot
  reproduces the failure in the native `arcana-native-exact` case (43/44);
  `card-ward-retirement-red.log` records the controlled failing probe. Both
  production files were restored byte-for-byte afterwards. The corrected frame
  tracks recipient and source lifetimes independently.
- NativeExact cannot be combined with authored profiles by the existing pack
  contract. Its fixture is separate; the production parser was not relaxed.
- The first menu regression retained an old prohibition on Earth/Wind labels.
  It now checks the user-approved labels and unchanged bits. A separate check
  preserves explicit former Custom aliases after the default names change.
- An initial VFX run lacked `et_battle.bin` in the generic native fixture root.
  A new owned fixture lane combines the existing PE/save/CRT inputs and the
  exact VFX resource. Its whole-file SHA-256 is
  `1f4ebc789a7815c6a9162bc921b30ecd58a8f262733d47082d77b718769656e2`;
  the `7060...` hash in runtime evidence identifies the admitted resident program
  slice, not the whole file. Production resource admission was not weakened.


## Final local validation checkpoint

- Current worktree includes the VFX deployment record `e96bed2`; its source change
  from `2ce1f3d` is documentation only. Installed VFX is a separate artifact.
- Reviewed DLL: 3,431,424 bytes, SHA-256
  `3016fe298c208531370294c440b0f69408ad681ed8e256ddbef0a30425bcfadc`.
  All 310 production inputs match `.superpowers/mod007/arcana-name-priority-ffx-mod007-7c2536b9226f/receipt.json`.
- Native recipient-only Ward regression now passes 44/44, including independent
  source/recipient retirement; the controlled preceding snapshot failed 43/44.
- Explicit native v4 migration passes 39/39 in the shared Arcana/Spira harness;
  portable real-disk migration passes 33/33, including read-only migration.
- All portable Arcana checks pass with and without address/undefined sanitizers.
  F7/F8/menu/elemental/VFX consumers and the complete Windows build passed; the
  handoff records the bounded per-suite results and superseded fixture failures.
- All 26 same-binary Proton cases pass in the task-owned
  `work/arcana-elemental-strikes-20260929/proton/prefix`, with binary/fixture hashes.
- PR integration and hosted checks completed as recorded below. No deployment
  or public release is implied by this checkpoint.


## Promoted-default alias priority follow-up

PR #22 merged the first validated tree into Holy/Shadow. Before completing PR #23,
review found a saved alias could collide with a newly named default (for example,
a saved Holy alias `Poison`). Defaults now yield to valid saved aliases: only the
colliding promoted slot shows its previous `Custom 01`–`Custom 04` label. Raw
canonical descriptors and configuration are unchanged; Restore default follows
the same presentation rule. Invalid duplicate manual aliases still fail closed.

The portable regression first reproduced 9 failures (7/16), then passed 16/16
normally and with ASan/UBSan. The real native settings harness covers priority
and Reset. The renewed candidate above supersedes the first `be2031...` candidate;
all 310 production inputs match its new receipt. The newer Elemental/Menu binaries
are rechecked on Proton; the unchanged VFX binary retains its original 11 cases.


## Source integration completed

- [PR #22](https://github.com/WanxTitanx/ffx-hooks/pull/22) merged the feature into
  Holy/Shadow as `b532a5e65a78fb0984c3c50fd661d5642f8f2862`, with three hosted checks passing.
- Alias-priority follow-up `5349eb0afbb8f84be3b58c25328cfbc5900cbab3` also passed
  all three hosted checks before the final merge.
- [PR #23](https://github.com/WanxTitanx/ffx-hooks/pull/23) merged into main at
  `d21e576f5918750b823e1912079412e940de04fd` on 2026-09-29 08:48:43 UTC. Its Git tree
  exactly matches the reviewed final head. Primary main was fast-forwarded cleanly.
- The verified candidate and a 310-input receipt are also available in primary
  `work/arcana-elemental-strikes-20260929/delivery/` and `candidate-source.json`.
  The installed game DLL and public release were not replaced by this source integration.
