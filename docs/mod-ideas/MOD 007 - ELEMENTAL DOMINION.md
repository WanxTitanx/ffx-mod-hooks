# MOD-007 — Spira: Elemental Dominion

**Jarvis-HOOK · proposta de design · 27/09/2026.** Elementos passam a ser ferramentas táticas: explorar, proteger, enfraquecer e transformar afinidades. O núcleo preserva a ideia da imagem; os módulos seguintes ampliam as possibilidades sem obrigar todo mod a usar todas elas.

**Onde continuar:** repo `/home/wanderson/Documents/ffx-hooks`; este pacote está em `/home/wanderson/.codex/worktrees/mod-007-elemental-dominion/ffx-hooks`, branch `codex/mod-007-elemental-dominion-20260927`, derivada da branch documental MOD-006, commit `f53aa3b0c7526a15b667fe317665194004ad6f07`. Runtime consultado: `main` em `f2308dddc1833811899c0999c96b5ee25a18bd66`, no checkout principal. **O runtime herdado nesta branch documental é anterior; implementar sobre a versão atual da lane de runtime, levando apenas os documentos/probes necessários.** Editor: `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor`, snapshot `399164236638bd34d44c4833b02ea3b15a49d771`.

Links: [pesquisa e provas](../research/MOD_007_ELEMENT_REGISTRY_FEASIBILITY_2026-09-27.md), [plano técnico](../research/MOD_007_IMPLEMENTATION_PLAN_2026-09-27.md), [handoff único ao Editor](../ai/HOOKS_EDITOR_MODE_BOUNDARY_2026_09_27.md), [exemplo de contrato](../../research/mod_007_elements/example_manifest.json).

## 1. O que veio da imagem e o que foi acrescentado

| Origem | Requisito preservado | Tratamento no MOD-007 |
|---|---|---|
| Kari, 14:15 | Afinidade em passos de 25%, de absorver 100% até receber 250% do dano | Núcleo: 15 patamares por elemento e por alvo. |
| Kari, 14:28 | Demi contra chefes em 1/16 do HP máximo, em vez de 1/8 | Módulo Gravity independente, com seleção explícita de comandos/chefes. Não é descrição da Demi vanilla. |
| Nuckyduck, 14:29 | MDF negativa até −255, +1% de dano por ponto, até 355% total | Experimento separado; não escrever números negativos no byte nativo de MDF. |
| Gabryc, 14:52 | Magia que reduz resistências elementais, utilizável pelos dois lados | Imperil por elemento; jogadores, monstros e Aeons seguem a mesma regra de efeito. |
| Nuckyduck, 14:13 | Compartilhar o sistema e incorporá-lo a outros mods | Registro com chaves estáveis, manifesto, capacidades e detecção de conflitos. |
| Usuário, nesta tarefa | Acrescentar 8º, 9º, 10º elemento e integrar ao Editor OnlyMod | Oito bits já existem na matemática nativa; elementos além deles usam metadados externos e consumidores do Hook. |
| Usuário, adendo desta tarefa | Magias com Break Damage Limit podem chegar a **999.999**; sem BDL continuam em **9.999** | Módulo independente Magic Break Damage Limit; regra por hit/HP, integrado ao seletor de teto e à classificação explícita de magia. |
| Expansão proposta por Jarvis-HOOK | Ward, Dispel/Cleanse, Scan numérico, encontros temáticos, equipamentos | Módulos opcionais com fases e testes próprios. |

O sucesso do mod atual foi relatado pelo usuário. A validação nova desta pesquisa é RT0/RT1 de partes delimitadas; **MOD-007 ainda não foi implementado nem observado em jogo**.

## 2. Sete visíveis, oito nativos e mais elementos

A UI atual combina quatro elementos básicos com Holy, Darkness e **um** Custom (`0x20` **ou** `0x40`). O formato nativo comporta oito bits, e a rotina original de afinidade processou os oito no harness desta pesquisa. Mostrar os dois Custom simultaneamente exige ampliar a apresentação atual. Isso é uma etapa menor do que criar um 9º elemento.

Proposta de pack **Tenfold**; nomes e ordem são configuração do pack, não significado obrigatório do executável:

| Posição no exemplo | Chave estável | Nome | Transporte |
|---|---|---|---|
| 1 | `ffx.fire` | Fire | Bit `0x01` |
| 2 | `ffx.ice` | Ice | Bit `0x02` |
| 3 | `ffx.thunder` | Thunder | Bit `0x04` |
| 4 | `ffx.water` | Water | Bit `0x08` |
| 5 | `ffx.holy` | Holy | Bit `0x10` |
| 6 | `spira.earth` | Earth | Bit `0x20`, nome escolhido pelo pack |
| 7 | `spira.wind` | Wind | Bit `0x40`, nome escolhido pelo pack |
| 8 | `spira.dark` | Darkness | Bit `0x80`, mapeamento já usado localmente |
| 9 | `spira.poison` | Poison | Somente registro externo/Hook |
| 10 | `spira.gravity` | Gravity | Somente registro externo/Hook |

**Poison elemental ≠ status Poison; Darkness elemental ≠ status de cegueira; Gravity elemental ≠ fórmula percentual de HP.** O pack pode associar esses conceitos, mas eles continuam independentes. Dano sem elemento usa uma lista vazia, não ocupa uma posição. Physical/Magical/HP/MP/CTB continuam categorias do golpe, não elementos novos automáticos.

O registro proposto admite até **32 descritores por pack na primeira versão**, como limite de engenharia a validar, não limite descoberto do FFX nem promessa de 32 elementos funcionando. O primeiro marco acima do nativo exige **9 e 10 simultâneos**, com imunidade/fraqueza próprias, golpes mistos e dois monstros com perfis diferentes. Não basta renomear um elemento existente.

## 3. Afinidade numérica: regra sem ambiguidade

O Editor mostra **dano recebido (%)**. Internamente, `damage_taken_bp` é inteiro em pontos-base: `10000 = 100%`. Passo de authoring inicial: `2500 = 25 pontos percentuais`.

| Valor exibido | Resultado para 1.000 de dano antes da afinidade |
|---|---:|
| −100%, −75%, −50%, −25% | Recupera 1.000 / 750 / 500 / 250 HP |
| 0% | Imunidade: zero |
| 25%, 50%, 75% | Recebe 250 / 500 / 750 HP |
| 100% | Recebe 1.000 HP |
| 125%, 150%, 175%, 200%, 225%, 250% | Recebe 1.250 / 1.500 / 1.750 / 2.000 / 2.250 / 2.500 HP |

Há 15 patamares. Converter a afinidade nativa individual: normal→100%, resist→50%, weak→150%, ignore→0%, absorb→−100%. Máscaras nativas sobrepostas exigem respeitar a precedência nativa documentada na pesquisa; não tratar flags contraditórias como uma soma.

Fórmula proposta para o caminho expandido:

`M = clamp(base + equipment_delta + imperil_delta − ward_delta, −10000, 25000)`

`damage_after_affinity = trunc_toward_zero(damage_before_affinity × M / 10000)`

No preset sem absorção concedida por Ward, `ward_delta` na fórmula é o valor efetivo: `min(ward_stacks × 2500, max(0, base + equipment_delta + imperil_delta))`. Assim, Ward não atravessa zero para criar absorção nem aprofunda absorção já existente. A clamp final mantém o intervalo −100%..250%.

Intermediários de 64 bits; retornar ao contrato de 32 bits com saturação explícita e aplicar a política de teto ativa. O módulo **Magic Break Damage Limit** abaixo eleva expressamente o teto de magias elegíveis com BDL para 999.999; com ele OFF, conservar os limites anteriores. Arredondar uma vez depois da combinação dos elementos; valores pequenos podem resultar em zero, como no exemplo 3×50%=1. O sistema não chama uma segunda rotina de cura para absorção: fornece resultado assinado ao fluxo de aplicação já existente.

**Primeira entrega:** comandos de dano em HP. Cura intencional, dano em MP, CTB e fórmulas especiais precisam de políticas próprias antes de entrar no modo expandido. A inversão de sinal por Zombie/Drain no fluxo nativo deve ser testada, não reinterpretada como cura nova pelo mod. Esses caminhos permanecem sob tratamento nativo enquanto não forem admitidos.

### Ataques com mais de um elemento

Uma opção global do pack define a regra; o preview do Editor e o Hook usam o mesmo resolver:

| Política | Definição | Exemplo: Fire 150%, Ice −100%, golpe de 1.000 |
|---|---|---:|
| `native_exact` | Chama a rotina original; aceita somente semântica nativa, sem afinidades graduais nem elementos externos | 1.500; várias fraquezas nativas podem acumular ×1,5 |
| `highest_exposure` | Maior multiplicador entre os elementos ativos, aplicado uma vez | 1.500 |
| `split_weighted` | Média ponderada assinada; peso padrão 1 por elemento, soma antes de arredondar | 250 com pesos iguais |
| `lowest_exposure` | Menor multiplicador, aplicado uma vez | −1.000 |

Recomendação do preset **Dominion Core**: `highest_exposure`, simples de comunicar e próximo da intenção favorável do vanilla; ele **não reproduz o empilhamento vanilla de múltiplas fraquezas**. `split_weighted` é uma opção de balanceamento, inspirada no precedente Fantasia, sem assumir que seu código atual reconhece os bits extras. Ao escolher `native_exact`, o Editor recusa recursos que essa política não representa. Nenhuma média com lista vazia; nenhum bit desconhecido tratado silenciosamente como Fire.

## 4. Imperil e Ward

**Imperil aumenta o percentual de dano recebido.** De 75% para 100%, e depois 125%; nunca subtrair 25 do multiplicador ao dizer que a resistência foi reduzida.

Proposta inicial para playtest:

| Regra | Padrão proposto |
|---|---|
| Aplicação | +25 pontos percentuais por acerto de status, em um elemento escolhido |
| Acúmulo | Até quatro aplicações por elemento; chefes marcados no manifesto: até duas |
| Duração | Três ações efetivamente concluídas pelo alvo, incluindo Guard/ação forçada; não ticks de renderização |
| Reaplicação | Soma uma aplicação até o teto e renova duração para três ações |
| Ordem | Efeito entra após a ação aplicadora; não aumenta retroativamente o dano dessa mesma ação |
| Multi-hit | No máximo uma tentativa/aplicação por alvo por ação; todos os hits compartilham o snapshot anterior |
| Acerto | Chance de status separada da afinidade; miss/Reflect/resistência de status usam o alvo efetivo |
| Party/enemies/Aeons | Mesma semântica; perfis de imunidade e limites são dados do encontro |
| Troca para retaguarda | Mantém estado; duração pausa sem ações do alvo; impedir reciclagem por novo ponteiro |
| KO, petrificação removida, fuga, mudança de batalha | Limpar modificadores transitórios; reinserção é nova geração |
| Save/load | Não persistir Imperil/Ward da batalha; limpar na admissão da nova sessão |

Contador por ação precisa de `battle_generation + actor_instance + action_sequence`. Se a engine notificar início/fim várias vezes, decrementar uma vez. Aplicação durante a própria ação do alvo começa a contar na ação seguinte. Batalhas encadeadas são nova geração salvo regra explícita e testada.

**Cruzamento de patamares:** no padrão, absorção pode ser enfraquecida: −100→−75→−50→−25→0. Imunidade 0→25 também pode ser quebrada. Monstros especiais podem ter `affinity_lock` por elemento ou imunidade a Imperil. Essas proteções devem aparecer no Scan; não criar chefes que ignoram uma regra invisível. O teto de duas aplicações em chefes é uma proposta de balanceamento, não fato do FFX.

**Ward numérico:** −25 pontos percentuais por aplicação, mesmos limites/duração; não concede absorção sozinho por padrão (`ward_floor_bp=0`). Absorção definida pelo monstro/equipamento continua válida. Ward e Imperil têm contadores separados: podem se compensar, e remover um revela o outro.

**Nul de uma carga** continua uma mecânica separada. Não converter NulBlaze em redução de 25%. O plano deve fixar uma única autoridade para consumir Nul/novos wards de bloqueio por golpe, inclusive ataques mistos. O protótipo inicial de afinidade não deve inventar cargas para o 9º/10º elemento antes dessa integração.

**Remoção proposta:** um comando `Cleanse` remove Imperil; um `Dispel Ward` remove Ward. Integrar ao Esuna/Dispel vanilla é toggle do pack, não consequência implícita de ocupar um bit de status. Ribbon não protege contra status externo automaticamente: cada efeito declara sua relação com Ribbon/imunidades.

## 5. Gravity com controle útil contra chefes

Demi vanilla no fixture é fórmula 5, potência 4, sem elemento: 1/4 do **HP atual**. Demi Fury usa potência 2: 1/8 do atual. A proposta da Kari diz **HP máximo de chefes**, portanto muda também a base do cálculo.

Uma magia universal de 1/16 do máximo cabe em `TargetMaxHp` (fórmula 8), potência 1. Para a mesma Demi fazer 1/4 do atual em monstros comuns e 1/16 do máximo em chefes selecionados, o Hook resolve uma regra por comando/alvo. O Editor pode optar por comandos distintos com dados nativos quando a AI/menu já consegue escolher o adequado.

Padrão do módulo proposto:

1. Lista explícita de chefes/perfis; nada de identificar chefe por HP alto ou pelo nome traduzido.
2. `base_damage = floor(target_max_hp / 16)` para os alvos cadastrados.
3. Preservar imunidade Gravity nativa por padrão; liberar somente no perfil explicitamente selecionado. Trocar a fórmula não remove essa imunidade.
4. Não letal por padrão: resultado final não ultrapassa `current_hp−1`. O limite vale depois de amplificadores; validá-lo no ponto final real, sem descontar HP duas vezes.
5. Respeitar teto de dano ativo. Sem Break Damage Limit, continua 9.999; com o módulo Magic BDL ativo e a magia elegível, pode chegar a 999.999. A restrição não letal continua valendo. Mostrar ambos os limites no preview.
6. Afinidade elemental do novo `spira.gravity` é **outro toggle**, OFF no exemplo. Se ON, a regra precisa declarar se absorção é permitida e como atravessa a imunidade Gravity; primeira implementação deve rejeitar regras contraditórias.

Exemplo: chefe com 160.000 HP máximos e 40.000 atuais → bruto 10.000; Demi vanilla também daria 10.000 nesse instante, mas as curvas divergem nos outros turnos. Com máximo 160.000 e atual 5.000, a regra não letal limita a 4.999 antes do teto aplicável. Isso precisa de playtest para não transformar todos os chefes em uma sequência de Demi.

## 5.1. Magic Break Damage Limit — requisito adicional do usuário

**Regra solicitada:** magias podem ultrapassar 99.999 e chegar a **999.999 de dano com Break Damage Limit**. Sem BDL, o teto continua **9.999**. É limite por hit e por alvo, não o total somado de uma ação. O módulo não multiplica o dano: um cálculo de 120.000 fica em 120.000; um cálculo de 1.200.000 fica em 999.999.

| Situação, com o módulo ON | Teto de dano positivo em HP |
|---|---:|
| Magia sem BDL efetivo | **9.999** |
| Magia com BDL efetivo | **999.999** |
| Ataque não elegível sem BDL | Regra nativa, normalmente 9.999 |
| Ataque não elegível com BDL | Regra nativa, normalmente 99.999 |
| Módulo OFF | Regra anterior, incluindo limites nativos 9.999/99.999 |

**Classificação proposta para implementar o pedido:** magias de dano das categorias Black/White e suas variantes explicitamente cadastradas, de jogador, inimigo ou Aeon. Holy/Fire/Demi/Ultima entram; Fury de Lulu entra como variantes de magia de dano, por binding de comando. O flag nativo de dano mágico é pista de importação, não definição suficiente: **Fire Fury e Demi Fury têm esse flag desligado no fixture**. Arma com Firestrike continua ataque de arma; STR/MAG usados na fórmula, elemento visual ou nome não decidem sozinhos. Itens, Mix, Blue Magic/Nova e Overdrives de Aeons exigem classificação explícita do pack; não expandir todos os Overdrives por consequência. Cura intencional, MP, CTB e status sem dano ficam fora da primeira versão.

**BDL efetivo:** preservar a decisão nativa: autoability agregada no atacante (`+0x6BE & 0x0800`) ou flag inata do comando (`DamageFlgs+0x20 & 0x80`); a flag `0x40` suprime BDL de equipamento quando não há o override `0x80`. Assim, um comando explicitamente sem BDL fica em 9.999. O Editor deve mostrar a origem do BDL e avisar se uma flag inata já quebra o limite sem equipamento. Não conceder BDL a toda magia para alcançar o teto novo.

**Ordem:** fórmula → afinidade/Imperil/Ward e demais modificadores → seleção do teto por hit/componente → clamp final → aplicação/mostrar dano. Capturar o valor **antes** do clamp original; aumentar um resultado que já foi cortado para 99.999 não recupera o dano calculado. Amplificação 250% não ultrapassa 999.999 no resultado final. Gravity conserva sua proteção não letal adicional.

**Cura/absorção:** elevar apenas o teto positivo de dano. O piso negativo nativo de cura/absorção continua separado; não transformar o pedido em cura de 999.999 sem uma opção futura explícita. Validar também Zombie/Drain, para uma cura invertida não ser classificada acidentalmente como magia ofensiva.

**Integração concreta:** os pontos já localizados são RVA `0x38ED1A` (escolha do teto), `0x38EDD3` (clamp superior), `0x38EDD9` (writeback). O `NovaSuperDamageHook.cpp` atual já possui o ponto `0x38EDD3`; incorporar uma política compartilhada ou rejeitar combinação incompatível, nunca instalar um segundo patch por cima. Preservar o ramo de piso negativo e o loop HP/MP/CTB. Mudar somente o imediato `99999` seria global e não satisfaz esta regra.

**OnlyMod:** opção independente `[DERIVADO DE MOD-007] Magic Break Damage Limit`, OFF por padrão, sem exigir ativar 9º/10º elemento ou Imperil. Editor oferece elegibilidade/preview e exporta bindings versionados; o Hook implementa o limite. Nenhum novo ID `a_ability.bin` é necessário para reutilizar o Break Damage Limit existente (`0x8019`, índice 25). A flag vanilla “Breaks Damage Limit” continua com sua semântica original fora do perfil do mod.

**Prova pendente no jogo:** número de seis dígitos, HP realmente removido, acumuladores/Overdrive/overkill/counters, várias magias no mesmo turno e coexistência com Nova. A pesquisa nova conferiu bytes/flags e possui teste RT0 da política; não há Hook desse teto implementado nesta entrega. Ver [pesquisa, seção 9](../research/MOD_007_ELEMENT_REGISTRY_FEASIBILITY_2026-09-27.md#9-magic-break-damage-limit-999999).

## 6. Ampliações que dão identidade ao sistema

Entregar como pacotes opcionais, depois do núcleo; valores são pontos de partida para balanceamento.

| Mecânica | Proposta concreta | Dependência |
|---|---|---|
| Imperil Fire/Ice/etc. | Habilidade single-target, uma aplicação; variante em área custa mais MP/rank | Status externo, comandos/textos no Editor |
| Elemental Fracture | Duas aplicações em um elemento, maior custo/rank, chefes mantêm seu teto | Núcleo Imperil e economia de comando |
| Elemental Ward | Proteção numérica visível, distinta de Nul | Estado por ator e remoção |
| Scan tático | Mostra valor base→efetivo, duração, lock e motivo de imunidade | Modelo do mesmo resolver, UI dinâmica |
| Monstros adaptativos | Trocam perfil ao mudar de fase, com indicação de batalha | Eventos/AI; usar snapshot por ação |
| Blue Magic | Kimahri aprende uma habilidade de enfraquecimento ou troca de afinidade definida pelo pack | Concessão/comando e compatibilidade com sua lane |
| Equipamentos | Resist −25 pp, Imperil Strike com chance própria, Ward em baixa vida | Chaves de efeito, IDs de autoability remapeáveis; MOD-002/004/005 |
| Rikku | Itens de exposição elemental com ingredientes/custos existentes | `item.bin`, menus Use/Mix e receitas; não criar item novo por suposição |
| Aeons | Pacotes opcionais Bahamut Holy/Valefor Water e golpes Earth/Wind | Dados e AI próprios; não trocar identidade elemental por padrão |
| Encontros | Flan com fraqueza móvel, inimigo que usa Ward, chefe que pede Cleanse/Imperil | AI que consulta o perfil efetivo, telegráficos legíveis |

### Módulos experimentais, fora da primeira entrega

- **MDF negativa virtual:** `exposure_points` separado, de 0 a 255, aumenta dano mágico em 1% por ponto. −255 MDF nativa vira 1 se for estreitada para byte; isso não implementa a ideia. É preciso escolher se a fórmula usa MDF nativa normalmente antes do amplificador ou zera a mitigação quando o atributo virtual fica negativo. São modelos diferentes. Até essa decisão e os testes, o toggle não pode ser liberado como funcional.
- **Oil / Soaked / Brittle:** estados de exposição específicos, inspirados em outros FF, sem ocupar bits nativos ao acaso. Para o primeiro ensaio, cada estado contribui +50 pp em seu elemento, compartilhando o teto, em vez de criar multiplicadores em cascata. Relações elementais são dados do pack.
- **Ressonância:** uma sequência anunciada de dois elementos pode conceder um bônus limitado na próxima ação. Guardar no máximo um gatilho por ação/alvo e impedir counters de se realimentarem. Não misturar com o núcleo antes de provar cancelamento, Reflect e múltiplos hits.
- **Campos elementais:** área/batalha altera afinidades de ambos os lados. Fonte separada no preview, removida na saída do encontro. Não deve persistir no save vanilla.

## 7. Perfis e interface

Todos os módulos novos começam OFF. Configuração seleciona um pack e capacidades; ativação depende de versão/assinatura e só muda entre batalhas. Trocas visuais de cor podem continuar imediatas como no Scan atual; trocar regras no meio de uma ação não pode invalidar o snapshot.

| Preset | Conteúdo |
|---|---|
| Classic | Rotas nativas; permite apresentação extra dos oito bits; nenhuma regra de afinidade nova |
| Dominion Core | Escala de 15 patamares + `highest_exposure`; mantêm-se os elementos do pack atual |
| Dominion Tactics | Core + Imperil/Ward/remoção e Scan numérico |
| Tenfold | Tactics + dez descritores, comandos e resistências externas explicitamente cadastrados |
| Magic BDL | Módulo independente: magias elegíveis com BDL até 999.999; sem BDL, 9.999; combinável com os presets anteriores |
| Experimental | Habilitação individual de MDF virtual, reações ou campos; resultados nunca presumidos estáveis |

**Scan para 8/10/32:** descritores dinâmicos; resumo compacto dos relevantes no Sensor, grade/paginação no Scan completo. Mostrar nome/ícone e percentual, sem depender só de cor. Não encolher dez colunas até ficarem ilegíveis nem limitar o dano à quantidade de ícones visíveis. Preservar o toggle independente de stats/MP.

Se houver configuração via tecla F, o usuário já determinou **menu novo, independente**, com nome Elemental Dominion e tecla não vinculada por padrão. O Scan nativo é a visualização de batalha; não substituir o menu de refino nem assumir F7/F8/F9/F10 livres. O Editor é a ferramenta principal de authoring do pack.

## 8. Fronteira com o Editor

**Vanilla permanece o modo padrão.** Manter `Element_Flags : byte`, registros, máscaras e edição já existente. Campos comprovados, como fórmula 8/potência 1 ou um bit nativo de afinidade, continuam disponíveis sem requerer MOD-007. A mudança de produto está em funções que exigem a extensão.

Somente no perfil **OnlyMod `[DERIVADO DE MOD-007]`**:

- Criar/renomear descritores por chave estável; cores/ícones/nomes são apresentação.
- Adicionar 9º/10º elemento sem escrever `0x100`/`0x200` nos bytes de `command.bin`, `monmagic`, `a_ability.bin` ou atores.
- Editar matriz numérica, regras Imperil/Ward, comandos e perfis de chefes; prever dano com a política selecionada.
- Exportar manifesto/overlays para destino próprio; validar versão, hashes e referências; não aplicar no Steam durante authoring.
- Mostrar “dados prontos / Hook ausente / capacidade indisponível / sem RT2”, conforme o caso. Authoring offline pode funcionar sem DLL; ações de integração exigem as capacidades apropriadas.
- Recusar exportação vanilla de recursos irrepresentáveis. Uma conversão explícita pode produzir uma cópia e um relatório de perdas; nunca arredondar 75% para 50% silenciosamente.

O Hook entrega ao final um handoff exato com versão de schema, capacidades, arquivos, IDs efetivos, ranges, exemplos válidos/inválidos, provas e limitações. IDs de equipamento MOD-002 não serão consumidos como IDs de elementos: são domínios diferentes. O efeito de um equipamento se vincula à chave do elemento e ao ID remapeável da autoability.

## 9. Critérios de conclusão do mod

- [ ] Eight: oito afinidades simultâneas com math/presentation consistentes; nenhum Custom escondido por exclusão mútua.
- [ ] Core: 15 patamares, arredondamento, absorção, limites e políticas mistas explícitos.
- [ ] Tactics: Imperil/Ward simétricos, duração/remoção/Reflect/multi-hit e gerações provados.
- [ ] Tenfold: 9º e 10º funcionam simultaneamente de ponta a ponta, sem modificar layouts nativos.
- [ ] Gravity: bases atual/máximo, imunidade, regra por chefe e limite não letal provados.
- [ ] Magic BDL: magias com BDL até 999.999, sem BDL em 9.999, Fury classificada por comando, demais categorias/cura preservadas e seis dígitos validados.
- [ ] Editor: perfil OnlyMod, import/export, preview e regressão vanilla; chaves/manifesto compartilhados.
- [ ] Compatibilidade: Workshop, NulWard, Difficulty e Fantasia tratados por um contrato explícito.
- [ ] RT2 autorizado depois: matriz reproduzível de jogador/inimigo/Aeon, field/battle/save/load, entrada/saída/OFF; promoção separada.

**Estado desta entrega:** design, pesquisa de bytes/source e probes isolados. As caixas acima descrevem a futura implementação; não estão concluídas pelo teste da rotina original.
