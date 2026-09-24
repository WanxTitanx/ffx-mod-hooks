# F8 Dashboard — reviewed branch candidate

The F8 dashboard is the native FLAGS submenu backed by the single catalog in
`hooks/F8FlagCatalog.cpp`. This reviewed branch candidate has eight tabs named **Plugins, Boosters, Cheats,
Scout, Arena+, Input, Dev, Lab** and exactly **37 rows: 15 LIVE, 15 RESTART REQUIRED, 7 NOT WIRED**.
The old GDI/56-row/30-item descriptions are not this implementation.

The tracked INI (`src/runtime/FfxHooksDll/ffx-hooks.ini`) and the built-in fallback in
`shared/Config.cpp` both set `[dashboard] enabled=1`. A config-load failure does not inherit that default: installation
fails closed. Dashboard visibility being ON does not arm gameplay rows; all 15 LIVE target
defaults are false.

This page reports source, RT0, and offline build truth. The exact candidate described here is not
deployed, and no Speed-cycle or configurable-multiplier RT2 case has run.
`LIVE` means a production consumer is connected in source, not that the case is Production.

## Catalog

| Tab | Row | Default evidence | Source status | Apply mode |
|---|---|---:|---|---|
| Plugins | dinput8 - hook proxy | true | NOT WIRED | — |
| Plugins | dxgi - Special K | false | NOT WIRED | — |
| Plugins | unx - UnX loader | false | NOT WIRED | — |
| Plugins | ffx-probe | false | NOT WIRED | — |
| Boosters | Permanent Sensor | false | LIVE | RuntimeAcknowledged |
| Boosters | Playable Seymour | false | LIVE (experimental battle roster) | RuntimeAcknowledged |
| Boosters | Speed Hack | false | LIVE | ConfigPolled |
| Boosters | Entire Party Earns AP | false | LIVE | RuntimeAcknowledged |
| Cheats | Invincible Party | false | LIVE | RuntimeAcknowledged |
| Cheats | Invincible Enemies | false | LIVE | RuntimeAcknowledged |
| Cheats | Always Overdrive | false | LIVE | RuntimeAcknowledged |
| Cheats | Always Critical | false | LIVE | RuntimeAcknowledged |
| Cheats | Damage 99999 | false | LIVE | RuntimeAcknowledged |
| Cheats | Always Rare Drop | false | LIVE | RuntimeAcknowledged |
| Cheats | AP Multiplier | false | LIVE | RuntimeAcknowledged |
| Cheats | Gil Multiplier | false | LIVE | RuntimeAcknowledged |
| Scout | FieldScout Master | false | RESTART REQUIRED | — |
| Scout | FieldScout Heavy | false | RESTART REQUIRED | — |
| Scout | FieldScout Max | false | RESTART REQUIRED | — |
| Scout | FieldScout Ultra | false | RESTART REQUIRED | — |
| Arena+ | Arena+ Master | false | RESTART REQUIRED | — |
| Arena+ | Arena+ Compose F7 | false | LIVE | ConfigPolled |
| Arena+ | Bypass Progression | false | LIVE | ConfigPolled |
| Arena+ | Arena+ Victory Hook | false | RESTART REQUIRED | — |
| Arena+ | Arena+ Resolver Log | false | RESTART REQUIRED | — |
| Arena+ | Arena+ Music | false | RESTART REQUIRED | — |
| Input | Block Windows Key | true | NOT WIRED | — |
| Input | Fix Background Input | true | NOT WIRED | — |
| Input | Filter IME | true | NOT WIRED | — |
| Input | Dialog Skip | false | LIVE | ConfigPolled |
| Dev | Fastload Autosave | false | RESTART REQUIRED | — |
| Lab | Nova Super Damage | false | RESTART REQUIRED | — |
| Lab | Ronso Mana | false | RESTART REQUIRED | — |
| Lab | Grid Teach | false | RESTART REQUIRED | — |
| Lab | Lancet Dual Grant | false | RESTART REQUIRED | — |
| Lab | Item Stack Cap | false | RESTART REQUIRED | — |
| Lab | Double/Triple Drop | false | RESTART REQUIRED | — |

The four plugin rows and the three evidence-default-true input rows remain read-only NOT WIRED.
A displayed/default value is not a runtime consumer.

## Lab controls

The six Lab rows control six features. All changes take effect on the next process;
no Lab toggle installs or removes a hook from the menu. Their original environment/flag names
remain supported. Unmarked INI true still opts in, unmarked false still allows a legacy flag,
and an F8 edit writes an authority marker so its OFF wins over that flag. Explicit environment
overrides retain their higher priority. No new disable-marker names are introduced.

Ronso Mana alone enables Kimahri's capacity of 200, affordable partial Ronso usage and full
saved charge. It does not grant charge or skills. Nova Super Damage independently removes
the upper HP damage cap only for Kimahri's Nova; it does not enable the pool. With the current
mod table, Nova costs 200 and therefore needs Ronso Mana's capacity to become affordable.
The obsolete Ronso Mana Apply row is removed; its old INI, environment and flag-file settings
are ignored, so they cannot silently enable Ronso Mana or the retired implementation.
Previously owned surplus still has its save-compatibility path when Ronso Mana is OFF.

Lancet Dual Grant requires Grid Teach and does not enable it implicitly. Bulk actions include
all six independent rows in one atomic edit. The eight tabs retain Dev at index 6 and Lab at
index 7. Lab has six boolean rows, one Item Cap scalar, two bulk controls and Back; normal
scrolling handles those ten rows.

Item Stack Cap uses `labs.item_stack_cap_value`, default 255, range 1–255. The scalar remains
editable while enabled and is displayed as an item count. The existing
`FFXHOOKS_ITEM_STACK_CAP` override wins at startup; that value is captured with the immutable
startup gates and clamped to the byte range. When installation succeeds, the technical line
keeps its actual startup ceiling separate from the configured next-process value. Native item
patching and save behavior are unchanged. The Item Cap row does not imply a reward multiplier.

NulWard/NulWardTeach, ElementScanDark, AbilitySfx, probes and retired Sphere Grid prototypes
remain outside this selected Lab slice. Their inclusion would need their own conflict/lifecycle
review. The new menu surface is offline-validated; each underlying Lab behavior still needs its
own authorized live case. See [the selected plan](superpowers/plans/2026-09-16-f8-lab-tab-integration.md).

## Runtime contracts and corrected addresses

The 11 `RuntimeAcknowledged` rows require a matching `[f8-runtime]` applied/restored readback.
The three `ConfigPolled` rows require requested/effective edit anchors and manual observation.
Speed Hack is armed by `boosters.speed_hack`. Ctrl+Shift+K cycles 1x/2x/4x/8x/1x and the
top-center indicator reports route truth. Its neutral ready state is exactly
`Speed Hack 1x - Armed [Ctrl+Shift+K]`. Native 2x/4x is shown as `Standard boost ARMED`; the hook
does not infer per-scene application from the native booster. Custom 8x is shown as
`Fast field scenes ARMED`, then as `APPLIED` only after a callback for the exact published
generation records its input/output delta. The reviewed target is a field-scene service tick;
dialogue and rendered-scene coverage must be observed in RT2 rather than inferred from the hook
alone. Pre-rendered FMV is unsupported. The 8x route composes with the single
Dialog Skip voice owner; 2x/4x does not require that dependency. F12 is not owned by Speed Hack;
it remains available for screenshots.
Compose F7 is gate-only in the Task-8 protocol: it proves the config/menu gate, never launches
Compose or copies/mutates battle files.

The catalog still has 29 boolean rows and preserves the legacy `cheats.ap_100x` and
`cheats.gil_100x` gates. The Cheats UI inserts `AP Rate` and `Gil Rate` parameter rows immediately
after those gates, so that tab has ten functional physical rows plus Back and scrolls through the
existing nine-row viewport. New scalar keys are `cheats.ap_multiplier` and
`cheats.gil_multiplier`; missing keys mean 100 for old configurations, while present values must
be strict decimal integers in `1..100`. Invalid text fails closed and remains repairable in the UI.

The supported executable is PE32/I386, SHA-256
`78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`.
Current source-candidate address/width truth is:

| Surface | RVA / layout | Width |
|---|---|---|
| Debug base | `0x00D2A8F8` (preferred VA `0x0112A8F8`) | 32-byte layout; each owned field below is 1 byte |
| Invincible Enemies / Party | Debug `+0x00` / `+0x01` | 1 byte each |
| Always Overdrive / Critical | Debug `+0x14` / `+0x15` | 1 byte each |
| Damage 99999 / Rare Reward | Debug `+0x18` / `+0x19` | 1 byte each |
| AP 100x / Gil 100x | Debug `+0x1A` / `+0x1B` | 1 byte each |
| AP multiplier signature / patch | `0x00399121` / `0x00399123` (resume `0x00399129`) | 8-byte signature; 6-byte patch |
| Gil multiplier signature / patch | `0x0039913C` / `0x0039913E` (resume `0x00399144`) | 8-byte signature; 6-byte patch |
| Permanent Sensor | Debug `+0x1D` | 1 byte |
| AP participation | `0x01F10EA0` (preferred VA `0x02310EA0`) | exactly 7 contiguous bytes |
| AP earned | `0x01F10EC4` (preferred VA `0x02310EC4`) | exactly 7 contiguous bytes |
| Party structure / slot-zero `in_party` | `0x00D32060` / `0x00D32088` | stride 148 bytes; active seven-slot contract |
| Speed native state | `0x008E82A4` (preferred VA `0x00CE82A4`) | aligned DWORD; states 1/2 arm native 2x/4x |
| Speed native availability | `0x008E82AC` (preferred VA `0x00CE82AC`) | read-only byte; not a scene classifier |
| Speed field-service tick | `0x00420C00` | relocated exact 16-byte prefix before detour |
| Dialog/voice event entry | `0x0030AEC0` | exact 16-byte prefix before the sole voice detour |

`AuroraD3DRender()` is the actual producer. It calls `UnXBoosterFrameTick()` from Present; the
portable lifecycle admits the first frame immediately, rejects reentrancy, and throttles later
ticks to **33 ms**. There is no F8 window timer in this reviewed branch candidate.

AP and Gil are independent compound bindings rather than members of the generic Debug-byte loop.
Admitted Present work verifies the complete immutable signature and waits for battle inactive,
then installs one exact `E9 rel32 90` patch per reward. Each target owns a 15-byte stub containing
non-clobbering absolute-memory `imul`, the original EBP-relative store, and an exact rel32 resume.
The stub transitions from RW/NX to RX/RO before the code site can point to it. Later edits use only
an aligned `InterlockedExchange` scalar, exact scalar readback, and then the matching Debug gate.
`APPLIED 25x` therefore requires scalar readback; a gate byte alone cannot prove the rate.
If an unowned gate changes from OFF to ON between the initial gate check and the ownership read,
the binding reports Conflict and never adopts that external byte or publishes an applied scalar.

The AP and Gil sites occupy the shared executable image page RVA `0x00399000`. Their byte ownership and ordinary
failures remain independent, but page protection is the unavoidable exception: one unresolved active
protection token makes the peer defer. A dedicated transaction lock spans protection, compare/write,
flush, restoration, and readback. Writable or non-executable prior protection is rejected; admitted
Present recovery must discharge the token, restore any possibly patched site, and prove original
bytes before the peer may install. Cache provenance is also sticky: after any potentially-mutating
site callback or failed restoration flush, observing original bytes is not enough to free the stub.
Recovery must re-confirm battle inactivity, flush the exact already-RX site span, and re-read the
original bytes. A later failed allocation free may retry without another flush because that cache
proof has already completed.

## FLAGS close, layout, and input

Source/RT0 behavior makes Back, Cancel, and direct F8 exit converge on one idempotent cleanup that
closes cleanly: it requests native close, releases only this menu's owned modal/cursor state, clears
local state, and does not respawn the native hub. This fixes the branch's prior Back/Cancel
black-screen regression at source/RT0 level; manual RT2 remains pending.

The selected row's concise description uses the header subtitle, while compact technical status
stays inside the main panel above the footer. Technical status is capped at 95 characters and keeps
requested/effective authority before LIVE availability, APPLIED or ARMED readback, and the latest
EDIT result. The shared `F7Sub_DrawCb` path used by FLAGS and the other F7Sub pages snaps its
highlight to the selected row before drawing it, so the highlight, description, and value always
name the same selection. Other menu families own separate render paths. The selection background
is drawn before row labels.
Confirm on a Rate row starts a draft. In edit mode Left/Right changes by 1, Up/Down changes by 10,
Confirm saves atomically, and Back or F8 cancels. The changing `Rate [25x_]` label reuses the row's
single text draw to stay inside the native text-pool budget. The exact compact footers distinguish
Toggle, Configure, and scalar-edit modes without adding a text draw. Mouse input remains tab-only
outside editing and is ignored during a draft; tab coordinates are converted from the game client
area rather than the decorated window, while mouse row and Back navigation remain deferred.
Cursor acquisition increments Win32's shared `ShowCursor` balance until visible, records only this
dashboard's increments, and reverses exactly that count on rejected open, direct return, or close.

The shared Present producer is preserved. Aurora developer UI is default OFF and requires an
explicit Aurora environment/config/file source. With that source enabled and Shift up,
Ctrl+Alt+F9 toggles actor visibility and Ctrl+Alt+F10 toggles detail; plain F9/F10 are not Aurora hotkeys.
When the developer gate is OFF, the switchboard contributes zero Aurora rows: Refresh becomes the
first row and plugin rows follow it. Enabling the gate prepends the two Aurora rows through the same
row mapper used for count, activation, labels, Refresh, icons, and plugin indexes.

## Playable Seymour boundary

Playable Seymour is **LIVE only as an experimental battle-roster consumer**. It is default OFF,
RAM-only, supported-profile/signature-gated, and does not patch the historical menu-filter sites.
It does not support Sphere Grid or claim full Playable Seymour.

The shared battle-entry infrastructure has one MinHook owner for `InitScene` RVA `0x00383ED0`.
The composer calls the original exactly once. Seymour is admitted only for the battle-state return
RVA `0x0038321C`; bootstrap return `0x00381C76` and startup/Sphere-Grid return `0x003821CF` are
explicitly rejected. Difficulty remains the sole `ActorPopulate` completion detour at RVA `0x00384010`.
Seymour owns one separate exit detour at `SyncPartyStatsFromActors` RVA `0x00386080`, admitted only
for its unique caller return RVA `0x00390F07`, and calls that original exactly once before cleanup.

The profile-gated infrastructure is installed inert at startup so the LIVE row can move from OFF
to ON without installing hooks from Present. OFF callbacks call vanilla exactly once and never
call the formation writer. The current F7 owner retains ResolveEncounter, InitScene, and ActorPopulate
as one process-lifetime batch; consequently, the Seymour LIVE producer also reserves the shared
ResolveEncounter target. Requesting Arena ResolverLog in the same startup is reported as an
ownership conflict instead of permitting a second detour.

The official function at RVA `0x00386A70` uses the exact cdecl ABI
`int __cdecl(uint8_t slot, int active)`. Entry requires party byte RVA `0x00D32494` to be exactly
`0x10`, no actor ID 7 in the persistent 3+17-byte lists, and a free `0xFF`. It uses the official
assigner for slot 7, runs vanilla battle initialization, then uses the official remover so the
persistent lists return clean while the battle-local copies retain Seymour. Cleanup owns only a
combined 20-byte multiset with exactly one `0xFF` replaced by one `0x07`, preserves legitimate
Switch reordering, rejects duplicates or foreign membership drift, and verifies party byte
`0x10` plus baseline membership after official removal. Local buffers may remain at the owned
current-battle image until next battle initialization; this is reported as RestorePending rather
than falsely claimed clean.

The four observed list leaves are persistent state `0x00D307E8` (3 bytes), persistent ability
`0x00D307EB` (17), battle-local state `0x00D2C895` (7), and battle-local ability `0x00D2C8A3`
(17). The status is `AVAILABLE` while clean/OFF, `ARMED BATTLE` while ON and awaiting the exact
entry caller, `APPLIED ON` after an acknowledged roster copy, and `RESTORE PENDING` whenever
cleanup ownership cannot be proved. RT0/RT1 and a successful x86 Release build are offline evidence only. Selection, Switch,
turn control, normal exit, next-battle cleanup, and no reintroduction remain RT2-pending.

## Arena+ and lifecycle boundaries

- Compose F7: LIVE / ConfigPolled. Opens the RAM editor for x3/x4/x5 and Ultra after the
  Arena+ Master startup request and the exact shared runtime have succeeded. It does not enable
  the retired disk composer. The F7 master is not a prerequisite.
- Bypass Progression: LIVE / ConfigPolled, default OFF. Unlocks boss choices without changing
  defeat flags. OFF restores the current-save requirements, including after restart.
- Music selects the Arena battle soundtrack. An existing `arena_plus_music.flag.off` or global
  `music.flag.off` still overrides the menu, and the status detail names the blocker. No automatic
  removal or renaming of these files occurs.
- Master, Victory Hook, Resolver Log, and Music: RESTART REQUIRED.
- Victory Hook is scaffold/log-only; it does not implement reward/progress mutation.
- Dynamic `FreeLibrary`/hot unload is unsupported. `DLL_PROCESS_DETACH` only publishes the
  lock-free Speed, Dialog, and booster stop requests; full teardown is not run under the loader lock. The portable
  normal-context removal primitive refuses battle-active or ambiguous state and never frees a
  stub while its site may still target it, but no dynamic-unload owner invokes that primitive.

## Manual RT2 gate

`run_f8_rt2.ps1` has two explicit phases, `Preflight` and `Verify`, and one of 14 finite cases.
It never starts/stops FFX or the editor, copies/deploys a DLL, invokes Steam, or waits for boot.
Preflight requires all 14 effective OFF and first reads and hashes four exact leaves. It then
validates both `cheats.ap_multiplier` and `cheats.gil_multiplier` for every selected case before
persisting any evidence snapshot. Missing means 100; present text must be digits-only, signed-`int`
safe, and in `1..100`, otherwise Preflight gives repair/remove guidance. Only after validation does
it snapshot the INI bytes and write an integrity-bounded manifest. Verify requires the exact evidence directory, both human
observations, a byte-offset log slice, and explicit `-RestoreConfigSnapshot` before atomic INI
restoration. See [RT2_PROTOCOL.md](RT2_PROTOCOL.md).

The generated AP/Gil manifest steps explicitly require a baseline, one saved non-default Rate,
ON/applied observation, OFF/restoration observation, and manual close. For either reward, Verify accepts
exactly one non-default `F8 scalar edit ... edit=SAVED` after the catalog anchor and before ON. Its
requested/configured value must match the later `state=applied gate=01 scalar=N` readback exactly;
OFF must end at `state=restored gate=00 scalar=none`, because disabling restores the gate without
reading or resetting scalar memory. Truthful baseline restored records are allowed only before ON;
additional, malformed, contradictory, or phase-invalid selected records are rejected. This
metadata and parser do not execute RT2.

Source, RT0, and successful builds remain below RT2. There is no F8 Production claim.
