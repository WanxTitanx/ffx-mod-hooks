# Aeon Equipment Workshop

Jarvis-HOOK — 2026-09-27. Implementation and isolated validation; no installation,
installed INI/save changes, game launch or RT2/Production promotion.

## Rules

The Workshop edits existing canonical weapon/armor records of acquired Aeons,
including equipped gear. It does not create replacements, change owners/models,
unhide equipment in unrelated menus, or permit editing during battle.

| Owner | Aeon | Additional acquisition requirement | Applied-Crest bit |
|---:|---|---|---:|
| 8 | Valefor | Nirvana with its Crest applied | 0x02 |
| 9 | Ifrit | World Champion with its Crest applied | 0x10 |
| 10 | Ixion | Spirit Lance with its Crest applied | 0x08 |
| 11 | Shiva | Onion Knight with its Crest applied | 0x20 |
| 12 | Bahamut | None beyond acquisition | none |
| 13 | Anima | None beyond acquisition | none |
| 14 | Yojimbo | Masamune with its Crest applied | 0x04 |
| 15/16/17 | Cindy / Sandy / Mindy | Acquisition of the native party record | none |

The five linked Aeons require the applied Crest, not merely the weapon, a Crest
item, equipping the weapon, or a full-power flag alone. For inherently uncapped
Aeons, acquisition-only is the mod's explicit policy for their lack of a native
Celestial link; no Tidus/Rikku association is invented. Planet names vary by
release, so the requirements identify the weapon and its applied Crest.

Fifth unlock requires four open native slots and four each of five DISTINCT
sphere types: Attribute, Special, Skill, Wht Magic and Blk Magic. Master replaces
the cumulative shortage 1:1, including twenty Masters for the whole recipe.
Repeated character owners of one sphere type do not duplicate its requirement.
Fifth placement still requires all four native abilities filled. The existing
1.5x material recipe remains unchanged.

All allowed Aeon operations double their normal Gil price ONCE in the shared
plan, before affordability/debit: evolution 50,000; fifth placement 400,000;
Fusion twice the configured per-ability price; progressive refinement doubled.
Zero-Gil operations remain zero. General material costs are not doubled.
Explicit developer resource exemptions retain the existing ownership rules.

## Permanent immunity and transactions

Native word 0x807B is Aeon Immunity, distinct from Ribbon 0x8080. Core guards
protect it in any native slot and a legacy fifth slot. Clear, evolution, Fusion
replacement and fifth replacement cannot erase it. Refinement excludes it from
random and whole-item outcomes. Protected words and identities are verified
again after mutation. Aeons cannot be retired, reforged or destroyed as Fusion
donors, even if an untrusted request marks one unequipped. Normal compatible
items can donate native abilities; Fusion still cannot populate a fifth slot.

Missing weapon immunity, unknown progression bits, wrong saved equipment index,
mismatched owner or invalid flags fail closed for Aeon editing, without silently
repairing bytes or spending resources. Preview and Commit read current native
progression and canonical identities. Commit recomputes and compares the entire
reviewed plan; resource, policy or progression drift invalidates confirmation.
The existing readback/rollback journal remains the native writer boundary.

Persistent Piece=80 bytes, State=16260 bytes and sidecar v1 are unchanged.
Transient ABI4 adds AeonProgress=28 bytes: Economy=76 and Plan=16800 bytes.
The obsolete ABI3 entry rejects without writing to its smaller caller buffer.

## Native evidence and confidence

Exact PE32/I386, preferred image base 0x00400000, SHA-256:
`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`.
The save payload starts after a 64-byte file header.

| Claim | Evidence/command | Confidence / conflict / next step |
|---|---|---|
| Acquisition bit | Preferred VA 0x785460 reads BYTE [owner*0x94+0x1132088], shifts 4, masks 1; private probe.py 0x785400:0xB0 | Exact RT0 bytes; do not infer another executable build. |
| Party and equipment | Save RAM RVA 0xD2CA90; payload party 0x55CC, stride 0x94; flags BYTE +0x2C, equipped slots BYTE +0x2D/+0x2E | All-owner/kind core tests and native runtime fixture; canonical identity rechecked on confirmation. |
| Distinct upgrade stages | mcfr0100.ebp declarations resolve to payload BYTE 0xC39 (obtained), 0xC6C (Crest), 0xC6D (Sigil) | RT0 extracted ATEL; runtime reads 0xC6C, never substitutes C39/C6D. |
| Persistent Crest bits | Code-relative 0x864D/0x8654 and 0x9ECA/0x9ED1 read/OR/write Crest bits 1 and 2; other weapon branches use their corresponding masks | Sigil branches OR the separate full-power variable rather than clearing Crest state. |
| Five links | ATEL call 0x0215: human/Aeon 1/8, 2/14, 3/10, 4/9, 5/11, level 1 | Matches public gameplay research; Tidus/Rikku have no Aeon call. |
| Actual producer | VA 0x8C3150 / RVA 0x4C3150 dispatches through owner 17; writes WORDs +0x0E/+0x10/+0x12/+0x14 at VA 0x8C3230..0x8C3248 | Exact RT0 bytes and real producer execution in RT1. Level 0 is not a valid upgrade table. |
| Exact gear pointer | Native legend lookup returns from Gear at RVA 0x4C30F2 | Observe actual inventory identity, not a guessed replacement. |
| Real immunity | Aeon level-1/2 tables contain 0x807B; real producer/aggregator fixture asserts preservation | Source naming corroborates the native word; global kernel stays unchanged. |

ATEL SHA-256:
`63466cb214dfb0bc4bc44d719cd42b49a007f201078986ddbb72d5157cd12fa3`.
Code section is +0x1AA8 relative to its ATEL block. Example code-relative call
pairs: Yuna/Valefor 0x8359/0x8362; Auron/Yojimbo 0x839C/0x83A5;
Kimahri/Ixion 0x83DF/0x83E8; Wakka/Ifrit 0x8422/0x842B;
Lulu/Shiva 0x8465/0x846E. These repeat in other altar branches.

The profile-gated legend observer calls the original once, reconciles its actual
four-word result, retains identities/ranks for unchanged abilities and the fifth
slot, and quarantines an unsupported immunity-losing transition. Paid edits
refresh the equipped Aeon's native field calculation. Field/battle effects use
private extended gear/kernel views; native records remain 22 bytes. Protect and
Shell refinement recognizes verified Aeon identities without admitting Seymour.

## Validation and limits

Local normal and ASan/UBSan groups pass, including Aeon matrix 169/169, plus all
45 Python tests. These cover owner/kind/Crest combinations, canonical equipment,
Master substitution, costs, immutable ability positions, old ABI buffers,
controller persistence and stale confirmation.

The first full packet `aeon-final-full-1764526a` built successfully; all 17 normal
Proton cases passed. Its additional Aeon invocation passed 40/40 in
`resume-verification-14a267ab02/aeon-extra-12a3f38f`.

A follow-up renderer test reproduced an incorrect mod-only-recipe disclosure
for permanent immunity: RED 11832/11833, then GREEN 11833/11833 after excluding
protected outcomes from the disclosure. Actual charges already excluded it.
Private packets: `aeon-permanent-label-red-82c3c889` and
`aeon-permanent-label-green-693ae346`. Release-independent Crest labels also
had a five-failure RED before the final 169/169 core result. Final post-follow-up
candidate identities and full validation are appended after completion.

Existing Windows C4996 warnings remain at dllmain.cpp:217 and
FieldScoutHook.cpp:1603/1619. RT1 runs privately loaded bytes with real
field/aggregate/legend consumers and bounded substitutes for unloaded graphics,
growth tables and selected damage setup. It is not live GPU/gameplay acceptance,
RT2 or Production. Proprietary PE/kernel/ATEL/save data and candidates stay out
of Git. The installed DLL was not changed.

## Research provenance

Consulted 2026-09-27:
- First-hand gameplay and release-name comparison: https://guides.flactem.com/final-fantasy-x/celestial-weapons/
- Fahrenheit commit c3feefda9534edc3beb112e998a17b8ebc0707e3, savedata.cs,
  ids/ply_save.cs and ids/a_ability.cs:
  https://github.com/fahrenheit-crew/fahrenheit/tree/c3feefda9534edc3beb112e998a17b8ebc0707e3/src/core/ffx
- FFXDataParser call-name reference, commit 6e86fe15a81a2f3410513161ac50509d0c963cb8:
  https://github.com/Karifean/FFXDataParser/blob/6e86fe15a81a2f3410513161ac50509d0c963cb8/src/main/java/atel/model/ScriptCallTargetLib.java

These are informational cross-checks of independently inspected bytes/data,
not copied implementations. Fahrenheit declares LGPL-3.0-or-later; no external
source code was adapted into this patch.


## Final candidate receipt

- Full MSVC packet: `vm-native-coverage-aeon-release-final-918d30c6`, exit 0,
  437 verified inputs, 17 recovered artifacts.
- DLL: `27e301aa843f1859e8473988ae905d2155b32fe5c3594839e218918f10dc1740`, 1876992 bytes.
- Proton: `resume-verification-bd118f83a5`, all 17 ordinary cases passed, plus
  `aeon-extra-67fe1f1e` with native Aeon runtime 40/40.
- Final menu11833/11833; Aeon core169/169 normal/sanitized; all 45 Python tests.
  The two presentation regressions have recorded RED/GREEN results.
- Review was inline, not independent. Scan source and installed files unchanged.
  Player RT2/visual acceptance and deployment remain separate and unperformed.
