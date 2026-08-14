#include "SphereGridFullGridCompilerHook.h"
#include "GridTeachHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#endif

namespace FfxHooks {
namespace {

/* ═══════════════════════════════════════════════════════════════════════════
 * SGM 861 INCIDENT REGISTRY — append rows; NEVER delete.
 * Sources: agent transcript 36b307de, docs/reverse/FFX_SPHEREGRID_861_GHOST_CRASH_DEEP_DIVE,
 * docs/ai/SGM_861_COMPLETE_STATUS_2026-06-22.md, SESSION_HANDOFF.md, git 680411a8/93d727ee.
 * Runtime: CRASH-RECORD in %TEMP%\ffx-hooks.log + %TEMP%\ffx-hooks-sgm-incidents.log
 *
 * ── STATUS 2026-06-23: MOD PAUSED / DISABLED ───────────────────────────────
 * Halyson request: stop all SGM 861 runtime attempts; restore vanilla Sphere Grid.
 * dllmain SphereGridFullGridCompilerEnabled() hard-returns false (no install).
 * Research source kept; re-arm only via FFXHOOKS_SG_861_LAB_OVERRIDE + doc review.
 * Handoff: docs/ai/HANDOFF_SGM_861_DISABLED_2026-06-23.md
 *
 * ── VERSION CHRONOLOGY v1.1 → v1.44 (FullGridCompiler) ─────────────────────
 * v1.1  | sidecar seed + compile-write | RT2-03 FAIL grid vazia/L3 (asset path, not OOB yet)
 * v1.2  | A45570 layout dump rec[858..862] | observe OK; ghost persists
 * v1.3  | bump flags r16/r48/r8; hook 6392A0 clamp | partial bump → OOB; per-frame clamp → ghost
 * v1.4  | never-shrink bump rule | REGRESSÃO boot: shrink 7065→7056 @ Square logo (SG-C011)
 * v1.4.1| bumped>size guard only | logo OK; slot860ok still 0 when alloc already 41328
 * v1.5  | NoteBatchAllocSize pass-through | mark ready on 861-slot alloc without shrink
 * v1.6–8| clamp experiments A54860/A49590 | GPU corrupt; exit-only clamp proven later (v1.28)
 * v1.9  | reset batch flags @ A45570 entry | wiped pre-layout bumps (SG-F001)
 * v1.10 | fix flag reset; path 3392/78960 bump | rebound still blocked; ghost + FLOAT-SCAN
 * v1.11 | master=base+CCC81C (not deref); ALLOW w/o rebound | kSlack=128 → boot shrink (SG-C011)
 * v1.12 | g_sgBatchArmed; remove 3104 bump; path slack ±16 | logo OK; A54660 disarm + A54860 clamp bugs
 * v1.13 | arm bumps @ boot manifest; no disarm A54660 | 78960 bump OK; ABMAP writer table still stale
 * v1.14 | PatchAbmapMenuWriterTable @ menu+0xF858 | early A54860 corrupt if slot860ok=0 (SG-C005)
 * v1.15 | defer/skip A54860; draw w/ pos-only bump | ghost ball; activation OOB persists (SG-C004)
 * v1.16 | PurgeStaleBatchPointers 320KB EVERY draw | severe lag; replaced=0 (SG-F004)
 * v1.17 | event-only purge; single writer patch | tracked 41280 not 41252 → purge never fires
 * v1.18 | MigrateBatch860 + hook 6859E0 global | SOFTLOCK + inverted migrate (SG-C007/C008)
 * v1.19 | remove upload hook; stale capture | menu OK; original OOB/confetti returns (SG-F006)
 * v1.20 | ForceWriterRedirect; remove SKIP; migrate stale | patched wrong table (a1=payload) (SG-F025)
 * v1.21 | PatchCaptureCtxDrawWriters batchRoot+0x6C | IDA root cause; OOB @ boot 10872E50 persists
 * v1.22 | ResolveDrawWriterFromA2; 639180 resolve | ctx/root=0 in log; boot arena untouched
 * v1.23 | PurgeBootArena every 7F4900; migrate pre-draw | ~15k logs/frame; cancel-movement AV (SG-C013)
 * v1.24 | patch slot≥860 only; throttle capture-writer log | col/uv stale @ first open → triangles
 * v1.25 | SyncAllSgWritersAtLayout; DiscoverBootBatchPtrs | boot-pos late; SKIP if stale writer
 * v1.26–27| harvest boot; master scan 4096; PIN latest | drift reduced; producer still hidden
 * v1.28 | SKIP draw860 + exit-only clamp A54660 | passive exit OK — baseline (no activate test)
 * v1.29 | SKIP n861≤−861/−862; F858 +0/+4 only; ALLOC-TRACE 942B60 | dual-buffer proven; OOB post-A54860
 * v1.30 | boot bump + conditional draw860 | GPU REGRESSION FLOAT-HIT boot+860 (SG-F009)
 * v1.31 | DrawBatchSlot860Safe; capture redirect partial | col/uv stale → activation crash (SG-C009)
 * v1.32 | SKIP unconditional; PIN buffers; ScrubAllSgWritersOffBoot | SKIP OK; PRODUCER-WRITE persists
 * v1.33 | defer A54860 when 861 nodes; PIN @ layout sync | commit 680411a8; mitigates A54860 not activation
 * v1.34 | ALLOW-draw860 LAB (sg_draw_slot860.flag) | commit 93d727ee; exit OOB despite defer (SG-C006)
 * v1.35 | revert ALLOW on exit; SKIP always | passive exit OK; node invisible (ghost by design)
 * v1.36 | RepairSlot860Poison infer-scrub post-A48910 | AV invalid ptr during scrub (SG-C002)
 * v1.37 | VirtualQuery on scrub/migrate paths | guards OK; confetti persists; PRODUCER-WATCH wrong ptr
 * v1.38 | clamp A48910 + pre-upload 6859E0 guarded | activate crash: clamp 861→860 + nodeIdx=860 (SG-C003)
 * v1.39 | remove clamp A48910; keep A54660 only | G3 Def+4 PASS; G4/G5 FAIL bootPos=0 (SG-C010)
 * v1.40 | CRASH-RECORD + incidents.log + VEH classify | documentation runtime; no behavior fix
 * v1.41 | expand kCrashRegistry kFailedFixRegistry arrays | full chat history consolidated
 * v1.42 | DiscoverBootPos; ScrubBootPosOobTail; FreezeLatestToPinned | ALLOW-draw860 broke activate (SG-C014)
 * v1.43 | bootPos verified-only (41252 alloc); SKIP draw860 always; no pre-activate scrub | RT2 pending
 *
 * v1.43 | bootPos verified-only; SKIP draw860; no pre-activate scrub | fixed SG-C014 infinite activate; enter still laggy
 * v1.44 | enter-sync-once (g_writerSyncGen); lite PrepActivationSlot860 | ENTER LAG FIXED; G4/G5 still FAIL (SG-C015)
 * v1.45 | RedirectProducerPointersFull (menu+anim+stack); ALLOW draw860 during activate w/ writer redirect | RT2 pending
 * v1.46 | PatchCaptureBatchFromDrawA2: dual triple @ batch+0/+0x6C via ResolveCaptureCtx(a2,3); hook A5A800 | REGRESSION SG-C017
 * v1.47 | FIX skip -862 (all nodes); capture-batch-dual ONLY on activate+slot860; revert passive redirect | RT2 pending
 * v1.48 | FIX DrawBatchTargetsSlot860 (861→slot0 not 860); migrate 41252B vanilla→pinned before writer redirect | RT2 pending
 * v1.50 | Fase B scan menu+69768..71340; hooks A45930/A58080; PRODUCER-REDIRECT-WRITE @ 7F4900 activate+slot860 | G-visual OK; ghost+confetti persist (SG-C019)
 * v1.51 | STOP SKIP-draw860 when slot860ok; ForceDrawWriters @ every slot860 7F4900; scrub stale41252 OOB tail | REGRESSION: confetti+re-enter crash (SG-C020)
 * v1.52 | REVERT passive DRAW860; NEVER batch-migrate from boot; scrub boot OOB @ layout sync | RT2 pending
 * v1.53 | F1 RedirectDrawStoresIfStale; F2 detour 712330; F3 PurgeAllStale41252Pointers on bump | upload RVA wrong then fixed → SG-C022
 * v1.54 | disable upload detour (SG-C022 menu-open AV) | ALLOW-draw860-activate → black grid SG-C023
 * v1.55 | SKIP draw860 on activate too | A45930 skip → confetti; A58080 still corrupts SG-C024
 * v1.56 | SKIP A45930+712330 for 861 asset | soft-lock no exit UI → SG-C025
 * v1.57 | SKIP A58080+A47D50 globally; ForceAbmapPlacementAnimIdle every frame | stacked names SG-C026
 * v1.58 | activate-gated anim skip; vanilla A58080 while navigating | activate crash SG-C027 (post-gate race)
 * v1.59 | arm post-gate before scrub; 712330 skip unconditional; upload @2859E0 layoutGen>0 | RT2 pending
 * v1.60 | disable upload detour again; g_sgLayoutSyncActive guard | SG-C028 menu-open reentrant repair AV
 * v1.61 | block vanilla 712330 for 861; lite post-activate + lite pre-upload @2859E0 | SG-C029 exit crash
 * v1.62 | upload detour OFF again (2859E0 AV @+0xA); keep no-vanilla-712330 + lite exit scrub | SG-C030
 * v1.63 | full RepairSlot860 after node860 activate; post-860 exit scrub boost | SG-C001 exit partial
 * v1.64 | frame scrub gate post-860; boot OOB scrub @ A58080/A49590 | SG-C001 exit=0 post-activate
 * v1.65 | PRODUCER-REACT on write; END_CAPTURE @2392A0 lite scrub; no per-frame redirect | SG-C033 stack-scan AV
 * v1.66 | PreUploadGuard @2392A0; F1 writer redirect pre-skip-draw860; post-activate RepairSlot860 | SG-C034
 * v1.67 | F1 bounds @7F4900 (TR-style); re-enter prep @A45570; END_CAPTURE OFF | SG-C035 re-enter CDCD
 * v1.68 | revert F1-DRAW860; purge dying-pin re-enter; F1 writer redirect+SKIP860 | SG-C036
 * v1.69 | RedirectAllWritersToBoot @exit/re-enter; END_CAPTURE scrub-only | SG-C037 purge=0
 * v1.70 | revert v1.69 lag; DetachSessionHeapWriters exit-only (no undo) | SG-C038
 * v1.71 | defer g_sgBatchArmed until A45570 (fix login crash); revert DetachSession | SG-C039
 * v1.72 | ReleaseSessionHeapRefs @exit/re-enter; deferred sync on pin; exit scrub lite | SG-C040
 * v1.73 | reject false stale BF800000; exit scrub w/o Purge41252->pin; reset batch flags re-enter | SG-C041
 * v1.74 | capture-graph purge + session tombstones; clear pins @exit; offset writer remap | SG-C042
 * v1.75 | F1 inline store redirect @7F4C0F/7F5208/7F57E6; lift SKIP860 when sg_f1_inline.flag | SG-C043 target
 * v1.76 | F1 inline patches stay; SKIP860 always unless sg_f1_inline_draw860.flag (col/uv unpatched) | SG-C043
 *
 * ── USER-REPORTED SYMPTOMS (Halyson in-game, 2026-06-22) ───────────────────
 * Symptom                          | First   | Last    | Status v1.44 | Incident
 * GPU confetti / triângulos azuis  | v1.15+  | v1.44   | OPEN         | SG-C004
 * Crash ao sair do grid            | v1.20+  | v1.44   | OPEN         | SG-C001
 * Ativar esfera "infinitamente"    | v1.42   | v1.43   | FIXED v1.44  | SG-C014 (no A48910 SEH)
 * Def+4 aplica mas nó não "commita"| v1.42   | v1.43   | FIXED v1.44  | SG-C014
 * Lentidão ao ENTRAR no grid       | v1.16+  | v1.43   | FIXED v1.44  | SG-C016 / SG-F035
 * Nó 860 invisível (bola fantasma)| v1.28+  | v1.52   | OPEN         | SKIP-draw860 passive; v1.51 DRAW860 regressed (SG-C020)
 * Ativação sem confetti (target)   | —       | v1.46   | NOT YET      | needs producer redirect A48910
 * NENHUM node renderiza (regressão)| v1.46   | v1.47   | FIX v1.48    | SG-C017/C018 (wrong n861 map + no migrate41252)
 *
 * ── RT2 GATES (861-node Square overlay) — last verified v1.44 ───────────────
 * G1 Grid 861/882 enters menu          | PASS
 * G2 rec[860] layout/content valid     | PASS (Def+4 0x0009)
 * G3 Def+4 stat applies on activate    | PASS (v1.39+ no A48910 clamp)
 * G4 No GPU corruption post-activate   | FAIL — FLOAT 0x3F008081 @ 0x0EEF0Exx game-heap stale (v1.44 log)
 * G5 Exit menu without crash           | FAIL — FAULT eax=0x3F008081 rva=9435AA/2FB9D6 UploadBatches (v1.44)
 * G6 Activate without A48910 SEH crash | PASS v1.44 (was FAIL v1.42-43 SG-C014)
 * G7 Enter grid without severe lag     | PASS v1.44 (was FAIL v1.42-43 SG-C016)
 * G3-visual draw860 visible            | FAIL — SKIP-draw860 by design
 * RT2-03 (v1.1 compile-write)         | FAIL — sidecar/asset identity (SG-F017)
 *
 * ── CRASHES (SG-Cxxx) ─────────────────────────────────────────────────────
 * SG-C001 | v1.20+ | PARTIAL | exit 6859E0/9435AA | FAULT eax=0x3F008081; frame UploadBatches
 *         |        |         | heap free 94318A   | OOB slot860 → 41252B pos batch poisons freelist
 * SG-C002 | v1.36  | v1.37   | infer-scrub        | InferAndScrubBatchFromFloatHit AV invalid ptr
 * SG-C003 | v1.38  | v1.39   | A48910 activate    | clamp 861→860 during activate + nodeIdx=860 → OOB
 * SG-C004 | v1.15+ | OPEN    | A48910/A47D50      | PRODUCER-WRITE boot+860=0x3F008081; GPU confetti
 * SG-C005 | v1.14+ | v1.33+  | A54860 recompute   | OOB slot860 @ boot+860×48 (10872Exx); defer blocks path
 * SG-C006 | v1.34  | v1.35+  | 7F4900 draw860     | ALLOW-draw860 on A54660 exit → FLOAT-HIT w/ defer
 * SG-C007 | v1.18  | v1.19   | 6859E0 global hook | SOFTLOCK — UploadBatches hooked without g_sgBatchArmed
 * SG-C008 | v1.18  | v1.19   | MigrateBatch860    | memcpy boot slot860 garbage → latest (inverted migrate)
 * SG-C009 | v1.31  | v1.32   | A48910/A47D50      | capture redirect pos-only; col/uv stale → activation crash
 * SG-C010 | v1.39  | v1.42   | post-A48910 / exit | bootPos=0; producer OOB @10872E50; writer drift off pinned
 * SG-C011 | v1.4–12| v1.12   | boot 942B60 logo   | bump SHRINK (7065→7056, 3208→3107) → AV @542C23
 * SG-C013 | v1.23  | v1.24   | 7F4900 cancel move | capture-writer-patch EVERY frame corrupts col/uv mid-draw
 * SG-C014 | v1.42  | v1.43   | PrepSgActivation   | false bootPos 0x10860000 + ScrubBootPosOobTail -> A48910 SEH;
 *         |        |         |                    | status stays 00 -> infinite re-activate (FIXED v1.44)
 * SG-C015 | v1.44  | OPEN    | A48910/A47D50 GPU  | game-heap stale 41252B producer OOB slot860;
 *         |        |         |                    | FLOAT 0x3F008081 @ 0x0EEF0Exx/0x0E200Exx; bootPos=0; confetti persists
 * SG-C019 | v1.50  | OPEN    | enter+activate     | menu-scan redirect insufficient; SKIP-draw860 ghost;
 *         |        |         |                    | writer pos stale @7F4900 (16866DA0!=pin); poison @0DB1xxxx pre-activate
 * SG-C020 | v1.51  | OPEN    | enter+re-enter     | passive DRAW860 confetti all nodes; batch-migrate boot→pinned; crash @542C23
 *         |        |         |                    | writer pos stale @7F4900 (16866DA0!=pin); poison @0DB1xxxx pre-activate
 * SG-C016 | v1.42  | v1.44   | enter A45570       | per-alloc ABMAP-writer-patch x256 + PrepSg full re-sync each activate;
 *         |        |         |                    | severe enter lag (FIXED v1.44 enter-sync-once)
 *
 * ── FAILED FIXES / REGRESSIONS (SG-Fxxx) — do NOT repeat ─────────────────
 * SG-F001 | v1.9   | Reset batch861 flags at A45570 entry → wiped pre-layout bumps
 * SG-F002 | v1.14  | A54860 without buffer gates → slot860ok=0 early heap corrupt
 * SG-F003 | v1.15  | Draw slot860 pos bump only (not r16/r48/r8) → ghost ball
 * SG-F004 | v1.16  | PurgeStaleBatchPointers 320KB scan every DrawBatch frame → lag; replaced=0
 * SG-F005 | v1.17  | Stale detect used 41280 not 41252 → boot pos never flagged
 * SG-F006 | v1.19  | Removed 6859E0 hook after v1.18 softlock → OOB/confetti returned
 * SG-F007 | v1.20-28| Patch ABMAP writers @ menu+0xF858 only → dual-buffer; hidden producer OOB
 * SG-F008 | v1.29  | Patched writer table entry+8 → payload for 7F4900, NOT writer ptr
 * SG-F009 | v1.30  | Boot bump + re-enable draw860 → GPU corruption FLOAT-HIT boot+860
 * SG-F010 | v1.32  | SKIP draw860 + ScrubAllSgWritersOffBoot → PRODUCER-WRITE after-A48910 persists
 * SG-F011 | v1.11  | kSlack=128 path bump → false-positive boot allocs as 861-ready
 * SG-F012 | RE     | Menu 320KB DWORD stale-ptr-purge → replaced=0; producer on anim struct
 * SG-F013 | RE     | Suspect A45570 enter for 860-buffer alloc → lazy render @ first draw (IDA S7)
 * SG-F014 | RE     | ALLOC-TRACE size~860 only → missed 41252; false hit 880B eiichi_abmap load
 * SG-F015 | v1.37-38| BeginVanillaMenuCountClamp on A48910 → SG-C003 activate crash
 * SG-F016 | v1.39  | RepairSlot860 post-facto w/o boot discovery → bootPos=0; watch armed on pinned
 * SG-F017 | v1.1   | RT2-03 compile-write w/ sidecar → grid empty/unmovable/L3 (identity/hash gate)
 * SG-F018 | v1.2–3 | Remap draw index 860→859 → corrupts slot 859; path redraw on activate
 * SG-F019 | v1.3   | 6392A0 per-frame menu clamp 861→860 → ghost; partial r16/r8 not bumped
 * SG-F020 | v1.4   | Bump without never-shrink → 7065→7056 link batch shrink @ boot logo
 * SG-F021 | v1.6–8 | Clamp A54860/A49590 (not exit-only) → GPU batch corruption
 * SG-F022 | v1.12  | A54660 disarmed g_sgBatchArmed mid-session → bumps stopped; slot860ok=0
 * SG-F023 | v1.13  | A54860 TEMP clamp 861→860 on activation path → node 860 skipped
 * SG-F025 | v1.20  | Patch writer on 7F4900 a1 param → a1 is vertex payload not batch writer
 * SG-F026 | v1.21-22| capture-writer-patch ctx=0 root=0 → ResolveDrawWriterFromA2 failed silently
 * SG-F027 | v1.23  | PatchCaptureCtx + PurgeBoot on ALL 7F4900 calls → 15k logs; cancel AV
 * SG-F028 | v1.38  | pre-upload repair AFTER poison written → GPU already read 0x3F008081 same frame
 * SG-F029 | v1.42  | Scrub-only without redirect inside A48910/A47D50 → symptom not root (CONFIRMED v1.44 FAIL)
 * SG-F032 | v1.43  | Reset bootPos on layout rebind (ResetSgBatchRebindOnly) → bootPos=0 whole session
 * SG-F033 | v1.42-43| RepairSlot860Poison + full SyncAllSgWriters before EVERY activate → lag + heap stress
 * SG-F034 | v1.44  | PrepActivationSlot860 lite purge without producer hook → G4/G5 still FAIL (confetti)
 * SG-F035 | v1.42-43| PatchAbmapMenuWriterTable on every after-core-batch-alloc → enter lag (FIXED v1.44)
 *
 * ── PROVEN STABLE BASELINES (do not regress) ───────────────────────────────
 * v1.28: SKIP draw860 + exit-only clamp A54660 → passive exit OK
 * v1.33: defer-A54860 + SKIP draw860 + PIN layout buffers → partial stability
 * v1.35: SKIP draw860 always (ignore draw860 flag on exit path)
 * v1.39: NO menu clamp during A48910; clamp ONLY ExitUiFlush_Shim (A54660)
 *
 * ── MECHANICAL FACTS (constraints, not optional polish) ────────────────────
 * pos batch vanilla = 41252 (860×48−28), NOT 41280; slot860 @ +41280 = OOB by ~28 B into heap
 * poison float = 0x3F008081 (~1.0) → GPU confetti + freelist crash @9435AA
 * dual-buffer: ABMAP writers → pinned 41328B; hidden producer → stale 41252B on game heap OR boot ~10868D10
 * v1.44 log proof: PRODUCER-WATCH probe=1684F520/1C7E2410 (game heap stale); FLOAT @ 0x0EEF0E80 not 10872E50
 * 7F4900 n861=-861 → slot 860 via capture ctx [639180(a2,3)+0x94]+0x6C, NOT menu+0xF858 entry
 * menu+0xF858 stride 48: patch +0/+4 writers only; +8 = vertex payload passed as 7F4900 a1
 * enter A45570 does NOT alloc render batches; first draw lazy-alloc via 942B60/630670
 *
 * ── DO NOT REPEAT (one-line checklist) ────────────────────────────────────
 * ❌ hook 6859E0 UploadBatches globally   ❌ scan 320KB menu per frame
 * ❌ clamp menu count during A48910       ❌ patch F858 entry+8 as writer
 * ❌ shrink bump (bumped must be > size)  ❌ MigrateBatch860 from boot slot860
 * ❌ ALLOW draw860 on A54660 exit path    ❌ scrub-only without producer redirect
 * ✅ defer A54860 when 861 nodes          ✅ pin writer @ EVERY slot860 7F4900 when slot860ok (v1.51)
 * ✅ exit clamp ONLY @ A54660             ✅ enter-sync-once @ after-A45570 (v1.44)
 * ❌ SKIP draw860 when buffers ready      ❌ menu-region scan without per-draw writer patch (v1.50 SG-C019)
 * ❌ passive DRAW860 + boot batch-migrate (v1.51 SG-C020)
 * ❌ ALLOW draw860 on activate path (v1.54-55 SG-C023 black grid + exit crash)
 * ❌ SKIP A58080/A47D50 on ALL 861 sessions (v1.57 SG-C026 stacked name labels)
 * ❌ force-placement-idle during navigation (v1.57 zeros menu+71252 every frame)
 * ❌ 712330 skip gated on g_sgBatchArmed only (v1.58 SG-C027 pre-712330 vanilla re-poisons)
 * ❌ clear g_sgBlockProducerAnims before post-gate armed (v1.58 race → vanilla anim during scrub)
 * ❌ upload hook @2859E0 without layoutGen>0 guard (v1.53 SG-C022 menu-open AV)
 * ❌ RepairSlot860Poison inside UploadBatches during A45570 sync (v1.59 SG-C028 reentrant AV)
 * ❌ vanilla 712330 flush for 861 asset (v1.60 SG-C029 pre-712330 re-poisons boot+860)
 * ❌ ANY upload detour @2859E0 for 861 (v1.53/59/61 SG-C022/C028/C030 AV @+0xA)
 * ❌ wrong upload RVA 0x6859E0 absolute (v1.53 no-op detour)
 * ✅ SKIP draw860 passive; pin writer activate-only @ 7F4900
 * ✅ NEVER MigrateBatch860 from boot arena (SG-C008/C020)
 * ✅ skip producer anims ONLY during/post A48910 node860 (v1.58+)
 * ✅ upload pre-repair ONLY when layoutGen>0 (SG session entered via A45570)
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char* kFullGridCompilerVersion = "v1.80-PAUSED";

struct SgCrashIncidentDef {
    const char* id;
    const char* seenVer;
    const char* fixedVer;   /* "OPEN" | "PARTIAL" if mitigated not root-fixed */
    const char* site;
    const char* rootCause;
    const char* fixNote;
};

struct SgFailedFixDef {
    const char* id;
    const char* ver;
    const char* approach;
    const char* whyFailed;
};

static const SgCrashIncidentDef kCrashRegistry[] = {
    { "SG-C001", "v1.20", "PARTIAL", "exit/6859E0/9435AA",
      "OOB slot860 in 41252B pos batch; 0x3F008081 poisons heap; free/UploadBatches reads as pointer",
      "SKIP draw860; bump 861; RepairSlot860; exit clamp A54660; pre-upload WITH g_sgBatchArmed guard" },
    { "SG-C002", "v1.36", "v1.37", "activation/infer-scrub",
      "InferAndScrubBatchFromFloatHit wrote non-committed inferred batch base",
      "VirtualQuery + IsCommittedWritable on all scrub/migrate paths" },
    { "SG-C003", "v1.38", "v1.39", "A48910 activate",
      "BeginVanillaMenuCountClamp(861->860) during activate nodeIdx=860 -> indices 0..859 only",
      "NEVER clamp menu count in ActivateNode_Shim; clamp ONLY ExitUiFlush_Shim (A54660)" },
    { "SG-C004", "v1.15", "OPEN", "A48910/A47D50 activation",
      "Hidden producer writes slot860 into boot 41252B bypassing ABMAP writers and SKIP-draw860",
      "Redirect producer inside A48910/A47D50 to pinned 861 buffer; scrub alone insufficient" },
    { "SG-C005", "v1.14", "v1.33+", "A54860 recompute",
      "A54860 OOB-writes slot860 into boot arena (10872Exx) bypassing 7F4900 draw hook",
      "Defer A54860 when menu has 861 nodes until producer redirect proven" },
    { "SG-C006", "v1.34", "v1.35+", "7F4900 draw860 exit",
      "ALLOW-draw860 OOB-wrote boot+860 during A54660 exit despite defer-A54860",
      "SKIP draw860 unconditionally on exit; v1.42 allows draw ONLY during activation" },
    { "SG-C007", "v1.18", "v1.19", "6859E0 global",
      "UploadBatches hooked globally (runs outside SG) -> softlock / SEH every frame",
      "NEVER hook 6859E0 without g_sgBatchArmed; pre-upload repair only during SG session" },
    { "SG-C008", "v1.18", "v1.19", "MigrateBatch860",
      "MigrateBatch860 copied boot slot860 garbage (0x3F008081) into latest bumped buffer",
      "Do NOT memcpy from boot slot860; zero or populate from node record only" },
    { "SG-C009", "v1.31", "v1.32", "A48910/A47D50",
      "Capture ctx writer redirect patched pos only; col/uv stale -> activation crash",
      "Patch full writer triple (+0x0C/+0x14/+0x18) or SKIP draw860 until triple pinned" },
    { "SG-C010", "v1.39", "v1.42", "post-A48910 / exit",
      "bootPos never captured; producer OOB @10872E50; latest drifted off pinned; SKIP blocked pinned write",
      "Discover boot early; scrub boot OOB tail; freeze latest=pinned; draw860 ONLY during activation" },
    { "SG-C011", "v1.4", "v1.12", "boot/942B60 logo",
      "Bump heuristic SHRINK (7065->7056, 3208->3107) corrupted boot heap before SG opened",
      "NEVER shrink: bumped must be > requested size; g_sgBatchArmed; tight path slack +/-16" },
    { "SG-C013", "v1.23", "v1.24", "7F4900 cancel-move",
      "PatchCaptureCtxDrawWriters on EVERY 7F4900 frame corrupted col/uv mid-cancel animation",
      "Patch writers only for slot>=860 paths; throttle logs; no per-frame global patch" },
    { "SG-C014", "v1.42", "v1.44", "PrepSgActivation/A48910",
      "False bootPos 0x10860000; ScrubBootPosOobTail corrupted heap -> A48910 SEH; infinite re-activate",
      "bootPos verified-only; gate scrub; SKIP draw860; no layout boot reset (v1.43-44)" },
    { "SG-C015", "v1.44", "OPEN", "A48910/A47D50 GPU+exit",
      "Game-heap stale 41252B producer OOB slot860; FLOAT 0x3F008081 @ 0x0EEF0Exx; bootPos=0; confetti+9435AA",
      "Hook producer write inside A48910/A47D50 to pinned 41328B; purge/scrub alone insufficient (SG-F034)" },
    { "SG-C016", "v1.42", "v1.44", "enter/A45570",
      "Per-alloc ABMAP-writer-patch x256 + PrepSg full SyncAllSgWriters on every activate caused enter lag",
      "g_writerSyncGen: SyncAllSgWritersAtLayout once per layout; defer heavy patch from alloc hooks" },
    { "SG-C019", "v1.50", "OPEN", "enter+activate 7F4900",
      "Menu-region producer scan + SKIP-draw860; writer pos stale at draw (16866DA0!=pin); poison pre-activate @0DB1xxxx",
      "v1.51 tried draw860+writer-pin; v1.52 revert passive draw; never boot migrate" },
    { "SG-C020", "v1.51", "OPEN", "enter+re-enter A45570",
      "Passive DRAW860 confetti all nodes; batch-migrate boot 108603B0→pinned re-enter; crash SG-C011 @542C23",
      "v1.52: SKIP passive draw860; never MigrateBatch860 from boot; scrub boot OOB @ layout" },
    { "SG-C021", "v1.52", "PARTIAL", "712330/716850 anim capture",
      "Anim capture chain (animObj+48 list) writes slot860 bypassing menu+0xF858 producer scan",
      "v1.53: detour 712330 RedirectAnimCaptureList; F1 RedirectDrawStoresIfStale @ 7F4900 activate-only" },
    { "SG-C022", "v1.53", "v1.54", "2859E0 upload @ menu open",
      "Correct upload RVA 0x2859E0 made detour hit real UploadBatches; poison boot+860 read as ptr @+0xA AV",
      "v1.54: disable upload detour until pre-upload can guarantee no stale ptr reach GPU path" },
    { "SG-C023", "v1.54", "v1.55", "activate/exit A48910+A5BB70",
      "ALLOW-draw860-activate + stale writer pos (game heap) → black grid + SG-C001 @9435AA on save exit",
      "v1.55: SKIP draw860 on activate too; scrub+purge after-A48910 and before-A5BB70" },
    { "SG-C024", "v1.55", "v1.56", "A45930/712330 activate anim",
      "PlacementFx loop writes boot+860 and zeros pinned pos batch → black grid; poison survives to A5BB70 exit",
      "v1.56: SKIP A45930+712330 trampolines for 861-node asset; scrub on defer-A54860" },
    { "SG-C025", "v1.56", "v1.57", "A58080/A47D50 post-activate",
      "SKIP-A45930 left A58080 corrupting batches (confetti); placement anim never idle → no exit UI soft-lock",
      "v1.57: SKIP A58080+A47D50; ForceAbmapPlacementAnimIdle after A48910 node860" },
    { "SG-C026", "v1.57", "v1.58", "A58080 every frame navigate",
      "SKIP-A58080+force-placement-idle on ALL 861 sessions → menu+71252 band zeroed → stacked name labels",
      "v1.58: skip producer anims ONLY during/post A48910 node860; vanilla A58080 while navigating" },
    { "SG-C027", "v1.58", "v1.59", "post-A48910 activate / UploadBatches",
      "Race: block cleared before post-gate armed; 712330 skip required g_sgBatchArmed; upload hook OFF → poison 0x3F008081 @5435AA",
      "v1.59: arm post-gate+block before scrub; 712330 skip unconditional; upload @2859E0 when layoutGen>0" },
    { "SG-C028", "v1.59", "v1.60", "A45570 enter / UploadBatches reentrant",
      "RepairSlot860Poison in UploadBatches_Shim re-entered during SyncAllSgWritersAtLayout → AV in hook DLL @ NOT-FFX pc",
      "v1.60: disable upload detour; g_sgLayoutSyncActive blocks future pre-upload repair during layout sync" },
    { "SG-C029", "v1.60", "v1.61", "post-activate 712330 / exit UploadBatches",
      "pre-712330 ran vanilla flush after node860 activate → boot+860 poison → GPU confetti + SG-C001 @5435AA on exit",
      "v1.61: never call vanilla 712330 for 861 asset; LitePreUploadScrub @2859E0; lite post-activate repair" },
    { "SG-C030", "v1.61", "v1.62", "2859E0 upload detour SG session",
      "LitePreUploadScrub upload hook still AV @ pcRva=0x002859EA (+0xA) during grid enter/navigation after A54660",
      "v1.62: upload detour permanently OFF; exit scrub only via before-A54660 RepairSlot860Poison" },
    { "SG-C031", "v1.62", "v1.63", "post-860 activate exit",
      "LitePostActivateRepair insufficient; PRODUCER-WRITE during A48910 leaves poison for UploadBatches on exit",
      "v1.63: full RepairSlot860Poison after node860 activate; boosted exit scrub when g_sgNode860Activated" },
    { "SG-C032", "v1.63", "v1.64", "post-860 in-menu UploadBatches",
      "SG-C001 @5435AA with exit=0 — crash during render/upload after activate before A54660 exit scrub runs",
      "v1.64: g_sgPostActivateScrubGate frame scrub; boot OOB scrub @ A49590/A58080 each frame" },
    { "SG-C033", "v1.64", "v1.65", "after-A49590 RedirectProducerPointersFull",
      "ReplaceStalePtrScanStack + per-frame redirect caused AV @0x03E5A595 accessed=0x00571000 + 2-3s lag",
      "v1.65: drop stack scan + per-frame redirect; PRODUCER-REACT on slot860 write; END_CAPTURE @2392A0 scrub" },
    { "SG-C034", "v1.65", "v1.66", "post-860 activate UploadBatches",
      "PRODUCER-REACT on boot probe OK but SG-C001 @5435AA exit=0; END_CAPTURE lite scrub insufficient",
      "v1.66: PreUploadGuard @2392A0; F1 RedirectDrawStores pre-skip-draw860; RepairSlot860 post-activate" },
    { "SG-C035", "v1.66", "v1.67", "2nd enter A45570 post-sync",
      "PreUploadGuard @2392A0 + stale pinned ptrs -> AV WRITE @283889 to CDCDCDCD on re-enter",
      "v1.67: END_CAPTURE OFF; PrepSgReEnterSession before A45570; F1 bounds allow-draw860 when writer safe" },
    { "SG-C036", "v1.67", "v1.68", "F1-DRAW860 + re-enter-prep RedirectProducer",
      "F1-DRAW860 with pinned writer still confetti; re-enter-prep kept pin=196EF500 -> crash @542C7A",
      "v1.68: SKIP860 always; purge dying-pin from menu; F1 writer redirect only (no vanilla slot860)" },
    { "SG-C037", "v1.68", "v1.69", "re-enter purge-dying-pin replaced=0",
      "PurgeDyingSessionBatchesFromMenu found 0 refs; capture ctx still held session heap -> @542C7A",
      "v1.69: RedirectAllWritersToBoot @A54660 exit + re-enter; END_CAPTURE scrub-only" },
    { "SG-C038", "v1.69", "v1.70", "RedirectAllWritersToBoot + RepairSlot860 undo",
      "v1.69 patched 256 writers->boot then RepairSlot860 ScrubAllSgWritersOffBoot patched 256->pinned; END_CAPTURE every frame lag",
      "v1.70: DetachSessionHeapWriters (session-only->boot); END_CAPTURE OFF; no ApplyState RedirectProducer" },
    { "SG-C039", "v1.70", "v1.71", "g_sgBatchArmed @ DLL install",
      "Batch bump active during title/login (A53DE0) before A45570; AV NOT-FFX pc during BUMP-core-batch spam",
      "v1.71: defer g_sgBatchArmed until first A45570; revert DetachSessionHeapWriters to v1.68 exit path" },
    { "SG-C040", "v1.71", "v1.72", "re-enter bootPos=0 + exit RepairSlot860->pinned",
      "SyncAllSgWriters skipped (full=0); purge-dying noop; exit ScrubAllSgWritersOffBoot kept writers on dying heap -> UAF @167CF000",
      "v1.72: ReleaseSessionHeapRefs @exit+re-enter; RepairSlot860PoisonExit (no rebind->pin); deferred sync on pin" },
    { "SG-C041", "v1.72", "v1.73", "re-enter no-activate exit #2",
      "stalePos=BF800000 (float -1) harvested as batch ptr; LitePreUploadScrub Purge41252->pinned undid release-session -> AV @2857E0",
      "v1.73: IsPlausibleBatchHeapPtr gate; exit scrub without Purge41252; reset batch flags on re-enter prep" },
    { "SG-C042", "v1.73", "v1.74", "2nd exit VEH pcRva=0x006675CA",
      "UAF write accessed=0x1C4EE000 (interior 1st-session pin); menu purge=7 missed capture-graph refs; pins lived until re-enter",
      "v1.74: PurgeSessionHeapFromCaptureGraph + tombstones; ClearSessionPinGlobals @A54660; offset-preserving writer remap" },
    { "SG-C043", "v1.74", "v1.75", "F1 inline store redirect RT2",
      "Shim-only F1 cannot redirect stores inside 7F4900 when SKIP860 bypasses vanilla; producer 712330 still bypasses draw path",
      "v1.75: 5-byte inline patches NEG writer-prep + NEG/POS pos-store; allow vanilla draw860 when sg_f1_inline.flag" },
    { "SG-C044", "v1.75", "v1.76", "sg_f1_inline.flag enter grid instant crash",
      "Lift SKIP860 with only pos-store patches; OOB @0x3F5B7E col/uv POS + @0x3F54AA UV NEG; grid empty then AV",
      "v1.76: sg_f1_inline installs patches only; lift SKIP860 requires sg_f1_inline_draw860.flag (after col/uv patches)" },
    { "SG-C044", "v1.76", "v1.77", "F2 col/uv inline patches + auto lift SKIP860",
      "v1.76 blocked lift until col/uv; v1.77 adds 5 phase2 sites; sg_f1_inline.flag lifts SKIP when 8/8 install",
      "v1.77: F2-NEG-col-prep/store, NEG-uv, POS-col, POS-uv @0x3F5B7E; g_f1Phase2Complete gates lift" },
    { "SG-C045", "v1.77", "v1.78", "F1 inline 8/8 enter crash pcRva~stub",
      "cdecl helper inside 7F4900 fst/fxch path corrupts x87 st(0); ecx garbage AV in stub",
      "v1.78: fst/fld save-restore around all C calls in stubs; no auto lift SKIP860 (draw860 flag only)" },
    { "SG-C046", "v1.78", "v1.79", "F1 inline stubs still AV on all node draws",
      "FPU save insufficient; handmade cdecl stubs corrupt ecx every 7F4900 store path",
      "v1.79: inline patch install OFF by default; sg_f1_inline_install.flag required; sg_f1_inline.flag is noop" },
    { "SG-C047", "v1.79", "v1.80", "activate node860 confetti @5435AA eax=3F008081 pinPos=0",
      "Exit clearPins + writers on stale 01B72DDC; activate before re-pin; GPU reads poison slot860 OOB",
      "v1.80: EnsureActivate861PinnedBuffers pre-A48910; scrub interim stale on exit; PostActivate860Guard" },
};

static const SgFailedFixDef kFailedFixRegistry[] = {
    { "SG-F001", "v1.9", "Reset batch861 flags at A45570 entry",
      "Wiped bumps from pre-layout draw; rebound never ran" },
    { "SG-F002", "v1.14", "A54860 without buffer gates",
      "slot860ok=0 before bumps -> early heap corrupt before user activates" },
    { "SG-F003", "v1.15", "Draw slot860 with pos bump only (not full r16/r48/r8)",
      "Ghost ball — UV/col still vanilla-sized; activation crash persisted" },
    { "SG-F004", "v1.16", "PurgeStaleBatchPointers 320KB scan every DrawBatch frame",
      "Severe lag; replaced=0 — stale not in menu DWORD scan" },
    { "SG-F005", "v1.17", "Stale detect used 41280 not 41252",
      "Boot pos batch never registered as stale; purge never triggered" },
    { "SG-F006", "v1.19", "Removed 6859E0 hook after v1.18 softlock",
      "Menu OK again but OOB/confetti/crash returned — symptom fix not root fix" },
    { "SG-F007", "v1.20-28", "Patch ABMAP writers @ menu+0xF858 only",
      "Dual-buffer: writers point to 41328B but hidden producer still writes 41252B boot" },
    { "SG-F008", "v1.29", "Patch writer table entry+8",
      "entry+8 is vertex payload for 7F4900, NOT writer ptr — patch +0/+4 only" },
    { "SG-F009", "v1.30", "Boot bump + re-enable draw860",
      "GPU corruption regression; FLOAT-HIT @ boot+860 even with bump" },
    { "SG-F010", "v1.32", "SKIP draw860 + ScrubAllSgWritersOffBoot",
      "PRODUCER-WRITE after-A48910 boot+860=0x3F008081 still logged" },
    { "SG-F011", "v1.11", "kSlack=128 on path alloc bump heuristic",
      "False-positive'd boot allocs as 861-ready path batch; see SG-C011" },
    { "SG-F012", "RE", "Menu 320KB DWORD stale-ptr-purge",
      "replaced=0 always — producer cached on stack/anim struct not menu struct" },
    { "SG-F013", "RE", "Suspect enter path A45570 for 860-buffer alloc",
      "IDA Session 7: enter writes static menu only; render buffer lazy on first draw" },
    { "SG-F014", "RE", "ALLOC-TRACE size~860 heuristic only",
      "Missed 41252 render buffers; false hit A53DE0 880B eiichi_abmap file load" },
    { "SG-F015", "v1.37-38", "BeginVanillaMenuCountClamp on A48910 (copied exit fix)",
      "Exit clamp OK; same clamp during activate made nodeIdx 860 OOB -> SG-C003" },
    { "SG-F016", "v1.39", "RepairSlot860 post-facto without boot discovery",
      "bootPos=0 so boot+860 OOB unscubbed; PRODUCER-WATCH armed on pinned not boot" },
    { "SG-F017", "v1.1", "RT2-03 compile-write with sidecar seed",
      "Grid empty/unmovable/L3 despite node=860 patched — asset identity/hash gate" },
    { "SG-F018", "v1.2-3", "Remap draw index 860->859",
      "Corrupted slot 859; path redraw on activate spread corruption" },
    { "SG-F019", "v1.3", "6392A0 per-frame clamp + partial r16/r8 bump",
      "Ghost from clamp; OOB from incomplete triple bump" },
    { "SG-F020", "v1.4", "Bump without never-shrink guard",
      "7065->7056 link batch shrink @ Square logo -> SG-C011" },
    { "SG-F021", "v1.6-8", "Clamp A54860/A49590 (not exit-only)",
      "GPU batch corruption; exit-only A54660 clamp is the stable pattern" },
    { "SG-F022", "v1.12", "A54660 disarmed g_sgBatchArmed mid-session",
      "Bumps stopped; slot860ok=0; SKIP-draw860 ghost returned" },
    { "SG-F023", "v1.13", "A54860 TEMP clamp 861->860 on activation",
      "Node 860 not processed; 78960 bump OK but activation path still broken" },
    { "SG-F025", "v1.20", "Patch 7F4900 writer on a1 parameter",
      "a1 is vertex payload from A4FE40 not batch writer; ABMAP patch wrong table" },
    { "SG-F026", "v1.21-22", "PatchCaptureCtxDrawWriters with ctx=0 root=0",
      "ResolveDrawWriterFromA2 failed; boot arena 10872E50 still received OOB writes" },
    { "SG-F027", "v1.23", "PatchCaptureCtx + PurgeBoot on ALL 7F4900 calls",
      "~15k log lines/frame; cancel-movement AV -> SG-C013" },
    { "SG-F028", "v1.38", "pre-upload repair after A48910 poison write",
      "GPU UploadBatches read 0x3F008081 same frame before repair ran" },
    { "SG-F029", "v1.42", "Scrub-only without producer redirect in A48910/A47D50",
      "If RT2 G4/G5 still FAIL: must hook producer write site not post-facto scrub" },
    { "SG-F030", "v1.42", "DiscoverBootPos arena-scan + writer PtrInBootPosBatch fallback",
      "False bootPos 0x10860000; ScrubBootPosOobTail -> A48910 SEH infinite activate (SG-C014)" },
    { "SG-F031", "v1.42", "ALLOW-draw860-activate during A48910/A47D50",
      "Reverted v1.43 — activation must not call 7F4900 slot860 until producer redirect proven" },
    { "SG-F032", "v1.43", "Reset bootPos on layout rebind (ResetSgBatchRebindOnly)",
      "bootPos=0 entire session after enter; boot capture never succeeded" },
    { "SG-F033", "v1.42-43", "RepairSlot860Poison + full SyncAllSgWriters before every activate",
      "Enter/activate lag and heap stress; replaced by PrepActivationSlot860 lite (v1.44)" },
    { "SG-F034", "v1.44", "PrepActivationSlot860 + lite pre-upload without producer hook",
      "G4/G5 still FAIL in-game — confetti + 9435AA; confirms scrub/purge cannot fix SG-C004" },
    { "SG-F035", "v1.42-43", "PatchAbmapMenuWriterTable on every after-core-batch-alloc",
      "Severe lag first SG enter; fixed v1.44 defer to single SyncAllSgWritersAtLayout" },
    { "SG-F036", "v1.50", "Menu-region scan A45930/A58080 + PRODUCER-REDIRECT-WRITE activate-only",
      "G-visual PASS but ghost+confetti persist; SKIP-draw860 + writer pos stale bypasses menu scan (SG-C019)" },
    { "SG-F037", "v1.51", "Passive DRAW860 + PatchCaptureBatch on every slot860 draw",
      "Confetti all nodes; re-enter crash; boot migrate copied poison session 2 (SG-C020)" },
    { "SG-F038", "v1.53", "Upload detour @ wrong RVA 0x6859E0 (absolute not relative)",
      "Detour was no-op until fixed; then SG-C022 menu-open AV on real UploadBatches" },
    { "SG-F039", "v1.54-55", "ALLOW-draw860 during A48910 activate + stale writer on game heap",
      "Black grid on activate; SG-C001 @9435AA on save/exit despite F1/F2 redirect" },
    { "SG-F040", "v1.57", "SKIP A58080+A47D50 + ForceAbmapPlacementAnimIdle on every 861 frame",
      "Fixed soft-lock but zeroed menu+71252 anim band -> stacked character name labels (SG-C026)" },
    { "SG-F041", "v1.58", "712330 producer skip requires g_sgBatchArmed && post-gate",
      "A45930 skipped without armed check; 712330 fell through to pre-712330 vanilla -> poison (SG-C027)" },
    { "SG-F042", "v1.58", "Clear g_sgBlockProducerAnims before arming g_sgPostActivateAnimGate",
      "Race window between A48910 return and scrub: vanilla A58080/712330 during repair (SG-C027)" },
    { "SG-F043", "v1.54-58", "Upload hook disabled entirely after SG-C022",
      "Menu boot safe but poison reached UploadBatches unguarded on activate -> SG-C001 @5435AA" },
    { "SG-F044", "v1.59", "Full RepairSlot860Poison in UploadBatches_Shim when layoutGen>0",
      "Reentrant call during A45570 SyncAllSgWritersAtLayout -> menu-open AV NOT-FFX pc (SG-C028)" },
    { "SG-F045", "v1.60", "pre-712330 redirect then vanilla FlushCaptureBatch48 for 861 asset",
      "ShouldSkip gate expired/missed; vanilla wrote boot+860 OOB -> confetti + exit SG-C001 (SG-C029)" },
    { "SG-F046", "v1.61", "LitePreUploadScrub upload detour @2859E0 with layoutGen>0 guard",
      "Still AV @2859EA during grid session; lite scrub insufficient — any upload hook is toxic (SG-C030)" },
    { "SG-F047", "v1.64", "g_sgPostActivateScrubGate 240 frames + pre-A58080 scrub + RedirectProducer every frame",
      "2-3s lag after activate; confetti+SG-C001 persist — scrub post-facto cannot beat same-frame upload" },
    { "SG-F048", "v1.65", "PRODUCER-REACT + END_CAPTURE LitePreUploadScrub only",
      "Boot PRODUCER-REACT logged; SG-C001 @5435AA exit=0 — poison reached heap free before upload guard" },
    { "SG-F049", "v1.66", "PreUploadGuard @2392A0 every frame during SG session",
      "1st enter confetti passive; 2nd enter crash CDCDCDCD @283889 — scrub at upload too late + stale pin" },
    { "SG-F050", "v1.67", "F1-DRAW860 allow vanilla 7F4900 when writer pos pinned",
      "F1-DRAW860 logged but boot poison 24×0x3F008081 + explosion; vanilla draw not safe for slot860" },
    { "SG-F051", "v1.69", "END_CAPTURE @2392A0 ScrubBootPosOobTail every upload",
      "Absurd lag enter/navigate/activate; confetti+SG-C001 persist — per-frame scrub toxic (SG-C038)" },
    { "SG-F052", "v1.69", "RedirectAllWritersToBoot(256) + RepairSlot860 ScrubAllSgWritersOffBoot(256)",
      "512 unconditional writer patches on exit; boot redirect immediately undone to pinned" },
    { "SG-F053", "v1.70", "DetachSessionHeapWriters + PatchMenu2DWriterIfSessionHeap",
      "Reverted v1.71 — insufficient benefit; login crash unrelated" },
    { "SG-F054", "v1.72", "HarvestStaleBatchPointersFromWriters + PurgeAllStale41252Pointers on exit",
      "RememberWriterStalePtr stored 0xBF800000 float as stalePos; exit purge redirected garbage<->pinned" },
    { "SG-F055", "v1.72-73", "ReleaseSessionHeapRefs without capture-graph scan or exit pin clear",
      "purge-dying-pin replaced=7 but interior ptr 1C4EE000 survived in capture ctx until 2nd A54660 UAF" },
};

static const char* g_activeSgShim = "idle";
static bool g_sgActivationActive = false;  /* defined early; also used by activation shims below */
static bool g_sgBlockProducerAnims = false; /* true only during A48910 node860 commit */
static bool g_sgLayoutSyncActive = false; /* SyncAllSgWritersAtLayout in progress — block upload repair */
static bool g_sgNode860Activated = false;   /* node860 committed this SG session — boost exit scrub */
static int g_sgPostActivateAnimGate = 0;    /* skip-count for A45930/A58080/712330 after activate */
static constexpr int kSgPostActivateAnimGateFrames = 128;

static void RecordCrashIncident(const char* incidentId, const char* site, const char* detail);
static const SgCrashIncidentDef* LookupCrashIncident(const char* incidentId);
static const int kVanillaNodeCapacity = 860;
static const int kVanillaLinkCapacity = 881;
static const int kMaxExtraNodes = 256;
static const int kMaxExtraLinks = 256;
static const uintptr_t kStateLinkOffset = 0xA00u;
static const uintptr_t kStateCursorOffset = 0xF00u;
static const int kCursorSlots = 7;

static const uintptr_t kAbmapMenuNodeArrayOffset = 0x808u;
static const uintptr_t kAbmapMenuNodeStride = 40u;
static const uintptr_t kAbmapMenuNodeContentWordOffset = 6u;
static const uintptr_t kAbmapMenuNodeStatusByteOffset = 33u;
// sub_A51340 / sub_A4FE40: per-content-type Menu2D writer triple @ menu+63528, stride 48.
static const uintptr_t kAbmapMenuWriterTableOffset = 0xF858u;
static const uintptr_t kAbmapMenuWriterStride = 48u;
static const int kAbmapMenuWriterEntries = 128;
// Placement/anim band (menu+71248 frame counter @ 0x11668): producer caches stale pos ptr here.
static const uintptr_t kAbmapMenuAnimBandOffset = 0x10000u;
static const size_t kAbmapMenuAnimBandLen = 0x8000u;
// FFX_Abmap_UpdateRuntimeLinkGeometry (A5A800): link table @ +43016, geom write cursor @ +71272.
static const uintptr_t kAbmapMenuLinkArrayOffset = 43016u;
static const uintptr_t kAbmapMenuLinkGeomCursorOffset = 71272u;
// IDA 2026-06-20: placement/anim/projection/callback band (master plan §5).
static const uintptr_t kAbmapMenuPlacementSlotBase = 69768u;
static const uintptr_t kAbmapMenuPlacementSlotStride = 80u;
static const int kAbmapMenuPlacementSlotCount = 8;
static const uintptr_t kAbmapMenuProjectionBase = 70560u;
static const size_t kAbmapMenuProjectionLen = 384u;
static const uintptr_t kAbmapMenuAnimDispatchBase = 71252u;
static const size_t kAbmapMenuAnimDispatchLen = 92u;
static const uintptr_t kAbmapMenuCallbackChainBase = 71080u;
static const size_t kAbmapMenuCallbackChainLen = 264u;
static const uintptr_t kAbmapMenuLastActivateNodeOffset = 71336u;

struct ExtraNodeEntry {
    uint16_t nodeId;
    uint8_t content;
    uint8_t status;
    bool present;
};

struct ExtraLinkEntry {
    uint16_t linkId;
    uint8_t state;
    bool present;
};

struct SidecarData {
    char saveSha256[65];
    char layoutSha256[65];
    char contentsSha256[65];
    char profileKey[128];
    int vanillaNodes;
    int vanillaLinks;
    int assetNodes;
    int assetLinks;
    ExtraNodeEntry nodes[kMaxExtraNodes];
    int nodeCount;
    ExtraLinkEntry links[kMaxExtraLinks];
    int linkCount;
    bool loaded;
    bool hashMatched;
};

static bool g_installed = false;
static uintptr_t g_base = 0;
static SphereGridFullGridCompilerLogFn g_logFn = nullptr;
static SidecarData g_sidecar = {};
static char g_sidecarPath[MAX_PATH] = {};
static bool g_observeOnly = true;
static bool g_writeCompile = false;
static bool g_writeSidecar = false;
static bool g_clampVanillaLoop = false;
static bool g_skipHashCheck = false;
static bool g_compileRan = false;
static int g_trustedAssetNodes = 0;
static int g_trustedAssetLinks = 0;
// sub_7F4900 (IDA) writes slot n861_1 into THREE batch buffers: 16B color, 48B pos, 8B uv.
// ALL must be bumped 860->861 slots before slot 860 is safe. A 32B bump alone is NOT enough.
static bool g_batch861Ready16 = false;
static bool g_batch861Ready48 = false;
static bool g_batch861Ready8 = false;
static bool g_linkBatch882Ready = false;
static bool g_pathBatch861Ready = false;
static bool g_drawSlot860Experimental = false;
static bool g_sgF1InlineActive = false;
static bool g_sgF1InlineInstallPatches = false;
static bool g_sgF1InlineLiftSkip860 = false;
static bool g_f1Phase2Complete = false;
static bool g_exitUiFlushActive = false;
static void HookLog(const char* fmt, ...);
static inline volatile uint8_t* AbmapMenuStateBase();
static int PatchAbmapMenuWriterTable(const char* tag);
static void MigrateBatch860ToLatest(const char* tag);
static void RebindMenu2DNodeBatchBuffers(const char* tag);
static bool PatchMenu2DWriterBuffers(uint32_t writer);
static int PatchCaptureCtxDrawWriters(const char* tag);
static bool ResolveDrawWriterFromA2(int a2, uint32_t* outWriter, uint32_t* outPos);
static int ScrubAllSgWritersOffBoot(const char* tag);
static int PurgeBootArenaBatchPointers(const char* tag);
static void HarvestBootBatchPtrFromWriters(const char* tag);
static void SyncAllSgWritersAtLayout(const char* tag);
static void PrepActivationSlot860(const char* tag);
static int RedirectProducerPointersFull(const char* tag);
static void EnsurePinnedWritersForActivation(const char* tag);
static void EnsureActivate861PinnedBuffers(const char* tag);
static void PostActivate860Guard(const char* tag);
static void PurgeAllStale41252Pointers(const char* tag);
static void ClearSlot860InLatest(const char* tag);
static void RepairSlot860Poison(const char* tag);
static void RepairSlot860PoisonExit(const char* tag);
static int ReleaseSessionHeapRefs(const char* tag);
static void LitePreUploadScrub(const char* tag);
static void LitePreUploadScrubExit(const char* tag);
static void SanitizeStaleBatchGlobals(const char* tag);
static void LitePostActivateRepair(const char* tag);
static void PrepSgReEnterSession(const char* tag);
static void PreUploadGuard(const char* tag);
static void ReactToProducerSlot860Write(void* probe, const char* tag);
static void Scrub41252PosOobAt(void* base, const char* tag);
static bool RedirectDrawStoresIfStale(int a2, int slot, int n861, const char* tag);
static void ScanForCorruptFloat(const char* tag);
static void ScrubBootPosOobTail(const char* tag);
static void ScrubStalePosOobTail(const char* tag);
static void DiscoverBootPosForSession(const char* tag);
static void FreezeLatestToPinned();
static void DiscoverBootBatchPtrs(const char* tag);
static void HarvestBootBatchPtrFromWriters(const char* tag);
static void MaybeRegisterBootPosFromPtr(void* p, int reqSize);
static void RememberStalePtr(void** staleSlot, void* oldPtr, void* freshPtr);
static int ScrubPoisonFloatsInBuffer(void* base, size_t len);
static bool PopulateSlot860FromNodeRecord(const char* tag);
static bool IsCommittedWritable(const void* addr, size_t len);
static bool IsPlausibleBatchHeapPtr(uint32_t p);
static void ArmProducerSlot860Watch(const char* tag);
static void CheckProducerSlot860Write(const char* tag);
static bool IsSgBatchAllocTraceSize(int size);
static bool IsBootHeapRange(uint32_t p);
static void* g_latestPosBatch48 = nullptr;
static void* g_latestColorBatch16 = nullptr;
static void* g_latestUvBatch8 = nullptr;
static void* g_latestPath79051 = nullptr;
static void* g_stalePos86048 = nullptr;
static void* g_staleCol86016 = nullptr;
static void* g_staleUv8608 = nullptr;
// First 861-slot bump alloc per A45570 layout — redirect target; later reallocs must not drift writers.
static void* g_pinnedPos86148 = nullptr;
static void* g_pinnedCol86116 = nullptr;
static void* g_pinnedUv8618 = nullptr;
// Tombstoned session heaps — purge interior refs on exit/re-enter (SG-C042).
static const int kMaxSessionTombs = 3;
static void* g_sessionTombPos[kMaxSessionTombs] = {};
static void* g_sessionTombCol[kMaxSessionTombs] = {};
static void* g_sessionTombUv[kMaxSessionTombs] = {};
static int g_sessionTombCount = 0;
// Pre-hook boot heap batches (41252/13760/6880) — never captured by NoteLatestBatchPtr.
static void* g_bootPosBatch860 = nullptr;
static void* g_bootColBatch860 = nullptr;
static void* g_bootUvBatch860 = nullptr;
static bool g_bootPosVerified41252 = false;  /* true only after 942B60 alloc req=41252 or float-hit infer */
static uint32_t g_captureDrawWriterAddr = 0;
static bool g_batchPointersRebound = false;
static uint32_t g_layoutGen = 0;
static bool g_sgBatchArmed = false;  // armed only on A45570 SG enter — NOT at DLL install (SG-C039)
static uint32_t g_slot860WatchSnapshot = 0;
static bool g_slot860WatchArmed = false;
static bool g_populatedSlot860 = false;
static uint32_t g_writerSyncGen = 0;  /* SyncAllSgWritersAtLayout ran for this layout gen */

static const int kBatch861Bytes16 = (kVanillaNodeCapacity + 1) * 16;
static const int kBatch861Bytes48 = (kVanillaNodeCapacity + 1) * 48;
static const int kBatch861Bytes8 = (kVanillaNodeCapacity + 1) * 8;
static const int kVanillaBytes48 = kVanillaNodeCapacity * 48;
static const int kVanillaBytes16 = kVanillaNodeCapacity * 16;
static const int kVanillaBytes8 = kVanillaNodeCapacity * 8;
// Game allocs 41252 (=860*48-28 hdr), not 41280; must track exact size for stale detection.
static const int kGamePosBatch860 = 41252;
static const int kGameColBatch860 = 13760;  // 860*16
static const int kGameUvBatch860 = 6880;    // 860*8
static const size_t kCaptureObjScanLen = 0x200u;

static int BootPosBatchCap() {
    return g_trustedAssetNodes > kVanillaNodeCapacity ? kBatch861Bytes48 : kGamePosBatch860;
}

// Boot heap allocs stay at vanilla 41252/13760/6880 — never the bumped 861-slot size.
static int BootArenaPosCap() { return kGamePosBatch860; }
static int BootArenaColCap() { return kGameColBatch860; }
static int BootArenaUvCap() { return kGameUvBatch860; }
static int BootColBatchCap() {
    return g_trustedAssetNodes > kVanillaNodeCapacity ? kBatch861Bytes16 : kGameColBatch860;
}
static int BootUvBatchCap() {
    return g_trustedAssetNodes > kVanillaNodeCapacity ? kBatch861Bytes8 : kGameUvBatch860;
}

static bool DrawBatchSlot860Ready() {
    return g_batch861Ready48;  // pos 48B batch is the OOB/crash + ghost critical path
}

static bool DrawBatchSlot860FullReady() {
    return g_batch861Ready16 && g_batch861Ready48 && g_batch861Ready8;
}

// v1.9 reset all batch flags at A45570 entry wiped bumps from pre-layout draw → rebound never ran.
static void ResetSgBatchRebindOnly() {
    g_batchPointersRebound = false;
    g_populatedSlot860 = false;
    g_slot860WatchArmed = false;
    g_pinnedPos86148 = nullptr;
    g_pinnedCol86116 = nullptr;
    g_pinnedUv8618 = nullptr;
    g_captureDrawWriterAddr = 0;
    /* Boot/stale batch bases are process-lifetime — do NOT clear on layout rebind (v1.43 reset broke capture). */
}

static void* RedirectPosBatch() {
    return g_pinnedPos86148 ? g_pinnedPos86148 : g_latestPosBatch48;
}
static void* RedirectColBatch() {
    return g_pinnedCol86116 ? g_pinnedCol86116 : g_latestColorBatch16;
}
static void* RedirectUvBatch() {
    return g_pinnedUv8618 ? g_pinnedUv8618 : g_latestUvBatch8;
}

static void NoteLatestBatchPtr(int origSize, int req, int allocRv) {
    if (!allocRv) return;
    void* p = reinterpret_cast<void*>(static_cast<uintptr_t>(allocRv));
    MaybeRegisterBootPosFromPtr(p, origSize);
    if (origSize == kGamePosBatch860 && IsBootHeapRange(static_cast<uint32_t>(allocRv))) {
        g_bootPosBatch860 = p;
        g_bootPosVerified41252 = true;
        if (!g_stalePos86048) g_stalePos86048 = p;
    } else if ((origSize == kGamePosBatch860 || origSize == kVanillaBytes48) && req < kBatch861Bytes48) {
        const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        if (!IsBootHeapRange(addr) && !g_stalePos86048 && p != g_pinnedPos86148)
            g_stalePos86048 = p;
    } else if ((origSize == kGameColBatch860 || origSize == kVanillaBytes16) && req < kBatch861Bytes16) {
        const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        if (IsBootHeapRange(addr)) {
            if (!g_bootColBatch860) g_bootColBatch860 = p;
        } else if (!g_staleCol86016 && p != g_pinnedCol86116) {
            g_staleCol86016 = p;
        }
    } else if ((origSize == kGameUvBatch860 || origSize == kVanillaBytes8) && req < kBatch861Bytes8) {
        const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        if (IsBootHeapRange(addr)) {
            if (!g_bootUvBatch860) g_bootUvBatch860 = p;
        } else if (!g_staleUv8608 && p != g_pinnedUv8618) {
            g_staleUv8608 = p;
        }
    }
    if (!g_sgBatchArmed) return;
    if (req == kBatch861Bytes48) {
        if (g_pinnedPos86148) {
            RememberStalePtr(&g_stalePos86048, p, g_pinnedPos86148);
            g_latestPosBatch48 = g_pinnedPos86148;
            return;
        }
        if (g_bootPosBatch860 && p != g_bootPosBatch860 && !g_stalePos86048)
            g_stalePos86048 = g_bootPosBatch860;
        else if (g_latestPosBatch48 && g_latestPosBatch48 != p && !g_stalePos86048)
            g_stalePos86048 = g_latestPosBatch48;
        if (!g_pinnedPos86148) {
            g_pinnedPos86148 = p;
            HookLog("[ffx-hooks] FullGridCompiler PIN-pos86148 %p (req=%d gen=%u)",
                p, req, g_layoutGen);
            if (g_writerSyncGen != g_layoutGen && !g_sgLayoutSyncActive)
                SyncAllSgWritersAtLayout("after-pin-pos861");
        }
        g_latestPosBatch48 = p;
    } else if (req == kBatch861Bytes16) {
        if (g_pinnedCol86116) {
            RememberStalePtr(&g_staleCol86016, p, g_pinnedCol86116);
            g_latestColorBatch16 = g_pinnedCol86116;
            return;
        }
        if (g_latestColorBatch16 && g_latestColorBatch16 != p && !g_staleCol86016)
            g_staleCol86016 = g_latestColorBatch16;
        if (!g_pinnedCol86116) {
            g_pinnedCol86116 = p;
            HookLog("[ffx-hooks] FullGridCompiler PIN-col86116 %p (req=%d gen=%u)",
                p, req, g_layoutGen);
        }
        g_latestColorBatch16 = p;
    } else if (req == kBatch861Bytes8) {
        if (g_pinnedUv8618) {
            RememberStalePtr(&g_staleUv8608, p, g_pinnedUv8618);
            g_latestUvBatch8 = g_pinnedUv8618;
            return;
        }
        if (g_latestUvBatch8 && g_latestUvBatch8 != p && !g_staleUv8608)
            g_staleUv8608 = g_latestUvBatch8;
        if (!g_pinnedUv8618) {
            g_pinnedUv8618 = p;
            HookLog("[ffx-hooks] FullGridCompiler PIN-uv8618 %p (req=%d gen=%u)",
                p, req, g_layoutGen);
        }
        g_latestUvBatch8 = p;
    }
}

static void RememberWriterStalePtr(uint32_t writer) {
    if (writer < 0x10000u) return;
    __try {
        const uint32_t pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
        const uint32_t col = *reinterpret_cast<volatile uint32_t*>(writer + 0x14);
        const uint32_t uv = *reinterpret_cast<volatile uint32_t*>(writer + 0x18);
        const uint32_t latestPos = g_latestPosBatch48
            ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestPosBatch48)) : 0;
        const uint32_t latestCol = g_latestColorBatch16
            ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestColorBatch16)) : 0;
        const uint32_t latestUv = g_latestUvBatch8
            ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestUvBatch8)) : 0;
        if (pos >= 0x10000u && pos != latestPos && !g_stalePos86048 && IsPlausibleBatchHeapPtr(pos))
            g_stalePos86048 = reinterpret_cast<void*>(static_cast<uintptr_t>(pos));
        if (col >= 0x10000u && col != latestCol && !g_staleCol86016 && IsPlausibleBatchHeapPtr(col))
            g_staleCol86016 = reinterpret_cast<void*>(static_cast<uintptr_t>(col));
        if (uv >= 0x10000u && uv != latestUv && !g_staleUv8608 && IsPlausibleBatchHeapPtr(uv))
            g_staleUv8608 = reinterpret_cast<void*>(static_cast<uintptr_t>(uv));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void HarvestStaleBatchPointersFromWriters() {
    if (!g_sgBatchArmed) return;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return;
    __try {
        volatile uint8_t* table = menu + kAbmapMenuWriterTableOffset;
        for (int i = 0; i < kAbmapMenuWriterEntries; ++i) {
            volatile uint8_t* entry = table + static_cast<uintptr_t>(i) * kAbmapMenuWriterStride;
            for (int w = 0; w < 2; ++w) {
                const uint32_t writer = *reinterpret_cast<volatile uint32_t*>(entry + w * 4);
                RememberWriterStalePtr(writer);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int ReplaceStaleDwordScan(volatile uint8_t* base, size_t len, uint32_t stale, uint32_t fresh) {
    if (!base || !stale || !fresh || stale == fresh) return 0;
    int n = 0;
    for (size_t off = 0; off + 4 <= len; off += 4) {
        __try {
            volatile uint32_t* cell = reinterpret_cast<volatile uint32_t*>(base + off);
            if (*cell == stale) {
                *cell = fresh;
                ++n;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return n;
}

static int PurgeStaleBatchPointers(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return 0;
    if (!g_stalePos86048 && !g_staleCol86016 && !g_staleUv8608) {
        HarvestStaleBatchPointersFromWriters();
    }
    if (!g_stalePos86048 && !g_staleCol86016 && !g_staleUv8608) return 0;
    int total = 0;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        if (g_stalePos86048 && g_latestPosBatch48) {
            const uint32_t stale = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_stalePos86048));
            const uint32_t fresh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestPosBatch48));
            total += ReplaceStaleDwordScan(menu, 0x50000u, stale, fresh);
        }
        if (g_staleCol86016 && g_latestColorBatch16) {
            const uint32_t stale = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_staleCol86016));
            const uint32_t fresh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestColorBatch16));
            total += ReplaceStaleDwordScan(menu, 0x50000u, stale, fresh);
        }
        if (g_staleUv8608 && g_latestUvBatch8) {
            const uint32_t stale = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_staleUv8608));
            const uint32_t fresh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestUvBatch8));
            total += ReplaceStaleDwordScan(menu, 0x50000u, stale, fresh);
        }
    }
    if (total > 0) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s stale-ptr-purge replaced=%d stalePos=%p freshPos=%p "
                "staleCol=%p freshCol=%p staleUv=%p freshUv=%p",
                tag, total, g_stalePos86048, g_latestPosBatch48, g_staleCol86016, g_latestColorBatch16,
                g_staleUv8608, g_latestUvBatch8);
        }
    }
    return total;
}

static bool WriterPosMatchesLatest(uint32_t writer) {
    if (writer < 0x10000u || !g_latestPosBatch48) return true;
    __try {
        const uint32_t pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
        return pos == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestPosBatch48));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

static void EnsureBatch860Migrated(const char* tag) {
    (void)tag;
    // v1.31: disabled — boot slot860 is OOB garbage (0x3F008081); memcpy poisons latest buffers.
}

// v1.20: force writer triple to latest bumped buffers before 7F4900 writes slot>=860.
static bool ForceWriterRedirectToLatest(uint32_t writer, int slot, const char* tag) {
    if (writer < 0x10000u || !g_latestPosBatch48) return false;
    uint32_t pos = 0, col = 0, uv = 0;
    __try {
        pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
        col = *reinterpret_cast<volatile uint32_t*>(writer + 0x14);
        uv = *reinterpret_cast<volatile uint32_t*>(writer + 0x18);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    const uint32_t latestPos = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestPosBatch48));
    const uint32_t latestCol = g_latestColorBatch16
        ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestColorBatch16)) : 0;
    const uint32_t latestUv = g_latestUvBatch8
        ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_latestUvBatch8)) : 0;
    const bool needsPos = pos >= 0x10000u && pos != latestPos;
    const bool needsCol = latestCol && col >= 0x10000u && col != latestCol;
    const bool needsUv = latestUv && uv >= 0x10000u && uv != latestUv;
    if (!needsPos && !needsCol && !needsUv) return false;
    RememberWriterStalePtr(writer);
    PatchMenu2DWriterBuffers(writer);
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler redirect-producer %s slot=%d writer=0x%08X "
            "pos %08X->%08X col %08X->%08X uv %08X->%08X stalePos=%p latestPos=%p",
            tag, slot, writer, pos, latestPos, col, latestCol, uv, latestUv,
            g_stalePos86048, g_latestPosBatch48);
    }
    return true;
}

static void PrepSgActivationWriters(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity || !g_sgBatchArmed) return;
    FreezeLatestToPinned();
    if (g_writerSyncGen != g_layoutGen)
        SyncAllSgWritersAtLayout(tag);
    else
        EnsurePinnedWritersForActivation(tag);
}

static void PrepActivationSlot860(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    FreezeLatestToPinned();
    EnsurePinnedWritersForActivation(tag);
    RedirectProducerPointersFull(tag);
    PatchCaptureCtxDrawWriters(tag);
}

// sub_7F4900 negative-n861 path: capture ctx via sub_639180(a2,3), then [ctx+0x94]+0x6C writer.
static bool ResolveDrawWriterFromA2(int a2, uint32_t* outWriter, uint32_t* outPos) {
    if (!g_base || a2 < 0x10000) return false;
    uint32_t ctx = 0;
    __try {
        using ResolveCapFn = int(__cdecl*)(char*, int);
        auto fn = reinterpret_cast<ResolveCapFn>(g_base + RVA_FFX_MENU2D_RESOLVE_CAPTURE_CTX);
        char* str = reinterpret_cast<char*>(static_cast<uintptr_t>(a2) + 0x10);
        ctx = static_cast<uint32_t>(fn(str, 3));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (ctx < 0x10000u) return false;
    uint32_t batchRoot = 0;
    __try {
        batchRoot = *reinterpret_cast<volatile uint32_t*>(ctx + 0x94);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (batchRoot < 0x10000u) return false;
    const uint32_t writer = batchRoot + 0x6C;
    uint32_t pos = 0;
    __try {
        pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (outWriter) *outWriter = writer;
    if (outPos) *outPos = pos;
    return true;
}

static int TryPatchWriterCandidate(uint32_t writer, int* redirects) {
    if (writer < 0x10000u) return 0;
    uint32_t posBefore = 0;
    __try {
        posBefore = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!PatchMenu2DWriterBuffers(writer)) return 0;
    uint32_t posAfter = posBefore;
    __try {
        posAfter = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (posBefore >= 0x10000u && posBefore != posAfter && redirects)
        ++*redirects;
    return 1;
}

// IDA FFX_Menu_DrawQuadIndexed_structural @ 0x7F4900 (naming goal 2026-06-17):
// batch = [ResolveCaptureCtx(a2,3) + 0x94]; positive path uses batch+0x0C/+0x14/+0x18;
// negative n861 path (slot860 via -861) uses nested writer @ batch+0x6C (v22[3/5/6]).
static int PatchCaptureBatchDualFromCtx(uint32_t ctx, const char* tag) {
    if (ctx < 0x10000u || g_trustedAssetNodes <= kVanillaNodeCapacity) return 0;
    uint32_t batch = 0;
    __try {
        batch = *reinterpret_cast<volatile uint32_t*>(ctx + 0x94);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (batch < 0x10000u) return 0;
    int redirects = 0;
    int patched = 0;
    patched += TryPatchWriterCandidate(batch, &redirects);
    patched += TryPatchWriterCandidate(batch + 0x6Cu, &redirects);
    if (redirects > 0) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            uint32_t posA = 0, posB = 0;
            __try {
                posA = *reinterpret_cast<volatile uint32_t*>(batch + 0x0C);
                posB = *reinterpret_cast<volatile uint32_t*>(batch + 0x6C + 0x0C);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            HookLog("[ffx-hooks] FullGridCompiler %s capture-batch-dual ctx=0x%08X batch=0x%08X "
                "redirects=%d pos@+0=%08X pos@+6C=%08X pinPos=%p",
                tag, ctx, batch, redirects, posA, posB, RedirectPosBatch());
        }
    }
    return redirects;
}

static void PatchCaptureBatchFromDrawA2(int a2, const char* tag) {
    if (!g_base || a2 < 0x10000 || g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    uint32_t ctx = 0;
    __try {
        using ResolveCapFn = int(__cdecl*)(char*, int);
        auto fn = reinterpret_cast<ResolveCapFn>(g_base + RVA_FFX_MENU2D_RESOLVE_CAPTURE_CTX);
        char* str = reinterpret_cast<char*>(static_cast<uintptr_t>(a2) + 0x10);
        ctx = static_cast<uint32_t>(fn(str, 3));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    PatchCaptureBatchDualFromCtx(ctx, tag);
}

static int PatchCaptureCtxDrawWriters(const char* tag) {
    if (!g_sgBatchArmed || g_trustedAssetNodes <= kVanillaNodeCapacity || !g_base) return 0;
    if (!g_latestPosBatch48 && !g_latestColorBatch16 && !g_latestUvBatch8) return 0;
    const uint32_t master = static_cast<uint32_t>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
    int patched = 0;
    int redirects = 0;
    uint32_t captureCtx = 0;
    uint32_t batchRoot = 0;
    __try {
        captureCtx = *reinterpret_cast<volatile uint32_t*>(master + 0x94);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        captureCtx = 0;
    }
    if (captureCtx >= 0x10000u) {
        __try {
            batchRoot = *reinterpret_cast<volatile uint32_t*>(captureCtx + 0x94);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            batchRoot = 0;
        }
        if (batchRoot >= 0x10000u) {
            patched += TryPatchWriterCandidate(batchRoot + 0x6C, &redirects);
            patched += TryPatchWriterCandidate(batchRoot, &redirects);
            uint32_t inner = 0;
            __try {
                inner = *reinterpret_cast<volatile uint32_t*>(batchRoot + 0x94);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                inner = 0;
            }
            if (inner >= 0x10000u) {
                patched += TryPatchWriterCandidate(inner, &redirects);
                patched += TryPatchWriterCandidate(inner + 0x6C, &redirects);
            }
        }
    }
    // sub_684E70 all capture ctx slots (this[34..56]).
    for (int idx = 34; idx <= 56; ++idx) {
        uint32_t ctx = 0;
        __try {
            ctx = *reinterpret_cast<volatile uint32_t*>(master + static_cast<uintptr_t>(idx) * 4u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (ctx < 0x10000u) continue;
        uint32_t root = 0;
        __try {
            root = *reinterpret_cast<volatile uint32_t*>(ctx + 0x94);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (root < 0x10000u) continue;
        patched += TryPatchWriterCandidate(root + 0x6C, &redirects);
        patched += TryPatchWriterCandidate(root, &redirects);
        uint32_t inner = 0;
        __try {
            inner = *reinterpret_cast<volatile uint32_t*>(root + 0x94);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            inner = 0;
        }
        if (inner >= 0x10000u) {
            patched += TryPatchWriterCandidate(inner, &redirects);
            patched += TryPatchWriterCandidate(inner + 0x6C, &redirects);
        }
    }
    // Batch objects @ master+136..192 (RebindMenu2D pattern).
    static const int kBatchSlots[] = { 136, 140, 144, 148, 152, 156, 160, 176, 180, 184, 188, 192 };
    for (int off : kBatchSlots) {
        uint32_t batchObj = 0;
        __try {
            batchObj = *reinterpret_cast<volatile uint32_t*>(master + off);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (batchObj < 0x10000u) continue;
        uint32_t sub = 0;
        __try {
            sub = *reinterpret_cast<volatile uint32_t*>(batchObj + 0x94);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sub = 0;
        }
        if (sub >= 0x10000u) {
            patched += TryPatchWriterCandidate(sub, &redirects);
            patched += TryPatchWriterCandidate(sub + 0x6C, &redirects);
        }
        patched += TryPatchWriterCandidate(batchObj + 0x6C, &redirects);
    }
    static uint32_t s_logGen = 0;
    static bool s_loggedZeroCtx = false;
    if (patched > 0 || redirects > 0) {
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            s_loggedZeroCtx = false;
            HookLog("[ffx-hooks] FullGridCompiler %s capture-writer-patch ctx=0x%08X root=0x%08X "
                "patched=%d redirects=%d latest=%p bootPos=%p",
                tag, captureCtx, batchRoot, patched, redirects, g_latestPosBatch48, g_bootPosBatch860);
        }
    } else if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        s_loggedZeroCtx = false;
    }
    if (!patched && !redirects && !s_loggedZeroCtx) {
        s_loggedZeroCtx = true;
        HookLog("[ffx-hooks] FullGridCompiler %s capture-writer-patch ctx=0x%08X root=0x%08X "
            "patched=0 (no-op once) latest=%p bootPos=%p",
            tag, captureCtx, batchRoot, g_latestPosBatch48, g_bootPosBatch860);
    }
    return patched;
}

static uint32_t ProbeStaleSlot860Dword() {
    if (!g_stalePos86048) return 0;
    __try {
        const uintptr_t off = static_cast<uintptr_t>(860) * 48u;
        return *reinterpret_cast<volatile uint32_t*>(
            reinterpret_cast<volatile uint8_t*>(g_stalePos86048) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static void RememberStalePtr(void** staleSlot, void* oldPtr, void* freshPtr) {
    if (!staleSlot || !oldPtr || oldPtr == freshPtr) return;
    const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(oldPtr));
    if (!IsPlausibleBatchHeapPtr(v)) return;
    if (!*staleSlot) *staleSlot = oldPtr;
}

static bool IsBootHeapRange(uint32_t p) {
    return p >= 0x10800000u && p < 0x10A00000u;
}

static bool IsBootBatchBase(uint32_t p) {
    return p >= 0x10860000u && p < 0x10890000u;
}

static void SanitizeBootPosBase() {
    if (!g_bootPosBatch860) {
        g_bootPosVerified41252 = false;
        return;
    }
    const uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_bootPosBatch860));
    if (!IsBootBatchBase(p) || !g_bootPosVerified41252) {
        HookLog("[ffx-hooks] FullGridCompiler boot-pos reject invalid base=%08X verified=%d",
            p, g_bootPosVerified41252 ? 1 : 0);
        g_bootPosBatch860 = nullptr;
        g_bootPosVerified41252 = false;
    }
}

static void RegisterBootBatchPtr(void** bootSlot, void* ptr, int batchBytes) {
    if (!bootSlot || !ptr) return;
    const uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr));
    if (!IsBootHeapRange(p)) return;
    if (!*bootSlot) {
        *bootSlot = ptr;
        return;
    }
    const uint32_t cur = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*bootSlot));
    if (p < cur) *bootSlot = ptr;
    (void)batchBytes;
}

static void InferBootPosBatchFromFloatHit(uintptr_t hitAddr) {
    if (hitAddr < 0x10860000u || hitAddr >= 0x10890000u) return;
    const uintptr_t base = hitAddr - static_cast<uintptr_t>(860) * 48u;
    if (!IsBootBatchBase(static_cast<uint32_t>(base))) return;
    if (g_bootPosBatch860) {
        const uint32_t cur = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_bootPosBatch860));
        if (IsBootBatchBase(cur)) return;
    }
    g_bootPosBatch860 = reinterpret_cast<void*>(base);
    g_bootPosVerified41252 = true;
    if (!g_stalePos86048) g_stalePos86048 = g_bootPosBatch860;
    HookLog("[ffx-hooks] FullGridCompiler boot-pos inferred base=%p from float-hit=%p",
        g_bootPosBatch860, reinterpret_cast<void*>(hitAddr));
}

static void MaybeRegisterBootPosFromPtr(void* p, int reqSize) {
    if (!p || reqSize != kGamePosBatch860) return;
    const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    if (!IsBootHeapRange(addr)) return;
    g_bootPosBatch860 = p;
    g_bootPosVerified41252 = true;
    if (!g_stalePos86048) g_stalePos86048 = p;
}

static void DiscoverBootPosForSession(const char* tag) {
    if (g_bootPosVerified41252 && g_bootPosBatch860) return;
    (void)tag;
    /* Boot pos base comes ONLY from 942B60 req=41252 (NoteLatestBatchPtr) or float-hit infer.
     * v1.42 arena-scan / writer-harvest falsely picked 0x10860000 and ScrubBootPosOobTail
     * corrupted heap -> A48910 SEH -> infinite re-activate (status stays 00). */
}

static void FreezeLatestToPinned() {
    if (g_pinnedPos86148) g_latestPosBatch48 = g_pinnedPos86148;
    if (g_pinnedCol86116) g_latestColorBatch16 = g_pinnedCol86116;
    if (g_pinnedUv8618) g_latestUvBatch8 = g_pinnedUv8618;
}

static bool PtrInBootPosBatch(uint32_t p) {
    if (!g_bootPosVerified41252 || !g_bootPosBatch860) return false;
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_bootPosBatch860));
    return p >= base && p < base + static_cast<uint32_t>(BootArenaPosCap());
}

static void HarvestBootBatchPtrFromWriters(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return;
    __try {
        volatile uint8_t* table = menu + kAbmapMenuWriterTableOffset;
        for (int i = 0; i < kAbmapMenuWriterEntries; ++i) {
            volatile uint8_t* entry = table + static_cast<uintptr_t>(i) * kAbmapMenuWriterStride;
            for (int w = 0; w < 2; ++w) {
                const uint32_t writer = *reinterpret_cast<volatile uint32_t*>(entry + w * 4);
                if (writer < 0x10000u) continue;
                const uint32_t pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
                const uint32_t col = *reinterpret_cast<volatile uint32_t*>(writer + 0x14);
                const uint32_t uv = *reinterpret_cast<volatile uint32_t*>(writer + 0x18);
                if (!g_bootColBatch860 && IsBootHeapRange(col))
                    RegisterBootBatchPtr(&g_bootColBatch860,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(col)), kGameColBatch860);
                if (!g_bootUvBatch860 && IsBootHeapRange(uv))
                    RegisterBootBatchPtr(&g_bootUvBatch860,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(uv)), kGameUvBatch860);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (g_bootPosBatch860) {
        HookLog("[ffx-hooks] FullGridCompiler %s boot-pos harvested from writers base=%p col=%p uv=%p",
            tag, g_bootPosBatch860, g_bootColBatch860, g_bootUvBatch860);
    }
}

static void DiscoverBootBatchPtrs(const char* tag) {
    SanitizeBootPosBase();
    (void)tag;
    /* Pos batch base is NOT discovered via menu/master DWORD scan — too many false positives. */
}

static bool WriterTripleIsLatest(uint32_t writer) {
    if (writer < 0x10000u) return true;
    void* wantPos = RedirectPosBatch();
    void* wantCol = RedirectColBatch();
    void* wantUv = RedirectUvBatch();
    if (!wantPos || !wantCol || !wantUv) return false;
    __try {
        const uint32_t pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
        const uint32_t col = *reinterpret_cast<volatile uint32_t*>(writer + 0x14);
        const uint32_t uv = *reinterpret_cast<volatile uint32_t*>(writer + 0x18);
        const uint32_t latestPos = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(wantPos));
        const uint32_t latestCol = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(wantCol));
        const uint32_t latestUv = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(wantUv));
        if (PtrInBootPosBatch(pos) || IsBootHeapRange(col) || IsBootHeapRange(uv)) return false;
        return pos == latestPos && col == latestCol && uv == latestUv;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int ScrubAllSgWritersOffBoot(const char* tag) {
    if (!g_sgBatchArmed || g_trustedAssetNodes <= kVanillaNodeCapacity) return 0;
    if (!RedirectPosBatch() && !RedirectColBatch() && !RedirectUvBatch()) return 0;
    int patched = PatchAbmapMenuWriterTable(tag);
    volatile uint8_t* menu = AbmapMenuStateBase();
    int stillBoot = 0;
    if (menu) {
        __try {
            volatile uint8_t* table = menu + kAbmapMenuWriterTableOffset;
            for (int i = 0; i < kAbmapMenuWriterEntries; ++i) {
                volatile uint8_t* entry = table + static_cast<uintptr_t>(i) * kAbmapMenuWriterStride;
                for (int w = 0; w < 2; ++w) {
                    const uint32_t writer = *reinterpret_cast<volatile uint32_t*>(entry + w * 4);
                    if (writer < 0x10000u) continue;
                    uint32_t pos = 0;
                    __try {
                        pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        continue;
                    }
                    if (PtrInBootPosBatch(pos) || !WriterTripleIsLatest(writer)) {
                        if (PatchMenu2DWriterBuffers(writer)) ++patched;
                        __try {
                            pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                            pos = 0;
                        }
                        if (PtrInBootPosBatch(pos)) ++stillBoot;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    patched += PatchCaptureCtxDrawWriters(tag);
    if (g_captureDrawWriterAddr >= 0x10000u) {
        if (PatchMenu2DWriterBuffers(g_captureDrawWriterAddr)) ++patched;
        uint32_t pos = 0;
        __try {
            pos = *reinterpret_cast<volatile uint32_t*>(g_captureDrawWriterAddr + 0x0C);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            pos = 0;
        }
        if (PtrInBootPosBatch(pos)) ++stillBoot;
    }
    if (g_bootPosBatch860)
        patched += PurgeBootArenaBatchPointers(tag);
    static uint32_t s_logGen = 0;
    if (patched > 0 || stillBoot > 0) {
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s scrub-writers patched=%d stillBoot=%d "
                "pinPos=%p bootPos=%p",
                tag, patched, stillBoot, RedirectPosBatch(), g_bootPosBatch860);
        }
    }
    return stillBoot;
}

static void RebindCaptureCtxDrawWriter(const char* tag) {
    if (!g_base || !DrawBatchSlot860FullReady()) return;
    uint32_t captureCtx = 0;
    __try {
        captureCtx = *reinterpret_cast<volatile uint32_t*>(g_base + RVA_FFX_MENU2D_CAPTURE_CTX_PTR);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (captureCtx < 0x10000u) return;
    uint32_t batchRoot = 0;
    __try {
        batchRoot = *reinterpret_cast<volatile uint32_t*>(captureCtx + 0x94);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (batchRoot < 0x10000u) return;
    const uint32_t writer = batchRoot + 0x6C;
    if (PatchMenu2DWriterBuffers(writer)) {
        HookLog("[ffx-hooks] FullGridCompiler %s capture-draw-writer rebound w=0x%08X pos=%p col=%p uv=%p",
            tag, writer, g_latestPosBatch48, g_latestColorBatch16, g_latestUvBatch8);
    }
}

static void PinLayoutBatchBuffers(const char* tag) {
    if (g_pinnedPos86148 && g_pinnedCol86116 && g_pinnedUv8618) return;
    if (g_latestPosBatch48 && !g_pinnedPos86148) {
        g_pinnedPos86148 = g_latestPosBatch48;
        HookLog("[ffx-hooks] FullGridCompiler %s PIN-pos86148 %p (layout sync)",
            tag, g_pinnedPos86148);
    }
    if (g_latestColorBatch16 && !g_pinnedCol86116) {
        g_pinnedCol86116 = g_latestColorBatch16;
        HookLog("[ffx-hooks] FullGridCompiler %s PIN-col86116 %p (layout sync)",
            tag, g_pinnedCol86116);
    }
    if (g_latestUvBatch8 && !g_pinnedUv8618) {
        g_pinnedUv8618 = g_latestUvBatch8;
        HookLog("[ffx-hooks] FullGridCompiler %s PIN-uv8618 %p (layout sync)",
            tag, g_pinnedUv8618);
    }
}

static void SyncAllSgWritersAtLayout(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    if (!g_batch861Ready48 && !g_pinnedPos86148 && !g_latestPosBatch48) return;
    SanitizeStaleBatchGlobals(tag);
    g_sgLayoutSyncActive = true;
    PinLayoutBatchBuffers(tag);
    FreezeLatestToPinned();
    SanitizeBootPosBase();
    DiscoverBootPosForSession(tag);
    ScrubBootPosOobTail(tag);
    ScrubStalePosOobTail(tag);
    /* Post-A45570 harvest only — pre-trampoline harvest captured empty pre-layout ptrs (SG-C018). */
    g_stalePos86048 = nullptr;
    g_staleCol86016 = nullptr;
    g_staleUv8608 = nullptr;
    HarvestStaleBatchPointersFromWriters();
    HarvestBootBatchPtrFromWriters(tag);
    DiscoverBootBatchPtrs(tag);
    SanitizeBootPosBase();
    ScrubBootPosOobTail(tag);
    MigrateBatch860ToLatest(tag);
    ClearSlot860InLatest(tag);
    PatchAbmapMenuWriterTable(tag);
    PurgeStaleBatchPointers(tag);
    if (g_bootPosBatch860)
        PurgeBootArenaBatchPointers(tag);
    RebindMenu2DNodeBatchBuffers(tag);
    RebindCaptureCtxDrawWriter(tag);
    if (g_captureDrawWriterAddr)
        PatchMenu2DWriterBuffers(g_captureDrawWriterAddr);
    PatchCaptureCtxDrawWriters(tag);
    PopulateSlot860FromNodeRecord(tag);
    RedirectProducerPointersFull(tag);
    g_writerSyncGen = g_layoutGen;
    HookLog("[ffx-hooks] FullGridCompiler %s writer-sync done rebound=%d bootPos=%p verified=%d populated860=%d",
        tag, g_batchPointersRebound ? 1 : 0, g_bootPosBatch860, g_bootPosVerified41252 ? 1 : 0,
        g_populatedSlot860 ? 1 : 0);
    g_sgLayoutSyncActive = false;
}

static int ReplaceBootBatchPtrScan(volatile uint8_t* base, size_t len, void* bootBase, void* freshBase, int batchCap) {
    if (!base || !bootBase || !freshBase || batchCap <= 0) return 0;
    const uint32_t boot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bootBase));
    const uint32_t fresh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(freshBase));
    if (boot == fresh) return 0;
    int n = 0;
    for (size_t off = 0; off + 4 <= len; off += 4) {
        __try {
            volatile uint32_t* cell = reinterpret_cast<volatile uint32_t*>(base + off);
            const uint32_t v = *cell;
            if (v == boot) {
                *cell = fresh;
                ++n;
                continue;
            }
            if (v > boot && v < boot + static_cast<uint32_t>(batchCap)) {
                *cell = fresh + (v - boot);
                ++n;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return n;
}

static bool IsKnownFreshPosBatchPtr(uint32_t p) {
    if (p < 0x10000u) return true;
    const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(p));
    if (ptr == RedirectPosBatch() || ptr == g_pinnedPos86148 || ptr == g_latestPosBatch48)
        return true;
    return false;
}

static bool IsKnownFreshColBatchPtr(uint32_t p) {
    if (p < 0x10000u) return true;
    const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(p));
    return ptr == RedirectColBatch() || ptr == g_pinnedCol86116 || ptr == g_latestColorBatch16;
}

static bool IsKnownFreshUvBatchPtr(uint32_t p) {
    if (p < 0x10000u) return true;
    const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(p));
    return ptr == RedirectUvBatch() || ptr == g_pinnedUv8618 || ptr == g_latestUvBatch8;
}

static bool IsStale41252PosBase(uint32_t p) {
    if (p < 0x10000u || IsKnownFreshPosBatchPtr(p)) return false;
    if (IsBootHeapRange(p)) return true;
    if (g_stalePos86048 && p == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_stalePos86048)))
        return true;
    if (g_bootPosBatch860 && p == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_bootPosBatch860)))
        return true;
    return false;
}

static bool IsStale41252ColBase(uint32_t p) {
    if (p < 0x10000u || IsKnownFreshColBatchPtr(p)) return false;
    if (IsBootHeapRange(p)) return true;
    if (g_staleCol86016 && p == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_staleCol86016)))
        return true;
    if (g_bootColBatch860 && p == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_bootColBatch860)))
        return true;
    return false;
}

static bool IsStale41252UvBase(uint32_t p) {
    if (p < 0x10000u || IsKnownFreshUvBatchPtr(p)) return false;
    if (IsBootHeapRange(p)) return true;
    if (g_staleUv8608 && p == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_staleUv8608)))
        return true;
    if (g_bootUvBatch860 && p == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_bootUvBatch860)))
        return true;
    return false;
}

static void RegisterProducerStalePos(void* p) {
    if (!p || p == RedirectPosBatch() || p == g_pinnedPos86148) return;
    if (reinterpret_cast<uintptr_t>(p) < 0x10000u) return;
    RememberStalePtr(&g_stalePos86048, p, RedirectPosBatch());
}

static void HarvestProducerStaleFromWriters() {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        __try {
            volatile uint8_t* table = menu + kAbmapMenuWriterTableOffset;
            for (int i = 0; i < kAbmapMenuWriterEntries; ++i) {
                volatile uint8_t* entry = table + static_cast<uintptr_t>(i) * kAbmapMenuWriterStride;
                for (int w = 0; w < 2; ++w) {
                    const uint32_t writer = *reinterpret_cast<volatile uint32_t*>(entry + w * 4);
                    if (writer < 0x10000u) continue;
                    const uint32_t pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
                    if (!IsKnownFreshPosBatchPtr(pos))
                        RegisterProducerStalePos(reinterpret_cast<void*>(static_cast<uintptr_t>(pos)));
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_base) {
        volatile uint8_t* master = reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
        for (int idx = 34; idx <= 56; ++idx) {
            uint32_t ctx = 0;
            __try {
                ctx = *reinterpret_cast<volatile uint32_t*>(master + static_cast<uintptr_t>(idx) * 4u);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            if (ctx < 0x10000u) continue;
            uint32_t root = 0;
            __try {
                root = *reinterpret_cast<volatile uint32_t*>(ctx + 0x94);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            if (root < 0x10000u) continue;
            const uint32_t writer = root + 0x6C;
            uint32_t pos = 0;
            __try {
                pos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            if (!IsKnownFreshPosBatchPtr(pos))
                RegisterProducerStalePos(reinterpret_cast<void*>(static_cast<uintptr_t>(pos)));
        }
    }
    RegisterProducerStalePos(g_bootPosBatch860);
}

static void ScanMenuRegionForStalePos(volatile uint8_t* base, size_t len) {
    if (!base || len < 4) return;
    for (size_t off = 0; off + 4 <= len; off += 4) {
        __try {
            const uint32_t v = *reinterpret_cast<volatile uint32_t*>(base + off);
            if (v >= 0x10000u && !IsKnownFreshPosBatchPtr(v))
                RegisterProducerStalePos(reinterpret_cast<void*>(static_cast<uintptr_t>(v)));
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static void HarvestProducerStaleFromMenuRegions() {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return;
    ScanMenuRegionForStalePos(
        menu + kAbmapMenuPlacementSlotBase,
        static_cast<size_t>(kAbmapMenuPlacementSlotCount) * kAbmapMenuPlacementSlotStride);
    ScanMenuRegionForStalePos(menu + kAbmapMenuProjectionBase, kAbmapMenuProjectionLen);
    ScanMenuRegionForStalePos(menu + kAbmapMenuAnimDispatchBase, kAbmapMenuAnimDispatchLen);
    ScanMenuRegionForStalePos(menu + kAbmapMenuCallbackChainBase, kAbmapMenuCallbackChainLen);
    ScanMenuRegionForStalePos(menu + kAbmapMenuLinkGeomCursorOffset, 32u);
}

static int RedirectBatchPtrsInRegion(volatile uint8_t* base, size_t len,
    void** stalePosList, int nStalePos, void* freshPos, void* freshCol, void* freshUv) {
    if (!base || !len) return 0;
    int total = 0;
    if (freshPos) {
        for (int i = 0; i < nStalePos; ++i)
            total += ReplaceBootBatchPtrScan(base, len, stalePosList[i], freshPos, kGamePosBatch860);
    }
    if (g_staleCol86016 && freshCol)
        total += ReplaceBootBatchPtrScan(base, len, g_staleCol86016, freshCol, kGameColBatch860);
    if (g_staleUv8608 && freshUv)
        total += ReplaceBootBatchPtrScan(base, len, g_staleUv8608, freshUv, kGameUvBatch860);
    return total;
}

static int ReplaceStalePtrScanStack(void* staleBase, void* freshBase, int batchCap) {
    if (!staleBase || !freshBase || batchCap <= 0) return 0;
    const uint32_t stale = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(staleBase));
    const uint32_t fresh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(freshBase));
    if (stale == fresh) return 0;
    int n = 0;
    volatile uint32_t* sp = reinterpret_cast<volatile uint32_t*>(_AddressOfReturnAddress());
    for (size_t i = 0; i < 4096; ++i) {
        __try {
            volatile uint32_t* cell = sp + i;
            const uint32_t v = *cell;
            if (v == stale) {
                *cell = fresh;
                ++n;
                continue;
            }
            if (v > stale && v < stale + static_cast<uint32_t>(batchCap)) {
                *cell = fresh + (v - stale);
                ++n;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
    }
    return n;
}

// v1.45/v1.50: redirect hidden producer stale 41252B pos/col/uv ptrs -> pinned 861 buffers.
static int RedirectProducerPointersFull(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity || !g_sgBatchArmed) return 0;
    SanitizeStaleBatchGlobals(tag);
    FreezeLatestToPinned();
    void* freshPos = RedirectPosBatch();
    void* freshCol = RedirectColBatch();
    void* freshUv = RedirectUvBatch();
    if (!freshPos) return 0;

    PatchAbmapMenuWriterTable(tag);
    PatchCaptureCtxDrawWriters(tag);
    HarvestProducerStaleFromWriters();
    HarvestProducerStaleFromMenuRegions();

    void* stalePosList[6] = {};
    int nStalePos = 0;
    auto addStalePos = [&](void* p) {
        if (!p || p == freshPos) return;
        const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        if (!IsPlausibleBatchHeapPtr(v)) return;
        for (int i = 0; i < nStalePos; ++i)
            if (stalePosList[i] == p) return;
        if (nStalePos < 6) stalePosList[nStalePos++] = p;
    };
    addStalePos(g_stalePos86048);
    addStalePos(g_bootPosBatch860);

    struct RegionSpec {
        const char* name;
        uintptr_t offset;
        size_t length;
    };
    static const RegionSpec kRegions[] = {
        { "placement-slots", kAbmapMenuPlacementSlotBase,
            static_cast<size_t>(kAbmapMenuPlacementSlotCount) * kAbmapMenuPlacementSlotStride },
        { "projection", kAbmapMenuProjectionBase, kAbmapMenuProjectionLen },
        { "anim-dispatch", kAbmapMenuAnimDispatchBase, kAbmapMenuAnimDispatchLen },
        { "callbacks", kAbmapMenuCallbackChainBase, kAbmapMenuCallbackChainLen },
        { "link-cursor", kAbmapMenuLinkGeomCursorOffset, 32u },
    };

    int total = 0;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        total += RedirectBatchPtrsInRegion(menu, 0x50000u, stalePosList, nStalePos, freshPos, freshCol, freshUv);
        for (const RegionSpec& reg : kRegions) {
            const int n = RedirectBatchPtrsInRegion(
                menu + reg.offset, reg.length, stalePosList, nStalePos, freshPos, freshCol, freshUv);
            if (n > 0) {
                static uint32_t s_regionLogGen = 0;
                if (s_regionLogGen != g_layoutGen) {
                    s_regionLogGen = g_layoutGen;
                    HookLog("[ffx-hooks] FullGridCompiler %s producer-region %s replaced=%d off=0x%X len=%u",
                        tag, reg.name, n, static_cast<unsigned>(reg.offset), static_cast<unsigned>(reg.length));
                }
                total += n;
            }
        }
        total += RedirectBatchPtrsInRegion(
            menu + kAbmapMenuAnimBandOffset, kAbmapMenuAnimBandLen,
            stalePosList, nStalePos, freshPos, freshCol, freshUv);
    }
    if (g_base) {
        volatile uint8_t* master = reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
        total += RedirectBatchPtrsInRegion(master, 8192u, stalePosList, nStalePos, freshPos, freshCol, freshUv);
    }
    /* SG-C033: ReplaceStalePtrScanStack corrupts stack locals @0x03E5A595 — menu-region scan only. */

    if (total > 0) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s producer-redirect replaced=%d stalePos=%p freshPos=%p nStale=%d",
                tag, total, g_stalePos86048, freshPos, nStalePos);
        }
    }
    PurgeAllStale41252Pointers(tag);
    return total;
}

static int PurgeBootArenaBatchPointers(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return 0;
    if (!g_bootPosBatch860 && !g_bootColBatch860 && !g_bootUvBatch860) return 0;
    void* freshPos = RedirectPosBatch();
    void* freshCol = RedirectColBatch();
    void* freshUv = RedirectUvBatch();
    if (!freshPos && !freshCol && !freshUv) return 0;
    int total = 0;
    if (g_base) {
        volatile uint8_t* master = reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
        total += ReplaceBootBatchPtrScan(master, 4096u, g_bootPosBatch860, freshPos, BootArenaPosCap());
        total += ReplaceBootBatchPtrScan(master, 4096u, g_bootColBatch860, freshCol, BootArenaColCap());
        total += ReplaceBootBatchPtrScan(master, 4096u, g_bootUvBatch860, freshUv, BootArenaUvCap());
    }
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        total += ReplaceBootBatchPtrScan(menu, 0x50000u, g_bootPosBatch860, freshPos, BootArenaPosCap());
        total += ReplaceBootBatchPtrScan(menu, 0x50000u, g_bootColBatch860, freshCol, BootArenaColCap());
        total += ReplaceBootBatchPtrScan(menu, 0x50000u, g_bootUvBatch860, freshUv, BootArenaUvCap());
    }
    if (total > 0) {
        HookLog("[ffx-hooks] FullGridCompiler %s boot-ptr-purge replaced=%d bootPos=%p freshPos=%p cap=%d",
            tag, total, g_bootPosBatch860, freshPos, BootArenaPosCap());
    }
    return total;
}

static bool IsPoisonCoordDword(uint32_t v) {
    return v == 0x3F008081u || v == 0x3E808081u || v == 0x40008081u;
}

static bool IsPlausibleBatchHeapPtr(uint32_t p) {
    if (p < 0x10000u) return false;
    if (IsPoisonCoordDword(p)) return false;
    if (p == 0xBF800000u || p == 0x3F800000u || p == 0xFFFFFFFFu) return false;
    if (p >= 0xFFFF0000u) return false;
    return IsCommittedWritable(reinterpret_cast<const void*>(static_cast<uintptr_t>(p)), 64u);
}

static void SanitizeStaleBatchGlobals(const char* tag) {
    auto fix = [&](void** slot, const char* label) {
        if (!*slot) return;
        const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*slot));
        if (IsPlausibleBatchHeapPtr(v)) return;
        HookLog("[ffx-hooks] FullGridCompiler %s sanitize-stale reject %s=%08X", tag, label, v);
        *slot = nullptr;
    };
    fix(&g_stalePos86048, "stalePos");
    fix(&g_staleCol86016, "staleCol");
    fix(&g_staleUv8608, "staleUv");
}

static bool IsCommittedWritable(const void* addr, size_t len) {
    if (!addr || len == 0) return false;
    const uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    const uintptr_t end = start + len;
    uintptr_t cur = start;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t regionEnd = regionBase + mbi.RegionSize;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)))
            return false;
        if (cur < regionBase)
            return false;
        cur = regionEnd;
    }
    return true;
}

static int ScrubPoisonFloatsInBuffer(void* base, size_t len) {
    if (!base || len < 4 || !IsCommittedWritable(base, len)) return 0;
    int n = 0;
    __try {
        uint32_t* p = reinterpret_cast<uint32_t*>(base);
        const size_t count = len / 4;
        for (size_t i = 0; i < count; ++i) {
            if (IsPoisonCoordDword(p[i])) {
                p[i] = 0;
                ++n;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

static void ScrubKnownBatchBuffers(const char* tag) {
    int total = 0;
    auto scrub = [&](void* p, int cap) {
        if (p && cap > 0) total += ScrubPoisonFloatsInBuffer(p, static_cast<size_t>(cap));
    };
    scrub(g_latestPosBatch48, kBatch861Bytes48);
    scrub(g_pinnedPos86148, kBatch861Bytes48);
    scrub(g_latestColorBatch16, kBatch861Bytes16);
    scrub(g_pinnedCol86116, kBatch861Bytes16);
    scrub(g_latestUvBatch8, kBatch861Bytes8);
    scrub(g_pinnedUv8618, kBatch861Bytes8);
    scrub(g_stalePos86048, kGamePosBatch860);
    scrub(g_staleCol86016, kGameColBatch860);
    scrub(g_staleUv8608, kGameUvBatch860);
    scrub(g_bootPosBatch860, kGamePosBatch860);
    scrub(g_bootColBatch860, kGameColBatch860);
    scrub(g_bootUvBatch860, kGameUvBatch860);
    if (total > 0) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s poison-scrub cleared=%d", tag, total);
        }
    }
}

static void InferAndScrubBatchFromFloatHit(uintptr_t hitAddr) {
    if (IsBootHeapRange(static_cast<uint32_t>(hitAddr))) {
        InferBootPosBatchFromFloatHit(hitAddr);
        return;
    }
    /* Game-heap stale 41252B pos batch: producer OOB @ base+860*48 (log: 0x0E200Exx). */
    const uintptr_t slotOff = static_cast<uintptr_t>(860) * 48u;
    uintptr_t base = (hitAddr >= slotOff) ? (hitAddr - slotOff) : hitAddr;
    if (base < 0x10000u) return;
    if (!IsCommittedWritable(reinterpret_cast<void*>(base), kGamePosBatch860)) return;
    if (!g_stalePos86048 && base != reinterpret_cast<uintptr_t>(g_pinnedPos86148))
        g_stalePos86048 = reinterpret_cast<void*>(base);
    if (IsCommittedWritable(reinterpret_cast<void*>(base + slotOff), 48u)) {
        __try {
            memset(reinterpret_cast<void*>(base + slotOff), 0, 48u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static void Scrub41252PosOobAt(void* batchBase, const char* tag) {
    if (!batchBase || batchBase == RedirectPosBatch()) return;
    uint8_t* base = reinterpret_cast<uint8_t*>(batchBase);
    const size_t oobLen = static_cast<size_t>(kBatch861Bytes48 - kGamePosBatch860);
    const size_t slotOff = static_cast<size_t>(860) * 48u;
    int cleared = 0;
    if (IsCommittedWritable(base + kGamePosBatch860, oobLen))
        cleared += ScrubPoisonFloatsInBuffer(base + kGamePosBatch860, oobLen);
    if (IsCommittedWritable(base + slotOff, 48u)) {
        cleared += ScrubPoisonFloatsInBuffer(base + slotOff, 48u);
        __try {
            memset(base + slotOff, 0, 48u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (cleared > 0) {
        static uint32_t s_gen = 0;
        if (s_gen != g_layoutGen) {
            s_gen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s oob-scrub base=%p cleared=%d", tag, batchBase, cleared);
        }
    }
}

static void ScrubBootPosOobTail(const char* tag) {
    if (!g_bootPosVerified41252 || !g_bootPosBatch860) return;
    Scrub41252PosOobAt(g_bootPosBatch860, tag);
}

// v1.51: game-heap stale 41252B batches (bootPos=0 sessions) poison +41280 OOB same as boot arena.
static void ScrubStalePosOobTail(const char* tag) {
    if (!g_stalePos86048 || g_stalePos86048 == RedirectPosBatch()) return;
    Scrub41252PosOobAt(g_stalePos86048, tag);
}

static void ReactToProducerSlot860Write(void* probe, const char* tag) {
    if (!probe || g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    if (probe == RedirectPosBatch() || probe == g_pinnedPos86148) return;
    RegisterProducerStalePos(probe);
    Scrub41252PosOobAt(probe, tag);
    PurgeAllStale41252Pointers(tag);
    if (probe == g_bootPosBatch860)
        PurgeBootArenaBatchPointers(tag);
    ClearSlot860InLatest(tag);
    PatchCaptureCtxDrawWriters(tag);
    static uint32_t s_gen = 0;
    if (s_gen != g_layoutGen) {
        s_gen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler %s PRODUCER-REACT probe=%p stale=%p pin=%p",
            tag, probe, g_stalePos86048, RedirectPosBatch());
    }
}

static int ReplaceExactBatchPtrScan(volatile uint8_t* base, size_t len, void* staleBase, void* freshBase, int batchCap) {
    if (!base || !staleBase || !freshBase || batchCap <= 0) return 0;
    const uint32_t stale = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(staleBase));
    const uint32_t fresh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(freshBase));
    if (stale == fresh) return 0;
    int n = 0;
    for (size_t off = 0; off + 4 <= len; off += 4) {
        __try {
            volatile uint32_t* cell = reinterpret_cast<volatile uint32_t*>(base + off);
            const uint32_t v = *cell;
            if (v == stale) {
                *cell = fresh;
                ++n;
                continue;
            }
            if (v > stale && v < stale + static_cast<uint32_t>(batchCap)) {
                *cell = fresh + (v - stale);
                ++n;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return n;
}

static void ClearSessionPinGlobals() {
    g_pinnedPos86148 = nullptr;
    g_pinnedCol86116 = nullptr;
    g_pinnedUv8618 = nullptr;
    g_latestPosBatch48 = nullptr;
    g_latestColorBatch16 = nullptr;
    g_latestUvBatch8 = nullptr;
    g_batch861Ready16 = false;
    g_batch861Ready48 = false;
    g_batch861Ready8 = false;
}

static bool SessionTombRegistered(void* pos) {
    if (!pos) return false;
    for (int i = 0; i < g_sessionTombCount; ++i) {
        if (g_sessionTombPos[i] == pos) return true;
    }
    return false;
}

static void RegisterSessionTomb(void* pos, void* col, void* uv) {
    if (!pos && !col && !uv) return;
    if (pos && SessionTombRegistered(pos)) return;
    if (g_sessionTombCount < kMaxSessionTombs) {
        g_sessionTombPos[g_sessionTombCount] = pos;
        g_sessionTombCol[g_sessionTombCount] = col;
        g_sessionTombUv[g_sessionTombCount] = uv;
        ++g_sessionTombCount;
        return;
    }
    memmove(&g_sessionTombPos[0], &g_sessionTombPos[1],
        static_cast<size_t>(kMaxSessionTombs - 1) * sizeof(void*));
    memmove(&g_sessionTombCol[0], &g_sessionTombCol[1],
        static_cast<size_t>(kMaxSessionTombs - 1) * sizeof(void*));
    memmove(&g_sessionTombUv[0], &g_sessionTombUv[1],
        static_cast<size_t>(kMaxSessionTombs - 1) * sizeof(void*));
    g_sessionTombPos[kMaxSessionTombs - 1] = pos;
    g_sessionTombCol[kMaxSessionTombs - 1] = col;
    g_sessionTombUv[kMaxSessionTombs - 1] = uv;
}

static int PurgeSessionHeapFromCaptureGraph(void* dyingPos, void* dyingCol, void* dyingUv,
    void* interimPos, void* interimCol, void* interimUv) {
    if (!g_base) return 0;
    if ((!dyingPos || !interimPos) && (!dyingCol || !interimCol) && (!dyingUv || !interimUv)) return 0;
    int total = 0;
    volatile uint8_t* master = reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
    auto scanObj = [&](uintptr_t obj) {
        if (obj < 0x10000u) return;
        volatile uint8_t* blob = reinterpret_cast<volatile uint8_t*>(obj);
        if (dyingPos && interimPos)
            total += ReplaceExactBatchPtrScan(blob, kCaptureObjScanLen, dyingPos, interimPos, kBatch861Bytes48);
        if (dyingCol && interimCol)
            total += ReplaceExactBatchPtrScan(blob, kCaptureObjScanLen, dyingCol, interimCol, kBatch861Bytes16);
        if (dyingUv && interimUv)
            total += ReplaceExactBatchPtrScan(blob, kCaptureObjScanLen, dyingUv, interimUv, kBatch861Bytes8);
    };
    auto scanCtxTree = [&](uint32_t ctx) {
        if (ctx < 0x10000u) return;
        scanObj(ctx);
        uint32_t root = 0;
        __try {
            root = *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(ctx) + 0x94u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            root = 0;
        }
        if (root < 0x10000u) return;
        scanObj(root);
        scanObj(root + 0x6Cu);
        uint32_t inner = 0;
        __try {
            inner = *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(root) + 0x94u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            inner = 0;
        }
        if (inner >= 0x10000u) {
            scanObj(inner);
            scanObj(inner + 0x6Cu);
        }
    };
    uint32_t captureCtx = 0;
    __try {
        captureCtx = *reinterpret_cast<volatile uint32_t*>(master + 0x94u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        captureCtx = 0;
    }
    scanCtxTree(captureCtx);
    for (int idx = 34; idx <= 56; ++idx) {
        uint32_t ctx = 0;
        __try {
            ctx = *reinterpret_cast<volatile uint32_t*>(master + static_cast<uintptr_t>(idx) * 4u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        scanCtxTree(ctx);
    }
    static const int kBatchSlots[] = { 136, 140, 144, 148, 152, 156, 160, 176, 180, 184, 188, 192 };
    for (int off : kBatchSlots) {
        uint32_t batchObj = 0;
        __try {
            batchObj = *reinterpret_cast<volatile uint32_t*>(master + off);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (batchObj < 0x10000u) continue;
        scanObj(batchObj);
        scanObj(batchObj + 0x6Cu);
        uint32_t sub = 0;
        __try {
            sub = *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(batchObj) + 0x94u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sub = 0;
        }
        if (sub >= 0x10000u) {
            scanObj(sub);
            scanObj(sub + 0x6Cu);
        }
    }
    return total;
}

static int PurgeHeapRangeFromKnownRegions(const char* tag, void* dyingPos, void* dyingCol, void* dyingUv,
    void* interimPos, void* interimCol, void* interimUv) {
    if (!dyingPos && !dyingCol && !dyingUv) return 0;
    int total = 0;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        if (dyingPos && interimPos)
            total += ReplaceExactBatchPtrScan(menu, 0x50000u, dyingPos, interimPos, kBatch861Bytes48);
        if (dyingCol && interimCol)
            total += ReplaceExactBatchPtrScan(menu, 0x50000u, dyingCol, interimCol, kBatch861Bytes16);
        if (dyingUv && interimUv)
            total += ReplaceExactBatchPtrScan(menu, 0x50000u, dyingUv, interimUv, kBatch861Bytes8);
        struct RegionSpec {
            const char* name;
            uintptr_t offset;
            size_t length;
        };
        static const RegionSpec kRegions[] = {
            { "placement-slots", kAbmapMenuPlacementSlotBase,
                static_cast<size_t>(kAbmapMenuPlacementSlotCount) * kAbmapMenuPlacementSlotStride },
            { "projection", kAbmapMenuProjectionBase, kAbmapMenuProjectionLen },
            { "anim-dispatch", kAbmapMenuAnimDispatchBase, kAbmapMenuAnimDispatchLen },
            { "callbacks", kAbmapMenuCallbackChainBase, kAbmapMenuCallbackChainLen },
            { "link-cursor", kAbmapMenuLinkGeomCursorOffset, 32u },
        };
        for (const RegionSpec& reg : kRegions) {
            if (dyingPos && interimPos)
                total += ReplaceExactBatchPtrScan(menu + reg.offset, reg.length, dyingPos, interimPos, kBatch861Bytes48);
            if (dyingCol && interimCol)
                total += ReplaceExactBatchPtrScan(menu + reg.offset, reg.length, dyingCol, interimCol, kBatch861Bytes16);
            if (dyingUv && interimUv)
                total += ReplaceExactBatchPtrScan(menu + reg.offset, reg.length, dyingUv, interimUv, kBatch861Bytes8);
        }
        if (dyingPos && interimPos)
            total += ReplaceExactBatchPtrScan(
                menu + kAbmapMenuAnimBandOffset, kAbmapMenuAnimBandLen,
                dyingPos, interimPos, kBatch861Bytes48);
        volatile uint8_t* table = menu + kAbmapMenuWriterTableOffset;
        for (int i = 0; i < kAbmapMenuWriterEntries; ++i) {
            volatile uint8_t* entry = table + static_cast<uintptr_t>(i) * kAbmapMenuWriterStride;
            if (dyingPos && interimPos)
                total += ReplaceExactBatchPtrScan(entry, kAbmapMenuWriterStride, dyingPos, interimPos, kBatch861Bytes48);
            if (dyingCol && interimCol)
                total += ReplaceExactBatchPtrScan(entry, kAbmapMenuWriterStride, dyingCol, interimCol, kBatch861Bytes16);
            if (dyingUv && interimUv)
                total += ReplaceExactBatchPtrScan(entry, kAbmapMenuWriterStride, dyingUv, interimUv, kBatch861Bytes8);
        }
    }
    if (g_base) {
        volatile uint8_t* master = reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
        if (dyingPos && interimPos)
            total += ReplaceExactBatchPtrScan(master, 8192u, dyingPos, interimPos, kBatch861Bytes48);
        if (dyingCol && interimCol)
            total += ReplaceExactBatchPtrScan(master, 8192u, dyingCol, interimCol, kBatch861Bytes16);
        if (dyingUv && interimUv)
            total += ReplaceExactBatchPtrScan(master, 8192u, dyingUv, interimUv, kBatch861Bytes8);
    }
    total += PurgeSessionHeapFromCaptureGraph(dyingPos, dyingCol, dyingUv, interimPos, interimCol, interimUv);
    if (total > 0) {
        HookLog("[ffx-hooks] FullGridCompiler %s purge-dying-pin replaced=%d dyingPos=%p interimPos=%p",
            tag, total, dyingPos, interimPos);
    }
    return total;
}

static int PurgeAllSessionTombs(const char* tag, void* interimPos, void* interimCol, void* interimUv) {
    if (!interimPos && !interimCol && !interimUv) return 0;
    int total = 0;
    for (int i = 0; i < g_sessionTombCount; ++i) {
        total += PurgeHeapRangeFromKnownRegions(tag, g_sessionTombPos[i], g_sessionTombCol[i], g_sessionTombUv[i],
            interimPos, interimCol, interimUv);
    }
    if (total > 0) {
        HookLog("[ffx-hooks] FullGridCompiler %s tomb-purge replaced=%d tombs=%d interimPos=%p",
            tag, total, g_sessionTombCount, interimPos);
    }
    return total;
}

static int PurgeDyingSessionBatchesFromMenu(const char* tag, void* dyingPos, void* dyingCol, void* dyingUv) {
    if (!dyingPos && !dyingCol && !dyingUv) return 0;
    void* interimPos = g_bootPosBatch860 ? g_bootPosBatch860 : g_stalePos86048;
    void* interimCol = g_bootColBatch860 ? g_bootColBatch860 : g_staleCol86016;
    void* interimUv = g_bootUvBatch860 ? g_bootUvBatch860 : g_staleUv8608;
    return PurgeHeapRangeFromKnownRegions(tag, dyingPos, dyingCol, dyingUv, interimPos, interimCol, interimUv);
}

static bool PtrInBatchRange(uint32_t p, void* base, int cap) {
    if (!base || p < 0x10000u || cap <= 0) return false;
    const uintptr_t u = static_cast<uintptr_t>(p);
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    return u >= b && u < b + static_cast<uintptr_t>(cap);
}

static uint32_t RemapBatchPtr(uint32_t p, void* dying, int cap, void* fresh) {
    if (!dying || !fresh || cap <= 0) return p;
    const uint32_t db = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dying));
    const uint32_t fb = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fresh));
    if (p == db) return fb;
    if (p > db && p < db + static_cast<uint32_t>(cap)) return fb + (p - db);
    return p;
}

static bool PatchWriterOffSessionHeap(uint32_t writer, void* dyingPos, void* dyingCol, void* dyingUv,
    void* freshPos, void* freshCol, void* freshUv) {
    if (writer < 0x10000u) return false;
    bool ok = false;
    __try {
        volatile uint32_t* posCell = reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
        volatile uint32_t* colCell = reinterpret_cast<volatile uint32_t*>(writer + 0x14);
        volatile uint32_t* uvCell = reinterpret_cast<volatile uint32_t*>(writer + 0x18);
        const uint32_t pos = *posCell;
        const uint32_t col = *colCell;
        const uint32_t uv = *uvCell;
        if (freshPos && PtrInBatchRange(pos, dyingPos, kBatch861Bytes48)) {
            *posCell = RemapBatchPtr(pos, dyingPos, kBatch861Bytes48, freshPos);
            ok = true;
        }
        if (freshCol && PtrInBatchRange(col, dyingCol, kBatch861Bytes16)) {
            *colCell = RemapBatchPtr(col, dyingCol, kBatch861Bytes16, freshCol);
            ok = true;
        }
        if (freshUv && PtrInBatchRange(uv, dyingUv, kBatch861Bytes8)) {
            *uvCell = RemapBatchPtr(uv, dyingUv, kBatch861Bytes8, freshUv);
            ok = true;
        }
        for (int i = 0; i < g_sessionTombCount; ++i) {
            if (freshPos && PtrInBatchRange(pos, g_sessionTombPos[i], kBatch861Bytes48)) {
                *posCell = RemapBatchPtr(pos, g_sessionTombPos[i], kBatch861Bytes48, freshPos);
                ok = true;
            }
            if (freshCol && PtrInBatchRange(col, g_sessionTombCol[i], kBatch861Bytes16)) {
                *colCell = RemapBatchPtr(col, g_sessionTombCol[i], kBatch861Bytes16, freshCol);
                ok = true;
            }
            if (freshUv && PtrInBatchRange(uv, g_sessionTombUv[i], kBatch861Bytes8)) {
                *uvCell = RemapBatchPtr(uv, g_sessionTombUv[i], kBatch861Bytes8, freshUv);
                ok = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return ok;
}

static int PatchAllWritersOffSessionHeap(const char* tag, void* dyingPos, void* dyingCol, void* dyingUv,
    void* freshPos, void* freshCol, void* freshUv) {
    if (!freshPos && !freshCol && !freshUv) return 0;
    int patched = 0;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        __try {
            volatile uint8_t* table = menu + kAbmapMenuWriterTableOffset;
            for (int i = 0; i < kAbmapMenuWriterEntries; ++i) {
                volatile uint8_t* entry = table + static_cast<uintptr_t>(i) * kAbmapMenuWriterStride;
                for (int w = 0; w < 2; ++w) {
                    const uint32_t writer = *reinterpret_cast<volatile uint32_t*>(entry + w * 4);
                    if (writer >= 0x10000u &&
                        PatchWriterOffSessionHeap(writer, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
                        ++patched;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_base) {
        volatile uint8_t* master = reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
        uint32_t captureCtx = 0;
        __try {
            captureCtx = *reinterpret_cast<volatile uint32_t*>(master + 0x94u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            captureCtx = 0;
        }
        if (captureCtx >= 0x10000u) {
            uint32_t batchRoot = 0;
            __try {
                batchRoot = *reinterpret_cast<volatile uint32_t*>(captureCtx + 0x94u);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                batchRoot = 0;
            }
            if (batchRoot >= 0x10000u) {
                if (PatchWriterOffSessionHeap(batchRoot + 0x6C, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
                    ++patched;
                if (PatchWriterOffSessionHeap(batchRoot, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
                    ++patched;
            }
        }
        for (int idx = 34; idx <= 56; ++idx) {
            uint32_t ctx = 0;
            __try {
                ctx = *reinterpret_cast<volatile uint32_t*>(master + static_cast<uintptr_t>(idx) * 4u);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            if (ctx < 0x10000u) continue;
            uint32_t root = 0;
            __try {
                root = *reinterpret_cast<volatile uint32_t*>(ctx + 0x94);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            if (root < 0x10000u) continue;
            if (PatchWriterOffSessionHeap(root + 0x6C, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
                ++patched;
            if (PatchWriterOffSessionHeap(root, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
                ++patched;
        }
        static const int kBatchSlots[] = { 136, 140, 144, 148, 152, 156, 160, 176, 180, 184, 188, 192 };
        for (int off : kBatchSlots) {
            uint32_t batchObj = 0;
            __try {
                batchObj = *reinterpret_cast<volatile uint32_t*>(master + off);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            if (batchObj < 0x10000u) continue;
            if (PatchWriterOffSessionHeap(batchObj + 0x6C, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
                ++patched;
            if (PatchWriterOffSessionHeap(batchObj, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
                ++patched;
        }
    }
    if (g_captureDrawWriterAddr >= 0x10000u &&
        PatchWriterOffSessionHeap(g_captureDrawWriterAddr, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv))
        ++patched;
    (void)tag;
    return patched;
}

static int ReleaseSessionHeapRefs(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return 0;
    SanitizeStaleBatchGlobals(tag);
    HarvestBootBatchPtrFromWriters(tag);
    void* dyingPos = g_pinnedPos86148 ? g_pinnedPos86148 : g_latestPosBatch48;
    void* dyingCol = g_pinnedCol86116 ? g_pinnedCol86116 : g_latestColorBatch16;
    void* dyingUv = g_pinnedUv8618 ? g_pinnedUv8618 : g_latestUvBatch8;
    if (!dyingPos && !dyingCol && !dyingUv) return 0;
    void* freshPos = g_bootPosBatch860;
    if (!freshPos && g_stalePos86048 && g_stalePos86048 != dyingPos)
        freshPos = g_stalePos86048;
    void* freshCol = g_bootColBatch860;
    if (!freshCol && g_staleCol86016 && g_staleCol86016 != dyingCol)
        freshCol = g_staleCol86016;
    void* freshUv = g_bootUvBatch860;
    if (!freshUv && g_staleUv8608 && g_staleUv8608 != dyingUv)
        freshUv = g_staleUv8608;
    if (!freshPos && !freshCol && !freshUv) {
        HookLog("[ffx-hooks] FullGridCompiler %s release-session SKIP no interim boot/stale dyingPos=%p",
            tag, dyingPos);
        return 0;
    }
    if (freshPos && freshPos != dyingPos)
        Scrub41252PosOobAt(freshPos, tag);
    const int purged = PurgeDyingSessionBatchesFromMenu(tag, dyingPos, dyingCol, dyingUv);
    const int writers = PatchAllWritersOffSessionHeap(tag, dyingPos, dyingCol, dyingUv, freshPos, freshCol, freshUv);
    RegisterSessionTomb(dyingPos, dyingCol, dyingUv);
    const int tombPurge = PurgeAllSessionTombs(tag, freshPos, freshCol, freshUv);
    const bool clearPins = strstr(tag, "A54660") != nullptr || strstr(tag, "reenter") != nullptr;
    if (clearPins)
        ClearSessionPinGlobals();
    HookLog("[ffx-hooks] FullGridCompiler %s release-session purged=%d writers=%d tomb=%d clearPins=%d "
        "dyingPos=%p freshPos=%p boot=%p",
        tag, purged, writers, tombPurge, clearPins ? 1 : 0, dyingPos, freshPos, g_bootPosBatch860);
    return purged + writers + tombPurge;
}

static void PrepSgReEnterSession(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity || !g_sgBatchArmed) return;
    HarvestBootBatchPtrFromWriters(tag);
    void* interimPos = g_bootPosBatch860 ? g_bootPosBatch860 : g_stalePos86048;
    void* interimCol = g_bootColBatch860 ? g_bootColBatch860 : g_staleCol86016;
    void* interimUv = g_bootUvBatch860 ? g_bootUvBatch860 : g_staleUv8608;
    const int tombPurge = PurgeAllSessionTombs(tag, interimPos, interimCol, interimUv);
    ReleaseSessionHeapRefs(tag);
    g_batchPointersRebound = false;
    g_writerSyncGen = 0;
    g_populatedSlot860 = false;
    SanitizeStaleBatchGlobals(tag);
    ScrubBootPosOobTail(tag);
    if (g_bootPosBatch860)
        PurgeBootArenaBatchPointers(tag);
    HookLog("[ffx-hooks] FullGridCompiler %s re-enter-prep tombPurge=%d bootPos=%p stalePos=%p gen=%u",
        tag, tombPurge, g_bootPosBatch860, g_stalePos86048, g_layoutGen);
}

static void PreUploadGuard(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity || !g_sgBatchArmed) return;
    LitePreUploadScrub(tag);
    ScrubKnownBatchBuffers(tag);
    if (g_bootPosBatch860)
        PurgeBootArenaBatchPointers(tag);
    ClearSlot860InLatest(tag);
    if (g_sgNode860Activated)
        RedirectProducerPointersFull(tag);
    static uint32_t s_gen = 0;
    if (s_gen != g_layoutGen) {
        s_gen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler %s pre-upload-guard boot=%p pin=%p node860=%d",
            tag, g_bootPosBatch860, RedirectPosBatch(), g_sgNode860Activated ? 1 : 0);
    }
}

static void EnsurePinnedWritersForActivation(const char* tag) {
    if (!g_pinnedPos86148 || !g_pinnedCol86116 || !g_pinnedUv8618) return;
    FreezeLatestToPinned();
    if (g_writerSyncGen != g_layoutGen)
        ScrubAllSgWritersOffBoot(tag);
    PatchAbmapMenuWriterTable(tag);
    PatchCaptureCtxDrawWriters(tag);
    RebindMenu2DNodeBatchBuffers(tag);
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler %s pinned-writers pos=%p col=%p uv=%p",
            tag, g_pinnedPos86148, g_pinnedCol86116, g_pinnedUv8618);
    }
}

// v1.80 SG-C047: pins cleared @exit while writers still aim at stale 41252B — force re-pin + redirect
// before vanilla A48910 node860 commit so GPU never reads 0x3F008081 poison @ slot860 OOB tail.
static void EnsureActivate861PinnedBuffers(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity || !g_sgBatchArmed) return;
    PinLayoutBatchBuffers(tag);
    if (!RedirectPosBatch())
        SyncAllSgWritersAtLayout(tag);
    PinLayoutBatchBuffers(tag);
    ScrubBootPosOobTail(tag);
    ScrubStalePosOobTail(tag);
    if (g_stalePos86048)
        ReactToProducerSlot860Write(g_stalePos86048, tag);
    if (g_bootPosBatch860)
        ReactToProducerSlot860Write(g_bootPosBatch860, tag);
    RedirectProducerPointersFull(tag);
    EnsurePinnedWritersForActivation(tag);
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler %s activate861-buffers pin=%p latest=%p stale=%p boot=%p",
            tag, g_pinnedPos86148, g_latestPosBatch48, g_stalePos86048, g_bootPosBatch860);
    }
}

static void PostActivate860Guard(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    ReactToProducerSlot860Write(g_stalePos86048, tag);
    ReactToProducerSlot860Write(g_bootPosBatch860, tag);
    RepairSlot860Poison(tag);
    PreUploadGuard(tag);
    ScanForCorruptFloat(tag);
    RedirectProducerPointersFull(tag);
    PatchCaptureCtxDrawWriters(tag);
}

static void PurgeAllStale41252Pointers(const char* tag) {
    if (!g_stalePos86048 && !g_staleCol86016 && !g_staleUv8608) return;
    void* freshPos = RedirectPosBatch();
    void* freshCol = RedirectColBatch();
    void* freshUv = RedirectUvBatch();
    int total = 0;
    if (g_base) {
        volatile uint8_t* master = reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
        if (g_stalePos86048 && freshPos)
            total += ReplaceBootBatchPtrScan(master, 8192u, g_stalePos86048, freshPos, kGamePosBatch860);
        if (g_staleCol86016 && freshCol)
            total += ReplaceBootBatchPtrScan(master, 8192u, g_staleCol86016, freshCol, kGameColBatch860);
        if (g_staleUv8608 && freshUv)
            total += ReplaceBootBatchPtrScan(master, 8192u, g_staleUv8608, freshUv, kGameUvBatch860);
    }
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        if (g_stalePos86048 && freshPos)
            total += ReplaceBootBatchPtrScan(menu, 0x50000u, g_stalePos86048, freshPos, kGamePosBatch860);
        if (g_staleCol86016 && freshCol)
            total += ReplaceBootBatchPtrScan(menu, 0x50000u, g_staleCol86016, freshCol, kGameColBatch860);
        if (g_staleUv8608 && freshUv)
            total += ReplaceBootBatchPtrScan(menu, 0x50000u, g_staleUv8608, freshUv, kGameUvBatch860);
    }
    if (total > 0) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s stale41252-purge replaced=%d stalePos=%p freshPos=%p",
                tag, total, g_stalePos86048, freshPos);
        }
    }
}

static void LitePreUploadScrubExit(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    ScrubBootPosOobTail(tag);
    ScrubStalePosOobTail(tag);
    ScrubKnownBatchBuffers(tag);
    /* SG-C041: never PurgeAllStale41252Pointers on exit — redirects stale/garbage -> pinned. */
}

// v1.38: scrub slot860 + purge stale 41252 ptrs before GPU upload / after activation.
static void LitePreUploadScrub(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    ScrubBootPosOobTail(tag);
    ScrubStalePosOobTail(tag);
    ClearSlot860InLatest(tag);
    PurgeAllStale41252Pointers(tag);
}

static void LitePostActivateRepair(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    LitePreUploadScrub(tag);
    RedirectProducerPointersFull(tag);
}

static void RepairSlot860PoisonExit(const char* tag) {
    LitePreUploadScrubExit(tag);
    /* SG-C040/041: never ScrubAllSgWritersOffBoot nor Purge41252 on exit. */
}

static void RepairSlot860Poison(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    ScrubBootPosOobTail(tag);
    ScrubStalePosOobTail(tag);
    ClearSlot860InLatest(tag);
    ScrubKnownBatchBuffers(tag);
    PurgeAllStale41252Pointers(tag);
    ScrubAllSgWritersOffBoot(tag);
    if (g_bootPosBatch860)
        PurgeBootArenaBatchPointers(tag);
    PatchCaptureCtxDrawWriters(tag);
    RebindMenu2DNodeBatchBuffers(tag);
    if (g_captureDrawWriterAddr)
        PatchMenu2DWriterBuffers(g_captureDrawWriterAddr);
}

// Slot860 OOB boot data is coordinate garbage (0x3F008081) — never copy into latest; zero instead.
static void ClearSlot860InLatest(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    void* posBuf = RedirectPosBatch();
    void* colBuf = RedirectColBatch();
    void* uvBuf = RedirectUvBatch();
    __try {
        if (posBuf)
            memset(reinterpret_cast<uint8_t*>(posBuf) + 860u * 48u, 0, 48u);
        if (colBuf)
            memset(reinterpret_cast<uint8_t*>(colBuf) + 860u * 16u, 0, 16u);
        if (uvBuf)
            memset(reinterpret_cast<uint8_t*>(uvBuf) + 860u * 8u, 0, 8u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler %s clear-slot860 FAULT", tag);
        return;
    }
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler %s clear-slot860 pinned pos=%p col=%p uv=%p",
            tag, posBuf, colBuf, uvBuf);
    }
}

static void MigrateBatch860ToLatest(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    /* v1.52: NEVER memcpy from boot arena — slot860 OOB poisons boot+41280; re-enter migrate caused SG-C020. */
    void* srcPos = g_stalePos86048;
    void* srcCol = g_staleCol86016;
    void* srcUv = g_staleUv8608;
    if (srcPos == g_bootPosBatch860) srcPos = nullptr;
    if (srcCol == g_bootColBatch860) srcCol = nullptr;
    if (srcUv == g_bootUvBatch860) srcUv = nullptr;
    if (srcPos == RedirectPosBatch()) srcPos = nullptr;
    /* kGamePosBatch860=41252 excludes OOB slot860 tail (+41280); safe vanilla 0..859 copy only. */
    bool migrated = false;
    __try {
        if (srcPos && g_latestPosBatch48 && srcPos != g_latestPosBatch48) {
            memcpy(g_latestPosBatch48, srcPos, kGamePosBatch860);
            migrated = true;
        }
        if (srcCol && g_latestColorBatch16 && srcCol != g_latestColorBatch16)
            memcpy(g_latestColorBatch16, srcCol, kGameColBatch860);
        if (srcUv && g_latestUvBatch8 && srcUv != g_latestUvBatch8)
            memcpy(g_latestUvBatch8, srcUv, kGameUvBatch860);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler %s batch-migrate FAULT srcPos=%p latestPos=%p boot=%p",
            tag, srcPos, g_latestPosBatch48, g_bootPosBatch860);
        return;
    }
    if (migrated) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s batch-migrate pos %dB src=%p (boot=0) -> latest=%p",
                tag, kGamePosBatch860, srcPos, g_latestPosBatch48);
        }
    }
}

static void SyncSgBatchBufferPointers(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    PatchAbmapMenuWriterTable(tag);
}

static bool PatchMenu2DWriterBuffers(uint32_t writer) {
    if (writer < 0x10000u) return false;
    void* posTarget = RedirectPosBatch();
    void* colTarget = RedirectColBatch();
    void* uvTarget = RedirectUvBatch();
    if (!posTarget && !colTarget && !uvTarget) return false;
    bool ok = false;
    __try {
        if (posTarget) {
            const uint32_t oldPos = *reinterpret_cast<volatile uint32_t*>(writer + 0x0C);
            RememberStalePtr(&g_stalePos86048,
                reinterpret_cast<void*>(static_cast<uintptr_t>(oldPos)), posTarget);
            *reinterpret_cast<volatile uint32_t*>(writer + 0x0C) =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(posTarget));
            ok = true;
        }
        if (colTarget) {
            const uint32_t oldCol = *reinterpret_cast<volatile uint32_t*>(writer + 0x14);
            if (IsBootHeapRange(oldCol))
                RegisterBootBatchPtr(&g_bootColBatch860,
                    reinterpret_cast<void*>(static_cast<uintptr_t>(oldCol)), kGameColBatch860);
            else
                RememberStalePtr(&g_staleCol86016,
                    reinterpret_cast<void*>(static_cast<uintptr_t>(oldCol)), colTarget);
            *reinterpret_cast<volatile uint32_t*>(writer + 0x14) =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(colTarget));
            ok = true;
        }
        if (uvTarget) {
            const uint32_t oldUv = *reinterpret_cast<volatile uint32_t*>(writer + 0x18);
            if (IsBootHeapRange(oldUv))
                RegisterBootBatchPtr(&g_bootUvBatch860,
                    reinterpret_cast<void*>(static_cast<uintptr_t>(oldUv)), kGameUvBatch860);
            else
                RememberStalePtr(&g_staleUv8608,
                    reinterpret_cast<void*>(static_cast<uintptr_t>(oldUv)), uvTarget);
            *reinterpret_cast<volatile uint32_t*>(writer + 0x18) =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(uvTarget));
            ok = true;
        }
        return ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool RebindMenu2DBatchObject(uint32_t batchObj) {
    if (batchObj < 0x10000u) return false;
    uint32_t sub = 0;
    __try {
        sub = *reinterpret_cast<volatile uint32_t*>(batchObj + 0x94);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (sub < 0x10000u) return false;
    bool ok = false;
    ok = PatchMenu2DWriterBuffers(sub) || ok;
    ok = PatchMenu2DWriterBuffers(sub + 0x6C) || ok;
    return ok;
}

// g_Menu2D_BatchMaster is an embedded BSS struct at base+0xCCC81C (sub_684E70 this), NOT a heap pointer.
static void RebindMenu2DNodeBatchBuffers(const char* tag) {
    if (!g_sgBatchArmed || g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    if (!g_base) {
        HookLog("[ffx-hooks] FullGridCompiler %s Menu2D-rebind skip: base null", tag);
        return;
    }
    const uint32_t master = static_cast<uint32_t>(g_base + RVA_FFX_MENU2D_BATCH_MASTER_PTR);
    const bool haveNodeBatch = g_latestPosBatch48 || g_latestColorBatch16 || g_latestUvBatch8;
    if (!haveNodeBatch) {
        static uint32_t s_skipNoPtrsGen = 0;
        if (s_skipNoPtrsGen != g_layoutGen) {
            s_skipNoPtrsGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s Menu2D-rebind skip: latest ptrs pos=%p col=%p uv=%p (r16=%d r48=%d r8=%d)",
                tag, g_latestPosBatch48, g_latestColorBatch16, g_latestUvBatch8,
                g_batch861Ready16 ? 1 : 0, g_batch861Ready48 ? 1 : 0, g_batch861Ready8 ? 1 : 0);
        }
        return;
    }
    static const int kBatchSlots[] = { 136, 140, 144, 148, 152, 156, 160, 176, 180, 184, 188, 192 };
    int rebound = 0;
    for (int idx = 34; idx <= 56; ++idx) {
        uint32_t ctx = 0;
        __try {
            ctx = *reinterpret_cast<volatile uint32_t*>(master + static_cast<uintptr_t>(idx) * 4u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (ctx < 0x10000u) continue;
        uint32_t root = 0;
        __try {
            root = *reinterpret_cast<volatile uint32_t*>(ctx + 0x94);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (root < 0x10000u) continue;
        if (PatchMenu2DWriterBuffers(root + 0x6C)) ++rebound;
        if (PatchMenu2DWriterBuffers(root)) ++rebound;
    }
    for (int off : kBatchSlots) {
        uint32_t batchObj = 0;
        __try {
            batchObj = *reinterpret_cast<volatile uint32_t*>(master + off);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (RebindMenu2DBatchObject(batchObj)) ++rebound;
    }
    g_batchPointersRebound = rebound > 0;
    if (rebound > 0) {
        HookLog("[ffx-hooks] FullGridCompiler %s Menu2D-rebind master=0x%08X rebound=%d pos=%p col=%p uv=%p ok=%d",
            tag, master, rebound, g_latestPosBatch48, g_latestColorBatch16, g_latestUvBatch8,
            g_batchPointersRebound ? 1 : 0);
    }
}

// sub_7F4900 slot index from n861 arg. Positive >=861 remaps; negative uses ecx = -1 - n861
// (A4FE40 passes v3-NodeCount, e.g. -861 -> slot 860). Old shim only checked n861>=861 and missed
// the negative bypass that draws the ghost/extra node almost-invisibly then OOB-smashes on activate.
static int DrawBatchSlotFromN861(int n861) {
    if (n861 == 0xFFFF || n861 == -862)
        return -1;
    if (n861 >= 861)
        return n861 - 861;
    if (n861 < 0)
        return (-1) - n861;
    return n861;
}

using InitRuntimeStateFn = int(__cdecl*)(int16_t* state);
using NoArgIntFn = int(__cdecl*)();
using HeapAllocFn = void*(__cdecl*)(size_t size, void* align);
// sub_942B60 is __thiscall(state* ecx, int size). A __fastcall detour captures ecx (1st),
// edx (2nd, unused by thiscall) and the single stack arg (size) at the machine-ABI level.
using AllocCoreFn = int(__fastcall*)(void* thisptr, void* edx, int size);
using GameAllocFn = int(__cdecl*)(int size);
using DrawBatchFn = int(__cdecl*)(void* a1, int a2, int n861);
using ActivateNodeFn = int(__cdecl*)(char actor, int nodeIdx);

#ifdef FFXHOOKS_HAVE_POLYHOOK
static PLH::x86Detour* g_initDetour = nullptr;
static PLH::x86Detour* g_defaultStateDetour = nullptr;
static PLH::x86Detour* g_applyDetour = nullptr;
static PLH::x86Detour* g_saveDetour = nullptr;
static PLH::x86Detour* g_recomputeDetour = nullptr;
static PLH::x86Detour* g_layoutDetour = nullptr;
static PLH::x86Detour* g_allocDetour = nullptr;
static PLH::x86Detour* g_allocCoreDetour = nullptr;
static PLH::x86Detour* g_gameAllocDetour = nullptr;
static PLH::x86Detour* g_drawBatchDetour = nullptr;
static PLH::x86Detour* g_nodePlacementAnimDetour = nullptr;
static PLH::x86Detour* g_activateNodeDetour = nullptr;
static PLH::x86Detour* g_exitUiFlushDetour = nullptr;
static PLH::x86Detour* g_uploadBatchesDetour = nullptr;
static PLH::x86Detour* g_endCaptureDetour = nullptr;
static PLH::x86Detour* g_populateLinkBatchesDetour = nullptr;
static PLH::x86Detour* g_runPlacementFxDetour = nullptr;
static PLH::x86Detour* g_updatePlacementSlotDetour = nullptr;
static PLH::x86Detour* g_flushCaptureBatch48Detour = nullptr;
static PLH::x86Detour* g_prepSaveDetour = nullptr;
static uint64_t g_initTrampoline = 0;
static uint64_t g_defaultStateTrampoline = 0;
static uint64_t g_applyTrampoline = 0;
static uint64_t g_saveTrampoline = 0;
static uint64_t g_recomputeTrampoline = 0;
static uint64_t g_prepSaveTrampoline = 0;
static volatile LONG g_prepSkipFieldCount = 0;
static uint64_t g_layoutTrampoline = 0;
static uint64_t g_allocTrampoline = 0;
static uint64_t g_allocCoreTrampoline = 0;
static uint64_t g_gameAllocTrampoline = 0;
static uint64_t g_drawBatchTrampoline = 0;
static uint64_t g_nodePlacementAnimTrampoline = 0;
static uint64_t g_activateNodeTrampoline = 0;
static uint64_t g_exitUiFlushTrampoline = 0;
static uint64_t g_uploadBatchesTrampoline = 0;
static uint64_t g_endCaptureTrampoline = 0;
static uint64_t g_populateLinkBatchesTrampoline = 0;
static uint64_t g_runPlacementFxTrampoline = 0;
static uint64_t g_updatePlacementSlotTrampoline = 0;
static uint64_t g_flushCaptureBatch48Trampoline = 0;
#endif

/* Allocator trace (SGM exit-crash): find the heap buffer sized to the vanilla node
 * count (860) that the 861st node overflows. Logs each unique (callerRVA,size) once
 * for sizes that look node-count-derived (860*N / 861*N, small header tolerance). */
static bool g_traceAlloc = false;
static int  g_guardPad = 0;   // bytes of trailing slack added to EVERY heap alloc (mitigation)
static int  g_canaryFloor = 512;  // min alloc size to canary/track (tunable: sg_canary_floor.flag)
struct AllocTraceSeen { uint32_t callerRva; uint32_t size; };
static AllocTraceSeen g_allocSeen[4096] = {};
static int g_allocSeenCount = 0;
static bool g_allocTraceArmed = false;

// CANARY: tag the byte right after each instrumented alloc's user region. A trailing
// heap overflow (the 861st-node menu-2D buffer) smashes the canary first; the FaultProbe
// VEH scans this table at crash time and prints the exact (callerRva,size) that overran.
static const unsigned char kCanaryByte = 0xC3;
static const int kCanaryBytes = 16;
struct CanaryEntry { void* ptr; uint32_t userSize; uint32_t callerRva; uint32_t frames[5]; };
static CanaryEntry g_canary[16384] = {};
static int g_canaryCount = 0;
static CRITICAL_SECTION g_canaryCs;
static bool g_canaryInit = false;
static void ScanCanariesImpl(const char* tag);   // defined below; called from SG shims + VEH
static void FindAllocOwner(uintptr_t addr, const char* tag);  // defined below
static void ScanForCorruptFloat(const char* tag);
static void DumpCoreCensus(const char* tag);
static int BumpNodeBatchAllocSize(int size);
static int PatchAbmapMenuWriterTable(const char* tag);

static bool AllocSizeLooksNodeCount(size_t size) {
    // Match size ~= count*stride (+ small header) for the vanilla count (860) or live
    // count (861), across any plausible per-node stride. The exit-crash buffer is a
    // node-count-derived buffer NOT in the static menu struct (the enter path A45570 +
    // callees only fill the menu struct, node cap 1024) — most likely a render/geometry
    // work buffer allocated on first draw, so the stride/size can be large. The old
    // [850,40100] window with a tiny stride list missed those. Keep the stride list
    // "clean" (real strides) to bound false positives.
    if (size < 800 || size > 600000) return false;   // 860*1 .. ~600KB
    // Modulo model: size == count*stride + header for ANY per-node stride, header<=96.
    // The stride-list version missed the culprit (none matched in a whole-process census),
    // so don't assume the stride; just require size to be a near-multiple of 860 or 861.
    const long s = static_cast<long>(size);
    for (int base = 860; base <= 861; ++base) {
        const long q = s / base;
        if (q >= 1 && q <= 4096 && (s - q * base) <= 96) return true;
    }
    return false;
}

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[768] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static const SgCrashIncidentDef* LookupCrashIncident(const char* incidentId) {
    if (!incidentId) return nullptr;
    for (const SgCrashIncidentDef& e : kCrashRegistry) {
        if (strcmp(e.id, incidentId) == 0) return &e;
    }
    return nullptr;
}

static void AppendCrashJournal(const char* line) {
    char path[MAX_PATH] = {};
    DWORD n = GetTempPathA(sizeof(path), path);
    if (n == 0 || n >= sizeof(path)) return;
    strncat(path, "ffx-hooks-sgm-incidents.log", sizeof(path) - strlen(path) - 1);
    FILE* f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u %s\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, line);
    fclose(f);
}

static void RecordCrashIncident(const char* incidentId, const char* site, const char* detail) {
    const SgCrashIncidentDef* def = LookupCrashIncident(incidentId);
    const char* root = def ? def->rootCause : "unregistered — append row to CRASH INCIDENT REGISTRY in hook file";
    const char* fix = def ? def->fixNote : "investigate; document in hook file before next attempt";
    const char* seen = def ? def->seenVer : "?";
    const char* fixed = def ? def->fixedVer : "OPEN";
    uint16_t menuNodes = 0, menuLinks = 0;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (menu) {
        __try {
            menuNodes = *reinterpret_cast<volatile uint16_t*>(menu + 2);
            menuLinks = *reinterpret_cast<volatile uint16_t*>(menu + 4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    HookLog("[ffx-hooks] CRASH-RECORD id=%s hook=%s site=%s seen=%s fixed=%s detail=%s",
        incidentId ? incidentId : "SG-C???", kFullGridCompilerVersion,
        site ? site : "?", seen, fixed, detail ? detail : "");
    HookLog("[ffx-hooks] CRASH-RECORD cause=%s", root);
    HookLog("[ffx-hooks] CRASH-RECORD fix=%s ctx={shim=%s activation=%d exit=%d menu=%u/%u nodes=%d armed=%d}",
        fix, g_activeSgShim, g_sgActivationActive ? 1 : 0, g_exitUiFlushActive ? 1 : 0,
        menuNodes, menuLinks, g_trustedAssetNodes, g_sgBatchArmed ? 1 : 0);
    char journal[896] = {};
    snprintf(journal, sizeof(journal),
        "id=%s hook=%s site=%s seen=%s fixed=%s shim=%s act=%d exit=%d menu=%u/%u | %s | CAUSE: %s | FIX: %s",
        incidentId ? incidentId : "SG-C???", kFullGridCompilerVersion, site ? site : "?",
        seen, fixed, g_activeSgShim, g_sgActivationActive ? 1 : 0, g_exitUiFlushActive ? 1 : 0,
        menuNodes, menuLinks, detail ? detail : "", root, fix);
    AppendCrashJournal(journal);
}

static const char* ClassifyVehFault(uint32_t pcRva, uint32_t accessed, uint32_t eax) {
    if (eax == 0x3F008081u) return "SG-C001";
    if (g_sgActivationActive) {
        /* clamp regression vs producer OOB — check menu count if accessible */
        volatile uint8_t* menu = AbmapMenuStateBase();
        if (menu) {
            __try {
                const uint16_t nc = *reinterpret_cast<volatile uint16_t*>(menu + 2);
                if (nc == static_cast<uint16_t>(kVanillaNodeCapacity)) return "SG-C003";
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return "SG-C004";
    }
    if (g_exitUiFlushActive) return "SG-C001";
    if (pcRva >= 0x00648900u && pcRva <= 0x00648A00u) return "SG-C003";
    if (pcRva >= 0x00647D00u && pcRva <= 0x00647E00u) return "SG-C004";
    if (pcRva >= 0x00654850u && pcRva <= 0x00654950u) return "SG-C005";
    if (pcRva >= 0x006859D0u && pcRva <= 0x00685A50u) return "SG-C001";
    if (pcRva >= 0x00542B00u && pcRva <= 0x00542D00u) return "SG-C011";  /* boot alloc shrink */
    if (pcRva >= 0x003F4900u && pcRva <= 0x003F4A00u && !g_sgActivationActive) return "SG-C013";
    if (pcRva >= 0x00543100u && pcRva <= 0x00543600u) return "SG-C001";  /* heap free 943xxx */
    if ((accessed & 0xFF000000u) == 0x3F000000u) return "SG-C001";
    return "SG-C???";
}

static void LogCrashRegistrySummary() {
    HookLog("[ffx-hooks] FullGridCompiler incident-registry hook=%s crashes=%u failed-fixes=%u "
        "(see hook file + %%TEMP%%\\ffx-hooks-sgm-incidents.log)",
        kFullGridCompilerVersion,
        static_cast<unsigned>(sizeof(kCrashRegistry) / sizeof(kCrashRegistry[0])),
        static_cast<unsigned>(sizeof(kFailedFixRegistry) / sizeof(kFailedFixRegistry[0])));
    for (const SgCrashIncidentDef& e : kCrashRegistry)
        HookLog("[ffx-hooks]   %s [%s] fixed=%s site=%s", e.id, e.seenVer, e.fixedVer, e.site);
    for (const SgFailedFixDef& e : kFailedFixRegistry)
        HookLog("[ffx-hooks]   %s [%s] AVOID: %s", e.id, e.ver, e.approach);
}

static bool EnvFlagEnabled(const char* name) {
    char value[16] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
        value[0] == 't' || value[0] == 'T');
}

static bool ModuleDir(char* out, size_t outSize) {
    HMODULE self = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&ModuleDir),
        &self);
    if (!self) return false;
    if (GetModuleFileNameA(self, out, static_cast<DWORD>(outSize)) == 0) return false;
    char* slash = strrchr(out, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';
    return true;
}

static bool FileExists(const char* path) {
    if (!path || !path[0]) return false;
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool HookModuleFlagExists(const char* relativePath) {
    char dir[MAX_PATH] = {};
    if (!ModuleDir(dir, sizeof(dir))) return false;
    char path[MAX_PATH] = {};
    snprintf(path, sizeof(path), "%s%s", dir, relativePath);
    return FileExists(path);
}

static int HookModuleFlagReadInt(const char* relativePath, int dflt) {
    char dir[MAX_PATH] = {};
    if (!ModuleDir(dir, sizeof(dir))) return 0;
    char path[MAX_PATH] = {};
    snprintf(path, sizeof(path), "%s%s", dir, relativePath);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;                       // flag absent -> disabled
    char buf[32] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return dflt;                // present but empty -> default
    int v = atoi(buf);
    return v > 0 ? v : dflt;
}

static void ConfigureModes() {
    g_observeOnly = !EnvFlagEnabled("FFXHOOKS_SG_FULL_GRID_COMPILER_WRITE");
    if (EnvFlagEnabled("FFXHOOKS_SG_FULL_GRID_COMPILER_OBSERVE_ONLY"))
        g_observeOnly = true;
    g_writeCompile = EnvFlagEnabled("FFXHOOKS_SG_FULL_GRID_COMPILER_WRITE");
    g_writeSidecar = EnvFlagEnabled("FFXHOOKS_SG_FULL_GRID_COMPILER_WRITE_SIDECAR");
    g_clampVanillaLoop = EnvFlagEnabled("FFXHOOKS_SG_FULL_GRID_CLAMP_VANILLA_LOOP");
    g_skipHashCheck = EnvFlagEnabled("FFXHOOKS_SG_FULL_GRID_SKIP_HASH");
    g_traceAlloc = EnvFlagEnabled("FFXHOOKS_SG_TRACE_ALLOC") || HookModuleFlagExists("sg_trace_alloc.flag");
    g_drawSlot860Experimental = EnvFlagEnabled("FFXHOOKS_SG_DRAW_SLOT860") ||
        HookModuleFlagExists("sg_draw_slot860.flag") ||
        EnvFlagEnabled("FFXHOOKS_SG_DRAW_CLAMP860") ||
        HookModuleFlagExists("sg_draw_clamp860.flag");
    g_sgF1InlineActive = EnvFlagEnabled("FFXHOOKS_SG_F1_INLINE") ||
        HookModuleFlagExists("sg_f1_inline.flag") ||
        HookModuleFlagExists("config\\sg_f1_inline.flag");
    /* v1.79 SG-C045: sg_f1_inline.flag alone does NOT patch 7F4900 (stubs break all draws). */
    g_sgF1InlineInstallPatches = EnvFlagEnabled("FFXHOOKS_SG_F1_INLINE_INSTALL") ||
        HookModuleFlagExists("sg_f1_inline_install.flag") ||
        HookModuleFlagExists("config\\sg_f1_inline_install.flag");
    /* v1.76: inline patches alone do NOT lift SKIP860 — col/uv store sites still vanilla OOB */
    g_sgF1InlineLiftSkip860 = g_sgF1InlineActive && (
        EnvFlagEnabled("FFXHOOKS_SG_F1_INLINE_DRAW860") ||
        HookModuleFlagExists("sg_f1_inline_draw860.flag") ||
        HookModuleFlagExists("config\\sg_f1_inline_draw860.flag"));
    g_guardPad = HookModuleFlagReadInt("sg_guard_pad.flag", 256);  // mitigation: pad every alloc
    { int cf = HookModuleFlagReadInt("sg_canary_floor.flag", 0); if (cf > 0) g_canaryFloor = cf; }
    if (g_observeOnly) {
        g_writeCompile = false;
        g_writeSidecar = false;
    }
}

static inline volatile uint8_t* RuntimeStateBase() {
    if (!g_base) return nullptr;
    return reinterpret_cast<volatile uint8_t*>(g_base + RVA_FFX_SPHERE_GRID_RUNTIME_STATE_TABLE);
}

static bool RuntimeStateHasSaveGridProgress(volatile uint8_t* state) {
    if (!state) return false;
    int hits = 0;
    __try {
        for (int i = 0; i < kVanillaNodeCapacity; ++i) {
            const uintptr_t off = static_cast<uintptr_t>(i) * 2u;
            if (state[off] != 0 || state[off + 1] != 0) {
                if (++hits >= 3) return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

static inline volatile uint8_t* AbmapMenuStateBase() {
    if (!g_base) return nullptr;
    __try {
        const uintptr_t ptr = *reinterpret_cast<volatile uintptr_t*>(g_base + RVA_FFX_ABMAP_MENU_STATE_PTR);
        if (ptr < 0x10000u) return nullptr;
        return reinterpret_cast<volatile uint8_t*>(ptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ABMAP SG draws via writers in menu+0xF858 (NOT g_Menu2D_BatchMaster @ 0xCCC81C field batch).
// Per A51340: entry+0/+4 = active/inactive Menu2D writer ptrs; entry+8 = vertex payload for
// sub_7F4900(a1=...) — NOT writer+0x0C/+0x14/+0x18 (IDA: negative n861 uses capture ctx writer).
static int PatchAbmapMenuWriterTable(const char* tag) {
    if (!g_sgBatchArmed || g_trustedAssetNodes <= kVanillaNodeCapacity) return 0;
    if (!g_latestPosBatch48 && !g_latestColorBatch16 && !g_latestUvBatch8) return 0;
    HarvestStaleBatchPointersFromWriters();
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return 0;
    int patched = 0;
    int skippedPayload = 0;
    __try {
        volatile uint8_t* table = menu + kAbmapMenuWriterTableOffset;
        for (int i = 0; i < kAbmapMenuWriterEntries; ++i) {
            volatile uint8_t* entry = table + static_cast<uintptr_t>(i) * kAbmapMenuWriterStride;
            for (int w = 0; w < 2; ++w) {
                const uint32_t writer = *reinterpret_cast<volatile uint32_t*>(entry + w * 4);
                if (PatchMenu2DWriterBuffers(writer)) ++patched;
            }
            const uint32_t payloadPtr = *reinterpret_cast<volatile uint32_t*>(entry + 8);
            if (payloadPtr >= 0x10000u) ++skippedPayload;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return patched;
    }
    static uint32_t s_logGen = 0;
    static int s_lastPatched = 0;
    if (patched > 0 || skippedPayload > 0) {
        if (s_logGen != g_layoutGen || s_lastPatched != patched) {
            s_logGen = g_layoutGen;
            s_lastPatched = patched;
            HookLog("[ffx-hooks] FullGridCompiler %s ABMAP-writer-patch entries=%d patched=%d "
                "payload-fields=%d (entry+8 skipped) pos=%p col=%p uv=%p",
                tag, kAbmapMenuWriterEntries, patched, skippedPayload,
                g_latestPosBatch48, g_latestColorBatch16, g_latestUvBatch8);
        }
    }
    return patched;
}

struct MenuCountRestore {
    volatile uint8_t* menu = nullptr;
    uint16_t nodes = 0;
    uint16_t links = 0;
    bool active = false;
};

static bool BeginVanillaMenuCountClamp(MenuCountRestore* out, const char* tag) {
    if (!out) return false;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return false;
    __try {
        uint16_t nc = *reinterpret_cast<volatile uint16_t*>(menu + 2);
        uint16_t lc = *reinterpret_cast<volatile uint16_t*>(menu + 4);
        if (nc <= static_cast<uint16_t>(kVanillaNodeCapacity)) {
            // Menu header may still be 0 before A45570; batch paths can still see 861 via overlay.
            if (g_trustedAssetNodes <= kVanillaNodeCapacity) return false;
            nc = static_cast<uint16_t>(g_trustedAssetNodes);
            lc = static_cast<uint16_t>(g_trustedAssetLinks > 0 ? g_trustedAssetLinks : kVanillaLinkCapacity);
        }
        out->menu = menu;
        out->nodes = *reinterpret_cast<volatile uint16_t*>(menu + 2);
        out->links = *reinterpret_cast<volatile uint16_t*>(menu + 4);
        *reinterpret_cast<volatile uint16_t*>(menu + 2) = static_cast<uint16_t>(kVanillaNodeCapacity);
        *reinterpret_cast<volatile uint16_t*>(menu + 4) = static_cast<uint16_t>(kVanillaLinkCapacity);
        out->active = true;
        HookLog("[ffx-hooks] FullGridCompiler %s TEMP clamp menu %u/%u -> %d/%d (trusted=%d)",
            tag, out->nodes, out->links, kVanillaNodeCapacity, kVanillaLinkCapacity, g_trustedAssetNodes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void EndVanillaMenuCountClamp(MenuCountRestore* r) {
    if (!r || !r->active || !r->menu) return;
    __try {
        *reinterpret_cast<volatile uint16_t*>(r->menu + 2) = r->nodes;
        *reinterpret_cast<volatile uint16_t*>(r->menu + 4) = r->links;
        HookLog("[ffx-hooks] FullGridCompiler restored menu %u/%u after temp clamp", r->nodes, r->links);
        r->active = false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool ReadEnvHash(const char* name, char* out, size_t outLen) {
    DWORD n = GetEnvironmentVariableA(name, out, static_cast<DWORD>(outLen));
    return n > 0 && n < outLen && strlen(out) == 64;
}

static bool ReadEnvString(const char* name, char* out, size_t outLen) {
    if (!out || outLen < 2) return false;
    DWORD n = GetEnvironmentVariableA(name, out, static_cast<DWORD>(outLen));
    if (n == 0 || n >= outLen) {
        out[0] = '\0';
        return false;
    }
    out[n] = '\0';
    return true;
}

static bool ReadEnvInt(const char* name, int* out) {
    if (!out) return false;
    char value[32] = {};
    DWORD n = GetEnvironmentVariableA(name, value, sizeof(value));
    if (n == 0 || n >= sizeof(value)) return false;
    char* tail = nullptr;
    long parsed = strtol(value, &tail, 10);
    if (tail == value || parsed <= 0 || parsed > 1024) return false;
    *out = static_cast<int>(parsed);
    return true;
}

static void EnsureSidecarIdentityFromEnv() {
    if (!g_sidecar.saveSha256[0])
        ReadEnvHash("FFXHOOKS_SG_SAVE_SHA256", g_sidecar.saveSha256, sizeof(g_sidecar.saveSha256));
    if (!g_sidecar.layoutSha256[0])
        ReadEnvHash("FFXHOOKS_SG_LAYOUT_SHA256", g_sidecar.layoutSha256, sizeof(g_sidecar.layoutSha256));
    if (!g_sidecar.contentsSha256[0])
        ReadEnvHash("FFXHOOKS_SG_CONTENTS_SHA256", g_sidecar.contentsSha256, sizeof(g_sidecar.contentsSha256));
    if (!g_sidecar.profileKey[0])
        ReadEnvString("FFXHOOKS_SG_PROFILE_KEY", g_sidecar.profileKey, sizeof(g_sidecar.profileKey));
}

static bool HexEqual64(const char* a, const char* b) {
    if (!a || !b) return false;
    return _stricmp(a, b) == 0;
}

static bool ReadStringField(const char* start, const char* end, const char* key, char* out, size_t outLen) {
    if (!start || !end || !key || !out || outLen < 2) return false;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* hit = strstr(start, needle);
    if (!hit || hit >= end) return false;
    const char* p = hit + strlen(needle);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p != '"') return false;
    ++p;
    size_t i = 0;
    while (p < end && *p != '"' && i + 1 < outLen) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static bool ReadIntField(const char* start, const char* end, const char* key, int* out) {
    if (!start || !end || !key || !out) return false;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* hit = strstr(start, needle);
    if (!hit || hit >= end) return false;
    const char* p = hit + strlen(needle);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    int sign = 1;
    if (p < end && *p == '-') { sign = -1; ++p; }
    int v = 0;
    bool seen = false;
    while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; seen = true; }
    if (!seen) return false;
    *out = sign * v;
    return true;
}

static bool LoadTrustedAssetCountsFromManifest() {
    char dir[MAX_PATH] = {};
    if (!ModuleDir(dir, sizeof(dir))) return false;

    char path[MAX_PATH] = {};
    snprintf(path, sizeof(path), "%sconfig\\square_grid_manifest.json", dir);
    if (!FileExists(path)) return false;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024) {
        CloseHandle(h);
        return false;
    }

    char* buf = static_cast<char*>(malloc(static_cast<size_t>(size.QuadPart) + 1));
    if (!buf) {
        CloseHandle(h);
        return false;
    }

    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, static_cast<DWORD>(size.QuadPart), &got, nullptr);
    CloseHandle(h);
    if (!ok || got == 0) {
        free(buf);
        return false;
    }

    buf[got] = '\0';
    const char* end = buf + got;
    int nodes = 0, links = 0;
    const bool nodesOk = ReadIntField(buf, end, "node_count", &nodes);
    const bool linksOk = ReadIntField(buf, end, "link_count", &links);
    free(buf);

    if (!nodesOk || !linksOk || nodes <= 0 || links <= 0 || nodes > 1024 || links > 1024)
        return false;

    g_trustedAssetNodes = nodes;
    g_trustedAssetLinks = links;
    HookLog("[ffx-hooks] FullGridCompiler trusted asset counts from manifest nodes=%d links=%d path=%s",
        g_trustedAssetNodes, g_trustedAssetLinks, path);
    return true;
}

static void LoadTrustedAssetCounts() {
    int envNodes = 0, envLinks = 0;
    const bool envNodesOk = ReadEnvInt("FFXHOOKS_SG_ASSET_NODES", &envNodes);
    const bool envLinksOk = ReadEnvInt("FFXHOOKS_SG_ASSET_LINKS", &envLinks);
    if (envNodesOk && envLinksOk) {
        g_trustedAssetNodes = envNodes;
        g_trustedAssetLinks = envLinks;
        HookLog("[ffx-hooks] FullGridCompiler trusted asset counts from env nodes=%d links=%d",
            g_trustedAssetNodes, g_trustedAssetLinks);
        return;
    }

    if (!LoadTrustedAssetCountsFromManifest()) {
        HookLog("[ffx-hooks] FullGridCompiler trusted asset counts unavailable; live header fallback active");
    }
}

static const char* FindArrayBody(const char* start, const char* end, const char* key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* hit = strstr(start, needle);
    if (!hit || hit >= end) return nullptr;
    const char* p = hit + strlen(needle);
    while (p < end && *p != '[') {
        if (*p == '"' || *p == '{') return nullptr;
        ++p;
    }
    if (p >= end) return nullptr;
    return p + 1;
}

static bool ParseExtraNodesArray(const char* body, const char* end) {
    g_sidecar.nodeCount = 0;
    const char* p = body;
    while (p < end && g_sidecar.nodeCount < kMaxExtraNodes) {
        while (p < end && *p != '{') {
            if (*p == ']') return true;
            ++p;
        }
        if (p >= end) break;
        const char* objEnd = strchr(p, '}');
        if (!objEnd || objEnd > end) break;
        int nodeId = -1, content = 0, status = 0;
        if (!ReadIntField(p, objEnd, "node_id", &nodeId)) { p = objEnd + 1; continue; }
        ReadIntField(p, objEnd, "content", &content);
        ReadIntField(p, objEnd, "status", &status);
        ExtraNodeEntry& e = g_sidecar.nodes[g_sidecar.nodeCount++];
        e.nodeId = static_cast<uint16_t>(nodeId & 0xFFFF);
        e.content = static_cast<uint8_t>(content & 0xFF);
        e.status = static_cast<uint8_t>(status & 0xFF);
        e.present = true;
        p = objEnd + 1;
    }
    return true;
}

static bool ParseExtraLinksArray(const char* body, const char* end) {
    g_sidecar.linkCount = 0;
    const char* p = body;
    while (p < end && g_sidecar.linkCount < kMaxExtraLinks) {
        while (p < end && *p != '{') {
            if (*p == ']') return true;
            ++p;
        }
        if (p >= end) break;
        const char* objEnd = strchr(p, '}');
        if (!objEnd || objEnd > end) break;
        int linkId = -1, state = 0;
        if (!ReadIntField(p, objEnd, "link_id", &linkId)) { p = objEnd + 1; continue; }
        ReadIntField(p, objEnd, "state", &state);
        ExtraLinkEntry& e = g_sidecar.links[g_sidecar.linkCount++];
        e.linkId = static_cast<uint16_t>(linkId & 0xFFFF);
        e.state = static_cast<uint8_t>(state & 0xFF);
        e.present = true;
        p = objEnd + 1;
    }
    return true;
}

static void ResolveSidecarPath() {
    if (g_sidecarPath[0]) return;

    char envPath[MAX_PATH] = {};
    DWORD envLen = GetEnvironmentVariableA("FFXHOOKS_SG_FULL_GRID_SIDECAR_PATH", envPath, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        strcpy_s(g_sidecarPath, envPath);
        return;
    }

    char profileKey[128] = "default";
    char shaPrefix[32] = {};
    char dir[MAX_PATH] = {};
    if (!ModuleDir(dir, sizeof(dir))) {
        strcpy_s(g_sidecarPath, "sphere-grid-extra.json");
        return;
    }

    DWORD pkLen = GetEnvironmentVariableA("FFXHOOKS_SG_PROFILE_KEY", profileKey, sizeof(profileKey));
    if (pkLen == 0 || pkLen >= sizeof(profileKey))
        strcpy_s(profileKey, "default");

    DWORD spLen = GetEnvironmentVariableA("FFXHOOKS_SG_SAVE_SHA_PREFIX", shaPrefix, sizeof(shaPrefix));
    if (spLen >= 8 && spLen < sizeof(shaPrefix)) {
        snprintf(g_sidecarPath, sizeof(g_sidecarPath),
            "%smods\\Spira Reforge\\save-sidecars\\%s\\%s.sphere-grid-extra.json",
            dir, profileKey, shaPrefix);
        return;
    }

    snprintf(g_sidecarPath, sizeof(g_sidecarPath),
        "%smods\\Spira Reforge\\save-sidecars\\%s\\active.sphere-grid-extra.json",
        dir, profileKey);
}

static bool LoadSidecarFile() {
    memset(&g_sidecar, 0, sizeof(g_sidecar));
    ResolveSidecarPath();

    if (!FileExists(g_sidecarPath)) {
        HookLog("[ffx-hooks] FullGridCompiler sidecar missing: %s", g_sidecarPath);
        return false;
    }

    HANDLE h = CreateFileA(g_sidecarPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        HookLog("[ffx-hooks] FullGridCompiler sidecar open failed: %s", g_sidecarPath);
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 256 * 1024) {
        CloseHandle(h);
        HookLog("[ffx-hooks] FullGridCompiler sidecar size invalid");
        return false;
    }
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(size.QuadPart) + 1));
    if (!buf) {
        CloseHandle(h);
        return false;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, static_cast<DWORD>(size.QuadPart), &got, nullptr);
    CloseHandle(h);
    if (!ok || got == 0) {
        free(buf);
        return false;
    }
    buf[got] = '\0';
    const char* end = buf + got;

    ReadStringField(buf, end, "save_sha256", g_sidecar.saveSha256, sizeof(g_sidecar.saveSha256));
    ReadStringField(buf, end, "layout_sha256", g_sidecar.layoutSha256, sizeof(g_sidecar.layoutSha256));
    ReadStringField(buf, end, "contents_sha256", g_sidecar.contentsSha256, sizeof(g_sidecar.contentsSha256));
    ReadStringField(buf, end, "profile_key", g_sidecar.profileKey, sizeof(g_sidecar.profileKey));

    int vn = kVanillaNodeCapacity, vl = kVanillaLinkCapacity;
    ReadIntField(buf, end, "nodes", &vn);
    const char* vcBody = strstr(buf, "\"vanilla_capacity\"");
    if (vcBody && vcBody < end)
        ReadIntField(vcBody, end, "nodes", &vn);

    const char* acBody = strstr(buf, "\"asset_counts\"");
    int an = 0, al = 0;
    if (acBody && acBody < end) {
        ReadIntField(acBody, end, "nodes", &an);
        ReadIntField(acBody, end, "links", &al);
    }
    g_sidecar.vanillaNodes = vn > 0 ? vn : kVanillaNodeCapacity;
    g_sidecar.vanillaLinks = vl > 0 ? vl : kVanillaLinkCapacity;
    g_sidecar.assetNodes = an;
    g_sidecar.assetLinks = al;

    const char* extraNodes = FindArrayBody(buf, end, "extra_nodes");
    if (extraNodes)
        ParseExtraNodesArray(extraNodes, end);
    const char* extraLinks = FindArrayBody(buf, end, "extra_links");
    if (extraLinks)
        ParseExtraLinksArray(extraLinks, end);

    free(buf);
    g_sidecar.loaded = true;

    char expectSave[65] = {}, expectLayout[65] = {}, expectContents[65] = {};
    const bool hasSave = ReadEnvHash("FFXHOOKS_SG_SAVE_SHA256", expectSave, sizeof(expectSave));
    const bool hasLayout = ReadEnvHash("FFXHOOKS_SG_LAYOUT_SHA256", expectLayout, sizeof(expectLayout));
    const bool hasContents = ReadEnvHash("FFXHOOKS_SG_CONTENTS_SHA256", expectContents, sizeof(expectContents));

    if (g_skipHashCheck) {
        g_sidecar.hashMatched = true;
        HookLog("[ffx-hooks] FullGridCompiler sidecar loaded SKIP_HASH nodes=%d links=%d path=%s",
            g_sidecar.nodeCount, g_sidecar.linkCount, g_sidecarPath);
        return true;
    }

    bool layoutOk = !hasLayout || HexEqual64(g_sidecar.layoutSha256, expectLayout);
    bool contentsOk = !hasContents || HexEqual64(g_sidecar.contentsSha256, expectContents);
    bool saveOk = !hasSave || HexEqual64(g_sidecar.saveSha256, expectSave);

    if (!hasLayout && !hasContents && !hasSave) {
        HookLog("[ffx-hooks] FullGridCompiler sidecar loaded (no env hashes — apply without save binding) nodes=%d links=%d",
            g_sidecar.nodeCount, g_sidecar.linkCount);
        g_sidecar.hashMatched = true;
        return true;
    }

    g_sidecar.hashMatched = layoutOk && contentsOk && saveOk;
    if (!g_sidecar.hashMatched) {
        HookLog("[ffx-hooks] FullGridCompiler HASH MISMATCH — ignore stale sidecar layout=%d contents=%d save=%d",
            layoutOk ? 1 : 0, contentsOk ? 1 : 0, saveOk ? 1 : 0);
        g_sidecar.nodeCount = 0;
        g_sidecar.linkCount = 0;
        return true;
    }

    HookLog("[ffx-hooks] FullGridCompiler sidecar matched nodes=%d links=%d asset=%d/%d path=%s",
        g_sidecar.nodeCount, g_sidecar.linkCount, g_sidecar.assetNodes, g_sidecar.assetLinks, g_sidecarPath);
    return true;
}

static bool GetAssetCounts(uint16_t* nodeCount, uint16_t* linkCount) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return false;
    __try {
        *nodeCount = *reinterpret_cast<volatile uint16_t*>(menu + 2);
        *linkCount = *reinterpret_cast<volatile uint16_t*>(menu + 4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void EffectiveAssetCounts(int* nodeCount, int* linkCount, uint16_t* liveNodes, uint16_t* liveLinks) {
    uint16_t liveNodeCount = 0, liveLinkCount = 0;
    GetAssetCounts(&liveNodeCount, &liveLinkCount);

    int trustedNodes = g_trustedAssetNodes;
    int trustedLinks = g_trustedAssetLinks;
    if ((!trustedNodes || !trustedLinks) && g_sidecar.loaded && g_sidecar.hashMatched) {
        if (!trustedNodes && g_sidecar.assetNodes > kVanillaNodeCapacity)
            trustedNodes = g_sidecar.assetNodes;
        if (!trustedLinks && g_sidecar.assetLinks > kVanillaLinkCapacity)
            trustedLinks = g_sidecar.assetLinks;
    }

    int effectiveNodes = trustedNodes > 0 ? trustedNodes :
        (liveNodeCount > 0 ? static_cast<int>(liveNodeCount) : kVanillaNodeCapacity);
    int effectiveLinks = trustedLinks > 0 ? trustedLinks :
        (liveLinkCount > 0 ? static_cast<int>(liveLinkCount) : kVanillaLinkCapacity);

    if (effectiveNodes < kVanillaNodeCapacity) effectiveNodes = kVanillaNodeCapacity;
    if (effectiveLinks < kVanillaLinkCapacity) effectiveLinks = kVanillaLinkCapacity;
    if (effectiveNodes > 1024) effectiveNodes = 1024;
    if (effectiveLinks > 1024) effectiveLinks = 1024;

    if (nodeCount) *nodeCount = effectiveNodes;
    if (linkCount) *linkCount = effectiveLinks;
    if (liveNodes) *liveNodes = liveNodeCount;
    if (liveLinks) *liveLinks = liveLinkCount;
}

static const ExtraNodeEntry* FindSidecarNode(uint16_t nodeId) {
    for (int i = 0; i < g_sidecar.nodeCount; i++) {
        if (g_sidecar.nodes[i].nodeId == nodeId)
            return &g_sidecar.nodes[i];
    }
    return nullptr;
}

static const ExtraLinkEntry* FindSidecarLink(uint16_t linkId) {
    for (int i = 0; i < g_sidecar.linkCount; i++) {
        if (g_sidecar.links[i].linkId == linkId)
            return &g_sidecar.links[i];
    }
    return nullptr;
}

static void ClampCursors(volatile uint8_t* state, int assetNodeCount, const char* tag, bool write) {
    if (!state || assetNodeCount <= 0) return;
    __try {
        for (int i = 0; i < kCursorSlots; i++) {
            volatile uint8_t* slot = state + kStateCursorOffset + static_cast<uintptr_t>(i);
            const uint8_t before = *slot;
            if (before >= static_cast<uint8_t>(assetNodeCount)) {
                const uint8_t clamped = 0;
                if (write && g_writeCompile)
                    *slot = clamped;
                HookLog("[ffx-hooks] FullGridCompiler %s cursor[%d] %u -> %u (clamp assetNodes=%d)%s",
                    tag, i, before, clamped, assetNodeCount,
                    write && g_writeCompile ? "" : " (observe)");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler WARN cursor clamp failed %s", tag);
    }
}

static void CompileExtraState(const char* tag, bool write) {
    volatile uint8_t* state = RuntimeStateBase();
    if (!state) {
        HookLog("[ffx-hooks] FullGridCompiler %s skipped (state table null)", tag);
        return;
    }

    int assetNodes = 0, assetLinks = 0;
    uint16_t liveNodes = 0, liveLinks = 0;
    EffectiveAssetCounts(&assetNodes, &assetLinks, &liveNodes, &liveLinks);
    const int vanillaNodes = g_sidecar.vanillaNodes > 0 ? g_sidecar.vanillaNodes : kVanillaNodeCapacity;
    const int vanillaLinks = g_sidecar.vanillaLinks > 0 ? g_sidecar.vanillaLinks : kVanillaLinkCapacity;
    const bool applySidecar = g_sidecar.loaded && g_sidecar.hashMatched;
    const bool doWrite = write && g_writeCompile && !g_observeOnly;

    int touchedNodes = 0;
    int touchedLinks = 0;

    HookLog("[ffx-hooks] FullGridCompiler %s counts live=%u/%u trusted=%d/%d sidecarAsset=%d/%d effective=%d/%d",
        tag, liveNodes, liveLinks, g_trustedAssetNodes, g_trustedAssetLinks,
        g_sidecar.assetNodes, g_sidecar.assetLinks, assetNodes, assetLinks);
    HookLog("[ffx-hooks] FullGridCompiler %s compile asset=%d/%d vanilla=%d/%d sidecar=%d hash=%d write=%d",
        tag, assetNodes, assetLinks, vanillaNodes, vanillaLinks,
        g_sidecar.loaded ? 1 : 0, applySidecar ? 1 : 0, doWrite ? 1 : 0);

  __try {
        for (int node = vanillaNodes; node < assetNodes && node < 1024; node++) {
            volatile uint8_t* slot = state + static_cast<uintptr_t>(node) * 2u;
            const uint8_t oldContent = slot[0];
            const uint8_t oldStatus = slot[1];
            uint8_t nextContent = oldContent;
            uint8_t nextStatus = oldStatus;

            if (applySidecar) {
                const ExtraNodeEntry* sc = FindSidecarNode(static_cast<uint16_t>(node));
                if (sc) {
                    nextContent = sc->content;
                    nextStatus = sc->status;
                }
            }

            if (nextContent != oldContent || nextStatus != oldStatus) {
                if (doWrite) {
                    slot[0] = nextContent;
                    slot[1] = nextStatus;
                }
                touchedNodes++;
                if (touchedNodes <= 8 || node >= assetNodes - 2) {
                    HookLog("[ffx-hooks] FullGridCompiler %s node=%d %02X/%02X -> %02X/%02X%s",
                        tag, node, oldContent, oldStatus, nextContent, nextStatus,
                        doWrite ? "" : " (observe)");
                }
            }
        }
        if (touchedNodes > 8)
            HookLog("[ffx-hooks] FullGridCompiler %s ... %d more node touches omitted", tag, touchedNodes - 8);

        for (int link = vanillaLinks; link < assetLinks && link < 1024; link++) {
            volatile uint8_t* slot = state + kStateLinkOffset + static_cast<uintptr_t>(link);
            const uint8_t oldState = *slot;
            uint8_t nextState = applySidecar ? 0 : oldState;
            const ExtraLinkEntry* sc = applySidecar ? FindSidecarLink(static_cast<uint16_t>(link)) : nullptr;
            if (sc)
                nextState = sc->state;
            else if (!applySidecar)
                nextState = oldState;

            if (nextState != oldState) {
                if (doWrite)
                    *slot = nextState;
                touchedLinks++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler WARN exception during compile %s", tag);
    }

    ClampCursors(state, assetNodes, tag, doWrite);
    g_compileRan = true;
    HookLog("[ffx-hooks] FullGridCompiler %s done nodes=%d links=%d", tag, touchedNodes, touchedLinks);
}

static void VerifyMenuExtraRecords(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return;
    int assetNodes = 0, assetLinks = 0;
    EffectiveAssetCounts(&assetNodes, &assetLinks, nullptr, nullptr);
    const int vanillaNodes = g_sidecar.vanillaNodes > 0 ? g_sidecar.vanillaNodes : kVanillaNodeCapacity;
    int bad = 0;
    __try {
        for (int node = vanillaNodes; node < assetNodes && node < 1024; node++) {
            volatile uint8_t* rec = menu + kAbmapMenuNodeArrayOffset +
                static_cast<uintptr_t>(node) * kAbmapMenuNodeStride;
            const uint16_t contentWord = *reinterpret_cast<volatile uint16_t*>(
                rec + kAbmapMenuNodeContentWordOffset);
            if (contentWord == 0xFFFFu) {
                bad++;
                if (bad <= 6) {
                    HookLog("[ffx-hooks] FullGridCompiler %s WARN menu node=%d contentWord=FFFF", tag, node);
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler WARN menu verify failed %s", tag);
    }
    if (bad > 6)
        HookLog("[ffx-hooks] FullGridCompiler %s WARN %d extra menu records still FFFF", tag, bad);
}

static void PatchMenuExtraRecords(const char* tag, bool write) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    volatile uint8_t* state = RuntimeStateBase();
    if (!menu || !state) return;
    const bool doWrite = write && g_writeCompile && !g_observeOnly;
    int assetNodes = 0, assetLinks = 0;
    EffectiveAssetCounts(&assetNodes, &assetLinks, nullptr, nullptr);
    const int vanillaNodes = g_sidecar.vanillaNodes > 0 ? g_sidecar.vanillaNodes : kVanillaNodeCapacity;
    int patched = 0;
    __try {
        for (int node = vanillaNodes; node < assetNodes && node < 1024; node++) {
            volatile uint8_t* rec = menu + kAbmapMenuNodeArrayOffset +
                static_cast<uintptr_t>(node) * kAbmapMenuNodeStride;
            volatile uint16_t* contentWord = reinterpret_cast<volatile uint16_t*>(
                rec + kAbmapMenuNodeContentWordOffset);
            const uint16_t oldWord = *contentWord;
            const uint8_t wantContent = state[static_cast<uintptr_t>(node) * 2u];
            const uint16_t wantWord = static_cast<uint16_t>(wantContent);
            if (oldWord == 0xFFFFu || oldWord != wantWord) {
                if (doWrite)
                    *contentWord = wantWord;
                patched++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler WARN menu patch failed %s", tag);
    }
    if (patched)
        HookLog("[ffx-hooks] FullGridCompiler %s menu %s records=%d",
            tag, doWrite ? "patched" : "would-patch", patched);
}

static bool WriteSidecarAtomic(const char* path, const char* json) {
    char tmpPath[MAX_PATH] = {};
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    HANDLE h = CreateFileA(tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    const size_t len = strlen(json);
    DWORD written = 0;
    BOOL ok = WriteFile(h, json, static_cast<DWORD>(len), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || written != len)
        return false;
    if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmpPath);
        return false;
    }
    return true;
}

static void CaptureAndWriteSidecar(const char* tag) {
    if (!g_writeSidecar || g_observeOnly) {
        HookLog("[ffx-hooks] FullGridCompiler %s sidecar write skipped (observe or WRITE_SIDECAR off)", tag);
        return;
    }

    volatile uint8_t* state = RuntimeStateBase();
    if (!state) return;

    int assetNodes = 0, assetLinks = 0;
    EffectiveAssetCounts(&assetNodes, &assetLinks, nullptr, nullptr);
    const int vanillaNodes = g_sidecar.vanillaNodes > 0 ? g_sidecar.vanillaNodes : kVanillaNodeCapacity;
    const int vanillaLinks = g_sidecar.vanillaLinks > 0 ? g_sidecar.vanillaLinks : kVanillaLinkCapacity;

    if (assetNodes <= vanillaNodes && assetLinks <= vanillaLinks) {
        HookLog("[ffx-hooks] FullGridCompiler %s no extra capacity — sidecar write skipped", tag);
        return;
    }

    EnsureSidecarIdentityFromEnv();
    if (!g_sidecar.saveSha256[0] || !g_sidecar.layoutSha256[0] || !g_sidecar.contentsSha256[0]) {
        HookLog("[ffx-hooks] FullGridCompiler %s sidecar write refused — missing env hash identity save=%d layout=%d contents=%d",
            tag,
            g_sidecar.saveSha256[0] ? 1 : 0,
            g_sidecar.layoutSha256[0] ? 1 : 0,
            g_sidecar.contentsSha256[0] ? 1 : 0);
        return;
    }

    char json[64 * 1024] = {};
    size_t pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos,
        "{\n  \"schema\": 1,\n  \"profile_key\": \"%s\",\n",
        g_sidecar.profileKey[0] ? g_sidecar.profileKey : "runtime");

    pos += snprintf(json + pos, sizeof(json) - pos, "  \"save_sha256\": \"%s\",\n", g_sidecar.saveSha256);
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"layout_sha256\": \"%s\",\n", g_sidecar.layoutSha256);
    pos += snprintf(json + pos, sizeof(json) - pos, "  \"contents_sha256\": \"%s\",\n", g_sidecar.contentsSha256);

    pos += snprintf(json + pos, sizeof(json) - pos,
        "  \"grid_kind\": \"Standard\",\n"
        "  \"vanilla_capacity\": { \"nodes\": %d, \"links\": %d, \"activation_bytes\": 881 },\n"
        "  \"asset_counts\": { \"clusters\": 0, \"nodes\": %d, \"links\": %d },\n"
        "  \"extra_nodes\": [\n",
        vanillaNodes, vanillaLinks, assetNodes, assetLinks);

    bool firstNode = true;
    for (int node = vanillaNodes; node < assetNodes && node < 1024; node++) {
        const uint8_t content = state[static_cast<uintptr_t>(node) * 2u];
        const uint8_t status = state[static_cast<uintptr_t>(node) * 2u + 1u];
        pos += snprintf(json + pos, sizeof(json) - pos,
            "    %s{ \"node_id\": %d, \"content\": %u, \"status\": %u }\n",
            firstNode ? "" : ",", node, content, status);
        firstNode = false;
        if (pos + 64 >= sizeof(json)) break;
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "  ],\n  \"extra_links\": [\n");

    bool firstLink = true;
    for (int link = vanillaLinks; link < assetLinks && link < 1024; link++) {
        const uint8_t st = state[kStateLinkOffset + static_cast<uintptr_t>(link)];
        pos += snprintf(json + pos, sizeof(json) - pos,
            "    %s{ \"link_id\": %d, \"state\": %u }\n",
            firstLink ? "" : ",", link, st);
        firstLink = false;
        if (pos + 64 >= sizeof(json)) break;
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "  ]\n}\n");

    ResolveSidecarPath();
    if (WriteSidecarAtomic(g_sidecarPath, json)) {
        HookLog("[ffx-hooks] FullGridCompiler %s sidecar written %s (%zu bytes)",
            tag, g_sidecarPath, pos);
    } else {
        HookLog("[ffx-hooks] FullGridCompiler %s sidecar write FAILED %s", tag, g_sidecarPath);
    }
}

// Track A diagnostic: dump the live menu header (NodeCount/LinkCount) and the
// menu node record content words around the vanilla boundary. This is the only
// place that proves whether the Square dat02 layout actually loaded as 861 nodes
// and built real (non-0xFFFF) records for index 860+. Read-only.
static void DumpMenuLayout(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) {
        HookLog("[ffx-hooks] FullGridCompiler %s layout dump: menu ptr null", tag);
        return;
    }
    __try {
        const uint16_t nodeCount = *reinterpret_cast<volatile uint16_t*>(menu + 2);
        const uint16_t linkCount = *reinterpret_cast<volatile uint16_t*>(menu + 4);
        const uint16_t hdr0 = *reinterpret_cast<volatile uint16_t*>(menu + 0);
        HookLog("[ffx-hooks] FullGridCompiler %s LAYOUT hdr0=%u NodeCount=%u LinkCount=%u (expect 861/882 for Square)",
            tag, hdr0, nodeCount, linkCount);

        // Probe records around the vanilla boundary (858..862).
        for (int idx = 858; idx <= 862; idx++) {
            volatile uint8_t* rec = menu + kAbmapMenuNodeArrayOffset +
                static_cast<uintptr_t>(idx) * kAbmapMenuNodeStride;
            const uint16_t contentWord = *reinterpret_cast<volatile uint16_t*>(
                rec + kAbmapMenuNodeContentWordOffset);
            const uint8_t statusByte = rec[kAbmapMenuNodeStatusByteOffset];
            HookLog("[ffx-hooks] FullGridCompiler %s LAYOUT rec[%d] content=%04X status=%02X %s",
                tag, idx, contentWord, statusByte,
                contentWord == 0xFFFFu ? "(GHOST/empty record)" : "(built)");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler %s LAYOUT dump faulted", tag);
    }
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
static bool IsCallerBattleKernelInit(void* retAddr) {
    if (!g_base || !retAddr) return false;
    const uintptr_t r = reinterpret_cast<uintptr_t>(retAddr);
    return r >= g_base + RVA_FFX_BTL_KERNEL_INIT_LO && r < g_base + RVA_FFX_BTL_KERNEL_INIT_HI;
}

static int __cdecl PrepareSaveFieldGuard_Shim() {
    if (!IsCallerBattleKernelInit(_ReturnAddress())) {
        const long n = InterlockedIncrement(&g_prepSkipFieldCount);
        if (n <= 4)
            HookLog("[ffx-hooks] FullGridCompiler skip PrepareSave field path (Status/Equip stat guard)");
        return 0;
    }
    return reinterpret_cast<NoArgIntFn>(g_prepSaveTrampoline)();
}

static int __cdecl LayoutLoad_Shim() {
    const bool reEnter = (g_layoutGen > 0);
    if (reEnter && g_trustedAssetNodes > kVanillaNodeCapacity && g_sgBatchArmed)
        PrepSgReEnterSession("before-A45570-reenter");
    ++g_layoutGen;
    g_sgBatchArmed = true;
    g_sgNode860Activated = false;
    g_sgPostActivateAnimGate = 0;
    g_sgBlockProducerAnims = false;
    ResetSgBatchRebindOnly();
    if (g_traceAlloc && !g_allocTraceArmed) {
        g_allocTraceArmed = true;
        HookLog("[ffx-hooks] FullGridCompiler ALLOC-TRACE window opened (A45570 SG enter)");
    }
    const int rv = reinterpret_cast<NoArgIntFn>(g_layoutTrampoline)();
    SyncAllSgWritersAtLayout("after-A45570");
    DumpMenuLayout("after-A45570");
    ScanCanariesImpl("after-A45570");
    HookLog("[ffx-hooks] FullGridCompiler after-A45570 batch r16=%d r48=%d r8=%d link882=%d path861=%d slot860ok=%d full=%d rebound=%d armed=%d",
        g_batch861Ready16 ? 1 : 0, g_batch861Ready48 ? 1 : 0, g_batch861Ready8 ? 1 : 0,
        g_linkBatch882Ready ? 1 : 0, g_pathBatch861Ready ? 1 : 0, DrawBatchSlot860Ready() ? 1 : 0,
        DrawBatchSlot860FullReady() ? 1 : 0, g_batchPointersRebound ? 1 : 0, g_sgBatchArmed ? 1 : 0);
    return rv;
}

static int __cdecl InitRuntimeState_Shim(int16_t* state) {
    if (g_traceAlloc && !g_allocTraceArmed) {
        g_allocTraceArmed = true;
        HookLog("[ffx-hooks] FullGridCompiler ALLOC-TRACE window opened (A53DE0 load/init)");
    }
    volatile uint8_t* live = state
        ? reinterpret_cast<volatile uint8_t*>(state)
        : RuntimeStateBase();
    if (live && RuntimeStateHasSaveGridProgress(live)) {
        HookLog("[ffx-hooks] FullGridCompiler skip-A53DE0 save grid populated (field stat guard)");
        CompileExtraState("skip-A53DE0", true);
        return 0;
    }
    const int rv = reinterpret_cast<InitRuntimeStateFn>(g_initTrampoline)(state);
    HookLog("[ffx-hooks] FullGridCompiler after-A53DE0 seed verify rv=%d", rv);
    CompileExtraState("after-A53DE0-verify", true);
    return rv;
}

static int __cdecl DefaultState_Shim() {
    const int rv = reinterpret_cast<NoArgIntFn>(g_defaultStateTrampoline)();
    if (!g_sidecar.loaded)
        LoadSidecarFile();
    CompileExtraState("after-A47210", true);
    return rv;
}

static int __cdecl ApplyStateToMenu_Shim() {
    DumpMenuLayout("before-A49590");
    PatchMenuExtraRecords("before-A49590", true);
    int rv = reinterpret_cast<NoArgIntFn>(g_applyTrampoline)();
    VerifyMenuExtraRecords("after-A49590");
    if (g_trustedAssetNodes > kVanillaNodeCapacity && g_bootPosVerified41252)
        ScrubBootPosOobTail("after-A49590");
    return rv;
}

static int __cdecl SaveMenuToState_Shim() {
    /* Lane A TODO: if A5BB70 loops to assetNodeCount and writes vanilla save slice for 860+,
       clamp_vanilla_loop temporarily patches menu header counts — experimental only. */
    uint16_t savedNodes = 0, savedLinks = 0;
    bool restoredCounts = false;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (g_clampVanillaLoop && menu) {
        __try {
            savedNodes = *reinterpret_cast<volatile uint16_t*>(menu + 2);
            savedLinks = *reinterpret_cast<volatile uint16_t*>(menu + 4);
            *reinterpret_cast<volatile uint16_t*>(menu + 2) = static_cast<uint16_t>(kVanillaNodeCapacity);
            *reinterpret_cast<volatile uint16_t*>(menu + 4) = static_cast<uint16_t>(kVanillaLinkCapacity);
            restoredCounts = true;
            HookLog("[ffx-hooks] FullGridCompiler before-A5BB70 EXPERIMENTAL clamp menu counts %u/%u -> %d/%d",
                savedNodes, savedLinks, kVanillaNodeCapacity, kVanillaLinkCapacity);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            restoredCounts = false;
        }
    }

    PatchMenuExtraRecords("before-A5BB70", true);
    if (g_trustedAssetNodes > kVanillaNodeCapacity && g_sgBatchArmed) {
        RedirectProducerPointersFull("before-A5BB70");
        PurgeAllStale41252Pointers("before-A5BB70");
        ScrubBootPosOobTail("before-A5BB70");
        ScrubStalePosOobTail("before-A5BB70");
        RepairSlot860Poison("before-A5BB70");
    }
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_saveTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RecordCrashIncident("SG-C???", "A5BB70 save", "SEH inside A5BB70 trampoline");
        rv = -1;
    }

    if (restoredCounts && menu) {
        __try {
            *reinterpret_cast<volatile uint16_t*>(menu + 2) = savedNodes;
            *reinterpret_cast<volatile uint16_t*>(menu + 4) = savedLinks;
            HookLog("[ffx-hooks] FullGridCompiler after-A5BB70 restored menu counts %u/%u", savedNodes, savedLinks);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (g_trustedAssetNodes > kVanillaNodeCapacity && g_sgBatchArmed) {
        PurgeAllStale41252Pointers("after-A5BB70");
        ScrubBootPosOobTail("after-A5BB70");
        ScrubStalePosOobTail("after-A5BB70");
        RepairSlot860Poison("after-A5BB70");
    }

    CompileExtraState("after-A5BB70-restore-runtime", true);
    CaptureAndWriteSidecar("after-A5BB70");
    return rv;
}

static bool SgActivationBuffersReady() {
    return DrawBatchSlot860FullReady() && g_pathBatch861Ready && g_latestPosBatch48 != nullptr;
}

static int __cdecl RecomputeStats_Shim() {
    CompileExtraState("before-A54860", true);
    PatchMenuExtraRecords("before-A54860", true);
    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        uint16_t nc = 0, lc = 0;
        volatile uint8_t* menu = AbmapMenuStateBase();
        if (menu) {
            __try {
                nc = *reinterpret_cast<volatile uint16_t*>(menu + 2);
                lc = *reinterpret_cast<volatile uint16_t*>(menu + 4);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (nc == 0 && lc == 0) {
            HookLog("[ffx-hooks] FullGridCompiler skip-A54860 pre-SG menu=%u/%u (avoid slot860 OOB before layout)",
                nc, lc);
            return 0;
        }
        PatchAbmapMenuWriterTable("before-A54860");
        PinLayoutBatchBuffers("before-A54860");
        ScrubAllSgWritersOffBoot("before-A54860");
        if (!SgActivationBuffersReady()) {
            HookLog("[ffx-hooks] FullGridCompiler defer-A54860 buffers not ready "
                "(r16=%d r48=%d r8=%d path861=%d pos=%p) — would OOB slot860",
                g_batch861Ready16 ? 1 : 0, g_batch861Ready48 ? 1 : 0, g_batch861Ready8 ? 1 : 0,
                g_pathBatch861Ready ? 1 : 0, g_latestPosBatch48);
            return 0;
        }
        // v1.33: A54860 recompute OOB-writes slot860 into boot arena (10868xxx+860*48) bypassing
        // 7F4900 draw hook — defer until producer redirect is proven (log: FLOAT-HIT after-A54860).
        HookLog("[ffx-hooks] FullGridCompiler defer-A54860 extra-node menu=%u/%u "
            "pinPos=%p bootPos=%p (block boot slot860 producer)",
            nc, lc, RedirectPosBatch(), g_bootPosBatch860);
        PurgeAllStale41252Pointers("defer-A54860");
        ScrubBootPosOobTail("defer-A54860");
        ScrubStalePosOobTail("defer-A54860");
        RepairSlot860Poison("defer-A54860");
        return 0;
    }
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_recomputeTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RecordCrashIncident("SG-C005", "A54860 recompute", "SEH inside A54860 trampoline");
        return -1;
    }
  HookLog("[ffx-hooks] FullGridCompiler after-A54860 rv=%d compileRan=%d batch r16=%d r48=%d r8=%d link882=%d path861=%d slot860ok=%d full=%d rebound=%d",
      rv, g_compileRan ? 1 : 0, g_batch861Ready16 ? 1 : 0, g_batch861Ready48 ? 1 : 0,
      g_batch861Ready8 ? 1 : 0, g_linkBatch882Ready ? 1 : 0, g_pathBatch861Ready ? 1 : 0,
      DrawBatchSlot860Ready() ? 1 : 0, DrawBatchSlot860FullReady() ? 1 : 0, g_batchPointersRebound ? 1 : 0);
    ScanCanariesImpl("after-A54860");
    ScanForCorruptFloat("after-A54860");
    SyncAllSgWritersAtLayout("after-A54860");
    return rv;
}

static void RecordCanary(void* p, size_t size, uint32_t callerRva) {
    if (!p) return;
    memset(reinterpret_cast<char*>(p) + size, kCanaryByte, kCanaryBytes);
    // Many allocs route through thin wrappers (FFX_Heap_Alloc16 -> sub_687190), so the
    // immediate caller is uninformative. CaptureStackBackTrace is useless on x86 FPO/release,
    // so manually scan the stack for real return addresses: a stack dword that points into
    // FFX .text AND is immediately preceded by a 0xE8 (call rel32) is a return address.
    uint32_t fr[5] = {};
    int fi = 0;
    if (g_base) {
        uintptr_t* sp = reinterpret_cast<uintptr_t*>(_AddressOfReturnAddress());
        const uintptr_t lo = g_base + 0x1000, hi = g_base + 0x1100000;
        for (int i = 0; i < 96 && fi < 5; ++i) {
            const uintptr_t a = sp[i];
            if (a < lo || a >= hi) continue;
            const unsigned char* call = reinterpret_cast<const unsigned char*>(a - 5);
            if (!IsBadReadPtr(call, 1) && *call == 0xE8) fr[fi++] = (uint32_t)(a - g_base);
        }
    }
    EnterCriticalSection(&g_canaryCs);
    int idx = -1;
    for (int i = 0; i < g_canaryCount; ++i)
        if (g_canary[i].ptr == p) { idx = i; break; }   // ptr reused after free -> overwrite
    if (idx < 0 && g_canaryCount < (int)(sizeof(g_canary) / sizeof(g_canary[0])))
        idx = g_canaryCount++;
    if (idx >= 0) {
        g_canary[idx].ptr = p; g_canary[idx].userSize = (uint32_t)size; g_canary[idx].callerRva = callerRva;
        for (int k = 0; k < 5; ++k) g_canary[idx].frames[k] = fr[k];
    }
    LeaveCriticalSection(&g_canaryCs);
}

// Scan all live canaries and report any smashed ones, tagged with WHEN the scan ran. By
// scanning at load/recompute/teardown/fault we learn whether a buffer is corrupted at boot
// (noise) or only after the SG draw with 861 nodes (the real culprit).
static void ScanCanariesImpl(const char* tag) {
    if (!g_canaryInit) return;
    int hits = 0;
    for (int i = 0; i < g_canaryCount; ++i) {
        const CanaryEntry e = g_canary[i];
        if (!e.ptr) continue;
        unsigned char* can = reinterpret_cast<unsigned char*>(e.ptr) + e.userSize;
        if (IsBadReadPtr(can, kCanaryBytes)) continue;
        bool ok = true;
        for (int k = 0; k < kCanaryBytes; ++k) if (can[k] != kCanaryByte) { ok = false; break; }
        if (!ok) {
            // Filter known allocator noise: 0xCDCDCDCD (uninit fill) and 0xABCDEF12 (adjacent
            // block header magic). A REAL data overflow leaves other bytes (e.g. floats).
            const uint32_t d0 = *reinterpret_cast<uint32_t*>(can);
            if (d0 == 0xCDCDCDCDu || d0 == 0xABCDEF12u) continue;
            ++hits;
            HookLog("[ffx-hooks] CANARY-CORRUPT[%s] callerRva=0x%08X size=%u ptr=0x%08X over=%02X%02X%02X%02X %02X%02X%02X%02X stack=%08X<-%08X<-%08X<-%08X<-%08X (n860=%.2f n861=%.2f)",
                tag, e.callerRva, e.userSize, (unsigned)reinterpret_cast<uintptr_t>(e.ptr),
                can[0], can[1], can[2], can[3], can[4], can[5], can[6], can[7],
                e.frames[0], e.frames[1], e.frames[2], e.frames[3], e.frames[4],
                e.userSize / 860.0, e.userSize / 861.0);
        }
    }
    HookLog("[ffx-hooks] CANARY-SCAN[%s] done: %d corrupt of %d tracked", tag, hits, g_canaryCount);
}

static void* __cdecl HeapAlloc_Shim(size_t size, void* align) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint32_t callerRva = (g_base && caller >= g_base) ? static_cast<uint32_t>(caller - g_base) : 0;
    int bumped = BumpNodeBatchAllocSize(static_cast<int>(size));
    // Exact SG draw-path allocs @ caller 0x43F0AE (bypass stride slack false-positives).
    if (g_sgBatchArmed && callerRva == 0x0043F0AE) {
        if (size == 78960u) { g_pathBatch861Ready = true; bumped = 79051; }
        else if (size == 3392u) { g_batch861Ready16 = true; bumped = 3444; }
    }
    const size_t userSize = static_cast<size_t>(bumped);
    if (bumped != static_cast<int>(size))
        HookLog("[ffx-hooks] FullGridCompiler BUMP-heap-batch %u -> %u callerRva=0x%08X (r16=%d r48=%d r8=%d path861=%d)",
            (unsigned)size, (unsigned)userSize, callerRva, g_batch861Ready16 ? 1 : 0, g_batch861Ready48 ? 1 : 0,
            g_batch861Ready8 ? 1 : 0, g_pathBatch861Ready ? 1 : 0);
    const bool instrument = g_traceAlloc && userSize >= (size_t)g_canaryFloor && userSize <= 4000000;
    const size_t reqSize = userSize
        + (instrument ? (size_t)kCanaryBytes : 0);
    void* p = reinterpret_cast<HeapAllocFn>(g_allocTrampoline)(reqSize, align);
    if (userSize == 79051u) g_latestPath79051 = p;
    NoteLatestBatchPtr(static_cast<int>(userSize), static_cast<int>(userSize),
        static_cast<int>(reinterpret_cast<uintptr_t>(p)));
    /* Writer sync deferred to after-A45570 SyncAllSgWritersAtLayout (v1.44 enter lag fix). */
    if (instrument && g_canaryInit) {
        const uint32_t crva = (g_base && caller >= g_base) ? static_cast<uint32_t>(caller - g_base) : 0;
        RecordCanary(p, userSize, crva);
    }
    if (g_traceAlloc && userSize >= 512 && userSize <= 4000000 &&
        g_allocSeenCount < (int)(sizeof(g_allocSeen) / sizeof(g_allocSeen[0]))) {
        const uint32_t callerRva = (g_base && caller >= g_base) ? static_cast<uint32_t>(caller - g_base) : 0;
        bool seen = false;
        for (int i = 0; i < g_allocSeenCount; ++i)
            if (g_allocSeen[i].callerRva == callerRva && g_allocSeen[i].size == (uint32_t)size) { seen = true; break; }
        if (!seen) {
            g_allocSeen[g_allocSeenCount].callerRva = callerRva;
            g_allocSeen[g_allocSeenCount].size = (uint32_t)userSize;
            ++g_allocSeenCount;
            HookLog("[ffx-hooks] FullGridCompiler ALLOC-TRACE%s size=%u (n860=%.2f n861=%.2f) callerRva=0x%08X ptr=0x%08X",
                AllocSizeLooksNodeCount(userSize) ? "*" : "", (unsigned)userSize,
                userSize / 860.0, userSize / 861.0,
                callerRva, (unsigned)reinterpret_cast<uintptr_t>(p));
        }
    }
    return p;
}

// Universal allocator census/pad: sub_942B60 is the SOLE low-level allocator behind every
// wrapper. Pad (proven ineffective -> fixed-offset overflow) optional. CENSUS: for every
// node-count-plausible size, capture the caller chain (stack scan) so we can name the alloc
// site of the per-node render buffer that the 861st node overflows. Dumped at a checkpoint.
struct CoreSeen { uint32_t size; uint32_t frames[4]; };
static CoreSeen g_coreSeen[1024] = {};
static int g_coreSeenCount = 0;
struct CoreMap { uint32_t ptr; uint32_t size; uint32_t frames[3]; };
static CoreMap g_coreMap[4096] = {};
static int g_coreMapCount = 0;

static void RecordCoreMap(uint32_t ptr, uint32_t size) {
    if (!ptr || !size || g_coreMapCount >= (int)(sizeof(g_coreMap)/sizeof(g_coreMap[0]))) return;
    uint32_t fr[3] = {};
    int fi = 0;
    if (g_base) {
        uintptr_t* sp = reinterpret_cast<uintptr_t*>(_AddressOfReturnAddress());
        const uintptr_t lo = g_base + 0x1000, hi = g_base + 0x1100000;
        for (int i = 0; i < 96 && fi < 3; ++i) {
            const uintptr_t a = sp[i];
            if (a < lo || a >= hi) continue;
            const unsigned char* call = reinterpret_cast<const unsigned char*>(a - 5);
            if (!IsBadReadPtr(call, 1) && *call == 0xE8) fr[fi++] = (uint32_t)(a - g_base);
        }
    }
    EnterCriticalSection(&g_canaryCs);
    g_coreMap[g_coreMapCount].ptr = ptr;
    g_coreMap[g_coreMapCount].size = size;
    for (int k = 0; k < 3; ++k) g_coreMap[g_coreMapCount].frames[k] = fr[k];
    ++g_coreMapCount;
    LeaveCriticalSection(&g_canaryCs);
}

// Slab cells are 4080 bytes (freelist link at +0xFF0 per sub_9435A0). Map corrupt addr -> alloc site.
static void FindAllocOwner(uintptr_t addr, const char* tag) {
    if (!g_canaryInit || !addr) return;
    const uintptr_t blockBase = (addr >= 0xFF0u) ? (addr - 0xFF0u) : 0;
    int hits = 0;
    for (int i = 0; i < g_coreMapCount && hits < 8; ++i) {
        const CoreMap e = g_coreMap[i];
        const uintptr_t p = e.ptr;
        const uintptr_t sz = e.size;
        const bool inUser = (addr >= p && addr < p + sz);
        const bool inSlab = blockBase && (
            (p >= blockBase + 16 && p < blockBase + 16 + sz) ||
            (blockBase >= p && blockBase < p + sz) ||
            (blockBase + 4080 > p && blockBase < p + sz));
        if (inUser || inSlab) {
            ++hits;
            HookLog("[ffx-hooks] CORE-OWNER[%s] corrupt=0x%08X block=0x%08X allocPtr=0x%08X size=%u stack=%08X<-%08X<-%08X",
                tag, (unsigned)addr, (unsigned)blockBase, e.ptr, e.size, e.frames[0], e.frames[1], e.frames[2]);
        }
    }
    if (!hits) HookLog("[ffx-hooks] CORE-OWNER[%s] corrupt=0x%08X block=0x%08X NO alloc owner in %d tracked", tag, (unsigned)addr, (unsigned)blockBase, g_coreMapCount);
}

// Vanilla menu-2D batches are sized to 860 nodes / 881 links. sub_7F4900 uses 16/48/8 B per slot.
static int BumpNodeBatchAllocSize(int size) {
    if (!g_sgBatchArmed || g_trustedAssetNodes <= kVanillaNodeCapacity || size <= 0)
        return size;
    constexpr int kSlack = 128;
    auto bump = [&](int vanillaCount, int extraCount, int stride, bool* readyFlag) -> int {
        const int vanillaBytes = vanillaCount * stride;
        const int extraBytes = extraCount * stride;
        const int lo = vanillaBytes - kSlack;
        const int hi = extraBytes + kSlack;
        if (size < lo || size > hi) return 0;
        if (size >= extraBytes) {
            if (readyFlag) *readyFlag = true;
            return size;
        }
        if (size >= vanillaBytes - kSlack) {
            if (readyFlag) *readyFlag = true;
            return extraBytes;
        }
        return 0;
    };
    int bumped = bump(kVanillaNodeCapacity, kVanillaNodeCapacity + 1, 16, &g_batch861Ready16);
    if (bumped) return bumped;
    bumped = bump(kVanillaNodeCapacity, kVanillaNodeCapacity + 1, 48, &g_batch861Ready48);
    if (bumped) return bumped;
    bumped = bump(kVanillaNodeCapacity, kVanillaNodeCapacity + 1, 8, &g_batch861Ready8);
    if (bumped) return bumped;
    // CANARY @ caller 0x43F0AE: size=3392 = 860*4-48 (4B index table per node).
    bumped = bump(kVanillaNodeCapacity, kVanillaNodeCapacity + 1, 4, &g_batch861Ready16);
    if (bumped) return bumped;
    bumped = bump(kVanillaLinkCapacity, kVanillaLinkCapacity + 1, 8, &g_linkBatch882Ready);
    if (bumped) return bumped;
    bumped = bump(kVanillaLinkCapacity, kVanillaLinkCapacity + 1, 16, &g_linkBatch882Ready);
    if (bumped) return bumped;
    bumped = bump(kVanillaLinkCapacity, kVanillaLinkCapacity + 1, 48, &g_linkBatch882Ready);
    if (bumped) return bumped;
    bumped = bump(kVanillaLinkCapacity, kVanillaLinkCapacity + 1, 32, &g_linkBatch882Ready);
    if (bumped) return bumped;
    bumped = bump(kVanillaLinkCapacity, kVanillaLinkCapacity + 1, 64, &g_linkBatch882Ready);
    if (bumped) return bumped;
    bumped = bump(kVanillaLinkCapacity, kVanillaLinkCapacity + 1, 12, &g_linkBatch882Ready);
    if (bumped) return bumped;
    // ALLOC-TRACE @ callerRva=0x43F0AE: 78960 bytes path/activation work buffer (860*91 + ~700 hdr).
    constexpr int kPath860 = 78960;
    constexpr int kPath861 = kPath860 + (kPath860 / kVanillaNodeCapacity);  // +91 -> 79051
    constexpr int kPathSlack = 16;  // tight window — kSlack=128 false-positive'd boot allocs in v1.11
    if (size >= kPath860 - kPathSlack && size <= kPath860 + kPathSlack) {
        g_pathBatch861Ready = true;
        return kPath861;
    }
    if (size >= kPath861 - kPathSlack && size <= kPath861 + kPathSlack) {
        g_pathBatch861Ready = true;
        return size;
    }
    return size;
}

static int __fastcall AllocCore_Shim(void* thisptr, void* edx, int size) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint32_t callerRva = (g_base && caller >= g_base)
        ? static_cast<uint32_t>(caller - g_base) : 0;
    const int req = BumpNodeBatchAllocSize(size);
    if (req != size) {
        static int s_lastLoggedReq = 0;
        static int s_lastLoggedSize = 0;
        if (s_lastLoggedReq != req || s_lastLoggedSize != size) {
            s_lastLoggedReq = req;
            s_lastLoggedSize = size;
            HookLog("[ffx-hooks] FullGridCompiler BUMP-core-batch %d -> %d (r16=%d r48=%d r8=%d link882=%d path861=%d)",
                size, req, g_batch861Ready16 ? 1 : 0, g_batch861Ready48 ? 1 : 0,
                g_batch861Ready8 ? 1 : 0, g_linkBatch882Ready ? 1 : 0, g_pathBatch861Ready ? 1 : 0);
        }
    }
    if (g_sgBatchArmed && IsSgBatchAllocTraceSize(size)) {
        static uint32_t s_seenReq[8] = {};
        static int s_seenCount = 0;
        bool seen = false;
        for (int i = 0; i < s_seenCount; ++i) {
            if (s_seenReq[i] == static_cast<uint32_t>(req)) { seen = true; break; }
        }
        if (!seen && s_seenCount < 8) {
            s_seenReq[s_seenCount++] = static_cast<uint32_t>(req);
            HookLog("[ffx-hooks] FullGridCompiler ALLOC-TRACE-core req=%d bumped=%d callerRva=0x%08X ptr-pending",
                size, req, callerRva);
        }
    }
    const int pad = (g_guardPad > 0 && req > 0) ? g_guardPad : 0;
    int r = reinterpret_cast<AllocCoreFn>(g_allocCoreTrampoline)(thisptr, edx, req + pad);
    NoteLatestBatchPtr(size, req, r);
    if (req == kBatch861Bytes48 || req == kBatch861Bytes16 || req == kBatch861Bytes8)
        PinLayoutBatchBuffers("after-core-batch-alloc");
    if (g_sgBatchArmed && g_trustedAssetNodes > kVanillaNodeCapacity &&
        size == kGamePosBatch860 && req == kBatch861Bytes48) {
        PurgeAllStale41252Pointers("after-core-41252-bump");
    }
    if (g_canaryInit && r && req >= 64 && req <= 200000)
        RecordCoreMap((uint32_t)r, (uint32_t)req);
    // Census node-count-plausible sizes (covers 860*stride for stride 1..~64), deduped by size.
    if (g_canaryInit && req >= 700 && req <= 70000) {
        bool seen = false;
        for (int i = 0; i < g_coreSeenCount; ++i) if ((int)g_coreSeen[i].size == req) { seen = true; break; }
        if (!seen && g_coreSeenCount < (int)(sizeof(g_coreSeen)/sizeof(g_coreSeen[0]))) {
            uint32_t fr[4] = {};
            int fi = 0;
            if (g_base) {
                uintptr_t* sp = reinterpret_cast<uintptr_t*>(_AddressOfReturnAddress());
                const uintptr_t lo = g_base + 0x1000, hi = g_base + 0x1100000;
                for (int i = 0; i < 96 && fi < 4; ++i) {
                    const uintptr_t a = sp[i];
                    if (a < lo || a >= hi) continue;
                    const unsigned char* call = reinterpret_cast<const unsigned char*>(a - 5);
                    if (!IsBadReadPtr(call, 1) && *call == 0xE8) fr[fi++] = (uint32_t)(a - g_base);
                }
            }
            EnterCriticalSection(&g_canaryCs);
            if (g_coreSeenCount < (int)(sizeof(g_coreSeen)/sizeof(g_coreSeen[0]))) {
                g_coreSeen[g_coreSeenCount].size = (uint32_t)req;
                for (int k = 0; k < 4; ++k) g_coreSeen[g_coreSeenCount].frames[k] = fr[k];
                ++g_coreSeenCount;
            }
            LeaveCriticalSection(&g_canaryCs);
        }
    }
    return r;
}

// sub_630670: bump 860-slot vertex batches to 861 slots (also mirrored in AllocCore_Shim).
static int __cdecl GameAlloc_Shim(int size) {
    const int req = BumpNodeBatchAllocSize(size);
    if (req != size)
        HookLog("[ffx-hooks] FullGridCompiler BUMP-game-batch %d -> %d (r48=%d slot860ok=%d rebound=%d)",
            size, req, g_batch861Ready48 ? 1 : 0, DrawBatchSlot860Ready() ? 1 : 0,
            g_batchPointersRebound ? 1 : 0);
    const int r = reinterpret_cast<GameAllocFn>(g_gameAllocTrampoline)(req);
    NoteLatestBatchPtr(size, req, r);
    if (req == kBatch861Bytes48 || req == kBatch861Bytes16 || req == kBatch861Bytes8)
        PinLayoutBatchBuffers("after-game-batch-alloc");
    return r;
}

static bool IsSgBatchAllocTraceSize(int size) {
    return size == kGamePosBatch860 || size == kBatch861Bytes48 ||
        size == kGameColBatch860 || size == kBatch861Bytes16 ||
        size == kGameUvBatch860 || size == kBatch861Bytes8;
}

static bool ApplyF1WriterRedirectBeforeDraw860(int a2, int slot, int n861, const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity || slot < 860) return false;
    RedirectDrawStoresIfStale(a2, slot, n861, tag);
    PatchCaptureBatchFromDrawA2(a2, tag);
    ScrubBootPosOobTail(tag);
    return true;
}

static bool RedirectDrawStoresIfStale(int a2, int slot, int n861, const char* tag) {
    if (a2 < 0x10000 || g_trustedAssetNodes <= kVanillaNodeCapacity || slot < 860) return false;
    PatchCaptureBatchFromDrawA2(a2, tag);
    uint32_t w = 0, pos = 0;
    if (!ResolveDrawWriterFromA2(a2, &w, &pos) || !w) return false;
    uint32_t col = 0, uv = 0;
    __try {
        col = *reinterpret_cast<volatile uint32_t*>(w + 0x14);
        uv = *reinterpret_cast<volatile uint32_t*>(w + 0x18);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (pos >= 0x10000u && IsStale41252PosBase(pos))
        RegisterProducerStalePos(reinterpret_cast<void*>(static_cast<uintptr_t>(pos)));
    const bool stalePos = pos >= 0x10000u && IsStale41252PosBase(pos);
    const bool staleCol = col >= 0x10000u && IsStale41252ColBase(col);
    const bool staleUv = uv >= 0x10000u && IsStale41252UvBase(uv);
    if (!stalePos && !staleCol && !staleUv) return false;
    RememberWriterStalePtr(w);
    PatchMenu2DWriterBuffers(w);
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler STORE-REDIRECT %s slot=%d n861=%d "
            "writer=0x%08X pos %08X->%p colStale=%d uvStale=%d pinPos=%p",
            tag, slot, n861, w, pos, RedirectPosBatch(), staleCol ? 1 : 0, staleUv ? 1 : 0,
            g_pinnedPos86148);
    }
    return true;
}

static void RedirectAnimCaptureList(int animObj, const char* tag) {
    if (animObj < 0x10000 || g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    void* posFresh = RedirectPosBatch();
    if (!posFresh) return;
    int redirects = 0;
    uint32_t node = 0;
    int hops = 0;
    __try {
        node = *reinterpret_cast<volatile uint32_t*>(
            reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(animObj)) +
            FFX_MENU2D_ANIMOBJ_CAPTURE_LIST_OFF);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    while (node >= 0x10000u && hops < 64) {
        ++hops;
        TryPatchWriterCandidate(node, &redirects);
        TryPatchWriterCandidate(node + 0x6Cu, &redirects);
        if (g_bootPosBatch860) {
            ReplaceBootBatchPtrScan(reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(node)),
                256u, g_bootPosBatch860, posFresh, BootArenaPosCap());
        }
        uint32_t next = 0;
        __try {
            next = *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(node));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (next == node) break;
        node = next;
    }
    uint8_t primCount = 0;
    __try {
        primCount = *reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(animObj) + 6);
        const uint8_t uploadCount = *reinterpret_cast<volatile uint8_t*>(
            static_cast<uintptr_t>(animObj) + FFX_MENU2D_ANIMOBJ_UPLOAD_COUNT_OFF);
        if (uploadCount > 0 && uploadCount < primCount) primCount = uploadCount;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        primCount = 0;
    }
    if (primCount > 32) primCount = 32;
    if (g_bootPosBatch860) {
        for (int i = 0; i < primCount; ++i) {
            volatile uint8_t* slot = reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(animObj)) +
                FFX_MENU2D_ANIMOBJ_SLOT_ARRAY_OFF + static_cast<uintptr_t>(i) * 16u;
            ReplaceBootBatchPtrScan(slot, 16u, g_bootPosBatch860, posFresh, BootArenaPosCap());
        }
    }
    if (redirects > 0) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler %s ANIM-CAPTURE-REDIRECT animObj=0x%08X "
                "listHops=%d redirects=%d pinPos=%p",
                tag, animObj, hops, redirects, posFresh);
        }
    }
}

static bool ForceDrawWritersToPinnedIfStale(int a2, int slot, int n861, const char* tag) {
    return RedirectDrawStoresIfStale(a2, slot, n861, tag);
}

static void ArmProducerSlot860Watch(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    DiscoverBootPosForSession(tag);
    HarvestProducerStaleFromMenuRegions();
    void* probe = (g_bootPosVerified41252 && g_bootPosBatch860) ? g_bootPosBatch860 : nullptr;
    if (!probe) probe = g_stalePos86048;
    if (!probe) return;
    __try {
        const uintptr_t off = static_cast<uintptr_t>(860) * 48u;
        g_slot860WatchSnapshot = *reinterpret_cast<volatile uint32_t*>(
            reinterpret_cast<volatile uint8_t*>(probe) + off);
        g_slot860WatchArmed = true;
        HookLog("[ffx-hooks] FullGridCompiler %s PRODUCER-WATCH-ARM probe=%p boot=%p slot860=0x%08X",
            tag, probe, g_bootPosBatch860, g_slot860WatchSnapshot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_slot860WatchArmed = false;
    }
}

static void CheckProducerSlot860Write(const char* tag) {
    if (!g_slot860WatchArmed) return;
    void* probes[3] = { g_stalePos86048, g_bootPosBatch860, RedirectPosBatch() };
    for (int pi = 0; pi < 3; ++pi) {
        void* probe = probes[pi];
        if (!probe) continue;
        __try {
            const uintptr_t off = static_cast<uintptr_t>(860) * 48u;
            const uint32_t now = *reinterpret_cast<volatile uint32_t*>(
                reinterpret_cast<volatile uint8_t*>(probe) + off);
            if (now != g_slot860WatchSnapshot) {
                const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
                const uint32_t callerRva = (g_base && caller >= g_base)
                    ? static_cast<uint32_t>(caller - g_base) : 0;
                HookLog("[ffx-hooks] FullGridCompiler PRODUCER-WRITE %s probe=%p old=0x%08X new=0x%08X "
                    "callerRva=0x%08X latestPos=%p",
                    tag, probe, g_slot860WatchSnapshot, now, callerRva, g_latestPosBatch48);
                ReactToProducerSlot860Write(probe, tag);
                g_slot860WatchSnapshot = now;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static bool PopulateSlot860FromNodeRecord(const char* tag) {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity || !DrawBatchSlot860FullReady())
        return false;
    FreezeLatestToPinned();
    void* posBuf = RedirectPosBatch();
    if (!posBuf) return false;
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return false;
    __try {
        volatile uint8_t* rec = menu + kAbmapMenuNodeArrayOffset +
            static_cast<uintptr_t>(860) * kAbmapMenuNodeStride;
        const uint16_t contentWord = *reinterpret_cast<volatile uint16_t*>(
            rec + kAbmapMenuNodeContentWordOffset);
        if (contentWord == 0xFFFFu) return false;
        ClearSlot860InLatest(tag);
        g_populatedSlot860 = true;
        HookLog("[ffx-hooks] FullGridCompiler %s populate-slot860 ready content=%04X status=%02X pos=%p",
            tag, contentWord, rec[kAbmapMenuNodeStatusByteOffset], posBuf);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HookLog("[ffx-hooks] FullGridCompiler %s populate-slot860 FAULT", tag);
        return false;
    }
}

// True only when resolved slot index is 860+. n861=861 → slot 0 (861−861); n861=1721 → slot 860.
// n861=-862 → slot -1 (normal panel draw via entry+8 payload) — never SKIP.
static bool DrawBatchTargetsSlot860(int n861) {
    const int slot = DrawBatchSlotFromN861(n861);
    return slot >= 860;
}

// v1.68 F1: redirect stale writers @7F4900 entry; SKIP860 unless sg_f1_inline.flag (v1.75).
static int __cdecl DrawBatch_Shim(void* a1, int a2, int n861) {
    const int slot = DrawBatchSlotFromN861(n861);
    const bool extraNode = g_sgBatchArmed && g_trustedAssetNodes > kVanillaNodeCapacity;
    if (extraNode && g_bootPosVerified41252)
        ScrubBootPosOobTail("pre-7F4900");
    const bool targetsSlot860 = extraNode && DrawBatchTargetsSlot860(n861);

    if (targetsSlot860) {
        ApplyF1WriterRedirectBeforeDraw860(a2, slot, n861, "F1-skip860");
        if (!g_sgF1InlineLiftSkip860) {
            static uint32_t s_skip860Gen = 0;
            if (s_skip860Gen != g_layoutGen) {
                s_skip860Gen = g_layoutGen;
                uint32_t w = 0, pos = 0;
                if (a2 >= 0x10000) ResolveDrawWriterFromA2(a2, &w, &pos);
                HookLog("[ffx-hooks] FullGridCompiler F1-SKIP860 slot=%d n861=%d writer=0x%08X pos=0x%08X "
                    "pinPos=%p bootPos=%p inline=%d",
                    slot, n861, w, pos, RedirectPosBatch(), g_bootPosBatch860,
                    g_sgF1InlineActive ? 1 : 0);
            }
            return 1;
        }
        static uint32_t s_inlineDrawGen = 0;
        if (s_inlineDrawGen != g_layoutGen) {
            s_inlineDrawGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler F1-INLINE-DRAW860 slot=%d n861=%d (vanilla 7F4900 + store patches)",
                slot, n861);
        }
    }
    return reinterpret_cast<DrawBatchFn>(g_drawBatchTrampoline)(a1, a2, n861);
}

static bool ShouldSkipProducerAnimShim() {
    if (g_trustedAssetNodes <= kVanillaNodeCapacity) return false;
    if (g_sgBlockProducerAnims) return true;
    if (g_sgPostActivateAnimGate > 0) {
        --g_sgPostActivateAnimGate;
        return true;
    }
    return false;
}

static void ForceAbmapPlacementAnimIdle(const char* tag) {
    volatile uint8_t* menu = AbmapMenuStateBase();
    if (!menu) return;
    __try {
        *reinterpret_cast<volatile uint32_t*>(menu + 71248) = 0;
        volatile uint8_t* band = menu + kAbmapMenuAnimDispatchBase;
        for (size_t off = 0; off + 4 <= kAbmapMenuAnimDispatchLen; off += 4)
            *reinterpret_cast<volatile uint32_t*>(band + off) = 0;
        *reinterpret_cast<volatile uint16_t*>(menu + FFX_ABMAP_MENU_ACTIVATION_FLAGS_OFF) = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler %s force-placement-idle (post-activate unlock SG-C025)", tag);
    }
}

static void SkipExtraNodeProducerPath(const char* tag, const char* site) {
    static uint32_t s_siteGen[4] = {};
    static const char* kSites[] = { "A47D50", "A45930", "A58080", "712330" };
    int si = 0;
    for (; si < 4; ++si) {
        if (site && strcmp(kSites[si], site) == 0) break;
    }
    if (si < 4 && s_siteGen[si] != g_layoutGen) {
        s_siteGen[si] = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler SKIP-%s activate-gated (%s)", site, tag);
    }
    ScrubBootPosOobTail(tag);
    ScrubStalePosOobTail(tag);
}

static void __cdecl RunPlacementFx_Shim(int a1) {
    if (ShouldSkipProducerAnimShim()) {
        SkipExtraNodeProducerPath("skip-A45930", "A45930");
        return;
    }
    using RunPlacementFxFn = void(__cdecl*)(int);
    reinterpret_cast<RunPlacementFxFn>(g_runPlacementFxTrampoline)(a1);
}

static void __cdecl FlushCaptureBatch48_Shim(int animObj, int flags) {
    if (g_trustedAssetNodes > kVanillaNodeCapacity && g_sgBatchArmed) {
        const bool skipVanilla = ShouldSkipProducerAnimShim();
        RedirectAnimCaptureList(animObj, skipVanilla ? "skip-712330" : "redirect-712330");
        SkipExtraNodeProducerPath(skipVanilla ? "skip-712330" : "block-vanilla-712330", "712330");
        /* SG-C029: never call vanilla 712330 for 861 — flush re-writes boot+860 OOB poison. */
        return;
    }
    using FlushCaptureBatch48Fn = void(__cdecl*)(int, int);
    reinterpret_cast<FlushCaptureBatch48Fn>(g_flushCaptureBatch48Trampoline)(animObj, flags);
}

static unsigned int __cdecl UpdatePlacementSlot_Shim(int a1) {
    if (ShouldSkipProducerAnimShim()) {
        SkipExtraNodeProducerPath("skip-A58080", "A58080");
        return 0;
    }
    using UpdatePlacementSlotFn = unsigned int(__cdecl*)(int);
    return reinterpret_cast<UpdatePlacementSlotFn>(g_updatePlacementSlotTrampoline)(a1);
}

// FFX_Abmap_UpdateRuntimeLinkGeometry (A5A800): link geom after activate; menu+71272 write cursor.
static unsigned int __cdecl PopulateLinkBatches_Shim() {
    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        RedirectProducerPointersFull("before-A5A800");
        EnsurePinnedWritersForActivation("before-A5A800");
    }
    unsigned int rv = 0;
    __try {
        using PopulateLinkFn = unsigned int(__cdecl*)();
        rv = reinterpret_cast<PopulateLinkFn>(g_populateLinkBatchesTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RecordCrashIncident("SG-C004", "A5A800 link-geom", "SEH inside A5A800 trampoline");
        return 0;
    }
    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        CheckProducerSlot860Write("after-A5A800");
        RedirectProducerPointersFull("after-A5A800");
    }
    return rv;
}

static int __cdecl NodePlacementAnim_Shim() {
    g_activeSgShim = "A47D50";
    if (ShouldSkipProducerAnimShim()) {
        SkipExtraNodeProducerPath("skip-A47D50", "A47D50");
        g_activeSgShim = "idle";
        return 0;
    }
    if (g_trustedAssetNodes > kVanillaNodeCapacity)
        RedirectProducerPointersFull("before-A47D50");
    g_sgActivationActive = true;
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_nodePlacementAnimTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_sgActivationActive = false;
        g_activeSgShim = "idle";
        RecordCrashIncident("SG-C004", "A47D50 placement", "SEH inside A47D50 trampoline");
        return -1;
    }
    if (g_trustedAssetNodes > kVanillaNodeCapacity)
        RedirectProducerPointersFull("after-A47D50");
    g_sgActivationActive = false;
    g_activeSgShim = "idle";
    return rv;
}

static int __cdecl ActivateNode_Shim(char actor, int nodeIdx) {
    g_activeSgShim = "A48910";
    const bool extraActivate = g_trustedAssetNodes > kVanillaNodeCapacity &&
        nodeIdx >= kVanillaNodeCapacity;
    if (g_trustedAssetNodes > kVanillaNodeCapacity)
        PrepSgActivationWriters("before-A48910");
    if (g_trustedAssetNodes > kVanillaNodeCapacity)
        RedirectProducerPointersFull("before-A48910");
    if (extraActivate) {
        EnsureActivate861PinnedBuffers("before-A48910");
        PrepActivationSlot860("before-A48910");
        ArmProducerSlot860Watch("before-A48910");
        g_sgBlockProducerAnims = true;
        HookLog("[ffx-hooks] FullGridCompiler before-A48910 node=%d actor=%d (menu stays 861; no exit-clamp)",
            nodeIdx, static_cast<int>(static_cast<unsigned char>(actor)));
    }
    g_sgActivationActive = extraActivate;
    int rv = -1;
    __try {
        rv = reinterpret_cast<ActivateNodeFn>(g_activateNodeTrampoline)(actor, nodeIdx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_sgActivationActive = false;
        g_sgBlockProducerAnims = false;
        g_sgPostActivateAnimGate = 0;
        g_activeSgShim = "idle";
        char detail[128] = {};
        snprintf(detail, sizeof(detail), "SEH inside A48910 node=%d actor=%d", nodeIdx,
            static_cast<int>(static_cast<unsigned char>(actor)));
        RecordCrashIncident(extraActivate ? "SG-C004" : "SG-C004", "A48910 activate", detail);
        return -1;
    }
    if (extraActivate) {
        g_sgPostActivateAnimGate = kSgPostActivateAnimGateFrames;
        g_sgBlockProducerAnims = true;
    }
    g_sgActivationActive = false;
    g_activeSgShim = "idle";
    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        CheckProducerSlot860Write("after-A48910");
        if (extraActivate) {
            g_sgNode860Activated = true;
            PostActivate860Guard("after-A48910-node860");
        } else {
            LitePostActivateRepair("after-A48910");
        }
        if (extraActivate)
            ForceAbmapPlacementAnimIdle("after-A48910-unlock");
    }
    g_sgBlockProducerAnims = false;
    return rv;
}

static void __cdecl UploadBatches_Shim() {
    /* SG-C028: lite scrub only — never RepairSlot860Poison (reentrant AV during A45570 sync). */
    if (!g_sgLayoutSyncActive && g_layoutGen > 0 && g_sgBatchArmed &&
        g_trustedAssetNodes > kVanillaNodeCapacity) {
        LitePreUploadScrub("pre-upload");
    }
    reinterpret_cast<NoArgIntFn>(g_uploadBatchesTrampoline)();
}

// A54660 teardown: always clamp 861->860 on exit (proven exit-only fix). Open/activate paths
// (A49590/A54860) must NOT clamp or slot860 draw/recompute corrupts GPU batches.
static int __cdecl ExitUiFlush_Shim() {
    g_activeSgShim = "A54660";
    MenuCountRestore mc = {};
    g_exitUiFlushActive = true;
    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        ReleaseSessionHeapRefs("before-A54660");
        LitePreUploadScrubExit("before-A54660");
        RepairSlot860PoisonExit("before-A54660");
        if (g_sgNode860Activated) {
            RedirectProducerPointersFull("before-A54660-post860");
            if (g_bootPosBatch860)
                PurgeBootArenaBatchPointers("before-A54660-post860");
            ScanForCorruptFloat("before-A54660-post860");
            RepairSlot860PoisonExit("before-A54660-post860");
            HookLog("[ffx-hooks] FullGridCompiler before-A54660 post-860-exit-scrub armed=1");
        }
    }
    if (g_trustedAssetNodes > kVanillaNodeCapacity)
        BeginVanillaMenuCountClamp(&mc, "before-A54660");
    int rv = -1;
    __try {
        rv = reinterpret_cast<NoArgIntFn>(g_exitUiFlushTrampoline)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_exitUiFlushActive = false;
        g_activeSgShim = "idle";
        EndVanillaMenuCountClamp(&mc);
        RecordCrashIncident("SG-C001", "A54660 exit", "SEH inside A54660 trampoline");
        return -1;
    }
    EndVanillaMenuCountClamp(&mc);
    g_exitUiFlushActive = false;
    g_activeSgShim = "idle";
    if (g_sgNode860Activated && rv >= 0)
        g_sgNode860Activated = false;
    HookLog("[ffx-hooks] FullGridCompiler after-A54660 rv=%d batch r16=%d r48=%d r8=%d slot860ok=%d armed=%d",
        rv, g_batch861Ready16 ? 1 : 0, g_batch861Ready48 ? 1 : 0,
        g_batch861Ready8 ? 1 : 0, DrawBatchSlot860Ready() ? 1 : 0, g_sgBatchArmed ? 1 : 0);
    return rv;
}

// Locate the deterministic corruption: the exit crash always reads float 0x3F008081 as a
// freelist link. Scan committed memory for that exact dword; the hit sitting next to allocator
// free-block magic 0xABCDEF12 is the smashed free-block. Logs address + context so we can set a
// hardware write-breakpoint there next run and catch the producer.
static void ScanForCorruptFloat(const char* tag) {
    if (!g_canaryInit) return;
    SanitizeBootPosBase();
    const uint32_t kNeedle = 0x3F008081u;
    int hits = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x02000000;
    const uintptr_t end = 0x40000000;
    while (addr < end && hits < 24) {
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const size_t rs = mbi.RegionSize;
        const bool readable = (mbi.State == MEM_COMMIT) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD);
        if (readable && rs >= 4) {
            const uint32_t* p = reinterpret_cast<const uint32_t*>(base);
            const size_t n = rs / 4;
            for (size_t i = 0; i < n && hits < 24; ++i) {
                if (p[i] == kNeedle) {
                    const uint32_t* ctx = p + i;
                    // look back up to 4096 bytes for the allocator free-block magic
                    bool nearMagic = false; uintptr_t magicAt = 0;
                    for (int b = 1; b <= 1024; ++b) {
                        if ((uintptr_t)(ctx - b) < base) break;
                        if (ctx[-b] == 0xABCDEF12u) { nearMagic = true; magicAt = reinterpret_cast<uintptr_t>(ctx - b); break; }
                    }
                    ++hits;
                    const uintptr_t hitAddr = reinterpret_cast<uintptr_t>(ctx);
                    const uintptr_t blockBase = hitAddr - 0xFF0u;
                    InferAndScrubBatchFromFloatHit(hitAddr);
                    HookLog("[ffx-hooks] FLOAT-HIT[%s] at=0x%08X block=0x%08X prev=%08X next=%08X nearMagic=%d magicAt=0x%08X",
                        tag, (unsigned)hitAddr, (unsigned)blockBase,
                        (i > 0 ? p[i-1] : 0), (i + 1 < n ? p[i+1] : 0),
                        nearMagic ? 1 : 0, (unsigned)magicAt);
                    if (nearMagic) FindAllocOwner(reinterpret_cast<uintptr_t>(ctx), tag);
                }
            }
        }
        addr = base + rs;
    }
    HookLog("[ffx-hooks] FLOAT-SCAN[%s] %d hits of 0x3F008081 bootPos=%p", tag, hits, g_bootPosBatch860);
    if (g_bootPosBatch860)
        PurgeBootArenaBatchPointers(tag);
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
// ── F1/F2 inline store redirect (v1.75–v1.77) ───────────────────────────────
// Patches inside FFX_Menu2D_DrawQuadIndexedBatch @7F4900: pos/col/uv stores
// remap stale 860-slot boot bases to pinned 861-slot buffers before fst/fstp.
static constexpr size_t kF1PatchLen = 5;
static constexpr int kF1SiteCount = 8;

enum class F1StubKind : uint8_t {
    NegWriterPrep,
    PosFstpMovsx,
    NegColPrep,
    ColFstMovEax14,
    UvFstEdxFldEbp,
    ColFstFldEbpDc,
    UvFstpMovEax18,
};

struct F1InlinePatchSite {
    uint32_t patchRva;
    uint32_t resumeRva;
    const uint8_t expected[kF1PatchLen];
    const char* label;
    F1StubKind stubKind;
};

static const F1InlinePatchSite kF1InlineSites[kF1SiteCount] = {
    { 0x003F4C0Fu, 0x003F4C14u, { 0x8B, 0x46, 0x14, 0xD9, 0xC9 }, "F1-NEG-writer-prep", F1StubKind::NegWriterPrep },
    { 0x003F5208u, 0x003F520Fu, { 0xD9, 0x1C, 0x88, 0x0F, 0xBF }, "F1-NEG-pos-store", F1StubKind::PosFstpMovsx },
    { 0x003F57E6u, 0x003F57EDu, { 0xD9, 0x1C, 0x88, 0x0F, 0xBF }, "F1-POS-pos-store", F1StubKind::PosFstpMovsx },
    { 0x003F50DFu, 0x003F50E6u, { 0x8B, 0xCA, 0x2B, 0xCB, 0xC1 }, "F2-NEG-col-prep", F1StubKind::NegColPrep },
    { 0x003F50E6u, 0x003F50ECu, { 0xD9, 0x14, 0x88, 0x8B, 0x46 }, "F2-NEG-col-store", F1StubKind::ColFstMovEax14 },
    { 0x003F54AAu, 0x003F54B3u, { 0xD9, 0x14, 0x90, 0xD9, 0x85 }, "F2-NEG-uv-store", F1StubKind::UvFstEdxFldEbp },
    { 0x003F56A4u, 0x003F56ADu, { 0xD9, 0x14, 0x88, 0xD9, 0x85 }, "F2-POS-col-store", F1StubKind::ColFstFldEbpDc },
    { 0x003F5B7Eu, 0x003F5B84u, { 0xD9, 0x1C, 0x88, 0x8B, 0x46 }, "F2-POS-uv-store", F1StubKind::UvFstpMovEax18 },
};

static uint8_t        g_f1Saved[kF1PatchLen * kF1SiteCount] = {};
static uint8_t*       g_f1Stubs[kF1SiteCount]              = {};
static size_t         g_f1StubLens[kF1SiteCount]           = {};
static uintptr_t      g_f1PatchVa[kF1SiteCount]             = {};
static bool           g_f1InlineInstalled                    = false;

static bool F1MemWrite(void* dest, const void* src, size_t len) {
    if (!dest || !src || len == 0) return false;
    DWORD old = 0;
    if (!VirtualProtect(dest, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dest, src, len);
    VirtualProtect(dest, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dest, len);
    return true;
}

static bool F1BytesMatch(const uint8_t* actual, const uint8_t* expected, size_t len) {
    return memcmp(actual, expected, len) == 0;
}

static void F1PatchCallRel(uint8_t* mem, size_t callInsnOff, uintptr_t helper) {
    const int32_t callRel = static_cast<int32_t>(helper -
        (reinterpret_cast<uintptr_t>(mem) + callInsnOff + 5));
    memcpy(mem + callInsnOff + 1, &callRel, sizeof(callRel));
}

static void F1AppendCall(std::vector<uint8_t>& stub, uintptr_t helper, size_t* outCallInsnOff) {
    (void)helper;
    if (outCallInsnOff) *outCallInsnOff = stub.size();
    stub.push_back(0xE8);
    stub.insert(stub.end(), { 0, 0, 0, 0 });
}

static uint32_t __cdecl SgF1RemapPosStoreEa(uint32_t baseEa, uint32_t ecxSlotMul12) {
    if (!g_sgF1InlineActive || g_trustedAssetNodes <= kVanillaNodeCapacity) return baseEa;
    if (ecxSlotMul12 < 12u * 860u) return baseEa;
    if (!IsStale41252PosBase(baseEa)) return baseEa;
    void* fresh = RedirectPosBatch();
    if (!fresh) return baseEa;
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler STORE-INLINE-POS slot=%u base=%08X->%p",
            ecxSlotMul12 / 12u, baseEa, fresh);
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fresh));
}

static uint32_t __cdecl SgF1RemapColStoreEa(uint32_t baseEa, uint32_t indexMul16) {
    if (!g_sgF1InlineActive || g_trustedAssetNodes <= kVanillaNodeCapacity) return baseEa;
    if (indexMul16 < 16u * 860u) return baseEa;
    if (!IsStale41252ColBase(baseEa)) return baseEa;
    void* fresh = RedirectColBatch();
    if (!fresh) return baseEa;
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler STORE-INLINE-COL slot=%u base=%08X->%p",
            indexMul16 / 16u, baseEa, fresh);
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fresh));
}

static uint32_t __cdecl SgF1RemapUvStoreEa(uint32_t baseEa, uint32_t indexMul8) {
    if (!g_sgF1InlineActive || g_trustedAssetNodes <= kVanillaNodeCapacity) return baseEa;
    if (indexMul8 < 8u * 860u) return baseEa;
    if (!IsStale41252UvBase(baseEa)) return baseEa;
    void* fresh = RedirectUvBatch();
    if (!fresh) return baseEa;
    static uint32_t s_logGen = 0;
    if (s_logGen != g_layoutGen) {
        s_logGen = g_layoutGen;
        HookLog("[ffx-hooks] FullGridCompiler STORE-INLINE-UV slot=%u base=%08X->%p",
            indexMul8 / 8u, baseEa, fresh);
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fresh));
}

static void __cdecl SgF1NegWriterResolved(void* writer, int n861) {
    if (!g_sgF1InlineActive || g_trustedAssetNodes <= kVanillaNodeCapacity) return;
    if (!writer || reinterpret_cast<uintptr_t>(writer) < 0x10000u) return;
    if (n861 >= 0 || n861 == -862 || static_cast<uint16_t>(n861) == 0xFFFFu) return;
    const int slot = (-1) - n861;
    if (slot < 860) return;
    if (PatchMenu2DWriterBuffers(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(writer)))) {
        static uint32_t s_logGen = 0;
        if (s_logGen != g_layoutGen) {
            s_logGen = g_layoutGen;
            HookLog("[ffx-hooks] FullGridCompiler STORE-INLINE-NEG writer=0x%08X slot=%d n861=%d",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(writer)), slot, n861);
        }
    }
}

static void F1EmitFpuSave(std::vector<uint8_t>& stub) {
    stub.insert(stub.end(), { 0x83, 0xEC, 0x04 });       // sub esp, 4
    stub.insert(stub.end(), { 0xD9, 0x1C, 0x24 });       // fst dword ptr [esp]
}

static void F1EmitFpuRestore(std::vector<uint8_t>& stub) {
    stub.insert(stub.end(), { 0xD9, 0x04, 0x24 });       // fld dword ptr [esp]
    stub.insert(stub.end(), { 0x83, 0xC4, 0x04 });       // add esp, 4
}

static void F1FinalizeStub(uint8_t* mem, size_t stubLen, size_t callOff, uintptr_t helper,
    uintptr_t resumeVa) {
    F1PatchCallRel(mem, callOff, helper);
    const size_t jmpOff = stubLen - 5;
    const int32_t jmpRel = static_cast<int32_t>(resumeVa -
        (reinterpret_cast<uintptr_t>(mem) + jmpOff + 5));
    memcpy(mem + jmpOff + 1, &jmpRel, sizeof(jmpRel));
    FlushInstructionCache(GetCurrentProcess(), mem, stubLen);
}

static uint8_t* BuildF1NegWriterPrepStub(uintptr_t resumeVa, size_t* outLen) {
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&SgF1NegWriterResolved);
    std::vector<uint8_t> stub;
    stub.push_back(0x53);
    stub.push_back(0x56);
    F1EmitFpuSave(stub);
    stub.push_back(0x53);
    stub.push_back(0x56);
    size_t callOff = 0;
    F1AppendCall(stub, helper, &callOff);
    stub.insert(stub.end(), { 0x83, 0xC4, 0x08 });
    F1EmitFpuRestore(stub);
    stub.push_back(0x5E);
    stub.push_back(0x5B);
    stub.insert(stub.end(), { 0x8B, 0x46, 0x14 });
    stub.insert(stub.end(), { 0xD9, 0xC9 });
    stub.push_back(0xE9);
    stub.insert(stub.end(), { 0, 0, 0, 0 });
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stub.data(), stub.size());
    F1FinalizeStub(mem, stub.size(), callOff, helper, resumeVa);
    *outLen = stub.size();
    return mem;
}

static uint8_t* BuildF2NegColPrepStub(uintptr_t resumeVa, size_t* outLen) {
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&SgF1NegWriterResolved);
    std::vector<uint8_t> stub;
    stub.push_back(0x53);
    stub.push_back(0x56);
    F1EmitFpuSave(stub);
    stub.push_back(0x53);
    stub.push_back(0x56);
    size_t callOff = 0;
    F1AppendCall(stub, helper, &callOff);
    stub.insert(stub.end(), { 0x83, 0xC4, 0x08 });
    F1EmitFpuRestore(stub);
    stub.push_back(0x5E);
    stub.push_back(0x5B);
    stub.insert(stub.end(), { 0x8B, 0xCA, 0x2B, 0xCB, 0xC1, 0xE1, 0x04 });
    stub.push_back(0xE9);
    stub.insert(stub.end(), { 0, 0, 0, 0 });
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stub.data(), stub.size());
    F1FinalizeStub(mem, stub.size(), callOff, helper, resumeVa);
    *outLen = stub.size();
    return mem;
}

static uint8_t* BuildF1PosFstpMovsxStub(uintptr_t resumeVa, size_t* outLen) {
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&SgF1RemapPosStoreEa);
    std::vector<uint8_t> stub;
    stub.push_back(0x51);
    stub.push_back(0x50);
    F1EmitFpuSave(stub);
    stub.push_back(0x51);
    stub.push_back(0x50);
    size_t callOff = 0;
    F1AppendCall(stub, helper, &callOff);
    stub.insert(stub.end(), { 0x83, 0xC4, 0x08 });
    F1EmitFpuRestore(stub);
    stub.push_back(0x5B);
    stub.push_back(0x59);
    stub.insert(stub.end(), { 0xD9, 0x1C, 0x88 });
    stub.insert(stub.end(), { 0x0F, 0xBF, 0x47, 0x02 });
    stub.push_back(0xE9);
    stub.insert(stub.end(), { 0, 0, 0, 0 });
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stub.data(), stub.size());
    F1FinalizeStub(mem, stub.size(), callOff, helper, resumeVa);
    *outLen = stub.size();
    return mem;
}

static uint8_t* BuildF1ColFstStub(uintptr_t resumeVa, const uint8_t* tail, size_t tailLen, size_t* outLen) {
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&SgF1RemapColStoreEa);
    std::vector<uint8_t> stub;
    stub.push_back(0x51);
    stub.push_back(0x50);
    F1EmitFpuSave(stub);
    stub.push_back(0x51);
    stub.push_back(0x50);
    size_t callOff = 0;
    F1AppendCall(stub, helper, &callOff);
    stub.insert(stub.end(), { 0x83, 0xC4, 0x08 });
    F1EmitFpuRestore(stub);
    stub.push_back(0x5B);
    stub.push_back(0x59);
    stub.insert(stub.end(), { 0xD9, 0x14, 0x88 });
    stub.insert(stub.end(), tail, tail + tailLen);
    stub.push_back(0xE9);
    stub.insert(stub.end(), { 0, 0, 0, 0 });
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stub.data(), stub.size());
    F1FinalizeStub(mem, stub.size(), callOff, helper, resumeVa);
    *outLen = stub.size();
    return mem;
}

static uint8_t* BuildF1UvFstEdxStub(uintptr_t resumeVa, size_t* outLen) {
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&SgF1RemapUvStoreEa);
    std::vector<uint8_t> stub;
    stub.push_back(0x52);
    stub.push_back(0x50);
    F1EmitFpuSave(stub);
    stub.push_back(0x52);
    stub.push_back(0x50);
    size_t callOff = 0;
    F1AppendCall(stub, helper, &callOff);
    stub.insert(stub.end(), { 0x83, 0xC4, 0x08 });
    F1EmitFpuRestore(stub);
    stub.push_back(0x5B);
    stub.push_back(0x5A);
    stub.insert(stub.end(), { 0xD9, 0x14, 0x90 });
    stub.insert(stub.end(), { 0xD9, 0x85, 0xF0, 0xFE, 0xFF, 0xFF });
    stub.push_back(0xE9);
    stub.insert(stub.end(), { 0, 0, 0, 0 });
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stub.data(), stub.size());
    F1FinalizeStub(mem, stub.size(), callOff, helper, resumeVa);
    *outLen = stub.size();
    return mem;
}

static uint8_t* BuildF1UvFstpMovEax18Stub(uintptr_t resumeVa, size_t* outLen) {
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&SgF1RemapUvStoreEa);
    std::vector<uint8_t> stub;
    stub.push_back(0x51);
    stub.push_back(0x50);
    F1EmitFpuSave(stub);
    stub.push_back(0x51);
    stub.push_back(0x50);
    size_t callOff = 0;
    F1AppendCall(stub, helper, &callOff);
    stub.insert(stub.end(), { 0x83, 0xC4, 0x08 });
    F1EmitFpuRestore(stub);
    stub.push_back(0x5B);
    stub.push_back(0x59);
    stub.insert(stub.end(), { 0xD9, 0x1C, 0x88 });
    stub.insert(stub.end(), { 0x8B, 0x46, 0x18 });
    stub.push_back(0xE9);
    stub.insert(stub.end(), { 0, 0, 0, 0 });
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stub.data(), stub.size());
    F1FinalizeStub(mem, stub.size(), callOff, helper, resumeVa);
    *outLen = stub.size();
    return mem;
}

static uint8_t* BuildF1StubForSite(const F1InlinePatchSite& site, uintptr_t resumeVa, size_t* outLen) {
    switch (site.stubKind) {
    case F1StubKind::NegWriterPrep:
        return BuildF1NegWriterPrepStub(resumeVa, outLen);
    case F1StubKind::PosFstpMovsx:
        return BuildF1PosFstpMovsxStub(resumeVa, outLen);
    case F1StubKind::NegColPrep:
        return BuildF2NegColPrepStub(resumeVa, outLen);
    case F1StubKind::ColFstMovEax14: {
        static const uint8_t kTail[] = { 0x8B, 0x46, 0x14 };
        return BuildF1ColFstStub(resumeVa, kTail, sizeof(kTail), outLen);
    }
    case F1StubKind::UvFstEdxFldEbp:
        return BuildF1UvFstEdxStub(resumeVa, outLen);
    case F1StubKind::ColFstFldEbpDc: {
        static const uint8_t kTail[] = { 0xD9, 0x85, 0xDC, 0xFE, 0xFF, 0xFF };
        return BuildF1ColFstStub(resumeVa, kTail, sizeof(kTail), outLen);
    }
    case F1StubKind::UvFstpMovEax18:
        return BuildF1UvFstpMovEax18Stub(resumeVa, outLen);
    }
    return nullptr;
}

static bool InstallF1InlinePatchSite(uintptr_t base, int siteIdx) {
    const F1InlinePatchSite& site = kF1InlineSites[siteIdx];
    const uintptr_t patchVa = base + site.patchRva;
    const uintptr_t resumeVa = base + site.resumeRva;
    uint8_t* patchMem = reinterpret_cast<uint8_t*>(patchVa);
    if (!F1BytesMatch(patchMem, site.expected, kF1PatchLen)) {
        HookLog("[ffx-hooks] WARN FullGridCompiler %s byte mismatch @0x%08X",
            site.label, static_cast<unsigned>(site.patchRva));
        return false;
    }
    size_t stubLen = 0;
    uint8_t* stub = BuildF1StubForSite(site, resumeVa, &stubLen);
    if (!stub) {
        HookLog("[ffx-hooks] WARN FullGridCompiler %s stub alloc failed", site.label);
        return false;
    }
    memcpy(g_f1Saved + siteIdx * kF1PatchLen, patchMem, kF1PatchLen);
    g_f1PatchVa[siteIdx] = patchVa;
    g_f1Stubs[siteIdx] = stub;
    g_f1StubLens[siteIdx] = stubLen;
    uint8_t jmp[kF1PatchLen] = { 0xE9, 0, 0, 0, 0 };
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (patchVa + 5));
    memcpy(jmp + 1, &rel, sizeof(rel));
    if (!F1MemWrite(patchMem, jmp, kF1PatchLen)) {
        HookLog("[ffx-hooks] WARN FullGridCompiler %s VirtualProtect failed @0x%08X",
            site.label, static_cast<unsigned>(site.patchRva));
        VirtualFree(stub, 0, MEM_RELEASE);
        g_f1Stubs[siteIdx] = nullptr;
        return false;
    }
    HookLog("[ffx-hooks] FullGridCompiler %s inline patch ok patch=0x%08X resume=0x%08X stub=%p",
        site.label, static_cast<unsigned>(site.patchRva), static_cast<unsigned>(site.resumeRva),
        static_cast<void*>(stub));
    return true;
}

static int InstallF1InlinePatches(uintptr_t base) {
    if (!g_sgF1InlineInstallPatches) {
        if (g_sgF1InlineActive && g_trustedAssetNodes > kVanillaNodeCapacity) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                HookLog("[ffx-hooks] FullGridCompiler F1-INLINE deferred (SG-C045): "
                    "need sg_f1_inline_install.flag — sg_f1_inline.flag is noop");
            }
        }
        return 0;
    }
    if (!g_sgF1InlineActive || g_trustedAssetNodes <= kVanillaNodeCapacity) return 0;
    if (g_f1InlineInstalled) return kF1SiteCount;
    int ok = 0;
    for (int i = 0; i < kF1SiteCount; ++i) {
        if (InstallF1InlinePatchSite(base, i)) ++ok;
    }
    if (ok > 0) {
        g_f1InlineInstalled = true;
        g_f1Phase2Complete = (ok == kF1SiteCount);
        HookLog("[ffx-hooks] FullGridCompiler F1-INLINE installed %d/%d phase2=%d (SKIP860 lift needs sg_f1_inline_draw860.flag)",
            ok, kF1SiteCount, g_f1Phase2Complete ? 1 : 0);
    }
    return ok;
}

static void RemoveF1InlinePatches() {
    if (!g_f1InlineInstalled) return;
    for (int i = 0; i < kF1SiteCount; ++i) {
        if (g_f1PatchVa[i])
            F1MemWrite(reinterpret_cast<void*>(g_f1PatchVa[i]), g_f1Saved + i * kF1PatchLen, kF1PatchLen);
        if (g_f1Stubs[i]) {
            VirtualFree(g_f1Stubs[i], 0, MEM_RELEASE);
            g_f1Stubs[i] = nullptr;
        }
        g_f1PatchVa[i] = 0;
        g_f1StubLens[i] = 0;
    }
    g_f1InlineInstalled = false;
    g_f1Phase2Complete = false;
}
#endif // FFXHOOKS_HAVE_POLYHOOK

static void DumpCoreCensus(const char* tag) {
    if (!g_canaryInit) return;
    for (int i = 0; i < g_coreSeenCount; ++i) {
        const CoreSeen e = g_coreSeen[i];
        HookLog("[ffx-hooks] CORE-ALLOC[%s] size=%u (n860=%.2f n861=%.2f) stack=%08X<-%08X<-%08X<-%08X",
            tag, e.size, e.size / 860.0, e.size / 861.0,
            e.frames[0], e.frames[1], e.frames[2], e.frames[3]);
    }
    HookLog("[ffx-hooks] CORE-CENSUS[%s] %d unique node-plausible sizes", tag, g_coreSeenCount);
}

static bool InstallDetour(uintptr_t targetVa, uint64_t hookFn, uint64_t* trampoline,
    PLH::x86Detour** detour, const char* label) {
    try {
        *detour = new PLH::x86Detour(static_cast<uint64_t>(targetVa), hookFn, trampoline);
        if (!(*detour)->hook()) {
            HookLog("[ffx-hooks] WARN FullGridCompiler %s detour hook() false @0x%08X",
                label, static_cast<unsigned>(targetVa));
            delete *detour;
            *detour = nullptr;
            *trampoline = 0;
            return false;
        }
        HookLog("[ffx-hooks] FullGridCompiler %s detour ok @0x%08X tramp=0x%llX",
            label, static_cast<unsigned>(targetVa), static_cast<unsigned long long>(*trampoline));
        return true;
    } catch (const std::exception& ex) {
        HookLog("[ffx-hooks] WARN FullGridCompiler %s exception: %s", label, ex.what());
    } catch (...) {
        HookLog("[ffx-hooks] WARN FullGridCompiler %s unknown exception", label);
    }
    delete *detour;
    *detour = nullptr;
    *trampoline = 0;
    return false;
}

static void RemoveDetour(PLH::x86Detour** detour, uint64_t* trampoline) {
    if (*detour) {
        (*detour)->unHook();
        delete *detour;
        *detour = nullptr;
    }
    if (trampoline) *trampoline = 0;
}
#endif

} // namespace

SphereGridFullGridCompilerInstallResult InstallSphereGridFullGridCompilerHook(
    uintptr_t base, SphereGridFullGridCompilerLogFn log) {
    SphereGridFullGridCompilerInstallResult result = { false, 0, 0, false, false };
    if (g_installed) {
        result.ok = true;
        result.extraNodeCount = g_sidecar.nodeCount;
        result.extraLinkCount = g_sidecar.linkCount;
        result.sidecarLoaded = g_sidecar.loaded;
        result.hashMatched = g_sidecar.hashMatched;
        return result;
    }

    g_base = base;
    g_logFn = log;
    g_compileRan = false;
    ConfigureModes();

    HookLog("[ffx-hooks] FullGridCompiler %s observe=%d write=%d sidecar=%d clamp=%d skipHash=%d draw860-flag=%d f1-inline=%d f1-install=%d f1-lift-skip=%d",
        kFullGridCompilerVersion, g_observeOnly ? 1 : 0, g_writeCompile ? 1 : 0, g_writeSidecar ? 1 : 0,
        g_clampVanillaLoop ? 1 : 0, g_skipHashCheck ? 1 : 0, g_drawSlot860Experimental ? 1 : 0,
        g_sgF1InlineActive ? 1 : 0, g_sgF1InlineInstallPatches ? 1 : 0, g_sgF1InlineLiftSkip860 ? 1 : 0);
    LogCrashRegistrySummary();

    LoadTrustedAssetCounts();
    LoadSidecarFile();
    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        HookLog("[ffx-hooks] FullGridCompiler manifest nodes=%d (batch bump deferred until A45570)",
            g_trustedAssetNodes);
    }

#ifdef FFXHOOKS_HAVE_POLYHOOK
    int detours = 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_LOAD_LAYOUT,
        reinterpret_cast<uint64_t>(&LayoutLoad_Shim), &g_layoutTrampoline, &g_layoutDetour, "A45570-layout") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_SPHERE_GRID_INIT_RUNTIME_STATE,
        reinterpret_cast<uint64_t>(&InitRuntimeState_Shim), &g_initTrampoline, &g_initDetour, "A53DE0-init") ? 1 : 0;
    if (!IsGridTeachHookInstalled()) {
        detours += InstallDetour(base + RVA_FFX_BTL_PREPARE_SAVE_COMMAND_STATE,
            reinterpret_cast<uint64_t>(&PrepareSaveFieldGuard_Shim), &g_prepSaveTrampoline, &g_prepSaveDetour,
            "786BC0-field-guard") ? 1 : 0;
    }
    detours += InstallDetour(base + RVA_FFX_SPHERE_GRID_LOAD_DEFAULT_STATE,
        reinterpret_cast<uint64_t>(&DefaultState_Shim), &g_defaultStateTrampoline, &g_defaultStateDetour, "A47210-default") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_APPLY_STATE_TO_MENU,
        reinterpret_cast<uint64_t>(&ApplyStateToMenu_Shim), &g_applyTrampoline, &g_applyDetour, "A49590-apply") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_SAVE_MENU_TO_STATE,
        reinterpret_cast<uint64_t>(&SaveMenuToState_Shim), &g_saveTrampoline, &g_saveDetour, "A5BB70-save") ? 1 : 0;
    detours += InstallDetour(base + RVA_FFX_ABMAP_RECOMPUTE_STATS,
        reinterpret_cast<uint64_t>(&RecomputeStats_Shim), &g_recomputeTrampoline, &g_recomputeDetour, "A54860-recompute") ? 1 : 0;

    // 861-node mitigation: bump 860/881-slot batch allocs; allow draw slot 860 when bumped.
    InstallDetour(base + RVA_FFX_GAME_ALLOC_WRAPPER,
        reinterpret_cast<uint64_t>(&GameAlloc_Shim), &g_gameAllocTrampoline, &g_gameAllocDetour, "630670-batch-bump");
    InstallDetour(base + RVA_FFX_SG_DRAW_NODE_BATCH,
        reinterpret_cast<uint64_t>(&DrawBatch_Shim), &g_drawBatchTrampoline, &g_drawBatchDetour, "7F4900-draw-redirect");
    InstallDetour(base + RVA_FFX_ABMAP_NODE_PLACEMENT_ANIM,
        reinterpret_cast<uint64_t>(&NodePlacementAnim_Shim), &g_nodePlacementAnimTrampoline,
        &g_nodePlacementAnimDetour, "A47D50-activation-prep");
    InstallDetour(base + RVA_FFX_ABMAP_ACTIVATE_NODE,
        reinterpret_cast<uint64_t>(&ActivateNode_Shim), &g_activateNodeTrampoline,
        &g_activateNodeDetour, "A48910-activation-prep");
    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        InstallDetour(base + RVA_FFX_ABMAP_POPULATE_LINK_BATCHES,
            reinterpret_cast<uint64_t>(&PopulateLinkBatches_Shim), &g_populateLinkBatchesTrampoline,
            &g_populateLinkBatchesDetour, "A5A800-link-geom");
        InstallDetour(base + RVA_FFX_ABMAP_RUN_PLACEMENT_FX,
            reinterpret_cast<uint64_t>(&RunPlacementFx_Shim), &g_runPlacementFxTrampoline,
            &g_runPlacementFxDetour, "A45930-placement-fx");
        InstallDetour(base + RVA_FFX_ABMAP_UPDATE_PLACEMENT_SLOT,
            reinterpret_cast<uint64_t>(&UpdatePlacementSlot_Shim), &g_updatePlacementSlotTrampoline,
            &g_updatePlacementSlotDetour, "A58080-placement-slot");
        InstallDetour(base + RVA_FFX_MENU2D_FLUSH_CAPTURE_BATCH48,
            reinterpret_cast<uint64_t>(&FlushCaptureBatch48_Shim), &g_flushCaptureBatch48Trampoline,
            &g_flushCaptureBatch48Detour, "712330-anim-capture-redirect");
    }
    InstallDetour(base + RVA_FFX_HEAP_ALLOC_CORE,
        reinterpret_cast<uint64_t>(&AllocCore_Shim), &g_allocCoreTrampoline, &g_allocCoreDetour, "942B60-core-bump");
    InstallDetour(base + RVA_FFX_ABMAP_EXIT_FULL_UI_FLUSH,
        reinterpret_cast<uint64_t>(&ExitUiFlush_Shim), &g_exitUiFlushTrampoline, &g_exitUiFlushDetour, "A54660-exit-clamp");
    /* SG-C038: END_CAPTURE @2392A0 OFF — per-frame scrub caused absurd lag (v1.69). Upload @2859E0 stays OFF. */

    if (g_trustedAssetNodes > kVanillaNodeCapacity) {
        if (g_traceAlloc) {
            InitializeCriticalSection(&g_canaryCs);
            g_canaryInit = true;
        }
        InstallDetour(base + RVA_FFX_HEAP_ALLOC,
            reinterpret_cast<uint64_t>(&HeapAlloc_Shim), &g_allocTrampoline, &g_allocDetour, "sub_687190-batch-bump");
        if (g_traceAlloc)
            HookLog("[ffx-hooks] FullGridCompiler ALLOC-TRACE+CANARY on sub_687190; guardPad=%d canary=%dB", g_guardPad, kCanaryBytes);
        else
            HookLog("[ffx-hooks] FullGridCompiler sub_687190 batch-bump detour (861-node path/index allocs)");
    } else if (g_traceAlloc) {
        InitializeCriticalSection(&g_canaryCs);
        g_canaryInit = true;
        InstallDetour(base + RVA_FFX_HEAP_ALLOC,
            reinterpret_cast<uint64_t>(&HeapAlloc_Shim), &g_allocTrampoline, &g_allocDetour, "sub_687190-alloc-trace");
        HookLog("[ffx-hooks] FullGridCompiler ALLOC-TRACE+CANARY detour installed; guardPad=%d canary=%dB", g_guardPad, kCanaryBytes);
    }

    if (detours == 0) {
        HookLog("[ffx-hooks] FullGridCompiler disabled: no detours installed");
        return result;
    }
    if (g_trustedAssetNodes > kVanillaNodeCapacity)
        InstallF1InlinePatches(base);
    HookLog("[ffx-hooks] FullGridCompiler detours installed count=%d", detours);
#else
    HookLog("[ffx-hooks] WARN FullGridCompiler needs PolyHook");
    return result;
#endif

    g_installed = true;
    result.ok = true;
    result.extraNodeCount = g_sidecar.nodeCount;
    result.extraLinkCount = g_sidecar.linkCount;
    result.sidecarLoaded = g_sidecar.loaded;
    result.hashMatched = g_sidecar.hashMatched;
    return result;
}

bool RemoveSphereGridFullGridCompilerHook(SphereGridFullGridCompilerLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    RemoveF1InlinePatches();
    RemoveDetour(&g_layoutDetour, &g_layoutTrampoline);
    RemoveDetour(&g_prepSaveDetour, &g_prepSaveTrampoline);
    RemoveDetour(&g_initDetour, &g_initTrampoline);
    RemoveDetour(&g_defaultStateDetour, &g_defaultStateTrampoline);
    RemoveDetour(&g_applyDetour, &g_applyTrampoline);
    RemoveDetour(&g_saveDetour, &g_saveTrampoline);
    RemoveDetour(&g_recomputeDetour, &g_recomputeTrampoline);
    RemoveDetour(&g_allocDetour, &g_allocTrampoline);
    RemoveDetour(&g_allocCoreDetour, &g_allocCoreTrampoline);
    RemoveDetour(&g_gameAllocDetour, &g_gameAllocTrampoline);
    RemoveDetour(&g_drawBatchDetour, &g_drawBatchTrampoline);
    RemoveDetour(&g_exitUiFlushDetour, &g_exitUiFlushTrampoline);
    RemoveDetour(&g_uploadBatchesDetour, &g_uploadBatchesTrampoline);
    RemoveDetour(&g_endCaptureDetour, &g_endCaptureTrampoline);
    RemoveDetour(&g_populateLinkBatchesDetour, &g_populateLinkBatchesTrampoline);
    RemoveDetour(&g_runPlacementFxDetour, &g_runPlacementFxTrampoline);
    RemoveDetour(&g_updatePlacementSlotDetour, &g_updatePlacementSlotTrampoline);
    RemoveDetour(&g_flushCaptureBatch48Detour, &g_flushCaptureBatch48Trampoline);
    RemoveDetour(&g_nodePlacementAnimDetour, &g_nodePlacementAnimTrampoline);
    RemoveDetour(&g_activateNodeDetour, &g_activateNodeTrampoline);
#endif
    g_installed = false;
    g_base = 0;
    g_logFn = nullptr;
    memset(&g_sidecar, 0, sizeof(g_sidecar));
    g_sidecarPath[0] = '\0';
    g_compileRan = false;
    if (log) log("[ffx-hooks] FullGridCompiler removed ok");
    return true;
}

bool IsSphereGridFullGridCompilerHookInstalled() {
    return g_installed;
}

// Called from the FaultProbe VEH (dllmain) at crash time: report every live alloc whose
// trailing canary was smashed -> that (callerRva,size) is the buffer the 861st node overran.
extern "C" void FfxHooks_FullGridScanCanaries(void) { ScanCanariesImpl("FAULT"); ScanForCorruptFloat("FAULT"); }

extern "C" void FfxHooks_FullGridRecordFault(uint32_t pcRva, uint32_t accessed, uint32_t eax,
    uint32_t ebx, uint32_t ecx, uint32_t edx) {
    char detail[256] = {};
    snprintf(detail, sizeof(detail),
        "VEH pcRva=0x%08X accessed=0x%08X eax=0x%08X ebx=0x%08X ecx=0x%08X edx=0x%08X",
        pcRva, accessed, eax, ebx, ecx, edx);
    RecordCrashIncident(ClassifyVehFault(pcRva, accessed, eax), "VEH-FAULT", detail);
}

} // namespace FfxHooks
