# Holy / Shadow weapon VFX — Jarvis-HOOK

User-authorized implementation; isolated branch `codex/holy-shadow-vfx-20260929`
from `97eefd445758a2de8f9a65d834113f60693be482`. The shared Hooks checkout has an
active element-name/built-in-element lane; its dirty files are not inputs here.

## Contract

- Vanilla behavior and the native four weapon-aura handles remain unchanged.
- Holy (element bit `0x10`) and Shadow (elemental Darkness bit `0x80`) receive
  separate optional visuals. Existing blindness Darkstrike keeps its identity.
- New runtime code starts OFF, admits only the verified executable/resource,
  runs on the native battle thread, and owns its extra state/resources.
- No game installation, game launch, live RT2, or public binary release is authorized by
  this implementation request. Offline/native harnesses are the validation path.

## Execution

1. Trace effect bytecode, rendering colors/resources, and lifecycle in the exact
   native producer and battle effect asset. Prove ABI and offsets before use.
2. Create distinct Holy/Shadow visual programs/resources and a bounded runtime
   adapter that leaves native slots and assets intact.
3. Exercise native code/resource paths and failure/cleanup controls in isolated
   tests; build the complete x86 DLL with exact source binding.
4. Record implementation, checks, artifact identities, and remaining live gates.

## Inputs

- PE32/i386 image base `0x400000`; FFX.exe SHA-256
  `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
- Battle effect member `ffx_ps2/ffx/master/jppc/effect/et_battle/et_battle.bin`,
  SHA-256 `1f4ebc789a7815c6a9162bc921b30ecd58a8f262733d47082d77b718769656e2`.
- Prior Editor research commit `1afea907eb4c9aeff8870b40cf95631c7eab2c7d` is a
  static map. Its `0x208F` emitter-relative link is explicitly a candidate and
  must not be promoted to a resource writer without tracing its native consumer.

## Implemented behavior

F8 → Extras → Additional mods → **Holy / Shadow weapon effects** is an independent
restart-required gate. `weapon_strike_vfx.enabled=0` is present in both default
configuration templates. The option adds visuals to existing weapon element
bits; it does not grant an element, ability, card, or equipment item. Scope is the
seven human party characters (actor IDs 0–6). Aeon/monster visuals are not claimed.

| Visual | Bit | Resident animation | RGBA (native neutral RGB = 128) | Lifetime | Emission interval |
| --- | --- | --- | --- | --- | --- |
| Holystrike | `0x10` | AN2 4, `KIRA_1` sparkle | 128 / 110 / 52 / 112 | 24 ticks | 2 ticks |
| Shadowstrike | `0x80` | AN2 1, `SMOKE_1` | 70 / 32 / 128 / 96 | 20 ticks | 3 ticks |

Holy uses smaller shrinking particles; Shadow uses expanding particles. They
have opposite native Y velocities and rotation. Both fade alpha by 4 per tick,
without wrapping, and terminate explicitly. No texture or game archive is
rewritten or redistributed. The resident effect texture atlas was decoded and
inspected offline; it contains grayscale glow/smoke shapes suitable for tinting.
That inspection is not a screenshot of the new effects running in the game.

## Native evidence

The MCP endpoints were unavailable. Local IDA 9.2 idalib on `windows11-dev-next`
opened an exclusively owned copy with auto-analysis disabled and closed without
saving. Its input hash and image base match the PE above. The original IDB hash
was verified unchanged: `bab0b5b213ff01eabd5ca5e2ea8950b8766f79887a990462d44ca3aa2ddb368a`.
Misleading old function names were treated as historical labels, not semantics;
the original database was not renamed or modified.

All addresses below are **RVAs**, not file offsets:

| RVA | Proved contract used by this implementation |
| --- | --- |
| `0x39ED60` | Native producer, cdecl `(actorIndex, actorPointer) -> int`; its original result and four slots are preserved |
| `0x393B98`, `0x3A726A`, `0x3B7D70` | Return addresses of admitted native producer callers; the first admitted call binds the owner thread |
| `0x3FD710` | Synchronous effect registration; no bytecode execution occurs inside registration |
| `0x41BCD0` | OPU `0x8F`: type-9 ring constructor and emitter; a signed relative particle-script pointer and selector `-256` target SELF |
| `0x404BB0` | `0x9099` consumes **eight bytes**: interpolation field `0x3C` and weapon endpoints `0x800E`, `0x8001`; those endpoints are not separate opcodes |
| `0x410D40` | Particle `0x0B` writes packed RGBA at particle `+0x1C`; `0x100B` writes per-tick delta at `+0x2C` |
| `0x410CD0` | Particle `0x09` sets size/size delta/angle/angle delta using its high bits |
| `0x412723..0x4127AD` | Native motion, size and RGBA integration, including the copy into the draw context |
| `0x3FF1F0` | Native arena initialization; the primary arena has 393,216 bytes and the record pool has 512 entries |
| `0x3FF6A0` | Native allocator; deliberately faults on exhaustion, so the adapter checks its linked list and metadata/data gap first |
| `0x400C80` | Side-pass registration; the adapter checks list capacity before inserting its new record |
| `0x3FC370` | Sets native stop bit `0x4000` on the owned effect |
| `0x40AF20` | Cancels the record's child scripts; followed by the native 32-tick drain |
| `0x4013E0`, `0x3FF0F0` | Type-9 teardown releases cached particle textures and the native arena allocation |
| `0x3FB090` | Battle effect cleanup; an independent hook invalidates the generation before calling the original once |

The authored programs use the native weapon interpolation and resource
association, then emit into their **own** 16-particle rings. Each ring is 1,392
bytes. They do not use the shared vanilla emitter ID `0x20`, the four actor aura
slots at `+0xE90..+0xE9F`, or particle opcodes `0x18/0x1018` that alter the native
weapon particle counter. Their constructor was compared byte-for-byte with the
real x86 type-9 constructor under emulation.

Registration temporarily associates a new record with resident program 60;
before returning to the VM, the adapter publishes its own authored program and
its checked ring. It never patches that resident program. The native sequence
WORD at actor `+0xE28` supplies collision-checked effect keys, as for other native
visuals; it is volatile runtime state, not a saved equipment field.

Admission checks the supported executable and native instruction spans, actor
table/identity, native caller and thread, relocated resident resource pointers,
and SHA-256 `7060a7e6396af923d859a3e32784ca70d91845aa327ef310153745e888815a45`
of the immutable battle bytecode at resource `+0x140`, length `0x1BB0`. This is a
runtime bytecode check, not a claim that the entire relocated resource still has
its original file hash. Unsupported resources and unavailable capacity leave the
extra effects inactive. Per-frame ownership checks use the retained slot index;
retirement additionally checks key uniqueness before invoking native stop.

## Validation checkpoint before base refresh

- Portable C++ core: **1,297 checks** (all 256 masks, OFF, independent selection,
  replacement/drain/failure handling, heap bounds/cycles/metadata overlap).
- Exact native x86 emulation: both programs, constructor parity, SELF emission,
  child cancellation, 32-tick drain, RGBA/motion/size integration, finite lifetime
  and particle canaries passed. Resource pointers are fixture inputs; no graphics
  device or game is executed by this verifier.
- Windows x86 RT1: **11/11** cases; OFF, validate-only, signatures, foreign
  resource, exhausted/read-only memory, foreign caller/thread, native four-aura
  equivalence, blindness separation, repeated battle generation and teardown.
  Registration/graphics and the battle-cleanup boundary are fixture callbacks;
  the real native producer, allocator, stop routine, side-list insertion and
  MinHook detours execute inside the isolated process.
- The **same** RT1 executable passed **11/11 on Proton 10.0**.
- F8: **4,608 checks**; native menu/settings: **29,558/29,558**.
- Complete x86 Release DLL built. Only the three existing C4996 diagnostics
  (`fscanf`/`fopen`) appeared. All **613** transported inputs were hash-verified
  before and after the build; no tracked runtime C/C++ input was omitted.
- Checkpoint DLL: 3,409,408 bytes, SHA-256
  `b21e105f1f9547482c3a3a2617b860ed8aa9a113f513c07c33bf0c559e675094`.

Private evidence is in `work/holy-shadow/` and
`.superpowers/mod007/holy-shadow-verified-ffx-mod007-6425eced72a5/`.
The shared `main` advanced to `5cfe3c2` during this work; this checkpoint predates
that base refresh and is not an installation candidate for replacing its newer
features. Final combined evidence is recorded below when available.

Live appearance, game/session lifecycle, save/reload gameplay and RT2 acceptance
remain unverified. No DLL was installed and no game process was launched.

## Current-main integration

The feature branch incorporates `main@5cfe3c2`, including built-in elements,
editable element names, and the current CI policy. The only merge conflict was
the exact detach-body test string: both the VFX atomic stop and the element-name
input abort are retained. The shared checkout and its installed DLL are intact.

Combined Windows validation passed: VFX **11/11**, F8 **4,611** checks, menu/settings
**29,587/29,587**, and all 15 Elemental Core groups including the **56/56** built-in
element checks. Complete x86 Release build passed with the same three existing
C4996 warnings. All **620** transported files were verified before and after the
build, and current local inputs match the packet. The final Windows RT1 executable
(`294e40b4b573fe176d3ededeb2328ec82a431de1af316697784b4aed1f036e6b`) also passed
**11/11 on Proton 10.0**. Readback and artifact hashes are retained with delivery.

Final combined DLL: **3,420,672 bytes**, SHA-256
`7cda5bb76128a45e6063da0c6bb238d1c78528c461b67858c1e65d1ec7cedbbd`.
Private artifact: `work/holy-shadow/delivery/ffx-hooks-holy-shadow-candidate.dll`.
Evidence: `.superpowers/mod007/holy-shadow-current-main-ffx-mod007-ecb0ef2b2346/`.
This supersedes the pre-refresh checkpoint. It remains a candidate, not an
installed DLL, a public release, or live RT2 acceptance.

## Subsequent authorized installation

The user then explicitly requested installation for their own test. On
2026-09-29 at 06:52:01 UTC, the combined candidate was atomically installed at
`<game>/modules/ffx-hooks.dll`. Source, temporary copy, destination and backup
hashes were verified. Both the preflight and immediate replacement checks found
no game process; read-only tools taking `FFX.exe` as an input were distinguished
from a launched game image.

The previous DLL hash was
`f4302d6770aeb8b190a9fde767bc80c6a547ad89103802b3c226409b44dadf0b`.
Backup, receipt and full before/after inventories are preserved under
`work/holy-shadow/deployment-20260929T065158Z/`. All **948 protected files** were
unchanged. Existing configuration was not edited: Holy/Shadow VFX remains OFF
by default. The agent did not launch the game or perform/accept live RT2.
