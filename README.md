# ffx-mod-hooks

<div align="center">

**Runtime engine hook layer for FINAL FANTASY X / X-2 HD Remaster (Steam, PC)**

Part of the **FFX Mod Studio** ecosystem

🇧🇷 **Made by a Brazilian developer** — WanxTitanx (FFX Mod Studio)

[![Version](https://img.shields.io/badge/version-0.1.0--beta.1-informational)](https://github.com/WanxTitanx/ffx-mod-hooks/releases)
[![Status](https://img.shields.io/badge/status-BETA-red)](#status-beta)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x86-lightgrey)]()
[![Game](https://img.shields.io/badge/game-FFX%2FFFX--2%20HD%20Remaster%20(Steam)-green)]()

[Download the latest release →](https://github.com/WanxTitanx/ffx-mod-hooks/releases)

</div>

---

## Status: BETA

> **This project just left alpha.** Expect bugs, rough edges, and mod
> incompatibilities. Use a disposable save. All hooks are OFF by default.

## Downloads & install

Grab the latest release from the
[Releases page](https://github.com/WanxTitanx/ffx-mod-hooks/releases).

**Install guides:**
- 🇬🇧 English: [docs/INSTALL.md](docs/INSTALL.md)
- 🇧🇷 Português: [docs/INSTALACAO_PT-BR.md](docs/INSTALACAO_PT-BR.md)

Both cover installing from a release zip, building from source, uninstalling,
and troubleshooting.

## What works (beta)

### F7 In-Live menu

Arm with `modules\config\f7_inlive.flag` or env `FFXHOOKS_ENABLE_F7=1`.

- **Difficulty** — multiply monster stats per battle in RAM only (never edits
  .bin files). Presets: Off, Hunter, Sombra de Sin, True Nightmare.
- **Force Last Battle** — captures the last natural encounter and re-triggers
  it (crash-free main-thread route).
- **Music** — track lock, battle-entry override, randomizer, fade control.
- **Monster AI Swap** — per-monster, per-ability extra status effects on the
  target (JSON config, atomic writes).

### Also included (off by default)

- `ffx-probe.dll` — main-thread DINPUT8 probe (READ / WRITE / CALL, ForceBattle)
- `SinScaleInject.exe` — offline .bin injector for the SIN monster-difficulty
  system

## Roadmap

See the complete, honest status of every feature in [docs/ROADMAP.md](docs/ROADMAP.md) —
all F8 dashboard items, what's wired, what's toggle-only, what needs RE, what's never ported.

## Known bugs

See the release notes of each release for the full known-bugs list. Highlights:

- Force Last Battle stutters briefly (main-thread Sleep between reps)
- Current HP/MP not rescaled on difficulty apply
- Per-area / per-monster difficulty presets have no UI yet
- F8 Dashboard never passed a full RT2 (pending)
- Expect mod incompatibilities (vtable[9] hooks, dxgi proxies, UnX, custom
  dinput8 loaders)

## Compatibility

- **Just out of alpha** — expect conflicts with mods that hook the same seams:
  - `IDirectInputDevice8::GetDeviceState` (vtable slot 9) — our probe seam
  - `dxgi.dll` proxies (Special K) and UnX (`unx.dll`)
  - Mods shipping their own `dinput8.dll` proxy
- Hooks install only if the target byte signature matches. On mismatch, the
  hook stays off and logs — it never corrupts the game.
- Everything is RAM-only and fully reversible: delete the flag or restart.

## The FFX Mod Studio ecosystem

| Component | Repo | Visibility |
|---|---|---|
| Editor | `ffx-editor-main` | Private |
| Launcher | `ffx-mod-launcher` | Private |
| Launcher releases | `ffx-mod-launcher-releases` | Public |
| Website | `ffx-mod-website` | Private |
| Hooks (source) | `ffx-hooks` | Private (during beta) |
| **Hooks (releases)** | **`ffx-mod-hooks`** | **Public** |
| Magic RE | `ffx-magic-re` | Public |

## Source

Source code lives in a private repo (`WanxTitanx/ffx-hooks`) during beta. It
will be published when the project stabilizes. Build instructions are included
in the release notes.

## Credits

This project builds on open source and community work — full list in
[NOTICE](NOTICE): PolyHook2 (stevemk14ebr), MinHook (TsudaKageyu),
Zydis/Zycore (zyantific), asmjit/asmtk, Xe.BinaryMapper (Xeeynamo),
the DINPUT8 proxy concept (ffgriever).

## Support

🇧🇷 This project is made by a Brazilian developer. If it helps you, consider
supporting:

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue)](https://www.paypal.com/paypalme/wandersonwpires)

## License

MIT — see [LICENSE](LICENSE). FINAL FANTASY X/X-2 HD Remaster is property of
Square Enix. This is a fan-made tooling layer — no game assets or executable
code are distributed.

