# FFX Battle Photo Mode (lab interativo, RAM-first)

O **"modo a puta que pariu"**: congela/segura a cena de batalha do FFX e deixa tu **navegar
e editar ao vivo pela tecla** — selecionar boneco, mover, rotacionar, subir/descer, paneiar
a câmera, e dar **snapshot** da cena. Tudo escrevendo direto na RAM (sem DLL).

É também o **protótipo do futuro BOTÃO IN-GAME**: a lógica daqui porta pro menu próprio da
`ffx-hooks.dll` (GDI→textura D3D→quad no Present hook) — aí vira "aperta tecla no jogo, sem
PowerShell, sem janela externa".

## Rodar

```powershell
.\RuntimeTools\BattlePhotoMode\BattlePhotoMode.ps1
# opcoes: -MoveStep 0.6 -CamStep 0.6 -YawStep 0.04 -SpeedMult 5 -PollMs 16 -NoRestore
```
Precisa estar **numa batalha** (atores deployados). Sai com **Esc** (restaura tudo por padrão).

## Keymap

| Tecla | Ação |
|---|---|
| `[` / `]` | seleciona boneco anterior / próximo |
| Setas | move o boneco selecionado no plano (Esq/Dir = X, Cima/Baixo = Z) |
| `PgUp` / `PgDn` | sobe / desce o boneco (Y) |
| `,` / `.` | rotaciona o boneco (yaw) — *experimental* |
| `W A S D` | paneia o ALVO da câmera (ref) no plano |
| `R` / `F` | sobe / desce o alvo da câmera (Y) |
| `Q` / `E` | orbita heading − / + — *experimental* |
| `Z` / `X` | elevação da câmera − / + — *experimental* |
| `Shift` (segurar) | 5× velocidade |
| `P` | congela / descongela o jogo (freeze real) |
| `Enter` | **snapshot** da cena → `work/actor_ram/photo/scene_*.json` |
| `Backspace` | reset (restaura tudo ao original) |
| `Esc` | sair (restaura e descongela) |

## O que funciona vs experimental (honesto)

- ✅ **Mover boneco (X/Z/Y)** e **paneiar câmera (ref)** — PROVADO (move na tela).
- ⚠️ **Tremor no lab externo:** com o jogo rodando, a engine reescreve posição/câmera por-frame → o
  boneco/câmera "treme" entre o valor novo e o da engine. O loop escreve em ~60Hz pra dominar, mas não
  é 100% liso fora do processo. **Porte in-DLL:** chamando o tick depois do update do ator no frame, a
  override vence a corrida e mover/rotacionar/mirar fica liso. Free-cam completa ainda exige a câmera
  final/matriz D3D.
- ⚠️ **Rotação / heading / elevação** — experimental. Rotação reescreve o 3×3 da world matrix
  (preservando escala); heading/elev escrevem floats que a engine trata como readout (podem não pegar).
- 🧊 **Freeze (P):** suspende o processo de verdade — para tudo. Útil pra "fotografar", mas a tela
  não re-renderiza enquanto suspenso (mudanças aparecem ao descongelar). Pra editar VENDO ao vivo,
  use sem freeze (com o tremor).

## Portável entre runs
Resolve a base do módulo + lê o ponteiro da tabela de atores ao vivo a cada execução (nada hardcoded).
Fecha e reabre o FFX à vontade — só rodar de novo. A câmera ref é global estático (RVA `0xD378A0`,
estável a menos do rebase de ASLR, que o tool resolve sozinho).

## Roadmap → BOTÃO IN-GAME (visão do Halyson)

Set de features alvo pra versão in-game (no menu da `ffx-hooks`):
1. **Toggle Photo Mode** (uma tecla no jogo) — entra/sai, com HUD na tela (textura D3D já existe).
2. **Seleção de ator** com destaque visual (label/seta no selecionado).
3. **Mover / rotacionar** boneco com setas/analógico + gizmo simples.
4. **Free-cam** de verdade (eye+target+roll+FOV) — exige vencer/achar o controlador de câmera.
5. **Fixar liso no jogo** pelo tick da DLL depois do update do ator → "corrige cagada do Aurora in-game".
6. **Salvar cena** → exporta `scene.json` (atores+câmera) compatível com o import do Aurora (offline+online).
7. **Presets/undo**, velocidade, snap-to-grid.

Pré-requisitos de RE pra ficar 100%: (a) harness de input/tick no menu da DLL,
(b) controlador de câmera/matriz D3D para free-cam completa. Mover/pan pelo tick ja e o caminho liso.
Por ora o PowerShell entrega o laboratório jogável + o blueprint.
