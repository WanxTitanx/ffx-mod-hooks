# Minigame assist — Jarvis-HOOK

The assisted mode is implemented in `hooks/MinigameAssist*`, with two optional
F8 gates. Read the [implementation evidence](../../docs/research/MINIGAME_ASSIST_2026_10_09.md)
for behavior, executable identities and validation limits.

`build_catalog.py` reads the four original EV01 assets from an explicitly supplied
event directory. It verifies instruction boundaries and original signed-immediate
values before generating the 39-rule catalog. It does not modify game assets.

Regenerate from a privately supplied matching extraction, from the repository root:

```sh
python3 research/minigame_assist/build_catalog.py \
  --event-root /path/to/ffx/master/jppc/event/obj \
  --evidence work/minigame-assist/catalog
```

This overwrites the generated source header and writes a local provenance report.
Review any generated diff before using it; an incompatible field asset is rejected.

Reverse-engineering references used on 2026-10-09:

- Private, matching FFX executable/EV01 bytes; exact identities are in the evidence.
- The local FFXProjectEditor ATEL parser/disassembler and native format notes
  (`research_tools/Atel`, GPL-3.0 project). These tools were used for read-only
  analysis, not copied into the runtime.
- [FFX_TAS_Python](https://github.com/coderwilson/FFX_TAS_Python), GPL-3.0,
  commit `f31d2371611cbc720ea69f44d016715de55de49c`: contextual route/actor
  references only. No TAS script was imported or executed.

Portable check:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  src/runtime/FfxHooksDll/tests/MinigameAssistRt0.cpp \
  src/runtime/FfxHooksDll/hooks/MinigameAssistCore.cpp -o /tmp/minigame-assist-rt0
/tmp/minigame-assist-rt0
```

Native check: `minigame_assist_rt1.ps1` maps a private PE without its entrypoint.
The bounded runner accepts `--minigame-fixtures` for four private `.atel.bin`
inputs and `--remote-root` for the disposable Windows build directory.

The four fixtures are `mcfr0100.atel.bin`, `mcfr0200.atel.bin`,
`nagi0000.atel.bin` and `lmyt0000.atel.bin`. Each contains the ATEL blob beginning
at byte `0x40` of its verified EV01 file, including its header and constant pools.
They are private test inputs and must not be committed.

Example for the current Steam profile:

```sh
python3 tools/run_mod_runtime_checks.py \
  --windows-host WINDOWS_SSH_HOST --remote-root V:/VMTasks \
  --test minigame_assist_rt1.ps1 --label minigame-assist-current \
  --steam-profile --executable-fixture /private/FFX.exe \
  --ability-kernel /private/a_ability.bin \
  --minigame-fixtures /private/minigame-fixtures
```

For the legacy profile, omit both `--steam-profile` and `--executable-fixture`.
Use `--fixture-root` for the Windows directory containing the matching legacy PE
and native save, and `--dependency-root` for its native dependencies. The runner
packages only the explicitly selected fixture files
with hashes; it does not deploy the DLL or launch the game. Run
`f8_runtime_rt0.ps1` through the same runner for the affected F8 governance checks.
Build the complete DLL with `build_hooks.ps1 -WithPolyHook -Release -Steam20261001`
in an MSVC x86 environment. These checks do not replace live gameplay acceptance.
