# FfxDinput8Probe — in-process main-thread probe for FFX HD (PC)

> **Status:** lab / PROVADO na tela (2026-06-03). É a **chave-mestra** do Tier 2 mecânico: executa
> READ / WRITE / CALL de funções da engine **na thread principal do jogo**, todo frame, **sem
> `CreateRemoteThread`** (que corrompe estado global e crasha). Ver
> `docs/reverse/FFX_DINPUT8_INPROCESS_PROBE_ACTION_PLAN_2026-06-03.md` e o history doc da sessão.

## O seam (por que é honesto)
O FFX importa `DirectInput8Create` de `dinput8.dll` (o **FFX Module Loader**, que carrega `modules\*.dll`).
O jogo poll-a o teclado **todo frame, na thread principal**, via `IDirectInputDevice8::GetDeviceState`
(vtable[9], provado no IDA — `FFX_Input_CreateKeyboardDevice`). O módulo:
1. é carregado pelo loader (exporta `FF10HgetName`/`FF10HgetVer`; trabalho no `DllMain`);
2. espera o `IDirectInput8*` aparecer (`g_FFX_Input_DirectInput8`, RVA `0x8C9CD4`);
3. cria um device keyboard descartável só pra pegar a **vtable compartilhada** e dá patch em
   `GetDeviceState` (race-free) → soltando o device; o device do jogo (mesma vtable) fica hookado;
4. dentro do hook (main thread, por frame) roda comandos do **Command Block** (memory-mapped file
   `Local\FFXProbeBlock_v1`), com SEH protegendo contra crash. **STANDBY por padrão** (só executa
   quando o editor/ctl incrementa `seq`).

## Arquivos
- `ffx-probe.c` / `ffx_probe_block.h` — o módulo (x86, CRT estático) + layout do Command Block.
- `ctl/` — `ffxprobectl.exe` (console externo) que lê heartbeat e arma comandos.
- `build.ps1` — builda (e com `-Deploy` copia pro `modules\` do jogo, com o jogo fechado).

## Build & deploy
```powershell
.\build.ps1            # module + ctl
# feche o FFX, depois:
.\build.ps1 -Deploy    # copia ffx-probe.dll -> <Steam>\...\modules\
# abra o FFX; o modulo anexa no startup (DirectInput init)
```

## Uso (ctl)
```
ffxprobectl mon                          # heartbeat/hooked (prova de vida main-thread)
ffxprobectl read  <rvaHex> <len>         # READ [base+rva .. +len)
ffxprobectl write <rvaHex> <hexBytes>    # WRITE bytes em base+rva
ffxprobectl call  <rvaHex> <abi> a0 a1 a2  # CALL fn (abi: 1=I 2=II 3=III 4=IIF __cdecl, 5=II __stdcall)
ffxprobectl soundcmd <cmd> <param0> [param1]  # lab: dispatcher de som com ECX correto
ffxprobectl forcebattle <field> <group> <formation>  # atomico: flags scriptadas + MsBattleEncountExe
ffxprobectl arena-flags [--all]            # read-only: captures, unlocks e Dark Aeon/Penance flags
```
RVA = VA(IDA) − 0x400000. O módulo expõe `moduleBase` no bloco; o ctl soma `base+rva` (ASLR-safe).

### Arena+ pre-RT2 helper

`ffxprobectl arena-flags [--all]` gera um snapshot read-only dos blocos que a pesquisa da Monster Arena
isolou para validação ingame:

- `base+0xD30C9C` — contadores de captura da Arena, 104 bytes.
- `base+0xD30D04` — flags de monstros especiais/conquest liberados, 35 bytes.
- `base+0xD2E384` — flags de Dark Aeons/Penance derrotados, 9 bytes (save file `0x1934`).

Uso esperado: rodar antes/depois em save descartável, por exemplo antes de derrotar um Dark Aeon, depois da
luta, e novamente ao falar com o NPC da Arena. O comando não escreve nada.

### MusicHook Phase 1 helper

Para acionar a rota de troca de musica na main thread, use:

```
ffxprobectl soundcmd 23 <track> 0
```

`cmd=23` passa por `0x707F50 -> 0x706AF0 -> FFX_FmodMusic_SwitchTrackCrossfade`.
Nao substitua por `call 2FA580 ...` cru: esse wrapper depende do contexto/ECX
correto, que o opcode `soundcmd` prepara a partir do sound-command manager vivo.

## Provado (FFX HD, 2026-06-03)
- **Heartbeat** subindo na main thread (hooked=1).
- **READ** main-thread bate byte-a-byte com `ReadProcessMemory` externo.
- **CALL** de função da engine (`FFX_Battle_EncounterRng`) com ABI `__cdecl`, retorno real, zero crash.
- **FORCE BATTLE**: `MsBattleEncountExe(field,group,formation)` na main thread → **batalha na tela**
  (Sinspawn Geneaux invocado), `ret=-1`, BattleMode 0→0x14, **sem crash**. `Repeat Encounter` deixou
  de depender de `CreateRemoteThread`.

## Alvos (RVA, FFX.exe, base 0x400000)
| RVA | Nome | O que |
|---|---|---|
| `0x380DE0` | `MsBattleEncountExe` | `int __cdecl(int field,int group,float walkedDelta)` |
| `0x3908F0` | `FFX_Kernel_GetImportantEntryById` | `char* __cdecl(int16 id, DWORD* out)` (CALL pura) |
| `0x398900` | `FFX_Battle_EncounterRng` | `int __cdecl(int)` |
| `0x8C9CD4` | `g_FFX_Input_DirectInput8` | `IDirectInput8*` (poll do seam) |
| `0x8C21C8` | `g_FFX_Encounter_RateMode` | 0=off 1=normal 2=tenfold |
| `0x8C21BC` | `g_FFX_Encounter_SystemEnabled` | enable |
| `0xD2CA20`/`0xD2CA24`/`0xD2CA28` | scripted-encounter flags / formation | (limpos pelo jogo todo frame → setar atômico) |

## Segurança / guardrails
- **STANDBY default** · arm explícito (seq) · **SEH** em todo comando (não crasha o jogo num cmd ruim).
- Use **save descartável** pra qualquer WRITE/CALL que afete gameplay; não salve por cima.
- **NUNCA** `CreateRemoteThread`. Promoção a feature pública só após gates de RT2 + safety
  (ver `docs/reverse/WRITER_PROMOTION_GATES.md`).
