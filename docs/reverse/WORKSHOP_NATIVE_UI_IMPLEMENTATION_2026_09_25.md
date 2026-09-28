# Native equipment presentation — Jarvis-HOOK

The user selected the previously deferred display implementation. Enable **F8 > Reforge > Equipment Workshop** and **Native equipment details**, then restart. Both default OFF and use the startup snapshot. Four-slot pieces keep four rows; an unlocked fifth adds an empty or occupied row. Native ability names keep their language and receive individual +1..+10 suffixes, including duplicate IDs with different ranks. Field Equipment, Customize and battle weapon/armor details are implemented. Vanilla Customize receives no fifth-slot writing capability.

## Pinned evidence

PE SHA256: 78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced. Preferred base: 0x00400000. Runtime addresses use module base plus RVA. Capstone inspected the pinned bytes; NativePresentationEvidence.h checks 32-byte spans with full HIGHLOW relocation adjustment.

| Consumer | Entry RVA | Verified interface / return sites |
|---|---:|---|
| Equipment | 0x4D02B0 | cdecl, no arguments; gear getter returns 0x4D02D2, label 0x4D03C4. |
| Customize | 0x4D63C0 | cdecl, selected index; getter returns 0x4D63DD, label 0x4D64F5. |
| Battle details | 0x4F34C0 | cdecl, gear ID and float x/y; getter returns 0x4F34D3. |
| Ability label | 0x4F4F10 | cdecl, ability word, float x/y, style; definition returns 0x4F4F23. |
| Ability definition | 0x3909C0 | Returns 108-byte row plus text base; battle measure/draw return 0x4F3526 / 0x4F370B. |
| Existing gear getter | 0x3ABBF0 | Existing runtime detour delegates presentation; no competing hook. |

Field backgrounds use capacity but labels stop at four. A scoped 24-byte copy adds the fifth WORD and private capacity five, leaving native 22-byte records unchanged. Fifth label coordinates follow the original design: x970; Equipment y599+4*79; Customize y735+4*68-2. The actual battle loop handles five rows and measures ranked names itself.

ReadPresentation requires an aligned inventory address, live associated identity, exact 22-byte equality and owning thread. Battle reads never grant mutation Capture. An identical foreign record cannot borrow ranks. Private row/name copies preserve kernel and save bytes. Names terminate within 180 bytes and use native space0x3A, plus0x45, digits0x30..39. Separate measurement/draw cursors preserve duplicate ranks. TLS restores on unwind; unknown identity, caller, profile or thread uses vanilla. Stop is an asserted lock-free atomic store. Published trampolines remain pinned for process lifetime; dynamic unload is not introduced.

Fahrenheit function names at commit 3a3887a783c21f60bac8831d15ad416a8fd6e8c1, https://github.com/fahrenheit-crew/fahrenheit, src/step/data/functions.ffx.csv, guided navigation. ABI and code were independently derived from pinned bytes; no external renderer or texture was copied. The authorized WanxTitanx/ffx-extracted repository was inspected; pinned local fixtures supplied actual test inputs.

NativePresentationRt1 executes all three actual mapped detail functions with installed hooks and real sidecar/load association. Device and unrelated world/name-kernel endpoints are explicit substitutions. An unused unloaded equipment-name table caused an initial fixture crash; its getter is now substituted only in the fixture. Tests cover duplicate ranks, empty fifth, refined four-slot items, battle denial, foreign identity and OFF restoration. GPU clipping, translated-name widths and live focus/animation remain RT2 acceptance work. No game launch or deployment occurred.
