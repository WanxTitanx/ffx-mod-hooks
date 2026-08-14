// FFX Hooks shared memory block — ffx-hooks.dll <-> FfxHooks_Service (C#)
// Memory-mapped file: DLL creates on attach; editor opens and reads/writes.
// Same pattern as FFXProbeBlock (Local\FFXProbeBlock_v1).
#ifndef FFX_HOOKS_BLOCK_H
#define FFX_HOOKS_BLOCK_H
#include <stdint.h>

#define FFXHOOKS_MMF_NAME  "Local\\FFXHooksBlock_v1"
#define FFXHOOKS_MAGIC     0x48584646u  /* 'FFXH' */
#define FFXHOOKS_VERSION   1u

#pragma pack(push, 1)
typedef struct FFXHooksBlock {
    uint32_t magic;    /* FFXHOOKS_MAGIC once DLL attached */
    uint32_t version;  /* FFXHOOKS_VERSION */

    /* ── Music override (Fase 1) ────────────────────────────────────────── */
    /* Editor sets musicOverrideTrackIndex (0..N) and increments musicSeq.  */
    /* Hook intercepts next PlayTrackByIndex call and substitutes the index. */
    volatile int32_t  musicOverrideTrackIndex; /* -1 = no override */
    volatile uint32_t musicSeq;                /* editor increments to apply */

    /* ── Element flags extension (Fase 2) ──────────────────────────────── */
    /* bits: 0x20=Earth  0x40=Wind  0x80=Dark                               */
    volatile uint8_t  elementFlagsExt;

    uint8_t pad[239]; /* pad to 256 bytes */
} FFXHooksBlock;
#pragma pack(pop)

/* 4+4+4+4+1 = 17 data bytes + 239 pad = 256 */
static_assert(sizeof(FFXHooksBlock) == 256, "FFXHooksBlock must be 256 bytes");

#endif /* FFX_HOOKS_BLOCK_H */
