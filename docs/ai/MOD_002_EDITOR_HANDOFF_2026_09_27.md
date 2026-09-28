# [DERIVADO DE MOD-002] Handoff para a lane do FFX Editor

## Identidade e locais

- **Hooks/documentação:** `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`, branch `codex/mod-002-parts-20260927`; [regras e IDs](<../mod-ideas/PARTE 2 MOD 002.md>), [manifesto de chaves estáveis](../../research/mod_002_autoabilities/default_ids.json), [relatório RT0](../research/MOD_002_HOOK_ONLY_AUTOABILITIES_2026-09-27.md).
- **Editor:** `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor`, HEAD `39916423` antes desta mudança local. Fonte relevante: `FFXProjectEditor/FfxLib/Ability/AutoAbility_File.cs`, `AutoAbilityHardcodedFlagCatalog.cs`, `Tools/AutoAbilityGrowRt0.cs` e `FfxLib/Encoding/FfxEncoding.us.cs`/`.jp.cs`.
- **Steam:** `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/`.
- **FFX Extracted:** `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/`.
- **Spira Reforge:** `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/`.
- **Referência de localização futura:** `/home/wanderson/Documents/external-compare/repos/Karifean_FFXDataParser`, branch `master`, commit `6e86fe1` no levantamento; o [README original](https://github.com/Karifean/FFXDataParser) documenta `autoAbilities.csv` para editar nomes/descrições por idioma. Fonte consultiva, código não copiado.

## O que foi feito na lane do Hooks

1. Reservados **IDs 135–147** para 13 auto-habilidades da Parte 2. Vampirism #139 agora representa cura de **2% do dano total causado em cada ação**, não cura por morte. `Energy Boost` #136 é o nome escolhido para o bônus elemental. P-Trade/M-Trade usam IDs separados.
2. Aplicadas localmente linhas com **payload vanilla de efeito zerado** (`+0x10..+0x6B`) em 13 `a_ability.bin`: Steam 10 idiomas, Extracted `new_uspc`, Spira `jppc/new_uspc`. Ampliadas 12 tabelas `arms_rate.bin`, com preços zero para os novos IDs. O ID 134 do Steam/Spira foi preservado; Extracted ganhou ponte 134 neutra.
3. Inglês provisório nos 6 idiomas de alfabeto latino; rótulos numéricos temporários em JP/CH/KR, aprovados pelo usuário. Arquivos binários não foram commitados na branch do Hooks.
4. Backup/rollback fora do Git: `/home/wanderson/.codex/backups/mod002-autoabilities-20260927T162559Z/manifest.json`. Hashes 25/25 verificados e parse/readback pelo Editor 13/13; **sem RT2, sem Hook de efeito e sem menu de remapeamento implementado**.

Os cinco arquivos rastreados do checkout Editor alterados localmente são:

- `docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/new_uspc/battle/kernel/a_ability.bin`
- `docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/jppc/battle/kernel/arms_rate.bin`
- `mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/jppc/battle/kernel/a_ability.bin`
- `mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/new_uspc/battle/kernel/a_ability.bin`
- `mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/jppc/battle/kernel/arms_rate.bin`

Não faça `reset`/`clean` desses arquivos sem consultar o manifesto de backup e o usuário. Eles não devem ser incluídos num commit comum de fonte sem decisão de empacotamento/proveniência de assets.

## Trabalho exato para o Editor

- Expor essas linhas na área de auto-habilidades com tag visível **`[DERIVADO DE MOD-002]`**, deixando explícito que o jogo vanilla **não executa** seus efeitos. A opção deve ser tratada como authoring de um mod instalado, não como nova habilidade nativa.
- Adicionar authoring/validação para nomes, descrições e preço das 13 linhas por idioma, preservando linhas e pool de texto anteriores. Substituir os rótulos numéricos JP/CH/KR por textos corretos somente quando a codificação/fontes e a tradução estiverem comprovadas. Não gravar letras inglesas com o mapa US nos arquivos asiáticos.
- Manter o manifesto `effect_key → default_id` como contrato de integração. O **menu de remapeamento é responsabilidade do Hook**: ele poderá selecionar outro ID existente para cada efeito, validar colisão/duplicata e mostrar o ID efetivo. Editor não deve presumir que o default continua fixo nem renomear uma linha de outro mod silenciosamente.
- Se o Editor permitir colocar uma dessas habilidades em equipamento, advertir que o efeito só existe com o Hook correspondente ativo. Validar leitura/escrita de `a_ability.bin` e `arms_rate.bin` em todas as formas de arquivo usadas pelo mod; não usar o writer de save de equipamento até seu defeito de offset +1 ser corrigido em trabalho separado.
- Entregar RT0 de round-trip, preservação de bytes antigos, nomes por idioma e falha segura para ID ausente/colidido. RT2 do jogo só com autorização específica; nenhum resultado RT0 deve ser chamado de efeito funcionando.

**Resumo copiável para a lane:** “Implementar no FFX Editor opções `[DERIVADO DE MOD-002]` para authoring/localização dos IDs hook-only 135–147, com 13 nomes no manifesto do Hooks e preço zero em `arms_rate.bin`. Cinco binários do Editor já foram editados localmente e têm backup externo; não os resetar nem commitá-los inadvertidamente. O Hook futuro fará 100% dos efeitos e terá menu próprio para remapear IDs. Validar fontes/round-trip por idioma; não prometer gameplay antes de RT2.”
