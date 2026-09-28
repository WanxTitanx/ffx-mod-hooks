# Aeon Ascension — Breaks exclusivos dos Aeons

**Jarvis-HOOK · adendo integrado MOD-002/004/005 · 27/09/2026.** Duas novas auto-habilidades de equipamento, aplicáveis **somente pelo Workshop**, permitem aos Aeons aliados ultrapassar os tetos anteriores. O nome de cada habilidade segue o pedido do usuário. Os valores máximos são requisitos; as receitas abaixo são a proposta de balanceamento desta pesquisa.

**Catálogo complementar:** a [Parte 2 do MOD-002](<PARTE 2 MOD 002.md>) agora inclui as auto-habilidades do **Spira Reforge**, inclusive Warden's Oath/Auron e Arcane Focus/Lulu, com fontes, IDs e pendências. São identidades separadas dos Breaks Aeon deste documento; compartilhar código de limite não compartilha autorização de proprietário.

**Repo/branch deste adendo:** `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, `codex/aeon-exclusive-breaks-plan-20260927`, base documental `39ceb19834e87d2ee74349dcf4b1b676f87e677b`. **Runtime de referência:** `/home/wanderson/Documents/ffx-hooks`, `main` em `f2308dddc1833811899c0999c96b5ee25a18bd66`. **Editor:** `/home/wanderson/Documents/ffx-editor-main`, `codexclaudiocodeffxeditor`, snapshot `399164236638bd34d44c4833b02ea3b15a49d771`. Esta branch documental contém source herdado anterior ao runtime citado: levar o adendo para a lane atual, sem substituir sua implementação por snapshots antigos.

**Atualização de dados:** IDs **148/149 (0x8094/0x8095)** criados nos três grupos, com payload neutro e descrições de Hook obrigatório. [Relatório aplicado](../research/AUTOABILITY_SPIRA_AEON_CREATION_2026-09-27.md). Receitas, aplicação/exclusividade e efeitos continuam dependendo do Workshop/Hook futuro.

## 1. Requisitos fechados

| Habilidade | Peça proposta | Efeito |
|---|---|---|
| **Aeon Break HP/MP Limit** | Armadura canônica do Aeon | Teto de HP **999.999**; teto de MP **9.999**. Uma única auto-habilidade combina as duas permissões. |
| **Aeon Break Damage Limit** | Arma canônica do Aeon | Teto de dano positivo em HP **999.999 por hit e por alvo**, para ataques físicos, magias e Overdrives numéricos do Aeon. |

- São auto-habilidades, não consumíveis novos na mochila. As receitas usam itens que já existem no FFX.
- Exclusivas de **Aeons aliados adquiridos**, não de humanos, Seymour, inimigos com modelo de Aeon ou Dark Aeons inimigos.
- Aquisição/aplicação apenas por ação dedicada dentro do **Equipment Workshop existente**. Sem receita em Customize vanilla, drops, lojas ou concessão automática pelo Editor.
- Elevar o teto não entrega 999.999 HP nem transforma todo golpe em 999.999. Crescimento, fórmula, defesa, afinidade e dano real continuam determinando o resultado.
- Comprar/remover a habilidade não cura gratuitamente nem reabastece MP. Recalcular o máximo e limitar o atual com `min(current, new_max)` quando necessário.
- Os Breaks vanilla de humanos continuam com sua semântica; nenhuma alteração global de HP/MP/dano.

## 2. Receitas originais conferidas no arquivo do jogo

Fonte primária lida nesta tarefa: `.../data/mods/ffx_ps2/ffx/master/jppc/battle/kernel/kaizou.bin` na instalação Steam. Caminho completo e hashes estão em [evidence.json](../../research/aeon_exclusive_breaks/evidence.json). Arquivo de **1.020 B**, SHA-256 **`fea70d34a8567260e5b27da19eb86234ec02c6ab1e59c774f3b799ccd15ed844`**, header 20 B, 125 linhas de 8 B. A tabela gerada `research/equipment_workshop/include/customize_recipes.h` registra o mesmo hash.

| Auto-habilidade vanilla | ID/word | Material | ID do item/word | Quantidade |
|---|---|---|---|---:|
| Break HP Limit | 23 / `0x8017` | Wings to Discovery | 108 / `0x206C` | **30** |
| Break MP Limit | 24 / `0x8018` | Three Stars | 69 / `0x2045` | **30** |
| Break Damage Limit | 25 / `0x8019` | Dark Matter | 53 / `0x2035` | **60** |

As três receitas são fatos do arquivo examinado; **o custo extra de Gil e os ingredientes adicionais propostos a seguir não são vanilla**. Os nomes/IDs dos ingredientes adicionais foram conferidos no catálogo do Workshop. A nomenclatura dos Breaks também coincide com [Fahrenheit, IDs de autoability](https://github.com/fahrenheit-crew/fahrenheit/blob/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx/ids/a_ability.cs).

## 3. Receita proposta — Aeon Break HP/MP Limit

**Preço final: 10.000.000 Gil por aplicação em uma armadura de um Aeon.**

| Ingrediente | ID / word | Quantidade | Motivo |
|---|---|---:|---|
| Wings to Discovery | 108 / `0x206C` | **60** | Duas vezes o material do Break HP vanilla; sustenta a identidade do upgrade. |
| Three Stars | 69 / `0x2045` | **60** | Duas vezes o material do Break MP vanilla; a habilidade também libera MP até 9.999. |
| Underdog's Secret | 110 / `0x206E` | **30** | Material adicional de customização avançada; diferencia a receita da mera soma dos Breaks. |
| Master Sphere | 80 / `0x2050` | **2** | Catalisador raro de fim de jogo, consumido explicitamente. |

Wings to Discovery e Master Sphere são descritos como raros no corpus local de wiki. Underdog's Secret é um material de equipamento avançado existente, também usado pela receita nativa de Double Overdrive; não depende de inventar um item. A receita concentra materiais de HP/MP e um custo adicional de fim de jogo.

## 4. Receita proposta — Aeon Break Damage Limit

**Preço final: 15.000.000 Gil por aplicação em uma arma de um Aeon.**

| Ingrediente | ID / word | Quantidade | Motivo |
|---|---|---:|---|
| Dark Matter | 53 / `0x2035` | **99** | Material original do BDL elevado de 60 a uma pilha completa. |
| Winning Formula | 111 / `0x206F` | **30** | Material da receita vanilla de Triple Overdrive, coerente com poder ofensivo de fim de jogo. |
| Gambler's Spirit | 109 / `0x206D` | **20** | Material da receita vanilla de SOS Overdrive; componente adicional ofensivo. |
| Master Sphere | 80 / `0x2050` | **3** | Catalisador raro, mais caro que o da habilidade defensiva. |

Todos os requisitos individuais cabem em uma pilha de 99. **Não aplicar o multiplicador genérico de 1,5× do quinto slot nesta receita**: Dark Matter 99 se tornaria 149 e deixaria a compra impossível com o inventário normal. A ação dedicada tem o mesmo preço final em slot nativo ou quinto lógico; o desbloqueio do quinto continua separado e precisa ter sido pago.

**Escala do investimento:** as duas habilidades em um Aeon custam 25.000.000 Gil e cinco Master Spheres, além dos outros materiais. Nos dez owners de Aeon (as três Magus Sisters são individuais), completar ambos em todos custa 250.000.000 Gil e 50 Master Spheres. Isso é uma progressão de fim de jogo proposta, não exigência para usar o Workshop básico.

### Como evitar cobrar o dobro duas vezes

O core atual já aplica **×2 no Gil de qualquer operação em Aeon**, em `workshop::Preview`, após planejar a edição e antes de verificar/debitar recursos. Portanto:

| Receita | Base passada ao multiplicador atual | ×2 Aeon, uma vez | Total exibido/debitado |
|---|---:|---:|---:|
| HP/MP | 5.000.000 | 10.000.000 | **10.000.000** |
| Dano | 7.500.000 | 15.000.000 | **15.000.000** |

Não acrescentar taxa genérica de Fusion ou de colocar o quinto atributo à ação especial: esses totais são o preço completo de aplicação. Não multiplicar materiais por ×2. Exceções de desenvolvimento já existentes devem continuar explícitas, mas não podem liberar humano, peça inválida, duplicata ou remoção de Aeon Immunity.

## 5. Quem pode receber e como aplicar

Reaproveitar `IsAeon`, `ReadAeonProgress` e `AeonAccess` do core atual, com verificação adicional do novo efeito. Owners permitidos: **8–17**. O owner 7 não entra por aproximação de faixa.

| Owner | Aeon | Gate do Workshop atual a preservar |
|---:|---|---|
| 8 | Valefor | Adquirido + Crest aplicado à Nirvana |
| 9 | Ifrit | Adquirido + Crest aplicado à World Champion |
| 10 | Ixion | Adquirido + Crest aplicado à Spirit Lance |
| 11 | Shiva | Adquirido + Crest aplicado à Onion Knight |
| 12 | Bahamut | Adquirido |
| 13 | Anima | Adquirido |
| 14 | Yojimbo | Adquirido + Crest aplicado à Masamune |
| 15/16/17 | Cindy / Sandy / Mindy | Aquisição individual reconhecida pelo registro nativo |

O gate usa o Crest **aplicado**, não apenas possuído. Não inventar vínculos com armas de Tidus/Rikku. Manter identidade de arma/armadura canônica, owner, índice equipado e flags conferidos na confirmação; personagem com aparência de Aeon não basta.

Fluxo proposto: **Workshop → Aeon → arma/armadura → Aeon Ascension → habilidade → posição → prévia de custo/efeito → confirmar**. Permitir peça equipada em campo conforme Workshop atual; aplicação durante batalha permanece bloqueada.

Regras de posição:

1. Cada habilidade ocupa **uma posição real de auto-habilidade**. Não é bônus invisível gratuito nem exige um sexto slot.
2. Aplicar em posição nativa vazia; ou substituir explicitamente um Break vanilla correspondente: arma BDL, armadura BHP ou BMP. Não remover automaticamente uma segunda habilidade redundante de HP/MP.
3. Permitir quinto lógico se já desbloqueado e se os quatro slots nativos estiverem preenchidos, conforme MOD-005. Preço especial permanece o da seção anterior.
4. Nunca substituir **Aeon Immunity `0x807B`** em qualquer posição. Manter seu tratamento permanente já implementado.
5. Sem duplicata do mesmo novo Break na mesma peça. Um Break vanilla coexistente não soma tetos nem multiplica o efeito.
6. As novas habilidades **não são refináveis**: rank 0 fixo; excluí-las de sorteios e do avanço global +10, permitindo refinar os outros atributos elegíveis. Não cobrar um avanço se só houver atributos protegidos/não refináveis.
7. Aplicação não pode acontecer pela operação genérica de Fusion, evolução ou quinto slot para contornar a receita. Essas entradas devem rejeitar os novos IDs e encaminhar à ação dedicada do Workshop.
8. Remover somente pelo Workshop, com confirmação da perda do teto, sem reembolso automático. Ao substituir um Break vanilla refinado, resetar o rank daquela posição e mostrar a perda na prévia.

## 6. Exclusividade também no efeito e na persistência

O menu sozinho não garante exclusividade. A execução deve exigir simultaneamente:

- Feature habilitada, perfil/assinaturas compatíveis e mapeamento de autoability válido.
- Aeon **aliado** adquirido e identidade canônica verificada; inimigos/Dark Aeons/humanos não passam no gate.
- Peça equipada do tipo correto e presença do atributo correspondente.
- Registro de aplicação do Workshop associado a `save_id + piece_id + owner + effect_key + recipe_version`, preservado pelo fluxo transacional. ID copiado isoladamente não conta como aplicação válida.

Essa proveniência previne cópia acidental/fusão/importação errada; não é promessa de mecanismo inviolável contra um editor externo. A lane deve definir uma extensão versionada do sidecar ou migração explícita. Não enfiar recibos em bytes reservados, ranks ou campos do save vanilla.

Sem recipe em `kaizou.bin`, sem atributo na lista comum de Customize, sem item novo em drop/loja. O Editor poderá criar texto/manifesto OnlyMod e inspecionar o estado, mas não apresentar concessão comum dessa habilidade no modo vanilla nem simular uma compra paga sem Workshop.

**Crest/Sigil e reconstrução nativa:** o Workshop atual observa o produtor de equipamento ligado às armas lendárias (`LegendShim`, RVA `0x4C3150`). Uma atualização posterior pode reescrever as quatro habilidades nativas. A integração deve preservar o Break pago na mesma peça/posição quando a transição for conhecida e válida, mantendo a imunidade e os demais resultados legítimos do produtor. Transição incompatível deve gerar conflito explícito, sem perder silenciosamente uma compra de milhões de Gil nem cobrar a receita outra vez. Cobrir também quinto lógico e receipts no teste da transição.

Se Hook/sidecar faltar, não reconstruir autorização só pela presença do ID. Aplicação fica indisponível e efeito fica inativo. A compatibilidade de saves com HP acima de 99.999 precisa ser provada antes de release: não prometer preservação da progressão além do teto apenas porque os campos são DWORDs.

## 7. Contrato de teto e interação com MOD-007

**HP/MP:** resolver antes dos clamps de máximo no campo e em batalha; a habilidade autoriza HP até 999.999 e MP até 9.999. Deve funcionar na entrada/saída de invocação, cura, revive, Double HP/MP e reload. Provar que o crescimento/Soul/improvement do Aeon consegue produzir valores acima de 99.999 sem já perder o valor antes do novo clamp. Não adulterar stats de Yuna para simular o resultado.

**Dano:** o Aeon Break Damage Limit novo fornece a permissão ampliada a todos os golpes numéricos de HP elegíveis do próprio Aeon, incluindo físicos e Overdrives. Continuar respeitando supressão explícita de BDL do comando conforme contrato nativo; registrar exceções se o pack desejar mudá-las. Ele não altera instant-kill/Zanmato, não aumenta potência, não eleva cura/absorção para 999.999 e não afeta dano MP/CTB. Overdrive multi-hit limita cada hit, não o total da ação.

Os novos Breaks **substituem funcionalmente a permissão dos Breaks normais** no Aeon autorizado. Não exigir que `0x8017/0x8018/0x8019` também permaneçam equipados: o upgrade pode justamente substituir um deles. O Hook deve fornecer a permissão virtual correspondente depois do gate de peça/recibo e antes da escolha de teto; linhas neutras em `a_ability.bin` não fazem isso sozinhas.

Integração com Magic BDL do MOD-007: usar **um único resolvedor de teto**. Para magia de Aeon elegível que tenha ambos, o teto é `max(999999, 999999) = 999999`; nunca multiplicar ou somar. A nova autoability torna o ataque físico de Aeon elegível onde o módulo mágico sozinho não o tornaria. Sem o novo atributo, conservar a rota anterior do Aeon e os módulos separadamente habilitados.

`NovaSuperDamageHook.cpp` já possui o clamp superior; `EquipmentWorkshopRuntime.cpp` já possui o contexto de dano. Reusar ownership e callbacks ou rejeitar a combinação até a composição estar provada; nenhum segundo detour sobreposto. Ataques do inimigo que estejam apenas atingindo um Aeon não ganham teto novo: a regra de dano é do **atacante**.

## 8. Pontos concretos encontrados na pesquisa

PE lido novamente: SHA-256 **`78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced`**, PE32/i386, base preferida `0x400000`. Endereços flat = RVA + base.

| Superfície | Evidência/ponto | Implicação |
|---|---|---|
| Workshop Aeon atual | `research/equipment_workshop/src/workshop.cpp`: `IsAeon`, `AeonAccess`, `Preview`; `docs/reverse/AEON_WORKSHOP_2026_09_27.md` no runtime atual | Reaproveitar gates/proteção/transação; não começar por novo inventário de Aeons. |
| Limite do catálogo | `SupportedRefinement`/`SupportedFifth` aceitam `0x8000..0x8082`; `CustomizeRecipes[131]` | Os novos IDs precisam de registro/catálogo próprios; ampliar apenas o intervalo criaria acessos fora da tabela. |
| Custos | `Plan.costs/requirements` por item, `Preview` aplica Gil ×2 uma vez | Core já representa múltiplos ingredientes; ação especial precisa compor a lista e conferir tudo antes de debitar. |
| Recalcular campo | RVA **`0x3861B0`** / flat `0x7861B0`; já interceptado por `FieldShim` | Aeons equipados são recalculados após transação; a operação atual não forja base stats. |
| Máximo em campo | Flat **`0x786882..0x7868A7`** seleciona HP 9.999/99.999 via WORD `PlySave+0x4C &0x200`, grava DWORD `+0x24`; MP **`0x7868AA..0x7868CA`**, bit `0x400`, teto 999/9.999 | HP 999.999 exige Hook nessa política; MP pode reutilizar semântica nativa com gate Aeon. |
| Helper de clamp | RVA **`0x39A0D0`** / flat `0x79A0D0`, 26 B: três argumentos na stack `value/min/max`, comparações signed, retorno sem limpeza | Candidato para adapter estritamente filtrado por caller e contexto do Aeon. Não mudar a função globalmente nem modificar limites de chamadas não admitidas. |
| Escala de Yuna no ramo Aeon | Campo tem `push 0x270F` em **`0x786260`**, após chamada com owner1 | Não substituir todos os literais 9999/99999: alguns pertencem à base de escala, não ao teto final do Aeon. |
| Double HP/MP em batalha | Rotina com prólogo flat **`0x78D330`**, lê estado `actor+0x640`; HP testa WORD **`+0x6BE &0x200`** em `0x78D37B`; MP testa `+0x6BE &0x400` em `0x78D3EF` | Aumentar só o máximo de campo não basta; status pode recapá-lo ao máximo antigo. Nota antiga indicava `+0x726`, contrariada pelos bytes atuais. |
| Valores em batalha | BaseHP `+0x59C`, maxHP `+0x594`, curHP `+0x5D0`; equivalentes MP `+0x5A0/+0x598/+0x5D4`, acessos DWORD observados | Tipo comporta 999.999; isso não prova todos os consumidores/save/UI. |
| Dano | RVA **`0x38ED1A`** seleciona BDL; **`0x38EDD3`** faz clamp superior; **`0x38EDD9`** escreve resultado/HP | Usar valor pré-clamp; preservar piso negativo e passagens MP/CTB. |

Provas desta tarefa são leitura de source, tabelas e disassembly estático. Nenhum novo adapter foi executado. Os testes anteriores de Aeon Workshop e MOD-007 são referências históricas com escopos próprios, não validação deste adendo.

Primeiro recorte técnico recomendado para HP: reusar o contexto `FieldShim/Produce` existente, interceptar de forma contextual o limite do helper apenas nas chamadas finais verificadas e construir contexto de ator para a rotina de Double HP/MP. Os retornos HP observados são flat `0x7868A7` no campo e `0x78D3A4` no status. A chamada que limita HP de Yuna no cálculo do Aeon retorna em `0x786272` e deve permanecer fora desse recorte. Revalidar ABI, thread e todos os callers restantes antes de instalar; essa é proposta de adapter, não código pronto.

## 9. IDs e handoff para o Editor

Chaves propostas: **`aeon_break_hp_mp_limit`** e **`aeon_break_damage_limit`**. Os defaults agora são **148/149**, criados como linhas hook-only neutras. O manifesto de receitas foi alinhado a esses IDs, sem instalar uma receita no jogo. Não reaproveitar 23/24/25 (Breaks vanilla), 123 (Aeon Immunity), 134 ou 135–147 (reservas anteriores MOD-002). A lane deve auditar as tabelas reais antes de escolher novas linhas, mantendo remapeamento de ID por chave no menu do Hook.

Se houver linhas novas em `a_ability.bin`, devem começar como identidade/texto hook-only, com payload de efeitos vanilla neutro. Colocar flags nativas de Break diretamente na linha poderia conceder efeitos a um humano que recebesse esse ID por outra via. Não existe linha de receita multi-material/Gil em `kaizou.bin` para estes Breaks.

Tags de UI/manifesto: **`[DERIVADO DE MOD-002]`** (atributos/IDs/efeitos), **`[DERIVADO DE MOD-004]`** (Workshop/transação/receita) e **`[DERIVADO DE MOD-005]`** (quinta posição lógica). Manter todos os serializers e fluxos vanilla disponíveis.

Ao concluir o Hook, entregar à lane Editor: schema e capabilities, IDs finais, linha/texto por idioma, paths, tipo de peça/owners admitidos, receita final e regra do ×2, posições elegíveis, migração de sidecar, vetores de preview, commands/resultados RT0/RT1/RT2 e limitações. Authoring de dados não deve afirmar que 999.999 já funciona sem o serviço runtime.

## 10. Sequência de implementação e aceitação

- [ ] Congelar receita/versionamento e auditar IDs reais; nomes/descrições e preços ficam no registro do mod, não em IDs vanilla substituídos.
- [ ] Novo handler do Workshop valida Aeon, owner/kind, progressão, immunity, posição, duplicatas, materiais e Gil; compara preview/commit inteiro; nenhuma cobrança parcial, rerrolagem ou queda em receita genérica.
- [ ] Integrar duas chaves no catálogo/allowed operations; excluir de Fusion/drops/Customize comum e de refinamento; preservar efeitos nas mudanças legítimas de inventário/save.
- [ ] Provar crescimento/base antes do clamp, política de HP/MP no campo, agregação em batalha, Double HP/MP, cura/revive, dismiss/resummon e remoção sem cura gratuita.
- [ ] Integrar teto Aeon e Magic BDL em resolvedor único; físico/magia/OD, humanos/enemy/Dark Aeon como controles; bloco instant-kill separado.
- [ ] Validar displays de seis dígitos, barra HP, menu de status/summon, Scan quando aplicável, comparação do Workshop e números de dano sem truncamento.
- [ ] Versionar persistência e testar save/reload, bits/gates de Crest, sidecar ausente/corrompido, mudança de ID, troca de peça e carga de save diferente. Estado persistente válido não nasce só de ID copiado.
- [ ] Casos de custo: item ou Gil faltando, cancelamento, erro de escrita, confirmação antiga, receita no quinto, multiplicador ×2 uma vez e nenhuma exigência de 149 Dark Matter. Humanos/Seymour/monstros e dono divergente sempre recusados sem débito.
- [ ] RT2/deploy apenas quando houver candidato concreto e autorização própria; registrar HP/MP efetivos, dano e persistência. Revisão independente e promoção permanecem gates separados.

**Estado atual:** linhas148/149 aplicadas aos assets e verificadas; receitas/efeitos/controle de owner continuam propostos. Save/equipamentos/configuração/DLL não foram alterados. O arquivo [recipes.proposed.json](../../research/aeon_exclusive_breaks/recipes.proposed.json) é dado de planejamento, não configuração funcional do Hook atual.
