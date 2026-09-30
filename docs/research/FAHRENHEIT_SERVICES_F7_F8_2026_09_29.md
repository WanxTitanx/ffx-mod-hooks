# Jarvis-HOOK: cooperative saves, resources and F7/F8

## Result and scope

V2 save/resource integration is implemented for Fahrenheit revision
cdb145d93295c1c6e2bf4766fda5a12877369f54. It requires the optional reproducible
provider overlay in integrations/fahrenheit/provider. Stock Fahrenheit retains
the V1 path and its explicit restrictions. This is not an upstream-approved API.
The user requested implementation, F7/F8 corrections and branch publication.
No installed game/DLL/configuration/save was changed; no FFX entrypoint or RT2
session was run. Passing isolated tests does not certify arbitrary mod combinations.

## Save transport

The managed provider exposes the actual final slot/path at manual-save, autosave,
read and cancel boundaries. Bootstrap runs after Fahrenheit commits its initial
hook registry and before game initialization. Native preparation is serialized
and completes synchronously before the provider admits I/O.

V2 reuses the existing Ronso serializer, checkpoint selector, ownership store,
NativeSaveEvents registry and verified-close tracker. The native CRT observer
remains a separate transport; its three IAT cells are untouched in managed mode.
Existing feature configuration remains decisive. No duplicate save format or
independent Workshop store was introduced.

Writes snapshot immutable source bytes, apply existing projections into a
separate image, seal the checksum and prepare metadata before touching the
primary. A new same-directory temporary file is bound to its OS file identity,
written once, flushed and closed, then replaces the requested primary. Completion
requires canonical-path, identity, exact-byte, checksum, epoch and thread checks.
Only verified completion publishes observers. Failure never retries a primary
write or reports successful persistence. Ronso ownership records are staged
under existing path-and-image-hash keys; contradictory existing metadata is
rejected instead of overwritten before an I/O failure.

Reads invalidate old buffer provenance before the attempt, verify the disk
handle and exact bytes, select the existing checkpoint and transform the real
game ref buffer before CRC validation. Cancel/short reads/failed CRC abort the
generation. The verifier tolerates only the game's exact four-byte CRC-field
clearing. Existing native load-copy callsites publish actual RAM-load events;
post-read success is not falsely presented as completed RAM copying.

One nonblocking transaction owner, monotonic tickets and OS-thread binding
reject overlapping, stale and foreign-thread completions. Slot names retain
their full nonnegative Int32 identity, including slot 1000 and above.

## Resource transport

The read-only EFL constructor asks the native resource service for claimed
handles. Resolution reuses pinned package files, locale rules, source verification
and the font-publication boundary. Cooperative mode installs only the font hook;
the native OpenStream worker remains untouched.

Every paired package path is checked against Fahrenheit's replacement index
before hook publication. A conflict rejects the whole pair. Source verification
reads through the provider-owned constructor under the existing reentrancy fence.
Published text and atlas handles remain paired through logical stop. A later
resource-open failure never silently combines translated text with a vanilla atlas.

V2 adds ten exports while preserving five V1 exports, the 24-byte status layout
and V1 frame capabilities 3. managedSaveCompatible becomes 1 only after active,
negotiated save-service startup. Native CRT and Present ownership remain separate.
Fastload/autosave selection still belongs to Fahrenheit; the competing native
bootstrap shortcut remains blocked.

## F7/F8 corrections

F7 edits an eight-entry playlist, active count and fade duration as one validated
music draft. Zero fade reaches battle transitions as zero. Invalid counts and
track IDs never become array indexes. Per-area Difficulty editing supports
sixteen canonical field rules, field-ID selection, deletion, global/area scope
and enable states while preserving undisplayed fields.

Music and Difficulty commits compare the current subsystem with the editor's
expected snapshot under the existing lock. Stale edits are rejected, unrelated
settings preserved, and rejected save/apply drafts do not publish success.
Difficulty infrastructure status remains distinct from configured intent.

Fahrenheit input capture suppresses native keyboard ownership without returning
early from the complete maintenance frame. F8 uses actual producer status after
V2 negotiation, preserves signature failures and restores adapter-required status
after shutdown. V1 rejection does not persist a different preference.

## Verification ledger

Counts are assertions, not coverage percentages. Evidence paths are relative to
work/fahrenheit-services and are excluded from Git along with private fixtures.

| Surface / command | Result | Evidence |
|---|---:|---|
| f7_runtime_rt0.ps1 | 4,020 passed | tests-dce56d32/f7_runtime_rt0.ps1.log |
| f7_runtime_rt1.ps1 | 176 passed | tests-e9077778/f7_runtime_rt1.ps1.log |
| f8_runtime_rt0.ps1 | 4,629 passed | tests-29c75ff7/f8_runtime_rt0.ps1.log |
| fahrenheit_governance_rt0.ps1 | Editor 42; input 10; services 20; V1 admission 37; zero failures | tests-29c75ff7/fahrenheit_governance_rt0.ps1.log |
| fahrenheit_managed_io_rt1.ps1 | 24 OFF + 24 ON + 24 pending-exit assertions; parent confirms no teardown callback | tests-56a19c68/fahrenheit_managed_io_rt1.ps1.log |
| Self-contained CLR x86 + final native DLL + filesystem | 11 passed | transport-f3d1940f/execute.log and receipt.json |
| ProviderIoTests.csproj | 30 passed | managed test stdout |
| BridgeLifecycleTests.csproj | 18 passed | managed test stdout |
| provider/tests/test_overlay.py | Copy/original preservation and source/marker tamper rejection passed | unittest stdout |
| fahrenheit_bridge_rt1.ps1 | 35 passed | tests-e9077778/fahrenheit_bridge_rt1.ps1.log |
| Cooperative text / foreign replacement / stopped service | 38 / 6 / 38 passed | tests-e9077778/fahrenheit_text_rt1.ps1.log |
| Existing text modes, pinned files and settings | Passed | same text log |
| Native menu / Windows language regression | 29,614 / 629 passed | same text log |
| Existing Ronso native I/O | 33 ON + 27 OFF passed | tests-dce56d32/ronso_pool_io_rt1.ps1.log |
| Full Workshop save lifecycle, native and cooperative transports | 48 assertions in each of native OFF/ON and managed OFF/ON: 192 passed | tests-56a19c68/equipment_workshop_save_flow_rt1.ps1.log |

The CLR test loads a test-only Fahrenheit marker and an independent real MinHook
provider, maps the exact private FFX PE without its entrypoint and loads the full
candidate DLL. It exercises the actual C# service/client against that DLL, including
read, Save As, CRC failure and recovery. It does not run the complete Fahrenheit
application or live gameplay. Text tests use actual native stream methods with
an isolated constructor-chain fixture. These limits remain explicit.

Resolved failures included old source-test signatures after the editor refactor,
a missing test include, absent copied INI/document inputs, the Windows max macro,
an omitted resource export and inconsistent module selection during load-event
startup. The VM lacked global x86 .NET 10, so the CLR test uses a self-contained
runtime inside its owned folder. Its intentional CRC rejection initially escaped
the wrong harness exception catch; the corrected InvalidDataException case passes.

The final consumer review reproduced a real Workshop provenance mismatch:
Fahrenheit confirms the read after clearing the CRC trailer, while Workshop's
read association hashed that raw buffer and later normalized it before load.
The initial regression failed native inventory admission (6 of 7 assertions
passed). Read association and load admission now normalize the same private copy
under the same checksum proof, without editing the real buffer. The complete
existing inventory/refinement/fusion/paid-checkpoint flow now passes all 48
assertions in each of four native/cooperative and Ronso OFF/ON combinations.

The final lifecycle regression leaves an issued transaction pending until process
exit. The old global unique_ptr invoked an observer's finish callback during CRT
destruction; the parent detected its marker after the child exited. Transaction
destruction now belongs only to explicit normal-context End/Abort operations.
An operation interrupted by process termination retains its memory for the OS
instead of running I/O/callback finalization under loader lock. The parent marker
check passes, together with all 72 transport and 192 Workshop assertions.

A custom PublishDir was also propagating into the framework project reference,
copying extra framework DLLs into the stock bridge package. The project reference
now removes that global property. Both fresh V1 and V2 output directories pass
the unchanged no-extra-DLL package validator. The red package remains preserved
as historical evidence; accepted-v2-final contains only validated alternatives.

## Candidate identities

Native command: build_hooks.ps1 -WithPolyHook -Release, exit 0, three existing
C4996 warnings. All 731 captured native inputs matched before/after compilation
and at local readback. Source-manifest SHA-256:
a3b4c322b22f53d33e151f58d0619d579083eecc0538762b83477b230adbc84a.
PE inspection confirms exactly fifteen expected bridge exports.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Native x86 DLL | 4,337,664 | 058ff103e5bd0ab707c36aef792aef1b962d72fef5cb825a1a8122d0b43fde28 |
| V2 managed bridge | 18,944 | b506e0e77a778cb8924bbc0975b613edc64fddc66368dbb051e6e6ceb6c7047c |
| Stock V1 alternative bridge | 13,824 | 58abfc588f45ff260b6c216d2bfccc637fe94a61f1aa615fda3ebc5bc2378529 |
| Optional provider fh.dll | 34,123,264 | 296a71ed020bcba405a26660dffe06930874591db64c3bc9f531a50d13040bf5 |
| Optional provider fhr.dll | 87,552 | 03babf5d1b41d25a369a05423905e4e74737ea018e3c3a18270f44f4b6fe31fa |

Paired local artifacts and license/hash receipts: work/fahrenheit-services/accepted-v2-final.
Complete corresponding provider source: work/fahrenheit-services/provider-v2-source.
The stock V1 bridge is separately built in accepted-v2-final/bridge-v1. V1 and V2 are
alternative packages, not two modules to load together. Provider/bridge builds
succeeded; core rebuilds retain three existing upstream C# warnings.

## Acceptance boundary

Claim -> cooperative save/resource implementation and the named F7/F8 fixes are
implemented and exercised at RT0/isolated RT1. Evidence -> source, commands, logs
and exact artifacts above. Confidence -> bounded to those tested contracts.
Conflict -> no live visuals, gameplay/per-feature RT2 or arbitrary-mod guarantee;
fastload selection remains provider-owned. Next -> separately authorize a
disposable-save RT2 matrix with the paired provider, real RAM-load publication,
menu input/focus, font initialization, save-set changes and selected mod load order.
