# ffx-mod-hooks

<div align="center">

![ffx-mod-hooks logo](assets/logo.png)

**Runtime engine hook layer for FINAL FANTASY X / X-2 HD Remaster (Steam, PC)**

Part of the **FFX Mod Studio** ecosystem

🇧🇷 **Made by a Brazilian developer** — WanxTitanx (FFX Mod Studio)

[![Version](https://img.shields.io/badge/version-0.4.0--beta-informational)](https://github.com/WanxTitanx/ffx-mod-hooks/releases)
[![Status](https://img.shields.io/badge/status-BETA-red)](#status-beta)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x86-lightgrey)]()
[![Game](https://img.shields.io/badge/game-FFX%2FFFX--2%20HD%20Remaster%20(Steam)-green)]()

[Download the latest release →](https://github.com/WanxTitanx/ffx-mod-hooks/releases)

</div>

---

## Status: BETA

> **This project is in beta.** It is actively developed as part of the FFX Mod
> Studio ecosystem. Expect bugs, rough edges, and mod incompatibilities. Use a
> disposable save — never test or play a modded session on your main save file.

- Every gameplay-mutating feature is **OFF by default** and RAM-only — nothing
  writes to the game's data files, and everything reverts on restart.
- Hooks are **profile/signature-gated**: they install only on the exact
  supported `FFX.exe` build. On mismatch they stay off and log — they never
  corrupt the game.
- In-game behavior is validated through a reproducible RT2 protocol
  ([docs/RT2_PROTOCOL.md](docs/RT2_PROTOCOL.md)); source/build evidence alone is
  never claimed as working. Known gaps are listed openly in
  [docs/KNOWN_BUGS.md](docs/KNOWN_BUGS.md).

## Downloads & install

Grab the latest release from the
[Releases page](https://github.com/WanxTitanx/ffx-mod-hooks/releases).

**Install guides:**
- 🇬🇧 English: [docs/INSTALL.md](docs/INSTALL.md)
- 🇧🇷 Português: [docs/INSTALACAO_PT-BR.md](docs/INSTALACAO_PT-BR.md)

Both cover installing from a release zip, building from source, uninstalling,
and troubleshooting.

## What's inside

### F7 In-Live menu

Arm with `modules\config\f7_inlive.flag` or env `FFXHOOKS_ENABLE_F7=1`, then
press **F7** in-game.

- **Difficulty** — multiply monster stats per battle **in RAM only** (never edits
  `.bin` files): max/current HP/MP, Overkill, and
  STR/DEF/MAG/MDF/AGI/LCK/EVA/ACC, with element and status control candidates.
  Presets: Off, Hunter, Sombra de Sin, True Nightmare.
- **S.I.N. RAM** — scales the reviewed structural fields of the bounded
  natural-encounter catalog in memory, composed through the single Difficulty
  writer.
- **Force Last Battle** — captures the last natural encounter and re-triggers it
  with one click (crash-free main-thread route).
- **Music** — track lock, battle-entry override, randomizer, fade control.
- **Monster AI Observer** — read-only lifecycle/dispatch telemetry around the
  game-owned monster-script runtime; zero mutation.
- **CustomMix Ultra** — session-only symbolic picker that composes up to eight
  reviewed aeon actors through the shared InitScene path.

### F8 Dashboard (FLAGS menu)

Native in-game dashboard with **8 tabs and 36 rows**: Plugins, Boosters, Cheats,
Scout, Arena+, Input, Dev, Lab.

- **Live in-game (observed in real sessions):** Permanent Sensor, Playable
  Seymour (experimental), Speed Hack (Ctrl+Shift+K cycles 1x/2x/4x/8x — native
  2x/4x booster plus custom 8x on the reviewed field-scene tick; FMV excluded),
  Entire Party Earns AP, Invincible Party/Enemies, Always Overdrive, Always
  Critical, Damage 99999, Always Rare Drop, AP Multiplier (1–100x), Gil
  Multiplier (1–100x), Dialog Skip.
- **Restart-required (wired, arm next launch):** FieldScout Master/Heavy/Ultra,
  Arena+ Master/Victory Hook/Resolver Log/Music.
- **Lab tab:** Equipment Workshop (native equipment ability/refinement
  workshop), Nova Super Damage, Ronso Mana, Grid Teach, Kimahri Lancet Dual
  Grant, Item Stack Cap 255, Double/Triple Drop — all OFF by default.
- **Honest rows:** Plugins tab items and three Input rows are display-only;
  Arena+ Compose is quarantined and reports itself unavailable instead of
  pretending to work. External `.flag.off` markers are named explicitly and are
  never silently deleted.
- Bulk actions (`Enable Supported` / `Disable Supported`) classify per row:
  Changed / Already / Unavailable / ExternalOverride / InvalidParameter /
  EffectiveMismatch — with the blocking artifact named.

### F9 — Maechen assistant

Translucent in-game Q&A modal (default OFF) with foreground admission, bounded
protocol validation, and cross-modal ownership. Answers come from a remote
service maintained in the website repository.

### Also included (off by default)

- `ffx-probe.dll` — main-thread DINPUT8 probe (READ / WRITE / CALL, ForceBattle)
- `SinScaleInject.exe` + `SinCoreLib` — offline S.I.N. research/materialization
  tooling; the runtime hook never launches it

## Roadmap

See the complete, honest status of every feature in
[docs/ROADMAP.md](docs/ROADMAP.md) — per-tab F8 truth, what's live, what's
restart-required, what's quarantined, and what's never being ported.

## Known bugs & limits

- Force Last Battle stutters briefly (main-thread Sleep between reps).
- Several wired features still need user-run RT2 before they are claimed as
  proven — the roadmap tags every row individually.
- Arena+ Music stays blocked while `modules/arena_plus_music.flag.off` exists;
  the row names the blocking artifact instead of failing silently.
- Speed Hack: dialogue/rendered-scene coverage unproven; pre-rendered FMV is
  never accelerated. F12 remains the game's screenshot key.
- Playable Seymour: Sphere Grid unsupported; experimental, RT2-pending.
- Expect mod incompatibilities (see below).

## Compatibility

- Expect conflicts with mods that hook the same seams:
  - `IDirectInputDevice8::GetDeviceState` (vtable slot 9) — our probe seam
  - `dxgi.dll` proxies (Special K) and UnX (`unx.dll`) — UnX crashed 3/3 when
    combined with the probe and was removed from the deploy
  - Mods shipping their own `dinput8.dll` proxy loader
- Everything is RAM-only and reversible: delete the flag or restart.
- Dynamic `FreeLibrary`/hot unload is unsupported; process detach only makes
  lock-free stop requests.

## The FFX Mod Studio ecosystem

| Component | Repo | Visibility |
|---|---|---|
| Editor | `ffx-editor-main` | Private |
| Launcher | `ffx-mod-launcher` | Private |
| Launcher releases | `ffx-mod-launcher-releases` | Public |
| Website | `ffx-mod-website` | Private |
| Hooks (development) | `ffx-hooks` | Private |
| **Hooks (source + releases)** | **`ffx-mod-hooks`** | **Public** |
| Magic RE | `ffx-magic-re` | Public |

## Source

This is an open-source project — the source code lives in this repository under
`src/` (GPL-3.0, see [LICENSE](LICENSE)). The private repo
(`WanxTitanx/ffx-hooks`) is the internal development workspace; releases here
are cut from reviewed, stable snapshots of it. Build instructions:
[docs/INSTALL.md](docs/INSTALL.md).

## Credits

This project builds on open source and community work — full list in
[NOTICE](NOTICE): PolyHook2 (stevemk14ebr), MinHook (TsudaKageyu),
Zydis/Zycore (zyantific), asmjit/asmtk, Xe.BinaryMapper (Xeeynamo),
the DINPUT8 proxy concept (ffgriever). The DINPUT8 main-thread seam
(`GetDeviceState` vtable hook) was discovered and proven in-house. ATEL/monster
codecs in SinCoreLib were extracted from the FFX Mod Studio editor codebase
(our own code).

## Support

🇧🇷 This project is made by a Brazilian developer. If it helps you, consider
supporting:

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=wandersonwpires%40hotmail.com&currency_code=USD)

**Any amount helps.** Even $2 is enough to kick off project maintenance — and
this project is bigger than just the hooks. The FFX Mod Studio ecosystem
includes the editor, a launcher, a website with download links, and many more
features planned through September 2026. Your support keeps it all moving.

## License

GPL-3.0 — see [LICENSE](LICENSE). FINAL FANTASY X/X-2 HD Remaster is property of
Square Enix. This is a fan-made tooling layer — no game assets or executable
code are distributed.
