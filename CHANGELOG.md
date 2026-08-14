# Changelog

All notable changes to ffx-hooks are documented here. The project uses independent SemVer
(`MAJOR.MINOR.PATCH[-pre]`) and will stay below `1.0.0` while in beta. Versioning rules:
MINOR = new capability; PATCH = fix/reuse/polish; REVISION (4th component, doc-only) = notes
that do not change behavior.

## [0.1.0-beta.1] — 2026-08-13

First independent release. The runtime hook layer leaves the FFX Mod Studio monorepo
(`RuntimeTools/FfxHooksDll`, `FfxDinput8Probe`, `SinCoreLib`, `SinScaleInject`,
`NativeMenuShell`, `BattlePhotoMode`) and becomes its own repository.

### Functional (beta)

- F7 In-Live menu: RAM-only difficulty levers, Force Last Battle, battle music control,
  Monster AI Swap (per-monster/per-ability status injection, JSON config).
- ffx-probe: main-thread DINPUT8 probe (READ/WRITE/CALL, ForceBattle opcode), proven in-game.
- F8 Dashboard: implemented, disabled by default (`[dashboard] enabled=0`) — next release
  candidate. Includes safe UnX-style ports (dialog skip, boosters, debug-flag cheats).
- SIN: SinCurseHook (area-transition detection) + SinScaleInject (.bin injector, UNI 1-4).
- Lab hooks (gated off): NovaSuperDamage clamp, RonsoMana, NulWard, GridTeach, Kimahri
  Lancet dual grant, ItemStackCap 255, Double/Triple Drop, ElementHook (Scan Holy/Dark).

### Notes

- Known bugs: see [docs/KNOWN_BUGS.md](docs/KNOWN_BUGS.md) (K-01..K-24).
- All hooks OFF by default; disposable saves required; expect mod incompatibilities.
- FFX Mod Studio (editor/launcher/site) integration is on the roadmap.
