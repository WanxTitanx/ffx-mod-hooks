# Jarvis-HOOK: ordered DLL languages and Fahrenheit integration

## Authorization and boundaries

The user authorized language -> DLL, Fahrenheit -> DLL, and finally DLL -> main
pull requests and merges. This supersedes earlier source-merge restrictions,
not installation, live-game, release, upstream publication or save permissions.
The main baseline is `3af0291e19af90a9ddad1c35306dba77544405d5`.

The older DLL integration is already an ancestor of main. Its unrelated dirty
Grid8 worktree stays untouched. The new DLL integration branch is
`jarvis/dll-integration-20260929`, created from the selected main baseline.
Language PR #25 uses an isolated, validated snapshot of the original uncommitted
`feat/dll-multilanguage` work; that original worktree/index was not changed.
The old game-text PT-BR PR #13 is not duplicated.

## Composition

The language branch supplies F8 Unicode captions, nine UI locales and independent
`language.ui_locale` persistence. The Fahrenheit branch supplies the paired V2
provider, protected frame ownership, save/resource transactions, and actual-
outcome F7/F8 feedback. Non-FLAGS F7 pages retain native English.

Four content conflicts were reconciled rather than taking entire files from one
side. The build retains both `/utf-8` and `FFXHOOKS_COEXISTENCE`; CI runs both
audio and UI-language suites. Focus loss clears captions before peer input
early-outs. Resource release preserves Fahrenheit's ownership gate. The native
draw wrapper reports whether drawing occurred while preserving the peer context
state in `__finally`; Unicode readiness is never advertised for a skipped draw.
The native Present shim and borrowed Fahrenheit frame callback remain distinct.

The interface-language action participates in the existing sound-outcome path:
successful save = 1, refused save = 3, Back = 4. Failed persistence retains the
old locale and its page. The F8 sound sweep now includes InterfaceLanguage and
explicitly exercises all nine choices, failed persistence and recovery.

## Verified local evidence

All eleven selected scripts completed with exit 0 in
`work/fahrenheit-services/tests-cfee987c`: UI languages, F7 audio, native
settings/Workshop, F7 UI/core/runtime, F8 runtime, Fahrenheit managed I/O, Workshop
save flow, bridge lifecycle and complete text/resource regressions. Highlights:
29,833 menu assertions; F7 audio114; arbitration13; F8 runtime4,630.

The first hosted language run caught a wall-clock-sensitive GDI test: two frame
captures expired after slow rasterization. The isolated fixture now uses an
explicit clock and separately advances it past the real 250 ms expiry boundary.
No product timeout, safety predicate or assertion was removed. The corrected UI
script passed in `tests-9c41a794`, including324 renderer assertions.
Language catalog parity:485 entries x9 locales, twelve Python tests passed.
Provider verifier8, managed I/O30, provider save/UI composition11, and bridge
lifecycle18 also passed. Portable locale/caption/format tests passed GCC ASan
and UBSan. Context tests114 passed on the language snapshot.

The composed full DLL was built with MSVC x86 Release+PolyHook in
`work/fahrenheit-services/build-8ee6cef1`. All750 captured native inputs matched
before/after the build and after copyback. Source manifest SHA-256:
`d2b346ca7ca67a5e5171319a38b515fc7a0a60b9d347960ce860d46a39c8fecc`.

Native DLL:4,683,264 bytes, SHA-256
`4e7dfe943e9496729b69c9c3af24dfef465ec4efddfb93dba5b6fdcd77af8a6e`.
It retains all15 Fahrenheit exports and imports no peer Fahrenheit/MinHook DLL.
The matching V2 bridge remains19,456 bytes, SHA-256
`521851b53e74cd3bf9299a6f7e7f77ed910f335a7319ce1f1b81891e94f06284`.
The optional paired provider remains required.

These results establish the named source/build and isolated runtime checks, not
live audio/visual/gameplay acceptance or compatibility with arbitrary mods.
No installed DLL, user save, game setting, public release or provider upstream
was modified. Hosted checks and actual PR merge receipts are recorded in each
PR conversation; local success is not presented as hosted success.
