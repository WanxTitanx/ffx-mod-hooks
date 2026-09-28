# Registro de Feedback e Requisitos — Workshop, Scan e Menus (26/09/2026)

Este documento registra fielmente o prompt enviado pelo usuário e cataloga todas as capturas de tela fornecidas para orientar futuras correções e melhorias no mod.

---

## 1. Prompt Original do Usuário (Transcrição Fidedigna)

> *"Alguns problemas identificados logo inicialmente é que atributos aparentemente podem ser repetidos no quinto slot. Normalmente no customize tem muitas limitações quais atributos podem estar ao mesmo tempo em uma arma. Isso não deveria ser possível. Você precisa trazer essas limitações do próprio customize para evitar duplicações ou atributos estranhos e meio que redundantes dentro da arma que podem ter consequências ou coisas não previstas pelo jogo e a gente não quer muito isso inicialmente. Então as limitações do jogo a gente vai manter. Então você precisa buscar a forma como isso é feito dentro do customize para, por exemplo, evitar que eu possa colocar dois atributos iguais, atributos que são conflitantes naturalmente e por aí vai.*
>
> *Na parte do evoluir o atributo, você sair de slow touch para, por exemplo, slow strike ou todas as outras evoluções, faça com que você cobre apenas metade dos itens cobrados normalmente para você dar esse upgrade. Ou seja, o item que normalmente seria cobrado para subir o slow strike 100% do tempo, agora vai custar só 50%. E coloca um custo de moedas, provavelmente 20.000, 30.000 GIL, algo assim.*
>
> *Uma coisa que faltou desse mod foi que o scan data também deveria mostrar os novos três elementos, caso tenham. E com isso a gente vai precisar praticamente de uma expansão desse menu, porque ele naturalmente é muito pequeno para comportar mais três elementos, então caso a gente adicione, ele vai ter que ir expandindo aos poucos. Na outra print que coloquei, que é o do scan normal, que é da solda do sensor, aquela faixa que cobre ali os elementos, ela deve ser esticada até dar até o final, então ela precisa ser esticada proporcionalmente à quantidade de elementos que tem ali.*
>
> *Já que vamos ter que mexer na parte do scan, uma ideia que eu tive é que normalmente só mostra dois atributos: força, HP e magia. Sendo que os monstros têm muito mais atributos do que isso, a minha ideia seria mostrar todos os atributos quando você dá um scan. Ou seja, todos os atributos básicos do monstro estariam ali para poder ser visto e, se possível, colocar o MP do monstro também, caso ele tenha.*
>
> *O status, aonde mostra as auto abilities, ela não tá mostrando os auto abilities das armas/equipamentos que poderiam ter, ou seja, ainda falta nesse menu mostrar os extras, que são referentes aos quintos slots. Nos últimos prints que eu encaminhei, o mod gear é uma coisa editada, mas é basicamente dentro dos itens, na hora de selecionar os equipamentos e principalmente para organizar eles, não tá mostrando o quinto atributo da arma ou do equipamento que vai ter, como você pode perceber, esse item teria um quinto atributo. Basicamente para todas essas partes de menu, provavelmente não está aparecendo nem o quinto lote e muito menos o refinamento. Os pontos onde você já colocou o refinamento, que é na equipe, no customize e dentro de jogo, esses estão funcionando sim, mas provavelmente as outras dependências que vão acabar tendo não estão mostrando corretamente."*

---

## 2. Síntese Estruturada dos Requisitos e Problemas

### A. Validação e Limitações do Quinto Slot (Customize Conflicts)
- **Problema**: Atualmente é possível inserir atributos duplicados (ex: `Piercing` já presente no slot 3 sendo adicionado novamente no 5º slot) ou atributos incompatíveis.
- **Regra**: Espelhar integralmente as regras do sistema nativo de Customize de FFX para o 5º slot:
  - Proibir atributos repetidos na mesma peça.
  - Proibir atributos mutuamente exclusivos / conflitantes (ex: regras de status touch vs strike, etc., já previstas no motor do jogo).
- **Evidência**: [Print 1: Duplicação no Quinto Slot](#print-1--duplicação-no-quinto-slot-equipment-workshop)

### B. Economia de Evolução de Atributos (Upgrades Touch → Strike, etc.)
- **Regra de Custo em Itens**: Ao evoluir uma habilidade (ex: `Slowtouch` → `Slowstrike`), o custo do item específico deve ser de **apenas 50%** da quantidade exigida pela receita normal de Customize da habilidade de destino.
- **Custo em Gil**: Cobrar uma taxa em dinheiro em torno de **20.000 a 30.000 Gil**.
- **Evidência**: [Print 2: Upgrade de Habilidade](#print-2--custo-de-evolução-de-habilidade-equipment-workshop)

### C. Menu de Batalha: Scan Data e Faixa do Sensor (Elementos Extras)
- **Menu Expandido ("Scan data")**: Deve comportar e exibir os 3 novos elementos (totalizando até 7 colunas). Como o layout original é compacto, a janela de Scan Data precisa ser expandida (gradual/dinamicamente) para acomodar os novos elementos sem cortes.
- **Faixa do Sensor (HUD Info bar)**: A faixa horizontal de fundo sob os orbes elementais no HUD simples de Scan/Sensor precisa ser esticada proporcionalmente ao número total de elementos exibidos, cobrindo todos os orbes até o final.
- **Evidências**:
  - [Print 3: Janela Detalhada de Scan Data](#print-3--janela-detalhada-de-scan-data-snow-wolf)
  - [Print 4: Barra de Sensor com Elementos Vazando (Snow Wolf)](#print-4--faixa-do-sensor-ultrapassando-fundo-snow-wolf)
  - [Print 5: Barra de Sensor com Elementos Vazando (Ice Flan)](#print-5--faixa-do-sensor-ultrapassando-fundo-ice-flan)
  - [Print 6: Barra de Sensor com Elementos Vazando (Blue Element)](#print-6--faixa-do-sensor-ultrapassando-fundo-blue-element)

### D. Scan Expandido: Todos os Atributos Básicos do Inimigo + MP
- **Situação Atual**: A janela de Scan mostra apenas HP, Força (Strength) e Magia (Magic).
- **Requisito**: Exibir todos os atributos básicos do monstro (HP, MP se possuir, Strength, Magic, Defense, Magic Defense, Agility, Luck, Evasion, Accuracy).

### E. Exibição do 5º Slot e Refinamento nos Menus Secundários
- **Menu Status (Auto-Abilities)**: A lista geral de auto-abilities ativas do personagem não está incluindo as habilidades provenientes dos quintos slots dos equipamentos equipados.
- **Menu Itens / Organização de Equipamentos (Manually Sort Equipment / Mod Gear)**:
  - As listas de equipamentos e o painel de detalhes (Abilities Equipped / Abilities) mostram apenas 4 slots, omitindo o 5º atributo existente.
  - O sufixo de refinamento (`+X`) não está sendo renderizado nesses menus.
- **Evidências**:
  - [Print 7: Menu Status - Auto-Abilities](#print-7--menu-de-status-auto-abilities)
  - [Print 8: Menu de Organização de Equipamentos](#print-8--menu-manually-sort-equipment-abilities-equipped)

---

## 3. Galeria de Capturas de Tela

### Print 1 — Duplicação no Quinto Slot (Equipment Workshop)
![Workshop Duplicate Fifth Slot](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/workshop_duplicate_fifth_slot.png)
*Tidus Weapon #19 já possui "Piercing" no slot 3, e o sistema permite adicionar outro "Piercing" no slot 5.*

---

### Print 2 — Custo de Evolução de Habilidade (Equipment Workshop)
![Workshop Upgrade Cost](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/workshop_upgrade_cost.png)
*Evolução de Slowtouch para Slowstrike cobrando 1 Attribute Sphere. Deve custar 50% dos itens do Slowstrike normal + 20k–30k Gil.*

---

### Print 3 — Janela Detalhada de Scan Data (Snow Wolf)
![Scan Data Window](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/battle_scan_data_window.png)
*Janela de Scan Data compacta original (4 elementos e apenas Strength/Magic). Necessita expansão para até 7 elementos e exibição de todos os stats + MP.*

---

### Print 4 — Faixa do Sensor Ultrapassando Fundo (Snow Wolf)
![Sensor Bar Snow Wolf](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/battle_sensor_bar_elements_wolf.jpg)
*Faixa horizontal de fundo no HUD Info termina prematuramente, deixando os 3 novos elementos flutuando fora da barra.*

---

### Print 5 — Faixa do Sensor Ultrapassando Fundo (Ice Flan)
![Sensor Bar Ice Flan](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/battle_sensor_bar_elements_flan.jpg)
*Orbes das linhas x1.5, + e 1/2 ultrapassam a barra base de fundo.*

---

### Print 6 — Faixa do Sensor Ultrapassando Fundo (Blue Element)
![Sensor Bar Blue Element](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/battle_sensor_bar_blue_element.jpg)
*Outro exemplo em batalha com 7 colunas de elementos na barra de Sensor ultrapassando a solda de fundo.*

---

### Print 7 — Menu de Status (Auto-Abilities)
![Menu Status Auto-Abilities](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/menu_status_auto_abilities.jpg)
*Tela de Status de Tidus listando apenas as habilidades dos slots padrão, omitindo habilidades provenientes do 5º slot.*

---

### Print 8 — Menu Manually Sort Equipment (Abilities Equipped)
![Menu Sort Gear Ragnarok](/home/wanderson/.gemini/antigravity/brain/d45cd922-d90e-43a2-b220-99b3f2a37040/menu_sort_gear_ragnarok_abilities.jpg)
*Tela de organização manual de itens: Ragnarok exibe apenas 4 habilidades e não mostra o 5º slot nem o refinamento (+X).*
