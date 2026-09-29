# Built-in elements and hook display names — Jarvis-HOOK

## User-visible result

With Elemental Core enabled and a restart, the default registry now provides eight
native identities plus `hook.custom03` and `hook.custom04`. The two new elements
are available in F7 and numerical Scan without an external manifest. They begin
neutral and unassigned; no native attack or item is silently converted.

F8 > Reforge > Scan settings > Element names edits Holy, Darkness, Custom 01,
Custom 02, Custom 03 and Custom 04. Keyboard entry and an on-screen character
picker both stage a draft. Save publishes the name; Reset inherits the canonical
label; Back, Escape and focus loss preserve the previous saved value. Unsupported
or overlength keyboard input must be corrected before acceptance, rather than
silently saving a truncated name.

Aliases update element labels in F7, numerical Scan, colors, visibility and
custom-column order. They do not rewrite item/equipment attributes, ability or
spell names, the Darkness status name, native asset records or gameplay IDs.

## Admission and compatibility

The original report had Core/Tactics/Gravity/Magic BDL saved ON but no
`elemental-pack.json`. Previously the menu provider was never published without
that file. The Scan hook's independent Running indicator did not prove Elemental
registry admission. Additionally, numerical Scan previously defaulted OFF behind
an internal setting even when Core and extra Scan elements were selected.

Preparation now selects the built-in manifest only when no explicit pack is
selected and the default file does not exist, or when `elemental.pack=builtin` is
explicit. Existing/selected external manifests still undergo strict parsing,
executable/profile/signature checks and loaded-bank admission. Missing explicit
paths, unreadable files, invalid JSON and invalid signatures remain failures.

Valid packs with fewer than two external descriptors gain unused canonical
descriptors without moving their original indices or bindings. Reserved-key
collisions fail closed. All eight native bits remain BYTE values; external
identities never become invented `0x100`/`0x200` bits.

Numerical Scan now defaults to the conjunction of Scan Extra Elements and
Core/Tactics. An explicit `elemental.numeric_scan` setting retains precedence.
The gameplay and drawing hooks remain OFF by default and restart-gated; this
change introduces no new native detour site.

## Name persistence and UI ownership

Native display names use `element_names.native_10`, `.native_80`, `.native_20` and
`.native_40`, indexed by native bit. External names use
`element_names.<registry-key>`, including `element_names.hook.custom03` and
`element_names.hook.custom04`. Empty values mean Reset. Aliases take precedence
over canonical registry labels and are validated as 1–32 supported ASCII
characters after trimming surrounding spaces.

Case-insensitive duplicates, empty saves, control bytes and INI delimiters are
rejected. Valid aliases are resolved together before duplicate checking so a
saved name exchange survives reload. Malformed/colliding manual aliases fall
back to canonical names without invalidating the underlying registry. Reordered
external descriptors retain names by stable key.

The existing atomic Config transaction persists changes and verifies string
contents on readback. Failed persistence leaves the prior alias. Colors,
visibility and F7 `diff_elemExtra` / `elemExtra` keys are unchanged by renaming.

The existing window callback only edits the protected draft and publishes input
events. Config writes occur on explicit Save in the native menu owner. Bound
F7/F8/Workshop shortcuts are suppressed during text capture. Reset/close cancels
the capture; the detach fallback consists solely of four `InterlockedExchange`
operations, covered by the existing loader-lock safety contract.

## Validation and candidate

- Portable built-in registry/name validation: **56/56**, Linux normal and
  ASan/UBSan, and MSVC x86. The existing Elemental core runner includes the case.
- Native menu: **29,565/29,565**, including save/reset/cancel, invalid/duplicate
  input, reload, identity reorder, focus loss and unchanged equipment state.
- Elemental native modes: OFF **4**, Magic **27**, Core **43**, Tactics **86**,
  Monster **20**, Gravity **23/23**, Equipment **29**, built-in Core **25** and
  built-in all-four-controls **25**. The latter includes missing/invalid pack and
  unsupported-profile negative cases, actual F7-to-Scan affinities, alias display,
  unbound command preservation and retirement.
- Native presentation modes: **3 / 61,762 / 58,171 / 64,834 / 23,477 / 29,621**.
  The new mode proves implicit numerical presentation with Core + extra Scan
  and no hidden numeric flag, alongside legacy extra-element rendering.
- F7 core **4,020**, F7 native **176**, F8 **4,598**; full x86 Release build passes.
- The recovered Windows binaries and final DLL pass **21 isolated Proton cases**,
  including a validation-only DLL worker/loader smoke test. No game entrypoint
  or live session is launched.
- Source-packet tests **2/2**; context tests **114/114**. Additional local document
  and staged-diff checks accompany source delivery.

Candidate: **3,409,920 bytes**, SHA-256
`f4302d6770aeb8b190a9fde767bc80c6a547ad89103802b3c226409b44dadf0b`.
All **304 production inputs** match the immutable build packet. Documentation and
the newly included portable-test runner are the only later non-production changes.

Private Windows packets are under `.superpowers/mod007/` with labels
`elements-builtins-admission`, `elements-names-full-candidate`,
`elements-names-build` and `elements-builtins-portable-msvc`. The full-candidate
packet first exposed an overly long subtitle and the new detach call missing its
strict whitelist; both were corrected and the final F8/build packet passed.
Initial tests also caught string-pointer versus string-content comparison in the
name-save readback; the corrected persistence/menu cases are green.

Source/artifact recovery, complete production binding and Proton replay receipts
are under `work/element-names-20260929/`. These are RT0/RT1 results. Deployment
readback and the source commit are recorded in the subsequent handoff; live
visual/gameplay acceptance and Production promotion remain separate.


## Source and deployment completion

Source commit `5eb1f3c508beaa820b6605545fbaf10750f68400` is pushed on main.
At **2026-09-29 05:12:47 UTC**, the candidate above was atomically installed at
`<game>/modules/ffx-hooks.dll`. Preflight/immediate process checks were clear.
The prior DLL (`734a0bf94b56157648e5391ca06dfb1c60aad2c6bb27748762a67315c98b8ffc`)
has a verified backup at `work/element-names-20260929/deployment-backup/ffx-hooks.dll`.
All 178 protected installation files, including existing configuration, remained
byte-identical. The source, installed DLL and backup hashes were read back.
The game was not launched. The new menus still require player visual/gameplay
acceptance; source publication and this local deployment are not a new public
binary release or Production promotion.


GitHub Actions runs `36524962948` (build), `36524962983` (text-languages) and
`36524962843` (context) did not start their jobs. GitHub's check annotations report
failed account payments or a spending-limit block. This is an external CI
admission failure, not a passing hosted check and not a compiler/test failure.
The local Windows/Proton evidence above remains the validation for this delivery;
no billing/account settings were changed. Exact annotations are preserved in
`work/element-names-20260929/github-ci-blocker.json`.
