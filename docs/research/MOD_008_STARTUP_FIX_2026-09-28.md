# MOD-008 startup correction — Jarvis-HOOK

## Reported failure and cause

The user reported missing Tarot rows in native Equip and `EDIT REJECTED UNAVAILABLE` for the Development full-deck option. The deployed DLL was `438f333053ff9fc30eead9ce65c0b17010a1be6a27978b50af71af1fc1fa0e9a`; its configuration enabled Arcana. The reported session recorded `runtime=1 combat=1 turn=1 ui=0 art=1; Arcana stopped`.

Arcana starts before `InstallHooks`, but its call to `StartNativeTextOutlineGuard()` depended on `g_base`, which `InstallHooks` initializes later. The guard returned false while `g_base` was still zero. The native UI start was consequently skipped, runtime admission closed, and F8 correctly rejected the unavailable live producer. The same log showed the ordinary text guard succeeding later, after global initialization. This is an initialization-order defect in the original candidate, not an acquisition rule or missing-card setting.

## Correction

Arcana now passes its already-admitted module base directly to the shared text guard. Ordinary F8 startup retains the existing global fallback and reuses an installed guard. Startup logs include the Arcana gate source and a separate `text=` result.

Arcana also publishes thread-local text-drawing ownership only while its native draw callback runs. The shared outline guard now recognizes that bounded scope, including the picker, and resumes ordinary rendering afterward. Merely enabling Arcana never suppresses vanilla outlines permanently.

## Evidence and limits

- A test compiles the production guard function and exact Arcana call expression with a substituted detour boundary. The old startup fails 3/8 checks when the legacy global is zero; the fix passes 8/8 normally and with ASan/UBSan. It checks the admitted target address, later F8 reuse and failure/retry behavior.
- Native private-PE Equip test: 38/38. Its new drawing-scope assertion failed before the scope implementation, then passed. Native controller/draw bodies execute with the fixture's documented external dependencies.
- Portable suite: all 76,417 checks pass normally and with ASan/UBSan.
- F8 governance: 3,988/3,988; Seymour 88/88; CustomMix 89/89. Context tooling: 114 tests, 14/14 retrieval cases.
- MSBuild and direct MSVC Release Win32 builds pass. The corrected candidate is 1,991,680 bytes, SHA-256 `a3e759501d5ee0850eda5d5cbebf55f0345b2297da69603eee54ba7d5dcd1f6f`; validation-only DLL loader/worker smoke passes with unchanged private PE boundaries.
- Artifact/source comparisons found no code drift. No changes to card effects, ownership, saves or acquisition rules are part of this correction.

The earlier RT1 suites exercised the native UI and runtime components separately and missed this worker-order dependency. Their pass results did not establish successful in-game startup; the user's reported failure invalidates that assumption. A fresh user-run session must confirm `text=1 ui=1`, native Tarot rows, and the accepted Development edit before claiming the live issue resolved.

Raw local evidence: `work/mod008/startup-fix-red.log`, `startup-ui-red.log`, `startup-ui-green.log`, `startup-portable*.log`, `startup-f8-regression.log`, `startup-build.log`, `startup-smoke.log`. The reported game's latest log slice is retained in the main checkout at `work/mod008-startup-fix-20260928/reported-session.log` (offset 1,789,983; 274,271 bytes; SHA-256 `8c376cfa23346c62b45e56baf22edb0aee64b473a539f2382cf86cc32a7df54f`). The game was not launched or controlled by this debugging run.
