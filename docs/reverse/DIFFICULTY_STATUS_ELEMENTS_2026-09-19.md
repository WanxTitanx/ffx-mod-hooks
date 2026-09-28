# Jarvis-HOOK — Difficulty status and element evidence, 2026-09-19

## Identity and limits

- Executable: installed `FFX.exe`, SHA-256
  `78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`,
  10,675,712 bytes, PE32/I386, preferred image base `0x00400000`,
  timestamp `0x55D2F3CC`, image span `0x0237D000`.
- Addresses below are preferred VAs; RVA = VA minus `0x00400000`. Actor offsets
  are little-endian offsets inside the existing validated `0xF90` enemy record.
- Evidence came from `pefile` plus Capstone disassembly of those exact disk bytes.
  The remote IDA connector returned HTTP 404; no IDA result is claimed for this work.
- No external implementation was adapted. Local Studio `FfxLib/Memory/MemoryChr.cs`
  and `FfxLib/Common/StatusByteList.cs` were navigation hints only. Native accesses,
  complete helper bodies and isolated execution are the evidence for the mapping.
- Raw slices and hashes are under
  `.superpowers/sdd/2026-09-16-f8-development-fastload-autosave/difficulty-status-20260919/`.
  The 8141BA9D candidate was deployed under explicit authorization on 2026-09-19.
  These observations are RT0/isolated RT1,
  not a complete user-run RT2 or a Production promotion.

## Fields and consumers

| Actor field | Width | Exact native evidence |
|---|---|---|
| `+0x5DA` absorb | 1 byte | `0x0078A452` reads byte; `0x0079C8EC` ORs byte |
| `+0x5DB` ignore | 1 byte | `0x0078A448` reads byte; `0x0079C8F6` ORs byte |
| `+0x5DC` resist | 1 byte | `0x0078A43A` reads byte; `0x0079C900` ORs byte |
| `+0x5DD` weak | 1 byte | `0x0078A441` reads byte; `0x0079C90A` ORs byte |
| `+0x630` innate first group | 2 bytes | `0x0079B2C0` reads word; `0x0079C9AB` ORs word |
| `+0x632` innate duration group | 2 bytes | `0x0079B2C8` reads word; `0x0079C9B6` ORs word |
| `+0x641..+0x659` resistance | 25 individual bytes | `0x0078AF3B` reads indexed byte; `0x0079C9D0..0x0079CA01` read/write a bounded byte loop |

`0x0078A420..0x0078A834` is a position-independent native elemental damage consumer:
`int __cdecl(actor*, unused, elementMask, damage)`. For one selected element and
100 damage, weak yields 150, resist 50, absorb -100 and ignore 0. The five UI
elements use bits 0..4. Its complete 1,045-byte isolated fixture has SHA-256
`3EC1A5447679CE745684028AC1E25F4708E33F8D4A7B79B8667C6AE211EDE36F`.
It has no calls or HIGHLOW relocations. RT1 executes these exact bytes against
the production Difficulty writer's actor fixture, including OFF restoring native
immunity; it does not launch or attach to FFX.

AUTO bits 0..11 map to the lower 12 bits of `u16 +0x630`: Death, Zombie, Petrify,
Poison, PowerBreak, MagicBreak, ArmorBreak, MentalBreak, Confuse, Berserk, Provoke,
Threaten. Bits 12..24 map to bits 0..12 of `u16 +0x632`: Sleep, Silence, Darkness,
Shell, Protect, Reflect, NulTide, NulBlaze, NulShock, NulFrost, Regen, Haste, Slow.
The extra innate word `+0x634` and SOS words `+0x636/+0x638/+0x63A` are not
configuration write targets.

## Native reconciliation

Two existing `void __cdecl(actor*)` helpers form the complete refresh:

| Helper | VA / RVA | Length | SHA-256 |
|---|---|---|---|
| Remove current full AUTO contribution | `0x0079B1B0` / `0x0039B1B0` | 239 bytes | `2C20BA2E98E9B66A4A7CA8DBDA45AEA4860B8F5E38A460C5DD371B56D1623E3A` |
| Rebuild innate/SOS and apply full AUTO | `0x0079B2A0` / `0x0039B2A0` | 470 bytes | `13B115E49E95F5D87C8B3E34EA8DD9B03EBD0A24224F64D7E8814693EFED6F7E` |

Both bodies have only internal branches, no external calls, no HIGHLOW operands,
and return with plain `ret`; the caller owns the one DWORD argument.
Vanilla calls this same pair at `0x0078D6CC/0x0078D6D2`,
`0x0078E27F/0x0078E285` and `0x007AF28F/0x007AF295`.

- Remove uses old full words `+0x62A/+0x62C/+0x62E` to restore selected temporary
  effects from backup word `+0x618`, 13 bytes `+0x61A..+0x626`, and word `+0x628`.
- Apply rebuilds full masks from the three innate words and conditionally ORs SOS
  when the HP weakness level `+0x63E` is 1 or 2.
- The 12-bit loop operates on suffer word `+0x606`. The next 13-bit loop saves
  selected duration bytes `+0x608..+0x614` and sets them to `0xFF`.
- Native side effects remain native: newly gained Haste can clear finite Slow;
  newly gained Regen resets its scheduling byte `+0x6D2`.
- Difficulty changes only the two permanent words, then invokes this pair once.
  It never hand-writes transient status/backups or treats a 25-bit config mask as
  a contiguous game status DWORD.

## Admission, ownership and regression obligations

The existing exact PE identity and three-target Difficulty batch remain mandatory.
Both entire native helper bodies are checked before installation and before refresh.
Actor writes require the captured battle-owner thread, open admission, no initializer
recursion and a readable admitted post-populate/active phase. No new detour, callback
producer, DllMain work, on-disk game mutation, or new default-ON gate was added.

Neutral optional settings leave native fields alone. AUTO adds to the captured
innate masks. For selected elements, competing native affinities are cleared, with
legacy conflicting JSON masks resolved absorb > resist > weak. Status resistance
sets a minimum, preserving stronger native resistance. OFF restores only owned
fields and lets the native helper pair restore/reapply status contributions.

Both innate words pass compare-before-write admission before either changes.
Native failure, unknown write readback, or a reported-success write that reverted
to the prevalue makes AUTO failure sticky for this actor generation. No retry or
OFF recaptures uncertain native backup state. Raw failure evidence includes:

- `f7-runtime-red-vm.log`: 29 missing-writer regressions before implementation.
- `auto-write-ambiguity-red.log`: two failures reproduced the initially missed
  successful-write/no-change case. That invalidated the initial safety claim
  and led to explicit paired AUTO ownership loss on ambiguous readback.

RT0 covers all 25 bit mappings, byte/word neighbors, immutable edits, neutral
preservation, affinity precedence, resistance minimums, idempotence, OFF,
missing admission, foreign ownership and sticky native/write failures.
RT1 uses the exact helper machine code in private read/execute pages against a
bounded actor fixture, checking actual suffer/duration fields and independent
temporary effects. Full-body mutation rejection and fresh source/VM hashes bind
those tests to this candidate. Live timing, animations, every status interaction,
save integrity and the complete OFF/next-battle protocol still need separately
authorized, disposable-save RT2.
