# MOD-008 — Spira: Arcana of the Fayth

> **Setenta e oito destinos. Uma peregrinação. Escolha quais vozes dos Fayth caminham com você.**

**Jarvis-HOOK · design e pesquisa de integração · 27/09/2026**

> **Atualização 28/09/2026:** candidato nativo implementado em `codex/mod-008-native-arcana-20260927`, com Equip, aquisição por marcos/desafios e liberação completa separada em Desenvolvimento. Consulte [contratos, testes e limites do runtime](../research/MOD_008_RUNTIME_2026-09-28.md). O histórico abaixo preserva a pesquisa anterior; RT2 continua pendente, com revisão apenas do autor.

## Onde continuar este trabalho

- **Esta ideia e seus assets:** `/home/wanderson/.codex/worktrees/mod-008-arcana-fayth/ffx-hooks`, branch `codex/mod-008-arcana-fayth-20260927`, criada de `711e08507fb35ae85b319e31f1624789f0c22d77`. Inclui o histórico documental MOD-001–007, Aeons e novas autoabilities.
- **Runtime atual a integrar futuramente:** `/home/wanderson/Documents/ffx-hooks`, `main`, consultado em `f2308dddc1833811899c0999c96b5ee25a18bd66`. O código herdado pela branch de ideias é anterior ao Workshop atual: transportar os contratos e assets, sem substituir o runtime recente pelo source antigo desta branch.
- **Editor:** `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor`. A integração é uma opção **`[DERIVADO DE MOD-008]`**, exclusiva do perfil Hooks/Mod; authoring vanilla continua disponível.
- **Catálogo:** [78 cartas, nomes completos e efeitos](<MOD 008 - CATALOGO DAS 78 CARTAS.md>), [JSON de design](../../research/mod_008_arcana/cards.proposed.json).
- **Implementação:** [pesquisa de injeção](../research/MOD_008_NATIVE_INJECTION_2026-09-27.md), [plano e critérios de aceite](../research/MOD_008_IMPLEMENTATION_PLAN_2026-09-27.md), [direção de arte](<MOD 008 - DIRECAO DE ARTE.md>).

**Estado:** proposta com catálogo, pesquisa estática e artes conceituais. O módulo de gameplay ainda não está implementado nem instalado. Ver o manifesto de assets e o relatório de validação para contagens realmente produzidas. Endereços encontrados e ilustrações prontas não comprovam que novas linhas do Equip já funcionam no jogo.

## 1. O conceito

As cartas são uma **terceira categoria de equipamento**, ao lado de arma e armadura. Não ocupam os quatro slots de autoabilities nem o quinto lógico do Workshop. São 78 identidades únicas por save: 22 Arcanos Maiores e 56 Menores, divididos entre Wands/Paus, Cups/Copas, Swords/Espadas e Pentacles/Ouros.

Uma carta pode ser usada por qualquer personagem elegível. Equipar a carta reserva sua única cópia; ela passa a aparecer como `Equipped by Yuna`, por exemplo, para os demais. Trocar de personagem não duplica a carta. A arte de Yuna numa carta é uma referência visual, não uma restrição de quem pode equipá-la.

Os sete protagonistas permanentes são o primeiro roster do Equip. Convidados e Aeons exigem que o respectivo ator tenha um seletor Equip integrado e identidade canônica provada; a definição de uma carta não recebe whitelist de Tidus/Yuna/etc. Essa extensão de roster é uma etapa separada, sem conceder a humanos os Breaks exclusivos de Aeons dos MOD-002/004/005.

**Arcanos Maiores têm os melhores efeitos individuais por padrão**, como o usuário definiu. Eles carregam grandes decisões de build; os Menores oferecem especialização, progressão e combinações de três cartas no modo B. O balanceamento pode ser editado por outro mod sem trocar nome, número ou identidade da carta.

### Origem pesquisada

A página de [acessórios de FFX-2](https://finalfantasy.fandom.com/wiki/Final_Fantasy_X-2_accessories) foi baixada pela **API MediaWiki do Fandom**, `action=parse`, revisão **3940120**, 362.997 bytes, HTTP 200. Ela apresenta dois acessórios por personagem e famílias de bônus de atributos, afinidade, status, habilidades e efeitos especiais. O jogo também possui um acessório chamado **Tarot Card**; o baralho de 78 cartas é uma expansão proposta aqui, não um sistema oculto de FFX-2.

Foram identificadas 128 linhas de acessórios no texto baixado. O snapshot completo permanece no cache local, com URL/revisão/hash no [recibo](../../research/mod_008_arcana/fandom-api-receipt.json). Os efeitos abaixo são adaptações para o CTB do FFX. Nomes de fontes no catálogo significam inspiração de design; os valores propostos não são uma transcrição dos acessórios.

## 2. Dois modos de equipamento

| Opção no Hook | Limite | Combinações completas permitidas |
|---|---|---|
| **A — Twin Arcana / Dupla Arcana** | 2 slots | 2 Maiores; 1 Maior + 1 Menor; 2 Menores |
| **B — Constellation / Constelação** | Até 3 slots e orçamento 4 | 2 Maiores; 1 Maior + 2 Menores; 3 Menores |

Slots vazios são permitidos. Em B, Maior pesa 2 e Menor pesa 1, com **duas regras simultâneas**: `quantidade <= 3` e `peso <= 4`. Só usar orçamento 4 permitiria quatro Menores, contrariando o pedido. Só limitar a três permitiria três Maiores, também errado.

Não existe sorteio de carta equipada. O usuário escolhe a combinação. Não há orientação invertida, durabilidade nem consumo de carta no padrão. Uma expansão de cartas invertidas pode existir depois como outro toggle, sem duplicar as 78 identidades.

O modo A é o padrão inicial proposto. O modo efetivo fica associado ao save; o default global serve a saves novos. Alterar B → A abre uma revisão das configurações com três cartas. A alteração só é confirmada após todos os personagens afetados serem ajustados, em uma transação; não remover a terceira carta silenciosamente. Cancelar preserva o modo e os equipamentos anteriores.

## 3. Nomes, números e identidade real do Tarot

Convenção **Rider–Waite–Smith**: The Fool **0**, Strength **VIII**, Justice **XI**, até The World **XXI**. Os Menores usam Ás a Dez, I–X. Nas 16 figuras, **Page/Pajem, Knight/Cavaleiro, Queen/Rainha e King/Rei ficam sem número impresso**, conforme escolha explícita do usuário. A ordem e os nomes seguem o [índice de A. E. Waite](https://sacred-texts.com/tarot/pkt/index.htm).

**Arte final escolhida:** número no medalhão superior; nome tradicional na faixa inferior do molde original. Figuras usam um símbolo de naipe no medalhão. O nome da referência de FFX fica no nome do item e nos metadados.

Formato de item:

```text
0 - The Fool - Tidus
I - The Magician - Lulu
II - The High Priestess - Yuna
VIII - Strength - Ifrit
XI - Justice - Bevelle Guardians
Queen of Cups - Besaid Coast
```

O ID interno 0–77 é apenas o índice do registro do mod. Não imprimir `22` no Ace of Wands nem `77` no King of Pentacles. Os nomes longos recebem duas linhas ou abreviação visual com nome completo na descrição; o identificador persistente é uma chave estável como `major.the_fool`, nunca o texto localizado.

## 4. Equip nativo: fluxo pretendido

Ao abrir **Menu → Equip → personagem**:

```text
Weapon      Brotherhood
Armor       ...
Tarot I     0 - The Fool - Tidus
Tarot II    II - The High Priestess - Yuna
Tarot III   Empty                         [somente modo B]
```

`Tarot I/II/III` identifica a posição do menu; isso não muda o número real da carta. Em modo A só existem duas linhas de Tarot. Com Arcana desligado, só aparecem as opções originais de arma e armadura.

Selecionar uma linha abre o catálogo no próprio fluxo Equip, com fontes, cursor, sons, confirmação e Back do jogo. A lista apresenta:

1. Ícone novo de Tarot; identificação de Maior/Menor e naipe por símbolo, além da cor.
2. Nome completo ou quebra controlada; raridade/progressão; efeito resumido.
3. Carta ilustrada selecionada no painel de preview, mantendo proporção 2:3.
4. Mudanças previstas de atributos/efeitos e capacidade restante em B.
5. Estado `Not acquired`, `Available`, `Equipped here` ou `Equipped by ...`.
6. Ação explícita de transferência quando a carta está com outro personagem. Confirmar mostra quem perde e quem recebe; cancelar não muda nenhum deles.

Não criar um novo menu em F7/F9/F10 para equipar. O menu de configurações do Hook pode conter `Arcana of the Fayth: Off/On`, modo A/B e configurações do módulo. O equipamento propriamente dito fica no **Equip nativo**.

### Estados que precisam ser cobertos

- Voltar da lista devolve foco, cursor, scroll e personagem ao Equip; voltar novamente segue o fluxo original.
- Trocar de personagem invalida um preview antigo antes de aceitar confirmação.
- A carta continua reservada se o personagem sai temporariamente da equipe. Sua propriedade não depende dos três atores em campo.
- Uma transferência de carta de personagem temporariamente ausente usa a identidade persistente do dono e confirmação explícita; não procura o dono num slot de batalha reutilizado.
- A aquisição de uma carta já possuída não cria segunda cópia. Recompensa repetida vira mensagem, sem refund/câmbio automático inventado.
- O primeiro escopo não permite trocar Tarot no meio da batalha. A seleção congela num snapshot do encontro; mudar arma em combate não troca Tarot.
- Falha de textura mantém texto, ícone de reserva e efeitos legíveis; falha de estado/identidade bloqueia alterações.

## 5. As cartas e suas famílias de efeito

O [catálogo completo](<MOD 008 - CATALOGO DAS 78 CARTAS.md>) define as 78 cartas individualmente, com cena, referência de FFX, efeito proposto e fase de aquisição. Este documento define as regras compartilhadas.

| Família | Exemplos no baralho | Rota de implementação |
|---|---|---|
| Atributos | HP/MP, STR/MAG, DEF/MDF, Accuracy/Evasion/Luck | Camada temporária de atributos de campo/batalha, com caps e preview consistentes |
| Elementos | Fire/Ice/Lightning/Water/Holy strike, Wards, amplificação seletiva | Agregação de afinidades e política comum de dano/elementos; extensão MOD-007 é opcional |
| Status no ataque | Darkness/Silence/Sleep/Slow/Poison/Death Touch ou Strike; Breaks na Tower | Agregação de ataque e evento de acerto, respeitando imunidades, duração e natureza do golpe |
| Proteção/status automático | Auto-Protect/Shell/Regen/Reflect/Haste, proofs, SOS | Reutilizar semântica nativa; acrescentar uma nova fonte de efeito com ownership |
| Reação | Counterattack, Evade & Counter, Magic Counter | Fonte adicional no consumidor de reação; uma reação por evento, sem recursão |
| Recursos/condições | Hermit/Star MP por turno; Death/Pentacles Six ao derrotar; Lovers cura compartilhada | Hooks de eventos com action/turn/battle IDs e contagem real do resultado |
| CTB | Fool, Hanged Man | Política de recuperação de turno; não importar ATB de FFX-2 literalmente |
| Economia | AP, Gil, Double Drop, Auto-Potion/Phoenix | Consumidores de recompensa/itens existentes; política de acumulação única |
| Tetos | The World | Política compartilhada de limites, sem cap paralelo ou cópia dos Breaks exclusivos de Aeons |

**Não basta cadastrar 78 linhas em `a_ability.bin`.** Mesmo efeitos já existentes precisam chegar ao agregador e aos consumidores através das cartas equipadas. Já os efeitos novos precisam de lógica própria. As cartas usam um registro próprio; as autoabilities 135–174 criadas anteriormente mantêm seus IDs e regras.

### Regras de acumulação propostas

- Atributos percentuais das cartas somam dentro da camada Arcana e incidem uma vez sobre o valor-base acordado. Recalcular do estado-base, sem reaplicar sobre resultado já modificado. Não escrever bônus permanentes na Sphere Grid.
- Efeitos booleanos usam união: duas fontes de Auto-Haste não produzem Haste ×2. A origem de cada efeito é conservada para desativar apenas a contribuição da carta.
- Proofs preservam os mesmos status cobertos e exceções reais do jogo. `Ribbon` não significa imunidade universal a todo status customizado.
- Para o mesmo status de ataque, a contribuição Arcana usa a melhor chance/duração entre cartas. A proposta é `max(semântica nativa existente, melhor contribuição Arcana)`, evitando nerfar uma combinação vanilla já presente e evitando somar Touch + Strike indefinidamente. Preservar o roll e a imunidade do alvo.
- Multielemento não replica um golpe por elemento. Aplicar uma única vez o bônus da carta quando há interseção de elementos. Com MOD-007, usar sua regra de combinação; sem ele, manter a seleção de afinidade vanilla.
- Recuperação de MP/HP usa resultado efetivo, com inteiros largos e clamp ao máximo válido. Miss, absorção, cura e dano negativo não viram dano causado. KO, Zombie e morte definitiva têm tratamento explícito; não curar automaticamente através de Zombie sem uma regra intencional do efeito.
- Lovers: mede HP realmente restaurado por White Magic em outro aliado; replica 25% ao portador, com teto de 10% do HP máximo dele por ação. A cura replicada não dispara Lovers novamente. Multialvo acumula antes do teto, sem um teto por alvo.
- Death/Pentacles Six: uma ativação por ação do portador que derrota inimigo elegível, não uma por hit, animação ou callback; sem crédito por matar aliado, objeto ou alvo já morto.
- Judgement: efeito proposto de sobrevivência uma vez por batalha, somente em hit elegível de dano HP. Petrify/shatter, ejetar, morte roteirizada e flags especiais precisam de política própria; não prometer que um clamp de HP intercepta essas mortes.
- MP por turno: apenas início real do turno do portador. Preview CTB, reabertura de menu, troca de arma/personagem, animação repetida ou reload de recursos não contam como turno.
- Custos de MP: primeiro resolver flags especiais nativas (`Spellspring`, One MP Cost), depois o ajuste permitido da carta; combinar reduções percentuais uma vez, arredondamento documentado, custo mínimo 1 quando o efeito nativo não concede zero. White/Black são categorias de comandos explicitamente resolvidas, não apenas um bit mágico genérico.
- Recompensas globais usam o maior multiplicador elegível, não produto por personagem. Double Drop da Wheel compartilha a política Double/Triple Drop; dois portadores ou Triple Drop não resultam em 4×/6×. No AP continua impedindo AP.
- Cartas que mexem em CTB alteram o atraso positivo da ação depois da classificação; não criam turnos negativos, não mudam todo Agility nem tornam cada rotação de Fury um turno.
- Todos os aumentos de dano passam pelo cap compartilhado **uma única vez ao final**. The World concede BDL normal; o teto mágico 999.999 só entra quando MOD-007 estiver ativo e o ataque satisfizer a política dele. Não concede HP/Dano 999.999 exclusivo de Aeon a humanos.

Essas regras são contratos propostos a serem provados em RT1/RT2, não garantias de comportamento atual.

## 6. Aquisição: o Álbum da Peregrinação

Proposta de apresentação: cada descoberta preenche uma página do álbum e revela uma pequena referência de Spira, com a carta em tamanho ampliado. O jogador escolhe builds no Equip; o álbum serve a coleção e leitura.

- **Primeiras cartas:** efeitos legíveis de uma só família, entregues por marcos iniciais.
- **Templos e exploração:** Menores especializados e alguns Maiores que abrem estilos de jogo.
- **Airship, sidequests e Monster Arena:** Maiores fortes e cartas de nicho.
- **Desafios finais:** cartas transformadoras como Devil, Tower, Sun e Judgement.
- **The World:** conclusão de uma trilha definida, nunca requisito circular de já possuir a própria carta.

Nenhuma carta deve ser permanentemente perdível. Saves antigos precisam de reconciliação de marcos já cumpridos ou rota alternativa garantida. Cada aquisição tem `award_key` estável, condição resolvida e recibo único no sidecar. Os eventos e flags nativos exatos ainda precisam ser mapeados; nomes de milestones no catálogo são propostas, não endereços verificados.

Padrão: sem venda, descarte, fusão ou consumo das cartas únicas. Refino do Workshop afeta armas/armaduras; não implica automaticamente refinar Tarot. Um futuro sistema de evolução das cartas precisa de escopo próprio e preservação da identidade, especialmente para impedir duplicação.

## 7. Dados separados e Hook desligado

### Escolha recomendada

Um pacote próprio sob `mods/arcana/` **fora dos nomes e rotas de kernels vanilla**, lido exclusivamente pelo módulo Arcana. Fonte editável em JSON e, se necessário, um cache binário versionado gerado pelo compilador do mod. Criar um `.bin` novo é viável como formato **do nosso loader**; não faz o FFX descobrir nem entender esse arquivo automaticamente.

Separar três coisas:

| Conteúdo | Dono | Persistência |
|---|---|---|
| Definições: IDs/chaves, nomes, categoria, efeitos e referências de arte | Pack do mod | Arquivo próprio versionado, independente do save |
| Coleção, dono de cada carta, slots, modo e recibos | Arcana Store | Sidecar ligado ao save e sua geração |
| Texturas e preview | Arcana Assets / renderer | Cache transitório; nunca ponteiro persistido no save |

**Não inserir Tarot em `a_ability.bin`, `arms_rate.bin`, `kaizou.bin`, inventário nativo ou bytes supostamente livres do save.** Não aumentar stride de equipamento e não usar o WORD após os quatro slots como nova posição. Os efeitos podem referenciar chaves do registro comum de autoabilities; isso não transforma uma carta em equipamento nativo.

### Contrato de desligamento

1. Default OFF; opção ON/OFF de infraestrutura requer reinício. Não retirar hooks/texturas/efeitos no meio de uma batalha.
2. OFF não instala o controlador de Tarot, não lê o pack como kernel e não mostra linhas de cartas no Equip.
3. Os sidecars ficam preservados para futura reativação. Remover o pack não pode apagar arma/armadura ou reinterpretar seu ID.
4. ID, schema, capacidade ou save desconhecido: módulo fica inativo para aquele contexto e informa a causa no diagnóstico; não aplica metade do pacote.
5. A gravadora precisa projetar apenas o estado compatível no save nativo, inclusive atributos/flags derivados que possam ser serializados. Não basta afirmar que o arquivo das cartas é separado: bônus temporários de HP/MP ou Auto-Haste não podem vazar permanentemente para o save.
6. Se a projeção de save não estiver comprovada, isso é um **bloqueio de entrega**, não algo a corrigir depois que o usuário salvar.
7. Uma sessão sem Hook que salva novamente invalida a associação por hash antigo. A reativação exige reconciliação explícita ou restauração de um par save/sidecar conhecido. Nome de arquivo e mtime não provam linhagem.

O sistema de eventos de save existente aceita hoje um único observer, ocupado pelo Workshop. O plano inclui um dispatcher compartilhado e um contrato de projeção/commit, em vez de instalar outro detour concorrente. Veja a pesquisa para a evidência direta.

## 8. Formato e validação do pack

O JSON entregue é de **design** (`runtime_compatible: false`). O compilador futuro deve produzir apenas handlers reconhecidos, não interpretar o texto livre `mechanics_proposed` como código.

Campos do formato executável proposto:

```text
pack_id, schema_version, content_revision, required_capabilities,
tarot_convention, cards[], localized_strings, asset_manifest

card: stable_key, numeric_id, arcana, suit, rank,
printed_number_or_null, printed_name_key, item_name_key,
ffx_reference_key, effects[], acquisition_key, art_key, icon_key

effect: registered_handler_key, typed_parameters, stacking_group,
scope, trigger, max_activations, exclusions
```

Cada parâmetro tem faixa e unidade: pontos percentuais não são multiplicador; %HP máximo não é %dano; turno do alvo não é rodada global. Rejeitar duplicatas, chaves inválidas, chance fora de faixa, duração sem semântica, paths absolutos/traversal, asset fora da raiz e handler não implementado. Limitar bytes, contagens, tamanho de strings e memória descomprimida antes de alocar.

Se for adotado binário: magic própria `FHARCANA`, versão/endianness/tamanho, offsets e counts verificados, checksum do conteúdo e índice de strings/efeitos explícito. Não serializar structs C++ com ponteiros nem assumir que um checksum é assinatura/autorização. O layout final só deve congelar depois do protótipo do loader e dos testes de corrupção.

## 9. O que precisa passar pelo Editor

**`[DERIVADO DE MOD-008] Arcana of the Fayth`**: aba/ferramenta opt-in para editar as 78 definições, efeitos tipados, modo, aquisição, nomes/localizações, referência de FFX e vínculo com imagens. Preview de 2/3 slots e exclusividade global; números tradicionais protegidos por validação. Um projeto vanilla não ganha campos de Tarot nem kernels alterados.

O Editor exporta o pack para uma pasta independente e mostra requisitos do Hook. Não instala DLL, não escreve cartas em inventário nativo e não transforma JSON de design em pacote executável sem compiler/handlers implementados. Rótulos/localização convivem com MOD-006; PNGs em inglês são conceitos de arte, não um substituto para strings localizáveis do menu.

Ao fim da implementação do Hook, entregar à lane Editor: commit, versão de schema e exemplo válido; matriz de handlers efetivos; IDs/chaves remapeáveis e suas restrições; convenção Tarot e nomes; formato/encoding das texturas; limites e atlas/UV; projeção de save e migrações; protocolo de transação; mensagens de erro; resultados RT0/RT1/RT2 e limitações restantes. Não prometer opções que o runtime não aceita.

## 10. Caminho de entrega

**P0:** Equip reconhece a categoria e apresenta uma carta de prova com navegação e OFF intactos. **P1:** coleção e transações globais, modos A/B e persistência sem contaminar save. **P2:** efeitos nativos simples. **P3:** recursos/CTB/reação/limites e integração com demais mods. **P4:** 78 artes no formato do renderer, aquisição e localização. **P5:** RT2 autorizado e promoção explícita.

O primeiro marco verificável deve ser uma carta de bônus simples equipada no **Equip real**, com preview, efeito e roundtrip de save. A partir desse corte vertical, expandir consumidores por família; o catálogo de 78 não justifica ligar 78 efeitos incompletos de uma vez.
