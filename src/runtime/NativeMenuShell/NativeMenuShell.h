// ============================================================================
//  NativeMenuShell.h  —  REFERENCE SKELETON (degrau 5), NOT wired into any build
// ----------------------------------------------------------------------------
//  Jarvis-BAHAMUT — lane do menu in-game NATIVO do FFX HD (FFX.exe x86).
//
//  O QUE É: a "casca" de um menu NATIVO de N linhas com TEXTO NOSSO ("MODO FOTO",
//  "Invocar 8 monstros", ...), desenhada pelas PRÓPRIAS primitivas do FFX (fonte/
//  cursor/janela/input/som nativos) — SEM overlay, SEM Present hook. É o degrau 5
//  da lane: o único pedaço que o probe não faz sozinho (rows de texto arbitrário
//  exigem um DRAW CALLBACK nosso = código in-DLL).
//
//  COMO FUNCIONA (provado): hand-roll de um objeto de menu de 152B no pool do jogo:
//    obj = Alloc()            -> acha slot livre e JÁ faz Reset (zera + defaults)
//    set fields + callbacks   -> +12 = input GENÉRICO do FFX (0x8B4460, navega+lê
//                                seleção de graça); +16 = NOSSO draw (janela+rows+
//                                cursor); +28 = NOSSO validador de confirm
//    RegisterAndEnter(obj)    -> marca ativo (+64=1), reseta state (+40=0)
//  O pump do jogo (tick nativo) chama +12 (input) e +16 (draw) por frame. A seleção
//  vive em +72 (cursor) / +44 (escolha confirmada) / +40 (state) — e JÁ provamos ao
//  vivo que dá pra LER isso (helper `ffxprobectl list-read`, SELECTED rastreou o
//  cursor no Customize). Aqui a gente também ESCREVE o draw, fechando o ciclo.
//
//  ⚠️ STATUS: REFERÊNCIA. Header standalone (nada o #inclui ainda) — não entra em
//  build e NÃO toca `dllmain.cpp`. A fusão real em `dllmain.cpp` é COORDENADA com a
//  lane Aurora (regra LANES: avisar no SESSION_HANDOFF antes de editar). As ações de
//  cada linha são as capacidades de RAM da Aurora (contrato
//  docs/ai/HANDOFF_AURORA_PHOTOMODE_CONTRACT_2026-06-09.md + RuntimeTools/
//  BattlePhotoMode/PhotoModeActions.h) — aqui só ligamos os eventos do menu nelas
//  via um PhotoModeBridge desacoplado (zero dependência de compilação).
//
//  EVIDÊNCIA (ABIs/offsets confirmados, IDA double-verify 2026-06-09):
//    docs/reverse/FFX_NATIVE_MENU_LIST_ROW_SOURCE_2026-06-09.md  (row source + receita §5)
//    docs/reverse/FFX_NATIVE_MENU_TICK_AND_ABI_2026-06-09.md     (tick, pool, lifecycle, fonte)
//  Imagebase IDA = 0x400000; em runtime resolve base+(VA-0x400000) via GetModuleHandle(NULL).
//
//  ⚠️ PRÉ-CONDIÇÃO RT2: só desenha com o subsistema de menu vivo
//  (g_FFX_MenuSubsystemActive @ VA 0x13407E4) — i.e. o jogo DENTRO de um menu/field.
//  Tudo roda na MAIN THREAD (RegisterAndEnter dispara +8 síncrono; o pump é main-thread).
// ============================================================================
#pragma once
#include <stdio.h>  // _snprintf_s (self-contained header; fix 2026-08-02)
#if defined(_WIN64) || defined(__x86_64__)
#  error "NativeMenuShell targets 32-bit FFX.exe (x86) ONLY — cdecl float-on-stack ABI + 4-byte pointers."
#endif
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NativeMenu {

// ---------------------------------------------------------------------------
// 0) Resolução de endereço (ASLR): runtime = base + (VA_IDA - IMAGE_BASE)
// ---------------------------------------------------------------------------
static const uintptr_t kImageBase = 0x00400000u;
static_assert(sizeof(void*) == 4, "NativeMenuShell is x86-only (FFX.exe is 32-bit; pointers carried in int).");

static inline uintptr_t FfxBase() {
#ifdef _WIN32
    return (uintptr_t)GetModuleHandleA(NULL);   // FFX.exe é o módulo principal
#else
    return 0;
#endif
}
// casta um VA (como aparece no IDA) p/ ponteiro de função do tipo T no processo vivo
#define FFX_FN(va, T) ((T)(NativeMenu::FfxBase() + (uintptr_t)(va) - NativeMenu::kImageBase))

// ---------------------------------------------------------------------------
// 1) ABIs nativas confirmadas (todas __cdecl) — VAs IDA (FFX.exe @0x400000)
// ---------------------------------------------------------------------------
// Lifecycle do objeto de menu (152 bytes = 0x98) — pool @0x18408C0, stride 152, máx 32.
typedef int   (__cdecl* Fn_Alloc)(void);          // 0x8AA150: acha slot livre, JÁ faz Reset, retorna obj (0 = pool cheio)
typedef int   (__cdecl* Fn_Reset)(int obj);       // 0x8AA460: zera + defaults (+52=0x01000000 -> +55=1; +62 dword=0x101 -> +62=1,+63=1)
typedef int   (__cdecl* Fn_Register)(int obj);    // 0x8AAAB0: +64=1 (ativo), +40=0 (state), chama *(obj+8) se !=0
// Input GENÉRICO de lista (navegação + seleção + scroll de graça; sem globais).
typedef int   (__cdecl* Fn_ListInput)(int obj);   // 0x8B4460: state machine sobre +40/+48/+50/+52/+58/+66/+69/+70/+72; +28 = validador de confirm
// Primitivos de desenho — coords em ESPAÇO FÍSICO 512x416 (use Scale* p/ vir de 1920x1080).
typedef void  (__cdecl* Fn_DrawWindow)(float left, float top, float w, float h, int style); // 0x8F5F70 (style 10 = moldura padrão)
typedef int   (__cdecl* Fn_DrawString)(int ctx, const unsigned char* ffxText, float x, float y,
                                       char flags, float scaleX, float scaleY);             // 0x9016B0 (ctx=0, texto = bytes FFX, cor fixa 128)
typedef int   (__cdecl* Fn_DrawCursor)(float x, float y, int kind);                         // 0x8C0640 (kind 0 = cursor de menu)
// Quad de TEXTURA do atlas do menu. selector escolhe o atlas (RE 0x903EE0/0x8AC870, Jarvis-VALEFOR):
//   0xFFFFFFFE=ffx_bg(2048x1024) · 0xFFFFFFFA=summonbg · 0xFFFFFFFC=stonetexture(256) · 0xFFFFFFFF=battle_kuang · 600..649=icon · 200..398=meswin · 400..598=battle.
//   x/y/w/h em coords FISICAS (use SX/SY); u0/v0/u1/v1 em TEXELS; c0/c1 cor RGBA (0x80=normal). NAO ha caminho de handle cru (so esses atlases).
typedef void  (__cdecl* Fn_DrawTexQuad)(unsigned int sel, float x, float y, float w, float h,
                                        float u0, float v0, float u1, float v1, unsigned int c0, unsigned int c1); // 0x903EE0
typedef float (__cdecl* Fn_ScaleX)(float v1920);  // 0x644990: v*512/1920 (eixo X / largura)
typedef float (__cdecl* Fn_ScaleY)(float v1080);  // 0x6449D0: v*416/1080 (eixo Y / altura)

static inline int   Alloc()             { return FFX_FN(0x8AA150, Fn_Alloc)(); }
static inline int   Reset(int o)        { return FFX_FN(0x8AA460, Fn_Reset)(o); }
static inline int   Register(int o)     { return FFX_FN(0x8AAAB0, Fn_Register)(o); }
static inline float SX(float v)         { return FFX_FN(0x644990, Fn_ScaleX)(v); }
static inline float SY(float v)         { return FFX_FN(0x6449D0, Fn_ScaleY)(v); }

/* Menu2D canvas: engine maps design 1920×1080 -> physical buffer via ScaleX/ScaleY (IDA: fixed
 * 512×416 today; if the game ever changes scalers, MenuPhys* tracks it). Layout uses normalized
 * fractions [0,1] — NOT monitor resolution (2K/720p is handled by the engine upscaler). */
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
// PRIMITIVAS CRUAS de cor (IDA-provado 2026-06-10, lane IFRIT). Assinatura IDENTICA p/ as duas; cores ARGB
// 0xAARRGGBB (alpha=byte alto, 0x80=normal); gradiente cor0(topo)->cor1(base). EU controlo a cor, ZERO icone.
//   0x8F4B20 FFX_Menu2D_DrawSolidRect  -> EmitQuad modo 0 (rect solido chapado)
//   0x8F4DF0 FFX_Menu2D_DrawPlasma     -> EmitQuad modo 2 (plasma/brilho ANIMADO — o "eletrico" das barras de nome)
typedef void  (__cdecl* Fn_DrawColorQuad)(float x, float y, float w, float h, unsigned int c0, unsigned int c1);
// A engine emite o vertex-color em ordem de componente que a GPU le como R,G,B,A a partir de byte0,1,2,3. Nos recebemos
// no padrao ARGB 0xAARRGGBB (paleta/intuicao), entao SWAPAMOS R<->B aqui (mantem A e G). BUG 2026-06-10: sem isso o
// vermelho virava azul, ciano virava dourado, etc. (SIN escapava por ser ~simetrico em R/B).
static inline unsigned int Argb2Abgr(unsigned int c) { return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16); }
static inline void  DrawSolidRect(float x, float y, float w, float h, unsigned int c0, unsigned int c1) { FFX_FN(0x8F4B20, Fn_DrawColorQuad)(x, y, w, h, Argb2Abgr(c0), Argb2Abgr(c1)); }
static inline void  DrawPlasma   (float x, float y, float w, float h, unsigned int c0, unsigned int c1) { FFX_FN(0x8F4DF0, Fn_DrawColorQuad)(x, y, w, h, Argb2Abgr(c0), Argb2Abgr(c1)); }
// Reduz o alpha de uma cor ARGB (p/ gradiente topo->base): FadeAlpha(c, 1, 2) = metade do alpha.
static inline unsigned int FadeAlpha(unsigned int argb, unsigned int num, unsigned int den) {
    unsigned int a = ((argb >> 24) & 0xFFu) * num / den;
    return (argb & 0x00FFFFFFu) | (a << 24);
}

// ---- CAMINHO DE HANDLE CRU (RE Jarvis-IFRIT 2026-06-10): desenhar QUALQUER textura do menu por atlasId ----
// FFX_Menu2D_TexHandleByAtlasId 0x8AC870 mapeia atlasId -> handle (chave de textura). FFX_Menu2D_EmitQuad_Accumulate
// 0x63F090 emite o quad com esse handle (2o arg). FFX_Menu2D_ClipQuadToScissor 0x8E5A20 clipa. Compondo os 3 dá pra
// desenhar atlas que NAO estao no switch do selector — ex.: 12032 = help/now_help (a arte do brasao do Jecht da aba
// HELP!), 1257216.. = help/mon_boku (bestiario), 16001 = texture, 11980 = strtex, 11948 = worldmap.
// IMPORTANTE: a textura precisa estar RESIDENTE em VRAM. Se nao, o handle vira chave sem GPU-tex -> desenha nada
// (sem crash). UVs NORMALIZADAS 0..1. Cores ARGB c0(topo)/c1(base) modulam a textura (0xFF=cheio, 0x80=~meio).
typedef char* (__cdecl* Fn_TexHandleByAtlasId)(int atlasId);                                           // 0x8AC870
typedef void  (__cdecl* Fn_EmitQuadAccum)(int quad, char* handle, int one, int mode, float zero);     // 0x63F090
typedef int   (__cdecl* Fn_ClipQuad)(float*, float*, float*, float*, float*, float*, float*, float*); // 0x8E5A20
static inline char* TexHandleByAtlasId(int atlasId) { return FFX_FN(0x8AC870, Fn_TexHandleByAtlasId)(atlasId); }
static inline void DrawTexByAtlasId(int atlasId, float x, float y, float w, float h,
                                    float u0, float v0, float u1, float v1, unsigned int c0, unsigned int c1) {
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h, uu0 = u0, vv0 = v0, uu1 = u1, vv1 = v1;
    c0 = Argb2Abgr(c0); c1 = Argb2Abgr(c1);                                                   // ARGB -> ordem da engine (R<->B)
    if (!FFX_FN(0x8E5A20, Fn_ClipQuad)(&x0, &y0, &x1, &y1, &uu0, &vv0, &uu1, &vv1)) return;   // off-screen -> skip
    int q[38];
    for (int i = 0; i < 38; ++i) q[i] = 0;                          // zera scratch (engine deixa lixo; zerar e mais seguro)
    *(float*)&q[0] = x0;  *(float*)&q[1] = y0;  *(float*)&q[2] = uu0; *(float*)&q[3] = vv0;
    q[4] = (int)(c0 & 0xFF); q[5] = (int)((c0 >> 8) & 0xFF); q[6] = (int)((c0 >> 16) & 0xFF); q[7] = (int)((c0 >> 24) & 0xFF);
    *(float*)&q[8] = x1;  *(float*)&q[9] = y1;  *(float*)&q[10] = uu1; *(float*)&q[11] = vv1;
    q[12] = (int)(c1 & 0xFF); q[13] = (int)((c1 >> 8) & 0xFF); q[14] = (int)((c1 >> 16) & 0xFF); q[15] = (int)((c1 >> 24) & 0xFF);
    FFX_FN(0x63F090, Fn_EmitQuadAccum)((int)q, TexHandleByAtlasId(atlasId), 1, 0, 0.0f);    // mode 0 = quad de TEXTURA
}

static const uintptr_t VA_ListInput = 0x8B4460;   // ponteiro p/ +12 (input genérico)

// ---------------------------------------------------------------------------
// 2) Offsets do objeto de menu (152B) — confirmados (ver TICK_AND_ABI + verify 2026-06-09)
// ---------------------------------------------------------------------------
enum Off {
    O_ENTER     = 8,    // dword: callback de entrada (chamado por Register se !=0). Use 0.
    O_UPDATE    = 12,   // dword: callback de input/tick  -> Fn_ListInput (0x8B4460)
    O_DRAW      = 16,   // dword: callback de desenho     -> OurDraw
    O_AUX       = 20,   // dword: callback aux/visibilidade (close-path). Use OurAux (retorna 1).
    O_VALIDATOR = 28,   // dword: validador de confirm: cdecl(obj, selRow=+72, confirmSlot=+69)->bool (0 rejeita a linha)
    O_STATE     = 40,   // dword: state machine (0 init; 2 idle/nav; 15/16 confirm-done; 17/18 cancel-close)
    O_CHOICE    = 44,   // word:  linha CONFIRMADA (escrita pelo input no confirm; -1 = nenhuma ainda)
    O_COUNT     = 48,   // word:  total de rows
    O_TOP       = 50,   // word:  primeira row visível (scroll)
    O_CANCEL    = 55,   // byte:  nao-fecha-no-ultimo (=1; Reset já deixa 1 via dword +52=0x01000000).
    O_PAGE      = 58,   // word:  rows visíveis (page size)
    O_GROUP62   = 62,   // byte:  LAYER DE DRAW (filtro do draw tick). Use 2.
    O_GROUP63   = 63,   // byte:  LAYER DE UPDATE (filtro do update tick). Reset deixa 1. Mantém 1.
    O_ACTIVE    = 64,   // byte:  in-use flag (NÃO escrever; Register seta 1; Alloc varre isto)
    O_SLOTS     = 66,   // byte:  nº de confirmações antes de fechar (=1)
    O_SELECTED  = 72,   // word:  LINHA SOB O CURSOR (live). <- isto rastreia a navegação (provado ao vivo)
};
static inline uint8_t*  Pb(int o, int f){ return (uint8_t*)((uintptr_t)o + f); }
static inline int16_t   RdW(int o, int f){ return *(int16_t*)Pb(o, f); }
static inline int32_t   RdD(int o, int f){ return *(int32_t*)Pb(o, f); }
static inline void      WrW(int o, int f, int16_t v){ *(int16_t*)Pb(o, f) = v; }
static inline void      WrB(int o, int f, uint8_t v){ *(uint8_t*)Pb(o, f) = v; }
static inline void      WrP(int o, int f, void* v){ *(uintptr_t*)Pb(o, f) = (uintptr_t)v; }

// ---------------------------------------------------------------------------
// 3) Encoder da fonte do FFX (degrau 2 — tabela 100% crackeada).
//    byte = 0x30 + índice em FFX_ATLAS (dígitos primeiro). Char desconhecido -> espaço.
//    0x9016B0 espera bytes assim, null-terminated. Guardamos os labels em memória
//    NOSSA (DLL) — não precisa do scratch do jogo (mesmo processo).
// ---------------------------------------------------------------------------
static const char* const FFX_ATLAS =
    "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";

static inline void EncodeLabel(const char* ascii, unsigned char* out, int cap) {
    int n = 0;
    for (const char* c = ascii; *c && n < cap - 1; ++c) {
        int gi = -1;
        for (int i = 0; FFX_ATLAS[i]; ++i) { if (FFX_ATLAS[i] == *c) { gi = i; break; } }
        if (gi < 0) gi = 10;                 // desconhecido -> espaço (índice 10 = 0x3A)
        out[n++] = (unsigned char)(0x30 + gi);
    }
    if (cap > 0) out[n] = 0;                 // null-terminate (FFX para no 0x00)
}

// ---------------------------------------------------------------------------
// 4) Modelo das rows + bridge p/ as ações da Aurora (desacoplado, zero dep)
// ---------------------------------------------------------------------------
// Itens do MENU IN-LIVE (lane IFRIT). Por ora = SO os NOMES; as acoes ainda NAO estao fiadas (bridge inerte) —
// ver docs/ai/IFRIT_F7_INLIVE_EDITOR_ROADMAP_2026-06-10.md. O membro ACT_EXIT e lido pelo dllmain p/ FECHAR o
// menu (NAO remover/renomear). O nome do tipo `ActionId` tambem e usado pelo dllmain (manter).
enum ActionId {
    ACT_DIFFICULTY = 0,      // stepper (futuro) — needs-RT2 (stat-block nao escrito-provado)
    ACT_CTB,                 // medidor de turno — light-RE
    ACT_FORCE_BATTLE,        // CALL 0x380DE0 MsBattleEncountExe — instant
    ACT_AI_SWAP,             // opcode byte-local @ +0xF78 — RT2-provado (down-payment do SIN)
    ACT_BATTLE_CHEATS,       // god/no-MP (flags nativas) — instant
    ACT_OVERDRIVE,           // encher/destravar — light-RE
    ACT_MUSIC,               // CALL 0x7097E0 PlayTrack — instant
    ACT_ARENA,               // unlock de monstros — light-RE
    ACT_EQUIPMENT,           // auto-ability — light-RE
    ACT_STATUS,              // inflict/cleanse — light-RE
    ACT_FORMATION,           // quem e frontline — light-RE
    ACT_GIL_ITEMS,           // save-data — persist nao-provado
    ACT_PLAYER_BOOST,        // needs-RT2 (escrita no player nunca provada)
    ACT_CAMERA,              // PARADO (AURORA encerrado) + re-poke por-frame
    ACT_SIN,                 // S.I.N. = Spira Instinct Network — BLOCKED (preview/research)
    ACT_EXIT,                // FECHA o menu (lido pelo dllmain)
    ACT__COUNT
};
enum ActKind { EDGE, HELD };
// KEYSTONE (FASE A do plano F7, 2026-08-02): o tipo da row — NONE = acao simples;
// TOGGLE/STEPPER = valor editavel com o direcional esquerda/direita (0x8000/0x2000).
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
    const char* labelAscii;   // texto humano; será FFX-encoded em SpawnMenu
    ActionId    action;
    ActKind     kind;
    unsigned int barTop;      // lane IFRIT: cor ARGB 0xAARRGGBB do TOPO da barra (DrawSolidRect/Plasma c0) — controlada por nos, ZERO icone
    unsigned int barBottom;   // cor ARGB da BASE da barra (c1) -> gradiente vertical topo->base
    int          barPlasma;   // 0 = solido; 1 = PLASMA (DrawPlasma, brilho animado) — usado no SIN
    RowType     rowType;      // KEYSTONE: RT_NONE/RT_TOGGLE/RT_STEPPER
    int         minVal;       // KEYSTONE: limite inferior (STEPPER)
    int         maxVal;       // KEYSTONE: limite superior (STEPPER)
    int         stepVal;      // KEYSTONE: passo do STEPPER
};

// O MENU IN-LIVE (lane IFRIT). Por enquanto = SO OS NOMES (acoes nao fiadas); ordem ~ ranking do roadmap.
// Todas EDGE (nenhuma HELD por ora). 8 rows com kVisiblePage=8 (cabem na pagina sem scroll).
// Campos 4/5/6 = COR ARGB 0xAARRGGBB do TOPO + da BASE (gradiente, DrawSolidRect, sem icone) + plasma(0/1).
// PALETA "Spira/Yevon" da Jarvis-SAFADA (docs/ai/NATIVE_MENU_PALETTE_SPIRA_YEVON_2026-06-10.md): 9 familias coesas;
// SIN estoura em 0xE6 plasma (vitrine demoniaca); Music/teal = o mais leve. Tunar = trocar os 2 hex (a paleta entrega).
static Row g_rows[] = {
    // COESAO: TODAS as barras = a MESMA ardosia escura (cor = excecao, NAO regra). A diferenca entre opcoes vem do
    // TEXTO (e icone, depois), nao da cor. So o SIN tem cor (a unica coisa viva). Selecao = o unico outro evento de cor.
    // KEYSTONE (2026-08-02): rowType RT_STEPPER/RT_TOGGLE habilita o direcional L/R (0x8000/0x2000) p/ editar o valor.
    { "Difficulty",                      ACT_DIFFICULTY,    EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_STEPPER, 0, 5, 1 },
    { "Force Battle",                    ACT_FORCE_BATTLE,  EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
    { "Monster AI Swap",                 ACT_AI_SWAP,       EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_STEPPER, 0, 255, 1 },
    { "Music",                           ACT_MUSIC,         EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_STEPPER, 0, 255, 1 },
    { "Party Invincible (debug)",        ACT_BATTLE_CHEATS, EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_TOGGLE,  0, 1, 1 },
    { "Arena+",                          ACT_ARENA,         EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
    { "S.I.N. - Spira Instinct Network", ACT_SIN,           EDGE, 0xE6B33CFFu, 0xE63A0A6Eu, 1, RT_NONE, 0, 0, 1 },  // A UNICA com cor (plasma violeta)
    { "Exit",                            ACT_EXIT,          EDGE, kMenuRowGlassTop, kMenuRowGlassBot, 0, RT_NONE,    0, 0, 1 },
};
static const int kRowCount = (int)(sizeof(g_rows) / sizeof(g_rows[0]));
// KEYSTONE: os valores atuais por row (editados com o direcional L/R — 0x8000/0x2000).
static int g_rowValue[kRowCount] = {};
// FIX 2026-08-02 (RT2): o stepper NAO mostra o valor antes de editar (o "[0]" cru assustava).
static bool g_rowEdited[kRowCount] = {};
static const int kVisiblePage = 8;          // quantas linhas visíveis (<= kRowCount)
static const int kLabelCap = 64;            // bytes/label (FFX-encoded)

static unsigned char g_labelBytes[ (sizeof(g_rows)/sizeof(g_rows[0])) ][kLabelCap]; // labels já encodados (preenchidos em SpawnMenu)

// Bridge: o integrador (na fusão com a Aurora) preenche estes ponteiros com as
// funções de RuntimeTools/BattlePhotoMode/PhotoModeActions.h. O menu NÃO depende
// da Aurora em tempo de compilação — só chama o que estiver setado.
struct PhotoModeBridge {
    void (*onEdge)(ActionId a);             // ações E: executa 1x (toggle/freeze/snapshot/reset/exit/select)
    void (*onHeldEnter)(ActionId a);        // ações H: entra no sub-modo "segurar" (move/rotate/pan/angle)
};
static PhotoModeBridge g_bridge = { 0, 0 };
static inline void SetBridge(const PhotoModeBridge& b) { g_bridge = b; }

// ---------------------------------------------------------------------------
// 5) NOSSO draw callback (+16): janela + rows (texto NOSSO) + cursor.
//    Chamado por frame pelo draw tick como cdecl(int obj). Coords em 1920x1080
//    virtual -> SX/SY -> 512x416 (igual a engine faz). Números são COSMÉTICOS (tunar ao vivo).
// ---------------------------------------------------------------------------
// --- DEMO DO AUGE (lane IFRIT): entrada animada + cursor com easing + Gil VIVO + retrato + icones ---
static volatile int g_menuAnimStart = 0;     // frame (g_ourDrawCalls) em que o menu abriu (set em SpawnMenu) -> entrada animada
static float        g_easedRowY     = -1.0f; // Y virtual do highlight; faz easing ate a linha selecionada

// UV NORMALIZADAS dos icones por opcao (atlas icon 15808, grade ~16x3) — RE workflow f7-demo-ingredients (estimado).
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

// Formata um uint decimal nos bytes da fonte do FFX (reusa o atlas via EncodeLabel).
static inline void EncodeUInt(unsigned int v, unsigned char* out, int cap) {
    char rev[16]; int n = 0;
    if (v == 0) rev[n++] = '0';
    while (v > 0 && n < 15) { rev[n++] = (char)('0' + (v % 10)); v /= 10; }
    char asc[16]; int m = 0;
    while (n > 0 && m < 15) asc[m++] = rev[--n];
    asc[m] = 0;
    EncodeLabel(asc, out, cap);
}

// Oscilacao suave 0..1..0 com periodo em frames (triangle + smoothstep) — "respirar"/pulsar sem strobe, sem math.h.
static inline float Osc01(int frame, int periodFrames) {
    if (periodFrames <= 0) return 0.0f;
    int ph = frame % periodFrames; if (ph < 0) ph += periodFrames;
    float t = (float)ph / (float)periodFrames;
    float tri = (t < 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f);   // 0..1..0
    return tri * tri * (3.0f - 2.0f * tri);                      // smoothstep
}
// Interpola 2 cores ARGB por s (0..1), componente a componente.
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

// Backdrop: scrim + worldmap (glass sobre a cena; opacidade moderada).
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

static volatile int g_ourDrawCalls = 0;   // DIAGNOSTICO: conta chamadas de OurDraw (sobe = pump DESENHA no field)
static int __cdecl OurDraw(int obj) {
    ++g_ourDrawCalls;

    const int F = g_ourDrawCalls;

    // ===== D) FUNDO — mapa de Spira (scrim + worldmap moderado; evita estouro do overlay branco v2.165) =====
    DrawMenuBackdrop();
    DrawMenuNeonFrame(F);

    // ===== B/C) Header + footer (glass panels, EN — i18n later) =====
    {
        static unsigned char s_title[64], s_sub[64], s_foot[64];
        static bool s_enc = false;
        if (!s_enc) {
            EncodeLabel("FFX Editor - In-Live", s_title, 64);
            EncodeLabel("Live RAM editing - persist to disk when ready", s_sub, 64);
            EncodeLabel("Arrows Navigate   Confirm Open   Cancel Back   F7 Exit", s_foot, 64);
            s_enc = true;
        }
        const float hx = NX(0.042f), hy = NY(0.044f), hw = NW(0.917f), hh = NH(0.139f);
        DrawMenuGlassPanel(hx, hy, hw, hh, F, 0);
        DrawString(s_title, NX(0.073f), NY(0.080f));
        DrawString(s_sub,   NX(0.073f), NY(0.132f));
        const float fx = NX(0.042f), fy = NY(0.887f), fw = NW(0.917f), fh = NH(0.072f);
        DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
        DrawString(s_foot,  NX(0.073f), NY(0.911f));
    }

    // ===== H) LAYOUT da lista — frações do canvas (responsivo ao buffer Menu2D) =====
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

    // ===== G) SELECAO: Y com EASING (slide ~180ms ate a linha focada) =====
    const float selVisY = vTop + (float)(sel - top) * vStep;
    if (g_easedRowY < 0.0f) g_easedRowY = selVisY;
    g_easedRowY += (selVisY - g_easedRowY) * 0.30f;

    for (int r = 0; r < page; ++r) {
        const int row = top + r;
        if (row >= count || row >= kRowCount) break;   // clamp a extensao do array
        // ENTRADA (cascata aprovada pelo Halyson): desliza +240px da direita + fade, stagger, easeOut ~12 frames.
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
        // KEYSTONE: o valor da row (TOGGLE/STEPPER) na coluna direita.
        // FIX 2026-08-02: o STEPPER so mostra o valor DEPOIS de editado (L/R) — sem o "[0]" inicial.
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

    // ===== SELECAO (no Y EASED): LIFT azul-aco sutil + fio verde neon + cursor FFX =====
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

// NOSSO aux/close callback (+20): trivialmente "ok/visível". Espelha o 0x8E36B0 do popup.
// ⚠️ DEVE retornar !=0 — o close tick (0x8A91E0) só finaliza/libera o slot se +20 for nulo OU retornar !=0.
//    Retornar 0 trava o menu (re-entra o close todo frame consumindo input). 0 = vetar fechar.
static int __cdecl OurAux(int obj) { (void)obj; return 1; }

// NOSSO validador de confirm (+28): cdecl(obj, selRow /*=+72*/, confirmSlot /*=+69, ~0*/) -> bool. Aceita qualquer row.
// (P/ vetar uma linha específica, ramifique no 2º arg = selRow, NÃO no 3º; aqui tudo confirma.)
static int __cdecl OurValidate(int obj, int selRow, int confirmSlot) { (void)obj; (void)selRow; (void)confirmSlot; return 1; }

// ---------------------------------------------------------------------------
// 5.5) MODAL input ownership — enquanto NOSSO menu está aberto, congela o INPUT
//      dos OUTROS objetos de menu do pool movendo o +63 (grupo de INPUT) deles p/
//      FORA do range pumpado (0..8). O tick de input (sub_8A91E0) filtra +63==layer
//      em 0..8 → PULA eles (param de navegar/confirmar). O draw (sub_8A9640, filtra
//      +62) continua → ficam VISÍVEIS mas congelados. Só o nosso recebe o pad → mata
//      o "double-action". (RE: workflow re-menu-input-ownership, alta confiança:
//      dword_23CC120 NÃO é gate de input e é perigoso; este é o lever limpo.)
//      Chamar FreezeOthersExcept(ourObj) ANTES do trampoline do pump e RestoreOthers()
//      DEPOIS, todo frame em que o menu está aberto.
// ---------------------------------------------------------------------------
static const uintptr_t POOL_VA = 0x18408C0;        // g_FFX_MenuObjPool (array in-place 32 x 152B)
static const int POOL_STRIDE = 152, POOL_MAX = 32;
static int     g_frozenObj[POOL_MAX];
static uint8_t g_frozenG63[POOL_MAX];
static int     g_frozenN = 0;

static inline void FreezeOthersExcept(int ourObj) {
    g_frozenN = 0;
    for (int i = 0; i < POOL_MAX; ++i) {
        int obj = (int)(FfxBase() + (POOL_VA - kImageBase) + (uintptr_t)(POOL_STRIDE * i));
        if (*(uint8_t*)((uintptr_t)obj + O_ACTIVE) == 0) continue;   // slot inativo
        if (obj == ourObj) continue;                                  // NUNCA o nosso (senão nos trancamos)
        uint8_t* g = (uint8_t*)((uintptr_t)obj + O_GROUP63);
        if (*g > 8) continue;                                          // já fora do range pumpado
        if (g_frozenN < POOL_MAX) { g_frozenObj[g_frozenN] = obj; g_frozenG63[g_frozenN] = *g; ++g_frozenN; }
        *g = 0xFF;                                                     // move o INPUT p/ layer não-pumpada (draw +62 segue)
    }
}
static inline void RestoreOthers() {
    for (int i = 0; i < g_frozenN; ++i) {
        uint8_t* g = (uint8_t*)((uintptr_t)g_frozenObj[i] + O_GROUP63);
        if (*g == 0xFF) *g = g_frozenG63[i];                          // só restaura o que ainda for o nosso 0xFF
    }
    g_frozenN = 0;
}

// ---------------------------------------------------------------------------
// 5.6) INPUT cb PROPRIO (+12) + MODAL via unk_23CC120 -- jeito SEGURO (RE 2026-06-09,
//      docs/reverse/FFX_MENU_INPUT_READERS_CUSTOM_CB_ABI_2026-06-09.md). Em vez do input
//      generico 0x8B4460 (que escreve +69 e faz a FSM do menu de campo sub_8B1580 AVANCAR
//      no confirm = CRASH), usamos NOSSO input: navega lendo os mesmos readers de pad,
//      NUNCA escreve +69 nem mexe no state +40. Setamos unk_23CC120=ourObj -> a FSM
//      sub_8B1580 fica PARADA (le [obj+0x45]==0) -> zero double-action, zero confirm-leak.
//      Confirm/cancel sinalizados por vars da DLL (g_ourClosed/g_ourResult).
//      (Substitui o pool-freeze de 5.5, que FALHOU: o pause menu nao mora no pool.)
// ---------------------------------------------------------------------------
static const uintptr_t VA_CurrentPopup = 0x23CC120;   // unk_23CC120: gate modal que a FSM sub_8B1580 observa
static const uintptr_t VA_PadReadDir   = 0x8BE440;    // ReaderB: edge+repeat (up 0x1000, down 0x4000, page 0x1/0x2)
static const uintptr_t VA_PadReadEdge  = 0x8BE480;    // ReaderC: edge single-press (confirm 0x20, cancel 0x40)
static const uintptr_t VA_MenuPlaySfx  = 0x886B00;    // FFX_Menu_PlaySfx(id): 1=move/confirm, 4=cancel
typedef int (__cdecl* Fn_PadRead)(void);
typedef int (__cdecl* Fn_PlaySfx)(int id);
static inline int  PadDir()  { return FFX_FN(VA_PadReadDir,  Fn_PadRead)() & 0xFFFF; }
static inline int  PadEdge() { return FFX_FN(VA_PadReadEdge, Fn_PadRead)() & 0xFFFF; }
static inline void PlaySfx(int id) { FFX_FN(VA_MenuPlaySfx, Fn_PlaySfx)(id); }

static const uintptr_t VA_PadGlobals = 0x25D09D2;   // bloco de estado do pad (held/edge/repeat/stick + fallbacks)
// "Engole" o pad: zera o estado lido pelos readers (0x8BE3E0/440/480) APOS a nossa leitura, p/ QUALQUER
// leitor depois (ex.: a FSM do menu de pause) ver "nenhum input" -> nao navega. So roda com o menu aberto.
static inline void SwallowPad() {
    volatile uint8_t* p = (volatile uint8_t*)(FfxBase() + (VA_PadGlobals - kImageBase));
    for (int i = 0; i < 24; ++i) p[i] = 0;          // 0x25D09D2..0x25D09EA (primarios + fallbacks)
}

static volatile int g_ourResult = 0;   // linha confirmada (>=0) / -1 = cancel (set pela cb, lido pelo wire)
static volatile int g_ourClosed = 0;   // 1 = confirm/cancel aconteceu

// NOSSO input cb (+12): navega + confirma/cancela. NUNCA escreve +69 nem +40. cdecl(int obj)->int.
static int __cdecl OurListInputCb(int obj) {
    const int dir  = PadDir();
    const int edge = PadEdge();
    int sel = RdW(obj, O_SELECTED);
    const int count = RdW(obj, O_COUNT);
    int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);
    if (count <= 0) return obj;
    if (dir & 0x1000) {                              // UP — WRAP: do topo sobe p/ a ULTIMA
        sel = (sel > 0) ? (sel - 1) : (count - 1); PlaySfx(1);
    } else if (dir & 0x4000) {                       // DOWN — WRAP: da base desce p/ a PRIMEIRA
        sel = (sel < count - 1) ? (sel + 1) : 0; PlaySfx(1);
    }
    // KEYSTONE: LEFT (0x8000) / RIGHT (0x2000) — edita o valor da row selecionada.
    if (sel >= 0 && sel < kRowCount && g_rows[sel].rowType != RT_NONE) {
        if (dir & 0x8000) {
            g_rowEdited[sel] = true;   // FIX 2026-08-02: valor visivel so apos editar
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
    if (sel < top) top = sel;                        // mantem a selecao visivel (cobre o wrap topo<->base)
    if (sel >= top + page) top = sel - page + 1;
    if (top > count - page) top = count - page;
    if (top < 0) top = 0;
    WrW(obj, O_SELECTED, (int16_t)sel);
    WrW(obj, O_TOP,      (int16_t)top);              // snap (sem easing); NAO escreve +69 nem +40
    if (!g_ourClosed) {
        if (edge & 0x20)      { PlaySfx(1); g_ourResult = sel; g_ourClosed = 1; }   // CONFIRM
        else if (edge & 0x40) { PlaySfx(4); g_ourResult = -1;  g_ourClosed = 1; }   // CANCEL
    }
    return obj;   // (swallow REVERTIDO: zerar o pad travava todo o input do jogo -> soft lock)
}

static inline void ClaimModal(int obj) { *(volatile int32_t*)(FfxBase() + (VA_CurrentPopup - kImageBase)) = obj; }
static inline void ReleaseModal()       { *(volatile int32_t*)(FfxBase() + (VA_CurrentPopup - kImageBase)) = 0; }

// ---------------------------------------------------------------------------
// 6) Spawn / poll / close  (TUDO na MAIN THREAD, com o subsistema de menu vivo)
// ---------------------------------------------------------------------------
struct Menu { int obj; };

// Cria a lista nativa. Retorna obj!=0 em sucesso (0 = pool cheio / contexto inativo).
static inline Menu SpawnMenu() {
    // encoda os labels uma vez (memória nossa)
    for (int i = 0; i < kRowCount; ++i) EncodeLabel(g_rows[i].labelAscii, g_labelBytes[i], kLabelCap);

    int obj = Alloc();                 // já vem Reset()-ado (zerado + defaults: +55=1, +62=1,+63=1)
    if (!obj) return Menu{ 0 };        // pool cheio -> aborta (Alloc retorna 0; SEM null-check no jogo)

    WrW(obj, O_COUNT,    (int16_t)kRowCount);
    WrW(obj, O_PAGE,     (int16_t)kVisiblePage);
    WrW(obj, O_TOP,      0);
    WrW(obj, O_SELECTED, 0);           // começa na 1ª linha (popup usa 1; lista começa em 0)
    WrB(obj, O_SLOTS,    1);           // ⚠️ OBRIGATÓRIO >=1: Reset deixa +66=0; com 0 o loop de confirm
                                       //    (0x8B4460 case15) roda 255x e escreve +44..+44+510 = ESTOURA o objeto!
    WrB(obj, O_CANCEL,   1);           // nao-fecha-no-ultimo (Reset já deixa 1; explícito)
    WrB(obj, O_GROUP62,  2);           // DRAW na layer 2 (pumpada; padrão de popup/lista)  <- BYTE preciso
    WrB(obj, O_GROUP63,  1);           // UPDATE na layer 1 (Reset já deixa 1; explícito)   <- BYTE preciso
    //  ^ NUNCA escreva um DWORD aqui: clobaria +64 (active) / +65.

    WrP(obj, O_ENTER,     (void*)0);                                   // sem enter cb
    WrP(obj, O_UPDATE,    (void*)(uintptr_t)&OurListInputCb);          // NOSSO input (navega; NUNCA escreve +69/+40)
    WrP(obj, O_DRAW,      (void*)(uintptr_t)&OurDraw);                 // NOSSO desenho
    WrP(obj, O_AUX,       (void*)(uintptr_t)&OurAux);                  // aux/close ok (DEVE retornar !=0)
    WrP(obj, O_VALIDATOR, (void*)0);                                   // sem validador (nosso input cuida do confirm)

    g_ourClosed = 0; g_ourResult = 0;
    g_menuAnimStart = g_ourDrawCalls; g_easedRowY = -1.0f;   // DEMO: reinicia entrada animada + easing do cursor
    Register(obj);                     // +64=1 (ativo), +40=0 (state). Sincrono na main thread.
    // (unk_23CC120 NAO usado: nao parou a FSM do pause ao vivo + risco se ele navegar p/ submenu. Usamos SwallowPad.)
    return Menu{ obj };
}

// Estado da leitura por frame (chame DEPOIS do pump, na main thread).
enum PollResult { POLL_NAV, POLL_CONFIRM, POLL_CANCEL };
struct Poll { PollResult what; int row; };

// Lê o que o jogador fez neste frame. row = linha sob o cursor (live) / confirmada.
static inline Poll PollMenu(const Menu& m) {
    const int sel = RdW(m.obj, O_SELECTED);
    if (g_ourClosed) {                                   // nosso input cb sinalizou confirm/cancel
        if (g_ourResult >= 0) return Poll{ POLL_CONFIRM, g_ourResult };
        return Poll{ POLL_CANCEL, sel };
    }
    return Poll{ POLL_NAV, sel };                        // navegando (cursor em `sel`)
}

// Dispatcher: liga a linha confirmada na ação da Aurora (via bridge).
static inline void DispatchConfirm(int row) {
    if (row < 0 || row >= kRowCount) return;
    const Row& R = g_rows[row];
    if (R.kind == EDGE) { if (g_bridge.onEdge)     g_bridge.onEdge(R.action); }
    else                { if (g_bridge.onHeldEnter) g_bridge.onHeldEnter(R.action); }
}

// Fecha o menu: seta +65; no próximo update tick (0x8A91E0) o jogo roda +12 (input) mais uma vez, depois
// +20 (OurAux, !=0), depois +24 ou o finalizer default 0x8AA3A0 -> Reset + limpa active(+64). Slot liberado.
static inline void CloseMenu(Menu& m) {
    if (!m.obj) return;
    WrB(m.obj, 65, 1);                  // +65 = close flag -> finalizer no próximo tick
    m.obj = 0;
    g_ourClosed = 0;                    // limpa o sinal (proximo SpawnMenu tambem zera)
}

// ---------------------------------------------------------------------------
// 7) INTEGRAÇÃO (degrau 5/6 — quando coordenar com a Aurora no dllmain.cpp)
// ---------------------------------------------------------------------------
//  • GATILHO: por hotkey/debug primeiro (degrau 5), NPC/diálogo depois (degrau 6).
//  • Onde chamar SpawnMenu(): na MAIN THREAD, com g_FFX_MenuSubsystemActive
//    (VA 0x13407E4) setado (jogador num menu/field). Fora disso = objeto zumbi.
//  • Loop por frame (no tick do menu ou no seu hook main-thread):
//        Poll p = PollMenu(menu);
//        if (p.what == POLL_CONFIRM) {
//            DispatchConfirm(p.row);
//            // EDGE: ação executou; re-spawne ou mantenha (o input genérico fecha
//            //       no confirm com +66=1 — re-spawne p/ menu persistente).
//            // HELD: entre no sub-modo "segurar": por frame leia os eixos do pad
//            //       (input readers 0x8BE440/0x8BE480) e chame as ações contínuas
//            //       da Aurora (PhotoMode::MoveSelected/PanCamera/...). Saia no cancel.
//        } else if (p.what == POLL_CANCEL) {
//            CloseMenu(menu);
//        }
//  • As ações: preencha g_bridge com SetBridge({onEdge, onHeldEnter}) apontando p/
//    RuntimeTools/BattlePhotoMode/PhotoModeActions.h (namespace PhotoMode da Aurora).
//  • SUAVIDADE de graça: a Aurora provou que escrevendo a override NO TICK depois do
//    update do ator (mesmo frame), o mover/paneia fica LISO (sem a corrida out-of-frame
//    do lab PowerShell). Logo o sub-modo HELD deve aplicar PhotoMode::Tick() no tick.
//  • GUARDRAILS: NÃO tocar camera RAM (0xD378A0 = Aurora) daqui — só chamar o bridge.
//    NÃO editar dllmain.cpp sem avisar no SESSION_HANDOFF (regra LANES).
//    Use save descartável no primeiro RT2. Pool tem 32 slots — 1 menu por vez.
// ---------------------------------------------------------------------------

} // namespace NativeMenu
