# Ronso Mana RT2 — hudSafe=17 (P0 `792AB0`)

**Deploy:** 2026-06-16 · **Lane:** Jarvis-MAGIC · **Modo:** **apply** (`kimahri_ronso_mana_apply.flag` ON)

## Pré-requisitos (já feitos pelo script)

- [x] `ffx-hooks.dll` Release PolyHook em `modules\`
- [x] `modules\config\kimahri_ronso_mana.flag`
- [x] `modules\config\kimahri_ronso_mana_apply.flag` ← **apply, não log-only**
- [x] `%TEMP%\ffx-hooks.log` limpo (sessão nova)

## No jogo

1. Save com **Kimahri** no party, `overdrive_max_charge=255`, pool **parcial** (ex. 120–200).
2. Entrar batalha → turno Kimahri → abrir **anel de comando do meio**.
3. Procurar linha **Overdrive** no anel (objetivo P0).
4. Testar **←** submenu OD com carga parcial (P1 — pode ainda falhar).

## Log — o que deve aparecer (`%TEMP%\ffx-hooks.log`)

```
RonsoMana install ... hudSafe=17
RonsoMana install result ok=1 ... logOnly=0
RonsoMana P0-dispatch detour ok target=0x...
RonsoMana P0 dispatch #1 kim=... charge=... df7=... 590=0x... 6C8=... earlyFlg=...
RonsoMana G0 ring #... kind=1
RonsoMana G0 ring #... kind=12
```

Se `df7=1` antes e depois `forced 7ACEC0(1+12)` → bug stuck confirmado + workaround ativo.

## PASS / FAIL

| Critério | PASS | FAIL |
|----------|------|------|
| Install | `ok=1`, `logOnly=0` | `ok=0` ou crash no boot |
| HUD party | Barras OD normais (Tidus etc.) | Barras pretas / erradas |
| Meio | Overdrive visível com pool parcial | Só Attack/Skill, sem OD |
| Drain | Rage consome pool parcial | — |

## Flags que **não** misturar nesta sessão

Desligue (renomeie/remova em `modules\config\`) se existirem:

- `nova_super_damage*.flag` — lane separada
- `nul_ward*.flag` — lane separada
- `ability_sfx.flag` — lane separada

## Rollback

```
modules\ffx-hooks.dll.backup-ronso-deploy-*
```

Remover `kimahri_ronso_mana_apply.flag` → volta log-only. Remover ambas flags Ronso → Ronso off.
