# Jarvis-HOOK — MOD-002: IDs novos em `a_ability.bin`, sem efeito vanilla

**Estado em 27/09/2026:** os dados foram aplicados localmente; o Hook **ainda não executa** os 13 efeitos e seu menu de remapeamento de IDs **ainda não existe**. Este é um resultado RT0 de arquivos/parse, não RT2 de jogo. Os IDs e as regras de gameplay estão em [PARTE 2 MOD 002.md](<../mod-ideas/PARTE 2 MOD 002.md>); Vampirism agora cura **2% do dano total causado por ação**, sem exigir morte.

## Localização e identidade

| Conjunto | Caminho/repo/branch no levantamento | Arquivos alterados |
|---|---|---:|
| Hooks | `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`, branch `codex/mod-002-parts-20260927` | Apenas documentos, manifesto e ferramenta de authoring. Nenhum runtime hook. |
| Steam | `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/` | 10 idiomas × `a_ability.bin` e 10 × `arms_rate.bin`. |
| FFX Extracted | `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/` | `new_uspc/.../a_ability.bin` e `jppc/.../arms_rate.bin`. |
| Spira Reforge | `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/`; Editor checkout branch `codexclaudiocodeffxeditor`, HEAD `39916423` antes desta edição | `jppc` e `new_uspc` de `a_ability.bin`; `jppc` de `arms_rate.bin`. As 5 alterações binárias do Editor ficaram **locais, não commitadas**. |
| Referência Fahrenheit | `/home/wanderson/Documents/external-compare/fahrenheit`, branch `main`, commit `c149c847b3a24a66114956f87f1b008599736f75` | Leitura do formato, nenhum arquivo alterado. |

O executável de referência da pesquisa anterior é `FFX.exe` PE32/i386, ImageBase `0x400000`, SHA-256 `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`. No snapshot IDA anterior, `FFX_Btl_AggregateActorEquipAbilities` flat `0x79C610`/RVA `0x39C610` lê WORDs de habilidade, quatro por arma/armadura, e consulta a entrada de tabela pelo ID baixo (`id & 0xFFF`). **Não houve nova sessão IDA ou execução do jogo nesta tarefa**; endereço/ABI permanecem restritos a esse PE e exigem verificação na implementação do Hook.

## O que foi gravado

- `a_ability.bin`: cabeçalho de `0x14` B, linha de `0x6C` B, pool de texto com offsets `u16` relativos ao início do pool. Todos os arquivos passaram a declarar IDs `0..147` (148 linhas; `0x3E70` B de linhas). IDs **135–147** são os 13 atributos do MOD-002, com palavras de equipamento `0x8087..0x8093` propostas. **Todos os bytes `+0x10..+0x6B` dessas linhas são zero**: não se marcou status, elemento, bônus de atributo, flag, ícone ou grupo do jogo. Só offsets/chaves de texto identificam as linhas. Um novo ID sozinho não possui efeito.
- O Steam/Spira já tinham ID **134** com conteúdo próprio, preservado byte a byte. O FFX Extracted terminava em 133; recebeu **ID 134 neutro** com rótulo numérico antes dos novos IDs, sem copiar um efeito de outro mod.
- Idiomas `inpc`, `new_uspc`, `new_depc`, `new_frpc`, `new_itpc`, `new_sppc` têm nomes ingleses provisórios, como `Energy Boost` e `Vampirism`; a descrição de Vampirism diz “Heals 2% of damage dealt. Hook required.”. `jppc`, `new_jppc`, `new_chpc`, `new_krpc` usam **apenas os números 135–147** como rótulos temporários no binário, pois a codificação de letras não é a mesma. O futuro menu do Hook deve usar os nomes do manifesto; localização de textos fica para a lane do Editor.
- `arms_rate.bin`: linhas de `u32` de 4 B. As tabelas relacionadas também passaram a `0..147` (`0x250` B de preços), com preço **zero** nos novos IDs. Todos os preços anteriores foram preservados. Isto evita deixar um ID novo sem linha correspondente quando o jogo consultar o valor de equipamento; comportamento econômico no jogo permanece não testado.
- Nenhuma receita `kaizou.bin` ou equipamento existente recebeu esses IDs. O [arquivo de inventário original](../../research/mod_002_autoabilities/source_inventory.json) fixa SHA-256/tamanho/caminho de cada um dos 25 arquivos. A [ferramenta](../../research/mod_002_autoabilities/author_hook_only_abilities.py) preserva todas as linhas e bytes de texto anteriores, recusa fontes divergentes, faz backup antes de escrever e oferece rollback por hash.

## Verificações e limite

1. Planejamento de escrita em memória de **25 arquivos**: 13 tabelas de habilidades e 12 tabelas de preços, sem tocar nos originais.
2. Cada uma das **13 combinações candidatas** foi lida pelo FFX Editor `--autoability-rt0`, com `Read → WriteAbilities` byte-idêntico: **13/13 PASS**. O executável de teste veio do checkout limpo `/home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor`, commit destacado `e5f05554426f27a83d4ee70f7be32695431ac66b`, usando `DOTNET_ROLL_FORWARD=Major`; o checkout Editor alvo de dados acima não foi compilado para esta checagem.
3. Aplicação local com backup externo em `/home/wanderson/.codex/backups/mod002-autoabilities-20260927T162559Z/manifest.json`. `author_hook_only_abilities.py --verify` recompôs os resultados a partir dos backups e conferiu os SHA-256 instalados: **25/25 verificados**.
4. Nova leitura dos **13 arquivos instalados** pelo FFX Editor: **13/13 PASS**. Steam `new_uspc` mostra `Hero’s Bravery` #135, `Vampirism` #139 e `Energy Barrier` #147; os idiomas asiáticos mostram os números. Em todos, os 13 novos payloads de efeito são zero.

O comando de verificação é:

```sh
python3 research/mod_002_autoabilities/author_hook_only_abilities.py \
  --verify /home/wanderson/.codex/backups/mod002-autoabilities-20260927T162559Z/manifest.json
```

Rollback local, se necessário, sem sobrescrever alterações posteriores não previstas:

```sh
python3 research/mod_002_autoabilities/author_hook_only_abilities.py \
  --rollback /home/wanderson/.codex/backups/mod002-autoabilities-20260927T162559Z/manifest.json
```

**Limites:** não se confirmou que o jogo carrega/exibe IDs 135–147 nem que `arms_rate.bin` zero produza o valor desejado; isto exige RT2 separado. O Steam/Spira podem ser sobrescritos por atualização/reinstalação. Os arquivos binários do jogo e os backups não pertencem ao commit do Hooks. Não usar os novos atributos como se seus efeitos estivessem ativos.

## Contrato da futura implementação do Hook

1. Centralizar `efeito → ID efetivo` usando as chaves estáveis e defaults 135–147 de [`default_ids.json`](../../research/mod_002_autoabilities/default_ids.json). O menu novo do Hook deve permitir alterar **cada ID independentemente** quando outro mod usar os defaults, sem duplicatas. Validar existência da linha no `a_ability.bin` realmente carregado, colisões com IDs vanilla/outro efeito e identidade da configuração antes de habilitar. Trocar o ID no menu **não edita automaticamente o binário**; se o destino não tiver a linha desejada, mostrar erro e deixar o efeito OFF.
2. Ler as habilidades equipadas nos quatro WORDs nativos e, se MOD-005 estiver ativo, na quinta posição lógica; não considerar atributo apenas por existir na tabela. Despachar o efeito conforme o ID configurado, com perfil/assinatura do PE, prevenção de recursão, stacking e teardown. O Hook deve aplicar **100% da lógica nova**; as linhas adicionadas ao binário são neutras.
   - O protótipo atual de Equipment Workshop em `research/equipment_workshop/src/workshop.cpp:177` só admite refino para os IDs vanilla 98–121, Auto-Shell #84 e Auto-Protect #85; a quinta posição também recusa IDs não suportados. Os novos 135–147 **ainda não podem ser refinados nem colocados ali com efeito** sem ampliar a política e seus testes. A ampliação deve consultar a chave/ID configurado, não pressupor que os defaults nunca mudam.
3. Para Vampirism, somar o dano final de HP atribuído ao usuário nos hits/alvos de cada ação ofensiva e curá-lo em 2% do total. Antes de codar, fixar arredondamento, overkill, dano refletido/aliado, múltiplos ataques e Zombie; a regra de “ao matar, 25% do HP máximo” foi substituída.
4. Entregar handoff separado ao Editor com `[DERIVADO DE MOD-002]`: IDs efetivos, formato de linha/texto/preço, opção de UI ligada ao mod, localização dos quatro idiomas numéricos e pontos de authoring. Nenhum menu de remapeamento ou patch do Editor foi implementado aqui.
