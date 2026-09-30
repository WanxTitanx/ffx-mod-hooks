# Ronso Mana RT2 — hudSafe=19 (camada B UI tree)

**Deploy:** 2026-06-15 · **Lane:** Jarvis-MAGIC · **Modo:** **apply**

**DLL:** `sha256-prefix: 2336220E23615AE9`

## Pré-requisitos

- [x] `ffx-hooks.dll` Release PolyHook em `modules\`
- [x] `kimahri_ronso_mana.flag` + `kimahri_ronso_mana_apply.flag`
- [ ] **Desligar** `nul_ward*.flag` (sessão limpa)
- [ ] `%TEMP%\ffx-hooks.log` limpo

## No jogo

1. Save Kimahri no party, `overdrive_max_charge=255`, pool parcial (ex. 100–200).
2. Batalha → turno Kimahri → anel de comando do **meio**.
3. Procurar **Overdrive** no anel.
4. Testar **←** submenu OD com carga parcial.

## Log esperado

```
RonsoMana install ... hudSafe=19
RonsoMana B0-push detour ok ...
RonsoMana B0-resolve detour ok ...
RonsoMana B0 push #... treeId=43 ringKind=1
RonsoMana B0 push #... treeId=43 ringKind=12
RonsoMana B0 resolve #... treeId=43 ->0 (ou >=0)
RonsoMana B0* inject #... main=0 od=0 blob=0x........ reason=G0-finalize
RonsoMana G0 ring #... kind=1 result=...
RonsoMana G0 ring #... kind=12 result=...
```

**Diagnóstico:**

| Log | Significado |
|-----|-------------|
| Zero linhas `B0 push` | `7ACEC0` nunca chegou na árvore UI — gates internos |
| `B0 push` mas `resolve ->-1` | Blob case-4 (`blob=0x0`) ou stack vazia |
| `inject od=-1` | Injeção direta falhou — próximo passo: patch display blob |
| `inject od>=0` mas sem OD na tela | Bloqueio em `86E970` / greyout ratio |

## PASS / FAIL

| Critério | PASS | FAIL |
|----------|------|------|
| Install | `ok=1`, `hudSafe=19`, `logOnly=0` | crash boot |
| HUD | Barras OD normais | barras pretas |
| Meio | Overdrive visível pool parcial | sem OD |
| ← OD | Submenu com skills parciais | vazio / crash |
