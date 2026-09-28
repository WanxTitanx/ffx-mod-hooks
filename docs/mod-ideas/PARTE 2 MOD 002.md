# PARTE 2 MOD 002 — Habilidades de armas e armaduras

As propostas abaixo foram apresentadas como auto-habilidades de equipamento. Algumas condições e interações são ideias iniciais, não regras fechadas. [Voltar à parte principal](<MOD 002.md>).

## Registros criados — atualização de 27/09/2026

**Criação local concluída:** os13 registros anteriores135–147 foram preservados e mais **27 auto-habilidades148–174** foram criadas em Steam, FFX Extracted e Spira Reforge. As tabelas agora têm175 linhas. [Relatório de criação, backup e verificações](../research/AUTOABILITY_SPIRA_AEON_CREATION_2026-09-27.md) · [registro único dos40 IDs](../../research/autoability_expansion/registry.json).

**Referência atual de IDs:** a tabela abaixo prevalece sobre propostas históricas de reaproveitar13/23/24/129/130. As linhas antigas permanecem intactas. Efeitos `hook_only` estão neutros; nomes/descrições não substituem a implementação de runtime.

| ID | Palavra | Nome | Estado dos dados |
|---:|---|---|---|
| 148 | `0x8094` | Aeon Break HP/MP Limit | Neutra; Hook/efeito pendente |
| 149 | `0x8095` | Aeon Break Damage Limit | Neutra; Hook/efeito pendente |
| 150 | `0x8096` | Mana Spring | Neutra; Hook/efeito pendente |
| 151 | `0x8097` | Break Limits | Campos nativos configurados |
| 152 | `0x8098` | Devil's Bargain | Neutra; Hook/efeito pendente |
| 153 | `0x8099` | Warden's Oath | Neutra; Hook/efeito pendente |
| 154 | `0x809A` | Arcane Focus | Neutra; Hook/efeito pendente |
| 155 | `0x809B` | Double Drop | Marcador para Hook de Drop existente |
| 156 | `0x809C` | Triple Drop | Marcador para Hook de Drop existente |
| 157 | `0x809D` | Element Eater | Campos nativos configurados |
| 158 | `0x809E` | HPMP +10% | Campos nativos configurados |
| 159 | `0x809F` | HPMP +20% | Campos nativos configurados |
| 160 | `0x80A0` | HPMP +40% | Campos nativos configurados |
| 161 | `0x80A1` | HPMP +60% | Campos nativos configurados |
| 162 | `0x80A2` | AIO +3% | Campos nativos configurados |
| 163 | `0x80A3` | AIO +6% | Campos nativos configurados |
| 164 | `0x80A4` | AIO +9% | Campos nativos configurados |
| 165 | `0x80A5` | AIO +12% | Campos nativos configurados |
| 166 | `0x80A6` | STR MAG +10% | Campos nativos configurados |
| 167 | `0x80A7` | STR MAG +20% | Campos nativos configurados |
| 168 | `0x80A8` | DEF MDEF +10% | Campos nativos configurados |
| 169 | `0x80A9` | DEF MDEF +20% | Campos nativos configurados |
| 170 | `0x80AA` | Foolstrike | Neutra; Hook/efeito pendente |
| 171 | `0x80AB` | Fooltouch | Neutra; Hook/efeito pendente |
| 172 | `0x80AC` | Fourstrike | 4 elementos nativos; estados extras pendentes |
| 173 | `0x80AD` | Fourtouch | 4 elementos nativos; estados extras pendentes |
| 174 | `0x80AE` | Spell Spring | Neutra; Hook/efeito pendente |

## Armas

- [ ] **Hero's Bravery:** +25% de chance de causar crítico e +25% de chance de receber crítico.
- [ ] **Energy Boost (bônus elemental):** a anotação propõe dano/cura elemental x1,2 e relaciona o efeito à barra de Overdrive acima de 50%; a sintaxe original está incompleta e deve ser confirmada.
- [ ] **Energy Burst:** dano e cura x1,4 enquanto a barra de Overdrive permanece acima de 75%; a proposta diz que acumula aditivamente com Energy Boost, chegando a x1,65.
- [ ] **Efficiency:** reduzir em 25% custos de MP e Overdrive; combinada com Half MP Cost, a redução de custo de MP chegaria a 75%.
- [ ] **Vampirism:** após toda ação ofensiva, curar o usuário em **2% do dano total de HP que ele causou**. Somar os acertos e alvos da ação; não exigir morte do inimigo. O Hook deverá fechar arredondamento, overkill e interação com Zombie antes de executar o efeito.
- [ ] **Assist Attack / Follow Up:** atacar automaticamente o mesmo alvo quando um aliado usar um ataque de HP de alvo único. Dawn prefere o nome Follow Up.

## Armaduras

- [ ] **P-Trade / M-Trade:** trocar mitigação entre dano físico e mágico; P-Trade recebe dano físico x0,8 e mágico x1,2, e M-Trade faz o inverso.
- [ ] **Hero's Caution:** descrição copiada diz que nunca causaria crítico aleatório, mas repete a mesma frase para o efeito negativo; confirmar o comportamento pretendido. Hero Drink seria uma exceção e garantiria crítico.
- [ ] **MP Regen:** recuperar 2% do MP máximo no começo do turno; a anotação considera reaproveitar o ponto de hook do booster F2, cuidando de trocas de personagem e modos de Overdrive.
- [ ] **Elude:** +50 de Evasion ao defender.
- [ ] **Energy Wall:** dano recebido x0,8 enquanto a barra de Overdrive permanece acima de 50%; não afetaria cura nem fórmulas de dano fixo fracionário.
- [ ] **Energy Barrier:** dano recebido x0,7 enquanto a barra permanece acima de 75%; acumularia aditivamente com Energy Wall até x0,5.
- [ ] Disponibilizar em combate as habilidades ativas da arma ou armadura enquanto a peça estiver equipada.
- [ ] Dar às habilidades de equipamento custo parcial de Overdrive e mostrar visualmente a parte consumida em branco e a parte que falta em vermelho. A autora esclareceu que pensava em habilidades multiplicadoras, não em habilidades que gastam grandes partes da barra.

## Integração MOD-002/004/005 — Aeon Ascension

Pedido adicional do usuário: duas auto-habilidades **exclusivas de Aeons aliados**, aplicadas **somente pelo Workshop**. Especificação, receitas, pontos de Hook e handoff: [Aeon Ascension](<AEON ASCENSION - MOD 002 004 005.md>).

- [ ] **Aeon Break HP/MP Limit (armadura):** teto de HP **999.999**, MP **9.999**. Receita proposta: **60 Wings to Discovery + 60 Three Stars + 30 Underdog's Secret + 2 Master Spheres + 10.000.000 Gil finais**.
- [ ] **Aeon Break Damage Limit (arma):** até **999.999 de dano por hit** para ataques físicos, magias e Overdrives numéricos do Aeon. Receita proposta: **99 Dark Matter + 30 Winning Formula + 20 Gambler's Spirit + 3 Master Spheres + 15.000.000 Gil finais**.

As receitas partem dos dados nativos: BHP=30 Wings to Discovery, BMP=30 Three Stars, BDL=60 Dark Matter. Materiais adicionais/Gil são proposta de balanceamento. O ×2 de Gil do Workshop Aeon já está incluído nos preços; não cobrar novamente nem aplicar 1,5× aos 99 Dark Matter no quinto slot. Quinta posição continua exigindo desbloqueio e quatro posições nativas preenchidas.

Chaves `aeon_break_hp_mp_limit` / `aeon_break_damage_limit`, agora nos IDs **148/149**, palavras **0x8094/0x8095**, como linhas nativas neutras criadas. A receita/aplicação Workshop e os efeitos ainda dependem da implementação. A tabela135–147 abaixo continua sendo a aplicação anterior, preservada. Os novos efeitos usarão o mesmo princípio de remapeamento por chave, payload vanilla neutro, gate de owner/peça/progressão e proveniência da compra no Workshop. Não afetam humanos/Dark Aeons inimigos; não são refináveis; nunca removem Aeon Immunity `0x807B`.

Local do adendo: `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, branch `codex/aeon-exclusive-breaks-plan-20260927`, base documental `39ceb198`; runtime de referência `/home/wanderson/Documents/ffx-hooks`, `main` `f2308ddd`. O código desta branch documental é anterior ao runtime de referência; usar o checkout atual da lane para implementar.

## Spira Reforge — auto-habilidades novas e reescritas

**Adendo Jarvis-HOOK, 27/09/2026.** Esta seção registra o levantamento anterior à criação aditiva148–174; os IDs efetivos estão na tabela inicial. Reúne o design do Spira Reforge com os dados anteriores e o source. **ID proposto, nome no Editor, linha binária e efeito runtime são estados diferentes.** As fontes e os campos amostrados estão em [snapshot de evidência](../../research/spira_autoabilities/catalog_snapshot.json).

**Onde continuar:** este documento está em `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, branch `codex/aeon-exclusive-breaks-plan-20260927`, sobre o checkpoint `3b010845ece1d5f927485eae0cf67f04a46c97b2`. Fontes Spira em `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge`; Editor na branch `codexclaudiocodeffxeditor`, HEAD consultado `399164236638bd34d44c4833b02ea3b15a49d771`. Runtime consultado em `/home/wanderson/Documents/ffx-hooks`, `main` `f2308dddc1833811899c0999c96b5ee25a18bd66`. A branch documental herda source antigo; implementar sobre a lane runtime atual.

### Catálogo histórico de efeitos e propostas (anterior à criação aditiva)

| Habilidade/família | ID e palavra de equipamento | Efeito pretendido no Spira | Exclusividade | Estado conferido |
|---|---|---|---|---|
| **Mana Spring** | Proposto **13 / `0x800D`**, substituindo One MP Cost no pack | Recuperar **5 MP fixos por turno de batalha** enquanto equipada | Sem restrição por personagem definida | Roadmap diz pendente. Linha US atual ainda se chama One MP Cost e mantém seu bit nativo. Não localizei handler dedicado de Mana Spring no runtime main; o nome também existe como item e não prova autoability. |
| **Break Limits** | Proposto **23 / `0x8017`** | Combinar Break HP + MP: tetos nativos **99.999 HP / 9.999 MP**, flags `+0x64 = 0x0600` | Geral; o bônus exclusivo de Auron é uma camada separada abaixo | Nas duas tabelas amostradas, a linha23 ainda tem `0x0200`, sem o bit MP. Não está materializado como combinação nesse snapshot. |
| **Devil's Bargain** | Proposto **24 / `0x8018`**, substituindo Break MP Limit no pack | **+50% de dano causado e +50% de dano recebido**; x1,5 em cada direção | Sem restrição por personagem definida; a fonte cita sinergias com Auron e Lulu | Ainda proposta de efeito via Hook. Linha24 atual conserva `0x0400` de Break MP Limit; não localizei handler dedicado no runtime main. Não chamar de x2. |
| **Double Drop** | **129 / `0x8081`** no catálogo/contrato | Multiplicar por **2** a quantidade de itens dos caminhos de recompensa de batalha admitidos | Party MAX; não é exclusiva de Rikku | `DoubleTripleDropHook.cpp` existe; espera bit `0x1000` no word `+0x64`. As linhas129 amostradas não têm esse bit. Roadmap registra teste in-game anterior, que não valida estes bytes atuais. |
| **Triple Drop** | **130 / `0x8082`** no catálogo/contrato | Quantidade de itens **x3**; prevalece sobre Double, sem x6 nem soma por personagem | Party MAX; não é exclusiva de Rikku | Mesmo Hook; bit esperado `0x2000`. Linha130 está neutra nas amostras atuais. Alinhamento entre pacote/registro/Hook precisa ser confirmado. |
| **Element Eater / mini-Celestial custom** | **134 / `0x8086`** | Candidato histórico de alternativa elemental às Celestials | Distribuição histórica em cinco templates de armas de **Tidus**; exclusividade runtime não demonstrada | Linha existe. Nome US atual é **Ribbon**, mas seu payload difere da Ribbon vanilla128: absorção Fire, bits Auto-Shell/Protect/Reflect/Regen/Haste e Auto-Life; não assumir só Fire Eater nem imunidade Ribbon pelo nome. |
| **HPMP +10/+20/+40/+60%** | **114–117 / `0x8072..0x8075`** | Uma habilidade aumenta **HP e MP** juntos, nos quatro tiers indicados | Sem restrição por personagem definida | Intenção e export histórico confirmados. Snapshot atual diverge por idioma/árvore: ver quadro abaixo. Não marcar estes percentuais como reconciliados. |
| **AIO +3/+6/+9/+12%** | **118–121 / `0x8076..0x8079`** | Aumentar **HP, MP, STR, MAG, DEF e MDEF**; não inclui automaticamente Agility/Luck/Evasion/Accuracy | Sem restrição por personagem definida | Mesmo conflito entre design/export e dados atuais. Máscara proposta `0x3F00`; percentuais precisam de alinhamento. |
| **STR+MAG +10/+20%** | **100/101/104/105**, palavras `0x8064/65/68/69` | Reaproveitar tiers altos de Strength/Magic para bônus conjunto | Sem restrição definida | Rebalance documentado no export histórico; os IDs continuam distintos, sem fundi-los automaticamente. Conferir arquivo/idioma antes de aplicar. |
| **DEF+MDEF +10/+20%** | **108/109/112/113**, palavras `0x806C/6D/70/71` | Bônus conjunto defensivo nos tiers altos | Sem restrição definida | Rebalance histórico, dependente de payload/idioma; não é nova linha de ID. |
| **Foolstrike / Fooltouch** | **46/47 / `0x802E/2F`**; antigos Deathstrike/Deathtouch | Trocar o papel de instant-death por pacote de estados | Sem restrição definida | Nomes de design/INI e rework no catálogo histórico. Os offsets de status desse export precisam de conferência no parser atual; não importar seus rótulos de chance/duração como contrato pronto. |
| **Fourstrike / Fourtouch** | **54/55 / `0x8036/37`**; antigos Stonestrike/Stonetouch | Strike dos quatro elementos básicos (`0x0F`) e pacote adicional de estados, reduzindo a dependência de petrificação | Sem restrição definida | Rework histórico; `0x0F` aparece no JP atual, enquanto o US amostrado mantém a forma vanilla. Exige reconciliação do pack. |
| **Strikes/touches e Piercing revisados** | Piercing11; famílias30/34/38/42/50/51/58/59/62/63/66/67/70/71/74/75 | Bônus de STR, estados/durações e riders elementais conforme design; Piercing tinha proposta de +8% STR | Sem restrição definida | Variantes de habilidades existentes. O export antigo não substitui a comparação atual de payload. Preservar a lista completa na fonte do catálogo, em vez de declarar que todos os valores já estão ativos. |
| **Auto-Reflect e SOS combinados** | **88–97 / `0x8058..0x8061`** | Auto-Reflect com resistências; SOS combinando buffs/Nul e vantagens/fraquezas elementais | Sem restrição definida | Combinações documentadas no catálogo histórico. Não inventar nomes comerciais novos nem transformar todos em novas linhas de ID. |
| **Capture refeito / Capture QoL distribuído** | **122 / `0x807A`** e bit Capture em várias outras linhas | Pacote de utilidade/combate em Capture e facilitação de captura em outras habilidades | Sem restrição definida | O JP e o US amostrados têm payloads diferentes. O JP122 carrega flags extras; o US122 conserva o Capture simples. A identidade e a economia precisam acompanhar o pack escolhido. |
| **Spell Spring** | Sem ID definitivo | Nome guardado no backlog de magia; efeito final não fechado nessa frente | Exclusividade não definida | Não tratar como sinônimo de Mana Spring +5 MP ou como confirmação de uma autoability que zera MP. O status vanilla Spellspring é outro contrato. |

O inventário acima inclui **reaproveitamentos de slots** e **famílias reescritas**. Eles integram o catálogo de possibilidades do MOD-002, mas não devem ser contados como dezenas de novas linhas já criadas em `a_ability.bin`.

### Auto-habilidades exclusivas por personagem

| Personagem | Auto-habilidade/direção | Restrição e efeito | Estado / ID |
|---|---|---|---|
| **Auron — owner 2** | **Warden's Oath** (nome provisório na pesquisa) | Com Break HP Limit ou Break Limits, permitir HP acima de 99.999, com teto candidato **999.999**, **somente para Auron** | Proposta de Hook. O documento antigo sugeria #23, mas a decisão posterior destina #23 a Break Limits geral. A criação aditiva escolheu linha própria153 para Warden; #23 legado permanece intacto e Break Limits novo está em151. Não ligar duas identidades ao mesmo ID. |
| **Lulu — owner5** | **Arcane Focus** | Auto-habilidade **Lulu-only**, ligada à identidade de magia; a fonte não fecha fórmula, bônus nem tipo de peça | Ideia explícita em `FFX_SPIRA_REFORGE_LULU_MAGIC_RESEARCH_2026-06-15.md`; Linha154 criada neutra, sem handler confirmado. Não inventar +MAG%, custo ou redução de CTB como regra já aprovada. |
| **Tidus — owner0** | Secreto de **OD/CTB/recuperação** | Direção de identidade por personagem | Sem nome/efeito/ID fechado. #134 foi distribuída em armas de Tidus, o que não prova exclusividade da autoability. |
| **Yuna — owner1** | Secreto de **Summon/White Magic/utilidade** | Direção; Entrust buff aparece como hipótese | Sem autoability de equipamento definida. Não transformar comando/alteração de Summon em atributo equipável por suposição. |
| **Kimahri — owner3** | **Blue Mage/Ronso Mana** | Sistema de personagem já pesquisado/implementado em sua lane | Sistema de comandos/Overdrive, não prova de uma nova linha passiva em `a_ability.bin`. Uma versão como equipamento exige contrato próprio. |
| **Wakka — owner4** | Secreto de **Reels com efeito determinístico** | Direção ainda em aberto | Sem nome/efeito/ID fechado; mudanças em comandos Reels não são automaticamente autoabilities. |
| **Rikku — owner6** | Secreto de **Mix/economia de itens** | Direção ainda em aberto | Sem autoability exclusiva definida. Copycat/Mugra/Mugga são comandos; Double/Triple Drop não têm restrição a Rikku no source consultado. |

**Contrato obrigatório das exclusivas:** validar owner canônico ao equipar, calcular efeito, trocar peça, reforge/fusão e ao usar o quinto lógico. Restrição só na lista de Customize ou no drop não basta. `CharacterUser` é campo de comando e não deve ser transplantado para um byte supostamente livre de `a_ability.bin`; o parser de autoability consultado não documenta um campo equivalente de owner. Usar metadado do mod + gate do consumidor quando a exclusividade depender do Hook.

**Warden's Oath e Aeon Ascension têm permissões distintas:** Auron é owner2; os Breaks Aeon pertencem aos owners8–17 adquiridos/canônicos. Compartilhar o serviço de teto não libera a habilidade Aeon em Auron nem concede Warden a todos os usuários de Break Limits. Manter IDs/chaves/requisitos separados.

### Divergências concretas a preservar no handoff

Foram lidas duas tabelas do pacote Spira (`jppc` e `new_uspc`) e suas correspondentes no Steam: **148 linhas de 108 B**, IDs 0–147. Spira e Steam coincidem byte a byte dentro de cada idioma amostrado; os dois idiomas têm payloads diferentes. A leitura é estática, sem confirmar qual recurso o processo carrega em cada configuração.

| Família | Design/export histórico | Snapshot JP atual | Snapshot US atual |
|---|---|---|---|
| HPMP114–117 | 10/20/40/60%, máscara `0x0300` | Byte de percentual `+0x55`: **118/119/120/121**, máscara `0x0300` | **5/10/20/30**, máscara HP `0x0100` |
| AIO118–121 | 3/6/9/12%, máscara `0x3F00` | Percentual **122/123/124/125**, máscara `0x3F00` | **5/10/20/30**, máscara MP `0x0200` |
| Break Limits23 | Word `+0x64 =0x0600` | `0x0200` | `0x0200`, nome Break HP Limit |
| Devil's Bargain24 | Novo efeito x1,5 causado/recebido | `0x0400` de BMP permanece | `0x0400`, nome Break MP Limit |
| Double/Triple Drop129/130 | Bits `0x1000/0x2000` | Ambos ausentes;129 tem outros bytes não neutros | Payloads neutros, nomes Extra 1 / Extra 2 |
| Custom134 | Nome/escopo mini-Celestial em aberto | Payload igual ao US para essa linha | Nome Ribbon; absorve Fire e contém Auto-Shell/Protect/Reflect/Regen/Haste/Auto-Life |

Os percentuais acima são **valores de campos lidos**, não afirmação de bônus observado em jogo. Eles não coincidem com os tiers pretendidos e não devem ser replicados automaticamente. O intervalo0–134 foi comparado ao backup anterior à inclusão dos IDs135–147 do MOD-002 e permaneceu idêntico nos dois arquivos Spira: essas diferenças não foram produzidas por este adendo nem pela anexação das novas linhas do MOD-002.

`arms_rate[134]` contém **200** na amostra; esse valor sozinho não define o preço de uma receita do Workshop. Não repetir a descrição antiga de “customizar por200 Gil” como se uma tabela de preço de habilidade fosse toda a transação.

Double/Triple Drop têm código real em `DoubleTripleDropHook.cpp`: filtro de namespace de item e callers de batalha, maior multiplicador do grupo, sem multiplicar Gil/equipamento. A ativação atual passa pelo catálogo F8/`labs.double_triple_drop`; o comentário antigo do header dizendo “env-only” está desatualizado. Nenhum estado de instalação ou RT2 foi revalidado nesta tarefa.

### Fontes e decisões para implementação

- **Design atual do pack:** `mods/Spira Reforge/VISION_AND_ROADMAP.md`, especialmente §10.8, §10.9, §10.16 e decisões do pass ofensivo. A decisão posterior é **#23 Break Limits / #24 Devil's Bargain**; a inversão de um documento anterior não é a referência a executar.
- **Inventário histórico:** `mods/Spira Reforge/docs/AUTOABILITY_REBALANCE_FULL_CATALOG.md`, `halyson_autoability_diff.json` e `halyson_weapon_aa_diff.md`. Registram a base de junho, incluindo64 alterações e custom134; não são um diff atual dos148 registros.
- **Exclusivas:** `docs/reverse/FFX_SPIRA_REFORGE_AURON_TANK_HOOK_RESEARCH_2026-06-15.md` e `docs/reverse/_archive/playbook/FFX_SPIRA_REFORGE_LULU_MAGIC_RESEARCH_2026-06-15.md` no Editor. São pesquisa, sem autorização operacional herdada.
- **Valores/layout:** `FFXProjectEditor/FfxLib/Ability/AutoAbility_File.cs` (`0x14` header, `0x6C` por linha; amount `+0x55`, statmask `+0x56`, flags `+0x64`), `FfxLib/Encoding/FfxEncoding.us.cs` e structs Fahrenheit de autoability/status. Conferir chance, duração e resistência como campos diferentes; o catálogo antigo mistura esses termos.
- **Runtime:** `src/runtime/FfxHooksDll/hooks/DoubleTripleDropHook.cpp`, `F8FlagCatalog.cpp`, `dllmain.cpp` e catálogo de owners do Workshop em `EquipmentWorkshopNames.h`, todos no `main` consultado.

Para a lane Editor/Hook: presets Spira devem ser opt-in e identificados por origem; preservar edição vanilla. Usar chaves estáveis e IDs efetivos/remapeáveis, detectar colisões com135–147 e com as propostas Aeon ainda sem ID, e definir dependências reais de cada efeito. Mana Spring fixo+5 e MP Regen do MOD-002 (2% do MP máximo) são habilidades distintas; sua coexistência precisa de regra explícita. As receitas exclusivas de Aeon Ascension continuam associadas àquele grupo; não estender silenciosamente a regra “só pelo Workshop” a toda habilidade histórica do Spira.

**Rota por tecnologia:** Break Limits nos tetos99.999/9.999, HPMP/AIO, bônus de stats e combinações nativas de elementos/status podem ser preparados por dados no Editor, após reconciliar os arquivos. Mana Spring, Devil's Bargain, Double/Triple Drop e o teto exclusivo de Auron precisam do consumidor de Hook correspondente. Arcane Focus ainda precisa de uma definição de efeito antes de escolher a rota. A marca `[DERIVADO DE MOD-002] [SPIRA REFORGE]` identifica o preset e suas dependências, mantendo a edição genérica de campos vanilla disponível.

**Atualização após o pedido de criação:** as27 linhas aditivas148–174 foram gravadas e verificadas. O levantamento acima descreve registros legados preservados; as novas linhas nativas/markers estão na tabela inicial. Receitas de menu e handlers pendentes não foram implementados.

## IDs iniciais para os atributos novos

Os IDs abaixo são a atribuição inicial de **linhas hook-only** em `a_ability.bin`. Os campos vanilla de efeito permanecem zerados: a linha fornece identidade e texto, mas **não concede o efeito sem o Hook**. `Energy Boost` é o nome escolhido para o antigo “Bônus elemental”. P-Trade e M-Trade usam linhas distintas. As duas últimas propostas da lista de armaduras (habilidades ativas de equipamento e custo parcial de Overdrive) são mecânicas do sistema, não novas auto-habilidades com IDs próprios.

| ID decimal | Palavra de equipamento | Atributo |
|---:|---:|---|
| 135 | `0x8087` | Hero's Bravery |
| 136 | `0x8088` | Energy Boost |
| 137 | `0x8089` | Energy Burst |
| 138 | `0x808A` | Efficiency |
| 139 | `0x808B` | Vampirism |
| 140 | `0x808C` | Follow Up (Assist Attack) |
| 141 | `0x808D` | P-Trade |
| 142 | `0x808E` | M-Trade |
| 143 | `0x808F` | Hero's Caution |
| 144 | `0x8090` | MP Regen |
| 145 | `0x8091` | Elude |
| 146 | `0x8092` | Energy Wall |
| 147 | `0x8093` | Energy Barrier |

**Remapeamento no Hook:** os defaults acima também estão no [manifesto por chave estável](../../research/mod_002_autoabilities/default_ids.json). O menu próprio do Hook deverá permitir alterar livremente a associação `atributo → ID` quando outro mod usar os IDs iniciais. A mudança deve validar que o ID existe em `a_ability.bin`, não está duplicado na configuração e não aponta silenciosamente para uma habilidade vanilla ou para o ID 134 já usado. Alterar só o número no menu **não renomeia nem move a linha binária**; se o novo ID pertencer a outro arquivo/mod, o usuário deverá ter a linha correspondente instalada. O Hook deve mostrar o mapeamento efetivo e manter o efeito desligado quando essa validação falhar.

## Localização para implementação

- **Hooks, branch documental:** `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`, `codex/mod-002-parts-20260927`. O Hook futuro deve consumir IDs configuráveis, não depender de literais dispersos pelo código.
- **FFX Editor:** `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor` no levantamento de 27/09/2026. Consultar `FFXProjectEditor/FfxLib/Ability/AutoAbility_File.cs`, `AutoAbilityHardcodedFlagCatalog.cs` e `Tools/AutoAbilityGrowRt0.cs`. Qualquer opção no Editor deve ser marcada `[DERIVADO DE MOD-002]`.
- **Fahrenheit:** `/home/wanderson/Documents/external-compare/fahrenheit`, branch `main`, commit `c149c847b3a24a66114956f87f1b008599736f75` no levantamento; consultar `src/core/ffx/aability.cs` e `src/core/ffx/equip.cs` como referência de formato.
- **Instalação Steam:** `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/` (10 pastas de idioma).
- **FFX Extracted:** `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/`.
- **Spira Reforge:** `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/` (`jppc` e `new_uspc`).

**Aplicação local dos dados:** os IDs 135–147 foram acrescentados nos três grupos acima, com as tabelas `arms_rate.bin` correspondentes ampliadas. Há [relatório de hashes, limites e rollback](../research/MOD_002_HOOK_ONLY_AUTOABILITIES_2026-09-27.md). O jogo não foi iniciado e os efeitos ainda dependem do Hook.
