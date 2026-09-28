# Registro Consolidado — Reestruturação F8 e MOD-002 Completo (Partes 1 e 2)

**Data de Registro:** 27/09/2026
**Status:** Armazenado integralmente para planejamento e futura implementação.

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
> *Seleção das ideias de Kari AP e demais participantes, registradas entre 14/01/2025 e 04/11/2025. Cada caixa acompanha uma proposta; não indica implementação. As propostas de auto-habilidades de equipamento estão em [PARTE 2 MOD 002.md](<../mod-ideas/PARTE 2 MOD 002.md>), e a discussão sobre patches reutilizáveis está em [PARTE 3 MOD 002.md](<../mod-ideas/PARTE 3 MOD 002.md>).*
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
> *## Caminhos dos repositórios e dados*
>
> *- **Hooks:** `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`, branch `codex/mod-002-parts-20260927` (este documento e futura configuração dos efeitos).*
> *- **FFX Editor:** `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor` no levantamento de 27/09/2026.*
> *- **Fahrenheit:** `/home/wanderson/Documents/external-compare/fahrenheit`, branch `main`, commit `c149c847b3a24a66114956f87f1b008599736f75` no levantamento.*
> *- **FFX Steam:** `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/`.*
> *- **FFX Extracted:** `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/`.*
> *- **Spira Reforge:** `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/`. A [Parte 2](<../mod-ideas/PARTE 2 MOD 002.md>) relaciona os novos IDs hook-only a esses arquivos.*
>
> *# PARTE 2 MOD 002 — Habilidades de armas e armaduras*
>
> *As propostas abaixo foram apresentadas como auto-habilidades de equipamento. Algumas condições e interações são ideias iniciais, não regras fechadas. [Voltar à parte principal](<../mod-ideas/MOD 002.md>).*
>
> *## Armas*
>
> *- [ ] **Hero's Bravery:** +25% de chance de causar crítico e +25% de chance de receber crítico.*
> *- [ ] **Energy Boost (bônus elemental):** a anotação propõe dano/cura elemental x1,2 e relaciona o efeito à barra de Overdrive acima de 50%; a sintaxe original está incompleta e deve ser confirmada.*
> *- [ ] **Energy Burst:** dano e cura x1,4 enquanto a barra de Overdrive permanece acima de 75%; a proposta diz que acumula aditivamente com Energy Boost, chegando a x1,65.*
> *- [ ] **Efficiency:** reduzir em 25% custos de MP e Overdrive; combinada com Half MP Cost, a redução de custo de MP chegaria a 75%.*
> *- [ ] **Vampirism:** após toda ação ofensiva, curar o usuário em **2% do dano total de HP que ele causou**. Somar os acertos e alvos da ação; não exigir morte do inimigo. O Hook deverá fechar arredondamento, overkill e interação com Zombie antes de executar o efeito.*
> *- [ ] **Assist Attack / Follow Up:** atacar automaticamente o mesmo alvo quando um aliado usar um ataque de HP de alvo único. Dawn prefere o nome Follow Up.*
>
> *## Armaduras*
>
> *- [ ] **P-Trade / M-Trade:** trocar mitigação entre dano físico e mágico; P-Trade recebe dano físico x0,8 e mágico x1,2, e M-Trade faz o inverso.*
> *- [ ] **Hero's Caution:** descrição copiada diz que nunca causaria crítico aleatório, mas repete a mesma frase para o efeito negativo; confirmar o comportamento pretendido. Hero Drink seria uma exceção e garantiria crítico.*
> *- [ ] **MP Regen:** recuperar 2% do MP máximo no começo do turno; a anotação considera reaproveitar o ponto de hook do booster F2, cuidando de trocas de personagem e modos de Overdrive.*
> *- [ ] **Elude:** +50 de Evasion ao defender.*
> *- [ ] **Energy Wall:** dano recebido x0,8 enquanto a barra de Overdrive permanece acima de 50%; não afetaria cura nem fórmulas de dano fixo fracionário.*
> *- [ ] **Energy Barrier:** dano recebido x0,7 enquanto a barra permanece acima de 75%; acumularia aditivamente com Energy Wall até x0,5.*
> *- [ ] Disponibilizar em combate as habilidades ativas da arma ou armadura enquanto a peça estiver equipada.*
> *- [ ] Dar às habilidades de equipamento custo parcial de Overdrive e mostrar visualmente a parte consumida em branco e a parte que falta em vermelho. A autora esclareceu que pensava em habilidades multiplicadoras, não em habilidades que gastam grandes partes da barra.*
>
> *## IDs iniciais para os atributos novos*
>
> *Os IDs abaixo são a atribuição inicial de **linhas hook-only** em `a_ability.bin`. Os campos vanilla de efeito permanecem zerados: a linha fornece identidade e texto, mas **não concede o efeito sem o Hook**. `Energy Boost` é o nome escolhido para o antigo “Bônus elemental”. P-Trade e M-Trade usam linhas distintas. As duas últimas propostas da lista de armaduras (habilidades ativas de equipamento e custo parcial de Overdrive) são mecânicas do sistema, não novas auto-habilidades com IDs próprios.*
>
> *| ID decimal | Palavra de equipamento | Atributo |*
> *|---:|---:|---|*
> *| 135 | `0x8087` | Hero's Bravery |*
> *| 136 | `0x8088` | Energy Boost |*
> *| 137 | `0x8089` | Energy Burst |*
> *| 138 | `0x808A` | Efficiency |*
> *| 139 | `0x808B` | Vampirism |*
> *| 140 | `0x808C` | Follow Up (Assist Attack) |*
> *| 141 | `0x808D` | P-Trade |*
> *| 142 | `0x808E` | M-Trade |*
> *| 143 | `0x808F` | Hero's Caution |*
> *| 144 | `0x8090` | MP Regen |*
> *| 145 | `0x8091` | Elude |*
> *| 146 | `0x8092` | Energy Wall |*
> *| 147 | `0x8093` | Energy Barrier |*
>
> ***Remapeamento no Hook:** os defaults acima também estão no [manifesto por chave estável](../../research/mod_002_autoabilities/default_ids.json). O menu próprio do Hook deverá permitir alterar livremente a associação `atributo → ID` quando outro mod usar os IDs iniciais. A mudança deve validar que o ID existe em `a_ability.bin`, não está duplicado na configuração e não aponta silenciosamente para uma habilidade vanilla ou para o ID 134 já usado. Alterar só o número no menu **não renomeia nem move a linha binária**; se o novo ID pertencer a outro arquivo/mod, o usuário deverá ter a linha correspondente instalada. O Hook deve mostrar o mapeamento efetivo e manter o efeito desligado quando essa validação falhar.*
>
> *## Localização para implementação*
>
> *- **Hooks, branch documental:** `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`, `codex/mod-002-parts-20260927`. O Hook futuro deve consumir IDs configuráveis, não depender de literais dispersos pelo código.*
> *- **FFX Editor:** `/home/wanderson/Documents/ffx-editor-main`, branch `codexclaudiocodeffxeditor` no levantamento de 27/09/2026. Consultar `FFXProjectEditor/FfxLib/Ability/AutoAbility_File.cs`, `AutoAbilityHardcodedFlagCatalog.cs` e `Tools/AutoAbilityGrowRt0.cs`. Qualquer opção no Editor deve ser marcada `[DERIVADO DE MOD-002]`.*
> *- **Fahrenheit:** `/home/wanderson/Documents/external-compare/fahrenheit`, branch `main`, commit `c149c847b3a24a66114956f87f1b008599736f75` no levantamento; consultar `src/core/ffx/aability.cs` e `src/core/ffx/equip.cs` como referência de formato.*
> *- **Instalação Steam:** `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/` (10 pastas de idioma).*
> *- **FFX Extracted:** `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/ffx-editor-pt29__PT29_DOSSIE_COMPLETO_CHAT/dependencies/D/FFX Extracted/FFX/ffx_ps2/ffx/master/`.*
> *- **Spira Reforge:** `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/` (`jppc` e `new_uspc`).*
>
> ***Aplicação local dos dados:** os IDs 135–147 foram acrescentados nos três grupos acima, com as tabelas `arms_rate.bin` correspondentes ampliadas. Há [relatório de hashes, limites e rollback](../research/MOD_002_HOOK_ONLY_AUTOABILITIES_2026-09-27.md). O jogo não foi iniciado e os efeitos ainda dependem do Hook."*

---

## 2. Mapa do Menu F8 Reorganizado

```
[F8 Native Hook Dashboard]
│
├── [Reforge] (Oficina & Desafios de Equipamento)
│   ├── Equipment Workshop (5th slot, refine, fuse, compare)
│   └── Arena+ (Movido do menu principal)
│       └── [Opções do Arena+]
│
├── [Extras] (Antigo "Arena+" promovido à aba de mecânicas de combate)
│   └── Vanguard Combat Engine (MOD-002)
│       ├── [1] Battle Formulas & Damage
│       ├── [2] Arcane & Spellcasting (Quickcast, White Magic, MP-Scaling)
│       ├── [3] Formations & Dynamic Swaps
│       ├── [4] Status Affinities & Mechanics
│       ├── [5] Hook-Only Equipment Auto-Abilities (Armas & Armaduras)
│       └── [6] Auto-Ability ID Remapping & Validation
│
└── [Demais Abas Nativas: Boosters, Cheats, Input, Plugins...]
```

---

## 3. Catálogo de Habilidades Hook-Only e Remapeamento

As novas auto-habilidades são registradas em `a_ability.bin` com efeitos vanilla zerados, sendo interpretadas e executadas exclusivamente pelo Hook em runtime.

### Tabela de Mapeamento Inicial
| ID Decimal | Palavra de Equipamento | Atributo | Tipo | Descrição Resumida |
|---:|:---:|---|:---:|---|
| **135** | `0x8087` | **Hero's Bravery** | Arma | +25% de chance de causar crítico e +25% de receber crítico. |
| **136** | `0x8088` | **Energy Boost** | Arma | Dano/cura elemental x1.2 com barra de Overdrive > 50%. |
| **137** | `0x8089` | **Energy Burst** | Arma | Dano/cura x1.4 com Overdrive > 75% (acumula até x1.65). |
| **138** | `0x808A` | **Efficiency** | Arma | -25% custos de MP e Overdrive (-75% com Half MP). |
| **139** | `0x808B` | **Vampirism** | Arma | Cura o usuário em 2% de todo dano de HP causado na ação. |
| **140** | `0x808C` | **Follow Up** | Arma | Ataque automático no alvo ao receber ataque HP de aliado. |
| **141** | `0x808D` | **P-Trade** | Armadura | Recebe dano físico x0.8 e dano mágico x1.2. |
| **142** | `0x808E` | **M-Trade** | Armadura | Recebe dano físico x1.2 e dano mágico x0.8. |
| **143** | `0x808F` | **Hero's Caution** | Armadura | Elimina críticos aleatórios sofridos (Hero Drink prevalece). |
| **144** | `0x8090` | **MP Regen** | Armadura | Recupera 2% do MP máximo no início do turno da unidade. |
| **145** | `0x8091` | **Elude** | Armadura | +50 de Evasion ao assumir a postura Defend. |
| **146** | `0x8092` | **Energy Wall** | Armadura | Dano sofrido x0.8 enquanto Overdrive > 50%. |
| **147** | `0x8093` | **Energy Barrier** | Armadura | Dano sofrido x0.7 com Overdrive > 75% (acumula até x0.5). |

### Regras de Validação e Remapeamento no Menu F8
- O submenu `[6] Auto-Ability ID Remapping` permite associar cada chave estável (ex: `vampirism`) a um novo ID inteiro.
- **Validações obrigatórias antes de salvar**:
  1. O ID selecionado deve existir dentro do `a_ability.bin` carregado.
  2. Não pode colidir com nenhum ID de habilidade vanilla (0 a 133).
  3. Não pode colidir com o ID 134 já utilizado.
  4. Não pode haver duplicatas entre as próprias chaves do MOD-002.
- **Fail-safe**: Se qualquer validação falhar, o Hook exibe aviso explícito no menu e mantém o efeito desativado, prevenindo instabilidade ou leitura incorreta de memória.

---

## 4. Repositórios, Branches e Ambientes Envolvidos

* **Hooks Engine (Trabalho Principal):**
  `/home/wanderson/.codex/worktrees/mod-002-parts/ffx-hooks`
  Branch: `codex/mod-002-parts-20260927`
* **FFX Project Editor:**
  `/home/wanderson/Documents/ffx-editor-main`
  Branch: `codexclaudiocodeffxeditor`
  Arquivos-chave: `AutoAbility_File.cs`, `AutoAbilityHardcodedFlagCatalog.cs`, `AutoAbilityGrowRt0.cs`.
* **Fahrenheit (Referência de Hooks e Formatos):**
  `/home/wanderson/Documents/external-compare/fahrenheit`
  Commit: `c149c847b3a24a66114956f87f1b008599736f75`
* **Instalação do Jogo (Steam):**
  `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/data/mods/ffx_ps2/ffx/master/`
* **Dados Vanilla Extraídos:**
  `/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/...`
* **Spira Reforge:**
  `/home/wanderson/Documents/ffx-editor-main/mods/Spira Reforge/data/mods/ffx_ps2/ffx/master/`
