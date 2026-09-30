# Jarvis-HOOK: F8/F7 sound feedback and Fahrenheit V2 hardening

## Delivered behavior

The compatibility branch now supplies native navigation, confirmation, rejection
and Back/Cancel feedback throughout the F8 settings families and the reviewed
F7 hub, Music, Force, Difficulty, observer, Arena+ and S.I.N. input paths.
No new audio assets, game installation changes or additional hook owner are used.

The native sound contract is movement/confirmation 1, rejection or blocked intent
3, and Back/Cancel 4. Successful saves, Apply, Reset, export and seed acceptance
use confirmation rather than the old cancel cue. A saved preference that cannot
become effective because of an external override remains a warning, with storage
and effective-state messages kept distinct.

One thread-local input scope combines multiple cues from the same callback and
nested handlers. An error cannot be replaced by a late movement cue. Deferred
Arena and hub actions wait for the actual action/allocation result; moving and
confirming together does not produce an optimistic success before rejection.
The native pump's existing SEH recovery retires an abandoned feedback owner.

Hover changes and wheel navigation are audible; a stationary pointer, untouched
information row, unchanged bounded draft and focus-loss cleanup remain silent.
Navigation into an information row still sounds even when Confirm has no action.
Keyboard/controller repeat guards and mouse confirmation cooldown remain intact.
Failed scalar persistence retains the editable draft. Music's existing
save-and-close action is labeled Save + Back and closes only after saving.

## Fahrenheit V2 corrections

The V2 save/resource provider was already implemented. This follow-up fixes
concrete defects found by independent review and composition tests.

The former 36-file reference check and runtime-only inventory could accept extra
compiled core sources. Verification now pins all 501 tracked source files from
official commit cdb145d93295c1c6e2bf4766fda5a12877369f54, tree
4f4faeed98a1f0c808ce3fa08366a6e23b5d6d15. The manifest's canonical SHA-256 is
7eb700c21069ea117eee5e19262fd3467f742817ffa3d820f967caafdd59a676.
The overlay verifier reverses only its explicit patches before checking the same
inventory. Unexpected core files, changed originals, injected build targets and
source-looking bin/obj directories are rejected. Actual MSBuild Compile output
confirmed that the tested nested bin/obj files would otherwise be compiled.
Only the known repository-root .git, .vs and artifacts directories are excluded.
This proves the source-tree relationship; it does not attest a compiler, NuGet
cache, arbitrary command-line build properties or arbitrary third-party mods.

Managed read failures now have one cleanup owner. The real patched save UI closes
without issuing a second native cancel after TryRead already retired its ticket.
A rejected BeginRead invalidates prior buffer provenance once; a consumed failed
EndRead is not canceled again. Native rejection of CancelRead or Abort is surfaced
to C# instead of discarded. A failed game CRC verdict retains the upstream
abort-and-return behavior; genuine I/O errors retain the upstream exception policy.
Short reads, refused cleanup, foreign-thread cancellation and later recovery are
covered. The actual ref buffer is cleared on failed reads.

The optional paired V2 provider remains required for these services. Stock V1
remains the separate restricted variant. The frame ABI, save format, registry,
checkpoint selection, font/text pairing and native CRT/Present ownership are
unchanged. No upstream publication or upstream API endorsement is implied.

## Verification

All ten selected native scripts completed with exit 0 in the immutable packet
work/fahrenheit-services/tests-b118183b. Counts below are assertions, not coverage
percentages or live acceptance. Raw logs and their recorded hashes are retained.

| Surface | Result |
|---|---:|
| Actual extracted F7/hub/Arena/S.I.N. input and outcome callbacks | 114 passed |
| Nested/threaded feedback policy and exception recovery | 13 passed |
| Native F8/settings/Workshop menu harness | 29,818 passed |
| F7 UI contracts | 59 passed |
| F7 core/source contracts and isolated runtime | 4,020 and 176 passed |
| F8 governance, persistence and source contracts | 4,630 passed |
| Native managed-I/O OFF/ON/pending-exit | 25 per mode, 75 total; no exit callback marker |
| Workshop native/cooperative save flow, Ronso OFF/ON | 48 per mode, 192 total |
| Native V1 bridge lifecycle | 35 passed |
| Native text/resource modes and language regressions | All script cases passed |
| Actual self-contained x86 CLR to final production DLL | 17 passed |
| Managed provider I/O and bridge lifecycle | 30 and 18 passed |
| Actual patched provider save/UI failure composition | 11 passed |
| Complete-source and reversible-overlay verification tests | 8 passed |

Dedicated sound tests execute the current callback bodies extracted from source,
with controlled input/storage/game-domain endpoints. They do not replace those
callbacks with reimplementations. Arena gameplay actions retain their separate
runtime tests. The full CLR test maps a private FFX image without its entrypoint;
it uses the final production DLL with a marker and independent real MinHook
provider, rather than claiming a live Fahrenheit application run.

Red/green evidence includes 129 missing F8 cues, wrong F7/FLAGS outcomes, duplicate
hub/child confirmation, bounded adjustments, missing capture errors, and the
combined navigation/information case. Provider composition reproduced five
cleanup/CRC failures before correction. Source tests rejected previously accepted
extra core/build inputs. One late sound test incorrectly assumed a reward row
had an environment override; the real catalog has no such source, so that invalid
fixture was removed. No production guard was weakened to satisfy a test.

CI's existing native menu regression includes the F8 sound cases, and build.yml
now runs the dedicated F7 audio harness and shared feedback policy as well.

## Exact candidate

Build: build_hooks.ps1 -WithPolyHook -Release. All 737 captured native inputs
matched before/after compilation and local readback. Source manifest SHA-256:
1de5db61e5b30527de7af726d7c597bf679f328eb98b9a62e3f195d0824c5512.
The PE retains the fifteen expected bridge exports and no imported peer MinHook
or Fahrenheit DLL. Existing compiler warnings remain documented in build logs.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Native x86 DLL | 4,354,048 | 92910b00a520e2e4de0360da3b17884d01168006491eda929b12fa563c8ff5b3 |
| V2 managed bridge | 19,456 | 521851b53e74cd3bf9299a6f7e7f77ed910f335a7319ce1f1b81891e94f06284 |

Paired native/bridge/provider binaries, license notices and integrity receipt:
work/fahrenheit-services/accepted-audio-v2-20260929. Complete corresponding
modified provider source: work/fahrenheit-services/audio-provider-20260929.
Native build packet: build-b33c32b0; CLR transport packet: transport-9d0e3c81.
These receipts are unkeyed integrity records, not authenticity signatures.

The initial online restore stalled. A new restore using only the already present
dependency cache succeeded; provider and bridge builds then completed. No machine
runtime installation or dependency-policy change was made to the repository.

Claim -> the implemented sounds and V2 corrections pass their named tests.
Evidence -> commands, immutable packets, hashes and paired artifacts above.
Confidence -> RT0/isolated RT1. Conflict -> no live auditory/visual/gameplay RT2,
no guarantee for arbitrary mod combinations. Next -> a separately authorized
disposable-save live matrix using the paired provider. Installed DLLs, settings,
user saves, main and upstream remain unchanged by this task.
