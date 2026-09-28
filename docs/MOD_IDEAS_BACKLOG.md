# Backlog de ideias de mods e hooks

Registro cumulativo de ideias vindas de conversas e fóruns. Os checkboxes acompanham o trabalho neste repositório; uma afirmação ou tentativa citada no fórum não conta como implementação ou validação local.

## MOD-001 — Overhaul de combate (nome TBD)

- **Tipo:** mod de gameplay
- **Estado:** ideia registrada; auditoria estática de viabilidade adicionada ao fim deste documento; implementação não realizada.
- **Fonte:** trechos de fórum enviados pelo usuário; tópico e link não informados.

### Objetivos declarados

- Tornar a IA dos monstros mais dinâmica e interessante.
- Reequilibrar o elenco para que todos os personagens possam ser úteis no conteúdo de fim de jogo.
- Adicionar recursos inspirados em RPGs modernos.
- Melhorar a diversão sem transformar o projeto em um mod de dificuldade.

### Ideias e recursos mencionados

- [ ] Criar um Overdrive próprio para Kimahri que reforce seu papel de Blue Mage.
- [ ] Adicionar habilidades inéditas a classes de monstros, reforçando identidade e função em combate.
- [ ] Dar multiplicadores de dano adicionais a certas habilidades quando condições específicas forem atendidas.
- [ ] Usar um limite de dano maior para facilitar o balanceamento de lutas avançadas.
  - O autor diz que está experimentando com o limite. Sua justificativa é manter lutas da arena mais longas do que seriam sem limite — segundo ele, cerca de 5–10 minutos em vez de 1–2 com atributos máximos — sem recorrer a chefes com HP excessivo.
- [ ] Tornar os ataques de Bahamut elementais de Holy.
  - Sugestão de afriendlyirin, que considera estranho não haver um Aeon de Holy; Dragoon803AP gostou da ideia e disse que poderia testá-la. A conversa também cita Holy como uma fonte de dano contra Yunalesca.
- [ ] Tornar Valefor elemental de Water.
  - afriendlyirin sugeriu a mudança por Valefor vir de uma ilha pequena. Dragoon803AP concordou; também comentou que sentia falta de um summon de Water.

### Discussões e decisões em aberto

- **Limite de dano:** afriendlyirin sugeriu remover o limite por completo, até o máximo da variável. Dragoon803AP prefere investigar um limite maior, pois considera que isso ajuda a balancear lutas avançadas sem encurtá-las demais. Nenhum valor foi definido.
- **Onde implementar:** afriendlyirin avalia que boa parte do mod pode ser feita editando arquivos de dados, mas que mudanças no motor serão mais difíceis. É uma avaliação do participante, ainda sem análise dos arquivos deste projeto.
- **Comandos por personagem:** afriendlyirin apontou um valor nos blocos de comando que associa comandos a personagens, mas disse que não o havia investigado. Dragoon803AP afirmou que cada comando tem um byte de associação e que Overdrives usam essa associação junto de uma flag para aparecer no menu de OD. Informação de fórum, ainda não verificada localmente.

### Próximos pontos para investigar

- [ ] Definir nome e escopo do mod.
- [ ] Identificar quais ajustes são feitos em dados e quais exigiriam hook ou mudança de engine.
- [ ] Localizar e confirmar o byte de associação personagem/comando e a flag do menu de Overdrive.
- [ ] Definir como o Overdrive de Kimahri aprende ou seleciona habilidades e como se integra ao papel de Blue Mage.
- [ ] Especificar quais classes de monstros recebem habilidades e quais condições ativam dano bônus.
- [ ] Definir o limite de dano desejado e comparar o efeito no ritmo das lutas avançadas.
- [ ] Verificar o impacto de Holy em Bahamut e Water em Valefor, incluindo compatibilidade com resistências, animações e efeitos existentes.

### Origem no fórum

- **15/01/2025, 21:27 — Dragoon803AP:** apresentou objetivos e recursos iniciais; nome do mod ainda não definido.
- **01/02/2025, 12:34 — afriendlyirin:** comentou sobre edição de dados, dificuldade de mudanças no engine, associação de comandos a personagens e remoção do limite de dano.
- **01/02/2025, 12:51 — afriendlyirin:** sugeriu Bahamut com elemento Holy.
- **01/02/2025, 21:02 — Dragoon803AP:** explicou a preferência por um limite de dano maior e comentou sobre o byte de personagem nos comandos e a flag dos Overdrives.
- **01/02/2025, 21:11 — Dragoon803AP:** gostou da sugestão de Bahamut Holy e mencionou a ausência de um summon de Water.
- **01/02/2025, 21:34 — afriendlyirin:** sugeriu Valefor com elemento Water.
- **01/02/2025, 22:21 — Dragoon803AP:** concordou com a sugestão de Valefor Water.

---

## MOD-002 — Patches de mudanças de engine (lista da Kari AP)

- **Tipo:** coleção de mudanças de gameplay e patches reutilizáveis
- **Estado:** ideias de fórum registradas; auditoria estática de viabilidade adicionada ao fim deste documento; implementação não realizada.
- **Fonte:** arquivo de texto enviado pelo usuário, com mensagens de Kari AP e outros participantes entre 14/01/2025 e 04/11/2025; link do tópico não informado.

**Resumo:** conjunto de alterações configuráveis para combate e mecânicas de FFX. A conversa distingue correções de bugs de mudanças subjetivas e sugere que correções gerais de engine possam ser oferecidas pelo Fahrenheit para outros mods reutilizarem.

### Fórmulas, dano e balanceamento

- [ ] Manter o limite normal de dano em 99.999 e fazer Break Damage Limit elevar o teto para 9.999.999, em vez de remover o limite por completo. A lista apresenta esses valores como a proposta da Kari.
- [ ] Aplicar bônus percentuais de STR, MAG, DEF e MDF vindos de equipamentos quando o atributo correspondente for usado, sem vinculá-los apenas a ataques físicos ou mágicos.
- [ ] Calcular bônus percentuais de DEF/MDF por pontos de vida efetivos; o exemplo da lista diz que DEF +20% reduziria o dano em cerca de 16,6%, não 20%.
- [ ] Fazer Shell e outros efeitos protetivos ou auto-habilidades deixarem de reduzir cura recebida.
- [ ] Ajustar Haste para reduzir atrasos de turno em 33%, Slow para aumentá-los em 50%, e Protect/Shell/Power Break/Magic Break para reduzir dano em 33%.
- [ ] Reformular escala de defesa com `DefenseMultiplier = 1 / (1 + Defense * 0.05)`, fazendo cada 20 pontos de defesa dobrarem os pontos de vida efetivos. A proposta assume atributos menores que 100.
- [ ] Alterar Armor Break e Mental Break: em vez de zerar defesa, acrescentariam 25% do dano-base sem mitigação por defesa. Exemplos da lista: 60 DEF, 25% para 50% do dano; 20 DEF, 50% para 75%; 0 DEF, 100% para 125%.
- [ ] Adicionar fórmulas mágicas novas que escalem como STR, mas respeitem MDF, e permitir novas fórmulas sem necessariamente substituir as originais ou as fórmulas não usadas.
  - peppy propôs como objetivo futuro permitir que uma fórmula de dano fosse uma função C# configurável, o que exigiria alterar a rotina de cálculo. Kari comentou que já havia reimplementado essa rotina para mudar frações de HP/MP/CTB de unidades de 1/16 para 1/60; ela observa que isso não seria facilmente opcional para tabelas de ataques já balanceadas.
  - O texto copiado também contém números de ajuste para golpes múltiplos e golpes únicos (`100%`, `110%`, `+5%`), mas a relação entre eles ficou ambígua; confirmar no post original antes de transformar em requisito.
- [ ] Consumir Auto-Crit e MP-0 ao usar, removendo o efeito ao fim do turno para que todos os acertos de um mesmo ataque ainda recebam o benefício.
- [ ] Ao atingir uma fraqueza elemental, aplicar também x1,25 de dano do elemento oposto; exemplo: fraqueza a Ice aumenta dano de Fire.
- [ ] Permitir que reaplicar um status já ativo renove sua duração.
- [ ] Substituir Doublecast por Quickcast: em vez de lançar duas vezes, transformar uma magia Black Magic em ação de rank 2 pelo dobro do custo de MP. Em 10/04/2025, Kari também sugeriu incluir White Magic em um submenu próprio de Doublecast/Quickcast.
- [ ] Rever a duração de Sleep e Poison: uma proposta torna Sleep não temporário (o personagem acorda ao chegar seu turno) e faz Poison durar uma quantidade definida de turnos antes de precisar ser reaplicado.
- [ ] Adicionar resistência à duração de status nos inimigos, além da resistência à chance de aplicação, para diferenciar habilidades de Attack Buster por quantidade de turnos em vez de apenas sucesso/falha.
- [ ] Fazer Threaten funcionar apenas uma vez por inimigo.
- [ ] Rever Accuracy e Luck: propostas incluem criar um resultado de acerto parcial com animação de esquiva atrasada, fazer críticos dependerem de Accuracy e usar Luck apenas para crítico ou diretamente na fórmula de acerto. Reequilibrar os monstros se a fórmula mudar.
- [ ] Rever efeitos de ataques que deveriam sempre acertar, mas aparentemente podem errar; a conversa também pergunta se um patch de 4 GB faria parte de uma coleção de correções, sem decisão registrada.
- [ ] Limitar atributos a 100 e HP a 10.000, ou definir limites próprios para cada personagem.
- [ ] Considerar outros atributos no escalamento de armas; exemplo: garras de Rikku escalando com Agility. Como alternativa para o dano de Rikku, foi sugerido fazer seus itens de ataque escalarem com MAG. Os participantes alertam que Agility já é muito forte e que ampliar o uso de atributos pode desequilibrar o jogo.
- [ ] Usar MP atual para fortalecer magia: fórmula sugerida `Base MAG = MAG + sqrt(MP / 2) + Focus stacks`, com bônus de MAG por faixas de MP: 0–1: +0; 2–7: +1; 8–17: +2; 18–31: +3; 32–49: +4; 50–71: +5; 72–97: +6; 98–127: +7; 128–161: +8; 162–199: +9; 200–241: +10. A anotação sobre efeitos MP-0 limitarem o bônus a +3 precisa ser esclarecida.

### Overdrives, troca de personagens e participação

- [ ] Permitir que certos Overdrives sejam usados antes de encher a barra, cobrando menos que 100% quando a habilidade exigir menos.
- [ ] Opcionalmente fazer a troca de personagem consumir um turno. Kari considera a troca livre parte central do combate de FFX; aceitou a possibilidade como opção para modders experimentarem, sem decisão de padrão.
- [ ] Quando um personagem for ejetado ou estilhaçado, colocar o primeiro personagem disponível da retaguarda em seu lugar, em vez de deixar a posição vazia. Verificar restrições de troca nas áreas de natação.
- [ ] Conceder AP a todos os personagens disponíveis ao fim da batalha, independentemente de participação ou KO, respeitando as armas com No AP, Double AP, Triple AP ou Overdrive→AP. A conversa registra casos de combate com personagens bloqueados ou ocultos e possíveis perdas de AP em batalhas consecutivas.
  - Dawn Veilwinter disse que levaria essa proposta para uma publicação de EFP; mantida aqui como referência de origem, não como item decidido para este conjunto.

### Habilidades de armas e armaduras

As propostas abaixo foram apresentadas como auto-habilidades de equipamento. Algumas condições e interações são ideias iniciais, não regras fechadas.

**Armas**

- [ ] **Hero's Bravery:** +25% de chance de causar crítico e +25% de chance de receber crítico.
- [ ] **Bônus elemental:** a anotação propõe dano/cura elemental x1,2 e relaciona o efeito à barra de Overdrive acima de 50%; a sintaxe original está incompleta e deve ser confirmada.
- [ ] **Energy Burst:** dano e cura x1,4 enquanto a barra de Overdrive permanece acima de 75%; a proposta diz que acumula aditivamente com Energy Boost, chegando a x1,65.
- [ ] **Efficiency:** reduzir em 25% custos de MP e Overdrive; combinada com Half MP Cost, a redução de custo de MP chegaria a 75%.
- [ ] **Vampirism:** ao derrotar um inimigo, recuperar 25% do HP máximo dele, usando a condição de Slayer e ignorando Zombie.
- [ ] **Assist Attack / Follow Up:** atacar automaticamente o mesmo alvo quando um aliado usar um ataque de HP de alvo único. Dawn prefere o nome Follow Up.

**Armaduras**

- [ ] **P-Trade / M-Trade:** trocar mitigação entre dano físico e mágico; P-Trade recebe dano físico x0,8 e mágico x1,2, e M-Trade faz o inverso.
- [ ] **Hero's Caution:** descrição copiada diz que nunca causaria crítico aleatório, mas repete a mesma frase para o efeito negativo; confirmar o comportamento pretendido. Hero Drink seria uma exceção e garantiria crítico.
- [ ] **MP Regen:** recuperar 2% do MP máximo no começo do turno; a anotação considera reaproveitar o ponto de hook do booster F2, cuidando de trocas de personagem e modos de Overdrive.
- [ ] **Elude:** +50 de Evasion ao defender.
- [ ] **Energy Wall:** dano recebido x0,8 enquanto a barra de Overdrive permanece acima de 50%; não afetaria cura nem fórmulas de dano fixo fracionário.
- [ ] **Energy Barrier:** dano recebido x0,7 enquanto a barra permanece acima de 75%; acumularia aditivamente com Energy Wall até x0,5.
- [ ] Disponibilizar em combate as habilidades ativas da arma ou armadura enquanto a peça estiver equipada.
- [ ] Dar às habilidades de equipamento custo parcial de Overdrive e mostrar visualmente a parte consumida em branco e a parte que falta em vermelho. A autora esclareceu que pensava em habilidades multiplicadoras, não em habilidades que gastam grandes partes da barra.

### Patches reutilizáveis e questões de escopo

- [ ] Criar no Fahrenheit uma coleção de patches gerais de engine para outros mods reutilizarem, evitando que cada um reimplemente correções de runtime complexas. peppy comparou a ideia ao USSEP de Skyrim; alterações específicas de Master's Challenge, como o overhaul do limite de dano, ficariam fora.
- [ ] Usar o termo **Patches** em vez de **Fixes** para não dar a entender que toda alteração subjetiva corrige um bug real do jogo.
- [ ] Decidir se as fórmulas vanilla não usadas devem ser reaproveitadas ou se novas constantes/fórmulas devem ser adicionadas, preservando as existentes quando possível.
- [ ] Delimitar o conjunto entre correções de problemas objetivos e mudanças subjetivas de balanceamento/expansão. A conversa questiona explicitamente se a coleção deveria incluir apenas bugs, o patch de 4 GB e/ou extensões de gameplay.
- [ ] Reavaliar amplificadores multiplicativos de dano: Dawn Veilwinter depois considerou Energy Boost/Burst má ideia, pois FFX é conservador com amplificação de dano e multiplicadores podem incentivar min-maxing.
- Kari AP chegou a considerar nomes personalizados para equipamentos, mas concluiu que essa ideia pertencia a EFP.
- [ ] Confirmar o fragmento de 04/11/2025 sobre summon e CTB antes de registrar uma mudança: o trecho explica que personagens fora de campo mantêm seu CTB e que quase todos os Aeons recebem um turno imediato ao serem invocados, mas não conserva a proposta que iniciou a conversa.

### Adendo do usuário — Aeon Ascension (integração MOD-002/004/005)

- [ ] **Aeon Break HP/MP Limit:** nova auto-habilidade de armadura exclusiva de Aeons aliados, aplicável somente pelo Workshop, com HP até 999.999 e MP até 9.999. Receita proposta: 60 Wings to Discovery, 60 Three Stars, 30 Underdog's Secret, 2 Master Spheres e 10.000.000 Gil finais.
- [ ] **Aeon Break Damage Limit:** nova auto-habilidade de arma exclusiva de Aeons aliados, aplicável somente pelo Workshop, com dano até 999.999 por hit. Receita proposta: 99 Dark Matter, 30 Winning Formula, 20 Gambler's Spirit, 3 Master Spheres e 15.000.000 Gil finais.

- [ ] **Catálogo Spira Reforge de equipamentos:** integrar Mana Spring, Break Limits, Devil's Bargain, Double/Triple Drop, custom134, famílias HPMP/AIO e reworks; registrar Warden's Oath (Auron) e Arcane Focus (Lulu) com gates por owner, mantendo direções ainda abertas dos demais personagens e distinguindo dados, Hook existente e proposta. [Parte 2 consolidada](<mod-ideas/PARTE 2 MOD 002.md>); snapshot pré-criação de 27/09 detectou divergências entre catálogo histórico e dados JP/US. Após autorização, foram criadas27 linhas aditivas148–174, preservando0–147; Double/Triple Drop155/156 receberam markers. [Relatório aplicado](research/AUTOABILITY_SPIRA_AEON_CREATION_2026-09-27.md).

O ×2 de Gil do Workshop Aeon já está incluído; receitas próprias não recebem 1,5× no quinto slot. Sem nova receita vanilla, sem refinamento dos Breaks, sem perda de Aeon Immunity ou concessão a humanos. IDs Aeon148/149 criados como linhas neutras; receitas/efeitos ainda pendentes. [Design/receitas/RE/contratos](<mod-ideas/AEON ASCENSION - MOD 002 004 005.md>), em `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, branch `codex/aeon-exclusive-breaks-plan-20260927`; runtime consultado `/home/wanderson/Documents/ffx-hooks`, `main` `f2308ddd`.

### Origem e andamento relatado

- **14/01/2025, 10:47 — Kari AP:** publicou a lista inicial de mudanças de engine.
- **14/01/2025, 10:55 — peppy:** sugeriu concentrar patches gerais reutilizáveis no Fahrenheit.
- **18/01/2025 — Kari AP e Dawn Veilwinter:** discutiram as auto-habilidades de armas e armaduras; Dawn levantou preocupação com efeitos que mudariam durante o próprio comando e Kari ajustou a descrição para a barra “permanecer acima” do limite.
- **21/01/2025 — Dragoon803 e Kari AP:** discutiram fazer a troca de personagem consumir turno; a ideia ficou como possível opção.
- **01/02/2025 — afriendlyirin, Dawn Veilwinter e Kari AP:** discutiram limites de atributos, BDL, custo parcial de Overdrive, Accuracy/Luck e um possível hook junto de Counterattack/Magic Counter para Follow Up.
- **06/02/2025 — Kari AP e Dawn Veilwinter:** discutiram AP garantido; Dawn disse que levaria a ideia para EFP.
- **22/02/2025 — Kari AP:** propôs fortalecer magia com MP atual.
- **10/04/2025 — Kari AP:** retomou Quickcast e a opção de um submenu de White Magic Doublecast/Quickcast.
- **13–17/08/2025 — Kari AP, peppy e Dawn Veilwinter:** discutiram fórmulas customizáveis, funções C# e resistência de duração de status.
- **31/10/2025 — Kari AP e Dawn Veilwinter:** discutiram substituir personagens ejetados/estilhaçados por alguém disponível da retaguarda.

---

## MOD-003 — Fantasia

- **Tipo:** pacote de mudanças de mecânica e opções configuráveis
- **Estado:** ideia e histórico registrados; estado do repositório Fantasia conferido em 23/09/2026; auditoria estática adicionada ao fim deste documento.
- **Fonte:** mensagens de Dawn Veilwinter e Kari enviadas pelo usuário; conversa de 12/08/2025 a 09/03/2026. Repositório citado: [EvelynTSMG/ffx-mods-fantasia](https://github.com/EvelynTSMG/ffx-mods-fantasia).

**Resumo:** Dawn descreve Fantasia como inspirado nos mods Fabrication (Fabric) e Forgery (Forge), reunindo mudanças nas mecânicas vanilla que ficaram fora de EFP por diferentes motivos, agrupadas pelo impacto e com muitas configurações.

### Balance

- [ ] **Rebalanced Elemental Affinities:** regra configurável para combinar afinidades. **Favorable** ignora afinidades piores quando existem melhores; **Fair** considera todas; **Unfavorable** ignora as melhores quando existem piores.
  - A lista original marca essa proposta com `[X]`; esse estado pertence ao post original e não confirma implementação neste repositório.
- [ ] **Useful Kimahri:** opção para começar com Fire, Blizzard, Thunder, Water, Esuna, Cure, Dark Attack, Power Break e Cheer; opção separada para começar com 3 Sphere Levels, configurável por Sphere Grid.

### Experiments

- [ ] **Mix Overhaul:** proposta citada sem detalhes no trecho enviado.
- [ ] **Water Aeon:** Geosgaeno no lugar de Anima. É diferente da proposta MOD-001 de tornar Valefor elemental de Water.

### Minor Mechanics

- [ ] Fazer Doublecast incluir White Magic.
- [ ] Zerar a carga de Overdrive no início de batalhas contra chefes.
- [ ] Dar usos a itens normalmente disponíveis apenas via Customize (**Useful Items**).
- [ ] Fazer raios na Thunder Plains causarem dano configurável — incluindo a fórmula — a Tidus, à linha de frente ou ao grupo; o exemplo é 50 de dano, reduzindo HP até no mínimo 1.

### Mechanics

- [ ] **Quickcast:** substituir Doublecast por uma magia de rank reduzido a custo de MP dobrado; o post deixa em aberto se o rank deve ser reduzido pela metade (com mínimo 2) ou simplesmente definido como 2.
- [ ] **Fury Overdrive:** consumir todo o MP para fortalecer uma única magia. Cada rotação acrescentaria 4 MP ao custo, de forma configurável.
  - As interações com Spellspring, One MP Cost e Half MP Cost ficaram em aberto: multiplicar o MP considerado (4x/3x/2x), facilitar as rotações, usar valores de referência como MP máximo/máximo−1/metade quando menores, ou considerar custo 0/1/metade do MP atual e enfraquecer a magia.
  - Dawn comentou que a ideia incentiva Lulu a não usar Half/One/Zero MP Cost.
- [ ] Fazer Yuna invocar apenas por Grand Summon, removendo Summon normal; opção para Grand Summon não invocar com Overdrive.
- [ ] Fazer statuses persistirem; o trecho não especifica quais durações ou transições mudariam.
- [ ] Fazer KO persistir; o trecho não detalha quando ou como o personagem voltaria.
- [ ] Fazer Fire Eater alimentar a carga de Overdrive em vez de recuperar HP.

### Utility — itens sem mudança de gameplay

- [ ] Mostrar ícones de quais personagens ainda não participaram da batalha.
- [ ] Mostrar ícones de status em vez de depender do texto que muda.
- [ ] Exibir HP/MP dos Aeons sem abrir o menu Summon.
- [ ] Mostrar quais chefes da Monster Arena já foram derrotados.
- [ ] Permitir encontros personalizados no Sphere Monitor.
- [ ] Tornar instantâneas as poções da mala/suitcase.
- [ ] Pré-visualizar a ordem de turnos após revival com Phoenix Down, Mega-Phoenix, Life ou Full Life.

### Vanilla+ e Style

- [ ] Exigir que todos os minigames de borboletas sejam vencidos para liberar o próximo, evitando perder recompensas.
- [ ] Não salvar mudanças de formação feitas durante a batalha.
- [ ] Não salvar mudanças de equipamento feitas durante a batalha.
- [ ] Adicionar mais encontros no Sphere Monitor.
- [ ] Renomear Soft para **Golden Needle**, além de outros nomes alternativos para itens.

### Proveniência, contribuições e andamento relatado

- Dawn pediu autorização a Kari para reaproveitar ideias da lista de engine changes; Kari aceitou e observou que outras pessoas também as implementariam e que várias já tinham sido feitas localmente por ela.
- Em 14/08/2025, Dawn disse que estava começando o framework; em 16/08/2025, anunciou a estrutura inicial e o repositório para contribuições.
- A mensagem de 16/08 diz que algumas patches dependiam bastante de FhSetting, que ainda não tinha renderização básica para alguns elementos. Em 09/03/2026, Dawn disse que deveria atualizar o projeto para usar o SDK. São relatos históricos das mensagens, não uma verificação atual do GitHub.
- A postagem termina aberta a sugestões e pede considerar se algumas delas se encaixariam melhor em EFP.

### Sobreposições a manter distintas

- **MOD-001 e MOD-003:** Kimahri com Overdrive de Blue Mage e Kimahri começando com habilidades/Sphere Levels são propostas diferentes.
- **MOD-001 e MOD-003:** Valefor Water e Geosgaeno no lugar de Anima são ideias distintas relacionadas a Aeons/Water.
- **MOD-002 e MOD-003:** ambas mencionam Quickcast, mas com definições de rank diferentes; acompanhar a configuração de Fantasia como alternativa, sem assumir que as duas descrições são equivalentes.
- **MOD-002 e MOD-004:** MOD-002 propõe novas habilidades de equipamento; MOD-004 trata de mudar o dono, fundir, remover e abrir slots de equipamentos.

---

## MOD-004 — Reforja e customização de equipamentos

- **Tipo:** sistema de equipamentos e opções de qualidade de vida
- **Estado:** proposta registrada; custos e comportamento de algumas opções seguem em discussão; auditoria estática adicionada ao fim deste documento.
- **Fonte:** Kari AP, Dawn Veilwinter, ImmortalFork e afriendlyirin; mensagens de 20/03/2026 a 24/04/2026.

**Resumo:** completar as opções do equipamento permitindo mudar seu dono/tipo, fundir peças e ajustar slots e habilidades.

### Propostas principais

- [ ] **Reforjar:** mudar o dono e a categoria de uma arma ou armadura; exemplos: Ring de Yuna virar Shield de Tidus, ou Katana de Auron virar Spear de Kimahri.
- [ ] **Fundir equipamentos:** escolher uma peça como base e transferir até duas habilidades da peça consumida para quaisquer slots da base, ocupados ou vazios.
  - Brotherhood e armas Celestiais poderiam receber habilidades por fusão, mas não poderiam ser consumidas como peça de origem.
- [ ] **Expandir slots:** como opção de Customize, adicionar slots vazios a armas ou armaduras com menos de quatro.
- [ ] **Remover habilidades:** usar Clear Spheres como opção de Customize para apagar habilidades existentes. Kari considera que isso pode ser redundante se fusão permitir substituir habilidades, mas também uma alternativa mais flexível.

### Sugestões adicionais

- [ ] Refinar armas/armaduras até +10, com custos e efeitos derivados das auto-habilidades da peça; drops podem vir pré-refinados. Dawn apresentou a noção de +10 em 20/03/2026; o usuário detalhou duas regras alternativas em 23/09/2026.
  - **Variante A — +10 global:** cada avanço da peça melhora todas as habilidades ocupadas em +1. O preço agrega o item-base e eventuais itens de marco de cada atributo, nunca uma receita fixa por modelo de arma/armadura. Substituir/adicionar uma habilidade em peça já refinada exige pagar os ranks anteriores dessa habilidade.
  - **Variante B — +40 por peça de quatro atributos:** cada tentativa consome quantidade predeterminada de catalisador do personagem dono e material-base; escolhe aleatoriamente uma habilidade ainda abaixo de +10 e melhora somente ela. Quatro habilidades ocupadas dão teto +40; três dão +30 e uma quinta real (MOD-005) daria +50. Não cobrar tentativa sem atributo elegível.
  - **Auto-status e outras habilidades binárias:** refinamento precisa de bônus lateral definido e testado; não basta exibir “Auto-Haste ×2” ou alterar o bit vanilla. No AP, No Encounters e Aeon Immunities seguem sem efeito universal honesto e bloqueiam uma peça no primeiro escopo.
  - **Interface:** se ativado por tecla F, será um **menu totalmente novo**, não uma linha/submenu de F7/F8/F9; alternativa é integrar profundamente em Customize nativo. Ambas pedem hooks e sidecar por save. [Pesquisa, custos propostos e gates](research/MOD_004_EQUIPMENT_REFINEMENT_2026-09-23.md).
- [ ] Permitir “evoluir” habilidades, como Darktouch para Darkstrike ou HP +10% para HP +20%.
  - A conversa começou com uma correção equivocada de que Darkstrike já podia ser customizada sobre Darktouch. Kari corrigiu: elas acumulam, e considerou o resultado excessivo — até 150% de chance de aplicar Darkness por seis turnos a cada acerto. A proposta de evolução/substituição ficou sem desenho final.

### Local e custos em aberto

**Integração posterior, 27/09/2026:** [Aeon Ascension](<mod-ideas/AEON ASCENSION - MOD 002 004 005.md>) acrescenta duas receitas exclusivas ao Workshop, contadas no MOD-002 para evitar duplicação do backlog. Preserva os gates de Aeon do runtime `main` `f2308ddd`, transações multi-material e Gil ×2 aplicado uma vez. Essas novas habilidades ficam fora do refinamento aleatório/global e das rotas genéricas de Fusion/Customize.

- Kari imaginou que reforjar e fundir (as duas primeiras opções) pudessem ser feitas em qualquer Rin Travel Agency ou loja.
- O custo de expansão de slots tem duas versões na conversa:
  - **Kari:** uma Key Sphere do nível correspondente para cada slot novo — Lv. 1 para slot 1, Lv. 2 para slot 2, Lv. 3 para slot 3 e Lv. 4 para slot 4.
  - **Dawn:** quantidades progressivas — 1 Lv. 1 Key Sphere para slot 1, 2 Lv. 2 para slot 2, 3 Lv. 3 para slot 3 e 4 Lv. 4 para slot 4.
- Ainda falta definir regras de compatibilidade entre tipos de equipamento, como escolher habilidades da peça consumida, a relação com slots já preenchidos e se Clear Spheres continuam necessárias com a fusão.

### Origem no fórum

- **20/03/2026, 20:42 — Kari AP:** publicou as quatro propostas principais.
- **20/03/2026, 20:43 — Kari AP:** sugeriu Rin Travel Agency ou lojas para reforja e fusão.
- **20/03/2026, 20:45 — Dawn Veilwinter:** propôs custos progressivos de Key Spheres para expansão de slots e mencionou equipamento com melhoria até +10.
- **20/03/2026, 20:46–20:52 — ImmortalFork, Dawn Veilwinter e Kari AP:** discutiram evolução de habilidades, esclareceram que versões como Darktouch/Darkstrike acumulam e apontaram o potencial de abuso.
- **24/04/2026, 11:03 — afriendlyirin:** apoiou reforjar para mudar o dono/tipo, pois isso reduziria a aleatoriedade incômoda dos drops de equipamento.

---

## MOD-005 — Quinto slot de auto-habilidade em armas e armaduras

- **Tipo:** expansão do sistema de equipamentos; depende de lógica de engine, interface e persistência.
- **Estado:** ideia registrada; pesquisa RT0 e protótipo isolado Equipment Workshop de operações/sidecar e uma fatia limitada de consumidores. Integração na DLL, cobertura dos demais consumidores, menu do jogo, save callback e RT2 continuam pendentes.
- **Fonte:** pedido direto do usuário em 23/09/2026.
- **Escopo:** uma quinta habilidade customizável **na mesma arma ou armadura**, além das quatro posições vanilla. Não é o quinto item do inventário nem uma terceira peça equipada.

- [ ] Implementar uma quinta auto-habilidade por peça, com efeito real em combate, customização/visualização coerentes e persistência correta após save/load.

**Resultado da pesquisa:** o equipamento vanilla tem 22 bytes e quatro `u16` em `+0x0E/+0x10/+0x12/+0x14`. O suposto quinto em `+0x16` coincide com o próximo registro; no último equipamento, com `PlySave`. A base IDA do `FFX.exe` examinado mostrou laços de quatro no combate/previews/drop, stride 22 no inventário e save, e um cálculo de custo que leria o equipamento seguinte se `slot_count` fosse 5. O FFX Editor atual não oferece um quinto campo. A rota candidata é manter os registros originais intactos e criar uma quinta posição lógica via sidecar por save, com hooks de ciclo de vida, combate e UI. Custo, desbloqueio, regras de drops e alcance para equipamentos especiais ainda precisam de decisão.

**Evidência e escopo reproduzível:** [relatório MOD-005](research/MOD_005_FIFTH_EQUIPMENT_ABILITY_SLOT_2026-09-23.md) e [probe RT0](../research/mod_005_fifth_slot/probe_save_overlap.py). Isto ultrapassa a proposta MOD-004 de expandir equipamentos de menos de quatro posições **até quatro**.

**Continuidade 27/09/2026:** o estado acima registra a pesquisa inicial de 23/09. O runtime de referência `main` `f2308ddd` já contém Workshop/Aeons/quinto lógico mais recentes. [Aeon Ascension](<mod-ideas/AEON ASCENSION - MOD 002 004 005.md>) poderá usar o quinto pago sem aumentar o registro de 22 B; a receita especial é integral, sem o fator 1,5× da receita genérica, e exige os quatro slots nativos preenchidos. Os novos efeitos continuam em planejamento.

---

## MOD-006 — Idiomas textuais adicionais selecionáveis no jogo

- **Tipo:** extensão de idiomas do FFX para Hooks com pacote de tradução independente.
- **Estado:** ideia registrada e escopo técnico inicial; nenhum hook de idioma, opção nativa de menu ou pacote PT-BR implementado.
- **Fonte:** pedido direto do usuário em 27/09/2026.
- **Escopo:** permitir uma nova opção de idioma, por exemplo **Português (Brasil)**, ao lado das opções já existentes. Tradução de texto, menus e legendas; **não inclui dublagem**.

- [ ] Registrar e selecionar um idioma textual adicional no jogo via Hook, carregando seus próprios recursos de texto/fontes e preservando integralmente as opções e arquivos vanilla, com fallback seguro quando o mod ou uma tradução parcial não estiver disponível.

**Caminhos candidatos:** um idioma virtual do mod com opção própria no menu e resolução de recursos `pt-BR → pack PT-BR → fallback vanilla`, ou um novo ID interno de idioma se todas as rotas de menu, arquivos, encoding, fonte, save e áudio forem comprovadas. Alterar o seletor para reaproveitar os arquivos English no lugar deles não entrega o requisito. O Editor deve preparar o pacote PT-BR como opção **`[DERIVADO DE MOD-006]`**, mantendo authoring vanilla disponível separadamente. Ver [pesquisa MOD-006](research/MOD_006_ADDITIONAL_TEXT_LANGUAGE_2026-09-27.md) e [handoff consolidado Hooks→Editor](ai/HOOKS_EDITOR_MODE_BOUNDARY_2026_09_27.md).

---

## MOD-007 — Spira: Elemental Dominion

**Origem:** imagem Discord fornecida pelo usuário em 27/09/2026 (Kari: afinidades em passos de 25%, −100% a 250%, Demi de chefe em 1/16 maxHP; Nuckyduck: MDF negativa experimental; Gabryc: magia de reduzir resistências para ambos os lados). O usuário acrescentou a expansão para 8º/9º/10º elemento e authoring OnlyMod no Editor.

- [ ] Implementar Elemental Dominion em fases: oito bits nativos visíveis simultaneamente; afinidades numéricas de 15 patamares; Imperil/Ward; Gravity por comando/alvo; registro externo para 9º/10º elementos independentes; preview/exportação OnlyMod e contratos de coexistência com Workshop/Nul/Difficulty.

**Adendo do usuário — Magic Break Damage Limit:** magias com BDL podem chegar a **999.999 por hit**; sem BDL continuam em **9.999**. Módulo independente com classificação de magias/Fury, teto final por componente HP e integração ao ponto já usado pelo Hook de Nova. A regra não aumenta automaticamente ataques físicos, cura, MP/CTB ou todos os Overdrives. Detalhes e evidência de bytes estão na seção 5.1 do design, seção 9 da pesquisa e tarefa T5b do plano abaixo. Nenhum novo ID de autoability é necessário para reutilizar o BDL existente.

**Estado pesquisado:** o helper original de afinidade usa os oito bits de um byte. RT1 isolado novo: 403.809 comparações sem divergência, incluindo bits extras e ausência de efeito dos bits `0x100/0x200`. Isso não implementa o MOD-007: nove/dez elementos requerem registro externo e adaptação dos consumidores. A UI corrente mostra quatro básicos + Holy/Darkness/um Custom escolhido entre `0x20/0x40`, explicando sete visíveis. Fórmula 8/potência 1 já representa 1/16 maxHP; condicionar a chefe, controlar imunidade e não letalidade são etapas adicionais.

**Documentos:** [design completo e propostas de expansão](<mod-ideas/MOD 007 - ELEMENTAL DOMINION.md>), [evidência/viabilidade](research/MOD_007_ELEMENT_REGISTRY_FEASIBILITY_2026-09-27.md), [plano por etapa](research/MOD_007_IMPLEMENTATION_PLAN_2026-09-27.md), [fronteira Editor vanilla/OnlyMod](ai/HOOKS_EDITOR_MODE_BOUNDARY_2026_09_27.md).

**Localização:** `/home/wanderson/.codex/worktrees/mod-007-elemental-dominion/ffx-hooks`, branch `codex/mod-007-elemental-dominion-20260927`; runtime de referência `/home/wanderson/Documents/ffx-hooks`, `main` em `f2308ddd`; Editor `/home/wanderson/Documents/ffx-editor-main`, `codexclaudiocodeffxeditor` em `39916423`. Nenhum novo hook/asset/deploy/RT2 nesta pesquisa.

## Modelo para novas ideias

Copie esta seção ao iniciar um novo registro e atribua o próximo ID (`MOD-008`, `HOOK-001` etc.).

### MOD/HOOK-XXX — Nome (ou TBD)

- **Tipo:** mod / hook / ferramenta
- **Estado:** ideia registrada
- **Fonte:** autor, data e link ou canal, se disponíveis.

**Resumo:**

### Objetivos e ideias

- [ ]

### Discussões e decisões em aberto

-

### Próximos pontos para investigar

- [ ]

## Jarvis-HOOK — auditoria técnica de viabilidade (23/09/2026)

### Escopo e significado dos rótulos

Esta auditoria cruza as quatro ideias registradas com a base IDA da VM Windows, o código de FFX Hooks, o checkout local do FFX Editor/Fahrenheit e exemplos de mods no corpus local. É uma análise estática de FFX HD Remaster para PC/Steam. Não houve inicialização do jogo, build, teste, deploy de DLL ou validação RT2. Nenhum checkbox de ideia foi marcado como concluído.

“Dados/Editor” significa alterar registros e arquivos de jogo usando formatos conhecidos, sem nova lógica nativa no jogo. Isso não afirma que o FFX Editor já ofereça um botão público para cada operação: algumas ferramentas existentes são pilotos RT0 ou código local ainda não classificado como baseline público. “Hook” significa comportamento novo em execução, normalmente em FFX.exe, DLL/plugin ou script de evento. “Dados + hook” exige ambos. “Especificação pendente” significa que a conversa ainda não define resultado suficiente para prometer um escopo. **Confiança alta** abaixo significa campo/rotina visto diretamente ou cruzado em duas fontes; **média** significa rota plausível sem prova de todos os consumidores; **pendente** significa regra incompleta ou comportamento que depende de jogo. Todas as rotas de gameplay continuam sem validação RT2.

### Evidências comuns usadas na classificação

Os caminhos relativos do FFX Editor abaixo referem-se ao checkout /home/wanderson/Documents/ffx-editor-main; os de Fahrenheit/Fantasia referem-se aos clones indicados na seção de precedentes.

| Evidência | O que foi confirmado | Consequência para o escopo |
|---|---|---|
| IDA, base C:/IDA_DB/ffxoficial.exe.i64; módulo FFX.exe, origem registrada como D:/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/FFX.exe, ImageBase 0x400000 | A sessão de leitura atual abriu essa base sem análise automática e com Hex-Rays. Os endereços abaixo são flat do IDA; a RVA de PE é o endereço flat menos 0x400000. | Os endereços são evidência para esse binário/base. O hash do executável de entrada não foi coletado nesta auditoria; não generalizar para outra versão/região sem assinatura. |
| FFX_Damage_ComputeHitDamage, IDA 0x78E680 / RVA 0x38E680; src/runtime/FfxHooksDll/shared/ffx_addresses.h e docs/reverse/FFX_DAMAGE_CAP_CLAMP_IDA_2026-06-15.md | A função acumula os estágios de dano e aplica o clamp perto do fim. O teto BDL é escolhido por mov ebx, 99999 em 0x78ED41; em 0x78EDD5 a assinatura de 5 bytes é 7E 02 8B C3 89 (JLE curto, MOV e início da próxima instrução), escrita em 0x78EDD9 e soma ao acumulador em 0x78EDDB. Sem BDL, o teto observado é 9.999; com BDL, 99.999. | Editar command.bin altera poder/fórmula, mas não remove esse clamp. Teto global maior requer patch/hook nessa região e atenção a acumuladores, overkill, cura, variantes de comando e HUD. Conversão visual acima de 99.999 continua sem prova. |
| FFX_Damage_FormulaDispatch, IDA 0x789CB0 / RVA 0x389CB0; FFXProjectEditor/FfxLib/Dictionaries/DamageFormula_Enum.cs | A dispatch tem fórmulas de ID 0 a 23: dano, cura, HP/MP do alvo, defesa ignorada e casos especiais. Não há em command.bin um campo para uma função nova. | Selecionar uma fórmula existente é authoring de dados. Fórmula C# nova, novo uso de atributo, nova curva ou fração diferente requer estender a dispatch e compatibilizar comandos/equipamentos que salvam o ID da fórmula. |
| FFX_Damage_ApplyElementResist, IDA 0x78A420 / RVA 0x38A420; Fahrenheit/src/core/ffx/call_4.g.cs e Fantasia/src/balance/elemental_affinities.cs | O ponto recebe alvo, comando, máscara de elementos e dano. O protótipo formal salvo na base IDA está desatualizado (3 argumentos fastcall); caller e pilha mostram quatro argumentos cdecl, em acordo com Fahrenheit e Fantasia. | Usar caller/pilha e assinatura cruzada, não o tipo desatualizado da base. Fire/Ice/Thunder/Water/Holy são flags 0x01/0x02/0x04/0x08/0x10; Água e Holy já existem. Bits superiores vistos no Editor não provam, por si, semântica completa do motor. |
| FFX_Btl_ClassicMenuCommandUsabilityGate, IDA 0x78ABE0 / RVA 0x38ABE0; FFX_Btl_IsOverdriveReadyMenuFlag, 0x79AF70 | O gate de comando lê o custo OD em command+0x26 e compara com a barra. A disponibilidade/estado do menu de Overdrive também usa um flag separado do ator. | Custo parcial por comando pode ser escrito em dados; exibir o anel/linha de Overdrive com gauge parcial e pintar trecho consumido/faltante exige ajustar gates e renderização do menu. |
| FFX_Battle_ResolveStatusInflictionMatrix, IDA 0x78AEC0, IDA flat 0x78AEC0 / RVA 0x38AEC0, contém 0x78B250 citado na conversa; tabela de status temporal no endereço flat IDA 0xC42457 | O pipeline consulta chance e resistência, e só inicia duração se o slot daquele status estiver vazio. A configuração temporal está no EXE, sem writer atual do Editor. | Chance/duração de uma ação pode ser editada em comando. Renovação, resistência à duração, status persistente ou mudança de Threaten passam pelo motor/tabela do EXE. 0x78B250 é interior da função identificada, não início de função separado. |
| FFX Editor: FFXProjectEditor/FfxLib/Ability/Ability_Command.cs, FfxLib/WeaponGear/WeaponGear_File.cs, FfxLib/Save/FfxSaveEquipment.cs, Tools/MonsterMagicGrowRt0.cs e Tools/CustomizationRt0.cs | O modelo de comando expõe usuário/menu, CostOverdrive u8 em +0x26, fórmula u8 em +0x28, poder u8 em +0x2A, máscara elemental u8 em +0x2D, hits, chance/duração de status e custo MP. weapon.bin tem 153 modelos, registros de 22 bytes e até quatro habilidades/slots. Save Editor conhece registros de inventário de 22 bytes, a partir do offset 17.628, com dono, tipo, capacidade 0–4 e quatro IDs. O piloto monmagic acrescenta uma linha em diretório de saída e relê o arquivo. | Há operações de arquivo adequadas para balanceamento e para preparar uma amostra de equipamento/monstro. Save Editor é edição offline de save; não cria menu de forja em Rin. Piloto que escreve em work/ e round-trip RT0 não prova que o jogo carrega ou executa o arquivo. |
| Contrato público do Editor: docs/release/PUBLIC_MODULE_CATALOG.md | battle-commands-hub e customizations-hub são superfícies públicas, mas subferramentas mantêm gates próprios. O writer público de auto-habilidades se limita ao modelo AU1–AU7/arms_rate; weapon.bin e os pilotos citados não aparecem como promessa pública. Enemy Design/Custom Boss/Difficulty mantém ressalvas de teste; RT0 não equivale RT2. | A auditoria diferencia “há código no checkout” de “feature pública pronta”. |
| Código de FFX Hooks: NovaSuperDamageHook.cpp, GridTeachHook.cpp, KimahriLancetDualGrantHook.cpp, RonsoManaHook.cpp | Há protótipos locais de bypass do clamp para Nova de Kimahri (#115), ensino/menus de comandos Kimahri/Ronso Rage e custo/gate de Overdrive customizado. Esses hooks têm flags/guardas; status histórico de RT2 não foi refeito aqui. | São referências de pontos de integração, não implementação pronta das propostas, nem prova de execução nesta sessão. |
| Corpus externo | A busca lexical cobriu 319 diretórios Git locais. Consultas incluíram DmgCalc_Elem, FFX_Damage_ApplyElementResist, 0x38A420, Quickcast, Fury Overdrive, Geosgaeno, Useful Kimahri e Persistent KO. Foram inspecionados Fantasia, FFX Customizable Battle Tweaks, FFX The Challenge HD e FFXDataParser. | A contagem é cobertura de repositórios, não revisão humana linha a linha de 319 projetos. Resultados negativos da busca não provam inexistência; hits genéricos de guias/simuladores não foram tratados como implementação de mod. Nenhum código externo foi copiado. |

**Proveniência da base IDA:** na primeira leitura desta investigação foi anotado SHA-256 bfaaf2e9ad743de76b3a711353e40b3ac85b282b3bd1dc50a18528fad7affe93 e tamanho 109.288.676 bytes para o .i64. Na leitura mais recente, o mesmo caminho apresentou SHA-256 2db22941692868124ca7dcd035688a035d83b2409dd906b0a0dcf16650af81a1, tamanho 109.481.351 bytes e LastWriteTime 23/09/2026 13:47 (hora da VM). A razão dessa diferença não foi determinada. Nenhuma ferramenta de renomear, mudar tipos/comentários, debugger ou salvar base foi chamada por esta auditoria; a sessão atual foi iniciada com perfil que só expõe leitura. A diferença fica registrada como conflito de proveniência: não salvar/restaurar a base por suposição; comparar uma cópia de segurança antes de qualquer edição futura do IDB.

### MOD-001 — Overhaul de combate

| Proposta | Rota provável | Viabilidade e prova necessária |
|---|---|---|
| IA inimiga mais dinâmica | Dados se a regra couber nos slots, condições e comandos que cada monstro já usa; hook/ATEL para nova condição, memória ou escolha | m###.bin e comandos de monstro suportam rebalancear stats/ações. FFX Customizable Battle Tweaks demonstra alterações de monstros/encontros por dados. Faltam classes e decisões concretas; o Editor não prova autor genérico de IA inédita. |
| Elenco viável no endgame | Principalmente dados: poder/custos/fórmulas de comando, stats/loot de monstros e Sphere Grid. Hook para exceções de personagem ou mecânicas novas | command.bin, m###.bin e tabelas de habilidade cobrem knobs existentes. “Viável” precisa de conteúdo-alvo e métricas; equilíbrio requer playtest. |
| Overdrive próprio de Blue Mage para Kimahri | Dados para clonar/remapear Overdrives existentes se menu, animação e aprendizado aceitarem o formato. Hook para nova seleção/execução, menu, progresso e persistência | KimahriExtendedCommandWriter acrescenta linhas em command.bin num piloto RT0. GridTeachHook ensina comandos de Kimahri e corrige menus; RonsoManaHook investiga custo/gate de OD. São peças relacionadas, não um novo modo de OD Blue Mage. Definir se Lancet/Rage vira skill MP ou OD, aprendizado e save/menu/animação. |
| Habilidades novas de monstros | Dados para adicionar linha em monmagic1/2 e apontar IA existente a um comando representável; dados + assets/hook/DLL para efeito, timeline ou animação inéditos | MonsterMagicGrowWriter/RT0 prova append e readback, não uso no jogo. Linha combina fórmula, flags, nome e animações existentes; efeito ausente na dispatch exige código, animação nova exige asset/ação compatível. |
| Multiplicador de dano condicional | Hook no cálculo de dano, com condição explícita e leitura do estado de alvo/atacante; linha de comando fornece valor base | command.bin guarda fórmula/poder/elemento, não expressão geral como “+X% se Y”. Se a condição virar uma ação separada de valor fixo, pode ser só dado; condição avaliada durante o hit é lógica de engine. |
| Teto maior para balancear arena | Hook/patch no clamp global ou hook condicionado por comando; preservar regras BDL | Teto observado é 9.999/99.999. NovaSuperDamageHook.cpp desvia o clamp só para Nova 0x3073 e é lab, default-off. Valor do autor não foi definido; MOD-002 propõe 99.999 normal e 9.999.999 com BDL. Decidir um contrato compartilhado. Overkill/acumulador/HUD acima de 99.999 são gates separados. |
| Bahamut Holy e Valefor Water | Dados, se a linha do ataque do Aeon aceitar o elemento existente | Holy 0x10 e Water 0x08 entram na rotina de afinidade. A conversa cita Yunalesca como alvo para dano Holy; confirmar resistências/fases nesse encontro. Localizar a linha exata e mudar só o campo elemental; testar absorção, ataques multielementais e apresentação. Não há evidência de que exija novo elemento ou hook. |

### MOD-002 — Lista de mudanças de engine da Kari

A lista mistura correções, balanceamento e recursos novos. Cada patch pode ser separado e desativado; o texto original não define todos os valores nem a plataforma de lançamento.

#### Fórmulas, atributos e dano

| Proposta | Rota provável | Delimitação |
|---|---|---|
| Teto 99.999 normal e 9.999.999 com BDL | Hook/patch de clamp; talvez ajuste de acumulador e exibição | O teto atual é 9.999 sem BDL e 99.999 com BDL. command.bin não muda o imediato. 9.999.999 não está provado para totalização ou HUD; é decisão compartilhada com MOD-001. |
| Bônus de STR/MAG/DEF/MDF vindos de equipamento | Hook na agregação de stats/cálculo, com nova semântica de autoability | Agregador de autoabilities aplica efeitos reconhecidos. Inserir ID não cria um efeito novo para todas as fórmulas. Só usar dados se existir um efeito vanilla com semântica igual. |
| DEF/MDF percentuais como EHP e curva 1/(1 + Defense * 0.05) | Hook na fórmula física/mágica | Alterar DEF de equipamento não altera a curva nem a relação com cura. Definir tabela de balanceamento; +20% DEF não é redução linear de 20%. |
| Shell/proteções não reduzem cura; Protect/Shell/Break −33% dano; Haste −33% CTB e Slow +50% CTB | Hook na aplicação de estados, dano/cura e cálculo CTB; dados para duração/chance existentes | Frações mudam a interpretação do estado. Definir interações com Auto-Phoenix, Zombie, Guard, Break e Aeons. |
| Armor/Mental Break adicionam 25% de dano-base sem mitigação | Hook no dano/defesa ou nova fórmula; dados para comando/chance/duração | Definir ordem com elemental, Protect/Shell, crítico, multi-hit e limite. |
| Fórmula mágica baseada em STR e MDF; fórmula C#; preservar fórmulas; frações HP/MP/CTB de 1/16 para 1/60 | Hook na dispatch e contrato de serialização; Editor requer esquema/autor de novos IDs | Há 24 IDs. A autora descreve reimplementação como mudança difícil de tornar opcional porque ataques dependem dela. Sistema C# exige ID estável e persistência/compatibilidade de mods; command.bin não salva função C#. |
| “COMBO System”: 100%, 110%, +5% | Especificação pendente; possivelmente hook por alvo/ação com parâmetros em dados | O recorte não diz se valores são bônus por hit, sequência, combo de aliados ou escalonamento. Não transformar números em requisito sem contexto. Se depender de hits anteriores, precisa estado de batalha. |
| Auto-Crit e MP-0 consumidos após o comando | Hook no consumo/limpeza dos flags do ator, sincronizado ao fim da ação | Limpar após cada hit quebra multi-hit; limpar no fim do turno talvez permita outros comandos. Definir Hero Drink/Three Stars e troca de personagem. |
| Fraqueza a um elemento dá ×1,25 ao elemento oposto | Hook na afinidade; dados para flags de cada ação | Depende de afinidade do alvo e par elemental. Definir pares, multielemental, absorção e cura. |
| Armas por outro atributo; garras de Rikku por Agility; Attack Items por MAG | Hook em fórmula por comando/arma; dados escolhem IDs/flags após extensão | A dispatch atual tem fórmulas fixas. Exige curva de stat e reequilíbrio; AGI já alimenta outras mecânicas. |
| MAG atual, Focus e MP fortalecem magia; faixas +0 a +10; MP-0 limita bônus a +3 | Hook no cálculo mágico; dados associam ações | Regra usa MP/Focus no instante do hit. Definir truncamento, arredondamento, aplicação a inimigos e interação com Magic Booster/dreno. |
| Magic Booster não altera custo/efeito de White Magic sem dano/cura | Hook/condição no caminho do booster; dados identificam categoria de magia | Relato de bug de 02/11/2025. Filtrar pelo efeito real, não só por White/Black. |
| Caps de stats 100, HP 10.000 ou diferentes por personagem | Hook nos caminhos de crescimento, equipamento e aplicação de stats; reduzir dados do Sphere Grid | Mudar nós não impede itens/autoabilities/save de ultrapassarem o teto. Editor pode editar save offline; enforcement precisa cobrir todas as fontes. |
| Accuracy/Luck, meio-acerto com esquiva atrasada e crítico baseado em Accuracy/Luck | Hook em hit/crítico e animação/UI; dados de monstros depois de fixar fórmula | IDA separa rotinas de hit e crítico. O resolver atual usa Accuracy/Evasion/Luck e RNG. A proposta também debate deixar Luck só para crítico ou incorporá-la diretamente na fórmula de accuracy em vez de somá-la como incremento plano; half-miss muda resultado e apresentação. Rebalancear m### é etapa posterior. |
| Corrigir ações que deveriam acertar sempre mas erram | Inventariar ID; dado se taxa errada está no registro, hook se for resolver | Sem lista de comandos/casos e reprodução não dá para classificar. |
| Patch de 4 GB | Compatibilidade/empacotamento do EXE ou launcher, fora do balanceamento | A pergunta aparece sem decisão nem plataforma. Especificar se é Large Address Aware ou outro patch antes de agrupar. |
| Multiplicadores 100%, 110%, +5% para golpes simples/múltiplos | Especificação pendente; dado por ação ou hook geral depois | O recorte não associa cada número a regra. Ajustar linhas específicas evita mudança global; regra dinâmica exige hook. |

#### Status, comandos, Overdrives e troca

| Proposta | Rota provável | Delimitação |
|---|---|---|
| Reaplicar status renova duração | Hook no resolvedor/writer de duração; dados de chance/duração por ação | O pipeline pula a nova duração quando o slot já está preenchido. Duração maior em command.bin afeta primeira aplicação, não prova refresh. |
| Sleep acorda no turno; Poison dura N turnos | Hook/transição de status; configuração do EXE e dados de comando | Sleep temporal e Poison usam configuração diferente. Definir troca, fim de batalha, Aeon e KO. |
| Resistência à duração separada da chance | Hook na aplicação de duração e novo campo/tabela por monstro; dados para preenchê-lo | O campo de duração do comando existe; não foi provado campo de duração-resist em monstro. Não inventar offset. |
| Threaten uma vez por inimigo | Hook/alteração na decadência do Threaten e estado por batalha | Kari aponta 0x78B250 e sugere zerar a decadência após a primeira aplicação; na IDA atual esse endereço cai dentro de FFX_Battle_ResolveStatusInflictionMatrix em 0x78AEC0. Confirmar instruções/caller e semântica por alvo antes do patch. |
| Quickcast no lugar de Doublecast; White Magic no submenu; custo 2× e rank 2 | Dados se houver ações/ranks prontos; hook de seleção/execução para converter magia escolhida | MOD-002 propõe qualquer Black Magic como rank 2; MOD-003 fala em rank reduzido e mínimo não definido. Não são a mesma especificação. Não foi identificada coluna que faça o mapeamento dinâmico. |
| Overdrive parcial | Dados no custo command+0x26; hook para menu/anel se OD só aparecer cheio | O gate entende custo parcial; flag de disponibilidade de menu é separado. O indicador de segmento branco/vermelho exige renderização. |
| Troca consumir turno (opcional) | Hook no fluxo switch/CTB, se mudar Rank não bastar; opção configurável | Mapear Switch até o avanço de CTB. A conversa preserva a troca livre como parte do vanilla e quer a opção para experimento. |
| Ejetado/estilhaçado substituído pelo primeiro da retaguarda | Hook no evento de remoção/troca; checar elegibilidade e permissão | Formação cobre roster inicial, não substituição dinâmica. Respeitar áreas de natação e batalhas sem troca. |
| Guaranteed AP: AP para todos os disponíveis, até KO, respeitando No AP/Double/Triple/Overdrive→AP | Hook no fechamento/distribuição de AP com roster e bônus de arma | A conversa relata falhas ao simular participação em lutas consecutivas e casos Sin’s Fin/Echuilles/Gui. Distinguir “disponível” de oculto/bloqueado e preservar multiplicadores. |
| CTB/summon de 04/11/2025 | Sem feature definida | O trecho relata CTB preservado fora de campo e Aeons normalmente com turno imediato, com exceções. Manter contexto; não há pedido inequívoco de mudança. |

#### Auto-habilidades propostas

Inserir linha em a_ability.bin basta apenas quando a engine já possui o efeito desejado. O catálogo público limita o writer a AU1–AU7/arms_rate; comportamento especial precisa de ID/handler e lógica de execução.

| Auto-habilidade | Rota provável | Delimitação |
|---|---|---|
| Hero’s Bravery: +25% de crítico causado e recebido | Hook no resolver de crítico e nova autoability | Efeito direcional; testar Luck, armas, Hero Drink e críticos recebidos. |
| Element Boost ×1,2 | Dados se autoability existente tiver semântica igual; senão hook de afinidade + novo efeito | Distinguir dano/cura e acúmulo com fraqueza/absorção. |
| Energy Boost ×1,25 acima de 50%; Energy Burst ×1,4 acima de 75%, aditivo até ×1,65 | Hook após cálculo base, gauge no instante definido; dado para equipar | “Permanece acima” evita mudar o preview durante a ação. Dawn depois questiona multiplicadores por min-max/cheese; balanceamento controverso. |
| Efficiency: custos MP/OD −25%, com Half MP Cost | Hook em cálculo/cobrança/preview ou campo existente se custo estático | Definir arredondamento, mínimo e interação com OD parcial; atualizar o menu. |
| Vampirism: ao matar, cura 25% do HP máximo do inimigo, ignora Zombie | Hook no aftermath de kill usando evento Slayer | Respeitar overkill, alvos múltiplos, morte por status e Zombie. |
| Follow Up: aliado dá dano HP em alvo único e dono ataca o mesmo | Hook próximo a Counterattack/Magic Counter, como sugerido no fórum | O ponto de counter é hipótese de integração. Evitar recursão, cadeia de counters, alvo morto e multi-hit. |
| P-Trade/M-Trade: ×0,8/×1,2 entre dano físico e mágico | Hook no dano recebido ou buff vanilla comprovadamente equivalente | Definir dano fixo, Reflect, cura e acúmulo. DEF/MDF simples não garante a regra. |
| Hero’s Caution | Especificação pendente + hook de crítico | A descrição repete “nunca causaria crítico aleatório”. Decidir se protege o usuário ou impede críticos causados. Hero Drink seria exceção garantida. |
| MP Regen: 2% do MP máximo no início do turno | Hook no evento de turno/CTB e nova autoability | Decidir troca de personagem, personagens fora de campo e modos OD. |
| Elude: +50 Evasion ao defender | Dados se efeito vanilla equivalente existir; senão hook de Guard e acerto | Condicional ao estado Defend; não é bônus permanente de EVA. |
| Energy Wall ×0,8 acima de 50%; Energy Barrier ×0,7 acima de 75%, juntas até ×0,5 | Hook de mitigação, leitura de gauge e autoability | Excluir cura e fórmulas fixas/fracionárias como descrito. Definir ordem com Protect/Defense e perda de gauge durante a ação. |
| Habilidades ativas da arma/armadura em batalha | Dados se comando existente for selecionável; hook de menu, alvo, custo e dispatch se autoability for passiva | Precisa ligar ação à peça equipada e atualizar menu quando troca/des equipa. |
| Custo OD parcial e indicador branco/vermelho para habilidade de equipamento | Campo de custo se comando suportar; hook de menu/gate/renderização | Campo CostOverdrive existe; ação ativa da peça e desenho do segmento não vêm automaticamente com ele. |

#### Escopo e separação do pacote

A conversa separa bug objetivo de preferência de balanceamento e sugere patches gerais reutilizáveis no Fahrenheit. Dividir em módulos pequenos e configuráveis: (a) bug reproduzível; (b) opção de balanceamento; (c) expansão. Patch de 4 GB aguarda identificação de artefato/plataforma. Nomes personalizados de equipamento foram destinados pela Kari a EFP. Fragmentos de CTB/summon não viram requisito sem um pedido claro.

### MOD-003 — Fantasia

O clone EvelynTSMG_ffx-mods-fantasia está no commit 64b03ae915c32f47d4a84aefffcd3130cb13ddce, mesmo HEAD retornado por git ls-remote em 23/09/2026. Tem licença MIT; nenhuma release publicada foi encontrada nesta conferência. A única patch de gameplay implementada em source é afinidade elemental; os outros grupos estão como scaffolds. O [X] do post e o anúncio do framework não provam release nem validação no jogo.

| Proposta | Rota provável | Delimitação |
|---|---|---|
| Rebalanced Elemental Affinities | Hook Fahrenheit em FFX_Damage_ApplyElementResist (FFX.exe + RVA 0x38A420) | src/balance/elemental_affinities.cs tem delegado cdecl de quatro args. DEFAULT delega ao vanilla; FAVORABLE, BALANCED, UNFAVORABLE e EXTRA_MEAN diferem. BALANCED divide dano igualmente por elemento, não apenas soma todas as afinidades. Padrão no source é BALANCED. Protótipo concreto; sem release/RT2 confirmados. |
| Useful Kimahri: começar com Fire/Blizzard/Thunder/Water/Esuna/Cure/Dark Attack/Power Break/Cheer | Dados para grade/estado inicial; hook se toggle de New Game conceder habilidades | Comandos existentes podem ser referenciados, mas concessão no novo jogo precisa de estado inicial/salvamento. GridTeachHook aprende ao ativar nó; não é concessão inicial pronta. |
| Três Sphere Levels configuráveis por Sphere Grid | Hook ou estado/save inicial | Alterar nível no save offline não entrega opção de New Game por grade. Mapear armazenamento das grades compartilhadas. |
| Mix Overhaul | Especificação pendente; tabela se reutilizar receitas/efeitos; hook para novas regras | Sem entradas, saídas, custo ou regra definida. Editor lista prepare.bin/Mix, sem prometer engine de composição nova. |
| Water Aeon: Geosgaeno no lugar de Anima | Substituir dados/assets se modelo, rig, animação e contrato de Aeon forem compatíveis; caso contrário conversão e/ou hook | Diferente de Valefor Water. Não há prova de rig/animação de Aeon compatível. Primeiro verificar asset, esqueleto, summon menu, slot e Overdrive. |
| Doublecast incluir White Magic | Dados se houver filtro de categoria comprovado; senão hook de menu/seleção/execução | Não foi identificada coluna de command.bin que expanda Doublecast. Testar cura, revive, buffs e ações não-magia. |
| Zerar OD no início de chefes | ATEL por encontro se opcode existir; alternativa hook de criação do encontro | Battle Tweaks injeta reset em scripts de lutas da Arena. Isso não prova writer geral do Editor nem script compartilhado por todos os chefes. |
| Useful Items (itens de Customize passam a ter uso) | Dados em item/command se efeito e ação Use existem; hook para nova ação/efeito/filtro | Primeiro listar item IDs e cenário. Sem lista não se sabe se é liberar ação ou programar efeito. |
| Raios na Thunder Plains causam dano configurável ao Tidus/frontline/grupo, mínimo 1 HP | Evento ATEL se houver opcode; senão hook de campo/raio e alteração de HP | Mecânica de campo, fora do dano de batalha. Definir se mata, atinge retaguarda/Aeons e interage com minigame. |
| Quickcast | Dados se rank 2 equivalente existir; hook para mapear magia selecionada e dobrar custo | Escolher rank metade com mínimo 2 ou rank fixo 2. É distinto do Quickcast MOD-002. |
| Fury de Lulu usa todo MP para fortalecer magia e adiciona 4 MP por rotação | Hook em seleção/execução Fury, consumo MP e dano; opções via settings | O texto oferece políticas diferentes para Spellspring/One/Half MP Cost: multiplicar MP 4×/3×/2×, usar MP máximo/máximo−1/metade, ou tratar gasto como 0/1/metade do atual e enfraquecer magia. Decidir custo efetivo, insuficiência de MP, rotação e fórmula. |
| Yuna só invoca por Grand Summon; Summon normal removido | Dados se flags/menu suportarem; hook para gate/ação se não | Separar remoção visual e disponibilidade real. Grand Summon sem OD é regra adicional que altera cobrança/Overdrive; testar barra e animação. |
| Status persistentes | Hook/configuração EXE após definir semântica | Especificar quais, duração, saída e comportamento entre batalha/save. |
| KO persistente | Hook em fim de batalha e estado salvo | Definir se atravessa encontros/mapa/save, e como Inn/cura/revive removem KO. |
| Fire Eater gera OD em vez de HP | Hook em absorção elemental e fluxo de OD; dado para equipar | Definir gauge por hit, limite e interação com cálculo atual de OD. |
| Ícones de participação, status e chefes derrotados da Arena | Hook/UI e leitura de estado batalha/save | FFX_BtlUI_HudStatusIcon indica infraestrutura de ícones de status; não prova que o layout atual mostra a visão solicitada. |
| HP/MP de Aeons sem abrir Summon | Hook/UI/HUD | Definir posição, se mostra Aeons não invocados e comportamento de Aeon ativo/armazenado. |
| Encontros no Sphere Monitor; adicionar mais encontros | Dados para slots/formações suportados; hook/UI/save para novas entradas e desbloqueio | Editor/corpus cobrem encontros e formações. Acrescentar seleção requer menu e flags; limite de slots ainda não identificado. |
| Instant suitcase potions | Especificação pendente e depois hook de item/UI ou script | “Instant” pode significar sem animação, sem seleção de alvo ou uso automático; o recorte não define. |
| Preview de turnos após Phoenix Down/Mega-Phoenix/Life/Full Life | Hook/UI que simule fila CTB após revive | Recalcular ordem antes de confirmar sem avançar RNG/estado. O arquivo do item não desenha a previsão. |
| Butterflies: vencer todos para liberar o próximo minigame | Evento ATEL/flags se condições existentes bastarem; senão hook de progressão | Enumerar trechos e flags; impedir que a etapa seguinte libere antes de completar a anterior. |
| Não salvar trocas de formação/equipamento feitas em batalha | Hook no commit ou save; guardar estado pré-batalha se for reversão | Definir se reverte ao fim ou apenas não persiste no save. Cobrir batalhas consecutivas e todos os tipos de encontro. |
| Renomear Soft para Golden Needle e outros itens | Arquivos de texto/localização, mantendo IDs e efeito | Atualizar nomes/descrições em idiomas alvo e respeitar limites da UI. |

### MOD-004 — Reforja e customização de equipamentos

| Proposta | Rota provável | Delimitação |
|---|---|---|
| Reforjar: trocar dono e categoria | Offline: weapon.bin/save. Em jogo: dados + menu/hook em Rin ou loja | weapon.bin tem dono e flag arma/armadura por template. Save tem dono em +4, tipo em +5, índice/aparência e habilidades. Mudar só dono pode deixar índice/modelo incompatível. Rin requer menu, custo, validação, inventário e transação. |
| Fundir peça consumida à base, transferindo até duas habilidades | Offline: copiar IDs nos quatro slots. Em jogo: menu/hook e atualização transacional dos registros | Quatro slots já cabem no formato; não exige ampliar save. Definir origem/destino, habilidades escolhíveis, duplicatas, slot sobrescrito/vazio, item equipado, preço e rollback. Brotherhood/Celestial como destinos exclusivos requer identificação confiável do item. |
| Expandir slots até quatro | Ajustar slotCount/template ou capacidade no save; Customize in-game requer menu/hook e consumir Key Spheres | Layout já suporta quatro. Acima de quatro requer formato/runtime novos. Custo em conflito: Kari propõe uma esfera do nível correspondente por slot; Dawn propõe 1/2/3/4 esferas dos níveis correspondentes. Guardar como variantes em aberto. |
| Clear Sphere apaga habilidade | Offline: limpar ID. Em jogo: menu Customize/hook, atualizar item e consumir item | Vanilla Clear Sphere apaga nó do Sphere Grid, não autoability de arma. Fusão pode sobrescrever, mas não cobre apagar livremente qualquer slot. |
| Refino global +10 ou aleatório por atributo até +40/+50; drops pré-refinados | Hook de inventário/efeito/save mais sidecar; menu novo por tecla F ou integração própria no Customize vanilla | Custos e transições A/B têm modelo RT0, mas efeitos de auto-status, 3 exceções, novos catalisadores por personagem e persistência ainda pedem especificação/RT1/RT2. Formato de 22 B não possui rank; `kaizou.bin` não representa materiais agregados ou sorteio. Ver [relatório de refino](research/MOD_004_EQUIPMENT_REFINEMENT_2026-09-23.md). |
| Evoluir Darktouch→Darkstrike ou HP+10%→+20% | Dados se só selecionar ID final; transformação in-game exige receita/custo/substituição | A conversa corrige que Darktouch/Darkstrike podem acumular. Evolução não pode ser só adicionar o superior; definir se substitui/consome e materiais. Rever interações potencialmente excessivas. |

#### Precedentes externos e limites de reutilização

- Fantasia: https://github.com/EvelynTSMG/ffx-mods-fantasia. Clone MIT no commit citado. Patch elemental é precedente de hook Fahrenheit e coincide com RVA/ABI cruzados; código não copiado.
- FFX Customizable Battle Tweaks: clone local lawhsia_ffx-customizable-battle-tweaks, commit cb48450850e5. README/scripts alteram m###.bin, monmagic*.bin, a_ability.bin, lojas, formações e eventos ATEL; exemplos incluem reset de OD em eventos específicos e loot/armas de captura. Não encontrei LICENSE no nível superior; leitura comparativa, sem adaptação.
- FFX The Challenge HD: clone local lawhsia_pbirdman-FFX-The-Challenge-HD-v2.6F, commit dd33b940a708. README documenta alterações de dados/AI/formação e limites de alterações só no EXE. LICENSE não encontrada no nível superior; sem reutilização.
- FFXDataParser: clone local EvelynTSMG_FFXDataParser, commit 13607aec9bd0; referência de formatos, sem LICENSE encontrada no nível superior; sem código adaptado.
- Fahrenheit: checkout local main em c149c847b3a2, licença LGPL-3.0-or-later; fornece FhMethodHandle, FhSetting e tipos FFX usados pelo patch Fantasia. Usado para compreender arquitetura/ABI, não como fonte copiada para o backlog.

### Sequência sugerida para implementação futura

1. Fixar alvo: versão exata de FFX HD Steam, EXE, formato de instalação e escolha entre Fahrenheit/Fantasia e FFX Hooks.
2. Congelar regras incompletas: fórmula, ordem, duração, custo, persistência e UI. Propostas incompatíveis viram opções distintas.
3. Preparar dados em staging com readback e preservação de bytes não modelados. Usar command.bin, monmagic, m###, itens/lojas, Sphere Grid, texto ou weapon.bin apenas para campos com semântica confirmada.
4. Projetar cada hook com assinatura/perfil do EXE, validação de bytes, default-off, original preservado, remoção idempotente e escopo de thread. Separar cálculo, menu/HUD e save.
5. Planejar validação por fase: RT0 de arquivos, harness isolado quando houver e RT2 no jogo após autorização específica. Esta auditoria não promove patch para uso/produção.


## MOD-008 — Spira: Arcana of the Fayth

Jarvis-HOOK, 2026-09-28: [identidade e 78 cartas](<mod-ideas/MOD 008 - ARCANA OF THE FAYTH.md>) preservadas em candidato nativo do menu principal **Equip**. Twin/Constellation, posse única, save separado, efeitos tipados e aquisição por templos/sidequests/desafios; opção independente de Desenvolvimento para obter as 78 instantaneamente, OFF por padrão. [Evidências e limites](research/MOD_008_RUNTIME_2026-09-28.md): RT0/RT1 e builds concluídos, sem deploy/RT2, revisão do autor.
