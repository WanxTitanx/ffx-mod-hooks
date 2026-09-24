# Equipment Workshop — Jarvis-HOOK experimental implementation

**Current main integration:** the shared core now has a native DLL adapter and
independent in-game menu. See
[main integration evidence](../../docs/reverse/MAIN_WORKSHOP_SIN_2026_09_24.md)
for activation, save ownership, supported effects and remaining RT2 limits.
The sections below describe the earlier isolated prototype and its evidence.

Concrete MOD-004/MOD-005 prototype on `codex/mod-ideas-precode-20260923`.
This is a C++ model, recoverable per-save host, dedicated interactive menu and
isolated native-consumer/producer adapter. **It is not installed in the Hooks DLL
and does not connect to a running game.** No F7/F8/F9 item or default key was added.
F10 already has two developer handlers in `dllmain.cpp`; the Workshop is unbound.

The menu at `http://127.0.0.1:8771/` runs real previews/transactions against the
same C++ model that the native harness uses. It imports a private save copy;
the original file is never a write target. The next integration gate is the
native inventory/save/menu lifecycle, not another browser mockup.

## Implemented scope

| Surface | Implemented and exercised | Limit |
|---|---|---|
| Identity | Independent monotonic piece and ability-instance IDs; exact before/after native event admission; create, byte-identical swap, equip, retire and fusion | Automatic hook installation, all shop/drop/treasure/save producers and unobserved byte-identical replacements are not covered. Unknown events quarantine metadata. |
| Persistence | Version1 per-save identity, workspace binding, SHA-256 envelope, atomic file replacement, durable before/after journal, paired backups, exact readback, interrupted-write recovery | Linux host only. No native game save callback or supported game-save export. |
| Reforge | Owner/type/model plus model-dependent name/formula/power/critical fields from a verified imported template; IDs/ranks retained | Unequipped ordinary pieces; one Master Sphere is a prototype recipe. |
| Fusion | One/two selected ability instances, arbitrary available destination slots, donor consumption, B ranks follow transferred instances, A catch-up | Same refinement mode. Celestial/Brotherhood cannot be donors; they may receive in their existing mode. |
| Native slots | Both proposed expansion recipes, capacity0–4, removal for a Clear Sphere, explicit numerical/touch→strike evolution pairs | Equipped/special pieces protected. Hidden nonempty words cannot be resurrected by expansion. |
| Refinement A | Rank0–10, every occupied ability contributes its per-ability recipe; insertion/replacement pays all catch-up ranks | Only executable effect families are eligible. No free conversion to B. |
| Refinement B | Rank0–10 per ability instance; rejection-sampled uniform eligible selection; persisted generator/counter; +40/+50 maxima | Owner catalyst + Ability Sphere; costs are known before the roll. No free mode conversion or replacement-rank inheritance. |
| Fifth slot | Per-piece unlock/selection, five-line menu, 24-byte temporary consumer view, native capacity remains≤4 | One Lv.4 Key Sphere unlock; four native slots required. Selection restricted to the verified effect slice. |
| Numeric effects | All24 native percent abilities #98–121, ranks0–10, +1 percentage point/rank, private row views | Native field-stat consumer region executed in RT1 with an explicit entry/exit bridge; full grid/field recalculation not claimed. |
| Auto-status | Fifth Auto-Shell#84/Auto-Protect#85 through the full native actor aggregator; original status bits unchanged; 1%/rank reduction after the respective native damage consumer | Strongest applicable rank, max10%, owner/thread/status checks. Full battle-hook placement remains uninstalled. |
| Menu | Search/filter, scroll, keyboard focus, separate action tabs, two-transfer fusion, before/after/cost confirmation, cancellation, saved readback | Dedicated loopback host, not yet an in-game render adapter. |

The131-row research catalog remains a **proposal catalog**, not131 implemented
effects. No AP#20, No Encounters#29 and Aeon Immunities#123 explicitly prevent
refining that piece. Other unimplemented effect families are also blocked before
charging materials. The interface explains this. There is no cosmetic rank-only
claim that a missing effect works.

## Safety and wire contract

`include/workshop.h` defines a packed little-endian v1 bridge shared by ctypes and
the x86/native tests: Piece80 bytes, State16260, Request62, Plan16492. This is a
**mod sidecar format**, never a native equipment/save format. `Validate` checks
versions, bounds, unique IDs, ranks, mode invariants and overflow. Aligned local
copies are used before passing packed IDs to STL reference-taking APIs.

Native gear stays22 bytes with abilities at14/16/18/20 and capacity at11. The
fifth WORD exists only in the24-byte temporary view. No `record+22` write and no
native capacity5 are permitted. State carries the exact native22-byte snapshot
for every piece; hash/fingerprint equality is never used to discover identity.

The host creates `origin.json`, `native.bin`, `sidecar.json`, paired `.bak` files
and a transient `pending.json` under the selected workspace. SHA-256 envelopes
detect accidental corruption; they are not signatures or an anti-tamper system.
The journal contains bounded exact before/after images. Recovery finishes only
when both current leaves belong to that pair. Foreign writes are preserved and
cause quarantine. Sidecar-only backup recovery requires the current native hash
to match exactly. Moving/copying a workspace changes its binding and is rejected;
an explicit fresh import starts fresh identities.

**`native.bin` is a Workshop working snapshot, not a supported game-save export.**
It preserves the PC container and updates the bounded gear/item regions, but the
game's save callback/checksum protocol is not wired. Do not copy it into a game
save directory. The host has no export/install button. The native adapter has no
disk I/O, detour installer or `DllMain` behavior. Both `Effects` and `Lifecycle`
start OFF, require profile admission, enforce owner-thread access and have
idempotent teardown. A future installed adapter must establish that admission
from actual PE signatures and save/lifecycle continuity, not from UI state.

## Run the isolated prototype

```sh
python3 research/equipment_workshop/run_checks.py \
  --save /absolute/path/to/private/user_ffx_000

python3 research/equipment_workshop/host/server.py \
  --workspace research/equipment_workshop/build/my-workspace \
  --import-save /absolute/path/to/private/user_ffx_000 \
  --port 8771
```

On subsequent starts omit `--import-save`. Use a new workspace for another import.
The host is Linux-only and binds127.0.0.1. Mutation requests require its session
token, a matching origin, a bounded request and a live preview confirmation.
The browser cannot submit native record bytes or manufacture inventory events.

Optional exact native checks use private inputs, never checked into Git:

```sh
python3 research/equipment_workshop/run_checks.py --skip-local \
  --windows windows11-dev-next --pe /absolute/path/to/FFX.exe \
  --kernel /absolute/path/to/a_ability.bin \
  --snapshot /absolute/path/to/validated-ui-state.bin \
  --proton-prefix /absolute/path/to/isolated-validation-prefix \
  --wine /absolute/path/to/Proton/files/bin/wine
```

`--snapshot` is optional; the595-count result includes a validated state exported
from the host with a fifth Auto-Protect ability. Without it, the native base suite
has592 assertions. The snapshot must remain a private build artifact.

The Windows runner creates one unique VMTasks directory, verifies transferred
SHA-256 values, builds with MSVC x86 `/W4 /WX /MT`, runs only the test executables,
copies the native harness back and removes its temporary directory. It maps the
PE as private memory; it never starts FFX. Proton runs the same MSVC harness.
Do not supply the game's Proton prefix. Build outputs and private fixtures are
ignored by the local `.gitignore`.

## Evidence and next gates

Current validation:165 C++ assertions on Linux/ASan/UBSan and Windows x86;
19 host/persistence/controller tests including the genuine PC save fixture;
595 native assertions in Windows and the same MSVC executable in Proton.
UI exercised in the Codex browser: import, search, keyboard tabs, cancellation,
fifth unlock/selection, removal, A refinement, receipt and restart/readback.

[Native evidence](../../docs/reverse/EQUIPMENT_WORKSHOP_NATIVE_2026_09_24.md) explains
which original instructions execute and which harness bridges are artificial.
[Editor handoff](../../docs/ai/EQUIPMENT_WORKSHOP_EDITOR_HANDOFF_2026_09_24.md)
separates mod extension data from native formats.

Remaining gates: complete native save/load association and all inventory producer
coverage; in-game standalone menu/focus integration; all affected preview, name,
price, direct-ID and battle refresh consumers; remaining effect families;
independent review; explicit deploy/RT2 authorization and disposable-save tests.
No independent review, game execution, DLL install, PR, merge or Production
promotion is claimed or performed by this prototype.

## Provenance

Original implementation by Jarvis-HOOK, based on this repository's MOD-004/005
research and proposals. Format facts were revalidated against the pinned private
PC PE/save/kernel and read-only Editor/Fahrenheit references recorded in the
research reports. No external source implementation was copied. Fahrenheit
commitc149c847b3a2 is LGPL-3.0-or-later; it is a reference for structure/ID names.
The131-row recipe catalog is reused from the existing research, without turning
its proposals into vanilla recipes. The separate Editor checkout was not edited.
