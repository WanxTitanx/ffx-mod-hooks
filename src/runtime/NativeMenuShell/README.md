# NativeMenuShell — native in-game menu runtime surface

Status: **active shared header in PolyHook builds**. It is no longer a standalone reference
skeleton. The current compilation surface includes it from:

- `src/runtime/FfxHooksDll/dllmain.cpp`
- `src/runtime/FfxHooksDll/hooks/ArenaPlusComposePick.cpp`
- `src/runtime/FfxHooksDll/hooks/MaechenHook.cpp`

The header supplies x86 FFX menu ABIs, address resolution, drawing and font helpers, input/modal
helpers, and menu-object lifecycle utilities. Each translation unit receives its own internal-linkage
`static` state; a caller must not assume that one consumer's menu state is shared with another.

## What it provides

`NativeMenuShell.h` renders DLL-owned row text through FFX's native font, cursor, window, sound,
color, and texture primitives. It is not an overlay. Arbitrary text requires an in-process draw
callback, which is the part an external memory probe cannot provide.

The primary F7 wire in `dllmain.cpp` uses the decoupled `PhotoModeBridge`. Arena+ and Maechen reuse
the native primitives and lifecycle helpers needed by their own menu objects. Inclusion in a build
does not by itself prove that a menu was opened in gameplay, deployed, or validated at RT2.

## Confirmed object lifecycle

The native menu pool contains up to 32 in-place objects, each 152 bytes (`0x98`):

1. `Alloc()` finds a free slot and already performs `Reset()`.
2. The caller writes the exact byte/word/dword fields and callback pointers.
3. `Register()` sets active `+64=1`, resets state `+40=0`, and synchronously invokes `+8` when set.
4. The main-thread menu pump invokes the update callback at `+12` and draw callback at `+16`.
5. Close sets byte `+65`; the next update tick runs the auxiliary close callback and the native
   finalizer, which resets the object, clears active `+64`, and releases the slot.

Selection state is stored at `+72` (live cursor), `+44` (confirmed choice), and `+40` (state).
Historical live probe evidence from `ffxprobectl list-read` observed `SELECTED` tracking the
Customize cursor. That observation is historical RT2 evidence for the field, not a current runtime
activation claim for every consumer.

## Input and modal ownership

Generic list input at `0x8B4460` is useful RE evidence, but `SpawnMenu()` binds the custom
`OurListInputCb`. The custom callback deliberately never writes `+69` or state `+40`: generic input
advanced field-menu FSM `sub_8B1580` on confirm and crashed this composition. Confirm/cancel instead
publish through DLL-owned result state.

Pool-layer freezing at `+63` can stop other pool-resident objects while leaving their `+62` draw
layer visible, but it cannot cover the pause menu because that menu is not in the pool. Active
consumers therefore coordinate modal ownership through `VA_CurrentPopup` and use
`ReleaseModalIfOwned()` so deferred cleanup cannot clear a newer owner's popup. `SwallowPad()` is
retained as historical RE scaffolding but is not called by the active custom callback; live use
soft-locked all game input.

## Confirmed ABIs and addresses

All function addresses below are preferred VAs from the supported 32-bit executable at IDA image
base `0x00400000`. Runtime resolution is `moduleBase + (VA - 0x00400000)`.

| Role | Preferred VA | ABI / contract |
|---|---:|---|
| Allocate object | `0x8AA150` | `int __cdecl()`; performs Reset; returns 0 when the pool is full |
| Reset object | `0x8AA460` | `int __cdecl(int obj)`; installs the `+55`, `+62`, and `+63` defaults |
| Register object | `0x8AAAB0` | `int __cdecl(int obj)`; sets `+64=1`, `+40=0` |
| Generic list input | `0x8B4460` | `int __cdecl(int obj)` |
| Draw window | `0x8F5F70` | `void __cdecl(float left, float top, float w, float h, int style)` |
| Draw string | `0x9016B0` | `int __cdecl(int ctx, byte* text, float x, float y, char flags, float sx, float sy)` |
| Draw cursor | `0x8C0640` | `int __cdecl(float x, float y, int kind)` |
| Scale X / Y | `0x644990` / `0x6449D0` | `float __cdecl(float)`; 1920x1080 design space to 512x416 Menu2D space |
| Current popup owner | `0x23CC120` | Modal-owner storage observed by `sub_8B1580` |
| Direction / edge readers | `0x8BE440` / `0x8BE480` | Native pad edge/repeat and single-edge readers |
| Menu sound | `0x886B00` | `int __cdecl(int id)`; 1 move/confirm, 4 cancel |

Layer fields require byte-precise writes: `+62=2` selects the draw layer and `+63=1` selects the
update layer. A dword write there would overwrite `+64` active and `+65` close state.

## Runtime guardrails and evidence boundary

- This surface is x86-only; callback pointers and object handles are four bytes.
- Menu-object work must run on the main thread.
- Drawing requires the menu subsystem to be live at `g_FFX_MenuSubsystemActive`, preferred VA
  `0x13407E4`, while the game is in a menu/field context. Otherwise an allocation can become an
  unpumped zombie object.
- The header's fixed VAs are address/ABI evidence, not a substitute for the active caller's
  supported-executable, admission, ownership, and teardown gates.
- Build inclusion is offline source/build evidence. Deployment, live behavior, and Production
  require their separately authorized evidence levels.
- Use a disposable save for the first separately authorized RT2 case.

## Historical reverse-engineering source identifiers

The original 2026-06-09 comments recorded the following source paths. They are retained here as
provenance identifiers, but the files are not present in this checkout:

- `docs/reverse/FFX_NATIVE_MENU_LIST_ROW_SOURCE_2026-06-09.md`
- `docs/reverse/FFX_NATIVE_MENU_TICK_AND_ABI_2026-06-09.md`
- `docs/reverse/FFX_MENU_INPUT_READERS_CUSTOM_CB_ABI_2026-06-09.md`
