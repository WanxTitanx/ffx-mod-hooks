# Roadmap — what's done, what's not, what's pending

Jarvis-HOOK — updated **2026-09-29** against runtime source `2787386a7144af04c16f194029a4a35616bf8dea`,
merged into `main` by [PR #24](https://github.com/WanxTitanx/ffx-hooks/pull/24).

**Keep completed checks.** `[x]` means the specific deliverable on that line is
complete at its stated evidence level. Do not erase or uncheck it when follow-up
work appears: add a new pending item and link the regression/evidence. Source,
deployment, player acceptance and Production promotion have separate checks.
Historical observations retain their date/artifact boundary.

## 2026-09-29 Nuls and large integration

- [x] Publish and merge [PR #24](https://github.com/WanxTitanx/ffx-hooks/pull/24), with all three hosted checks passing; install/read back the combined DLL and private assets.

- [x] Compose the published recovery/Seymour/Sphere Grid lane with current main, preserving native page/hook identities, recent Arcana strikes, element names, rewards, languages and VFX.
- [x] Reuse Ward IDs 320/321 for NulHoly/NulShadow and append NulEarth/NulWind/NulPoison/NulGravity at 370–373, preserving the other 368 records and text-pool prefix.
- [x] Author six private animation DLL/texture clones (870–875); native donor code and vanilla assets remain unchanged.
- [x] Add default-OFF F8 `elemental.nul_spells`, save/character-bound learning, derived White Magic+ root and private charge consumption for all six identities.
- [x] Validate both Nul/Elemental installation orders, incomplete mixed coverage, native status preservation, menu routing and no automatic save unlocks (RT0/RT1).
- [x] Validate merged F8/Workshop, F7, Arcana/Spira, elemental, Ronso save and recovery adapter suites; preserve earlier completed checks below.
- [ ] Observe the six cloned visuals, party casting, learning/load lifecycle and mixed attacks in a separately recorded live session.
- [ ] Accept current-artifact Seymour and eight-character Grid restoration through full live save/menu/battle transitions.

See [source, tests and installation receipts](ai/NUL_ELEMENTS_INTEGRATION_2026_09_29.md).

## 2026-09-29 source update: built-in elements and display aliases

- [x] Register Custom 03/04 from an internal ten-element manifest when the default pack is absent; keep explicit invalid packages fail-closed.
- [x] Supply missing unused external slots in valid packs without changing existing keys, indices or bindings.
- [x] Add F8 keyboard/controller name editing for Holy, Darkness and Custom 01–04, with stable identity, Save/Reset/Cancel and duplicate/input validation.
- [x] Resolve aliases in F7, Scan, palette/visibility/order controls while preserving native item/ability/status names and data.
- [x] Make numerical Scan follow Core/Tactics plus Scan Extra Elements by default, preserving an explicit override.

These source deliveries supersede the earlier missing-pack prerequisite for the
standard two slots. Earlier completed checks remain as the historical record.
Runtime validation/deployment identity is recorded in the latest handoff; current
live acceptance remains a separate gate.

## Current checkpoint and evidence

| Item | Current evidence |
|---|---|
| Branch | `main`; published large recovery integration and six Nuls merged through PR #24 |
| DLL | 4,011,008 bytes; SHA-256 `5f6a76f3260a1aa0f5a36e0ff6e9f641f7d92e0dda969910d0d66a2debc3016e` |
| Deployment | 2026-09-29 10:28:24 UTC, 169 verified installed leaves; backup and 1,116 protected files unchanged |
| Source binding | 355 production inputs match the tested build |
| Latest focused matrix | Nul 40/40 in both orders; F8 4,621; menu 29,614; F7 4,020; Seymour 172 cases; Grid8 2,146 checks |
| Cross-environment check | Same MSVC Nul/menu binaries and final DLL loader/worker pass in a private Proton prefix; prior checkpoints retain their own results |
| Earlier integration | Shared Vanguard/Elemental/Spira/Aeon/Workshop/Arcana/save tests recorded with their exact source and binaries |
| Live acceptance | Pending for this exact consolidated candidate; prior user screenshots/observations are not a complete new RT2 matrix |
| Editor handoff | Complete dependency/contract dossier delivered; Editor implementation remains a separate backlog |

Evidence: [latest fixes, formats and deployment](research/F8_F7_ELEMENTS_MONSTER_REWARDS_2026_09_28.md),
[integration matrix](research/INTEGRATION_FINALIZATION_2026_09_28.md),
[current-state entry point](ai/CURRENT_STATE.md), and
[complete Editor dossier](ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md).

## Legend

| Tag | Meaning |
|---|---|
| **Implemented** | Present and connected in current source; required packages/gates still apply |
| **RT0 / RT1** | Offline tests / isolated native harness; neither proves live gameplay |
| **Deployed candidate** | Exact built DLL installed and read back; no automatic gameplay promotion |
| **In-game (dated)** | Earlier player observation; valid for the recorded case and artifact only |
| **LIVE / RESTART REQUIRED / READ ONLY** | F8 setting-activation classes, not evidence levels |
| **Pending RT2** | Needs reproducible observation on the selected current DLL |
| **RE / definition pending** | Missing native evidence or gameplay specification; no invented behavior |
| **Quarantined / retired** | Not offered as an effective supported runtime feature |

## Consolidation and latest user-facing fixes

- [x] Merge the selected Vanguard, Elemental Dominion, Spira Reforge and Aeon Ascension work with main's language and Arcana implementations.
- [x] Resolve shared clamp, damage, turn, action/Nul and save/checkpoint ownership in the consolidated DLL (RT0/RT1).
- [x] Complete the Windows x86 Release build and bind the candidate to its native source inputs.
- [x] Deploy the consolidated DLL, verify its hash and preserve configuration plus the previous DLL backup.
- [x] Group six non-Vanguard Extras controls under **Additional mods**, keeping **Vanguard Combat Engine** separate.
- [x] Group FieldScout Master/Heavy/Max/Ultra under **Dev > FieldScout**.
- [x] Expose eight native element identities and two keyed Hook-only elements in F7.
- [x] Expose both Hook-only element colors and visibility in Scan settings; share identities/labels with F7.
- [x] Publish external gameplay affinities only after successful native Apply/Restore; retire stale actor/generation selections.
- [x] Add **Cheats > AP/Gil Multipliers**, monster browsing/direct IDs, independent multipliers and original/individual/total previews.
- [x] Preserve native reward WORDs while computing individual then general factors above 65,535 with bounded wide arithmetic.
- [x] Add atomic, conflict-aware `monster-rewards-v1.tsv` persistence beside the active INI.
- [x] Deliver the complete Editor dependency and implementation dossier after DLL deployment.
- [x] Restore the donation section and add a complete PT-BR README with reciprocal language links; include both READMEs in the public release recipe.
- [x] Publish [v0.6.0-beta](https://github.com/WanxTitanx/ffx-mod-hooks/releases/tag/v0.6.0-beta) with the validated DLL, complete tagged source, Arcana assets and checksums; public Windows CI and all download-back hashes pass.
- [ ] Complete player acceptance of the exact consolidated DLL: menus, elements, rewards, combat coexistence and save/load lifecycle.
- [ ] Complete independent release review and explicitly promote a validated public package; local deployment is not this gate.

## F8 Dashboard and native ports

The current catalog has **90 boolean identities** across **seven tabs**:
System, Boosters, Cheats, Extras, Input, Dev and Reforge. There are **25 LIVE**,
**61 RESTART REQUIRED** and **4 READ ONLY** entries. All editable boolean defaults
are OFF. The dashboard defaults ON and fails closed on configuration-load error.
Nested controls retain catalog membership for resolution and bulk actions.

- [x] Native shell, tab/row navigation, glyph footers and focus-loss draining; earlier shell behavior was observed on 2026-09-16.
- [x] Mouse hover/click/wheel and staged scalar confirm/cancel integrated with the existing native menu.
- [x] System window, cursor, performance, camera and scene-freeze adapters; Input Windows-key/background/IME adapters.
- [x] Keyboard/gamepad shortcut editing, controller selection and digital-button mapping, with duplicate/reserved-key validation.
- [x] Independent voice, battle-sound and movie-audio selectors; native/PT-BR text-language selector.
- [x] Bulk **Enable Supported / Disable Supported** with Changed, Already, Unavailable, ExternalOverride, InvalidParameter and EffectiveMismatch outcomes.
- [x] Preserve external OFF markers and identify the dominating source; do not silently delete them from F8.
- [x] Preserve parent selection when leaving the new Extras, FieldScout, rewards and Scan submenus.
- [ ] Revalidate mouse/controller navigation, focus restoration and setting acknowledgements on the current DLL in game.
- [ ] Confirm OS/platform-specific input limits in the target environment; Wine cannot override a Super key already intercepted by the compositor.

### Boosters, cheats and rewards

- [x] Permanent Sensor, Entire Party Earns AP, party/enemy invincibility, Overdrive, critical, 99999 damage and rare-drop consumers.
- [x] Experimental Playable Seymour battle-roster consumer and exit cleanup; later recovery/Grid work is tracked in the new integration checkpoint above.
- [x] Global AP/Gil rate controls, independent enablement and 1–100 numeric factors.
- [x] Default-OFF per-monster reward seam, IDs 0–4095 and independent integer AP/Gil factors 1–1000, neutral at 1.
- [x] Normal/Overkill AP and Gil previews, missing-base disclosure and capped-result indication.
- [x] Apply base × individual × general before vanilla bonuses; compose with the actual upstream S.I.N. reward view and downstream Arcana effects.
- [x] Keep monster files/loot WORDs intact; limit pre-vanilla AP to 382,494,549 and Gil to 573,741,824.
- [x] Reject malformed, duplicate, truncated or conflicting settings-table writes without replacing a foreign edit.
- [ ] Verify above-65,535 rewards, Overkill, Gillionaire/AP bonuses, multiple enemies, defeat/escape and post-battle totals in live play.
- [ ] Validate Seymour selection/switch/turn/exit/next-battle restoration on the current artifact.

### Speed, FMV, Fastload and diagnostics

- [x] Speed Hack default shortcut cycle 1x/2x/4x/8x, native 2x/4x and reviewed field-service 8x, with effective-state display.
- [x] Separate restart-required FMV acceleration gate, owned frame/audio rate, bounded queue and neutralization/restoration (native RT1 evidence).
- [x] Compose Dialog Skip with the existing voice owner rather than double-hooking it.
- [x] Implement Fastload Autosave; earlier automatic loading was player-confirmed on 2026-09-19. The old "not started" plan status is superseded.
- [x] FieldScout Master/Heavy/Max/Ultra configuration and startup gates; Dev submenu grouping completed.
- [x] Keep Aurora diagnostics opt-in with Ctrl+Alt+F9/F10, distinct from Maechen's F9.
- [ ] Complete full-FMV playback, audible synchronization, subtitles, EOF/transition and focus-restoration RT2. Accelerated audio changes pitch.
- [ ] Observe dialogue/rendered-scene speed coverage and startup autosave recovery with a disposable save on this DLL.

Evidence: [native controls/S.I.N./FMV](reverse/F8_CONTROLS_SIN_AI_FMV_2026_09_23.md)
and [RT2 protocol](RT2_PROTOCOL.md). The older 36-row/eight-tab layout is historical.

## Vanguard Combat Engine — MOD-002

- [x] All 31 independent F8 controls have native consumers and default OFF.
- [x] Universal stat percentages and effective-HP defense bonuses.
- [x] Unhindered healing, additive Armor/Mental Break and action-end buff consumption.
- [x] Opposite-element weakness, guaranteed-hit policy and single/multi-hit scaling.
- [x] Status refresh, enemy duration resistance and once-per-incarnation Threaten.
- [x] Quickcast, White Magic compatibility and current-MP magic scaling.
- [x] Turn-cost switching and Eject/Shatter auto-reinforcement.
- [x] Thirteen custom equipped abilities, default IDs 135–147; separate ownership/type checks and remapping.
- [x] Vampirism uses actual HP loss; Follow Up preserves its non-recursive action boundary.
- [x] Equipped active commands, partial Overdrive costs and matching native display/debit consumers.
- [x] Share native combat seams with Elemental, Spira, Workshop and Arcana; source/native regression matrix recorded.
- [ ] Validate the combined combat rules and equipment/command UI in the exact deployed candidate.
- [ ] Complete Editor export/validation of IDs, owners, command encodings, payloads and package dependencies.

## Elemental Dominion — MOD-007, F7 and Scan

- [x] Admitted `ffx.mod007.elements.v1` registry, command bindings, fingerprints, profiles and equipment deltas.
- [x] Core affinity calculation, stable external keys and native/highest/lowest/weighted mixture policies.
- [x] Tactics: Imperil, Ward, Nul, timers/charges and shared action-lifecycle consumption.
- [x] Gravity profile policy, defined nonlethal boss behavior and explicit immunity handling.
- [x] Magic Break Damage Limit consumer up to 999,999 HP per hit under the selected policy.
- [x] Fingerprinted monster-profile and equipment/SOS affinity consumers with identity retirement.
- [x] Numerical Scan and shared labels; preserve both native Custom bits independently of presentation order.
- [x] Exactly eight native plus two Hook-only slots in F7 and the delivered Scan settings UI.
- [x] Both external color/visibility settings persist by stable element key; absent pack entries remain unavailable.
- [x] Keyed F7 JSON affinities retain native BYTE masks; failed/stale Apply transactions cannot publish requested gameplay state.
- [ ] Validate all ten element slots visually and in damage/Scan results, including OFF, failed Apply and subsequent battles.
- [ ] Build the Editor's full Elemental exporter and conflict/capability validation; do not equate registry capacity with UI slot count.
- [ ] Define/implement any additional presentation capabilities before advertising them; the current capability mask is authoritative.

## Equipment Workshop — MOD-004/005

- [x] Native Workshop navigation, equipment details/comparison and logical fifth ability.
- [x] Preserve the 22-byte native record and its four WORD slots; all fifth-slot/rank metadata remains external.
- [x] Refinement modes A/B, generic refinement bonuses, progression and economy previews.
- [x] Expansion, fusion, recipe/cost disclosure, owner/protected-equipment checks and native Customize constraints.
- [x] Native move/confirm/cancel/error audio, field-return and list/navigation fixes.
- [x] F8 Dev settings for explicit development-only cost/progression overrides.
- [x] Transaction state, intent journal, save fingerprints, checkpoint recovery and native-save projection.
- [x] Compose native equipment and save presentation with Arcana, Ronso and paid Aeon receipts.
- [ ] Complete current-build live purchase/fusion/refinement, donor retirement, cancel/reload/crash recovery and old-save migration cases.
- [ ] Replace the Editor's old experimental Workshop JSON inspector path with a production-format implementation and validators.

Evidence: [Workshop economy](WORKSHOP_ECONOMY.md),
[integration matrix](research/INTEGRATION_FINALIZATION_2026_09_28.md) and
[complete Editor handoff](ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md).

## Spira Reforge and Aeon Ascension

- [x] Shared 27-entry ID catalog, defaults 148–174, collision checks, owner/type/payload admission and mappings.
- [x] Mana Spring, Break Limits, Devil's Bargain and Auron's Warden's Oath consumers.
- [x] Double/Triple Drop native reward consumer with party-maximum factor and bounded item slots/quantities; no AP/Gil multiplication here.
- [x] Element Eater; HP/MP, all-stat, STR/MAG and DEF/MDEF native payload combinations.
- [x] Fourstrike/Fourtouch defined native four-element base, without inventing unspecified riders.
- [x] Paid Aeon HP/MP and damage upgrades for acquired canonical Aeons, with owner/piece-bound receipt authorization.
- [x] Gil/material debit, receipt persistence, immunity preservation and no current-HP/MP refill on purchase.
- [x] Paid save checkpoint projection/recovery; rejected reads do not masquerade as confirmed resets.
- [ ] Define and implement Arcane Focus and Spell Spring; both remain inactive.
- [ ] Define and implement Foolstrike/Fooltouch; both remain inactive.
- [ ] Specify any Fourstrike/Fourtouch riders beyond the implemented native base before implementing them.
- [ ] Complete current-build purchase/removal/reload, native caps, drop composition and multi-mod live acceptance.
- [ ] Implement Editor authoring/export and make reserved/inactive effects visibly different from usable effects.

## Text languages — MOD-006

- [x] Merge the native text/font pack loader into main and expose F8 System > Text languages.
- [x] API/schema 2 admission with the documented older-version compatibility and exact source identities.
- [x] Menu/battle/event/scene-subtitle resources, atlas/metrics/shadows and native fallback.
- [x] Capacity, control-byte and line-width validation; language changes require restart.
- [x] Keep voice/battle-sound/movie-audio language selectors independent from text selection.
- [ ] Implement the Editor text-and-font exporter; the audited `Mod006Readiness.CanExportPack` is false.
- [ ] Author and verify the complete selected translation package; DLL support does not mean all text is translated.
- [ ] Validate glyph coverage, accents, long lines, fallback and every supported resource family in game.

Contract: [MOD-006 Editor handoff](ai/MOD006_EDITOR_HANDOFF.md).
New dubbing, arbitrary texture text and burned-in movie text are not implied.

## Spira: Arcana Fayth — MOD-008

### Elemental major-card additions — 2026-09-29

- [x] Preserve every existing major-card effect while adding Shadow, Earth, Wind,
  Poison and Gravity strikes with matching Wards; retain Sun's Holy pair.
- [x] Extend typed card capacity to ten without changing acquisition/save identity.
- [x] Resolve Poison/Gravity weapon hits through the shared elemental/F7 affinity
  owner, with signed absorption, native-element mixtures and bounded admission.
- [x] Keep blindness/Poison statuses and Demi/fractional HP separate from these strikes.
- [x] Use Earth, Wind, Poison and Gravity as default labels; preserve stable keys
  and every explicitly saved alias, including former Custom names.
- [ ] Reproduce the combined card strikes, affinity/Scan display and optional
  Holy/Shadow visuals in live gameplay on the matching new DLL (RT2).

Contract: [Arcana elemental strikes](ai/ARCANA_ELEMENTAL_STRIKES_2026_09_29.md).

- [x] Compile all 78 cards, seven character loadouts, up to three slots, acquisition/collection and Twin/Constellation modes.
- [x] Native Equip UI, Status/Auto-Abilities effects, card artwork, navigation audio and installed-package validation.
- [x] Combat/turn/reward effects and persistent collection/loadouts, including supported prior-catalog migration.
- [x] Shared save projection for temporary stats and coexistence with Workshop/Aeon/Ronso metadata.
- [x] Separate default-OFF Arcana and full-deck development controls; no dependency on Spira Reforge for standalone use.
- [x] Integrate the prior visibility, native Status/audio and MP-balance fixes into consolidated main.
- [ ] Finish current-build visual/gameplay acceptance, card combinations, rewards and old-save retention.
- [ ] Define a supported authoring/export contract before treating concept JSON as a dynamic card pack.

Guide and package inputs: [Arcana](../research/mod_008_arcana/README.md).

## F7 In-Live menu (RT0/build + isolated runtime/policy RT1; live callback RT1/RT2 pending)

| Feature | Status | Detail |
|---|---|---|
| F7 native shell and glyph footers | Implemented; earlier In-game observation | Backspace footer and focus-loss behavior are merged/deployed; exact-current-DLL acceptance remains separate |
| Difficulty: Max/current HP/MP, Overkill, STR/DEF/MAG/MDF/AGI/LCK/EVA/ACC | RT0 + isolated runtime/policy RT1 | RAM-only transforms, immutable baselines, transactional ratios, retry and compare-before-restore |
| Difficulty: Elemental weak/resist/absorb | **Offline runtime candidate** | Included in the deployed candidate; exact native BYTE masks plus two separately keyed external elements; live acceptance pending |
| Difficulty: Auto-statuses | **Offline runtime candidate** | Native innate words and exact native remove/apply helpers; included in deployed candidate, live acceptance pending |
| Difficulty: Status immunities | **Offline runtime candidate** | Exact 25-byte table, minimum values preserving stronger native resistance; JSON editor only |
| Difficulty: Per-area presets (N2) | Configuration/runtime supported | Native editor still pending |
| Force Last Battle | Implemented, pending RT2 | Main-thread encounter route, bounded 1–9 repetitions |
| Music lock / battle-entry override / randomizer / fade | Implemented, pending RT2 | Existing bounded playlist/expiry behavior retained |
| Music playlist editing / track-name crosswalk | Partial | UI and complete verified name map remain pending |
| Monster AI observer | Implemented, pending RT2 | Registration/cleanup/dispatch telemetry, zero mutation |
| General Monster AI Swap | RE pending | Empty compatibility whitelist; the bounded S.I.N. script route does not make general swapping safe |

### F7 retained issue checklist

- [ ] K-01: Add actual machine-detour scheduler RT1, then run the separately authorized
- [x] K-02: Prove exact element and status field xrefs/widths before enabling any runtime write.
- [ ] K-03: Add a native menu editor for per-area (N2) replacement rules.
- [ ] K-04: Music playlist edit — add UI
- [ ] K-05: Complete track name crosswalk (181 tracks)
- [ ] K-06: Validate the observe-only lifecycle plus normal/force/death dispatch families,
- [ ] K-07: Revalidate Difficulty preset Apply (admission/status split, lane-fixed) and the

K-02's native field/width proof and implementation are complete in the
[F7 contract](F7_INLIVE.md); live acceptance remains covered by K-01/K-07 and
the ten-element checklist. K-07's old "lane-fixed"/"after lane merge/deploy"
wording is retained as issue history: merge/deploy are now done; revalidation
is the remaining work. The 181-track estimate is historical; K-05 must reconcile
the actual index/name crosswalk rather than infer a count from a range.

## S.I.N. RAM, curse scripts and legacy tooling

The **legacy disk-writing field hook remains quarantined**: it installs no
runtime writer and launches no external materializer. The separate current
RAM/script route is implemented for the reviewed Macalania pilot. These are
different paths and must not be reported as a single "unavailable" feature.

- [x] Seed/visit/config persistence and bounded natural-encounter selection.
- [x] Native walking-encounter capture and full monster-identity admission.
- [x] Reviewed Macalania room mapping, including the player-observed Snowfield field 333, with shared roster/seed identity.
- [x] Private exporter for 47 compatible profiles across ten pilot species and UNI-001–008 curse types, including Chimera in the reviewed roster.
- [x] Native registration/allocation admission, retained immutable AI views and command/source fingerprints.
- [x] Shared Difficulty stat scaling, bounded curse names and Threat-scaled unsigned reward views.
- [x] Correct typed targets, once-only guards and termination in the repaired curse pack; RT0/native RT1 evidence recorded.
- [ ] Reconfirm Counter March no-softlock, Frost-Flood party targeting and support recipients in live play. Prior player failures stay part of the record.
- [ ] Expand only through reviewed compatible profiles; pilot coverage is not all-monster or all-area support.

### Retained SIN TODO

- [x] Implement UNI-005 through UNI-008
- [ ] Implement multi-worker monster support
- [x] Design and review a bounded RAM-only S.I.N. replacement

The completed UNI and bounded-RAM tasks refer to the separate implemented pilot,
not reactivation of the old materializer. Multi-worker expansion beyond reviewed
profiles remains pending. Offline UNI-001 Gloom, UNI-002 March, UNI-003 Rush and
UNI-004 Ward remain tooling assets; the old UNI-005–008 "reserved" status is
superseded by the private pilot exporter. Thunder Plains' old cap=1 exclusion
and the cancelled legacy hook-to-injector RT2 do not become support claims.

Evidence: [walking encounter/reward repair](reverse/SIN_RANDOM_ENCOUNTER_REPAIR_2026_09_23.md),
[Snowfield admission](reverse/SIN_SNOWFIELD_ADMISSION_2026_09_24.md), and
[player findings plus target/guard repair](reverse/SIN_PLAYER_FOLLOWUP_2026_09_24.md).

## Arena+

- [x] Native Arena+ browser/hub and Reforge settings submenu, with master, progression bypass, resolver, victory and music gates.
- [x] Bounded CustomMix Ultra actor selection and exact carrier request; restore the owned formation window after the shared InitScene call.
- [x] RAM mix library/battlefield controls and explicit launch/cancel/focus-loss ownership.
- [x] Preserve the distinction between the current RAM route and quarantined legacy disk-backed composition.
- [ ] Validate carrier entry, actor positions, cancel/escape/victory/next battle and external music overrides on the current DLL.
- [ ] Treat victory/resolver telemetry as diagnostics until its intended gameplay changes have their own evidence.

## Maechen F9 assistant

- [x] Native translucent title/input/answer/footer UI, single-sampled input and bounded answer paging.
- [x] Protocol-v1 `POST /api/maechen/game/v1` client with `{locale, question, version}`; earlier UI/transport round-trip observed on 2026-09-16.
- [ ] Verify current service-side grounding and the historical R5 bundled-guide/wiki arbitration fix in its own repository/deployment; this DLL audit does not establish its completion.
- [ ] Select and implement an approved protocol-v2 bounded local-context contract if desired; no such client support is claimed here.
- [ ] Define conversation-memory behavior; current protocol is stateless.
- [ ] Revisit save-file analysis only with a concrete format/privacy contract; raw save upload is not part of this client.
- [ ] Revalidate current-candidate F9 transport and response presentation against the selected service.

## Other runtime/lab families

| Family | Current status | Remaining boundary |
|---|---|---|
| Nova Super Damage | F8 Reforge, restart-required, integrated | Live damage/cap composition |
| Ronso Mana pool | F8 Reforge, restart-required, integrated with costs/save lifecycle | Current-build persistent ownership and load/save acceptance |
| Shared Nul / legacy NulWard | Shared consumers integrated; legacy experiments remain independently gated | Experimental writeback/Nova interaction must not inherit blanket compatibility |
| Grid Teach v4.5 / Lancet Dual Grant | F8 Reforge, restart-required | Native teaching/grant RT2 and dependency gates |
| NulWardTeach legacy | Compatibility route; Grid Teach preferred | Not a second simultaneously assumed teacher |
| Item Stack Cap | Configurable 1–255, restart-required | Native inventory cap/save restoration RT2 |
| Double/Triple Drop | Native reward consumers integrated | Item slots/quantity/party-composition RT2; separate from AP/Gil |
| Scan Expanded / Extra Elements | Stats/MP, numerical affinities, colors/visibility integrated | Current-build display/gameplay agreement |
| PhaseTurnEdge | Disabled in build | No shipped gameplay claim |
| Legacy SinCurseHook writer | Quarantined | No detour, external process launch or runtime-area claim |

## ffx-probe

The probe is a **separate optional DLL**, not a dependency implicitly enabled by
F8. Historical READ, WRITE, cdecl CALL and ForceBattle evidence remains attached
to the original probe sessions (including READ RT2 on 2026-06-03). An old
`.RT2OFF` deployment report is not proof of the current installed probe state.
Hooks' four module rows are READ ONLY; no file-rename behavior is claimed.

### Retained Probe TODO

- [ ] Re-activate probe in deploy (remove .RT2OFF with backup)
- [ ] Wire dashboard toggle to file rename
- [ ] Wire editor FfxProbe_Service to MMF
- [ ] Implement thiscall CALL (ECX shim)
- [ ] Adversarial RT2: ForceBattle during menu, FMV, active battle

Probe reactivation is an explicit future installation task; first establish the
actual file/heartbeat state. File-rename wiring needs an intentional UI/ownership
design because the current module rows are informational. Avoid remote-thread
execution as a substitute for the documented main-thread contract.

## FFX Mod Studio / Launcher integration

- [x] Deliver the [complete Editor dependency dossier](ai/EDITOR_INTEGRATION_COMPLETE_HANDOFF_2026_09_28.md): 40 custom ability identities, 31 Vanguard rules, 88 F8 controls, 78 Arcana cards, build dependencies, formats and 62 implementation tasks.
- [x] Audit the existing Editor authoring/manifests, language readiness and experimental Workshop inspector without editing its working tree.
- [x] Document native equipment widths, logical fifth-slot storage, fingerprints, sidecar magic/ABI, transactions and recovery.
- [x] Document new F7/Scan keyed elements and per-monster AP/Gil TSV, ordering, preview and saturation contracts.
- [ ] EDR foundation: explicit Vanilla/OnlyMod project boundary, central custom-ID registry and dependency/capability validation.
- [ ] Export correct ability identities/payloads/owners, preserve fixed names where runtime admission requires them, and enforce actual runtime package limits.
- [ ] Complete Vanguard command/equipment mapping authoring and Elemental commands/profiles/equipment export.
- [ ] Implement production Workshop/Aeon persistence tooling; the experimental inspector is not this implementation.
- [ ] Complete Spira defined-effect export and explicit inactive-effect handling.
- [ ] Implement MOD-006 text/font pack export and validation; audit confirmed export readiness is still false.
- [ ] Add ten-element F7/Scan editors and independent per-monster AP/Gil authoring/preview/export.
- [ ] Implement package inventory, hashes, dependency checks, atomic install/rollback and current-DLL identity readback.
- [ ] Finish RuntimeDllManager release integration, FfxProbe_Service MMF wiring and LiveBattleLab routing through the intended contract.
- [ ] Consume canonical `contracts/` definitions; decide submodule/distribution integration when the contract is ready.
- [ ] Run Editor format/round-trip/negative tests, integrated package tests and authorized live validation before claiming end-to-end readiness.

The dossier's EDR-001–EDR-062 records are the detailed backlog. Completing this
documentation does not check off those external-repository implementations.

## Next validation and release sequence

1. Keep the exact DLL/source/package identity fixed for each chosen case.
2. Run separately authorized RT2 slices: new menu hierarchy/ten elements,
   AP/Gil ordering and vanilla bonuses, combat coexistence, then paid/save flows.
3. Retain original failures, raw logs, human observations and restoration hashes;
   add regression items without deleting completed implementation checks.
4. Implement the selected Editor/export gaps with their own tests and package
   readback. Do not substitute catalog/schema presence for a runtime consumer.
5. Complete independent review and public source/package/license/dependency
   checks. A new release, deploy and Production promotion remain explicit acts.

`run_f8_rt2.ps1 -Phase Preflight` / `-Phase Verify` validate a selected human-run
session. They do not launch/stop the game or Editor and do not deploy a DLL.
See [RT2 protocol](RT2_PROTOCOL.md). Hot `FreeLibrary` unload remains unsupported.

## Historical completion ledger

These observations are retained; they are not re-certified for the current DLL.

| Date | Retained result | Scope |
|---|---|---|
| 2026-06-03 | Probe READ observed | Original probe RT2 artifact/session |
| 2026-09-16 | F7/F8/F9 shells, Maechen transport, focus drain and the then-supported LIVE controls observed | Earlier build/catalog; does not cover every current F8 identity |
| 2026-09-16 follow-up | Backspace glyph, Difficulty admission, bulk-outcome truth and direct-F8 focus drain fixed | Those changes are now merged/deployed; current live revalidation is still open |
| 2026-09-19 | Fastload automatic loading player-confirmed | Earlier DLL; source implementation is no longer a future plan |
| 2026-09-24 | S.I.N. stat increases, names and Opening Veil player-confirmed; targeting/softlock failures also retained | Subsequent target/guard fix passed offline/native harnesses; player confirmation remains open |
| 2026-09-25 | Workshop menu SFX player-confirmed | Earlier Workshop DLL; not all fusion/save cases |
| 2026-09-28 | Language/Arcana/Vanguard/Elemental/Spira/Aeon/Workshop main consolidation and latest menus/rewards deployed | RT0/RT1 plus artifact readback, not a complete RT2/Production result |

## Never port / retired

The existing exclusions remain recorded:

- Soft Reset / KillMeNow unsafe byte patch (`D2A8E2 = 2`).
- WININET network auto-updater as a runtime update mechanism.
- Special K texture injection or loading incompatible proxy stacks as a Hooks dependency.
- Cheat Engine signature scans and the rejected unvalidated hardcoded patches
  (`0x392930`, stale interior voice `0x30B040`, `Btl.battle_trigger`).
- SphereGridTrueNewNode (`true_new_node.flag`) and SphereGridFullGridCompiler
  (`sg_full_grid_compiler.flag`), both retired rather than pending supported ports.

## Complete F8 control inventory

Snapshot of [F8FlagCatalog.cpp](../src/runtime/FfxHooksDll/hooks/F8FlagCatalog.cpp),
[VanguardCatalog.h](../src/runtime/FfxHooksDll/hooks/VanguardCatalog.h) and
[ModFeatureCatalog.h](../src/runtime/FfxHooksDll/hooks/ModFeatureCatalog.h).
Numbers are stable catalog indices, not screen-row positions. LIVE describes
activation; profile/gate/effective-state checks can still reject a request.
READ ONLY does not imply that the named external module is installed.

| Index | Tab | Control | Canonical key | Default | Activation |
|---:|---|---|---|---|---|
| 0 | System | Borderless window | `window.borderless` | OFF | LIVE |
| 1 | System | Keep cursor in game | `window.clip_cursor` | OFF | LIVE |
| 2 | System | Hide idle cursor | `window.hide_cursor` | OFF | LIVE |
| 3 | System | Performance display | `diagnostics.performance` | OFF | LIVE |
| 4 | System | Free battle camera | `camera.free_look` | OFF | LIVE |
| 5 | System | Freeze field scene | `camera.freeze_scene` | OFF | LIVE |
| 6 | System | Native Hooks | `plugins.dinput8` | ON | READ ONLY |
| 7 | System | External render module | `plugins.dxgi` | OFF | READ ONLY |
| 8 | System | External UnX module | `plugins.unx` | OFF | READ ONLY |
| 9 | System | Native diagnostics | `plugins.ffx_probe` | OFF | READ ONLY |
| 10 | Boosters | Permanent Sensor | `boosters.permanent_sensor` | OFF | LIVE |
| 11 | Boosters | Playable Seymour | `boosters.playable_seymour` | OFF | LIVE |
| 12 | Boosters | Speed Hack | `boosters.speed_hack` | OFF | LIVE |
| 13 | Boosters | SpeedHack FMV acceleration | `boosters.speed_hack_fmv` | OFF | RESTART REQUIRED |
| 14 | Boosters | Entire Party Earns AP | `boosters.entire_party_earns_ap` | OFF | LIVE |
| 15 | Cheats | Invincible Party | `cheats.invincible_party` | OFF | LIVE |
| 16 | Cheats | Invincible Enemies | `cheats.invincible_enemies` | OFF | LIVE |
| 17 | Cheats | Always Overdrive | `cheats.always_overdrive` | OFF | LIVE |
| 18 | Cheats | Always Critical | `cheats.always_critical` | OFF | LIVE |
| 19 | Cheats | Damage 99999 | `cheats.damage_value` | OFF | LIVE |
| 20 | Cheats | Always Rare Drop | `cheats.always_rare_drop` | OFF | LIVE |
| 21 | Cheats | AP Multiplier | `cheats.ap_100x` | OFF | LIVE |
| 22 | Cheats | Gil Multiplier | `cheats.gil_100x` | OFF | LIVE |
| 23 | Dev | FieldScout Master | `field_scout.master` | OFF | RESTART REQUIRED |
| 24 | Dev | FieldScout Heavy | `field_scout.heavy` | OFF | RESTART REQUIRED |
| 25 | Dev | FieldScout Max | `field_scout.max` | OFF | RESTART REQUIRED |
| 26 | Dev | FieldScout Ultra | `field_scout.ultra` | OFF | RESTART REQUIRED |
| 27 | Reforge | Arena+ Master | `arena_plus.master` | OFF | RESTART REQUIRED |
| 28 | Reforge | Arena+ Compose F7 | `arena_plus.compose_f7` | OFF | LIVE |
| 29 | Reforge | Bypass Progression | `arena_plus.unlock_all` | OFF | LIVE |
| 30 | Reforge | Arena+ Victory Hook | `arena_plus.victory_hook` | OFF | RESTART REQUIRED |
| 31 | Reforge | Arena+ Resolver Log | `arena_plus.resolver_log` | OFF | RESTART REQUIRED |
| 32 | Reforge | Arena+ Music | `arena_plus.music` | OFF | RESTART REQUIRED |
| 33 | Input | Block Windows Key | `input.block_windows_key` | OFF | LIVE |
| 34 | Input | Fix Background Input | `input.fix_background_input` | OFF | LIVE |
| 35 | Input | Filter IME | `input.filter_ime` | OFF | LIVE |
| 36 | Input | Dialog Skip | `input.dialog_skip` | OFF | LIVE |
| 37 | Dev | Fastload Autosave | `development.fastload_autosave` | OFF | RESTART REQUIRED |
| 38 | Dev | Arcana: full deck | `development.arcana_full_deck` | OFF | LIVE |
| 39 | Reforge | Arcana of the Fayth | `arcana.enabled` | OFF | RESTART REQUIRED |
| 40 | Reforge | Nova Super Damage | `labs.nova_super_damage` | OFF | RESTART REQUIRED |
| 41 | Reforge | Ronso Mana | `labs.kimahri_ronso_mana` | OFF | RESTART REQUIRED |
| 42 | Reforge | Equipment Workshop | `labs.equipment_workshop` | OFF | RESTART REQUIRED |
| 43 | Reforge | Native equipment details | `labs.equipment_workshop_native_ui` | OFF | RESTART REQUIRED |
| 44 | Reforge | Scan Expanded | `labs.scan_expanded` | OFF | RESTART REQUIRED |
| 45 | Reforge | Scan Extra Elements | `labs.element_scan_dark` | OFF | RESTART REQUIRED |
| 46 | Reforge | Grid Teach | `labs.grid_teach` | OFF | RESTART REQUIRED |
| 47 | Reforge | Lancet Dual Grant | `labs.kimahri_lancet_dual_grant` | OFF | RESTART REQUIRED |
| 48 | Reforge | Item Stack Cap | `labs.item_stack_cap` | OFF | RESTART REQUIRED |
| 49 | Reforge | Double/Triple Drop | `labs.double_triple_drop` | OFF | RESTART REQUIRED |
| 50 | Extras | Universal stat percentages | `vanguard.stat_pct_universal` | OFF | RESTART REQUIRED |
| 51 | Extras | Effective-HP defense bonuses | `vanguard.defense_ehp_scaling` | OFF | RESTART REQUIRED |
| 52 | Extras | Unhindered healing | `vanguard.healing_ignore_shell` | OFF | RESTART REQUIRED |
| 53 | Extras | Additive Armor/Mental Break | `vanguard.breaks_additive_damage` | OFF | RESTART REQUIRED |
| 54 | Extras | Consume buffs at action end | `vanguard.auto_crit_mp0_turn_end` | OFF | RESTART REQUIRED |
| 55 | Extras | Opposite-element weakness | `vanguard.element_opposite_weakness` | OFF | RESTART REQUIRED |
| 56 | Extras | Refresh status duration | `vanguard.status_refresh_duration` | OFF | RESTART REQUIRED |
| 57 | Extras | Enemy duration resistance | `vanguard.enemy_duration_resistance` | OFF | RESTART REQUIRED |
| 58 | Extras | Single-use Threaten | `vanguard.threaten_single_use` | OFF | RESTART REQUIRED |
| 59 | Extras | Guaranteed-hit policy | `vanguard.guaranteed_hits_no_miss` | OFF | RESTART REQUIRED |
| 60 | Extras | Quickcast | `vanguard.quickcast_replace_doublecast` | OFF | RESTART REQUIRED |
| 61 | Extras | White Magic in Double/Quickcast | `vanguard.dualcast_white_magic` | OFF | RESTART REQUIRED |
| 62 | Extras | Current-MP magic power | `vanguard.magic_mp_scaling` | OFF | RESTART REQUIRED |
| 63 | Extras | Switch costs a turn | `vanguard.party_switch_costs_turn` | OFF | RESTART REQUIRED |
| 64 | Extras | Auto-reinforce Eject/Shatter | `vanguard.eject_shatter_auto_replace` | OFF | RESTART REQUIRED |
| 65 | Extras | Single/multi-hit scaling | `vanguard.single_multi_hit_normalization` | OFF | RESTART REQUIRED |
| 66 | Extras | Hero's Bravery | `vanguard.hero_bravery` | OFF | RESTART REQUIRED |
| 67 | Extras | Energy Boost | `vanguard.energy_boost` | OFF | RESTART REQUIRED |
| 68 | Extras | Energy Burst | `vanguard.energy_burst` | OFF | RESTART REQUIRED |
| 69 | Extras | Efficiency | `vanguard.efficiency` | OFF | RESTART REQUIRED |
| 70 | Extras | Vampirism | `vanguard.vampirism` | OFF | RESTART REQUIRED |
| 71 | Extras | Follow Up | `vanguard.follow_up` | OFF | RESTART REQUIRED |
| 72 | Extras | P-Trade | `vanguard.p_trade` | OFF | RESTART REQUIRED |
| 73 | Extras | M-Trade | `vanguard.m_trade` | OFF | RESTART REQUIRED |
| 74 | Extras | Hero's Caution | `vanguard.hero_caution` | OFF | RESTART REQUIRED |
| 75 | Extras | MP Regen | `vanguard.mp_regen` | OFF | RESTART REQUIRED |
| 76 | Extras | Elude | `vanguard.elude` | OFF | RESTART REQUIRED |
| 77 | Extras | Energy Wall | `vanguard.energy_wall` | OFF | RESTART REQUIRED |
| 78 | Extras | Energy Barrier | `vanguard.energy_barrier` | OFF | RESTART REQUIRED |
| 79 | Extras | Equipped active commands | `vanguard.equipment_active_commands` | OFF | RESTART REQUIRED |
| 80 | Extras | Partial Overdrive costs | `vanguard.equipment_partial_overdrive` | OFF | RESTART REQUIRED |
| 81 | Extras | Elemental Dominion: Core | `elemental.core` | OFF | RESTART REQUIRED |
| 82 | Extras | Elemental Dominion: Tactics | `elemental.tactics` | OFF | RESTART REQUIRED |
| 83 | Extras | Elemental Dominion: Gravity | `elemental.gravity` | OFF | RESTART REQUIRED |
| 84 | Extras | Magic Break Damage Limit | `elemental.magic_bdl` | OFF | RESTART REQUIRED |
| 85 | Extras | Spira Reforge abilities | `spira.enabled` | OFF | RESTART REQUIRED |
| 86 | Extras | Aeon Ascension upgrades | `aeon_ascension.enabled` | OFF | RESTART REQUIRED |
| 87 | Cheats | Per-monster AP/Gil | `cheats.monster_rewards` | OFF | RESTART REQUIRED |
| 88 | Extras | Holy / Shadow weapon effects | `weapon_strike_vfx.enabled` | OFF | RESTART REQUIRED |
| 89 | Extras | Elemental Nul spells | `elemental.nul_spells` | OFF | RESTART REQUIRED |

Additional submenu parameters include general AP/Gil factors, item-stack value,
per-monster factors, element RGB/visibility, languages, shortcuts/button maps,
Vanguard IDs/command costs, Workshop modes/economy and Arena selection/library.
They are deliberately not counted as extra boolean catalog identities.

Maintenance rule: refresh this inventory from the source catalog when it changes;
keep prior `[x]` deliverables and add new pending work instead of erasing history.
