# NativeMenuShell — casca de menu NATIVO in-game (degrau 5, REFERÊNCIA)

Lane: **Jarvis-BAHAMUT** (menu in-game nativo do FFX HD). **Não entra em build. Não toca `dllmain.cpp`.**

## O que é
`NativeMenuShell.h` é o **esqueleto paste-ready** do menu NATIVO de N linhas com **texto NOSSO**
("MODO FOTO", "Invocar 8 monstros"...), desenhado pelas **primitivas do próprio FFX** (fonte/cursor/
janela/input/som nativos) — **sem overlay, sem Present hook**. É o degrau 5 da lane: o único pedaço
que o probe não faz sozinho, porque rows de texto arbitrário exigem um **draw callback nosso** (in-DLL).

## Como funciona (tudo provado por RE + ao vivo)
- **Hand-roll** de um objeto de menu de 152B no pool do jogo: `Alloc()` (já faz Reset) → setar campos +
  callbacks → `RegisterAndEnter()`. O pump do jogo chama nosso `+16` (draw) e o `+12` (input GENÉRICO
  `0x8B4460`) por frame.
- **Navegação + leitura de seleção**: de graça pelo input genérico `0x8B4460` — a seleção vive em `+72`
  (cursor) / `+44` (confirmada) / `+40` (state). **Já provado ao vivo** (helper `ffxprobectl list-read`:
  o `SELECTED` rastreou o cursor no menu Customize, 35→42).
- **Texto NOSSO**: encoder da fonte do FFX (degrau 2) → bytes em memória da DLL → `0x9016B0` desenha.
- **Ações**: cada linha liga numa capacidade de RAM da **Aurora** via um `PhotoModeBridge` desacoplado
  (zero dependência de compilação) — fonte: `../BattlePhotoMode/PhotoModeActions.h` + o contrato
  `docs/ai/HANDOFF_AURORA_PHOTOMODE_CONTRACT_2026-06-09.md`.

## ABIs/offsets confirmados (IDA double-verify, 2026-06-09)
| Papel | VA (IDA) | Assinatura |
|---|---|---|
| Alloc objeto | `0x8AA150` | `int __cdecl()` (sem args; já faz Reset; 0=pool cheio) |
| Reset | `0x8AA460` | `int __cdecl(int obj)` (+55=1, +62=1, +63=1 default) |
| RegisterAndEnter | `0x8AAAB0` | `int __cdecl(int obj)` (+64=1, +40=0) |
| Input genérico (nav+seleção) | `0x8B4460` | `int __cdecl(int obj)` |
| Draw janela | `0x8F5F70` | `void __cdecl(float left, float top, float w, float h, int style=10)` |
| Draw string | `0x9016B0` | `int __cdecl(int ctx=0, byte* ffxText, float x, float y, char flags=0, float sx=0.78, float sy=1.0)` |
| Draw cursor | `0x8C0640` | `int __cdecl(float x, float y, int kind=0)` |
| Scale X / Y | `0x644990` / `0x6449D0` | `float __cdecl(float)` (1920→512 / 1080→416) |

Layer: `+62`=2 (draw), `+63`=1 (update) — **escritas byte-precisas** (nunca DWORD: clobaria `+64` active).
Coords: espaço virtual **1920×1080** → `SX/SY` → físico **512×416** (como a engine faz).

## Próximo (degrau 5, coordenado com Aurora)
1. Wire por **hotkey** primeiro (NPC depois = degrau 6).
2. `SetBridge({onEdge,onHeldEnter})` apontando pras ações do `PhotoModeActions.h`.
3. Sub-modo **HELD** (mover/paneia): por frame lê eixos do pad + `PhotoMode::Tick()` no tick (fica LISO).
4. Editar `dllmain.cpp` **só avisando no SESSION_HANDOFF** (regra LANES). Save descartável no 1º RT2.

Docs de RE: `docs/reverse/FFX_NATIVE_MENU_LIST_ROW_SOURCE_2026-06-09.md` e
`docs/reverse/FFX_NATIVE_MENU_TICK_AND_ABI_2026-06-09.md`.
