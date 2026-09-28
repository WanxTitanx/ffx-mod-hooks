# Jarvis-HOOK — revalidação Sol e pacote pré-código das ideias MOD-001..004

Data: 23/09/2026. Branch local: codex/mod-ideas-precode-20260923. Base: a70a3543251d029f7d3aa2fa18ef1620374eb3e0.

## Resultado e alcance

Na primeira entrega, o registro research/mod_ideas_precode/feature_ledger.tsv associava os **91 checkboxes** de MOD-001..004 a rota, evidência, confiança, estado e próxima verificação. O backlog original do Luna era idêntico ao do checkout principal naquele momento: SHA-256 2c56bf4813bb13b11007d3ee67fcd51da976cb6522dbfa90e880d09c21c39044. Os snapshots intermediários foram a20d679996c4cf68a60d64bd76f9ed6bc8a02bdcdc519a48e659090cff68a276 (MOD-005) e f71f396b351b91342ad3781e7f75e30062820466e5ade37a5c985541acc3ec9f (refino MOD-004), com 92/92 checkboxes então. O backlog **atual**, após registrar MOD-006, é SHA-256 c889167b980599f8639986da067f2e878359e28a580ef6aa9ba6a1be7ac05f3d e o ledger cobre **93/93** checkboxes. Os resultados 91/91 e 10/10 abaixo registram somente a primeira entrega; ver [MOD-005](MOD_005_FIFTH_EQUIPMENT_ABILITY_SLOT_2026-09-23.md), [refinamento MOD-004](MOD_004_EQUIPMENT_REFINEMENT_2026-09-23.md) e [MOD-006](MOD_006_ADDITIONAL_TEXT_LANGUAGE_2026-09-27.md) para os acréscimos. Nenhum arquivo de src/runtime/FfxHooksDll foi editado nesta branch de documentação.

Há código C++17 de regras puras em research/mod_ideas_precode/include e src. Ele representa somente comportamento que pode ser calculado com os parâmetros fornecidos: cap de dano, curva de DEF, opções de afinidade elemental, orçamento de OD/Quickcast/Fury, duração de status, operações de equipamento e decisões de elenco. O código não conhece ponteiros do jogo, não instala detours, não desenha UI, não altera arquivos e não está ligado à DLL. O registro mantém explícitas as propostas que ainda exigem escolha ou RE.

| Camada executada | Resultado | O que demonstra |
|---|---:|---|
| Registro contra backlog congelado | 91/91, IDs únicos | Cobertura de todos os checkboxes, incluindo decisões e dois itens de template. |
| Modelo C++17 em Linux | 116/116 | Aritmética e transações puras para as variantes nomeadas. |
| AddressSanitizer + UndefinedBehaviorSanitizer | 116/116 | Sem falha detectada no mesmo conjunto de entradas. |
| Cross-compile MinGW PE32 i386 | PASS | Fonte compila para Windows 32-bit; importações KERNEL32.dll e msvcrt.dll. |
| Harness MinGW executado na VM | 116/116; SHA da cópia verificado; temporário removido | Execução isolada em Windows x86, RT1 de modelo. |
| MSVC Community x86, /std:c++17 /W4 /WX /MT, executado na VM | 116/116; temporário removido | Compatibilidade com o compilador e padrão atuais de Hooks, ainda sem DLL/game. |
| Assinaturas diretas do PE FFX.exe arquivado | 11/11 | RVAs de cálculo, afinidade, status, OD e menu existem no binário com SHA fixado. |
| FFX Editor: command.bin e weapon.bin | 15/15 testes filtrados | Command/item/monmagic2 com fixtures reais e weapon.bin em mock sintético. |
| Escrita RT0 de monmagic2.bin | 247→248; cinco checks de preservação/readback | Novo registro staged em diretório temporário. Não há cast observado no jogo. |
| Edição in-memory de comandos | 7 ataques elementais, 1 custo OD e 1 dono, bytes locais/readback | Campos existentes podem ser escritos pelo Editor no fixture fixado. |
| Diagnóstico ligado ao código atual de save do Editor | 4/4 reproduções | Leitura deslocada, amostragem de 146 slots ocupados e escrita que cruza o próximo slot; bloqueia usar esse writer como base da forja. |
| Suíte integrada | PASS_RT0_AND_MODEL 10/10; quatro entradas de teste permaneceram idênticas | Execução automatizada dos gates offline acima. |

Os probes de identidade recusaram cópias alteradas do PE e dos dois fixtures. A suíte salva logs/JSON apenas em research/mod_ideas_precode/build/, ignorado por Git. Nenhum teste iniciou FFX, instalou DLL, abriu save para gravação ou realizou RT2.

## Identidade das fontes e isolamento

| Fonte | Identidade conferida | Limite |
|---|---|---|
| Hooks desta branch | Base a70a3543251d029f7d3aa2fa18ef1620374eb3e0; worktree gerido em /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-hooks | Main e a lane fastload-autosave-20260916 continuam separadas; nenhuma alteração de runtime nesta branch. |
| FFX Editor para compilar/testar | Checkout **limpo e detached** em /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor, commit e5f05554426f27a83d4ee70f7be32695431ac66b | O checkout principal do Editor já estava sujo (191 caminhos quando medi); os builds ocorreram só no worktree separado. |
| FFX.exe PE de referência | 10.675.712 bytes, SHA-256 78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced, PE32 i386, ImageBase 0x400000, TimeDateStamp 0x55D2F3CC | O arquivo arquivado no host é byte-idêntico a C:/IDA_DB/FFX_copy.exe na VM. O caminho original D:/SteamLibrary/.../FFX.exe salvo no IDB não existe hoje nessa VM; identidade de uma instalação em uso continua indeterminada. |
| IDA canônico | C:/IDA_DB/ffxoficial.exe.i64 | O Luna observou bfaaf2e9... (109.288.676 B), depois 2db22941... (109.481.351 B). Depois do encerramento por inatividade do servidor IDA, medi bab0b5b213ff01eabd5ca5e2ea8950b8766f79887a990462d44ca3aa2ddb368a (109.450.280 B, mtime 14:09 da VM). A causa da mudança não está provada; Sol não reabriu nem salvou o IDB. Para novas leituras, usar cópia consistente e registrar hash antes/depois. |
| Fantasia | Clone e upstream HEAD 64b03ae915c32f47d4a84aefffcd3130cb13ddce, MIT | Somente src/balance/elemental_affinities.cs contém patch de gameplay neste commit; os demais grupos são estrutura vazia. |
| Fahrenheit | c149c847b3a24a66114956f87f1b008599736f75, LGPL-3.0-or-later | Referência de ABI/FhMethodHandle; nenhum código copiado. |

O corpus local tem 319 diretórios Git. Busca lexical em código/Markdown/JSON encontrou DmgCalc_Elem em três forks Fahrenheit e no Fantasia; ElementalAffinitiesRebalanced em dois arquivos do próprio Fantasia; Quickcast, Useful Kimahri e Persistent KO em nenhum arquivo no escopo varrido. “Fury Overdrive” apareceu em um README de trainer, e Geosgaeno em 70 arquivos majoritariamente de guias, dados e transições. Resultado negativo é limite da busca, não prova universal de inexistência.

## Claims do Luna: confirmadas, corrigidas e qualificadas

### Combate e comandos

1. **Cap atual confirmado com uma exceção importante.** O PE em RVA 0x38ED1A lê o bit 0x0800 do ator em +0x6BE. Entre as RVAs 0x38ED31 e 0x38ED37, calcula 9.999 ou 99.999. O byte baixo dos flags do comando em +0x20 pode forçar 99.999 com bit 0x80; na ausência dele, bit 0x40 pode forçar 9.999. RVA 0x38ED41 contém BB 9F 86 01 00 (mov ebx, 99999); RVA 0x38EDD5 contém 7E 02 8B C3 89, assinatura do clamp. O relatório anterior resumiu o caso usual BDL, mas uma implementação global deve preservar/decidir essas precedências. O modelo VanillaDamageCap cobre os quatro ramos observados; CapPositiveDamage modela a proposta configurável, sem afirmar que o jogo já a usa.
2. **Fórmula de dano fixada à dispatch atual.** FFX_Damage_FormulaDispatch começa em RVA 0x389CB0; o enum do Editor tem IDs 0..23. Trocar um ID existente em command.bin é dado. C# formula arbitrária, AGI de arma, MP atual e curva global pedem dispatch/hook e contrato de IDs persistidos. Sem nova implementação não há prova de múltiplas fórmulas custom coexistindo.
3. **Afinidade: RVA e ABI cruzados.** O PE em RVA 0x38A420 acessa argumentos de pilha em [EBP+8], [EBP+0x10] e [EBP+0x14]; o caller em flat 0x78EA20 empilha quatro valores antes de call 0x78A420. O retorno é RET simples. Isso sustenta o delegado cdecl de quatro argumentos do Fantasia/Fahrenheit e contradiz o protótipo fastcall de três argumentos persistido no IDB. Nosso modelo aceita apenas as cinco flags Fire/Ice/Thunder/Water/Holy; Earth/Wind/Dark continuam sem prova semântica de motor.
4. **Bahamut/Valefor agora têm IDs exatos em dados.** O fixture command.bin (SHA-256 db4c87f33f27a7df41bc8a520bc2246b664167319386fe99da9880ae06000429) contém Valefor #203 Attack, #204 Sonic Wings, #205 Energy Blast, #206 Energy Ray; Bahamut #216 Attack, #217 Impulse, #218 Mega Flare. Todos têm ElementFlgs=0. O FFX Editor inseriu Water 0x08 nos quatro e Holy 0x10 nos três em memória; WriteList/readback alterou exatamente sete bytes nos offsets esperados (+0x2D em cada linha 0x60, cabeçalho 0x14). Há caminho de **authoring RT0 por dados** para essa versão dos sete ataques. Afinidade efetiva, animação, efeitos e Yunalesca ainda pedem RT2 finito.
5. **Campos de comando e menu OD são distintos.** No mesmo fixture, o custo OD do Ronso Rage #104 de Kimahri mudou de 100 para 40 alterando apenas o byte do arquivo 0x273A (campo +0x26). CharacterUser do comando #203 mudou Valefor→Bahamut em memória alterando apenas 0x4C4D (campo +0x19). O gate de comando no PE inicia em RVA 0x38ABE0 e o de menu-ready em 0x39AF70. O teste de arquivo prova edição localizada; não prova que o anel OD apareça sob 100 ou que o menu aceite novo dono.
6. **Novo ataque de monstro tem piloto concreto.** No commit limpo do Editor, --monmagic-grow-rt0 gerou linha #247/operando 0x60F7 em monmagic2.bin, preservou prefixo de registros/texto, releu o novo ID e verificou preserve-write. Fonte do fixture SHA-256 85402f76af8b0bacdfb3850a1c8907ef76f825b43ad22af3a0bfddc05983707a. O writer imprime “game-loadable”; o teste aqui demonstra estrutura/readback RT0 e não observou o jogo carregando/castando.
7. **Kimahri Blue Mage permanece composição.** KimahriExtendedCommandWriter e KimahriExtendedPackRt0 existem no Editor, mas o Program.cs do commit e5f0555 não expõe o pack como comando CLI. GridTeach, Lancet dual grant e RonsoMana em Hooks são referências para aprender, menu e custo; nenhum deles implementa o novo Overdrive inteiro. Definir OD versus skill MP, aprender habilidades e salvar antes de escolher ponto de instalação.

### Equipamento, save e dados de progressão

8. **Correção material ao backlog do Luna: Save Editor inseguro para forja.** FfxSaveEquipment.cs declara SlotStride=22, lê/grava quatro u16 em +15/+17/+19/+21 e SaveEditor_EquipmentBindings.cs repete os offsets. No fixture PC real de 26.880 B, SHA-256 6e2a617b58cc058a72526f20a31bf00b4d79847f7784ce5845935538e77af0b3, os quatro IDs plausíveis de slot0 estão em +14/+16/+18/+20: 0x8063, 0x8064, 0x802A, 0x8000. A classe atual lê 0x6480, 0x2A80, 0x0080, 0x2380. Em 146 slots ocupados do fixture, 575/584 IDs lidos em +14 são plausíveis versus 1/584 em +15. O probe que compila a classe original mostrou que escrever Auto4=0x1234 em +21 altera o primeiro byte do próximo slot para 0x12. Isto reproduz defeito do writer do Editor, não prova defeito do save do jogo. Reforja/fusão/expansão/Clear Sphere/evolução em save ficam BLOCKED_SAVE até layout e writer serem corrigidos e revalidados separadamente.
9. **Modelo estático de weapon.bin confirmado em fonte e mock.** WeaponGear_File.cs usa registro de 0x16 bytes, owner +0x04, tipo +0x05, capacidade +0x0B, model ID +0x0C e quatro habilidades u16 em +0x0E/+0x10/+0x12/+0x14. Os testes filtrados do Editor passaram 15/15, incluindo weapon.bin em mock e comando/item/monmagic2 com fixtures. Não foi encontrado weapon.bin real no corpus local pesquisado; não há afirmação de round-trip em arquivo real nem de transação no menu de Rin. O protótipo C++ só serializa o template de 22 B; não grava save.
10. **MixTable já tem writer no Editor.** MixTable_File.cs lê/escreve prepare.bin; MixTableEditor_DataModel.cs chama Write, e Main_Window registra “WRITABLE prepare.bin”. O texto “Mix Overhaul” não define receitas, efeitos ou regra nova e não foi encontrado prepare.bin fixture nesta pesquisa. A rota de mudar apenas resultados de pares existentes é fonte-confirmada, sem RT0 local nem gameplay.
11. **Useful Kimahri tem campo inicial de Sphere Levels.** PlayerKernel_File.cs lê/escreve SphereLevelsAvailable em +0x3B de uma linha de ply_save.bin de 0x94 bytes. Isso melhora a rota de dados para a opção “começar com 3 níveis”. Sem fixture ply_save.bin e sem teste New Game, ainda não há prova da concessão inicial nem da opção por grade. Habilidades iniciais exigem mapear a AbiMap/grade e o menu.
12. **Nível +10 não tem campo identificado.** Os 22 bytes de template/save examinados não expõem nível individual; modelos predefinidos poderiam simular variantes. Persistência dinâmica, UI, regra de bônus e loot precisam de desenho. Os custos de slot têm duas propostas divergentes (Kari 1 por slot; Dawn 1/2/3/4); ambas são opções explícitas no modelo C++.

### Fluxo de combate, UI e eventos

13. **MP-0 foi interpretado incorretamente no resumo anterior.** O arquivo original fornecido pelo usuário, linha 228, diz que o efeito MP-0 usa o MP máximo do personagem para o bônus de MAG e adiciona +3 por cima. Ele não limita o bônus total a +3. O modelo C++ agora representa a leitura do texto original. Mecanismo de consumo após a ação e interação com Focus ainda pedem hook/teste no jogo.
14. **Element Boost também foi misturado a outra habilidade no backlog anterior.** O anexo original, linha 65, diz apenas “Damage x1.2 with that element”. A linha 66 define Energy Boost para dano/cura e gauge OD acima de 50%; a 67 define Energy Burst. Portanto, cura e limiar de OD não pertencem à regra original de Element Boost. O modelo aplica ×1,2 ao dano mono-elemental correspondente e deixa ataque multielemental sem política escolhida.
15. **Auto-habilidades numéricas agora têm modelos parciais.** Efficiency reduz custo MP/OD em 25% e, com Half MP Cost, deixa 25% do custo MP; Vampirism calcula 25% do HP máximo do alvo morto; MP Regen calcula 2% do MP máximo; P-/M-Trade retorna ×0,8/×1,2 por canal; Elude soma 50 Evasion no Defend. Hero’s Bravery preserva duas leituras possíveis de “+25%” (pontos versus relativo) e expõe valores acima de 100%. Arredondamento, alvo, evento de kill/turno/Guard, ordem de multiplicadores e autoability ID/handler continuam sem implementação runtime.
16. **Status e Threaten permanecem RE parcial.** O PE confirma entradas de funções em RVA 0x38AEC0 (matriz), 0x38A950 (acerto) e 0x389750 (crítico). O Luna leu a lógica de duração no IDA e anotou tabela temporal flat 0xC42457. Sol não reabriu o IDB após a mudança de hash; não confirmou ABI/status writeback por execução. Refresh, resistência de duração, Poison/Sleep e Threaten precisam de testes separados e regras de persistência.
17. **Boss OD reset tem precedente de ATEL, ainda sem filtro de boss.** FFX Customizable Battle Tweaks, apply_buffs.py linhas 191–211 e 366–380, injeta reset de Overdrive em scripts de início de batalha e exclui dois tutoriais. A proposta Fantasia restringe a chefes; é preciso enumerar encontros e preservar as exclusões. Esse projeto externo não tinha LICENSE superior encontrado; nenhum código foi adaptado.
18. **UI, CTB e assets continuam sem consumer comprovado.** Ícones de participação/status/Arena, HP/MP de Aeons, preview após revive, Thunder Plains, butterflies, instant suitcase potions, novas entradas do Sphere Monitor e Geosgaeno→Anima exigem mapeamentos individuais. O ledger marca NEEDS_RE, NEEDS_SPEC ou ASSET_GATE. Protótipo de cálculo não é prova de UI ou substituição de rig/animação.

## Contratos de integração propostos para Astra

| Superfície futura | Entrada já preparada | Adaptação e gate necessários |
|---|---|---|
| Dano | CapPositiveDamage, VanillaDamageCap, DefenseMultiplier, DamageWithBreakContribution, MagicBonusFromMp | Comparar todos os caminhos HP/MP/CTB, cura, comando 0x20 bits 0x80/0x40, acumulador de hits e HUD. 250 hits de 9.999.999 excedem int32; o modelo sinaliza overflow. Hook default-off com assinatura da build e teardown. |
| Elemento | ElementalDamage, HasOppositeElementWeakness; licença MIT em THIRD_PARTY_NOTICE.md | Escolher modo e precedência; caller cdecl de quatro args em RVA 0x38A420; manter composição com Fahrenheit/outros hooks. Testar null/absorb, cura, Aeons, múltiplos elementos e bits 0x20+. |
| OD/Quickcast/Fury | InspectOverdriveGate, PlanQuickcast, PlanFuryBudget | Custo de command.bin é u8. Dobrar MP 200→400 exige custo calculado em runtime; decidir rank floor/ceil/fixo e tratamento de One/Half/Zero MP Cost. Menu-ready e confirmação são gates separados. |
| Status | DurationAfterResistance, RefreshStatusDuration, CanThreatenAgain | Definir Replace versus KeepLonger, resistência por monstro, estado por batalha e persistência entre batalha/save. Mapear writer e ABI antes do hook. |
| Equipamento | Decode/EncodeMasterGear; PlanReforge, PlanMerge, PlanExpandSlots, PlanClearAbility, PlanEvolveAbility | São planos puros para template/valor; nenhum débito/inventário/save/menu está implementado. Corrigir Save Editor, definir ID/modelo de destino, duplicatas, equipamento equipado, Brotherhood/Celestial, consumo transacional e rollback. Nenhum hook de Rin deve depender do writer atual. |
| Auto-habilidades numéricas | PercentOfMaximum, PlanEfficiencyCost, PlanVampirismHeal, PlanMpRegen, TradeDamageMultiplier, PlanElude, PlanHeroBraveryCrit, ElementBoostMultiplier | Definir rounding, caso multi-elemento, ponto exato de kill/turno/Defend/custo e prioridades com buffs. O modelo não registra novo autoability ID nem faz trigger no jogo. |
| Elenco | EligibleForGuaranteedAp, FirstAvailableBackline | Ligar ao roster elegível, AP real/No AP/Double/Triple/Overdrive→AP e regra de troca de combate/natação. O modelo não atribui AP nem altera CTB. |
| ATEL, UI, assets | Ledger e probes propostos | Localizar eventos e estado salvos, desenhar interface, identificar capacidade de slot/modelo, e fazer RT0 por arquivo antes de RT2 por caso finito. |

Regra de Hooks para qualquer integração futura: OFF por padrão, perfil/assinatura do EXE, chamada original preservada, instalação na thread apropriada, erro fechado, remoção reversível e teardown revisado. Padrão de código da DLL é MSVC C++17; o pacote foi compilado nesse modo. Nenhum endereço deste relatório deve ser aplicado em outro EXE apenas porque tem o mesmo nome.

## Como reproduzir

No worktree desta branch, o comando a seguir executa as dez etapas offline. A referência PE abaixo é o arquivo arquivado de SHA 78ce3439...; passar outro EXE produz falha de identidade.

~~~bash
python3 research/mod_ideas_precode/run_offline_suite.py \
  --editor-root /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor \
  --ffx-exe '/home/wanderson/Documents/ffx-editor-main/docs/history/DOSSIÊ FFX 01-06-2026/e-l-vamos-n-s__PT6_OWN_CHAT_SATANIC_DOSSIER_2026-06-01/dependencies/D/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/FFX.exe'
~~~

Resultado final: PASS_RT0_AND_MODEL, 10/10; Editor 15/15; C++ 116/116 normal e sanitizado; PE 11/11; Save Editor defeito 4/4; monmagic2 247→248; quatro entradas idênticas antes/depois. Saída e logs ficam em research/mod_ideas_precode/build/offline_suite_summary.json e arquivos irmãos (ignorados por Git).

Os testes separados de Windows x86 foram:

~~~bash
python3 research/mod_ideas_precode/probes/run_win32_vm.py \
  --exe research/mod_ideas_precode/build/mod_ideas_win32.exe \
  --vm-exec /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor/research_tools/Vm/vm_exec.sh
python3 research/mod_ideas_precode/probes/run_msvc_vm.py \
  --vm-exec /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor/research_tools/Vm/vm_exec.sh
~~~

MinGW PE32 na VM: 116/116, hash da cópia conferido, temporário removido. MSVC Community x86 /std:c++17 /W4 /WX /MT: 116/116, temporário removido. A primeira tentativa MSVC detectou comparação signed/unsigned no teste de retaguarda; foi corrigida e o ciclo passou. Uma primeira tentativa da suíte .NET revelou colisão de apphost entre dois csproj no mesmo diretório; os projetos foram separados em probes/save_layout e probes/aeon_elements, e a suíte final passou 10/10.

## Ordem sugerida de revisão Astra

1. Conferir o diff desta branch: nenhum arquivo de src/runtime ou instalação de jogo deve aparecer. Preservar os SHA históricos; conferir o SHA do backlog atual e a cobertura 93/93 do ledger após MOD-006.
2. Revisar o defeito do Save Editor com o fixture fixado e os offsets do formato real. Abrir tarefa separada no Editor para corrigir/testar; manter MOD-004 BLOCKED_SAVE até writer e consumer terem prova.
3. Revisar a seleção do cap com flags de comando e o risco de acumulador/HUD antes de reutilizar o modelo no ponto RVA 0x38EDD5. Decidir qual contrato de MOD-001 versus MOD-002 será adotado.
4. Comparar a adaptação de Fantasia com o código MIT fixado, confirmar chamada cdecl/owner do hook, e verificar sua convivência com outros plugins.
5. Revisar os oito edits in-memory de comando (sete elementos e um custo OD), mais o campo CharacterUser; decidir se o mod quer todos os golpes elementais ou apenas ataques selecionados. Só depois preparar pacote de dados.
6. Fechar escolhas de MP-0, Quickcast, Fury, status, slot costs, duplicatas de habilidades e capacidade de UI/ATEL/asset. O ledger mostra os itens que não têm definição suficiente.
7. Para cada feature selecionada, propor/adicionar adapter runtime em outra branch, testar RT0/RT1 afetados, e solicitar autorização específica para deploy/RT2 finito conforme docs/RT2_PROTOCOL.md. Não promover o resultado do modelo como gameplay.

O checkout principal ainda possui a cópia não rastreada de docs/MOD_IDEAS_BACKLOG.md com o mesmo SHA. Antes de integrar esta branch no principal, verificar se o documento foi adicionado lá; arquivos idênticos se resolvem diretamente, versões divergentes devem ser conciliadas conscientemente. Não houve push, PR, merge ou publicação neste trabalho.
