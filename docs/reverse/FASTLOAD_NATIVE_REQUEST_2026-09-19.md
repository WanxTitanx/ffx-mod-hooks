# Jarvis-HOOK — native Fastload request correction

This is an offline implementation contract, not RT2 acceptance or deployment authorization.

## Identity and evidence

FFX.exe SHA-256 `78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED`, PE32/I386, timestamp `0x55D2F3CC`, SizeOfImage `0x0237D000`, preferred base `0x00400000`. All addresses below are preferred VAs; production uses the corresponding RVAs and checks relocated operands. Evidence is raw PE disassembly plus the user session preserved in `second-failure-20260919` under the ignored SDD evidence directory. No external implementation was copied.

The corrected manual-load trace records scene `23 -> 348 -> 23 -> 164`, opening wait byte 1, screen `0 -> 1 -> 2 -> 3 -> 4 -> 0`, UI `0 -> 11 -> 12 -> 13 -> 14 -> 15 -> 17 -> 10`, and slot/page/row 0. Scene is a WORD at `0x0112CA90`; adjacent WORDs are not part of its ID. At settled scene 23, the pending-command fields are `-1 / 1`, not `0 / 0`. The trace proves ordering/values, not the exact function calls or save integrity. No pre-run save hash was captured, so it is not a formal RT2 PASS.

## Native admission

`0x00821870`, cdecl with one 32-bit command argument (return ignored), is the native menu command dispatcher. Command **`0x40100000`** is Load; `0x40200000` is Save and must never be used by Fastload. With screen state 0, the Load branch performs:

1. `0x006482B0()` checks screen state == 0.
2. `0x00648890(1)` writes screen state 1. Its unrelated state-0 reset branch is not taken.
3. `0x008B5570()` clears DWORD `0x01866384`.
4. Tail call `0x00648860(0)` explicitly selects load direction 0.

The adapter calls this dispatcher once after fresh idle proof. It does not duplicate its writes or call the UI initializer. A trailing sample must show screen 1, direction 0 and otherwise unchanged title/selection/command fields. The validated branch is straight-line scalar work: no file I/O, allocation, logging, wait or callback. A stop after admission cannot roll back vanilla state. A fault or drift becomes a visible fallback, with no compensating writes to unknown state.

Pin the full 127-byte dispatcher and full bodies of its four callees (12/35/11/13 bytes), including all absolute relocations. Validate them before installation and immediately before native request. The older research label `FFX_Title_LoadGame` at `0x00901280` is contradicted by raw code (numeric glyph rendering); never call it as a load function. No evidence supports the plan's speculative modular menu ID 3, so it is removed from the implementation contract.

## Callback admission and transaction ownership

- The exclusive inner tick remains `0x00820090`, cdecl void, original called exactly once. No outer tick detour is added.
- Opening loader `0x00657B60` is thiscall void. After original, byte `0x00CCB9C2 == 1` proves readiness. EAX is irrelevant. Optional opening skip calls validated `0x006525B0`, cdecl void, which only clears that byte. The title flow at `0x00641870` retains its fallback initialization when the wait loop is skipped.
- Apply is admitted only after both existing hooks are ready, on their latched callback thread, with a nonblocking producer guard. Stop, terminal phase, foreign thread, changed code or unreadable sample prevents a new effect.
- The newer first-ready-title candidate supersedes the original conservative 348 roundtrip guard. At request: scene 23, no controlled character, inner tick entered, no pending scene transition, screen/dialog/direction/selectLoad 0, UI 0 or 10, command fields -1/1. Exact caller/outer-Load-gate bytes prove that native Load bypasses ordinary field processing before the intro sequence. See [the early-start evidence](EARLY_FASTLOAD_2026-09-19.md). Unknown/busy startup state still waits or falls back at the deadline.
- After native admission, wait for screen 2 / UI 12 on scene 23, dialog/direction/selectLoad 0, slot/page/row exactly 0. Freshly revalidate those fields and compare-exchange the aligned UI DWORD **12 -> 14** once. Never select a manual save, write a save file, bypass checksum/language validation, hydrate directly, or roll back after state 14. Vanilla keeps its own read, wait, validation, close and field-transition flow.
- The immutable default-OFF/restart-required gate is unchanged. `FFXHOOKS_FASTLOAD_OBSERVE_ONLY=1` explicitly disables every action. Missing opening callback degrades skip but can still permit load after the full title sequence. Shift before request bypasses; deadlines and invalid UI 16 leave vanilla visible. No callback performs file/config I/O, formatting, allocation or Sleep.

## Acceptance boundary

Offline tests must exercise both the policy and the actual injected action executor, including fresh-read conflicts, stop at action boundaries, changed signatures, missing slot 0, nonzero page/row, one native request, one handoff, no post-handoff rollback, observer zero-effects, and terminal original-call forwarding. Build and isolated DLL loading are necessary but do not prove game behavior.

The next candidate needs separate explicit deployment and RT2 authorization. Before that live run, capture exact executable/DLL/config/save hashes and a fresh log boundary; require no-input startup to playable field, ordered action/result anchors, no trace loss or timeout, and unchanged save bytes before natural autosave. The user subsequently confirmed Difficulty behavior on 8141BA9D; its source remains unchanged by the early-start Fastload repair.
