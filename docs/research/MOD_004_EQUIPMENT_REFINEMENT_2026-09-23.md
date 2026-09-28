# Jarvis-HOOK — MOD-004: refinamento de equipamento (+10 ou +40)

**Data:** 23/09/2026. **Estado:** desenho e pesquisa RT0; nenhuma lógica foi ligada à DLL, ao jogo ou ao Editor. Este texto desenvolve a sugestão de Dawn Veilwinter de equipamento até +10 no [backlog](../MOD_IDEAS_BACKLOG.md) com as duas variantes explicadas pelo usuário. O refino altera as **auto-habilidades presentes em cada peça**, e não uma estatística fixa do modelo de espada/armadura. Números de custos e bônus são **propostas**, separados dos fatos de formato.

## Resultado técnico

As variantes são plausíveis com **estado extra por peça, hooks de efeitos/inventário/save e uma interface nova ou integração profunda no Customize nativo**. O `kaizou.bin`/FFX Editor isoladamente não guarda níveis individuais. O mesmo ID de auto-habilidade consulta uma entrada global, e cada equipamento no save tem quatro IDs em 22 bytes. Não há RT1/RT2 do refinamento.

| Decisão | Variante A | Variante B |
|---|---|---|
| Rank visível | Uma peça `+0..+10`. | Soma `+0..+40` com quatro habilidades; até `+50` com quinta real. |
| Cada tentativa | Melhora todas as habilidades ocupadas. | Sorteia uma habilidade ocupada ainda abaixo de +10. |
| Custo | Soma receitas específicas de cada habilidade; vários materiais. | Quantidade predeterminada de catalisador do personagem e material-base. |
| Estado mínimo | Um rank por instância de peça. | Um rank por instância de habilidade, mais PRNG/counter. |
| Principal risco | Materiais agregados e substituição de habilidade já refinada. | Sorteio justo, identidade por slot e impossibilidade de cobrar tentativa perdida. |

### Fontes e identidade

- PE PC `FFX.exe`: SHA-256 `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`, PE32/i386, ImageBase `0x400000`. Endereços abaixo são flat IDA; RVA = flat − `0x400000`. Mesmo alvo do [relatório MOD-005](MOD_005_FIFTH_EQUIPMENT_ABILITY_SLOT_2026-09-23.md).
- FFX Editor no commit `e5f05554426f27a83d4ee70f7be32695431ac66b`: `AutoAbility_Dictionary.cs` (131 IDs nomeados 0–130, SHA-256 `bfad6da0...`), `Item_Dictionary.cs` (112 IDs 0–111, `6bb4f04c...`), `Customization_File.cs`, `AutoAbility_File.cs`, `EquipmentStruct.cs`, `FfxSaveEquipment.cs`. Checkout apenas lido.
- `a_ability.bin` extraído, somente leitura, em `docs/history/.../new_uspc/battle/kernel/` do Editor: SHA-256 `d0610e7d37cde6e65298da116f6dc05a69236126d9ff157c2db65d17a97729aa`; header com 134 linhas (0–133) de `0x6C` bytes. IDs 131–133 sem nomes no dicionário examinado; não incluídos nas receitas.
- Formatos cruzados com [Fahrenheit `equip.cs`](https://github.com/fahrenheit-crew/fahrenheit/blob/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx/equip.cs), [`aability.cs`](https://github.com/fahrenheit-crew/fahrenheit/blob/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx/aability.cs) e [`customize.cs`](https://github.com/fahrenheit-crew/fahrenheit/blob/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx/customize.cs), commit `c149c847b3a2`, LGPL-3.0-or-later; leitura comparativa, sem código adaptado.
- [FFX-Info, documentação original do formato de `kaizou.bin`](https://grayfox96.github.io/FFX-Info/tech/data-files/customizations) descreve uma linha de 8 bytes com habilidade, item/quantidade e tipo de equipamento. O writer do Editor conserva linhas de `0x08` e contagem preexistente. **Não foi encontrado `kaizou.bin` real na amostra local**; ingredientes propostos abaixo não são receitas vanilla confirmadas.

| Camada | Evidência | Consequência |
|---|---|---|
| Save/equipamento | 200 peças × 22 B; capacidade em `+0x0B`, quatro `u16` em `+0x0E/+0x10/+0x12/+0x14`; `PlySave` começa imediatamente depois. | Não gravar nível ou quinta habilidade em `record+22`. Sidecar versionado por save e identidade de peça é a rota inicial. |
| `a_ability.bin` | Auto-Protect #85 tem `status_auto_temporal@+0x5A=0x0010`; Auto-Haste #86 = `0x0800`. Strength +10% #100 tem valor `10@+0x55`, flags `0x0400@+0x56`. | Mudar a entrada global afeta todas as peças com aquele ID. Haste/Protect são bits, sem potência por item. Variantes de ID por nível não cobrem automaticamente efeitos especiais. |
| Customize nativo | `FFX_CustomizeMenu_KaizouStateMachine` flat `0x8D5800`, RVA `0x4D5800`, caso `0xD`: decide habilidade em `0x8D5BDF` e chama `FFX_Inventory_AddItem(item_id, -quantidade_u8)` em `0x8D5BF0`. | Fluxo atual confirma uma receita/um item. Ingredientes agregados, ranks e sorteio exigem fluxo/hook novos mesmo usando o menu vanilla. |
| Inventário | `FFX_Inventory_AddItem` flat `0x7905A0`, RVA `0x3905A0`, tem clamps de stack em 99. Bytes do PE fixado nos RVAs `0x39061D` e `0x39064D` começam `6A 63` (`push 99`), conforme `ItemStackCapHook.h`. | Agregar custo por ID e conferir quantidade antes de consumir. O patch opcional de stack 255 não é pré-requisito. |
| Interface por tecla F | `dllmain.cpp` bloqueia F7 quando o menu vanilla está aberto; F8 é Dashboard; F9 é Maechen no estado atual. | Conforme o usuário esclareceu, a rota de tecla F será **um menu totalmente novo**, não item/submenu do F7/F8/F9. Pode compartilhar primitivas de baixo nível após revisão; F10 é só candidato após auditoria de conflito. |
| Efeitos | `FFX_Btl_AggregateActorEquipAbilities` flat `0x79C610`, RVA `0x39C610`, combina quatro habilidades por arma/armadura. | Desenhar `+r` não altera batalha; cada família exige ponto de efeito e prova de não duplicar refresh. |

## Variante A — nível global da peça, +0 a +10

A peça guarda **um rank global** `r∈[0,10]`. Cada avanço `r→r+1` melhora **todas as auto-habilidades ocupadas naquele momento** em um nível. O custo depende dos IDs das habilidades, não do nome, dono ou template da arma. Slots vazios não pagam nem recebem bônus.

**Receita candidata por habilidade:** no passo para o rank `r`, consumir `q(r)=1+⌊(r−1)/3⌋` do item-base: 1 nos ranks 1–3, 2 nos 4–6, 3 nos 7–9, 4 no 10. Nos ranks 4, 7 e 10, somar **um** item de marco daquela habilidade. Agregar quantidades iguais de todos os slots antes do débito. Duplicatas pagam por slot. Em toda a progressão, cada habilidade exige 22 itens-base e 3 de marco; se ambos forem o mesmo ID, quantidades são somadas.

Exemplo: peça com Auto-Haste #86, Auto-Protect #85, HP +10% #115 e Ribbon #128.

| Passo | Recibo candidato |
|---|---|
| +0→+1 | Chocobo Feather ×1, Light Curtain ×1, HP Sphere ×1, Remedy ×1. |
| +3→+4 | Chocobo Feather ×2 + Chocobo Wing ×1; Light Curtain ×3; HP Sphere ×2 + Attribute Sphere ×1; Remedy ×2 + Dark Matter ×1. |
| +9→+10 | Chocobo Feather ×4 + Chocobo Wing ×1; Light Curtain ×5; HP Sphere ×4 + Attribute Sphere ×1; Remedy ×4 + Dark Matter ×1. |

O [probe offline](../../research/mod_004_refinement/probe_refinement_design.py) reproduz esses recibos e valida IDs contra fontes fixadas. São números para ensaio, não balanceamento fechado. O maior custo de um mesmo item num único passo de cinco habilidades idênticas no modelo é 25, menor que o stack vanilla de 99; o custo total é pago em dez passos.

**Mudar habilidade depois de refinar:** inserir/substituir um atributo numa peça +r exige pagar seus custos de ranks `1..r` **antes** de equipá-lo como +r. Auto-Haste inserida em peça +10, por exemplo, exige 22 Chocobo Feathers e 3 Chocobo Wings. Sem pagamento, rejeitar a mudança; nunca dar rank grátis. Remover não devolve itens por padrão. Reforja de dono preserva rank, pois A não tem catalisador de personagem. Drops podem nascer pré-refinados por tier configurado, com registro sidecar na criação e sem cobrar o jogador.

## Variante B — um atributo sorteado por tentativa, até +40/+50

Cada habilidade **ocupada** possui seu `rᵢ∈[0,10]`, vinculado à instância da habilidade na peça. Uma tentativa escolhe uniformemente **um slot elegível** (`rᵢ<10`) e sobe apenas ele em +1. Slot vazio ou já +10 sai do sorteio. Se não há elegíveis, não há pagamento. Quatro habilidades ocupadas precisam de exatamente **40 sucessos** para `+10/+10/+10/+10`, exibidos como `Total +40/40`; três têm teto +30. Se MOD-005 implementar uma quinta, o teto passa a **+50**.

**Custo candidato por sucesso:** 1 catalisador do personagem dono da peça + 1 Ability Sphere vanilla #73, conhecidos **antes** do sorteio e independentes do resultado. +40 consome 40 de cada, ao longo de 40 transações. Seleção entre elegíveis impede desperdiçar recursos em posição já +10. PRNG/counter precisa ser persistido no mod; não reseedar com valor fixo a cada clique. O probe demonstra `(9,10,9,10)→(9,10,10,10)` com semente fixa só para RT0.

A versão temática teria **sete itens novos**, um para Tidus, Yuna, Auron, Kimahri, Wakka, Lulu e Rikku, com obtenção repetível e raridade equivalente. Isso requer crescer `item.bin`, textos, ícones/lojas/drops e verificar save e desinstalação. O fixture real `item.bin` tem 112 linhas de `0x60` B (IDs 0–111, SHA-256 `3c4273252b78fb98d630ce08e7d51eecc252a4e9a2c28dfe671a2ea32744fae2`). O `CommandBinGrowCore` do Editor foi feito para `command.bin` com mínimo de 320 linhas. **Não há prova nesta tarefa de round-trip/carga no jogo de sete novos itens.** O probe usa catalisador virtual e não inventa IDs do jogo.

Um protótipo sem novos IDs poderia reservar itens vanilla **distintos por dono** (exemplos sem balanceamento): Tidus Speed Sphere #72; Yuna White Magic Sphere #78; Auron Strength Sphere #87; Kimahri Ability Sphere #73; Wakka Accuracy Sphere #93; Lulu Black Magic Sphere #79; Rikku Special Sphere #76. Sua disponibilidade é muito desigual; isto é fallback técnico, não economia final. Quando um mod adicionar itens novos ao inventário vanilla, precisará provar o comportamento do save sem o mod ou migrar/remover esses itens. Moeda virtual em sidecar evita IDs novos, mas exige menu/loot inteiramente próprios.

Após reforjar a peça, **proposta:** novos custos usam o dono atual, ranks pagos continuam com a peça. Os catalisadores devem ter raridade equivalente para evitar arbitragem. Na fusão, rank de habilidade só migra quando a própria instância é transferida e a origem é consumida; copiar somente o ID cria rank 0. Substituir uma habilidade não pode herdar automaticamente o +10 do slot numérico.

## Candidatos a materiais por família

IDs abaixo existem em `Item_Dictionary.cs` fixado. A **associação aos ranks** é proposta nova, não extração de `kaizou.bin`. O probe classifica todos os **131 IDs nomeados** por família e item, com três exceções explícitas; a tabela individual, revisável e gerada do mesmo mapa está em [`ability_ingredient_candidates.tsv`](../../research/mod_004_refinement/ability_ingredient_candidates.tsv) (SHA-256 `4167f8eebfeb995a35a35bb108ad3ffb6056685d65fb7f211c1e93b9f6c50ca9`).

| Habilidades | Itens-base e de marco possíveis | Efeito a mapear |
|---|---|---|
| Auto/SOS Shell, Protect, Haste, Regen, Reflect (#84–93) | Lunar Curtain #56; Light Curtain #57; Chocobo Feather #54 → Wing #55; Healing Spring #59 → Healing Water #21; Star Curtain #58. | Status continua binário; rank dá escalar separado enquanto o status da peça está ativo. |
| Status Touch/Strike/Ward/Proof (#46–83), Ribbon #128 | Farplane Shadow/Wind #50/#51, Holy Water/Purifying Salt #14/#63, Petrify Grenade #49, Poison Fang #45, Dream Powder #38, Silence Grenade #39, Smoke Bomb #40, Hourglass #46/#47, Musk #102, Hypello Potion #103; Ribbon Remedy #15 → Dark Matter #53. | Touch/Ward podem alterar chance; Strike/Proof já binários ou no teto pedem bônus condicional, sem furar imunidade. |
| Element Strike/Ward/Proof/Eater (#30–45), SOS Nul (#94–97) | Fire Bomb Fragment/Core/Gem #26–28; Ice Antarctic/Arctic Wind/Gem #23–25; Lightning Electro/Lightning Marble/Gem #29–31; Water Fish/Dragon Scale/Gem #32–34. | Strike amplia dano; Ward redução; Eater cura; Proof/Nul pedem efeito secundário limitado. |
| Strength/Magic/Defense/Magic Def/HP/MP +% (#98–121) | Spheres correspondentes #85–90; marco Attribute Sphere #75. | Exemplo: +1 ponto percentual por rank no bônus da linha, com teto e recálculo em equip/unequip. |
| Auto-Potion/Med/Phoenix (#8–10), Alchemy #7 | Potion #0 → X-Potion #2; Remedy #15; Phoenix Down #6 → Mega Phoenix #7; Healing Water #21. | Resultado/consumo automático, limite e prevenção de recursão. |
| AP, OD, drops, roubo e gil (#14–22, #26, #129–130) | Door to Tomorrow #107, Winning Formula #111, Gambler's Spirit #109, Fortune Sphere #74, Pendulum #105, Amulet #106, Designer Wallet #52, Wings to Discovery #108. | Pequeno bônus após multiplicador vanilla com teto. No AP #20 pendente. |
| Sensor/abertura/counters/MP/caps/campo (#0–6, #11–13, #23–29) | Map #100, Chocobo Feather #54, Defense/Evasion/Magic Def Spheres #88/#92/#90, Twin/Three Stars #66/#69, HP/MP Spheres #85/#86, Stamina/Mana Spring #61/#60. | Hooks distintos; One MP Cost, Break Limits e No Encounters não escalam diretamente. |
| Capture/Distill (#122, #124–127) | Ability Sphere #73, Power/Mana/Speed/Ability Distillers #16–19. | Recompensa limitada sem alterar contagem de Monster Arena por acidente. |

### Auto-status e efeitos já no teto

Auto-Haste e Auto-Protect são **flags** no binário fixado. “Haste ×2” ou “Protect ×2” não é operação existente. A proposta mantém o status vanilla e acrescenta um efeito **separado**, só quando a habilidade refinada está equipada e seu status ativo. Estes valores são hipóteses de balanceamento, não resultados observados:

| Habilidade | Bônus candidato por rank | Guarda de implementação |
|---|---|---|
| Auto/SOS Haste | Atraso CTB reduzido em 0,5% adicional/rank, até 5% em +10. | CTB mínimo, fonte do status, SOS/KO/reequipar. |
| Auto/SOS Protect e Shell | Dano físico/mágico **após** status multiplicado por `1−0,01r`. | Não aplicar duas vezes, preservar ordem e cap. |
| Auto/SOS Regen | `0,2% × r` do HP máximo adicional por tick/turno. | Arredondamento, HP cap e Zombie. |
| Auto/SOS Reflect | Dano da magia realmente refletida +`1% × r`. | Origem da reflexão, sem reflexão recursiva. |
| Auto-Phoenix | Reviver com HP adicional `1% × r` do máximo. | Consumo do item, proc único. |
| Auto-Potion/Med | Cura automática +1%/rank; Auto-Med pode preservar consumível com chance limitada. | Inventário, proc único, cap. |
| Proof/Ribbon | Se um hit contém status que a habilidade bloqueou, reduzir dano do **mesmo hit** por exemplo 1% relativo/rank. | Identificar status original, não criar imunidade nova. |
| Element Proof | Converter fração limitada do dano elemental impedido em recuperação. | Dano pré-imunidade e teto; não virar Eater completo. |

Strike garantido, Death/Stone instantâneos, One MP Cost, Break Damage Limit e Capture também precisam **bônus lateral temático**, não percentuais acima de 100%. Exemplos a investigar: Strike aumenta um pouco o dano só contra alvo suscetível, sem atravessar imunidade; One MP Cost melhora levemente magia que ainda paga MP; Sensor revela detalhes adicionais e melhora pouco dano contra alvo analisado. `No AP` #20, `No Encounters` #29 e `Aeon Immunities` #123 **não têm desenho honesto universal** ainda. Peça com qualquer um deles deve ficar bloqueada na primeira versão, em vez de exibir falsamente que “tudo melhorou”. Outra política precisa de decisão explícita. A classificação 131/131 do probe não prova os efeitos de outras 128 habilidades.

Possíveis decisões futuras, ainda sem endosso: No AP poderia transformar parte do AP **que continuaria negado** em gil/OD limitado; No Encounters poderia ganhar utilidade de deslocamento em campo, mas alterar velocidade ameaça scripts/câmera; Aeon Immunities poderia dar pequena mitigação adicional ao Aeon sem mexer nas imunidades. Nenhum desses comportamentos aparece no jogo atual nem está modelado como efeito aprovado. Eles mostram por que “todas as habilidades sobem +1” é requisito de design maior do que guardar um contador.

**Leads de RE para o revisor, não endereços prontos para detour:** o binding gerado do Fahrenheit aponta Protect em flat `0x78AE00`/RVA `0x38AE00`, Shell em `0x78AE80`/`0x38AE80`, cálculo de próximo CTB em `0x78D290`/`0x38D290` e tratamento de Reflect em `0x79F180`/`0x39F180`; o caminho geral de dano já mapeado começa em `0x78E680`/`0x38E680`. No PE SHA-256 fixado, cada endereço resolve para bytes de código e começa com `55 8B EC`. Assinaturas/ABI, ordem de aplicação, reversão, multiplicidade de hit e cobertura de chamadas **ainda exigem descompilação/caller e RT1** antes de qualquer hook. Para Regen, o tipo Fahrenheit só confirma um campo `regen_strength@Chr+0x192`, não o ponto de cálculo.

## Estado e transação sem corrupção

- **A:** 1 byte lógico `global_rank` por peça regular (até 200). **B:** quatro bytes `slot_rank[4]`, ou cinco após MOD-005. Acrescentar versão, save associado, identidade/geração e estado PRNG. Isto é **sidecar**, nunca `record+22`.
- Ranks acompanham a **instância**, não modelo/nome: duas peças de bytes iguais podem ter ranks distintos. Instrumentar criação, drop, tesouro, loja, reorder/swap, equip/unequip, venda, destruição, reforja, fusão e troca de habilidades. Duplicatas ambíguas após edição externa entram em quarentena, não recebem rank alheio.
- Sidecar **por save** com versão, fingerprint da peça, índice/geração, checksum e journal. No load, conferir pareamento; sidecar ausente/incompatível desabilita bônus e informa. Testar save copiado/renomeado, peças especiais/Celestiais/Brotherhood, quinta habilidade e mod removido.
- No Confirmar: reler peça, ranks e inventário; calcular custo agrupado; bloquear falta/cap; mostrar recibo/chances; registrar intenção; consumir pelo caminho do jogo e aplicar rank **uma vez no thread do jogo**, com readback. Cancelar ou falhar no pré-flight não altera nada. Falha após débito exige compensação comprovada; editar bytes de inventário às cegas não desfaz efeitos colaterais.
- Crash entre save vanilla e sidecar exige journal/recuperação testada. Dois arquivos não viram transação atômica só com `rename`. Materiais salvos como consumidos com o mod depois removido exigem política de reversão/refund.
- O writer de save atual do Editor tem defeito `FfxSaveEquipment.cs` `+15/+17/+19/+21`, já reproduzido no [relatório anterior](MOD_IDEAS_PRECODE_REVALIDATION_2026-09-23.md). Não serve como base segura antes da correção separada.

## Interfaces possíveis

**Customize nativo:** adicionar rota “Refinar” com lista de peças, ranks, recibo multitem, prévia, sorteio e confirmação. Exige ampliar estado/layout, scroll, textos por idioma, callback e transação. `kaizou.bin` pode oferecer referências de itens/rótulos, mas não expressa rank por peça, materiais agregados ou sorteio. É mais integrado e invade mais o menu vanilla.

**Tecla F:** por esclarecimento do usuário, criar **menu inteiramente novo**, com estado, páginas, cursor, recibo, confirmação, resultado, fechamento/foco e dono de modal próprios. Primitivas baixas de `NativeMenuShell` podem ser reutilizadas como infraestrutura, sem inserir refino nas telas F7/F8/F9. Não abrir sobre Customize/menu vanilla; bloquear batalha, cutscene, save/load e outro modal. F7/F8/F9 têm usos no checkout; **F10 é apenas candidato configurável**, não declarado livre antes de auditoria de colisão e RT2. A interface nova simplifica a prova do fluxo, mas não elimina hooks de efeitos/persistência.

Prévia A: `Tidus / Brotherhood +4→+5`, quatro linhas, custo agrupado e saldo. Prévia B: `Total +17/40`, ranks individuais, `1 de 4 elegíveis (25% cada)`, custo fixo; resultado sorteado só após confirmar.

## Gates para revisão

| Gate | Prova exigida | Estado |
|---|---|---|
| RT0 formato/regras | 131/112 dicionários fixados; Auto-Haste/Protect como bits e stat% numérico no kernel; recibos A/B, +10/+40/+50, três exceções. | **Executado** no [probe](../../research/mod_004_refinement/probe_refinement_design.py), só em memória. |
| Efeitos | Definir cada família, sobretudo No AP, No Encounters, Aeon Immunities, Proof, caps e stacking; calibrar economia. | **Pendente.** |
| RT0/RT1 persistência | Instâncias, duplicatas, reorder, venda, reforja, fusão, save/load, save copiado, crash em cada fase, mod ausente, 4/5 slots. | **Pendente.** |
| RT0/RT1 menu próprio | Abrir/fechar/modal, falta de item, custo agrupado, confirmar/cancelar, foco, outro menu e reload. | **Pendente.** |
| RT2 finito | Autorização separada: peças A/B, Auto-status e stat%, custos reais, drop pré-refinado, save/reload/restauração. | **Não executado.** |
| Produção | Revisão independente, perfil OFF por padrão, assinatura PE, rollback/promoção separados. | **Não iniciado.** |

**Primeiro spike recomendado:** menu próprio por tecla configurável, **Variante A** com quatro habilidades conhecidas e itens vanilla, sem novos IDs; provar custo agrupado, sidecar e um efeito escalar. B reaproveita motor de efeito mas adiciona RNG/rank por slot. Customize nativo e sete itens novos ficam para fases posteriores. Ambas variantes seguem como propostas, sem escolha final de produto.

## Adendo de integração — Aeon Ascension (27/09/2026)

Esta seção acrescenta ao MOD-004 as receitas propostas de [Aeon Break HP/MP Limit e Aeon Break Damage Limit](<../mod-ideas/AEON ASCENSION - MOD 002 004 005.md>), compartilhadas com MOD-002/005. Custos finais: 10.000.000/15.000.000 Gil por peça, já incluindo o ×2 Aeon do Workshop atual; materiais estão no documento e no JSON de receitas. Aplicação exclusiva pela ação dedicada do Workshop, sem Fusion/Customize genérico como atalho.

As duas habilidades são binárias e **não refináveis**: rank 0, exclusão do sorteio e do avanço global, sem cobrar quando não existe outro atributo elegível. Preservar os demais atributos e `0x807B` Aeon Immunity. O adendo não altera os modelos de refino históricos descritos acima nem afirma que os novos tetos já funcionam.

Local: `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, branch `codex/aeon-exclusive-breaks-plan-20260927`, base documental `39ceb198`. Referência de implementação atual: `/home/wanderson/Documents/ffx-hooks`, `main` `f2308ddd`, com AeonAccess/progressão/custos/transações integrados; usar essa versão como ponto de partida e revalidar o contrato atual, não copiar source antigo desta branch documental.
