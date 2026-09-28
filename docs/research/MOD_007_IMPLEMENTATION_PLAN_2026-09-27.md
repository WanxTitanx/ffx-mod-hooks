# Jarvis-HOOK — plano de implementação do Elemental Dominion

**Plano, não implementação de runtime.** Referência de produto: [MOD-007](<../mod-ideas/MOD 007 - ELEMENTAL DOMINION.md>); provas/limites: [pesquisa](MOD_007_ELEMENT_REGISTRY_FEASIBILITY_2026-09-27.md).

**Local/branch documental:** `/home/wanderson/.codex/worktrees/mod-007-elemental-dominion/ffx-hooks`, `codex/mod-007-elemental-dominion-20260927`, base `f53aa3b0c7526a15b667fe317665194004ad6f07`. **Base de source consultada:** `/home/wanderson/Documents/ffx-hooks`, `main` em `f2308dddc1833811899c0999c96b5ee25a18bd66`. **Lane Editor:** `/home/wanderson/Documents/ffx-editor-main`, `codexclaudiocodeffxeditor`, snapshot `399164236638bd34d44c4833b02ea3b15a49d771`. Revalidar HEADs/dirty state antes de codar; esta branch herdou source anterior e deve fornecer documentação/probes, sem rebaixar o runtime integrado.

## Arquitetura escolhida para o protótipo

Usar os oito bits nativos onde já representam os dados; armazenar novos elementos, afinidades numéricas e efeitos transitórios em modelos próprios. Quatro caminhos foram comparados na pesquisa. Ampliar o layout nativo inteiro ou multiplexar um bit não são a rota deste plano.

| Componente proposto | Responsabilidade / contrato |
|---|---|
| `ElementRegistry` | Chaves ASCII namespaced estáveis, descritores de UI, mapeamento opcional para um bit nativo; capacidade inicial 32, sem IDs persistentes derivados da ordem. |
| `ElementPack` | Config versionada, dependências/capacidades, hashes de tabelas/linhas por idioma, bindings de comando, monstro e autoability; carregada antes da batalha. |
| `ActorAffinityState` | Baseline da instância, deltas de equipamento e efeitos transitórios; chave `battle_generation + actor_slot + incarnation`, não ponteiro isolado. |
| `ActionElementContext` | Comando/usuário/alvo efetivo, referências de dados, elementos/pesos, política, geração e sequência; stack por thread para Reflect/counters/ações aninhadas. |
| `AffinityResolver` | Cálculo puro com i64, resultado assinado, política mista única e metadados para preview/diagnóstico; nenhuma escrita em HP. |
| `SpellDamageCapPolicy` | Classifica magia ofensiva por binding, preserva BDL efetivo nativo, escolhe 999.999 somente para HP elegível; compartilha ownership do clamp com Nova. |
| `BattleAdapter` | Admite callers/assinaturas, injeta contexto e chama resolver no estágio correto; preserva ABI/retorno, integra counters/Nul e o Hook existente. |
| `ElementStatusController` | Imperil/Ward e contagem por ação; remoção, KO, troca, summon e teardown. |
| `ElementPresentation` | Descritores/valores/timers no Scan/Sensor, stats/MP independentes; snapshot sem mutação. |
| `OnlyModAuthoring` no Editor | Criação/preview/exportação de pacotes sem mudar serializers vanilla. |

Esses nomes/interfaces são **novos alvos de implementação**, não APIs já disponíveis. Começar pelo núcleo puro em `research/mod_007_elements/` ou biblioteca própria sem dependências Win32; só integrar em `src/runtime/FfxHooksDll/hooks/` após os gates correspondentes.

## Contrato de dados a congelar antes do adapter

O [manifesto de exemplo](../../research/mod_007_elements/example_manifest.json) é `draft-1`, deliberadamente marcado como não carregável pelo runtime atual. O schema de produção deve incluir:

- `schema_version`, `package_id`, versão do pack, versões/capacidades de Hook e Editor, perfil de EXE e hashes de entradas.
- Elementos: `key`, `label_key`, ícone/tint, `native_bit` opcional; bits nativos únicos em `1..128`, potências de dois. Nomes Earth/Wind são metadados de pack. Duas chaves diferentes não podem ocupar o mesmo bit no mesmo pack resolvido.
- Binding de comando: chave do comando, banco (`command`, `item`, `monmagic1/2/...`), índice/ID codificado, fingerprint da linha do idioma/versão selecionados, elementos e pesos, regra de fórmula/efeito opcional. ID sozinho ou ponteiro reciclado não identifica versão do conteúdo.
- Política de elementos do comando: padrão substitui a lista de elementos **do comando**; unir elementos da arma somente se a semântica nativa `uses_weapon_properties` estiver ativa. `augment` deve ser escolha explícita para evitar duplicar Fire existente. Deduplicar chaves; pesos inteiros positivos até 1000, no máximo 32 partes.
- Perfis: baseline por monstro/template, regras por personagem/Aeon quando necessário, equipamento/estado SOS, bloqueios de afinidade, limites de Imperil. Dois monstros do mesmo template em campo têm efeitos transitórios independentes.
- Afinidade: base/deltas em pontos-base, authoring de 25 pp, intervalo −10000..25000. Baseline nativo já inclui autoabilities nativas agregadas; adicionar somente os deltas externos. Substituir um efeito nativo exige declarar a substituição, não manter os dois cálculos.
- Gravidade: base atual/máxima, fração, seletor explícito de alvo, imunidade, não letal, cap e opção separada de passar por afinidade elemental. Elemento Gravity opcional não transforma todo dano percentual em dano desse elemento.
- Magic BDL: módulo independente, OFF por padrão, capacidade `mod007.spell-cap.v1`; 9.999 sem BDL efetivo, 999.999 para magia ofensiva com BDL, teto nativo para outras categorias. Bindings de magia incluem Fury explicitamente; não usar só o bit mágico. Teto por hit/alvo, piso negativo preservado.
- Não persistir timers/ponteiros/índices transitórios de registro no save. Persistir configuração por chave; ranks/equipamento continuam no contrato já existente do Workshop quando necessário.

**Invariantes de erro:** chave/ID duplicado, referência inexistente, fingerprint divergente, versão desconhecida ou capacidade ausente impedem admissão do pack. Não ignorar o erro de um elemento e converter o golpe silenciosamente em não elemental. Nenhuma escrita de um nono bit nos arquivos/atores nativos.

**Hook ausente é um caso real de distribuição:** um comando gravado permanentemente com elemento nativo zero pode virar golpe não elemental se a DLL não carregar. O pacote de produção precisa de loader/admissão de overlays condicionado às capacidades, ou fallback vanilla explícito e documentado por comando. O protótipo inicial deve usar bindings externos sobre dados originais preservados; não distribuir novas linhas hook-only como se o simples manifesto garantisse segurança com a DLL ausente. Esse gate precisa ser resolvido com a lane responsável pela instalação antes de release.

## T0 — Inventário atual, ownership e entrada de dados

- [ ] Registrar HEADs/dirty state, SHA do EXE e módulos/flags do mod de elementos relatado como funcionando; preservá-lo como controle. Não atribuir automaticamente o relato ao DLL gerado mais recentemente.
- [ ] Reexecutar o probe nativo quando o PE mudar; recusar perfil diferente. Identificar todos os consumidores de máscara usados no pack, com endereço/ABI/largura/caller.
- [ ] Conferir ownership do RVA `0x38E680` em `EquipmentWorkshopRuntime.cpp`, `0x39C610` na agregação e NulWard no writeback. Definir callback/dispatcher compartilhado antes de instalar detour adicional. Compatibilidade com Fantasia no mesmo processo é conflito a detectar, não composição garantida.
- [ ] Fechar a política de assets com Hook ausente e o vínculo do pacote a dados de outros mods/idiomas. Nenhuma instalação de pacote neste passo.

**Aceitação:** tabela de consumidores sem um “etc.” que esconda paths necessários; source/bytes de cada adapter e contrato de erro reviewáveis. O gap principal é contextualização antes dos prechecks/counters, além do resolver já executado.

## T1 — Eight: expor o oitavo elemento já existente

Superfícies atuais: `ElementScanCore.h`, `ElementScanSettings.h`, `ElementScanDetails.inl`, `ElementHook.cpp`, `NativeSettingsUi.inl`, `tests/ElementVisibilityCases.inl`, `tests/ElementScanCoreRt0.cpp`, `tests/NativePresentationRt1.cpp`.

- [ ] Migrar preferências legadas Holy/Dark/Custom preservando cor/seleção antiga. O outro Custom começa invisível até habilitação explícita.
- [ ] Permitir `0x20` e `0x40` ao mesmo tempo, com labels configuráveis; não transformar nomes de uma pack Earth/Wind em identidade global de todas as packs.
- [ ] Renderizar quatro extras com layout legível. Confirmar janelas, Sensor inline, Scan completo, texto de status e controles independentes de stats/MP.
- [ ] Manter contratos `F7DifficultyCore` u8; teste de todas as 256 máscaras deve continuar passando, inclusive restore/OFF.

**Teste necessário:** comparação matemática existente dos oito bits + harness das quatro colunas extras e suas combinações; cenas sem Scan, thread errada e configuração inválida preservam a rota original. Marco não depende de criar elemento externo nem de afinidades graduais.

## T2 — Registro, schema e resolver puro

Criar módulos pequenos `ElementRegistry`, `ElementPack`, `AffinityResolver` com tipos de dados puros; paths finais definidos pela organização atual do runtime, sem refatorar sistemas alheios.

- [ ] Parser com capacidade e limites explícitos; binding por chave, fingerprints, diagnósticos e nenhuma mutação de game data.
- [ ] Resolver import de máscaras com precedência nativa. Implementar `native_exact`, `highest_exposure`, `split_weighted`, `lowest_exposure`, recusando combinações irrepresentáveis de modo/capacidade.
- [ ] Usar i64 para multiplicações/ponderações, divisão truncada uma vez, saturação i32 explícita; aplicar teto de dano no estágio nativo correto, sem criar teto duplicado.
- [ ] Comparar modelo de compatibilidade com o corpo original via probe; preservar casos negativos, zero, múltiplas fraquezas e flags sobrepostas.
- [ ] Matriz de registro 8/9/10/16/32, limite 33 recusado, remapeamento/reordenação, chaves ausentes, colisão de bit, referências entre bancos e pesos inválidos.

**Aceitação:** resultados do design são reproduzíveis e a implementação nova tem testes próprios. `validate_design.py` desta pesquisa serve como exemplos de aceitação, não substitui teste do parser/serviço que vier a existir.

## T3 — Adapter de combate, com provas antes da integração

Alvos candidatos: `ComputeHitDamage` RVA `0x38E680`, `ApplyElementResist` RVA `0x38A420`. O segundo é cdecl quatro argumentos provado; o primeiro deve usar o contrato revalidado da lane atual.

- [ ] Adicionar contexto usando o proprietário existente do fluxo; pilha RAII/TLS, sem campo global reutilizado por todos os alvos. Pop garantido em retorno antecipado/exceção/teardown.
- [ ] Resolver comando + arma + alvo efetivo sem truncar chaves para byte. A rotina de afinidade recebe contexto validado; sem contexto deve seguir a política segura, com diagnóstico.
- [ ] Cobrir player/enemy/Aeon, `item.bin`, monmagic, attack/weapon, Fury/Overdrive, multi-hit, multi-target, Reflect, Copycat/counter e preview. Paths não cobertos ficam indisponíveis no schema/capabilities.
- [ ] Mapear prechecks/counters que precedem a afinidade e os que usam máscara depois; adicionar adaptadores ao mesmo conjunto sem marcadores em bits emprestados. Um golpe externo deve ser classificável em todos os consumidores admitidos.
- [ ] Garantir aplicação única: não multiplicar primeiro no helper original e depois novamente no mod. Capturar dano antes/depois sem escrevê-lo em HP.
- [ ] Master OFF, assinatura errada, pack ausente, erro de configuração, fim de batalha e uninstall seguem caminhos demonstrados. Sem I/O/alocação descontrolada por hit; configuração imutável durante a ação.

**RT1 aceito:** nove/dez elementos com resultados distintos em duas instâncias de monstro, mapa misto nativo+externo, nenhuma mudança nos bytes vizinhos dos atores/comandos, nested actions e ABI/canários. Nenhum “support=true” para um consumidor ainda não integrado.

## T4 — Imperil, Ward e duração

- [ ] Implementar `ActorAffinityState` por geração/instância; baseline não é sobrescrito para simular um status.
- [ ] Eventos de ação aplicam no máximo uma vez por alvo e decrementam timers no evento efetivamente concluído; a ação aplicadora não se beneficia retroativamente. Testar ação cancelada, miss, turn skip, Guard e ação forçada.
- [ ] Aplicar mesmas regras para lados opostos; configurar limites/imunidade de chefes por perfil explícito. Mostrar imunidade/lock no Scan.
- [ ] Remoção por comandos próprios; integração opcional Esuna/Dispel/Ribbon. KO, ejection, summon, dismiss, retorno à retaguarda e batalha consecutiva não deixam status em nova instância.
- [ ] Comparar absorb −100→−75 e immune 0→25; Ward não cria absorb por si no preset. Reaplicação capada renova duração; overflow de contador é rejeitado.

**Aceitação:** testes de sequência de eventos, não apenas função `value+25`; dois alvos idênticos não compartilham stack. Mudança de Difficulty/equipamento recalcula baseline entre ações e mantém transientes válidos na mesma instância.

## T5 — Gravity por regra e fronteira de imunidade

- [ ] Primeiro demonstrar comando estático fórmula 8/potência 1 em authoring separado; comparar com 5/4 e 5/2 do fixture, preservando demais bytes e idioma.
- [ ] Regra por chefe/banco/linha usa identificação validada. Preservar gate de `target+0x5B8 & 2` por padrão; overrides só para perfis selecionados.
- [ ] Nonlethal aplica ao resultado final após amplificadores sem descontar HP ou consumir RNG adicional. Provar ponto final em RE/harness antes de habilitar esse campo.
- [ ] Testar HP atual 0/1/3, máximo não divisível por 16, target já danificado, cap vanilla/BDL, multi-hit, imunidade, Reflect, absorb e exclusão MP/CTB.

**Aceitação:** no exemplo maxHP 160.000/currentHP 5.000, resultado não letal não passa de 4.999; contra outro alvo continua a regra desse perfil. Sem mudança global dos denominadores das outras fórmulas.

## T5b — Magic Break Damage Limit até 999.999

Requisito novo do usuário, independente de registros acima de oito elementos. Fonte: [pesquisa, seção 9](MOD_007_ELEMENT_REGISTRY_FEASIBILITY_2026-09-27.md#9-magic-break-damage-limit-999999), `src/runtime/FfxHooksDll/hooks/NovaSuperDamageHook.cpp`, `shared/ffx_addresses.h`, `tests/NovaSuperDamageRt1.cpp` no checkout runtime atual. O teste existente é referência a revalidar, não foi executado nesta pesquisa.

- [ ] Criar classificação `eligible_damage_spell` por tabela/ID/hash, com import de categorias/flags e variantes Fury. Fire Fury121/Demi Fury133/Ultima Fury138 têm `DamageFlgs=0`; incluir por binding. Attack elemental continua físico. Não conceder elegibilidade automática a toda fórmula MAG, item/Mix/Blue Magic ou Overdrive de Aeon.
- [ ] Reproduzir o seletor nativo em testes: flag de comando `0x80` prevalece; senão `0x40` força 9.999; senão autoability agregada `actor+0x6BE &0x800` permite 99.999. Elevar **somente** o caso magia+HP+BDL efetivo para 999.999.
- [ ] Integrar ao proprietário de `RVA0x38EDD3`, antes do clamp; não multiplicar o valor já capado nem alterar globalmente o imediato em `0x38ED41`. Registrar como se compõe com o bypass Nova existente. Enquanto não existir composição comprovada, recusar ativação conjunta conflitante com mensagem explícita.
- [ ] Preservar o piso negativo, o salto inferior que entra em writeback `0x38EDD9`, todos os registradores/flags necessários e o loop de três componentes. O teto extra só vale na primeira passagem HP; MP/CTB e cura intencional continuam com contrato anterior. Não deixar EBX alterado para a próxima passagem.
- [ ] Matriz RT1 sobre adapter real: sem BDL 9.999; com BDL valores 100.000/450.000/999.999; 1.000.000 corta em 999.999; físico com BDL continua nativo; Fury, dano refletido, single/multi-target/multi-hit, negativo, HP restante menor, Nul, Gravity não letal e teardown.
- [ ] Conferir aplicação no alvo, acumulador de dano, overkill/kill, carga OD, counters e cifra de seis dígitos. Um contador de UI maior sem maior redução real de HP falha; um HP correto com número truncado também falha.
- [ ] Editor OnlyMod expõe o toggle, elegibilidade/bindings e preview de teto por hit, com origem BDL de arma/comando/supressão; preserva checkbox vanilla BDL e o ID existente `0x8019`. Não criar autoability nova para esse limite.

**Aceitação:** uma magia calculada em 450.000 aplica 450.000 com BDL, 9.999 sem BDL; com cálculo 1.200.000 aplica no máximo 999.999. O preview, writeback e número exibido concordam; nenhum efeito novo em categorias excluídas. O probe RT0 desta pesquisa não satisfaz sozinho esse gate.

## T6 — Nul, equipamentos e mods coexistentes

- [ ] Arbitrar Nul vanilla/Radiant/Umbral e wards externos com um evento de consumo. Definir se bloqueia o golpe ou a parcela antes de oferecer `split_weighted` com cargas; a primeira versão pode manter o bloqueio integral, mas deve declarar a regra e preservar casos nativos.
- [ ] Converter deltas de autoability sem duplicar masks já agregadas. ID remapeável de MOD-002 é vínculo separado da chave de elemento; não atribuir ID 135..147 a um elemento novo.
- [ ] Cobrir quatro slots nativos e quinto lógico via API do Workshop, refino, SOS, troca em batalha, fusão/venda/reload. Não inferir que todo novo atributo já é refinável.
- [ ] Detectar Fantasia ou outro proprietário do resolver; desabilitar com diagnóstico ou implementar cooperação explícita. Nunca depender da ordem casual de DLLs.

**Aceitação:** testes de apenas cada mod, ambos, OFF/ON, mudanças de equipamento e estado; mesmo resultado independente de atualização de UI. Integrações não comprovadas bloqueiam o perfil que depende delas.

## T7 — Editor OnlyMod e exportação

Paths existentes para preservar: `FfxLib/Dictionaries/Element_Flags.cs`, `FfxLib/Ability/Ability_Command.cs`, `FfxLib/Ability/AutoAbility_File.cs`, `FfxLib/Common/ElementalWeaknessData.cs`, `FfxLib/Monster/Monster_StatSheet.cs` e módulos de edição correspondentes. Confirmar paths no checkout antes de editar.

- [ ] Criar modelos/serializer próprios do manifest de mod e perfil `[DERIVADO DE MOD-007]`; nenhuma mudança de largura nos serializers nativos.
- [ ] UI de registro, elementos ativos por comando, matriz por monstro/equipamento, 15 patamares, fontes de modificadores, regras Gravity e capacidades exigidas. Adicionar/remove/reorder não altera chaves já referenciadas.
- [ ] Preview usa as mesmas regras e vetores do resolver; mostrar dano/cura, cap, lock e diferença base→efetivo. Labels de UI em inglês e localização pelo pipeline existente.
- [ ] Export staging atômico, hashes e relatório de perda para conversão explícita. ID/linha/locale errado falha sem gravar parcialmente.
- [ ] Projeto vanilla round-trip preserva os bytes de exemplo, não exige DLL/manifesto e mantém as ferramentas atuais.

**Aceitação:** import/export do pack Tenfold preserva elementos 9/10; export vanilla de um deles é recusado. Hook ausente permite authoring offline identificado como tal, mas não permite afirmar execução/admissão. Nenhum novo binário é aplicado automaticamente em Steam/Extracted/Spira.

## T8 — Interface do jogador e conteúdo demonstrativo

- [ ] Scan/Sensor mostram a mesma afinidade usada no dano, percentual/timer/lock e nomes dinâmicos. Grade/páginas para 10/32, texto legível em resoluções e idiomas alvo, foco/controller/Back corretos.
- [ ] Se selecionado menu por tecla F: menu Elemental Dominion independente e sem hotkey padrão; exibir pack, módulos, estado e requerimentos. Não ocupar F7/F8/F9/F10 por suposição.
- [ ] Pack de ensaio pequeno: dois inimigos com perfis opostos, magia externa 9, magia externa 10, magia mista, Imperil, Ward, Cleanse e chefe Gravity. Reusar assets compatíveis com nomes/descrições próprios; elemento novo não precisa de animação nova para provar o efeito.
- [ ] AI de ensaio usa efeitos contra jogador e respeita perfil efetivo quando essa inteligência estiver habilitada. Outras AIs não recebem comportamento novo implicitamente.

**Aceitação:** demonstração determinística de cada mecânica, sem overhaul global obrigatório. Conteúdo amplo/novos assets/Mix completo ficam para fases seguintes.

## T9 — RT2 e entrega entre lanes

- [ ] Revisão independente obrigatória antes de promoção; sem autorização para delegação, registrar gate pendente e executar os demais passos autorizados inline.
- [ ] Solicitar autorização específica somente quando houver candidato concreto para deploy/RT2. Seguir `docs/RT2_PROTOCOL.md`, com save descartável e inventário de arquivos tocados.
- [ ] Matriz viva: OFF/ON, jogador/inimigo/Aeon, todos os elementos/patamares, resist/imune/absorve, Overdrive/Reflect/Nul, boss, multialvo, troca, batalha consecutiva, save/load e remoção do pack.
- [ ] Publicar handoff exato ao Editor: o que o Hook faz, schema/capabilities, campos/arquivos/IDs, ranges, migração, fixtures/vetores, comandos executados, resultado RT0/RT1/RT2 e limitações. Toda opção dependente marcada `[DERIVADO DE MOD-007]`.

## Foco de revisão

1. **Truncamento silencioso:** nono bit que vira zero/Fire; testar entrada/saída de cada binding, não só número total no Editor.
2. **Dano duplicado ou sinal invertido:** original + novo resolver, absorção/Drain/Zombie, divisão negativa e cap; comparar pipeline real com controle OFF.
3. **Instância reaproveitada:** mesmo slot/ponteiro em outro encontro/Aeon recebe stack antiga; geração e destruição precisam de teste de sequência.
4. **Dois donos de hook/carga:** Workshop/Fantasia/NulWard no mesmo fluxo; enumeração de detours, callbacks e teardown juntos.
5. **Pacote sem runtime:** comandos externos instalados ainda funcionam com semântica errada quando DLL falha; provar admissão/fallback antes de release.

## Entregas/commits recomendados

`docs/research` → Eight UI → registry/resolver puro → adapter contextual → status → Gravity → Magic BDL → integrações → Editor → UI/conteúdo → RT2 autorizado. Magic BDL também pode ser entregue como módulo isolado depois de T0 e do contrato de classificação/ownership, sem exigir nove/dez elementos. Cada etapa fecha testes do próprio comportamento e preserva as anteriores. Oitavo elemento pode ser entregue antes de Tenfold; não declarar o 9º/10º prontos só porque um único cálculo foi substituído.
