# Contracts

Memory-mapped-file contracts shared between the runtime DLLs and the FFX Mod Studio editor
(C#). These are the **canonical public copies** of the structs.

| Header | MMF name | Size | Produced by | Consumed by |
|---|---|---|---|---|
| `ffx_hooks_block.h` | `Local\FFXHooksBlock_v1` | 256 B | ffx-hooks.dll | FFX Mod Studio editor (RuntimeDllManager) |
| `ffx_probe_block.h` | `Local\FFXProbeBlock_v1` | 580 B | ffx-probe.dll | ffxprobectl, FFX Mod Studio editor (FfxProbe_Service) |

## Sync rule

The build-time headers live with their DLLs:

- `src/runtime/FfxHooksDll/shared/ffx_hooks_block.h`
- `src/runtime/FfxDinput8Probe/ffx_probe_block.h`

When a struct changes:
1. bump the struct `version` field;
2. update BOTH the build-time header and the copy in this folder;
3. bump the repo version (PATCH minimum).

Magic values: `FFXHOOKS_MAGIC = 0x48584646 ('FFXH')`, probe magic lives in
`ffx_probe_block.h`.
