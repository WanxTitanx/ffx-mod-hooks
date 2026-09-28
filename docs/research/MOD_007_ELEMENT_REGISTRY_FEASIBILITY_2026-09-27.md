# Jarvis-HOOK — MOD-007: viabilidade de afinidades graduais e mais de oito elementos

**27/09/2026 · pesquisa RT0 e harness RT1 delimitado.** A especificação de produto está em [Elemental Dominion](<../mod-ideas/MOD 007 - ELEMENTAL DOMINION.md>); a sequência de implementação está no [plano técnico](MOD_007_IMPLEMENTATION_PLAN_2026-09-27.md).

**Repo/branch:** `/home/wanderson/.codex/worktrees/mod-007-elemental-dominion/ffx-hooks`, `codex/mod-007-elemental-dominion-20260927`, base documental `f53aa3b0c7526a15b667fe317665194004ad6f07`. Runtime analisado separadamente em `/home/wanderson/Documents/ffx-hooks`, `main`, `f2308dddc1833811899c0999c96b5ee25a18bd66`. Não usar o runtime antigo desta branch documental como base de integração automática.

## Resultado e limites

| Pergunta | Resposta baseada na evidência | Próximo gate |
|---|---|---|
| FFX está limitado a sete elementos? | A UI consultada exibe até sete; o campo e a rotina de afinidade aceitam **oito bits**. | Mostrar os dois bits Custom juntos e validar todas as telas. |
| O oitavo precisa de novo formato? | Para matemática nativa de afinidade, não. Os bits `0x20/0x40/0x80` funcionaram no helper isolado. Nomes, apresentação, Nul e conteúdo têm contratos próprios. | Pack e consumidores completos; não generalizar o resultado do helper. |
| 9º/10º em um byte? | Não: os campos são u8; `0x100`/`0x200` não são transportados. A rotina original também ignora esses bits no argumento de 32 bits. | Registro externo e adaptação dos consumidores. |
| 9º/10º reais por Hook + Editor? | **Viável como arquitetura proposta**, usando identidades externas e substituição do resolver; não há prova de integração pronta. | Resolver contextual + estágio pré-hit/counters/Nul/AI/UI e teste RT1 completo. |
| −100% até 250% em passos de 25%? | As quatro máscaras nativas não codificam 15 patamares. Hook + dados externos representam os valores. | Integração de sinal, arredondamento, limites e efeitos. |
| Imperil para jogadores e monstros? | Modelo por instância de ator é plausível; não foi identificado campo nativo livre que entregue isso. | Eventos de ação/duração/remoção e simetria em harness. |
| 1/16 do HP máximo? | Fórmula 8/potência 1 já representa a base; fórmula 5/potência 1 usa HP atual. | Gates de imunidade, seleção por chefe, limite final e flags do comando. |
| MDF −255 diretamente? | Não no campo nativo: `movzx` lê um byte, sem sinal. | Atributo virtual separado e regra de fórmula aprovada/validada. |
| Magia com BDL até 999.999 e sem BDL em 9.999? | O seletor/clamp está localizado no PE; requer política de Hook, não apenas flag no Editor. Fury exige classificação explícita. | Compartilhar o ponto com Nova, preservar componentes/limite negativo e validar exibição/aplicação. |

## 1. Identidades e fontes consultadas

| Fonte | Identidade/localização | Uso e limite |
|---|---|---|
| FFX.exe instalado | `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/FFX.exe`; 10.675.712 B; SHA-256 `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced` | Leitura estática nova e extração temporária do helper para RT1; nenhum processo do jogo acessado. |
| Perfil PE | PE32/i386; ImageBase `0x400000`; timestamp `0x55D2F3CC` | **Flat IDA = RVA + 0x400000** neste binário. |
| Editor | `/home/wanderson/Documents/ffx-editor-main`; `codexclaudiocodeffxeditor`; `399164236638bd34d44c4833b02ea3b15a49d771`, checkout com mudanças locais preservadas | Enums, serializers, fixture de comandos e RE anterior. Nenhuma edição no Editor nesta tarefa. |
| IDA anterior | Exportações em `work/_btl/` e docs de RE do Editor | Navegação e contexto, não nova sessão IDA. Protótipos descompilados contêm tipos ruins; bytes atuais prevalecem. A base `C:\IDA_DB\ffxoficial.exe.i64` na `windows11-dev-next` não foi aberta/alterada nesta pesquisa. |
| Fahrenheit | `/home/wanderson/Documents/external-compare/fahrenheit`; `main`; `c149c847b3a24a66114956f87f1b008599736f75` | `src/core/ffx/element.cs`, `command.cs`, `aability.cs`, `battle/chr.cs`, `battle/mon_stats.cs`, `call_4.g.cs`. LGPL-3.0-or-later; leitura, sem adaptação de código. |
| Fantasia | `/home/wanderson/Documents/external-compare/repos/EvelynTSMG_ffx-mods-fantasia`; `main`; `64b03ae915c32f47d4a84aefffcd3130cb13ddce` | `src/balance/elemental_affinities.cs`, MIT; precedente de detour e políticas. Nenhum source copiado. |
| Wiki local | `/home/wanderson/Documents/ffx-editor-main/work/wiki_ffx_full/`; índice `docs/reverse/_archive/playbook/FFX_WIKI_INDEX_2026-08-20.md` | Contexto de design, fonte secundária. Índice anuncia 65.366 páginas; não foi auditado integralmente. |
| Imagem do usuário | `/tmp/codex-remote-attachments/01a0ced4-9fba-7a22-9466-d8897fbcba59/DC3EFA0E-267D-440F-81CA-63D9FF0597D3/1-Foto-1.jpg` | Requisitos Kari/Nuckyduck/Gabryc. Data do post não visível; horários não provam cronologia de implementação. |

Inventário de arquivos e hashes: [source_inventory.json](../../research/mod_007_elements/source_inventory.json). Links públicos primários conferidos: [Fantasia — implementação de afinidades](https://github.com/EvelynTSMG/ffx-mods-fantasia/blob/64b03ae915c32f47d4a84aefffcd3130cb13ddce/src/balance/elemental_affinities.cs), [Fahrenheit](https://github.com/fahrenheit-crew/fahrenheit). Não inferir versão instalada pela data do source ou pelo relato de sucesso.

## 2. Prova nova: a rotina nativa trabalha com oito bits

Alvo: `ApplyElementResist`, flat **`0x78A420`**, RVA **`0x38A420`**, tamanho **1045 B**, SHA-256 do corpo **`3ec1a5447679ce745684028ac1e25f4708e33f8d4a7b79b8667c6ae211ede36f`**.

Leituras u8 do alvo:

| Offset do ator | Semântica | Leitura no corpo |
|---|---|---|
| `+0x5DA` | Absorb | Flat `0x78A452` |
| `+0x5DB` | Ignore/Null | `0x78A448` |
| `+0x5DC` | Resist/Half | `0x78A43A` |
| `+0x5DD` | Weak | `0x78A441` |

O corpo lê os argumentos da stack: alvo `ebp+8`, máscara `ebp+0x10`, dano `ebp+0x14`, e retorna sem limpeza de argumentos. O segundo argumento não é lido nesse corpo. Harness usou **cdecl de quatro argumentos**, coerente com o precedente Fantasia e anotação Fahrenheit. Não reutilizar o protótipo antigo de três argumentos fastcall mostrado por algumas exportações IDA.

As oito condições são desenroladas. `0x20` aparece em `0x78A503/0x78A509`; `0x40` em `0x78A51D/0x78A528`; `0x80` em `0x78A53D`, seguido de teste do sinal do byte de fraqueza. A mesma cobertura existe nos demais caminhos.

Semântica observada no helper:

1. Se houver fraqueza em algum elemento do golpe, multiplicar sequencialmente por 3/2 para **cada** fraqueza e retornar. Divisão inteira com truncamento em direção a zero.
2. Sem fraqueza, se houver qualquer elemento nativo sem resist/null/absorb, retornar dano original.
3. Se todos forem cobertos: algum resist sem null/absorb no mesmo bit → metade; senão algum null sem absorb → zero; senão absorb → sinal invertido.
4. `0x100` e `0x200` não entram nessas condições. Passar um inteiro maior ao helper não cria novas afinidades.

**RT1 executado:** [probe_native.py](../../research/mod_007_elements/probe_native.py) verifica SHA do EXE/corpo, disassembly integral de 390 instruções, ausência de calls e branches externos ou endereçamento de memória absoluto. Extrai apenas esse corpo para diretório temporário, linka um harness ELF i386 sem libc e executa em processo próprio. O código é relocável nesse recorte; não carrega o PE inteiro, não injeta DLL e não abre o jogo. Bytes extraídos e binário do harness são temporários e não entram no Git.

| Conjunto | Comparações |
|---|---:|
| Oito bits × 16 combinações sobrepostas de flags × sete danos positivos/negativos/zero | 896 |
| Todas as 5^8 atribuições canônicas de afinidade com máscara `0xFF` | 390.625 |
| Todas as 256 máscaras × 16 padrões de flags × três variações, incluindo bits 9/10 | 12.288 |
| **Total** | **403.809, zero divergências** |

Cada comparação também confirma que o buffer do ator não foi modificado. Controle de 1.000 de dano: para **cada** bit `01,02,04,08,10,20,40,80`, resultados 1.500 / 500 / 0 / −1.000 nos quatro casos simples. [Resultado integral](../../research/mod_007_elements/native_validation.json).

**Limite:** a referência matemática e o helper coincidiram nesse domínio. Isso não testa entrada real de comandos, aplicação de HP, imunidade Gravity, Nul, Reflect, AI, save, animações ou UI. Não cobre overflow arbitrário de i32. As primeiras execuções do harness saíram cedo por uma declaração incorreta do registrador de retorno de `int 0x80`; foi corrigida no harness. Não houve falha do jogo nem alteração do helper.

## 3. Onde o nono elemento se perde

| Consumidor | Largura e evidência | Consequência |
|---|---|---|
| `command.bin` | `0x60` B/registro, `ElementFlgs` u8 em `+0x2D` | Gravar u16 no lugar invade o campo seguinte; o byte não representa `0x100`. |
| `monmagic*.bin` | Writer do Editor usa registro `0x5C`; mesmo modelo básico de comando | Não presumir que todos os bancos tenham stride de command.bin. |
| `a_ability.bin` | Registro `0x6C`; strike/absorb/ignore/resist/weak em bytes `+0x11..+0x15` | Não aumentar a struct globalmente para u16; deslocaria status/textos/consumidores. |
| Monstro | `Monster_StatSheet.ElementalWeakness` e `MonStats` com máscaras byte | Matriz de 15 patamares/elementos externos fica fora do formato original. |
| Ator | Weapon element `+0x5D9`, affinities `+0x5DA..+0x5DD` | Arma e resistência já se encontram em campos distintos de um byte. |
| Composição do golpe | Flat `0x78E7B6`: lê arma u8; `0x78E7CA`: lê comando u8; OR em `0x78E7CE`. Rota sem arma lê u8 em `0x78E7FB`. | Ampliar só o argumento do resolver não recupera informação que nunca saiu do arquivo. |
| Scan/Sensor | `ElementScanCore.h`: quatro básicos + array de três extras, `{0x10,0x80,extraBit}`; `extraBit` somente `0x20/0x40`; rejeita máscara >255 | A UI atual não é um renderer genérico de dez elementos. |
| Difficulty | `F7DifficultyCore.cpp`: máximo `0xFF`; offsets de afinidade escritos com largura 1 | Deve continuar aceitando o contrato antigo; novos valores exigem outro modelo/config versionada. |
| Fahrenheit | `ElementFlags : byte`, enum público com cinco nomes | Não confundir a enumeração de nomes com toda a capacidade do executável. |
| Fantasia | `BALANCED` conta cinco nomes explicitamente e percorre `Enum.GetValues` | Não reutilizar sem auditoria para extras: máscaras só com bits extras podem produzir contagem zero; modos enumerados não cobrem um registro dinâmico. |

`Element_Enum : byte` do Editor, com Physical=0, Fire=1 etc., é ainda outro domínio ordinal. Não usar esses números como bitmask ou como IDs persistentes do registro proposto.

## 4. Demi, Gravity e MDF: correções de interpretação

Fixture conferido: `/home/wanderson/Documents/ffx-editor-main/FFXProjectEditor.Tests/Fixtures/Battle/command.bin`, 44.558 B, SHA-256 `db4c87f33f27a7df41bc8a520bc2246b664167319386fe99da9880ae06000429`, header 20 B, 320 registros de 96 B.

| Linha | Identificação no dicionário | Fórmula `+0x28` | Potência `+0x2A` | Elemento `+0x2D` |
|---|---|---:|---:|---:|
| 78 | Demi | 5 (HP atual) | 4 | 0 |
| 133 | Demi Fury | 5 (HP atual) | 2 | 0 |

Leitura nova do PE, guiada pelo export IDA `work/_btl/damage_formula_dispatch.c`: os trechos flat `0x789F63..0x789F73` e `0x789FB4..0x789FC5` multiplicam potência e dividem por 16. `DamageFormula_Enum.cs` associa 5 a TargetHp e 8 a TargetMaxHp. Assim, **8/potência 1 é suficiente para a base 1/16 maxHP em dados**, sem converter o denominador global para 60. A escolha condicional de chefe/usuário/comando e a regra não letal são trabalho adicional.

O gate nativo flat **`0x78AE40`** / RVA **`0x38AE40`** testa fórmula 5 ou 8 e flag **`target+0x5B8 & 2`**; se ativa, devolve zero e altera flags/contador do resultado. Portanto, manter a flag de imunidade pode zerar a nova magia. Não remover globalmente essa flag só para liberar alguns chefes. Gravity não aparece como bit elemental na Demi desse fixture.

MDF é lida como u8 por `movzx edi, byte ptr [ecx+0x5AB]` em **`0x789CCE`**. DEF também é unsigned em `+0x5A9`. A proposta de −255 pertence a outro modelo de atributo. Guardar −1 em byte pode virar 255; guardar −255 pode virar 1. Um atributo virtual assinado e uma política matemática própria são obrigatórios.

## 5. Arquiteturas comparadas

| Caminho | Vantagem | Custo/limite | Decisão |
|---|---|---|---|
| Usar os oito bits existentes | Reaproveita arquivos e matemática comprovados | Continua discreto; UI atual não mostra ambos Custom; efeitos auxiliares são separados | Primeira etapa Eight |
| Ampliar todas as structs para u16/u32 | Representação aparentemente uniforme | Reescrever layouts, loaders, cópias, máscaras, saves, ATEL e consumidores; offsets deixam de valer | Não recomendado para esta implementação |
| Registro externo + adaptadores | Preserva layout vanilla; números e elementos independentes; packs composáveis | Mais contratos de identidade/lifecycle/UI e cobertura de callers | Recomendado para Core/Tenfold |
| Multiplexar um bit como marcador | Pouca alteração no arquivo | Elementos distintos colidem em arma/resistência/counters/UI; mistura perde identidade | Não satisfaz 9º/10º independentes |

Registro externo é **metadado** e precisa de execução: `stable_key → descriptor`, regras de comandos/monstros/equipamentos, perfis de alvo e estado transitório. O JSON sozinho não adiciona efeito. `uint32` interno ou lista de descritores é permitido **dentro do Hook**; não muda o tipo serialized u8. A lista de chaves persistidas permite reordenar UI sem trocar o significado de um elemento.

## 6. Integrações que impedem prometer “um hook resolve tudo”

| Superfície | Evidência/ponto candidato | Trabalho obrigatório |
|---|---|---|
| Contexto de comando/atacante | `ComputeHitDamage`, RVA `0x38E680`; já interceptado por `EquipmentWorkshopRuntime.cpp` | Compartilhar o ponto/dispatcher com a lane atual; não instalar detour concorrente. Stack contextual por thread/ação, incluindo golpes encadeados. |
| Afinidade | RVA `0x38A420`; cdecl 4 args provado neste helper | Resolver novo recebe conjunto externo completo e número assinado antes dos caps; fallback original quando OFF/sem contrato. |
| Antes do dano | RE de `ComputeHitDamage` mostra resolução de counters/elementos antes da afinidade | Só substituir o resultado final deixaria counters com visão errada. Mapear/implementar por consumidor antes de declarar suporte completo. |
| Nul vanilla | Candidato RVA `0x38C070`; quatro estados originais | Provar ABI/evento de consumo, sobretudo golpe misto e múltiplos hits. Não foi executado nesta pesquisa. |
| Nul Holy/Dark local | `NulWardHook.cpp`, `NulWard_ApplyNullBlocks`; mapas por ator, command TLS, etapa de writeback | Arbitrar autoridade única e integração de gerações. Não criar duas cobranças de ward para o mesmo golpe. |
| Auto-habilidades/Workshop | RVA `0x39C610` agregado, quinto lógico e efeitos já na lane main | Reaproveitar identidade de peça e agregação existente; somar só deltas externos, sem duplicar efeito já embutido no byte. |
| Difficulty | Transações nos bytes de afinidade | Rebasear o baseline após configuração válida entre ações; snapshot imutável para a ação corrente. |
| UI | `ElementHook.cpp`, `ElementScanDetails.inl`, `ElementScanCore.h`, `NativeSettingsUi.inl` | Trocar quantidade fixa por descritores, valor numérico, layout e navegação; stats/MP continuam independentes. |
| AI/Scan/auto-seleção | Operações nativas consultam masks/estados próprios | Catalogue callers e opcodes usados no pack. Regras numéricas novas não tornam AI antiga consciente delas. |
| Save/field/lifecycle | Mudanças de instância, batalha, invocação e reload | Perfis estáticos no pack; efeitos temporários fora do save; nada de usar apenas ponteiro como identidade. |

Os pontos fora do helper executado são evidência de source/RE e candidatos, não ABIs completos revalidados por este harness. Conferir prólogo, argumentos, callers, ownership e teardown antes de instalar qualquer adapter.

## 7. Contexto de outros FF

Foram lidas as páginas locais `Imperil (status).wiki.txt`, `Oil (status).wiki.txt`, `Gravity (ability).wiki.txt` e `Element (term).wiki.txt`. As duas primeiras descrevem, respectivamente, a ideia de descer uma classe de defesa elemental em FFXIII e de vulnerabilidade específica a Fire em FFXII. Servem de inspiração para estado legível, remoção e contrajogo. Os números, duração e curas do MOD-007 são propostas próprias; não importar automaticamente x3 de Oil nem a taxonomia de outro jogo.

A página Gravity diferencia HP atual da Demi e da Fury em FFX, corroborando os dados do fixture. A prova técnica principal continua sendo o arquivo/PE. Não usar wiki para afirmar ABI, offsets ou campos livres. Conteúdo de wiki e screenshot não foi copiado como asset do mod.

## 8. Verificação e reprodução

Na branch deste documento:

```bash
python3 research/mod_007_elements/probe_native.py '/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/FFX.exe' --output /tmp/mod007-native-validation.json
python3 research/mod_007_elements/validate_design.py
python3 research/mod_ideas_precode/generate_ledger.py --check
```

Primeiro comando requer Linux x86 com suporte a ELF i386, GCC, `pefile` e `capstone`; não baixa nem distribui FFX.exe. Recusa EXE/corpo desconhecido antes de executar. Segundo passou **124 verificações** somente do contrato **proposto**: chaves/duplicatas, limites, 8/9/10/16/32 descritores, 15 patamares, cálculos mistos, sinal, overflow intermediário e Gravity não letal. Ele não implementa status ou Hook. Resultado salvo em `research/mod_007_elements/design_validation.json`. O ledger passou **94/94**; conferência de integridade preservou **39 arquivos-fonte/fixtures** do inventário e resolveu **36 links locais** nos documentos principais. Casos negativos do probe recusaram EXE desconhecido e destino igual ao EXE de entrada, preservando o arquivo.

**Claim → evidence → confidence → conflict → next:**

- Oito bits funcionam no helper → PE fixado + 403.809 comparações → alta nesse recorte → UI só expõe sete e SDK enumera cinco → ampliar/validar consumidores.
- Nono/décimo exigem nova via de dados → leituras u8 + bits superiores sem efeito no harness → alta → argumento DWORD do helper pode sugerir capacidade inexistente → sidecar/contexto completo.
- 1/16 maxHP cabe em dados → fixture + enum + divisão por 16 → alta para a base → imunidade Gravity e caps ainda interferem → comando/target gate explícito.
- MOD-007 completo é implementável em fases → arquitetura/contrato e precedentes → média; inferência de engenharia → falta adapter end-to-end e RT2 → executar o plano, mantendo gates por fase.

Nada nesta pesquisa instala DLL, muda assets/save, marca produção ou prova a nova mecânica em jogo. O relato de sucesso anterior do usuário fica associado ao mod de elementos já existente.

## 9. Magic Break Damage Limit: 999.999

Adendo solicitado pelo usuário: **magias elegíveis com BDL podem causar até 999.999 por hit; sem BDL continuam limitadas a 9.999**. Não é aumento do dano base nem remoção total do limite. Módulo independente no mesmo pacote MOD-007, descrito na seção 5.1 do design.

### Bytes e source conferidos agora

Todos os endereços abaixo são para o mesmo EXE SHA `78ce3439…`; flat = RVA + `0x400000`.

| RVA / flat | Evidência atual | Papel |
|---|---|---|
| `0x38ED1A` / `0x78ED1A` | Seleção de BDL lê WORD do atacante `+0x6BE`, máscara `0x0800`; monta 9999 ou 99999 em EBX | BDL de equipamento agregado |
| `0x38ED29` / `0x78ED29` | Lê WORD do comando `+0x20`, usa AL; bit `0x80` força 99999, senão `0x40` força 9999 | BDL/supressão do comando |
| `0x38ED41` / `0x78ED41` | `mov ebx,0x1869F`, bytes `BB 9F 86 01 00` | Teto 99999; alterar só aqui não cobre todas as rotas nem classifica magia |
| `0x38EDCB` / `0x78EDCB` | Clamp inferior EDI, com salto direto a writeback | Preservar absorção/cura e destino do branch |
| `0x38EDD3` / `0x78EDD3` | `cmp eax,ebx; jle; mov eax,ebx`; seis bytes antes de writeback | Ponto candidato para teto superior contextual por componente |
| `0x38EDD9` / `0x78EDD9` | `mov [esi],eax`, `add [ecx+0x650],eax`, `sub [ecx],eax` | Valor usado pelo resultado e aplicação, não só texto |
| `0x38EDC1` / `0x78EDC1` | Contador de componentes começa em 3 e decresce no loop | HP é a primeira passagem; MP/CTB não podem herdar o teto novo |

O bloco de 16 bytes em `0x38EDCB` coincide com `kClampGraph` de `src/runtime/FfxHooksDll/hooks/NovaSuperDamageHook.cpp`, source atual consultado. Esse módulo possui um patch de seis bytes em `0x38EDD3`, preserva o writeback em `0x38EDD9` e já filtra Kimahri/Nova/primeiro componente. Deve servir de integração/ownership, **não de bypass incondicional para copiar**: o MOD-007 exige teto finito de 999.999 e BDL efetivo. `shared/ffx_addresses.h` nomeia os mesmos RVAs.

Documentos anteriores consultados no Editor: `docs/reverse/_archive/superseded/FFX_DAMAGE_CAP_CLAMP_IDA_2026-06-15.md` e `FFX_DAMAGE_CAP_BEYOND_99999_RESEARCH_BRIEF_2026-06-15.md`. Eles contêm tabelas antigas com VA `0xB8...`, confundindo soma de base, e descrevem erroneamente um trecho como três alvos. A referência desta entrega é a leitura PE atual: **RVA `0x38...`, flat `0x78...`, loop de componentes**. Não executar instruções antigas de rename/deploy presentes nesses arquivos.

### Classificação: por que não basta `DamageFlgs & 2`

No fixture `command.bin` já fixado, Fire 66/Holy 63/Ultima 83/Demi 78 têm `+0x20=0x02`. Fire Fury 121, Demi Fury 133 e Ultima Fury 138 têm **`+0x20=0x00`**; são variantes de magia apesar de não terem esse bit. Nova 115 tem `0x04`, fórmula 15; Attack0 tem `0x0D`, fórmula1. Logo, nem `MAG usado na fórmula` nem `bit de elemento` nem `flags & 2` definem sozinho o conjunto solicitado.

Usar binding versionado `eligible_damage_spell` com importação de categorias/flags e exceções explícitas por banco/linha/hash, abrangendo magias ofensivas de jogador/inimigo/Aeon e variantes Fury. Excluir cura intencional/MP/CTB; classificar itens/Mix/Blue Magic/Overdrives separadamente. Elemento de arma não transforma Attack em magia. A autoability BDL já existe, ID `0x8019` (25); não criar nova linha de autoability para esta capacidade.

### Política proposta e validação delimitada

```text
native_cap = command.force_bdl ? 99999
           : command.suppress_bdl ? 9999
           : attacker.has_bdl ? 99999 : 9999
upper_cap  = enabled && eligible_damage_spell && HP_component && native_cap == 99999
           ? 999999 : native_cap
lower_floor = -native_cap  // preservar cura/absorção na primeira versão
```

Aplicar sobre o valor pré-clamp. Um handler após o dano original voltar com 99.999 já perdeu informação e não implementa o pedido. Não alterar EBX globalmente para todas as passagens: piso negativo e componentes seguintes dependem dos valores nativos. Definir um único dono do patch com política por componente, preservação de registradores/flags, entrada do ramo inferior, writeback e neutralização segura. NulWard também interage com writeback; testar composição com Nova/Nul/Workshop. Nova sem teto em conflito deve ser recusada ou resolvida por política compartilhada explícita; ordem de carregamento e segundo patch não são soluções.

`research/mod_007_elements/probe_spell_cap.py` lê/valida EXE, bytes do clamp e linhas do fixture; testa o **modelo proposto**, não um hook instalado. Inclui cap OFF/ON, BDL de arma/inato/supressão/ambas flags, magia/físico/Fury, HP/MP/CTB, dano negativo, bordas 9999/10000/99999/100000/999999/1000000 e i32 máximo. Resultado: **3.175 verificações da política, zero falhas**. Relatório: `spell_cap_validation.json`. A exibição de seis dígitos e a integração runtime permanecem pendentes.

```bash
python3 research/mod_007_elements/probe_spell_cap.py '/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/FFX.exe' '/home/wanderson/Documents/ffx-editor-main/FFXProjectEditor.Tests/Fixtures/Battle/command.bin' --output /tmp/mod007-spell-cap.json
```
