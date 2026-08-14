// FFX DINPUT8 in-process probe — shared Command Block layout (module <-> editor).
// Memory-mapped file: the module (in-process, main thread via GetDeviceState hook) executes;
// the editor (external) arms commands and reads results. STANDBY by default.
#ifndef FFX_PROBE_BLOCK_H
#define FFX_PROBE_BLOCK_H
#include <stdint.h>

#define FFXPROBE_MMF_NAME "Local\\FFXProbeBlock_v1"
#define FFXPROBE_MAGIC    0x46585042u  /* 'FXPB' */
#define FFXPROBE_VERSION  1u

/* opcodes */
#define FFXPROBE_OP_NOP   0u
#define FFXPROBE_OP_READ  1u   /* buf <- [addr..addr+len)            */
#define FFXPROBE_OP_WRITE 2u   /* [addr..addr+len) <- buf            */
#define FFXPROBE_OP_CALL  3u   /* ret = ((fn)addr)(arg0,arg1,arg2)  */
#define FFXPROBE_OP_FORCEBATTLE 4u /* atomic (1 frame): set scripted-encounter flags + call
                                      MsBattleEncountExe(field=arg0, group=arg1, formation=arg2) */
#define FFXPROBE_OP_SETINPUT    5u /* PERSISTENT: latch forged input. arg0=dir/button mask (FFXIN_*),
                                      arg1=raw DIK keycode A (or 0), arg2=raw DIK keycode B (or 0).
                                      Hook overlays these onto the keyboard/joystick state every frame
                                      until changed. Set arg0=arg1=arg2=0 to release. */
#define FFXPROBE_OP_TEXLOG_START 6u /* install (once) the LOAD-TIME texture inline hook on
                                       FFX_Ps3Data_BuildTextureSlotRecord_LoadTime (RVA 0x2451F0), reset the
                                       ring, and ARM logging. Records (frame, full texture path) per load. */
#define FFXPROBE_OP_TEXLOG_STOP  7u /* disarm logging (hook stays installed but inert). ret = record count. */
#define FFXPROBE_OP_TEXLOG_DRAIN 8u /* copy texlog records [arg0 .. arg0+4) into buf (4 x 128B);
                                       ret = #records copied this call, len = total record count. */
#define FFXPROBE_OP_SOUNDCMD     9u /* lab: call FFX sound command dispatcher on main thread.
                                       arg0=command id, arg1=param0, arg2=param1. */
#define FFXPROBE_OP_KETHRES_START 10u /* install hook on FFX_MagicHost_LinkResourceBufferRange
                                          (sub_72C570 @ RVA 0x32C570); arm ring log. */
#define FFXPROBE_OP_KETHRES_STOP  11u /* disarm logging. ret = record count. */
#define FFXPROBE_OP_KETHRES_DRAIN 12u /* copy records [arg0 .. arg0+8) into buf (8 x 16B);
                                          ret = # copied, len = total count. */
#define FFXPROBE_OP_KETHRES_SNAP  13u /* copy 512B from latest in-hook blob snap. arg0=byte offset
                                          (0..3584 step 512). ret=snap nz, len=total snap events. */
/* PPP draw inline EXE hook — RETIRED 2026-06-15 (crash pós-explosão; ver FFX_THUNDAFIRA_PPPDRAW_HOOK_CRASH). */
#define FFXPROBE_PPPDRAW_RETIRED  1u
#define FFXPROBE_ERR_PPPDRAW_RETIRED 0x50505044u /* 'PPPD' */
#define FFXPROBE_OP_PPPDRAW_START 14u /* retired: was sub_71B980 tint hook */
#define FFXPROBE_OP_PPPDRAW_STOP  15u /* retired */
#define FFXPROBE_OP_PPPDRAW_DRAIN 16u /* retired */
#define FFXPROBE_OP_U1_START 17u
#define FFXPROBE_OP_U1_STOP  18u
#define FFXPROBE_OP_U1_DRAIN 19u
/* kethres record (16 bytes): u32 frame; u32 bufPtr; u32 size; u32 handlePtr */
/* pppdraw record (32 bytes): frame,a0,a1,a2,a3(tintStruct), tintR,tintG,tintB from a3+4 */
/* latest snap meta in ret/len/addr on SNAP: addr=bufPtr, len=snapCount, ret=nonZero bytes in snap */
/* input intent mask (FFXPROBE_OP_SETINPUT arg0) */
#define FFXIN_UP      0x01u
#define FFXIN_DOWN    0x02u
#define FFXIN_LEFT    0x04u
#define FFXIN_RIGHT   0x08u
#define FFXIN_CONFIRM 0x10u
#define FFXIN_CANCEL  0x20u
#define FFXIN_MENU    0x40u
#define FFXIN_JOY     0x100u /* also forge the joystick axes/POV (for controller users) */

/* calling conventions for CALL */
#define FFXPROBE_ABI_CDECL_I    1u  /* int __cdecl(int)             */
#define FFXPROBE_ABI_CDECL_II   2u  /* int __cdecl(int,int)         */
#define FFXPROBE_ABI_CDECL_III  3u  /* int __cdecl(int,int,int)     */
#define FFXPROBE_ABI_CDECL_IIF  4u  /* int __cdecl(int,int,float)   */
#define FFXPROBE_ABI_STDCALL_II 5u  /* int __stdcall(int,int)       */

/* status */
#define FFXPROBE_ST_IDLE  0u
#define FFXPROBE_ST_OK    1u
#define FFXPROBE_ST_ERR   2u

#pragma pack(push, 1)
typedef struct {
    volatile uint32_t magic;       /* FFXPROBE_MAGIC once the module attached     */
    volatile uint32_t version;     /* FFXPROBE_VERSION                            */
    volatile uint32_t moduleBase;  /* FFX.exe runtime base (editor: addr=base+RVA)*/
    volatile uint32_t heartbeat;   /* ++ every main-thread frame (proof of life)  */
    volatile uint32_t hooked;      /* 1 once GetDeviceState hook is live          */
    volatile uint32_t seq;         /* editor increments to ARM a command          */
    volatile uint32_t ackSeq;      /* module sets = seq when the command finished */
    volatile uint32_t opcode;      /* FFXPROBE_OP_*                               */
    volatile uint32_t status;      /* FFXPROBE_ST_*                               */
    volatile uint32_t addr;        /* absolute runtime target address             */
    volatile uint32_t len;         /* READ/WRITE byte count (clamped to buf)      */
    volatile uint32_t abi;         /* FFXPROBE_ABI_* for CALL                     */
    volatile uint32_t arg0;        /* CALL arg0 (int)                             */
    volatile uint32_t arg1;        /* CALL arg1 (int)                             */
    volatile uint32_t arg2;        /* CALL arg2 (int, or float bit-pattern)       */
    volatile int32_t  ret;         /* CALL return value                           */
    volatile uint32_t errCode;     /* SEH exception code when status == ERR       */
    volatile uint8_t  buf[512];    /* READ/WRITE payload                          */
} FFXProbeBlock;

typedef struct {
    uint32_t frame;
    uint32_t family;
    uint32_t context;
    uint32_t program;
    uint32_t slot;
    uint8_t  programBytes[32];
} FFXProbeU1Record;
typedef char FFXProbeU1RecordSizeCheck[(sizeof(FFXProbeU1Record) == 52u) ? 1 : -1];
#pragma pack(pop)

#endif /* FFX_PROBE_BLOCK_H */
