// ============================================================================
//  PhotoModeActions.h  —  REFERÊNCIA paste-ready pro porte C++ (Photo Mode)
//
//  Jarvis-AURORA, 2026-06-09.  Lane: câmera/ator RAM.
//
//  ⚠️ ESTE ARQUIVO É REFERÊNCIA — NÃO está wired no build, NÃO toca `dllmain.cpp`.
//     É o blueprint pra Jarvis-Yojimbo (ou Aurora) COLAR no menu nativo quando a
//     integração começar. Quem editar `dllmain.cpp` AVISA no SESSION_HANDOFF (regra LANES).
//
//  Espelha o protótipo PowerShell: RuntimeTools/BattlePhotoMode/BattlePhotoMode.ps1
//  Contrato/itens de menu/input: docs/ai/HANDOFF_AURORA_PHOTOMODE_CONTRACT_2026-06-09.md
//  RE da câmera/ator: docs/reverse/FFX_BATTLE_CAMERA_RAM_NEIGHBORHOOD_2026-06-09.md
//
//  CHAVE DE OURO (por que isto é melhor que o PowerShell):
//    A DLL roda DENTRO do processo do jogo → acesso DIRETO à memória (sem
//    ReadProcessMemory/WriteProcessMemory). E chamando Tick() NO FRAME, DEPOIS do
//    update do ator (FFX_Chr_UpdateActiveInstances @0x832E90), as overrides ganham
//    a corrida → mover/rotacionar/mirar fica LISO, sem o jitter do lab externo.
// ============================================================================
#pragma once
#include <cstdint>
#include <cmath>

namespace PhotoMode {

// ── Offsets (relativos ao módulo FFX.exe; endereço = g_base + RVA) ───────────
//    RE: dllmain.cpp (AuroraReadActors) + FFX_Chr_UpdateActiveInstances @0x832E90.
constexpr uint32_t RVA_ACTIVE_CHR_COUNT = 0x01FC44E0; // u32 count
constexpr uint32_t RVA_ACTIVE_CHR_TABLE = 0x01FC44E4; // u32 ptr da tabela
constexpr uint32_t ACTIVE_CHR_STRIDE    = 0x880;
constexpr uint32_t RVA_CAM_REF          = 0x00D378A0; // câmera ref X/Y/Z (look-at) — controla câmera viva (PROVADO)

// offsets DENTRO de cada ator (inst = table + idx*0x880):
constexpr uint32_t A_ID     = 0x000; // u16  (party<0x1000 | monstro 0x1000..0x1FFF | other 0x4xxx)
constexpr uint32_t A_ACTIVE = 0x002; // u8   (0 = pula)
constexpr uint32_t A_POSX   = 0x00C; // float  <- MOVER escreve aqui
constexpr uint32_t A_POSY   = 0x010;
constexpr uint32_t A_POSZ   = 0x014;
constexpr uint32_t A_FACING = 0x168; // float facing (rad)  <- ROTAÇÃO LIMPA (engine usa em cos/sin)
constexpr uint32_t A_FLAGS  = 0x194; // u32 (bit 0x80 = reserve clone)
constexpr uint32_t A_MTX    = 0x1D0; // world matrix 4x4 (row-major)
constexpr uint32_t A_MTX_TX = 0x200; // matrix translation (tx,ty,tz) == pos renderizada  <- MOVER tbm escreve aqui

extern uintptr_t g_base; // base do módulo FFX.exe (já existe como `g_base` no dllmain)

// ── Acesso in-process direto. Em produção, ler com guarda (reusar AuroraReadU32/
//    AuroraReadFloat/AuroraPtrOk do dllmain p/ não AV em ponteiro ruim). Escrever
//    nos `inst` já validados é seguro (data pages RW; sem VirtualProtect). ────────
inline float    RF (uintptr_t a)          { return *reinterpret_cast<float*>(a); }
inline void     WF (uintptr_t a, float v) { *reinterpret_cast<float*>(a) = v; }
inline uint32_t RU32(uintptr_t a)         { return *reinterpret_cast<uint32_t*>(a); }
inline uint16_t RU16(uintptr_t a)         { return *reinterpret_cast<uint16_t*>(a); }
inline uint8_t  RU8 (uintptr_t a)         { return *reinterpret_cast<uint8_t*>(a); }

enum Kind { PARTY, MONSTER, OTHER };
inline Kind KindOf(uint16_t id) {
    if (id >= 0x1000 && id <= 0x1FFF) return MONSTER;
    if (id < 0x1000) return PARTY;
    return OTHER;
}

struct ActorOverride {
    uint32_t  idx;
    uintptr_t inst;
    uint16_t  id;
    bool      dirty;                 // tem edição pra segurar no tick?
    float     x, y, z, yaw;          // alvo
    float     ox, oy, oz, oyaw;      // original (restore)
};

struct State {
    bool on       = false;
    bool frozen   = false;           // ver nota de FREEZE abaixo
    int  selected = 0;
    int  count    = 0;
    ActorOverride act[64];
    bool  camDirty = false;
    float camX, camY, camZ;          // alvo da câmera (ref)
    float camOX, camOY, camOZ;       // original
};
extern State g_pm;

// ── Enter: snapshot dos atores deployados + câmera (espelha AuroraReadActors) ──
inline void Enter() {
    g_pm = State{}; g_pm.on = true;
    const uint32_t  cnt   = RU32(g_base + RVA_ACTIVE_CHR_COUNT);
    const uintptr_t table = RU32(g_base + RVA_ACTIVE_CHR_TABLE);
    if (!table || cnt == 0 || cnt > 4096) return;
    for (uint32_t i = 0; i < cnt && g_pm.count < 64; ++i) {
        const uintptr_t inst = table + (uintptr_t)i * ACTIVE_CHR_STRIDE;
        if (RU8(inst + A_ACTIVE) == 0) continue;
        const uint16_t id = RU16(inst + A_ID);
        if (KindOf(id) == OTHER) continue;
        const float x = RF(inst + A_POSX), y = RF(inst + A_POSY), z = RF(inst + A_POSZ);
        if ((fabsf(x) < 0.01f && fabsf(y) < 0.01f && fabsf(z) < 0.01f) || fabsf(x) > 1000.0f) continue; // não-deployado
        ActorOverride& a = g_pm.act[g_pm.count++];
        a.idx = i; a.inst = inst; a.id = id; a.dirty = false;
        a.x = a.ox = x; a.y = a.oy = y; a.z = a.oz = z;
        a.yaw = a.oyaw = RF(inst + A_FACING);
    }
    g_pm.camX = g_pm.camOX = RF(g_base + RVA_CAM_REF);
    g_pm.camY = g_pm.camOY = RF(g_base + RVA_CAM_REF + 4);
    g_pm.camZ = g_pm.camOZ = RF(g_base + RVA_CAM_REF + 8);
}

// ── Ações (chamadas pelos eventos do menu nativo — ver contrato §4) ──────────
inline void SelectDelta(int d) {
    if (g_pm.count) g_pm.selected = (g_pm.selected + d + g_pm.count) % g_pm.count;
}
inline void MoveSelected(float dx, float dy, float dz) {
    if (!g_pm.count) return;
    ActorOverride& a = g_pm.act[g_pm.selected];
    a.x += dx; a.y += dy; a.z += dz; a.dirty = true;
}
inline void RotateSelected(float dyaw) {
    if (!g_pm.count) return;
    ActorOverride& a = g_pm.act[g_pm.selected];
    a.yaw += dyaw; a.dirty = true;            // <- caminho LIMPO (A_FACING), não a matriz 3x3
}
inline void PanCamera(float dx, float dy, float dz) {
    g_pm.camX += dx; g_pm.camY += dy; g_pm.camZ += dz; g_pm.camDirty = true;
}

// ── TICK: CHAMAR TODO FRAME, DEPOIS do update do ator (Present hook já roda 1x/frame).
//    É AQUI que o move/rotate/mirar fica LISO (override vence porque escreve por último). ──
inline void Tick() {
    if (!g_pm.on || g_pm.frozen) return;
    for (int i = 0; i < g_pm.count; ++i) {
        ActorOverride& a = g_pm.act[i];
        if (!a.dirty) continue;
        WF(a.inst + A_POSX, a.x);     WF(a.inst + A_POSY, a.y);       WF(a.inst + A_POSZ, a.z);
        WF(a.inst + A_MTX_TX, a.x);   WF(a.inst + A_MTX_TX + 4, a.y); WF(a.inst + A_MTX_TX + 8, a.z);
        WF(a.inst + A_FACING, a.yaw);
    }
    if (g_pm.camDirty) {
        WF(g_base + RVA_CAM_REF,     g_pm.camX);
        WF(g_base + RVA_CAM_REF + 4, g_pm.camY);
        WF(g_base + RVA_CAM_REF + 8, g_pm.camZ);
    }
}

inline void ResetAll() {
    for (int i = 0; i < g_pm.count; ++i) {
        ActorOverride& a = g_pm.act[i];
        if (!a.dirty) continue;
        WF(a.inst + A_POSX, a.ox);     WF(a.inst + A_POSY, a.oy);       WF(a.inst + A_POSZ, a.oz);
        WF(a.inst + A_MTX_TX, a.ox);   WF(a.inst + A_MTX_TX + 4, a.oy); WF(a.inst + A_MTX_TX + 8, a.oz);
        WF(a.inst + A_FACING, a.oyaw);
        a.dirty = false; a.x = a.ox; a.y = a.oy; a.z = a.oz; a.yaw = a.oyaw;
    }
    if (g_pm.camDirty) {
        WF(g_base + RVA_CAM_REF,     g_pm.camOX);
        WF(g_base + RVA_CAM_REF + 4, g_pm.camOY);
        WF(g_base + RVA_CAM_REF + 8, g_pm.camOZ);
        g_pm.camDirty = false;
    }
}
inline void Exit() { ResetAll(); g_pm.on = false; }

// Snapshot(): percorrer g_pm.act[0..count] + camera e serializar `scene.json`
//   (formato compatível com BattleActorRamCapture/actors.json p/ o import do Aurora).
//   Reusar o writer JSON do projeto; campos: {camera:{refX,refY,refZ}, actors:[{id,kind,x,y,z,yaw}]}.

} // namespace PhotoMode

// ============================================================================
//  NOTAS DE INTEGRAÇÃO (pra quem colar isto no menu nativo / dllmain.cpp)
//
//  1) ONDE CHAMAR Tick(): no handler do Present hook (já existe: vtable[8]), 1x por frame,
//     APÓS o jogo ter rodado o update do ator naquele frame. Aí a override vence a corrida
//     → move/rotate/mirar LISOS (sem o jitter do lab PowerShell, que escreve fora do frame).
//
//  2) INPUT: ligar os eventos do menu nativo (contrato §4) nas ações:
//       SELECT_PREV/NEXT -> SelectDelta(-1/+1)
//       MOVE_AXIS(dx,dz) -> MoveSelected(dx*step,0,dz*step)   ; MOVE_Y(dy) idem
//       ROTATE(dyaw)     -> RotateSelected(dyaw*yawStep)
//       CAM_PAN(...)     -> PanCamera(...)
//       RESET -> ResetAll() ; EXIT -> Exit() ; SNAPSHOT -> Snapshot()
//     `step`/`yawStep` por frame; Shift = 5x (igual ao protótipo PS).
//
//  3) FREEZE (tecla P no lab): NÃO use NtSuspendProcess in-process (a DLL roda na própria
//     thread do jogo — suspender a si mesmo trava). In-DLL, "freeze" = (a) achar/togglar o
//     flag de pause do jogo (RE separada), ou (b) simplesmente segurar as overrides (os atores
//     editados já ficam "congelados" no lugar pelo Tick). Comece por (b).
//
//  4) LEITURA SEGURA: trocar RU8/RU16/RU32/RF de Enter() pelos helpers guardados do dllmain
//     (AuroraReadU8/16/32/Float + AuroraPtrOk) p/ não dar AV se a tabela estiver num estado ruim
//     (fora de batalha, troca de cena). As escritas em `inst` já validados são seguras.
//
//  5) CÂMERA: PanCamera move só o ALVO (ref) — funciona liso. Free-fly do EYE/FOV/roll precisa
//     override da MATRIZ FINAL (o D3D sniff que o dllmain já tem: g_auroraSniffW2SMatrix),
//     não desta struct (a câmera de batalha é nó de animação Phyre — ver doc §2e). Fica fase 2.
//
//  6) COLISÃO: integrar isto = editar dllmain.cpp. Avisar no SESSION_HANDOFF; a outra lane
//     não mexe junto; nunca reescrever o arquivo inteiro (só a própria seção). Regra LANES.
// ============================================================================
