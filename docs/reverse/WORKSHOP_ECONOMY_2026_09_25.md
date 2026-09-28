# Workshop economy / comparison validation — Jarvis-HOOK

## Scope and observed state

The resumed checkout is `main` at `899c590`. Contrary to the compacted brief,
the economy, native transaction, private host, settings, and comparison changes
already existed as local work. They were preserved rather than reimplemented.
The accepted fusion/audio DLL installed when resuming was SHA-256
`df106b65cd12b8276d9faa18cf91ff61fb952c5195faae6e37f612f278ad10d7`.
No game launch, deployment, commit, push, or subagent was performed here.

Rules and adjustable defaults are documented in [WORKSHOP_ECONOMY](../WORKSHOP_ECONOMY.md).
Evidence lives in `.superpowers/sdd/2026-09-25-workshop-economy-jarvis/`.

## New finding and fix in this resumption

The core already distinguished real Customize recipes from its six mod-only
fallbacks, but native confirmation did not disclose the difference. New tests
against the existing renderer reproduced three failures: A, B, and fusion
confirmation omitted the mod-only notice (menu **124/127**).

The renderer now inspects all eligible refinement outcomes, or only the selected
fusion source abilities, and displays `Includes a mod-only recipe.`. It never
consults the hidden winner. Additional cases ensure maxed fallback abilities
and unselected fallback donor abilities do not trigger the notice. The same
menu harness then passed **127/127**. No economic price, RNG rule, effect hook,
sidecar, installed configuration, or donor-consumption routine was changed by
this disclosure fix. Exact before-images of the two edited source files are in
`resume-before/`.

## Verification actually run

| Surface | Result | Evidence and boundary |
|---|---|---|
| Shared core | 1283/1283, normal and ASan/UBSan | `resume-local-01.log`; current Linux source |
| Economy | 2660/2660, normal and ASan/UBSan | Same run; prerequisites, pricing and rank-policy cases |
| Python parser/host/store | 34/34 | Same run with private `ffx_000` fixture; no fixture skip |
| Settings | 15/15 | `resume-disclosure-green/`; MinGW Win32 under isolated Proton |
| Native menu/choice pages | 127/127 | Real menu/choice-page code, substituted device/render APIs |
| Transaction adapter | 18/18 | Extracted production ReadEconomy/Preview/Commit in private Win32 memory |
| Native recipes | 125 native, 6 mod-only | Generator `--check` matches supplied kernel bytes |

The transaction harness exercises insufficient Gil, forged prices, Gil/config
drift, successful combined debit, replay rejection, rollback before Gil, rollback
after an actual Gil write/readback failure, and all-outcome prerequisites. Its
Capture/Copy/device environment is a harness, not the running game's hooks.

The final repeated round produced the same counts in `resume-final-local.log`
and `resume-final-cross/`. `resume-final-recipes.log` records the final generator
readback. `validation-receipt.json` binds the results to
`resume-final-inputs.json` (393 files, no source drift), records harness hashes,
and confirms the installed DLL was unchanged. The receipt explicitly records
that no new MSVC DLL or deployment exists.

Commands from the repository root:

```sh
python3 research/equipment_workshop/run_checks.py \
  --save "$PWD/.superpowers/sdd/2026-09-25-workshop-economy-jarvis/fixtures/ffx_000"

python3 tools/run_workshop_ui_cross_checks.py \
  --output .superpowers/sdd/2026-09-25-workshop-economy-jarvis/resume-disclosure-green \
  --prefix .superpowers/sdd/2026-09-24-main-workshop-021558Z/isolated-proton-prefix \
  --wine '/home/wanderson/.steam/steam/steamapps/common/Proton 11.0/files/bin/wine'

python3 research/equipment_workshop/tools/generate_customize_recipes.py \
  '/mnt/nvme-samsung/FFX Extracted/FFX/ffx_ps2/ffx/master/inpc/battle/kernel/kaizou.bin' \
  --check
```

## Data identity and Gil address

The supplied `jppc` and `inpc` Customize tables have identical SHA-256
`fea70d34a8567260e5b27da19eb86234ec02c6ab1e59c774f3b799ccd15ed844`.
The private PE fixture has SHA-256
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
No new disassembly or live execution of that PE is claimed by this resumption.

`shared/ffx_addresses.h` identifies Gil as a uint32 at **RVA 0x00D307D8**.
With save RAM at RVA 0x00D2CA90 and the 64-byte file header, the corresponding
file offset is **0x3D88**, as used by the private host. This is not an inventory
item ID. The native transaction reads/writes four bytes at the admitted module
base plus that RVA. Existing runtime profile/thread/teardown gates remain.

## Limits and next gate

`ssh -o BatchMode=yes -o ConnectTimeout=8 windows11-dev-next hostname` returned
exit 255: `No route to host` for the configured VM. The VM was not started or
reconfigured. The existing failed alternate-Clang runtime log is not a build
pass and was not reused as one.

**No new MSVC Release DLL was built.** Current full native runtime/save-flow,
Ronso OFF/ON, full F7/F8 regression suites, both-platform DLL loaders, and RT2
must not be inferred from the smaller passing harnesses. Updated native tests
exist but still need the Windows build lane. Old fusion/audio candidate hashes
and old test counts are not evidence for this new source.

Review in this resumption was inline; there was no independent reviewer.
Connector `UNKNOWN_TOOL` errors were intermittent, and the requested `session`
action was absent from its catalog. The plan was recovered from the repository.
Only explicitly un-dispatched calls were retried; completed tests were read back.

Before a deployment decision, restore VM access, run the current-source MSVC
Release/native save-flow regression lane and Windows/Proton loaders, bind the
result to a fresh manifest, then obtain separate deployment authorization.
