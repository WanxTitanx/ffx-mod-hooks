// ============================================================================
//  NativeMenuShell.h  —  ACTIVE SHARED RUNTIME HEADER FOR POLYHOOK BUILDS
// ----------------------------------------------------------------------------
//  Jarvis-HOOK — native in-game menu surface for FFX HD (FFX.exe x86).
//
//  WHAT IT IS: a native N-row menu shell with DLL-owned text, rendered through
//  FFX's own font, cursor, window, input, sound, and texture primitives. It is not
//  an overlay and does not require a Present hook for drawing. Arbitrary row text
//  requires an in-DLL draw callback; an external probe cannot provide that callback.
//
//  CONFIRMED MECHANISM: construct a 152-byte menu object in the game's pool:
//    obj = Alloc()            -> finds a free slot and already performs Reset
//                                (zeroes the object and installs defaults)
//    set fields + callbacks   -> +12 = input callback; +16 = DLL draw callback;
//                                +28 = optional confirm validator
//    Register(obj)            -> marks the object active (+64=1) and resets state (+40=0)
//  The native menu pump invokes +12 and +16 each frame. Selection state lives at
//  +72 (cursor), +44 (confirmed choice), and +40 (state). Historical live probe
//  evidence (`ffxprobectl list-read`) observed SELECTED following the Customize
//  cursor; this header supplies the in-process draw side of that lifecycle.
//
//  CURRENT BUILD STATUS: this is no longer a standalone reference skeleton.
//  PolyHook builds include it from `dllmain.cpp`, `ArenaPlusComposePick.cpp`, and
//  `MaechenHook.cpp`. The primary F7 wire in `dllmain.cpp` uses the decoupled
//  PhotoModeBridge; Arena+ and Maechen reuse the native menu primitives and lifecycle
//  helpers. The bridge keeps this header free of a compile-time Aurora dependency.
//
//  HISTORICAL EVIDENCE IDENTIFIERS (ABIs/offsets double-verified in IDA on 2026-06-09;
//  these source documents are not present in the current checkout):
//    docs/reverse/FFX_NATIVE_MENU_LIST_ROW_SOURCE_2026-06-09.md  (row source and section 5 recipe)
//    docs/reverse/FFX_NATIVE_MENU_TICK_AND_ABI_2026-06-09.md     (tick, pool, lifecycle, font)
//  IDA image base is 0x400000. Runtime addresses resolve as base+(VA-0x400000)
//  through GetModuleHandle(NULL).
//
//  RT2 PRECONDITION: drawing requires the menu subsystem to be live
//  (g_FFX_MenuSubsystemActive at VA 0x13407E4), meaning the game is in a menu/field
//  context. All menu-object work runs on the main thread: Register invokes +8
//  synchronously, and the native pump is main-thread-owned.
// ============================================================================
#pragma once
#include <stdio.h>  // _snprintf_s keeps this header self-contained (2026-08-02 fix).
#if defined(_WIN64) || defined(__x86_64__)
#  error "NativeMenuShell targets 32-bit FFX.exe (x86) ONLY — cdecl float-on-stack ABI + 4-byte pointers."
#endif
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NativeMenu {

// ---------------------------------------------------------------------------
// 0) ASLR address resolution: runtime = base + (VA_IDA - IMAGE_BASE)
// ---------------------------------------------------------------------------
static const uintptr_t kImageBase = 0x00400000u;
static_assert(sizeof(void*) == 4, "NativeMenuShell is x86-only (FFX.exe is 32-bit; pointers carried in int).");

static inline uintptr_t FfxBase() {
#ifdef _WIN32
    return (uintptr_t)GetModuleHandleA(NULL);   // FFX.exe is the main module.
#else
    return 0;
#endif
}
// Cast an IDA VA to a function pointer of type T in the live process.
#define FFX_FN(va, T) ((T)(NativeMenu::FfxBase() + (uintptr_t)(va) - NativeMenu::kImageBase))

// ---------------------------------------------------------------------------
// 1) Confirmed native ABIs (all __cdecl) — IDA VAs (FFX.exe at 0x400000)
// ---------------------------------------------------------------------------
// Menu-object lifecycle (152 bytes = 0x98): pool at 0x18408C0, stride 152, maximum 32.
typedef int   (__cdecl* Fn_Alloc)(void);          // 0x8AA150: finds a free slot, already calls Reset, returns obj (0 = pool full)
typedef int   (__cdecl* Fn_Reset)(int obj);       // 0x8AA460: zeroes and applies defaults (+52=0x01000000 -> +55=1; +62 dword=0x101 -> +62=1,+63=1)
typedef int   (__cdecl* Fn_Register)(int obj);    // 0x8AAAB0: sets +64=1 (active), +40=0 (state), calls *(obj+8) when nonzero
// Generic list input provides navigation, selection, and scrolling without globals.
typedef int   (__cdecl* Fn_ListInput)(int obj);   // 0x8B4460: state machine over +40/+48/+50/+52/+58/+66/+69/+70/+72; +28 is the confirm validator
// Draw primitives use physical 512x416 coordinates; use Scale* to convert from 1920x1080.
typedef void  (__cdecl* Fn_DrawWindow)(float left, float top, float w, float h, int style); // 0x8F5F70 (style 10 = standard frame)
typedef int   (__cdecl* Fn_DrawString)(int ctx, const unsigned char* ffxText, float x, float y,
                                       char flags, float scaleX, float scaleY);             // 0x9016B0 (ctx=0, FFX-encoded bytes, fixed color 128)
typedef int   (__cdecl* Fn_DrawCursor)(float x, float y, int kind);                         // 0x8C0640 (kind 0 = menu cursor)
// Textured quad from the menu atlas. The selector chooses the atlas (RE 0x903EE0/0x8AC870, Jarvis-VALEFOR):
//   0xFFFFFFFE=ffx_bg(2048x1024) · 0xFFFFFFFA=summonbg · 0xFFFFFFFC=stonetexture(256) · 0xFFFFFFFF=battle_kuang · 600..649=icon · 200..398=meswin · 400..598=battle.
//   x/y/w/h are physical coordinates (use SX/SY); u0/v0/u1/v1 are texels; c0/c1 are RGBA colors (0x80=normal). This selector wrapper accepts only those atlas selectors; the separate atlas-ID path below resolves handles.
typedef void  (__cdecl* Fn_DrawTexQuad)(unsigned int sel, float x, float y, float w, float h,
                                        float u0, float v0, float u1, float v1, unsigned int c0, unsigned int c1); // 0x903EE0
typedef float (__cdecl* Fn_ScaleX)(float v1920);  // 0x644990: v*512/1920 (X axis / width)
typedef float (__cdecl* Fn_ScaleY)(float v1080);  // 0x6449D0: v*416/1080 (Y axis / height)

static inline int   Alloc()             { return FFX_FN(0x8AA150, Fn_Alloc)(); }
static inline int   Reset(int o)        { return FFX_FN(0x8AA460, Fn_Reset)(o); }
static inline int   Register(int o)     { return FFX_FN(0x8AAAB0, Fn_Register)(o); }
static inline float SX(float v)         { return FFX_FN(0x644990, Fn_ScaleX)(v); }
static inline float SY(float v)         { return FFX_FN(0x6449D0, Fn_ScaleY)(v); }

/* Menu2D canvas: the engine maps the 1920x1080 design space to its physical buffer through
 * ScaleX/ScaleY (IDA currently shows a fixed 512x416 buffer; MenuPhys* follows any future scaler
 * change). Layout uses normalized [0,1] fractions, not monitor resolution; the engine upscaler
 * handles output resolutions such as 2K and 720p. */
static inline float MenuPhysW() { return SX(1920.0f); }
static inline float MenuPhysH() { return SY(1080.0f); }
static inline float MenuBorderPx() {
    const float m = MenuPhysW() < MenuPhysH() ? MenuPhysW() : MenuPhysH();
    return m * 0.008f;
}
static inline float NX(float u) { return SX(u * 1920.0f); }
static inline float NY(float v) { return SY(v * 1080.0f); }
static inline float NW(float w) { return SX(w * 1920.0f); }
static inline float NH(float h) { return SY(h * 1080.0f); }
static inline void  DrawWindow(float l, float t, float w, float h, int style) { FFX_FN(0x8F5F70, Fn_DrawWindow)(l, t, w, h, style); }
static inline void  DrawString(const unsigned char* s, float x, float y)       { FFX_FN(0x9016B0, Fn_DrawString)(0, s, x, y, 0, 0.78f, 1.0f); }
static inline void  DrawStringSub(const unsigned char* s, float x, float y)   { FFX_FN(0x9016B0, Fn_DrawString)(0, s, x, y, 0, 0.52f, 0.70f); }
static inline void  DrawCursor(float x, float y)                               { FFX_FN(0x8C0640, Fn_DrawCursor)(x, y, 0); }
static inline void  DrawTexQuad(unsigned int sel, float x, float y, float w, float h, float u0, float v0, float u1, float v1, unsigned int c0, unsigned int c1) { FFX_FN(0x903EE0, Fn_DrawTexQuad)(sel, x, y, w, h, u0, v0, u1, v1, c0, c1); }
// Raw color primitives (IDA-confirmed 2026-06-10, IFRIT lane). Both use the same signature and
// ARGB 0xAARRGGBB colors (high byte is alpha; 0x80 is normal). c0 is the top and c1 the bottom of
// the vertical gradient. These calls provide caller-controlled color and no icon.
//   0x8F4B20 FFX_Menu2D_DrawSolidRect  -> EmitQuad mode 0 (flat solid rectangle)
//   0x8F4DF0 FFX_Menu2D_DrawPlasma     -> EmitQuad mode 2 (animated plasma/glow used by name bars)
typedef void  (__cdecl* Fn_DrawColorQuad)(float x, float y, float w, float h, unsigned int c0, unsigned int c1);
// The engine emits vertex color in the R,G,B,A component order read by the GPU from bytes 0..3.
// Callers provide palette-friendly ARGB 0xAARRGGBB, so swap R and B here while preserving A and G.
// Without this 2026-06-10 correction, red rendered blue and cyan rendered gold; S.I.N. happened
// to hide the defect because its R/B values were nearly symmetric.
static inline unsigned int Argb2Abgr(unsigned int c) { return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16); }
static inline void  DrawSolidRect(float x, float y, float w, float h, unsigned int c0, unsigned int c1) { FFX_FN(0x8F4B20, Fn_DrawColorQuad)(x, y, w, h, Argb2Abgr(c0), Argb2Abgr(c1)); }
static inline void  DrawPlasma   (float x, float y, float w, float h, unsigned int c0, unsigned int c1) { FFX_FN(0x8F4DF0, Fn_DrawColorQuad)(x, y, w, h, Argb2Abgr(c0), Argb2Abgr(c1)); }
// Scale an ARGB alpha for top-to-bottom gradients: FadeAlpha(c, 1, 2) halves the alpha.
static inline unsigned int FadeAlpha(unsigned int argb, unsigned int num, unsigned int den) {
    unsigned int a = ((argb >> 24) & 0xFFu) * num / den;
    return (argb & 0x00FFFFFFu) | (a << 24);
}

// ---- RAW HANDLE PATH (RE Jarvis-IFRIT 2026-06-10): draw any resident menu texture by atlasId ----
// FFX_Menu2D_TexHandleByAtlasId at 0x8AC870 maps atlasId to a texture-key handle.
// FFX_Menu2D_EmitQuad_Accumulate at 0x63F090 emits a quad using that handle as argument 2, and
// FFX_Menu2D_ClipQuadToScissor at 0x8E5A20 clips it. Together they reach atlases absent from the
// selector switch: 12032 = help/now_help (Jecht crest art on the HELP tab), 1257216.. =
// help/mon_boku (bestiary), 16001 = texture, 11980 = strtex, and 11948 = worldmap.
// The texture must already be resident in VRAM. Otherwise the handle is only a key without a GPU
// texture, so nothing is drawn but the game does not crash. UVs are normalized to 0..1. ARGB c0
// (top) and c1 (bottom) modulate the texture; 0xFF is full intensity and 0x80 is approximately half.
typedef char* (__cdecl* Fn_TexHandleByAtlasId)(int atlasId);                                           // 0x8AC870
typedef void  (__cdecl* Fn_EmitQuadAccum)(int quad, char* handle, int one, int mode, float zero);     // 0x63F090
typedef int   (__cdecl* Fn_ClipQuad)(float*, float*, float*, float*, float*, float*, float*, float*); // 0x8E5A20
static inline char* TexHandleByAtlasId(int atlasId) { return FFX_FN(0x8AC870, Fn_TexHandleByAtlasId)(atlasId); }
static inline void DrawTexByAtlasId(int atlasId, float x, float y, float w, float h,
                                    float u0, float v0, float u1, float v1, unsigned int c0, unsigned int c1) {
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h, uu0 = u0, vv0 = v0, uu1 = u1, vv1 = v1;
    c0 = Argb2Abgr(c0); c1 = Argb2Abgr(c1);                                                   // ARGB -> engine byte order (R<->B)
    if (!FFX_FN(0x8E5A20, Fn_ClipQuad)(&x0, &y0, &x1, &y1, &uu0, &vv0, &uu1, &vv1)) return;   // Skip fully off-screen quads.
    int q[38];
    for (int i = 0; i < 38; ++i) q[i] = 0;                          // Zero scratch storage because the engine leaves it uninitialized.
    *(float*)&q[0] = x0;  *(float*)&q[1] = y0;  *(float*)&q[2] = uu0; *(float*)&q[3] = vv0;
    q[4] = (int)(c0 & 0xFF); q[5] = (int)((c0 >> 8) & 0xFF); q[6] = (int)((c0 >> 16) & 0xFF); q[7] = (int)((c0 >> 24) & 0xFF);
    *(float*)&q[8] = x1;  *(float*)&q[9] = y1;  *(float*)&q[10] = uu1; *(float*)&q[11] = vv1;
    q[12] = (int)(c1 & 0xFF); q[13] = (int)((c1 >> 8) & 0xFF); q[14] = (int)((c1 >> 16) & 0xFF); q[15] = (int)((c1 >> 24) & 0xFF);
    FFX_FN(0x63F090, Fn_EmitQuadAccum)((int)q, TexHandleByAtlasId(atlasId), 1, 0, 0.0f);    // Mode 0 emits a textured quad.
}

static const uintptr_t VA_ListInput = 0x8B4460;   // Function pointer for +12 (generic list input).

// ---------------------------------------------------------------------------
// 2) Confirmed 152-byte menu-object offsets (see TICK_AND_ABI and 2026-06-09 verification)
// ---------------------------------------------------------------------------
enum Off {
    O_ENTER     = 8,    // dword: entry callback (Register calls it when nonzero). Use 0.
    O_UPDATE    = 12,   // dword: input/tick callback -> Fn_ListInput (0x8B4460)
    O_DRAW      = 16,   // dword: draw callback -> OurDraw
    O_AUX       = 20,   // dword: auxiliary/visibility callback on the close path. Use OurAux (returns 1).
    O_VALIDATOR = 28,   // dword: confirm validator, cdecl(obj, selRow=+72, confirmSlot=+69)->bool; 0 rejects the row
    O_STATE     = 40,   // dword: state machine (0 init; 2 idle/nav; 15/16 confirm-done; 17/18 cancel-close)
    O_CHOICE    = 44,   // word: confirmed row (written by input on confirm; -1 means no choice yet)
    O_COUNT     = 48,   // word: total row count
    O_TOP       = 50,   // word: first visible row (scroll position)
    O_CANCEL    = 55,   // byte: do not close at the last row (=1; Reset already sets it through dword +52=0x01000000)
    O_PAGE      = 58,   // word: visible row count (page size)
    O_GROUP62   = 62,   // byte: draw layer used by the draw-tick filter. Use 2.
    O_GROUP63   = 63,   // byte: update layer used by the update-tick filter. Reset sets 1; preserve 1.
    O_ACTIVE    = 64,   // byte: in-use flag. Do not write it; Register sets 1 and Alloc scans it.
    O_SLOTS     = 66,   // byte: number of confirmations before close (=1)
    O_SELECTED  = 72,   // word: row under the live cursor; historical RT2 observed it track navigation
};
static inline uint8_t*  Pb(int o, int f){ return (uint8_t*)((uintptr_t)o + f); }
static inline int16_t   RdW(int o, int f){ return *(int16_t*)Pb(o, f); }
static inline int32_t   RdD(int o, int f){ return *(int32_t*)Pb(o, f); }
static inline void      WrW(int o, int f, int16_t v){ *(int16_t*)Pb(o, f) = v; }
static inline void      WrB(int o, int f, uint8_t v){ *(uint8_t*)Pb(o, f) = v; }
static inline void      WrP(int o, int f, void* v){ *(uintptr_t*)Pb(o, f) = (uintptr_t)v; }

// ---------------------------------------------------------------------------
// 3) FFX font encoder (stage 2 — fully recovered table).
//    byte = 0x30 + index in FFX_ATLAS (digits first). Unknown characters become spaces.
//    0x9016B0 expects this encoding terminated by null. Labels remain in DLL-owned
//    memory; game scratch storage is unnecessary because both share the process.
// ---------------------------------------------------------------------------
static const char* const FFX_ATLAS =
    "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";

static inline void EncodeLabel(const char* ascii, unsigned char* out, int cap) {
    int n = 0;
    for (const char* c = ascii; *c && n < cap - 1; ++c) {
        int gi = -1;
        for (int i = 0; FFX_ATLAS[i]; ++i) { if (FFX_ATLAS[i] == *c) { gi = i; break; } }
        if (gi < 0) gi = 10;                 // Unknown -> space (index 10 = 0x3A).
        out[n++] = (unsigned char)(0x30 + gi);
    }
    if (cap > 0) out[n] = 0;                 // Null-terminate; FFX stops at 0x00.
}

// ---- INPUT PROMPT SHEETS (RE Jarvis-HOOK 2026-09-16) ----
// FFX_Menu_LoadInputDeviceIconTextures (0x6365E0) binds pad_icon.dds.phyre and
// keyboard_icon.dds.phyre into the Menu2D capture context once at menu startup.
// FFX_Menu_RenderEnqueue (0x63F090, the EmitQuadAccum target) forwards its second
// argument as the texture path string; FFX_Menu2D_EnqueueQuadCaptured (0x63EAE0)
// routes it by substring — "pad_icon" keeps the default capture entry and
// "keyboard_icon" selects the +324 entry. When the capture branch is inactive
// the normal path resolves the string via ResolveTextureNameForUI, which needs
// the REGISTERED VFS path, not a bare name — these are the exact strings the
// engine itself loads in FFX_Menu_LoadInputDeviceIconTextures (0x6365E0):
static const char* const kSheetPad = "/FFX_Data/GameData/PS3Data/menu/D3D11/pad_icon.dds.phyre";
static const char* const kSheetKb  = "/FFX_Data/GameData/PS3Data/menu/D3D11/keyboard_icon.dds.phyre";
// UnX replaces the pad sheet with the configured Gamepad TextureSet
// (same 12x8 cell grid), which means pad cells automatically render the glyphs
// of the user's controller set. KEY: 0x63F090 takes only TL and BR corners in
// slots 0 and 8; its capture branch expands them into four vertices at
// 0x63F1E7..0x63F258. Passing pre-expanded TL/BL/TR/BR makes slot 8 the BL
// corner, x1 equals x0, and every prompt becomes a zero-width invisible quad.
enum PromptCell {
    PC_PAD_FACE_L = 0x00,   // row0: Square on PS sets / X on Xbox sets
    PC_PAD_FACE_D = 0x01,   //        Cross / A  -> Confirm
    PC_PAD_FACE_R = 0x02,   //        Circle / B -> Cancel/Back
    PC_PAD_FACE_U = 0x03,   //        Triangle / Y
    PC_PAD_L1 = 0x04, PC_PAD_R1 = 0x05, PC_PAD_L2 = 0x06, PC_PAD_R2 = 0x07,
    PC_PAD_SELECT = 0x08, PC_PAD_START = 0x09, PC_PAD_LS = 0x0A, PC_PAD_RS = 0x0B,
    PC_PAD_UP = 0x10, PC_PAD_RIGHT = 0x11, PC_PAD_DOWN = 0x12, PC_PAD_LEFT = 0x13,
    PC_PAD_DPAD = 0x14,
    // keyboard_icon sheet: row3 = Esc + F1..F11, row4 = F12 + Enter + editing keys,
    // row5 = navigation arrows/Home/End/PgUp/PgDn, row7 = Shift/Ctrl/Alt + mouse.
    // KEY: arrow column order is decoded-image best effort; if Left/Right render
    // swapped at RT2 the fix is the two constants, not the plumbing.
    // Row 4 cell 2 is the Backspace glyph (decoded sheet + user RT2 2026-09-16):
    // the F-key menus' Back/Cancel rides the pad cancel edge, which the game maps
    // to Backspace on keyboard — Esc stays only for explicit Esc hints (F9) and
    // the emergency held-lane close, which has no footer hint.
    PC_KB_ESC = 0x30, PC_KB_F7 = 0x37, PC_KB_F8 = 0x38, PC_KB_F9 = 0x39,
    PC_KB_ENTER = 0x41, PC_KB_BACKSPACE = 0x42,
    PC_KB_LEFT = 0x53, PC_KB_RIGHT = 0x54, PC_KB_UP = 0x55, PC_KB_DOWN = 0x56,
    PC_SKIP = -1
};

// WHY: the vanilla prompt updater picks the pad sheet only while
// FFX_AsyncQ_IsActive(ctx) is nonzero — ctx = FFX_AsyncQ_GetContext() = &0xCCB170
// and IsActive reads dword ctx+0xA8 (0xCCB218). Reading the field directly keeps
// our footer hints in lockstep with the icons the game itself would show.
static inline bool PadInputActive() {
    return *(volatile unsigned int*)(FfxBase() + (uintptr_t)0xCCB218 - kImageBase) != 0;
}

static inline void DrawPromptGlyph(const char* sheet, int cell,
                                   float x, float y, float s, unsigned int argb) {
    const int col = cell & 15, row = (cell >> 4) & 15;
    float x0 = x, y0 = y, x1 = x + s, y1 = y + s;
    const float cu = 1.0f / 12.0f, cv = 1.0f / 8.0f;
    float u0 = cu * (float)col, v0 = cv * (float)row, u1 = u0 + cu, v1 = v0 + cv;
    if (!FFX_FN(0x8E5A20, Fn_ClipQuad)(&x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1)) return;
    const unsigned int c = Argb2Abgr(argb);
    int q[38] = {};
    *(float*)&q[0] = x0; *(float*)&q[1] = y0;
    *(float*)&q[2] = u0; *(float*)&q[3] = v0;
    q[4] = (int)(c & 0xFF); q[5] = (int)((c >> 8) & 0xFF);
    q[6] = (int)((c >> 16) & 0xFF); q[7] = (int)((c >> 24) & 0xFF);
    *(float*)&q[8] = x1; *(float*)&q[9] = y1;
    *(float*)&q[10] = u1; *(float*)&q[11] = v1;
    q[12] = (int)(c & 0xFF); q[13] = (int)((c >> 8) & 0xFF);
    q[14] = (int)((c >> 16) & 0xFF); q[15] = (int)((c >> 24) & 0xFF);
    FFX_FN(0x63F090, Fn_EmitQuadAccum)((int)q, (char*)sheet, 1, 0, 0.0f);
}

// One footer hint: up to two glyph cells followed by a short label. When no pad
// binding exists (padA == PC_SKIP) the keyboard cell is drawn even with a pad
// active — F7/F8/F9 keys simply have no pad equivalent. Returns the next x.
static inline float DrawInputHint(float x, float y, int padA, int padB, int kbA, int kbB,
                                  const char* label, unsigned int argb) {
    const bool pad = PadInputActive() && padA >= 0;
    const char* sheet = pad ? kSheetPad : kSheetKb;
    const int a = pad ? padA : kbA, b = pad ? padB : kbB;
    const float s = NH(0.030f);
    if (a >= 0) { DrawPromptGlyph(sheet, a, x, y, s, argb); x += s + NX(0.003f); }
    if (b >= 0) { DrawPromptGlyph(sheet, b, x, y, s, argb); x += s + NX(0.003f); }
    if (label && label[0]) {
        unsigned char enc[48] = {};
        EncodeLabel(label, enc, (int)sizeof(enc));
        x += NX(0.003f);
        DrawStringSub(enc, x, y + NH(0.006f));
        int len = 0; for (const char* p = label; *p; ++p) ++len;
        x += (float)len * NX(0.0086f);
    }
    return x + NX(0.020f);
}

// ---------------------------------------------------------------------------
// 4) Row model and decoupled bridge to runtime actions
// ---------------------------------------------------------------------------
// IN-LIVE menu action IDs from the IFRIT lane. `dllmain.cpp` consumes ACT_EXIT to close the menu;
// do not remove or rename it. `dllmain.cpp` also names the ActionId type directly, so preserve it.
enum ActionId {
    ACT_DIFFICULTY = 0,      // Opens the Difficulty editor; the hub never edits its preset.
    ACT_CTB,                 // Turn-order gauge; light RE.
    ACT_FORCE_BATTLE,        // Calls 0x380DE0 MsBattleEncountExe; immediate.
    ACT_AI_SWAP,             // Opens the no-write Monster AI registration observer.
    ACT_BATTLE_CHEATS,       // Native god/no-MP flags; immediate.
    ACT_OVERDRIVE,           // Fill/unlock; light RE.
    ACT_MUSIC,               // Opens the Music editor; preview belongs to that submenu.
    ACT_ARENA,               // Monster unlock; light RE.
    ACT_EQUIPMENT,           // Auto-ability; light RE.
    ACT_STATUS,              // Inflict/cleanse; light RE.
    ACT_FORMATION,           // Frontline membership; light RE.
    ACT_GIL_ITEMS,           // Save-data action; persistence is not proven.
    ACT_PLAYER_BOOST,        // Needs RT2; the player write has never been live-proven.
    ACT_CAMERA,              // Stopped with the retired Aurora lane; required per-frame repoke.
    ACT_SIN,                 // Read-only unavailable status for the quarantined legacy writer.
    ACT_EXIT,                // Closes the menu; consumed by dllmain.cpp.
    ACT__COUNT
};
enum ActKind { EDGE, HELD };
// KEYSTONE (F7 plan phase A, 2026-08-02): RT_NONE is a simple action;
// RT_TOGGLE and RT_STEPPER expose a value edited with Left/Right (0x8000/0x2000).
enum RowType { RT_NONE = 0, RT_TOGGLE, RT_STEPPER };

// Glass / neon palette (shared by hub + sub-menus).
static const unsigned int kMenuNeonGreenHi   = 0xF050FF90u;
static const unsigned int kMenuNeonGreenLo   = 0xF018AA55u;
static const unsigned int kMenuNeonGreenGlow = 0xC0B8FFD8u;
static const unsigned int kMenuNeonGreenLine = 0xA050FF90u;
static const unsigned int kMenuNeonGreenLineLo = 0x8018AA55u;
static const unsigned int kMenuGlassFillTop  = 0x58182840u;
static const unsigned int kMenuGlassFillBot  = 0x38101822u;
static const unsigned int kMenuGlassSheen    = 0x20FFFFFFu;
static const unsigned int kMenuGlassBorder   = 0x5040AA68u;
static const unsigned int kMenuGlassBorderLo = 0x38287048u;
static const unsigned int kMenuRowGlassTop   = 0x68283850u;
static const unsigned int kMenuRowGlassBot   = 0x48182028u;

struct Row {
    const char* labelAscii;   // Human-readable text; SpawnMenu converts it to the FFX encoding.
    ActionId    action;
    ActKind     kind;
    unsigned int barTop;      // IFRIT: caller-controlled top ARGB 0xAARRGGBB (DrawSolidRect/Plasma c0), with no icon.
    unsigned int barBottom;   // Bottom ARGB color (c1), producing a vertical top-to-bottom gradient.
    int          barPlasma;   // 0 = solid; 1 = animated plasma through DrawPlasma, used for S.I.N.
    RowType     rowType;      // KEYSTONE: RT_NONE, RT_TOGGLE, or RT_STEPPER.
    int         minVal;       // KEYSTONE: lower STEPPER bound.
    int         maxVal;       // KEYSTONE: upper STEPPER bound.
    int         stepVal;      // KEYSTONE: STEPPER increment.
};

// IN-LIVE menu from the IFRIT lane. The order follows the roadmap ranking. All current rows use
// EDGE; none uses HELD. Eight rows and kVisiblePage=8 fit on one page without scrolling.
// Fields 4/5/6 hold top and bottom ARGB 0xAARRGGBB colors for the no-icon gradient plus plasma 0/1.
// The Jarvis-SAFADA Spira/Yevon palette is documented in
// docs/ai/NATIVE_MENU_PALETTE_SPIRA_YEVON_2026-06-10.md. Most rows use one cohesive dark-slate
// family; S.I.N. deliberately uses high-alpha plasma while Music/teal is the lightest treatment.
static Row g_rows[] = {
    // Cohesion rule: every ordinary bar uses the same dark slate. Text, and eventually icons,
    // distinguish choices; color remains exceptional. Only S.I.N. carries a unique live color,
    // and selection is the only other color event.
    // KEYSTONE (2026-08-02): RT_STEPPER/RT_TOGGLE enables Left/Right (0x8000/0x2000) value editing.
    { "Difficulty",                      ACT_DIFFICULTY,    EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
    { "Force Battle",                    ACT_FORCE_BATTLE,  EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
    { "Monster AI Observer",             ACT_AI_SWAP,       EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
    { "Music",                           ACT_MUSIC,         EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
    { "Party Invincible (debug)",        ACT_BATTLE_CHEATS, EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_TOGGLE,  0, 1, 1 },
    { "Arena+",                          ACT_ARENA,         EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
    { "S.I.N. Curses",                   ACT_SIN,           EDGE, 0xE6B33CFFu, 0xE63A0A6Eu, 1, RT_NONE, 0, 0, 1 },
    { "Exit",                            ACT_EXIT,          EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
};
static const int kRowCount = (int)(sizeof(g_rows) / sizeof(g_rows[0]));
// KEYSTONE: current per-row values edited through Left/Right (0x8000/0x2000).
static int g_rowValue[kRowCount] = {};
// 2026-08-02 RT2 fix: a stepper stays unlabeled until edited; an unexplained initial "[0]" confused players.
static bool g_rowEdited[kRowCount] = {};
static const int kVisiblePage = 8;          // Visible row count (must be <= kRowCount).
static const int kLabelCap = 64;            // Bytes per FFX-encoded label.

static unsigned char g_labelBytes[ (sizeof(g_rows)/sizeof(g_rows[0])) ][kLabelCap]; // SpawnMenu fills these encoded labels.

// The active dllmain.cpp integration fills this bridge with runtime action callbacks.
// Keeping function pointers here avoids a compile-time dependency on Aurora's
// RuntimeTools/BattlePhotoMode/PhotoModeActions.h surface.
struct PhotoModeBridge {
    void (*onEdge)(ActionId a);             // EDGE actions run once: toggle/freeze/snapshot/reset/exit/select.
    void (*onHeldEnter)(ActionId a);        // HELD actions enter a continuous move/rotate/pan/angle mode.
};
static PhotoModeBridge g_bridge = { 0, 0 };
static inline void SetBridge(const PhotoModeBridge& b) { g_bridge = b; }
static bool (*g_inputAdmission)() = nullptr;
static inline void SetInputAdmission(bool (*admission)()) { g_inputAdmission = admission; }

// A remapped F7 hotkey must be visible inside the game, not only in a log the
// player may never open. The adapter owns this fixed buffer and updates it only
// before a menu object is published.
static unsigned char g_noticeBytes[64] = {};
static bool g_noticeActive = false;
static inline void SetNotice(const char* notice) {
    g_noticeActive = notice && notice[0];
    if (g_noticeActive) EncodeLabel(notice, g_noticeBytes, static_cast<int>(sizeof(g_noticeBytes)));
    else g_noticeBytes[0] = 0;
}

// ---------------------------------------------------------------------------
// 5) DLL draw callback (+16): window, DLL-owned row text, and cursor.
//    The draw tick calls it once per frame as cdecl(int obj). Coordinates convert
//    from virtual 1920x1080 through SX/SY to 512x416, matching the engine. Layout
//    constants are cosmetic and may be tuned during visual validation.
// ---------------------------------------------------------------------------
// IFRIT visual pass: staggered entry and an eased selection cursor.
static volatile int g_menuAnimStart = 0;     // g_ourDrawCalls frame captured by SpawnMenu to start entry animation.
static float        g_easedRowY     = -1.0f; // Virtual highlight Y eased toward the selected row.

// Estimated normalized per-option icon UVs for atlas 15808 (~16x3 grid), from the f7-demo-ingredients RE workflow.
static const float g_iconUV[kRowCount][4] = {
    {0.2500f,0.000f,0.3125f,0.333f},  // Difficulty
    {0.5625f,0.333f,0.6250f,0.666f},  // Force Battle
    {0.6875f,0.333f,0.7500f,0.666f},  // Monster AI
    {0.5000f,0.000f,0.5625f,0.333f},  // Music
    {0.8125f,0.333f,0.8750f,0.666f},  // Arena
    {0.0625f,0.333f,0.1250f,0.666f},  // Camera
    {0.1875f,0.000f,0.2500f,0.333f},  // SIN
    {0.9375f,0.333f,1.0000f,0.666f},  // Exit
};

// Format an unsigned decimal value with the FFX font encoding through EncodeLabel.
static inline void EncodeUInt(unsigned int v, unsigned char* out, int cap) {
    char rev[16]; int n = 0;
    if (v == 0) rev[n++] = '0';
    while (v > 0 && n < 15) { rev[n++] = (char)('0' + (v % 10)); v /= 10; }
    char asc[16]; int m = 0;
    while (n > 0 && m < 15) asc[m++] = rev[--n];
    asc[m] = 0;
    EncodeLabel(asc, out, cap);
}

// Smooth 0..1..0 oscillation over a frame period (triangle plus smoothstep), providing a pulse without strobing or math.h.
static inline float Osc01(int frame, int periodFrames) {
    if (periodFrames <= 0) return 0.0f;
    int ph = frame % periodFrames; if (ph < 0) ph += periodFrames;
    float t = (float)ph / (float)periodFrames;
    float tri = (t < 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f);   // Triangle wave: 0..1..0.
    return tri * tri * (3.0f - 2.0f * tri);                      // Smoothstep.
}
// Interpolate two ARGB colors component by component with s in [0,1].
static inline unsigned int ColorLerp(unsigned int a, unsigned int b, float s) {
    if (s < 0.0f) s = 0.0f; if (s > 1.0f) s = 1.0f;
    unsigned int out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float ca = (float)((a >> sh) & 0xFFu), cb = (float)((b >> sh) & 0xFFu);
        out |= ((unsigned int)(ca + (cb - ca) * s) & 0xFFu) << sh;
    }
    return out;
}

static const int kAtlasWorldmap = 11948;

// Backdrop: a scrim plus world map creates moderately opaque glass over the scene.
static inline void DrawMenuBackdrop() {
    const float x = 0.0f, y = 0.0f, w = MenuPhysW(), h = MenuPhysH();
    DrawSolidRect(x, y, w, h, 0xB0080814u, 0x90060810u);
    DrawTexQuad(0xFFFFFFFEu, x, y, w, h, 0.0f, 0.0f, 2048.0f, 1024.0f,
                0x38303848u, 0x30101820u);
    DrawTexByAtlasId(kAtlasWorldmap, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f,
                     0x4888A8B8u, 0x40586878u);
    DrawSolidRect(x, y, w, h, 0x50081018u, 0x6004080Cu);
}

/* Static crystal panel — no sweep/shimmer (accentEdge kept for call-site compat, ignored). */
static inline void DrawMenuGlassPanel(float x, float y, float w, float h, int /*animFrame*/, int /*accentEdge*/) {
    const float line = MenuBorderPx() * 0.22f;
    DrawSolidRect(x, y, w, h, kMenuGlassFillTop, kMenuGlassFillBot);
    if (line > 0.5f)
        DrawSolidRect(x + line, y + line * 0.45f, w - line * 2.0f, line * 0.30f, kMenuGlassSheen, 0x10FFFFFFu);
    DrawSolidRect(x, y, w, line, kMenuGlassBorder, kMenuGlassBorderLo);
    DrawSolidRect(x, y + h - line, w, line, kMenuGlassBorderLo, kMenuGlassBorder);
    DrawSolidRect(x, y, line, h, kMenuGlassBorder, kMenuGlassBorderLo);
    DrawSolidRect(x + w - line, y, line, h, kMenuGlassBorder, kMenuGlassBorderLo);
}

/* Retired: outer neon frame + sweep bar looked arcade; panels carry the glass look. */
static inline void DrawMenuNeonFrame(int /*animFrame*/) {}

static volatile int g_ourDrawCalls = 0;   // Diagnostic count: increasing means the field pump is invoking OurDraw.
static int __cdecl OurDraw(int obj) {
    ++g_ourDrawCalls;

    const int F = g_ourDrawCalls;

    // ===== D) BACKDROP — restrained Spira map and scrim avoid the blown-out white v2.165 overlay. =====
    DrawMenuBackdrop();
    DrawMenuNeonFrame(F);

    // ===== B/C) Header and footer: glass panels, English now, localization later. =====
    {
        static unsigned char s_title[64], s_sub[64], s_foot[64];
        static bool s_enc = false;
        if (!s_enc) {
            EncodeLabel("FFX Editor - In-Live", s_title, 64);
            EncodeLabel("Live RAM editing - persist to disk when ready", s_sub, 64);
            EncodeLabel("Arrows/Mouse Navigate   Confirm Open   Cancel Back   F7 Exit", s_foot, 64);
            s_enc = true;
        }
        const float hx = NX(0.042f), hy = NY(0.044f), hw = NW(0.917f), hh = NH(0.139f);
        DrawMenuGlassPanel(hx, hy, hw, hh, F, 0);
        DrawString(s_title, NX(0.073f), NY(0.080f));
        DrawString(g_noticeActive ? g_noticeBytes : s_sub, NX(0.073f), NY(0.132f));
        const float fx = NX(0.042f), fy = NY(0.887f), fw = NW(0.917f), fh = NH(0.072f);
        DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
        // Native button/key glyphs pick the pad or keyboard sheet like the vanilla
        // prompt bar; s_foot stays as the text fallback record.
        (void)s_foot;
        float hintX = NX(0.073f); const float hintY = fy + NH(0.018f);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_UP, PC_PAD_DOWN, PC_KB_UP, PC_KB_DOWN, "Navigate", 0xFFFFFFFFu);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_D, PC_SKIP, PC_KB_ENTER, PC_SKIP, "Open", 0xFFFFFFFFu);
        hintX = DrawInputHint(hintX, hintY, PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE, PC_SKIP, "Back", 0xFFFFFFFFu);
        DrawInputHint(hintX, hintY, PC_SKIP, PC_SKIP, PC_KB_F7, PC_SKIP, "Exit", 0xFFFFFFFFu);
    }

    // ===== H) LIST LAYOUT — canvas fractions adapt to the Menu2D buffer. =====
    const int top   = RdW(obj, O_TOP);
    const int page  = RdW(obj, O_PAGE);
    const int count = RdW(obj, O_COUNT);
    const int sel   = RdW(obj, O_SELECTED);
    const int animT = F - g_menuAnimStart;
    const float vLeft = NX(0.5625f), vTop = NY(0.213f), vWidth = NW(0.375f);
    const float vStep = NH(0.0648f), vBarH = NH(0.0593f), vPadX = NW(0.0146f);
    const float vSlide = NW(0.125f);
    const float selLine = MenuBorderPx() * 0.45f;
    const float cursorOff = NW(0.019f);

    // ===== G) SELECTION — ease Y for roughly 180 ms toward the focused row. =====
    const float selVisY = vTop + (float)(sel - top) * vStep;
    if (g_easedRowY < 0.0f) g_easedRowY = selVisY;
    g_easedRowY += (selVisY - g_easedRowY) * 0.30f;

    for (int r = 0; r < page; ++r) {
        const int row = top + r;
        if (row >= count || row >= kRowCount) break;   // Clamp access to the row array.
        // Staggered entry: slide from 240 design pixels to the right while fading with ~12-frame ease-out.
        const int   d = animT - r * 3;
        const float t = (d <= 0) ? 0.0f : (d >= 12 ? 1.0f : (float)d / 12.0f);
        const float e = 1.0f - (1.0f - t) * (1.0f - t);
        const float slide = (1.0f - e) * vSlide;
        const unsigned int fa = (unsigned int)(e * 255.0f);

        const float vy = vTop + (float)r * vStep;
        const float bx = vLeft + slide, by = vy, bw = vWidth, bh = vBarH;
        unsigned int c0 = g_rows[row].barTop, c1 = g_rows[row].barBottom;
        const bool isSin = (g_rows[row].barPlasma != 0);
        if (isSin)
            c0 = ColorLerp(0xE6B33CFFu, 0xE6D46BFFu, Osc01(F, 72));
        c0 = FadeAlpha(c0, fa, 255u); c1 = FadeAlpha(c1, fa, 255u);
        if (isSin) DrawPlasma(bx, by, bw, bh, c0, c1);
        else       DrawSolidRect(bx, by, bw, bh, c0, c1);
        DrawString(g_labelBytes[row], vLeft + vPadX + slide, vy + NH(0.017f));
        // KEYSTONE: render TOGGLE/STEPPER values in the right column.
        // 2026-08-02 fix: show a STEPPER value only after a Left/Right edit, avoiding the initial "[0]".
        if (row >= 0 && row < kRowCount && g_rows[row].rowType != RT_NONE) {
            if (g_rows[row].rowType == RT_TOGGLE || g_rowEdited[row]) {
                unsigned char valBytes[32];
                char valAsc[32];
                if (g_rows[row].rowType == RT_TOGGLE)
                    _snprintf_s(valAsc, sizeof(valAsc), "[%s]", g_rowValue[row] ? "ON" : "OFF");
                else
                    _snprintf_s(valAsc, sizeof(valAsc), "[%d]", g_rowValue[row]);
                EncodeLabel(valAsc, valBytes, 32);
                DrawString(valBytes, vLeft + vWidth - NW(0.085f) + slide, vy + NH(0.017f));
            }
        }
    }

    // ===== SELECTION AT EASED Y — subtle steel-blue lift, neon-green edge, and FFX cursor. =====
    if (sel >= top && sel < top + page) {
        const float ey = g_easedRowY;
        const unsigned int a = 0x44u + (unsigned int)(Osc01(F, 44) * 32.0f);
        unsigned int lift0, lift1;
        if (sel >= 0 && sel < kRowCount && g_rows[sel].barPlasma) {
            lift0 = (a << 24) | 0x00FFD27Au; lift1 = (a << 24) | 0x00A06820u;
        } else {
            lift0 = (a << 24) | 0x00305068u; lift1 = (a << 24) | 0x00182038u;
        }
        DrawSolidRect(vLeft, ey, vWidth, vBarH, lift0, lift1);
        DrawSolidRect(vLeft, ey + vBarH - selLine, vWidth, selLine, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
        DrawCursor(vLeft - cursorOff, ey + NH(0.002f));
    }
    return obj;
}

// DLL auxiliary/close callback (+20): a simple "visible/okay" result mirroring popup callback 0x8E36B0.
// It must return nonzero: close tick 0x8A91E0 finalizes and releases the slot only when +20 is null
// or returns nonzero. Returning 0 vetoes close and traps the menu in a per-frame close retry that consumes input.
static int __cdecl OurAux(int obj) { (void)obj; return 1; }

// DLL confirm validator (+28): cdecl(obj, selRow /*=+72*/, confirmSlot /*=+69, ~0*/) -> bool.
// It accepts every row. A row-specific veto must branch on argument 2 (selRow), not argument 3.
static int __cdecl OurValidate(int obj, int selRow, int confirmSlot) { (void)obj; (void)selRow; (void)confirmSlot; return 1; }

// ---------------------------------------------------------------------------
// 5.5) POOL-LOCAL MODAL INPUT OWNERSHIP — while this menu is open, freeze input
//      for other menu objects in the pool by moving their +63 input group outside
//      the pumped 0..8 range. Input tick sub_8A91E0 filters on +63==layer and skips
//      those objects, so they stop navigating/confirming. Draw tick sub_8A9640
//      filters independently on +62, leaving them visible but frozen. Only this
//      menu receives the pad, preventing double activation.
//      High-confidence re-menu-input-ownership RE found dword_23CC120 unsafe as a
//      general input gate; this pool-layer mechanism is the narrow lever for pool
//      members. Call FreezeOthersExcept(ourObj) before the pump trampoline and
//      RestoreOthers() afterward on every frame that uses this strategy.
// ---------------------------------------------------------------------------
static const uintptr_t POOL_VA = 0x18408C0;        // g_FFX_MenuObjPool: in-place array of 32 x 152-byte objects.
static const int POOL_STRIDE = 152, POOL_MAX = 32;
static int     g_frozenObj[POOL_MAX];
static uint8_t g_frozenG63[POOL_MAX];
static int     g_frozenN = 0;

static inline void FreezeOthersExcept(int ourObj) {
    g_frozenN = 0;
    for (int i = 0; i < POOL_MAX; ++i) {
        int obj = (int)(FfxBase() + (POOL_VA - kImageBase) + (uintptr_t)(POOL_STRIDE * i));
        if (*(uint8_t*)((uintptr_t)obj + O_ACTIVE) == 0) continue;   // Skip inactive slots.
        if (obj == ourObj) continue;                                  // Never freeze this object or it deadlocks itself.
        uint8_t* g = (uint8_t*)((uintptr_t)obj + O_GROUP63);
        if (*g > 8) continue;                                          // Already outside the pumped range.
        if (g_frozenN < POOL_MAX) { g_frozenObj[g_frozenN] = obj; g_frozenG63[g_frozenN] = *g; ++g_frozenN; }
        *g = 0xFF;                                                     // Move input to an unpumped layer; draw +62 remains active.
    }
}
static inline void RestoreOthers() {
    for (int i = 0; i < g_frozenN; ++i) {
        uint8_t* g = (uint8_t*)((uintptr_t)g_frozenObj[i] + O_GROUP63);
        if (*g == 0xFF) *g = g_frozenG63[i];                          // Restore only values that still carry this lane's 0xFF.
    }
    g_frozenN = 0;
}

// ---------------------------------------------------------------------------
// 5.6) CUSTOM INPUT CALLBACK (+12) AND MODAL PUBLICATION (RE 2026-06-09;
//      docs/reverse/FFX_MENU_INPUT_READERS_CUSTOM_CB_ABI_2026-06-09.md).
//      Generic input 0x8B4460 writes +69 and lets field-menu FSM sub_8B1580 advance
//      on confirm, which crashed this composition. The custom callback reads the same
//      pad readers for navigation but never writes +69 or changes state +40. Confirm
//      and cancel publish through DLL variables g_ourClosed/g_ourResult.
//      Pool freezing in section 5.5 could not cover the pause menu because that menu
//      is not pool-resident. Active callers instead publish modal ownership through
//      VA_CurrentPopup and release it only when they still own the slot.
// ---------------------------------------------------------------------------
static const uintptr_t VA_CurrentPopup = 0x23CC120;   // unk_23CC120: modal owner observed by FSM sub_8B1580.
static const uintptr_t VA_PadReadDir   = 0x8BE440;    // ReaderB edge+repeat: up 0x1000, down 0x4000, page 0x1/0x2.
static const uintptr_t VA_PadReadEdge  = 0x8BE480;    // ReaderC single edge: confirm 0x20, cancel 0x40.
static const uintptr_t VA_MenuPlaySfx  = 0x886B00;    // FFX_Menu_PlaySfx(id): 1=move/confirm, 4=cancel.
typedef int (__cdecl* Fn_PadRead)(void);
typedef int (__cdecl* Fn_PlaySfx)(int id);
static inline int  PadDir()  { return FFX_FN(VA_PadReadDir,  Fn_PadRead)() & 0xFFFF; }
static inline int  PadEdge() { return FFX_FN(VA_PadReadEdge, Fn_PadRead)() & 0xFFFF; }
static inline void PlaySfx(int id) { FFX_FN(VA_MenuPlaySfx, Fn_PlaySfx)(id); }

static const uintptr_t VA_PadGlobals = 0x25D09D2;   // Pad state block: held/edge/repeat/stick plus fallbacks.
// Historical input-swallow helper: zero the state consumed by readers 0x8BE3E0/440/480 after this
// menu reads it, making later readers such as the pause-menu FSM observe no input. It must only run
// while the menu is open. The active custom callback does not call it because live use soft-locked input.
static inline void SwallowPad() {
    volatile uint8_t* p = (volatile uint8_t*)(FfxBase() + (VA_PadGlobals - kImageBase));
    for (int i = 0; i < 24; ++i) p[i] = 0;          // 0x25D09D2..0x25D09EA: primary values plus fallbacks.
}

static volatile int g_ourResult = 0;   // Confirmed row >=0, or -1 for cancel; callback writes, integration reads.
static volatile int g_ourClosed = 0;   // 1 after confirm or cancel.

// Custom input callback (+12): navigate and confirm/cancel without ever writing +69 or +40. cdecl(int obj)->int.
static int __cdecl OurListInputCb(int obj) {
    // The native pump may continue running while FFX is backgrounded. Reject
    // the whole keyboard/controller snapshot before reading it in that state.
    if (g_inputAdmission && !g_inputAdmission()) return obj;
    // A pre-pump mouse press has already published both the displayed row and
    // its confirm result. Return before sampling PadDir/PadEdge so mixed input
    // cannot move or replace that authoritative click in the same frame.
    if (g_ourClosed) return obj;
    const int dir  = PadDir();
    const int edge = PadEdge();
    int sel = RdW(obj, O_SELECTED);
    const int count = RdW(obj, O_COUNT);
    int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);
    if (count <= 0) return obj;
    if (dir & 0x1000) {                              // UP wraps from the first row to the last.
        sel = (sel > 0) ? (sel - 1) : (count - 1); PlaySfx(1);
    } else if (dir & 0x4000) {                       // DOWN wraps from the last row to the first.
        sel = (sel < count - 1) ? (sel + 1) : 0; PlaySfx(1);
    }
    // KEYSTONE: LEFT (0x8000) and RIGHT (0x2000) edit the selected row value.
    if (sel >= 0 && sel < kRowCount && g_rows[sel].rowType != RT_NONE) {
        if (dir & 0x8000) {
            g_rowEdited[sel] = true;   // 2026-08-02 fix: reveal the value only after an edit.
            g_rowValue[sel] -= g_rows[sel].stepVal;
            if (g_rowValue[sel] < g_rows[sel].minVal) g_rowValue[sel] = g_rows[sel].minVal;
            PlaySfx(1);
        } else if (dir & 0x2000) {
            g_rowEdited[sel] = true;
            g_rowValue[sel] += g_rows[sel].stepVal;
            if (g_rowValue[sel] > g_rows[sel].maxVal) g_rowValue[sel] = g_rows[sel].maxVal;
            PlaySfx(1);
        }
    }
    if (sel < 0) sel = 0;
    if (sel > count - 1) sel = count - 1;
    if (sel < top) top = sel;                        // Keep selection visible across first/last wrapping.
    if (sel >= top + page) top = sel - page + 1;
    if (top > count - page) top = count - page;
    if (top < 0) top = 0;
    WrW(obj, O_SELECTED, (int16_t)sel);
    WrW(obj, O_TOP,      (int16_t)top);              // Snap without easing; do not write +69 or +40.
    if (!g_ourClosed) {
        if (edge & 0x20)      { PlaySfx(1); g_ourResult = sel; g_ourClosed = 1; }   // Confirm.
        else if (edge & 0x40) { PlaySfx(4); g_ourResult = -1;  g_ourClosed = 1; }   // Cancel.
    }
    return obj;   // Input swallowing was reverted because clearing the pad soft-locked all game input.
}

static inline void ClaimModal(int obj) { *(volatile int32_t*)(FfxBase() + (VA_CurrentPopup - kImageBase)) = obj; }
static inline void ReleaseModal()       { *(volatile int32_t*)(FfxBase() + (VA_CurrentPopup - kImageBase)) = 0; }
static inline void ReleaseModalIfOwned(int obj) {
    volatile int32_t* currentPopup =
        reinterpret_cast<volatile int32_t*>(FfxBase() + (VA_CurrentPopup - kImageBase));
    // Another native menu may have taken ownership before deferred cleanup reaches this object.
    if (*currentPopup == obj) *currentPopup = 0;
}

// ---------------------------------------------------------------------------
// 6) Spawn, poll, and close — main thread only, with the menu subsystem live
// ---------------------------------------------------------------------------
struct Menu { int obj; };

// Create the native list. A nonzero obj means success; 0 means a full pool or inactive context.
static inline Menu SpawnMenu() {
    // Encode labels once into DLL-owned memory.
    for (int i = 0; i < kRowCount; ++i) EncodeLabel(g_rows[i].labelAscii, g_labelBytes[i], kLabelCap);

    int obj = Alloc();                 // Alloc already called Reset: zeroed with +55=1, +62=1, and +63=1 defaults.
    if (!obj) return Menu{ 0 };        // Abort on a full pool; game code does not null-check Alloc's 0 result.

    WrW(obj, O_COUNT,    (int16_t)kRowCount);
    WrW(obj, O_PAGE,     (int16_t)kVisiblePage);
    WrW(obj, O_TOP,      0);
    WrW(obj, O_SELECTED, 0);           // Start at row 0; popups use 1, but lists are zero-based.
    WrB(obj, O_SLOTS,    1);           // Must be >=1. Reset leaves +66=0; at 0, confirm case 15 in 0x8B4460
                                       // loops 255 times and writes +44 through +44+510, overrunning the object.
    WrB(obj, O_CANCEL,   1);           // Do not close at the last row; Reset sets 1, and this makes it explicit.
    WrB(obj, O_GROUP62,  2);           // Draw on pumped layer 2, the popup/list convention. Byte-width write.
    WrB(obj, O_GROUP63,  1);           // Update on layer 1; Reset sets 1, and this makes it explicit. Byte-width write.
    // Never use a dword write here: it would clobber +64 (active) and +65.

    WrP(obj, O_ENTER,     (void*)0);                                   // No entry callback.
    WrP(obj, O_UPDATE,    (void*)(uintptr_t)&OurListInputCb);          // Custom navigation that never writes +69 or +40.
    WrP(obj, O_DRAW,      (void*)(uintptr_t)&OurDraw);                 // DLL-owned draw callback.
    WrP(obj, O_AUX,       (void*)(uintptr_t)&OurAux);                  // Close callback must return nonzero.
    WrP(obj, O_VALIDATOR, (void*)0);                                   // No validator; custom input owns confirm.

    g_ourClosed = 0; g_ourResult = 0;
    g_menuAnimStart = g_ourDrawCalls; g_easedRowY = -1.0f;   // Restart entry animation and cursor easing.
    Register(obj);                     // Sets +64=1 active and +40=0 state synchronously on the main thread.
    // Spawn itself does not claim unk_23CC120; active callers coordinate modal ownership around publication/cleanup.
    return Menu{ obj };
}

// Per-frame poll state. Call after the pump on the main thread.
enum PollResult { POLL_NAV, POLL_CONFIRM, POLL_CANCEL };
struct Poll { PollResult what; int row; };

// Read this frame's action. row is either the live cursor row or the confirmed row.
static inline Poll PollMenu(const Menu& m) {
    const int sel = RdW(m.obj, O_SELECTED);
    if (g_ourClosed) {                                   // The custom input callback signaled confirm/cancel.
        if (g_ourResult >= 0) return Poll{ POLL_CONFIRM, g_ourResult };
        return Poll{ POLL_CANCEL, sel };
    }
    return Poll{ POLL_NAV, sel };                        // Navigation remains active at cursor row sel.
}

// Dispatch a confirmed row through the decoupled runtime-action bridge.
static inline void DispatchConfirm(int row) {
    if (row < 0 || row >= kRowCount) return;
    const Row& R = g_rows[row];
    if (R.kind == EDGE) { if (g_bridge.onEdge)     g_bridge.onEdge(R.action); }
    else                { if (g_bridge.onHeldEnter) g_bridge.onHeldEnter(R.action); }
}

// Request close by setting +65. On the next update tick, 0x8A91E0 invokes +12 once more, then +20
// (OurAux, nonzero), and finally +24 or default finalizer 0x8AA3A0. The finalizer calls Reset,
// clears active at +64, and releases the slot.
static inline void CloseMenu(Menu& m) {
    if (!m.obj) return;
    WrB(m.obj, 65, 1);                  // +65 is the close flag consumed by the next update tick.
    m.obj = 0;
    g_ourClosed = 0;                    // Clear the signal; SpawnMenu also resets it.
}

// ---------------------------------------------------------------------------
// 7) ACTIVE INTEGRATION CONTRACT
// ---------------------------------------------------------------------------
//  • `dllmain.cpp` owns the primary F7 hotkey wire. Arena+ and Maechen own their
//    separate native-menu objects and reuse the helpers relevant to those surfaces.
//  • Call SpawnMenu on the main thread only while g_FFX_MenuSubsystemActive
//    (VA 0x13407E4) is set and the player is in a menu/field. Outside that context,
//    the allocation can become an unpumped zombie object.
//  • Main-thread per-frame flow:
//        Poll p = PollMenu(menu);
//        if (p.what == POLL_CONFIRM) {
//            DispatchConfirm(p.row);
//            // EDGE: execute once and let the owner decide whether to reopen or remain.
//            // HELD: enter a continuous mode, read pad axes through 0x8BE440/0x8BE480,
//            //       apply the owner's per-frame action, and leave on cancel.
//        } else if (p.what == POLL_CANCEL) {
//            CloseMenu(menu);
//        }
//  • SetBridge({onEdge,onHeldEnter}) supplies actions without coupling this header to
//    their implementation. Historical Aurora evidence found that applying an override
//    in the tick after actor update, within the same frame, avoids out-of-frame jitter;
//    any HELD action therefore belongs in its owner's main-thread tick.
//  • Guardrails: this header must not write camera RAM at 0xD378A0; it only dispatches
//    through the bridge. Preserve one owned menu object at a time within the 32-slot pool,
//    and use a disposable save for the first separately authorized RT2 case.
// ---------------------------------------------------------------------------

} // namespace NativeMenu
