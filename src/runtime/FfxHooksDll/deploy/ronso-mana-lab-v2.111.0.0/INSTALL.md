# Ronso Mana LAB — pacote deploy `hudSafe=17` (P0 `792AB0`)

**Lane:** Jarvis-MAGIC · **Build:** PolyHook Release · **Data:** 2026-06-16

## Conteúdo

| Arquivo | Uso |
|---------|-----|
| `ffx-hooks.dll` | DLL principal (RonsoMana + outros hooks wired) |
| `config/kimahri_ronso_mana.flag` | Arma Ronso (obrigatório) |
| `config/kimahri_ronso_mana_apply.flag` | **Apply** — G0/P0/drain ativos (sem isto = log-only) |
| `RT2_HUDSAFE17.md` | Checklist RT2 desta sessão |

## Instalação via script (recomendado)

```powershell
cd RuntimeTools\FfxHooksDll
.\build_hooks.ps1 -WithPolyHook -Release
cd deploy\ronso-mana-lab-v2.111.0.0
.\install_to_modules.ps1 -EnableApply
```

## RT2 hudSafe=17

Ver **`RT2_HUDSAFE17.md`**. Log: `%TEMP%\ffx-hooks.log` — procurar `hudSafe=17`, `P0 dispatch`, `G0 ring kind=12`.

## Instalação manual

1. Feche o FFX.
2. Copie `bin\Release\ffx-hooks.dll` → `<FFX>\modules\ffx-hooks.dll`
3. Copie flags para `<FFX>\modules\config\`
4. **Apply:** copie também `kimahri_ronso_mana_apply.flag`
5. Inicie o jogo; confira `%TEMP%\ffx-hooks.log`:
   - `RonsoMana install result ok=1` e `logOnly=0` (com apply)
   - `RonsoMana P0 dispatch` + `G0 ring ... kind=12` ao abrir menu Kimahri

## Flags opcionais (env)

| Variável | Efeito |
|----------|--------|
| `FFXHOOKS_ENABLE_RONSO_MANA=1` | equiv. `kimahri_ronso_mana.flag` |
| `FFXHOOKS_RONSO_MANA_APPLY=1` | equiv. apply flag |
| `FFXHOOKS_RONSO_DRAIN_COST=40` | custo do drain parcial (default 40) |
| `FFXHOOKS_RONSO_GATE_MIN=100` | mínimo de carga para OD/Ronso (default 100) |

## Desinstalar / rollback

Backup automático: `modules\ffx-hooks.dll.backup-ronso-deploy-<timestamp>`
