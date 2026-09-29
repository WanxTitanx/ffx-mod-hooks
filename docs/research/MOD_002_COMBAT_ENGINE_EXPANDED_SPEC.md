# Especificação Arquitetural e Expansão — MOD-002 & Menus F8

**Data de Registro:** 27/09/2026
**Status:** Arquivado, estruturado e expandido conceitualmente para implementação futura nos hooks.

---

## 1. Prompt Original do Usuário (Transcrição Integral)

> *"Modificações nos hooks:*
> *Colocar o Arena+ dentro do Reforge (F8 -> Reforge -> Arena+ -> Opções)*
> *O atual arena+ ser renomeado para "Extras"*
>
> *Dentro de extras ( F8 -> Extras -> Nova Opção ), ter uma nova opção envolvendo o mod abaixo ( crie um nome maneiro em inglês).*
> *Ao abrir essa opção, você poderá escolher o que ativar individualmente do MOD-002 que irei lhe passar.*
>
> *# MOD-002 — Mudanças de combate e mecânicas*
>
> *Seleção das ideias de Kari AP e demais participantes, registradas entre 14/01/2025 e 04/11/2025. Cada caixa acompanha uma proposta; não indica implementação. As propostas de auto-habilidades de equipamento estão em [PARTE 2 MOD 002.md](<PARTE 2 MOD 002.md>), e a discussão sobre patches reutilizáveis está em [PARTE 3 MOD 002.md](<PARTE 3 MOD 002.md>).*
>
> *## Fórmulas, dano e balanceamento*
>
> *- [ ] Aplicar bônus percentuais de STR, MAG, DEF e MDF vindos de equipamentos quando o atributo correspondente for usado, sem vinculá-los apenas a ataques físicos ou mágicos.*
> *- [ ] Calcular bônus percentuais de DEF/MDF por pontos de vida efetivos; o exemplo da lista diz que DEF +20% reduziria o dano em cerca de 16,6%, não 20%.*
> *- [ ] Fazer Shell e outros efeitos protetivos ou auto-habilidades deixarem de reduzir cura recebida.*
> *- [ ] Alterar Armor Break e Mental Break: em vez de zerar defesa, acrescentariam 25% do dano-base sem mitigação por defesa. Exemplos da lista: 60 DEF, 25% para 50% do dano; 20 DEF, 50% para 75%; 0 DEF, 100% para 125%.*
> *- O texto copiado contém números de ajuste para golpes múltiplos e golpes únicos (`100%`, `110%`, `+5%`), mas a relação entre eles ficou ambígua; confirmar no post original antes de transformar em requisito.*
> *- [ ] Consumir Auto-Crit e MP-0 ao usar, removendo o efeito ao fim do turno para que todos os acertos de um mesmo ataque ainda recebam o benefício.*
> *- [ ] Ao atingir uma fraqueza elemental, aplicar também x1,25 de dano do elemento oposto; exemplo: fraqueza a Ice aumenta dano de Fire.*
> *- [ ] Permitir que reaplicar um status já ativo renove sua duração.*
> *- [ ] Adicionar resistência à duração de status nos inimigos, além da resistência à chance de aplicação, para diferenciar habilidades de Attack Buster por quantidade de turnos em vez de apenas sucesso/falha.*
> *- [ ] Fazer Threaten funcionar apenas uma vez por inimigo.*
> *- [ ] Rever efeitos de ataques que deveriam sempre acertar, mas aparentemente podem errar.*
>
> *## Opções independentes de magia*
>
> *- [ ] **Quickcast:** opção própria para substituir Doublecast. Em vez de lançar duas vezes, lança uma magia como ação de rank 2 pelo dobro do custo de MP. Com esta opção desligada, Doublecast mantém seu comportamento. A abrangência inicial proposta é Black Magic.*
> *- [ ] **White Magic em Doublecast/Quickcast:** opção própria e independente de Quickcast. Quando ligada, permite White Magic no comando que estiver ativo — Doublecast ou Quickcast — em um submenu próprio. Pode ser ligada sem transformar Doublecast em Quickcast.*
> *- [ ] **Fortalecimento de magia pelo MP atual:** opção própria, que pode ser ligada ou desligada independentemente de Quickcast e White Magic. Fórmula sugerida: `Base MAG = MAG + sqrt(MP / 2) + Focus stacks`, com bônus de MAG por faixas de MP: 0–1: +0; 2–7: +1; 8–17: +2; 18–31: +3; 32–49: +4; 50–71: +5; 72–97: +6; 98–127: +7; 128–161: +8; 162–199: +9; 200–241: +10. A anotação sobre efeitos MP-0 limitarem o bônus a +3 precisa ser esclarecida.*
>
> *## Troca de personagens e formação*
>
> *- [ ] **Troca consumir um turno:** opção própria, independente das opções de magia. Kari considera a troca livre parte central do combate de FFX, mas aceitou esta alternativa para modders experimentarem; nenhum padrão foi decidido.*
> *- [ ] Quando um personagem for ejetado ou estilhaçado, colocar o primeiro personagem disponível da retaguarda em seu lugar, em vez de deixar a posição vazia. Verificar restrições de troca nas áreas de natação.*
>
> *Se possível, melhore, modernize e até mesmo adicione mais opções acima se desejar ou se achar interessante. Seja livre para melhorar o escopo e adicionar mais opções dentro de opções."*

---

## 2. Reestruturação do Menu Nativo F8

A reorganização solicitada simplifica o topo do HUD F8, agrupando sistemas de progressão de equipamento e criando um espaço dedicado para inovações de mecânica:

```
[F8 Native Hook Dashboard]
│
├── [Reforge] (Aba de Equipamentos & Oficina)
│   ├── Equipment Workshop (5th slot, refine, fuse, compare)
│   └── Arena+ (Movido do menu principal para cá)
│       └── [Opções do Arena+] (Monster packs, encounter modifiers, arena music)
│
├── [Extras] (Antigo "Arena+" promovido a aba de mecânicas avançadas)
│   ├── Vanguard Combat Engine (Novo Módulo do MOD-002)
│   │   ├── Damage & Formulas Overhaul
│   │   ├── Arcane & Magic Dynamics
│   │   ├── Tactical Formations & CTB
│   │   └── Advanced Status & Affinities
│   └── Outras opções futuras de expansão
│
└── [Demais Abas Nativas: Boosters, Cheats, Input, Plugins...]
```

### Nome Sugerido para a Nova Opção
> **`Vanguard Combat Engine`**
> *(Alternativas de destaque: `Combat Dynamics Overhaul` ou `Tactical Battle Engine`)*

---

## 3. Matriz Completa: MOD-002 + Melhorias e Sub-Opções Modernas

Abaixo está o design modular expandido, mantendo todos os pontos de Kari AP e agregando sub-opções detalhadas e mecânicas modernas complementares.

---

### Módulo 1: Damage & Formulas Overhaul (Fórmulas e Balanceamento)

#### 1.1 Universal Stat Multipliers (Bônus Percentuais Universais)
* **Descrição**: Equipamentos com bônus de STR, MAG, DEF ou MDF aplicam seu multiplicador a qualquer cálculo que utilize esses atributos (ex: magias baseadas em força ou habilidades híbridas), e não apenas à categoria estrita física/mágica.
* **Sub-opções granulares**:
  - `Offensive Stats Scope`: Aplicar a STR e MAG em todas as ações híbridas/especiais.
  - `Defensive Stats Scope`: Aplicar DEF e MDF contra dano especial que utilize essas resistências.
  - `Healing Stat Scaling`: Permitir que o bônus de MAG aumente proporcionalmente o poder de feitiços de cura e itens médicos escaláveis.

#### 1.2 Effective HP Defense Scaling (Cálculo de DEF/MDF por Vida Efetiva)
* **Descrição**: Reformula a curva de dano recebido baseada em Effective HP (EHP). Exemplo: bônus de +20% de DEF reduz o dano real sofrido em cerca de 16,6% ($\frac{1}{1 + 0.20} \approx 0.833$).
* **Sub-opções granulares**:
  - `Scaling Model`:
    - *Vanilla Smooth* (apenas suaviza os retornos decrescentes vanilla).
    - *Kari EHP Curve* (cada 20 pontos de defesa dobram o HP efetivo para stats < 100).
    - *Linear Diminishing Returns* (proteção progressiva sem imunidade total).

#### 1.3 Unhindered Recovery (Cura sem Mitigação de Proteções)
* **Descrição**: Shell, Reflect, Auto-habilidades e barreiras deixam de reduzir ou prejudicar o valor das magias de cura (Cure, Cura, Curaga, Regen) recebidas pelo alvo.
* **Sub-opções granulares**:
  - `Regen EHP Scaling`: Regen passa a recuperar uma fração limpa do HP máximo sem ser penalizado por efeitos de status.
  - `Zombie Interaction Guard`: Manter Zombie como o único bloqueador/inversor genuíno de cura.

#### 1.4 Additive Break Debuffs (Armor & Mental Break Reformulados)
* **Descrição**: Em vez de anular a defesa do monstro para 0 (o que trivialize chefes ou quebra o balanceamento), os Breaks somam **+25% de dano base puro**, ignorando a mitigação de defesa.
* **Sub-opções granulares**:
  - `Defensive Debuff Tier`:
    - *Additive Flat +25%* (conforme especificado por Kari).
    - *Dynamic Mitigation Bypass* (penetra 50% da armadura do alvo em vez de 100%).
  - `Power & Magic Break Redesign`: Opção complementar para padronizar Power e Magic Break reduzindo o dano emitido pelo inimigo em 33% ou 50% limpos.

#### 1.5 Multi-Hit vs. Single-Hit Normalization (Harmonização de Dano)
* **Descrição**: Resolve a ambiguidade das notas originais entre ataques de golpe único e golpes múltiplos. Golpes de acerto único ganham bônus de acerto crítico e penetração, evitando que overdrives multi-hit anulem o restante do arsenal.
* **Sub-opções granulares**:
  - `Single-Hit Lethality`: Golpes de hit único têm chance de dano crítico aumentada em +10% e maior chance de Break.
  - `Multi-Hit Decay`: Golpes sequenciais rápidos têm uma curva de retorno ajustada para balancear Overdrives.

#### 1.6 Turn-End Buff Consumption (Aproveitamento Total de Multi-Hits)
* **Descrição**: Efeitos como Auto-Crit e Spellspring (MP-0) duram até o encerramento do turno completo da unidade, garantindo que todos os acertos de uma mesma investida recebam o benefício antes de o buff expirar.
* **Sub-opções granulares**:
  - `Cheer / Focus Persistence`: Cargas de Focus e Cheer só são recalculadas no término da execução.
  - `Counter-Attack Preservation`: Se o personagem for contra-atacado no meio da ação, os buffs do turno se mantêm.

---

### Módulo 2: Arcane & Magic Dynamics (Magias e Quickcast)

#### 2.1 Quickcast System (Substituição Modular de Doublecast)
* **Descrição**: Converte o comando Doublecast em um lançamento ultra-rápido de magia única.
* **Sub-opções granulares**:
  - `Quickcast Execution Rank`:
    - *Rank 2 Fixo* (Custo do turno equivalente a Quick Pockets / ação veloz).
    - *Half-Rank* (Corta o rank normal da magia pela metade, mínimo rank 2).
  - `Cost Multiplier`:
    - *Double MP Cost (2.0x)* (Conforme Kari AP).
    - *1.5x MP Cost* (Ajuste dinâmico de balanceamento).
  - `Catalog Scope`: Abrange Black Magic, White Magic (se ligada) ou Magias Especiais.

#### 2.2 White Magic in Dualcast / Quickcast (Menu Dedicado de Magia Branca)
* **Descrição**: Adiciona um submenu dedicado para White Magic dentro do comando Doublecast ou Quickcast ativo.
* **Sub-opções granulares**:
  - `Independent Mode`: Funciona tanto com o Doublecast vanilla quanto com o Quickcast ativado.
  - `Smart Target Retain`: Memoriza se a magia anterior foi direcionada ao grupo inteiro ou a um membro específico.

#### 2.3 Arcane Surge — Fortalecimento de Magia por MP Atual
* **Descrição**: Aumenta o MAG base conforme o MP disponível no momento:
  $$\text{Base MAG} = \text{MAG} + \sqrt{\frac{\text{MP}}{2}} + \text{Focus stacks}$$
* **Sub-opções granulares**:
  - `MP-0 / Spellspring Interaction`:
    - *Kari Cap (+3)*: Trava o bônus máximo em +3 quando sob efeito de custo 0 de MP.
    - *Dynamic Ghost MP*: Calcula o bônus baseado no MP que a unidade teria sem o buff de custo 0.
  - `Reverse Arcane Mode (Desperation / Crisis Magic)`: Opção alternativa para que certas magias fiquem mais poderosas quando o personagem está com baixo MP.

---

### Módulo 3: Tactical Formations & CTB (Trocas e Dinâmica de Campo)

#### 3.1 Turn-Consuming Character Switch (Troca Tática de Membros)
* **Descrição**: Trocar um membro da linha de frente por um da reserva passa a consumir um turno no CTB, trazendo mais peso estratégico à formação.
* **Sub-opções granulares**:
  - `Switch Cost Rank`:
    - *Rank 1 Instant Swap* (Quase imediato, apenas consome a prioridade).
    - *Rank 2 Fast Action* (Custo balanceado).
    - *Rank 3 Standard Action* (Custo de um ataque normal).
  - `First Strike Exemption`: Personagens com a habilidade *First Strike* podem entrar com troca gratuita imediata.

#### 3.2 Frontline Auto-Reinforce on Eject / Shatter (Substituição Automática)
* **Descrição**: Se um personagem da linha de frente for ejetado de campo ou estilhaçado (Petrify + Shatter), o primeiro membro válido da retaguarda entra imediatamente na vaga aberta.
* **Sub-opções granulares**:
  - `Underwater Area Handling`: Respeita estritamente o trio nadador (Tidus, Wakka, Rikku) em áreas aquáticas, impedindo entrada de personagens incompatíveis.
  - `Reserve Selection Priority`:
    - *Formation Order* (Primeiro da lista na retaguarda).
    - *Highest CTB Readiness* (Membro da reserva cujo turno esteja mais próximo).

---

### Módulo 4: Advanced Status & Elemental Affinities

#### 4.1 Opposite Element Affinity (Ressonância Cruzada de Fraqueza)
* **Descrição**: Atingir uma fraqueza elemental aplica também multiplicador de **x1.25** do elemento oposto (ex: fraqueza a Ice concede bônus adicional para magias de Fire).
* **Sub-opções granulares**:
  - `Elemental Pairings`:
    - *Fire ↔ Ice*
    - *Thunder ↔ Water*
    - *Holy ↔ Dark* (Compatível com novos elementos do mod)
  - `Elemental Priming (Thermal Shock / Conductivity)`: Atacar alternadamente elementos opostos em turnos sucessivos causa sobrecarga elemental.

#### 4.2 Status Reapplication Refresh & Stacking
* **Descrição**: Reaplicar um status positivo ou negativo já ativo no alvo renova sua duração para o tempo máximo em vez de dar "Miss" ou ser ignorado.
* **Sub-opções granulares**:
  - `Potency Overwrite`: Permite que uma versão mais potente (ex: Haste+) sobreponha uma inferior sem necessidade de esperar o término.

#### 4.3 Enemy Status Duration Resistance (Resistência de Duração em Turnos)
* **Descrição**: Inimigos possuem resistência própria à quantidade de turnos que um status permanece neles, além da chance de acerto. Habilidades de *Attack Buster* (Delay, Sleep, etc.) causam durações variáveis conforme o nível do inimigo.
* **Sub-opções granulares**:
  - `Boss Minimum Duration`: Garante que chefes sofram no mínimo 1 turno de status vulneráveis sem que fiquem travados permanentemente.

#### 4.4 Single-Use Threaten (Ameaça Limitada)
* **Descrição**: O comando *Threaten* só tem efeito uma única vez por inimigo na mesma batalha, evitando exploits de congelar chefes indefinidamente.
* **Sub-opções granulares**:
  - *Hard Limit 1x*: Funciona exatamente uma vez por alvo.
  - *Diminishing Returns*: A cada uso subsequente no mesmo inimigo, a eficácia é reduzida em 50%.

#### 4.5 True Strike Validation (Ataques Infalíveis)
* **Descrição**: Corrige ações do jogo vanilla marcadas para 100% de acerto que em condições específicas de cálculo de Evasion/Luck ainda podiam falhar. Garante que ataques garantidos nunca errem.

---

## 4. Diagrama da Interface F8 Expandida

```
┌────────────────────────────────────────────────────────────────┐
│ Equipment Workshop / Reforge & Extras Dashboard                │
├────────────────────────────────────────────────────────────────┤
│ [Reforge]  [Extras]  [Boosters]  [Cheats]  [Scout]  [Input]    │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  ▶ Vanguard Combat Engine (MOD-002)                           │
│      ├── Damage & Formulas Overhaul                           │
│      │     ├─ Universal Stat Multipliers         [ON / OFF]   │
│      │     ├─ Effective HP Defense Scaling       [ON / OFF]   │
│      │     ├─ Unhindered Recovery (Shell/Heal)   [ON / OFF]   │
│      │     ├─ Additive Break Debuffs (+25%)      [ON / OFF]   │
│      │     ├─ Turn-End Buff Consumption          [ON / OFF]   │
│      │     └─ True Strike (Guaranteed Hits)      [ON / OFF]   │
│      │                                                        │
│      ├── Arcane & Magic Dynamics                              │
│      │     ├─ Quickcast (Replaces Doublecast)    [ON / OFF]   │
│      │     ├─ White Magic in Dual/Quickcast      [ON / OFF]   │
│      │     └─ Arcane Surge (MP-Scaled MAG)       [ON / OFF]   │
│      │                                                        │
│      ├── Tactical Formations & CTB                            │
│      │     ├─ Turn-Consuming Character Switch    [ON / OFF]   │
│      │     └─ Frontline Auto-Reinforce on Eject  [ON / OFF]   │
│      │                                                        │
│      └── Advanced Status & Affinities                         │
│            ├─ Opposite Element Affinity (x1.25)  [ON / OFF]   │
│            ├─ Status Duration Refresh            [ON / OFF]   │
│            ├─ Enemy Status Duration Resistance   [ON / OFF]   │
│            └─ Single-Use Threaten Rule           [ON / OFF]   │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```
