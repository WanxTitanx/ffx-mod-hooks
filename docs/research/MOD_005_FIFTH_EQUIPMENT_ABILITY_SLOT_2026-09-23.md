# Jarvis-HOOK — MOD-005: quinta auto-habilidade por arma ou armadura

**Escopo:** cada peça continua sendo uma arma ou armadura, mas pode ter uma quinta posição de **auto-habilidade customizável**. Isto não significa equipar uma terceira peça no personagem, nem criar o quinto registro de equipamento do inventário. A ideia chegou diretamente do usuário em 23/09/2026, sem regra de custo, desbloqueio ou distribuição de drops definida.

**Veredito para o FFX HD PC examinado:** uma quinta habilidade real é **tecnicamente concebível com hooks e armazenamento adicional**, mas não há implementação validada. Alterar somente `slot_count` de 4 para 5 ou gravar um quinto `u16` depois do quarto é incorreto: esse endereço pertence ao próximo equipamento, ou aos dados de personagem quando se trata da última peça. O FFX Editor atual escreve apenas quatro habilidades. Não foi encontrado, nas buscas delimitadas abaixo, um mod pronto de quinta auto-habilidade para reaproveitar. Este trabalho é RT0 estrutural/RE; nenhum jogo, DLL ou menu foi executado.

## Identidade, fontes e alcance da evidência

| Fonte | Identidade fixada | Uso e limite |
|---|---|---|
| Executável PC analisado | `FFX.exe`, PE32/i386, ImageBase `0x400000`, timestamp PE `0x55D2F3CC`, 10.675.712 B, SHA-256 `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced` | Endereços da tabela abaixo são **flat IDA**; `RVA = flat - 0x400000`. Não transportar a outra versão/região sem assinatura. |
| Base IDA | `windows11-dev-next`, `C:\IDA_DB\ffxoficial.exe.i64`; leitura feita sobre cópia `C:\Users\Public\Documents\mod005_ida_s0_20260923`, SHA-256 pré-abertura `bab0b5b213ff01eabd5ca5e2ea8950b8766f79887a990462d44ca3aa2ddb368a` | Serviço idalib restrito a funções de leitura. A base canônica não foi aberta diretamente nesta pesquisa e conservou o SHA pré-abertura após o serviço. A cópia foi regravada no fechamento: SHA pós-fechamento `24c85072bc292aabce21d60a3419461e14f2616731e1f4071fba05f3994d6671`, 109.663.516 B. Nomes são anotações de RE e podem ser imprecisos; offsets e fluxo de bytes são a evidência principal. |
| Fahrenheit | [`equip.cs`](https://github.com/fahrenheit-crew/fahrenheit/blob/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx/equip.cs), [`savedata.cs`](https://github.com/fahrenheit-crew/fahrenheit/blob/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx/savedata.cs), [`btldrop.cs`](https://github.com/fahrenheit-crew/fahrenheit/blob/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx/battle/btldrop.cs); commit `c149c847b3a2`, LGPL-3.0-or-later | Modelo de estruturas cruzado com PE e save. Fonte externa lida; nenhum código adaptado. |
| FFX Editor | commit `e5f05554426f27a83d4ee70f7be32695431ac66b`; `EquipmentStruct.cs`, `WeaponGear_File.cs`, `ShopGearCatalog_File.cs`, `FfxSaveEquipment.cs`, `AutoAbility_File.cs` | Confirma os writers e o limite do authoring existente. Checkout do Editor foi apenas lido. |
| Save PC real | `FFXProjectEditor.Tests/Fixtures/Save/user_ffx_000`, 26.880 B, SHA-256 `6e2a617b58cc058a72526f20a31bf00b4d79847f7784ce5845935538e77af0b3` | Probe lê o fixture e faz alterações **somente em cópias de memória**; arquivo não entra nesta branch. |

O nível RT0 significa aritmética de formato, fonte e descompilação, conforme [`RT2_PROTOCOL.md`](../RT2_PROTOCOL.md). A base IDA já continha nomes/comentários de sessões anteriores; desta sessão são os corpos consultados no snapshot. Isto não é prova de execução do jogo. Os arquivos decompilados temporários ficaram fora do Git em `/tmp/ffx-mod005-ida-20260923/`.

## Contrato de dados: onde termina a quarta habilidade

O equipamento tem **22 bytes (`0x16`)** no save, no `weapon.bin` e no catálogo `shop_arms.bin` que o Editor modela. Os campos decisivos de uma peça são:

| Offset na peça | Tipo | Significado |
|---:|---|---|
| `+0x00` | `u16` | ID/nome do modelo. |
| `+0x04`, `+0x05` | `u8`, `u8` | Dono e tipo, entre outros bytes de controle. |
| `+0x0B` | `u8` | Capacidade (`slot_count`). O byte comporta numericamente 5; os consumidores não. |
| `+0x0C` | `u16` | ID/aparência do modelo. |
| `+0x0E`, `+0x10`, `+0x12`, `+0x14` | `4 × u16` | Quatro IDs de auto-habilidade. O último ocupa `+0x14..+0x15`. |
| `+0x16` | `u16` de **outro campo** | Primeiro campo da peça seguinte; na peça 199, primeiro campo do `PlySave`. Não há campo 5 aqui. |

Fontes: `EquipmentAbilityArray` com `[InlineArray(4)]` em Fahrenheit `equip.cs:8`; `EquipmentArray` com 200 elementos em `savedata.cs:87`; `equipment@0x449C` e `ply_saves@0x55CC` em `savedata.cs:301-302`; Editor `EquipmentStruct.cs:21-26`, `WeaponGear_File.cs:41-42,88-91,139-142` e `ShopGearCatalog_File.cs:13-14,94-97`. A lista de **oito** habilidades por arma/armadura em `btldrop.cs` é um conjunto de candidatas para sorteio, não oito posições persistidas na peça.

No save de PC, o cabeçalho tem `0x40` bytes. Assim, o bloco de equipamentos começa no arquivo em `0x44DC = 0x40 + 0x449C`, tem `200 × 22 = 4400` bytes e acaba em `0x560C = 0x40 + 0x55CC`. O payload de `PlySave` começa exatamente nesse byte. A rotina de carga `0x8B5450` copia `RowStruct+64 → SaveData` por `0x68C0` bytes; a de escrita `0x8B3E10` faz a cópia inversa com o mesmo tamanho. Portanto, expandir os 200 registros para 24 bytes dentro do save atual deslocaria pelo menos 400 bytes de campos posteriores e exigiria mudança do contrato de arquivo/carga.

O probe [`probe_save_overlap.py`](../../research/mod_005_fifth_slot/probe_save_overlap.py) confirmou no fixture fixado: primeiro item possui IDs `0x8063, 0x8064, 0x802A, 0x8000`; o suposto quinto `u16` do primeiro item coincide com `name_id=0x5023` do item seguinte. No último item coincide com `PlySave` em `0x560C`. Escrever `0x800A` em uma cópia de memória altera apenas os dois bytes do nome seguinte, e o fixture original conserva o SHA-256. Isso é um contraexemplo executável à solução “só adicionar `Ability5`”.

Há ainda um defeito **independente** no Editor atual: `FfxSaveEquipment.cs:102,121` usa `+15/+17/+19/+21` para as quatro habilidades no save, enquanto o registro real usa `+14/+16/+18/+20`. O probe anterior da branch reproduziu escrita de `Auto4` atravessando o próximo registro. Corrigir e validar esse writer é pré-requisito para usar o Editor como ferramenta de authoring/teste de save; isso não cria um quinto slot por si só.

## Cruzamento de consumidores no executável

Endereços e larguras observados na cópia da base IDA. Os nomes servem para localizar funções, não garantem a interpretação de cada chamada. A maioria é evidência alta para **este PE** porque o corpo faz a leitura/cópia explícita; interpretação de efeito e cobertura global continua sujeita a RT2 e xrefs adicionais.

| Flat / RVA | Leitura ou escrita observada | Consequência para MOD-005 |
|---|---|---|
| `0x7ABBF0 / 0x3ABBF0` `FFX_Field_GetModelRecordByCode` | Registro normal: índice até 199 e endereço `base + 22 × índice`; famílias especiais 7/B também usam registros de 22 B. | Capacidade 5 não muda stride nem cria armazenamento. Planejar separadamente peças especiais/Aeons. |
| `0x7AB930 / 0x3AB930` `FFX_Field_RegisterModelRecord` | Procura vagas em saltos de 22 B; copia **5 DWORD + 1 WORD = 22 B** ao registrar equipamento. | Uma habilidade em `+22` não é copiada e corromperia a peça vizinha. Hook de criação/atribuição é necessário para sidecar. |
| `0x7ABA10 / 0x3ABA10` `FFX_Field_SwapModelRecordFull` | Troca registros inteiros de 22 B e ajusta referências equipadas. | Trocar/reordenar peças deve trocar a habilidade extra junto; índice sozinho não é identidade imutável. |
| `0x7ABCC0 / 0x3ABCC0` `FFX_Field_FreeModelRecord` | Libera registro e referências equipadas. | Limpar sidecar para evitar herança da quinta habilidade por peça futura. |
| `0x7AD650 / 0x3AD650` `FFX_Field_ReplaceTreasureModelRecord` | Copia modelo de tesouro em blocos `5 DWORD + 1 WORD`. | Substituição de recompensa exige sincronização da quinta habilidade. |
| `0x798C20 / 0x398C20` `FFX_Battle_AddRewardToKernel` | Usa temporários com stride 22; `FFX_Math_ClampInt(..., 1, 4)` em `0x798D3D`; preenche `u16` a partir de `+14` e zera posições restantes até **4** em `0x798E3D..0x798E61`. | Drop comum sai em 22 B e nunca gera posição 5 sozinho. As oito habilidades de pool são candidatas. |
| `0x799430 / 0x399430` `FFX_Battle_ProcessTreasureKernelEntry` | Recompensa roteirizada usa `22 × RewardSlot` e grava quatro `u16` em `+14/+16/+18/+20`. | Segundo produtor de equipamento deve ser tratado; alterar só drop comum não cobre tesouros. |
| `0x798EC0 / 0x398EC0` `FFX_Menu_CountGearAbilitySlots` | Conta explicitamente os `u16` em `+14/+16/+18/+20`, compara com `slot_count@+11`. | A contagem/normalização não descobre uma quinta habilidade. |
| `0x8D8A70 / 0x4D8A70` `FFX_CustomizeMenu_DrawAbilitySlots` | Desenha molduras em laço limitado por `slot_count@+11`, mas ícone/texto lê `+14` com contador **4** em `0x8D8B02`. | `slot_count=5` pode desenhar uma quinta moldura vazia, sem item real; layout pode transbordar. |
| `0x8D02B0 / 0x4D02B0` e `0x8D63C0 / 0x4D63C0` | Previews de equipamento e de habilidade iniciam em `+14` e usam `n4=4` (`0x8D039C`, `0x8D64B7`). | Um único patch de desenho não cobre os previews. |
| `0x8D9340 / 0x4D9340` `FFX_CustomizeMenu_CalcKaizouCost` | Percorre **`slot_count` WORDs** desde `+14`; para ID válido consulta a tabela usando `id & 0xFFF`. | Com capacidade 5 no registro de 22 B, preço lê `name_id` da próxima peça como se fosse auto-habilidade. É um erro funcional além do visual. |
| `0x79C610 / 0x39C610` `FFX_Btl_AggregateActorEquipAbilities` | Para arma e armadura (2 peças), lê `u16` desde `+14` com contador **4** (`0x79C8A3`), consulta `FFX_Battle_AutoAbilityTable` e agrega status, afinidades/flags e efeitos ao ator. | A quinta habilidade nunca ativa em combate pela rotina atual. Reproduzir efeitos/ordem e refresh requer hook próprio, não só uma linha de UI. |
| `0x7A0C40 / 0x3A0C40` `FFX_Btl_CheckCommandWordInList` | Varre `u16` a partir de `+14` e para quando o contador atinge 4. | Consultas por IDs, usadas por `FFX_Btl_CharData_DecideAbility`, ignoram o quinto. |
| `0x7A0D10 / 0x3A0D10` `FFX_Btl_CharData_DecideAbility` | Recebe modelos de Customize/recompensa e faz diversas buscas via helper de quatro WORDs. | Nome/modelo/seleção e possíveis regras especiais devem ser auditados por tipo de habilidade, inclusive IDs tratados diretamente. |
| `0x8B3E10 / 0x4B3E10`, `0x8B5450 / 0x4B5450` | Salvar/carregar copia `0x68C0` bytes entre save e RAM, com cabeçalho `0x40`. | Save vanilla não transporta a quinta habilidade; precisa extensão/sidecar com transação e migração, ou um formato novo completo. |

As assinaturas de RE relevantes são larguras (`u8` capacidade, `u16` habilidade, stride `0x16`, cópia `0x68C0`), não promessa de ABI para detour. Um hook futuro só deve usar assinatura exata deste PE, perfil default-off, estado por thread quando aplicável, e teardown reversível. O menu de `0x8CEFF0`, citado em notas antigas como Customize, é **Sphere Grid** nesta base e não prova nada sobre a quinta habilidade.

## Rotas de implementação comparadas

| Rota | O que entrega | Dados/Editor | Hooks e riscos | Veredito |
|---|---|---|---|---|
| **Capacidade 5 e quinto WORD em `+22`** | Nada confiável. | `slot_count` aceita o byte 5, mas os formatos de 22 B não têm `Ability5`. | Sobrescreve registro vizinho/PlySave; custo de Customize lê vizinho; battle e previews usam 4. | **Rejeitada por prova RT0.** |
| **Habilidade composta dentro de um dos quatro IDs** | Algumas combinações de efeito em uma linha/slot vanilla. | `a_ability.bin` contém múltiplos campos de status, elementos e flags por entrada; `AutoAbility_File.cs` lê/escreve a entrada de `0x6C`. É possível investigar receitas compostas por dados e ID customizado. | Efeitos especiais que consultam IDs/ordem, nome, preço e compatibilidade de cada par precisam análise; não oferece escolha livre de quinta habilidade nem cinco linhas. | **Atalho limitado**, não satisfaz a proposta geral. |
| **Sidecar por save + hooks** | Cinco habilidades lógicas preservando registros vanilla de 22 B e `slot_count≤4` no jogo. | Editor pode preparar quatro nativas após correção de seu writer; quinta fica em metadado do mod. `weapon.bin`/loja permanecem formato vanilla, salvo regras novas de drop. | Hooks para ciclo de vida do equipamento, efeitos de batalha, preço, menus, customização, save/load e falhas de sincronização. Nenhuma escrita extra deve ir em `record+22`. | **Arquitetura candidata** com melhor compatibilidade, ainda sem RT1/RT2. |
| **Expandir formato para 24 B em RAM, arquivos e save** | Um quinto `u16` fisicamente adjacente. | Requer modificar writers/loaders de `weapon.bin`, `shop_arms.bin`, save e suas versões. Só o bloco de 200 peças adicionaria 400 B: `0x68C0→0x6A50` de payload e `0x6900→0x6A90` de arquivo caso o formato inteiro seja deslocado. | Redirecionar **todos** os acessos `22×índice`, cópias de 22 B, offsets de campos posteriores, CRC/tamanho do save, UI e combate; compatibilidade com saves/mods vanilla complexa. | **Pesquisa de formato novo**, não caminho recomendado para primeira entrega. |

O “quinto slot” deve ser especificado como **posição lógica separada** enquanto a rota sidecar é usada. Manter `slot_count` original no intervalo 0–4 evita que loops antigos, como o do preço, avancem em memória indevida. Renderização, seleção, custo e gravação do quinto devem receber o ID do sidecar explicitamente. A arquitetura precisa tratar itens com menos de quatro posições: proposta inicial é exigir quatro nativas antes de liberar a quinta; a forma de desbloquear e o custo ficam em aberto com o usuário. MOD-004 já propõe expansão **até quatro**, então compartilha UI/custos mas não resolve MOD-005.

## Escopo de implementação que um revisor pode transformar em patch

1. **Modelo e identidade:** `FifthAbility { save_identity, inventory_slot, model_fingerprint, ability_id, unlocked }` para as 200 peças normais. Fingerprint sozinho não distingue cópias idênticas; manter índice + geração e acompanhar operações. Investigar explicitamente modelos especiais/aeons (`FFX_Field_GetModelRecordByCode` famílias 7/B) antes de prometer suporte a eles.
2. **Persistência:** arquivo sidecar versionado por save/slot de jogo, com checksum de versão e associação ao save vanilla. Escrever após confirmação de save, via temporário + rename e journal de recuperação; não fazer I/O pesado em `DllMain`. Salvar dois arquivos não é uma transação atômica por padrão: no load, detectar sidecar ausente/desatualizado, não aplicar um quinto ID a outra peça, oferecer recuperação por cópia validada e registrar diagnóstico. Save copiado/renomeado e mod removido precisam comportamento definido. Não ocupar bytes aparentemente livres do save sem mapa completo de usos.
3. **Ciclo de vida do inventário:** ligar criação/drop/tesouro/loja, fusão/reforja de MOD-004, reorder/swap, equip/unequip, venda, destruição e substituição ao sidecar; limpar no free. Adicionar regra de drop para quinto slot só se for desejada. Verificar equivalência dos registros de 22 B antes/depois de cada operação e o movimento do ID extra.
4. **Customização e economia:** dar quinta linha, cursor, seleção de receita, custo, consumo e cancelamento atômicos. Adaptar `CalcKaizouCost` ou tratá-la como 4-slot e somar quinto explicitamente. Fixar duplicatas, habilidades exclusivas, Brotherhood/Celestial, preço de venda e compatibilidade com outras mudanças de MOD-004. O post não definiu custo nem se a quinta posição chega vazia ou já ocupada.
5. **Efeito em combate:** identificar todos os caminhos por ID/efeito além de `AggregateActorEquipAbilities`. Aplicar a quinta entrada de `a_ability.bin` com mesma ordem, saturação e regras de stacking das quatro nativas; reavaliar equip change, KO/revive, summon, início/fim de batalha e refresh de ator. Não basta somar status: comandos especiais que procuram ID diretamente também devem enxergar a quinta posição.
6. **Exibição:** comparar/equipar em FieldMenu, Customize, previews, lista do inventário, shops e nome da peça. Ajustar layout para a quinta linha em resoluções/idiomas usados. A moldura extra gerada por `slot_count=5` não é implementação aceitável.
7. **Compatibilidade e segurança:** configuração OFF por padrão; assinatura do PE e rollback em falha de hook; sem alteração do `FFX.exe` ou save vanilla no primeiro protótipo. Há concorrência entre menus/save/batalha; estado deve ter dono claro por thread e lifecycle. Se a associação sidecar/save falhar, desabilitar o efeito extra e preservar o save original.

### Gates objetivos

| Gate | Prova exigida | Estado em 23/09/2026 |
|---|---|---|
| RT0 formato | Fixture real e cálculo de limites; recusa de quinto `u16` no registro. | **Executado:** probe PASS, hash do fixture intacto. |
| RT0 cobertura de código | Matriz de todos os xrefs/paths que leem habilidades, preço e mutam equipamentos; teste de regressão contra snapshot do PE. | **Parcial:** funções críticas acima examinadas; auditoria completa de xrefs e layout do quinto ainda pendente. |
| RT1 sidecar | Harness isolado para criar, trocar, duplicar, vender, salvar, carregar, crash/recovery e saves copiados; sem jogo. | **Pendente.** |
| RT2 gameplay | Sessão autorizada separadamente: quinta habilidade visível e ativa para arma/armadura, menu/custo, queda, reorder, venda, save/reload, removendo mod e controle sem mod, com logs e restauração. | **Não executado / sem autorização de runtime nesta tarefa.** |
| Produção | Revisão independente, release gate e promoção explícita após RT2. | **Não iniciada.** |

**Casos sentinela para a futura validação:** (a) pôr `Auto-Haste` só na quinta posição de uma armadura e comparar estado/CTB com controle sem ela; (b) `Capture` só na quinta posição de uma arma e conferir contagem de captura; (c) `Break Damage Limit` só na quinta posição e medir clamp; (d) `No Encounters` só na quinta posição e medir encontros em campo; (e) uma habilidade de preço alto para comparar compra, venda e Customize; (f) duas peças de bytes idênticos com quintas habilidades diferentes, seguidas de reorder, venda, save, cópia do save e reload. Os casos (b)–(d) existem para revelar caminhos que verificam IDs diretamente, fora da agregação geral. Em todos, inspecionar que os 22 bytes de cada registro e a capacidade nativa 0–4 continuam íntegros, inclusive após remover/desativar o mod. Isso é plano de teste, não resultado observado.

## Busca externa e falsos positivos

Em 23/09/2026, buscas Web por “FFX fifth auto-ability slot”, “5 ability slots”, “five auto abilities” e GitHub/Nexus não trouxeram código ou release que demonstre quinta auto-habilidade **por peça**. Busca lexical local no corpus `external-compare/repos` (319 diretórios Git no inventário anterior) por “fifth/5th ability/equipment slot”, `ability5` e variantes não encontrou uma implementação FFX identificável; isto não prova inexistência fora do corpus ou de fontes não indexadas.

Um resultado de cheat PS3 chama “[5th Slot Weapon/Armour]” a **quinta peça da lista de equipamentos** e avança endereços de inventário de `0x16` em `0x16`; não é quinta auto-habilidade na mesma peça: [tópico original](https://nextgenupdate.com/forums/ps3-mods-answered-questions/797545-help-mod-save-ff-x-hd.html). Ele não deve ser usado como precedente técnico deste MOD-005.

**Conclusão:** o formato e os consumidores observados explicam por que FFX Editor/data patch isolado não implementam a proposta. A rota sidecar é plausível, mas a ativação completa de qualquer quinta auto-habilidade, em todo equipamento e após save/load, permanece hipótese de engenharia até RT1/RT2. O próximo trabalho produtivo é fechar as regras de gameplay e fazer um spike isolado de sidecar/ciclo de vida; não há motivo para inserir um hook no runtime nesta branch de pesquisa.

## Adendo de integração — Aeon Ascension (27/09/2026)

[Aeon Ascension](<../mod-ideas/AEON ASCENSION - MOD 002 004 005.md>) propõe dois atributos exclusivos de Aeons para o Workshop. Podem ocupar uma posição nativa ou o quinto lógico já desbloqueado, respeitando os quatro slots nativos preenchidos antes de nova colocação no quinto. Não criam sexto slot e não alteram o registro nativo de 22 B.

A receita dedicada é o preço final em qualquer posição. Não aplicar 1,5× genérico aos 99 Dark Matter nem acrescentar outra taxa genérica do quinto; desbloquear o quinto continua operação separada. Os novos IDs ainda não foram atribuídos; o catálogo atual do runtime suporta `0x8000..0x8082` e exige extensão registrada, não mero aumento do limite seguido de indexação fora da tabela. Nunca substituir `0x807B`, transferir o atributo a humanos ou autorizar efeito sem compra válida do Workshop.

Local do adendo: `/home/wanderson/.codex/worktrees/aeon-exclusive-breaks/ffx-hooks`, branch `codex/aeon-exclusive-breaks-plan-20260927`, base documental `39ceb198`. O runtime consultado em `/home/wanderson/Documents/ffx-hooks`, `main` `f2308ddd`, é mais recente do que o estágio histórico deste relatório. Estado dos novos Breaks: pesquisa/design; nenhum efeito/ID/RT2 aplicado nesta tarefa.
