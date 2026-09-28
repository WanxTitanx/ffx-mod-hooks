# Jarvis-HOOK — External hook opportunities (2026-09-23)

## Scope and evidence boundary

This is an opportunity assessment, not authorization to build, deploy, or run FFX.
The review sampled Fahrenheit and its focused companion mods, FFX_MapWarper,
the FFX Editor warp research, and the current Hooks surfaces. It is not an audit
of all 320 repositories in `external-compare`.

- Hooks `main`: `a70a354`, six F8 tabs/29 catalog rows; untracked `.omo/` preserved.
- Integration target to recheck before work: worktree
  `ffx-hooks-fastload-autosave-20260916`, HEAD `cd0d3d2`, eight F8 tabs/37 rows.
  Its **Dev** tab already contains **Fastload Autosave**. The worktree has extensive
  unrelated dirty changes and must not be overwritten by this proposal.
- Fahrenheit local snapshot: `c149c847b3a24a66114956f87f1b008599736f75`;
  `LGPL-3.0-or-later`, per `README.md` and `COPYING.LESSER`.
- Companion snapshots: `fh-mods-csr` `fd35782` (MIT), `fh-mods-debug`
  `cce4614` (MIT), `fh-mods-truerng` `a2a6030` (MIT).
- FFX_MapWarper snapshot: `62a8bf9`; no license file was present. Treat its
  code as research only. No external code or assets were copied into Hooks.

Evidence labels below: **source** means inspected local code (including the
identified dirty Dev worktree), **historical
live** means a recorded user-observed game run with its old artifacts, and
**proposal** means no Hooks implementation or live verification exists.

## What the external projects actually show

| Evidence | Useful lesson | Limit for Hooks |
|---|---|---|
| Fahrenheit `src/runtime/events/game_loop.cs`: hooks `Sg_MainLoop` and FFX `AtelSetEventJump2`, emits pre/post update and post-title events | A scene transition observer can be a reusable event boundary | This is observation of one event call, not proof that its room/entrance arguments are interchangeable with the scene ID consumed by `RequestTransition` |
| Fahrenheit `src/core/handles.cs`: typed method handles, owner-associated hook chains and `chain_from` | A Dev health screen could expose each hook's owner, signature, activation and restore state | Hooks already has a MinHook batch coordinator and F8 runtime readback; extend those contracts rather than replacing detour infrastructure |
| Fahrenheit `src/core/ffx/savedata.cs`: current/last room, event-jump map and spawnpoint fields | Read-only location panels and transition trace can correlate scene, room and spawn | SaveData fields alone do not load a new area |
| Fahrenheit `src/core/atel/worker.cs`, `worker_ctrl.cs`; CSR `src/module.cs` | Worker/PC views can explain active events; CSR hooks event setup and patches selected scripts | ATEL layouts require profile and lifetime validation; blind script rewrites and generic cutscene skip are high-risk and already pursued by CSR |
| Fahrenheit `src/runtime/fileloader.cs` | External-file replacement and cross-load are technically possible | Broad file redirection is already Fahrenheit's strength and would add path/lifecycle conflicts; a bounded Studio preview is a more distinctive target |
| Fahrenheit `src/runtime/save_impl.cs` and TrueRNG `src/module.cs` | Save boundaries and RNG slots offer reproducibility hooks | Replacing the save system or collapsing RNG slots is not a small Dev tool; Hooks already has Fastload and F7 force-battle surfaces |
| FFX_MapWarper `FFXWarper.py` | Independent tool uses the same scene-request function offset `0x48E9D0`; includes map picker/free camera | It invokes shellcode through `CreateRemoteThread`; do not transplant this execution model into Hooks. No license was located |

The most distinctive product direction in this sample is **native F8 Dev
navigation connected to field telemetry and Studio authoring**. The sample is
too narrow to claim novelty over all FFX mods.

## Warp: established mechanism and missing integration

**Historical live:** Editor's
`docs/ai/FFX_LIVE_SESSION_WARP_AND_TEXTURE_CAPTURE_2026-06-07.md` records
`ffxprobectl warp 280 --pump-only` moving from `zk/zkrn0200` to
`az/azit0300` (Home Main Corridor), confirmed on screen. It also records
intra-area `nudge` persisting with instance position writes plus a reseat call
in that tested context. This is genuine evidence for the old probe, not RT2 of
the current Hooks DLL.

**Current source:** the tool moved from the Editor to
`src/runtime/FfxDinput8Probe/ctl/Program.cs` during the 2026-08-14 repo
split. It still exposes `whereami`, `scenes`, `pos`, `nudge`, `backup-pos`,
`restore-pos`, and `warp`. The cross-area path calls
`FFX_Scene_RequestTransition(sceneId, 0)` at RVA `0x0048E9D0` (preferred VA
`0x0088E9D0`); `--pump-only` lets the game's transition pump call InitScene.
This is a native game call that changes RAM and schedules a scene transition,
not merely writing an arbitrary map number into SaveData. Hooks already names
the transition and pending-state RVAs in `shared/ffx_addresses.h`.

The scene catalog is read from the loaded `cdrom.fid`/`cdrom.fnd` group-12
index, not from disk-order guesses or the volatile cached group base. See the
Editor `docs/reverse/FFX_FIELD_SCENEID_AREA_TABLE_2026-06-07.md` correction
and the current probe's `scenes` command. A valid scene ID can still enter an
event that expects story state; “any map” means every *validated, loadable*
scene offered by the active executable, with unsupported states rejected.

### Recommended F8 Dev surface

Keep **Dev** next to Fastload. Add **Map Navigator** as an action that opens a
child screen, not as a persistent boolean flag. The current FLAGS builder
assumes catalog rows are toggles/scalars and has tab-wide bulk operations;
an action must have its own row type, confirmation path, and be excluded from
Enable/Disable Tab. No new top-level tab is needed.

1. **Read-only first:** show current scene ID, resolved event path, current
   room/spawn, player XYZ, transition state, and an indexed, bounded scene list.
   Filter by path/name; label unknown or unloaded entries honestly. Keep the
   known-good `225 -> 280` route as a test case, not a hardcoded product limit.
2. **One-shot cross-area warp:** select a valid scene, show source and target,
   confirm, close/drain F8, then enqueue one request for a verified field-owner
   safe point. Do not invoke the game call from Present/draw code or use a
   remote thread. Determine the owner callback and exact ABI/signature from the
   supported executable before selecting a hook; reuse an existing owned
   callback only if its phase and thread are proved compatible.
3. **Admission:** no background effect until explicit confirmation;
   supported FFX executable/profile and unmodified target bytes; walkable
   field with a controlled instance; no battle, cutscene, save/load UI,
   pending transition or conflicting owner. Validate the destination against
   the current resource table. A mismatch disables the action visibly.
4. **Result:** distinguish queued, native request accepted, map load observed,
   controlled player ready, rejected, and timed out. Never turn an accepted
   call or `turn.completed` into proof of arrival. No automatic second warp on
   timeout; retain diagnostics and let the game recover naturally.
5. **Return/bookmarks, later:** capture source scene and position in memory
   before a request. Offer return only after the base warp is stable. Same-area
   position restore is a separate primitive with collision/actor-cache risk;
   prefer the native `FFX_Field_WarpActorToPosition` path if owner/ABI and
   controlled actor can be validated. The old probe's raw-write technique is
   a research fallback, not an unconditional shipping recipe. Returning to a
   place does not undo any story flags changed by its event script. User presets
   need explicit persistence format and must exclude game-derived assets.

## Opportunity ranking

| Priority | Hook/product idea | Evidence and existing Hooks fit | Main open risk / first gate |
|---|---|---|---|
| P0 | **Dev Map Navigator + cross-area warp** | Historical live warp, current probe, scene RVAs, existing Dev tab | Safe game-thread dispatch and F8 teardown; start read-only, then one bounded warp |
| P1 | **Transition trace / scene inspector** | Fahrenheit loop/warp event, Hooks FieldScout, current scene state | Correlate event-jump room, requested scene, loaded map and actor-ready without hook ownership conflict; observer-only RT2 first |
| P1 | **Dev hook health**: show owner, effective gate, signature status and restore-pending/conflict | Fahrenheit typed hook chains; Hooks `MinHookBatchCoordinator` and F8 runtime status already represent much of this | Read-only aggregation first; do not install/remove hooks from the diagnostic screen |
| P1 | **Position bookmarks and return** | Probe `nudge` historical live; Hooks FieldScout controlled-character observation | Collision, cutscene/actor pointer lifetime, destination-specific spawn; validate independently from cross-area warp |
| P2 | **Studio-to-field preview session**: jump to an authored map and capture FieldScout asset/trigger context | Fahrenheit fileloader pattern; Hooks FieldScout + Editor map tooling | Keep load-time file overrides scoped to a single manifest and restore path; prove no conflict with UnX/Fahrenheit/Special K before any resource replacement |
| P2 | **Encounter reproduction card**: record scene/zone/encounter and replay through existing F7 force path | Hooks already has Force Last Battle; TrueRNG proves per-slot RNG interception is feasible | Record-only first. Never silently change RNG or replay an encounter during a transition |
| P3 | **ATEL event timeline and named skip recipes** | Fahrenheit worker layouts and CSR event setup | ABI/lifetime and story flag integrity; only consider named, byte-verified recipes after a read-only timeline. Generic skip duplicates CSR |
| Park | General save-system rewrite, full VBF cross-loader, always-on free camera, wholesale debug-printf restoration | Fahrenheit/MapWarper already cover these patterns; Hooks has Fastload, Aurora/Scout and its own diagnostics | High conflict/scope cost relative to differentiated value |

## Suggested delivery slices and acceptance

1. **Evidence slice (RT0):** reconcile the dirty Dev worktree and executable
   identity; freeze a small scene-ID/path fixture from a permitted, read-only
   capture; test bounds, invalid paths, volatile-group-base avoidance, menu
   action versus toggle/bulk semantics, close/cancel and unsupported profile.
2. **Read-only Dev navigator (RT0/RT1):** location and paged catalog in the
   native F8 child menu, no transition call. An isolated x86 harness verifies
   pointer bounds, no writes, menu cleanup and source/destination labels.
3. **Warp writer (RT0/RT1):** single-generation queue, game-thread admission,
   once-only call to RequestTransition, native pump completion observation,
   timeout/conflict/teardown. Verify F7/F8/F9 and Fastload remain neutral when
   the action is idle. Do not install or remove hooks in `DllMain`.
4. **Separate, authorized RT2:** exact FFX.exe/DLL/config identities,
   disposable save, one feature armed, touched-file inventory and rollback
   under `docs/RT2_PROTOCOL.md`. Test known-good 225-to-280 (or another
   equally evidenced pair), disabled states, F8 closure, arrival path, player
   control, and restoration of inventoried files. A passing slice remains RT2;
   Production needs a separate promotion decision.
5. **After base acceptance:** add return/position bookmarks, then optional
   Studio/FieldScout integration and observer ideas as separately scoped work.

**Next concrete decision:** select the P0 read-only navigator and one-shot warp
as the first implementation scope. The current assessment supplies candidates
and gates, not a byte-verified implementation spec for the dirty Dev branch.

## Source pointers

- Hooks: `src/runtime/FfxDinput8Probe/ctl/Program.cs:990-1098`,
  `src/runtime/FfxHooksDll/shared/ffx_addresses.h:489-495`,
  `src/runtime/FfxHooksDll/hooks/FieldScoutHook.h`, `docs/RT2_PROTOCOL.md`.
- Dev worktree: `src/runtime/FfxHooksDll/hooks/F8FlagCatalog.cpp:12,152`,
  `src/runtime/FfxHooksDll/dllmain.cpp` FLAGS builder/input callbacks,
  `docs/F8_DASHBOARD.md`.
- Editor: `docs/ai/FFX_FIELD_WARP_TOOL_SPEC_2026-06-06.md`,
  `docs/ai/FFX_LIVE_SESSION_WARP_AND_TEXTURE_CAPTURE_2026-06-07.md`,
  `docs/reverse/FFX_FIELD_SCENEID_AREA_TABLE_2026-06-07.md`.
- External: `fahrenheit/src/runtime/events/game_loop.cs`,
  `fahrenheit/src/runtime/fileloader.cs`, `fahrenheit/src/core/handles.cs`,
  `fahrenheit/src/core/atel/worker.cs`,
  `repos/fahrenheit-crew_fh-mods-csr/src/module.cs`,
  `repos/fahrenheit-crew_fh-mods-truerng/src/module.cs`,
  `repos/Pika15959_FFX_MapWarper/FFXWarper.py`.
