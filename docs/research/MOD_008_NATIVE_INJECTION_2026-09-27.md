# MOD-008 — Pesquisa de injeção nativa

**Jarvis-HOOK · RT0: source, API e bytes; não RT2.**

Repo de entrega: `/home/wanderson/.codex/worktrees/mod-008-arcana-fayth/ffx-hooks`, branch `codex/mod-008-arcana-fayth-20260927`, base documental `711e0850`. Runtime consultado: `/home/wanderson/Documents/ffx-hooks`, `main` `f2308dddc1833811899c0999c96b5ee25a18bd66`. Editor: `/home/wanderson/Documents/ffx-editor-main`, `codexclaudiocodeffxeditor`.

## 1. Veredito delimitado

Há infraestrutura concreta para interceptar funções nativas, desenhar texto/janelas e acompanhar equipamentos e saves. **Uma categoria Tarot dentro do Equip é tecnicamente plausível, mas ainda requer um adapter novo de controle/lista e um caminho de carregamento de imagens próprias.** O Workshop atual resolve detalhes de autoabilities; não demonstra que duas/três novas linhas de categoria já funcionem.

Adicionar um arquivo novo não injeta código. A integração usa o bootstrap existente de FFX Hooks, um módulo opt-in e adapters nos consumidores do jogo. Não há necessidade arquitetural de alterar o EXE em disco nem de copiar o motor de FFX-2; isso ainda exige validação do caminho completo no FFX suportado.

## 2. Identidade da evidência

| Artefato | Identidade |
|---|---|
| FFX.exe | `/mnt/nvme-samsung/SteamLibrary/steamapps/common/FINAL FANTASY FFX&FFX-2 HD Remaster/FFX.exe` |
| SHA-256 | `78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced` |
| Formato | PE32 i386 `0x14C`, base preferida `0x00400000`, timestamp `0x55D2F3CC` |
| Endereços | Flat = base preferida + RVA. Runtime usa **base carregada + RVA**, não flat absoluto |
| Leitura nova | Python `pefile` + Capstone, faixas estáticas limitadas; hashes em `research/mod_008_arcana/pe-evidence.json` |
| Dumps locais | `/home/wanderson/.codex/research-cache/mod008-fandom/`, fora do Git |

As faixas lidas não são uma afirmação de extensão exata das funções. O controlador tem jump table e retornos intermediários; disassembly linear pode atravessar dados. Somente os trechos de entrada/ramificações explicitamente citados abaixo sustentam as conclusões.

A IDB canônica da VM não foi aberta ou alterada nesta tarefa. A evidência nova vem do executável local com hash conferido, além do source atual e relatórios anteriores identificados como históricos.

## 3. FFX-2: o que serve de referência

Fahrenheit em `/home/wanderson/Documents/external-compare/fahrenheit`, commit `c149c847b3a24a66114956f87f1b008599736f75`, contém:

- `src/core/ffx2/Accessory.cs`: nome/help, ícone, dez modificadores `sbyte`, quatro habilidades `ushort` e preço.
- `src/core/ffx2/plysave.cs`: `Accessories` com duas entradas `ushort`, localizado em `PlySave+0x3A` naquela estrutura.
- `src/core/ffx2/savedata.cs`: arrays de IDs/quantidades de acessórios em `+0x7C80/+0x7D80` daquela estrutura.

Os próprios arquivos dizem **Switch release**. Esses offsets documentam FFX-2 e não podem ser escritos no save do FFX PC. Usamos o modelo conceitual (definições, inventário e dois slots) como comparação. Não copiamos código do renderer ou implementação de acessório. Origem: [Fahrenheit](https://github.com/fahrenheit-crew/fahrenheit/tree/c149c847b3a24a66114956f87f1b008599736f75/src/core/ffx2), licença declarada nos arquivos `LGPL-3.0-or-later`, consultada em 27/09/2026.

Fandom: API `https://finalfantasy.fandom.com/api.php`, parâmetros `action=parse`, `page=Final_Fantasy_X-2_accessories`, `prop=wikitext|text|sections|revid`, `format=json`. Revisão 3940120, SHA `b904fd00e46453aacd27a2d0dac7c797f5bdab3b91d1fdf7e32d39a9ec2616b5`. Download API HTTP200; a tentativa de leitura HTML por navegador de pesquisa falhou e não sustenta a análise. Snapshot integral e tabela extraída ficam no cache local; o repositório mantém recibo e propostas originais, sem republicar o artigo inteiro.

## 4. Equip: pontos reais e o que falta

| Superfície | RVA / flat preferido | Prova e limite |
|---|---|---|
| Controlador Equip | `0x4CEFF0` / `0x8CEFF0` | CSV Fahrenheit nomeia `TkMenuCtrlEquip`; bytes locais mostram máquina de estados e chamadas de equipar. Novo controle Tarot ainda não existe |
| Detalhes do equipamento selecionado | `0x4D02B0` / `0x8D02B0` | `EquipmentWorkshopNativeUi.cpp`, detour `EquipmentShim`; adapter de detalhes já existente |
| Equipar peça nativa | `0x3AB990` / `0x7AB990` | Chamadas no controlador e ownership do Workshop; Tarot nunca passa seu ID aqui |
| Consultar peça nativa | `0x3ABBF0` / `0x7ABBF0` | Hook compartilhado do Workshop; stride nativo 22 B |
| Agregação de autoabilities de batalha | `0x39C610` / `0x79C610` | Já pertence ao Workshop; ponto candidato para compor uma fonte Arcana |
| Cálculo de atributos de campo | `0x3861B0` / `0x7861B0` | Já pertence ao Workshop; exige integração coordenada, caps e projeção de save |
| Cálculo de dano | `0x38E680` / `0x78E680` | Já usado pelo Workshop; o clamp separado também tem consumidor Nova. Não instalar outro detour concorrente |

### Observação nova sobre a assinatura do controlador

O CSV fornece um protótipo incompleto sem argumentos. Porém, os bytes em flat `0x8CF067` fazem `mov esi,[ebp+8]`, depois `mov eax,[esi+0x1C]`, com limite `0x1E` e salto pela tabela em `0x8CF5C0`. Logo **não usar o protótipo sem argumentos do CSV**. O significado completo do contexto e ABI precisa ser fechado a partir dos callers antes do detour.

Esse `context+0x1C` não é o campo `O_STATE=40` do objeto genérico de `NativeMenuShell`. São objetos/estágios diferentes até que a RE demonstre sua relação. Copiar a tabela de offsets da shell para o controlador seria uma suposição perigosa.

O controlador compara a seleção global em flat `0x0186A5E4` com os casos 0/1 em `0x8CF0EC..0x8CF106` e chama caminhos distintos; em `0x8CF135` chama o equipador nativo. É evidência de que o fluxo precisa ser estendido, não licença para escrever `2` nessa global e esperar um acessório. As globais também precisam de relocação e validação de perfil.

### Adapter proposto

1. Fechar assinatura/callers, criação/destruição e relação com os objetos de lista; capturar ator e geração do menu.
2. Criar estado Arcana externo ao objeto nativo: `NativeGear`, `TarotSlots`, `TarotPicker`, `TransferConfirm`, `ModeResolution`.
3. No ramo arma/armadura, delegar integralmente ao fluxo nativo e às extensões já registradas do Workshop.
4. Nas linhas Tarot, capturar input no mesmo controlador e usar lista/desenho nativos, mantendo um token de contexto. ID de carta nunca vira índice de arma ou ponteiro para registro de 22 B.
5. O painel de preview desenha a arte e texto; nenhum comando é aceito a partir de snapshot de seleção antigo.
6. Back/fechamento/load/reset invalidam o token. TLS limita escopo de leitura/desenho e profundidade; consumidores desconhecidos usam a rota original.

**Prova ainda necessária:** novos índices de linha, cursor, confirma/cancela, scroll, botão de ombro, seleção de ator, cancelamento durante transição e retorno ao submenu pai. Os harnesses do Workshop são bons padrões de teste, mas seus resultados anteriores não cobrem este adapter.

## 5. Desenhar não é carregar: imagens próprias

`src/runtime/NativeMenuShell/NativeMenuShell.h` já expõe:

| Operação | Flat / RVA | Observação |
|---|---|---|
| Resolver handle por atlas ID | `0x8AC870` / `0x4AC870` | `TexHandleByAtlasId`; só funciona para recurso residente |
| Emitir quad texturizado | `0x63F090` / `0x23F090` | `Fn_EmitQuadAccum`; exige handle válido e lifetime correto |
| Clipping do quad | `0x8E5A20` / `0x4E5A20` | Usado por `DrawTexByAtlasId` |
| Escalas de layout | `0x644990/0x6449D0` | Espaço de design 1920×1080; validar proporção na apresentação final |
| Ícones de input nativos | `0x6365E0` | Exemplo histórico de carga de `.dds.phyre`, não loader genérico de PNG demonstrado |

A anotação da shell diz explicitamente que o atlas deve estar residente. **Criar `tarot.png` e chamar um atlas ID inventado não carrega a imagem.** Também não sobrescrever `battle.dds.phyre`, ícone de item ou textura vanilla para simular um registro novo.

Duas rotas de implementação a comparar no corte vertical:

- **A — Recursos próprios no renderer nativo:** loader com chaves reservadas para Arcana, registro/remoção/lifetime provados, quad no painel Equip. Precisa resolver formato `.dds.phyre`, registro e cache, incluindo reset e coexistência com substituidores de textura.
- **B — Recursos D3D11 próprios apresentados no contexto Equip:** decodificar assets em worker, criar textura/SRV na thread de render, compor somente no retângulo do painel nativo, preservando device/context/state/scissor e ordem de apresentação. O source atual em `dllmain.cpp:4963+` contém criação de textura/SRV para Aurora; é precedente de acesso D3D11, não um renderer Tarot pronto. A navegação/slots permanecem no Equip.

A é preferível se a RE do registro ficar fechada; B é alternativa técnica sem ocupar atlas vanilla. Não reutilizar o overlay Aurora inteiro como substituto do menu. Qualquer rota precisa de 32-bit resource budget, upload limitado, device loss/ResizeBuffers, troca de resolução, fechamento de menu, teardown e reentrada. Não fazer I/O/decodificação em DllMain ou dentro do hook de draw.

Proposta de cache: ícone/verso fixos + selecionada e vizinhas, com orçamento explícito. Setenta e oito RGBA 1024×1536 consomem cerca de **468 MiB antes de mipmaps**, além de cópias de CPU; não carregar todos indiscriminadamente num processo x86. As imagens desta entrega são masters/conceitos, não DDS/Phyre já prontos.

## 6. Save: conflito confirmado e solução necessária

`src/runtime/FfxHooksDll/hooks/NativeSaveEvents.h` contém `std::atomic<const Observer*> observer`. `Subscribe` usa compare-exchange para um único ponteiro. `EquipmentWorkshopRuntime.cpp` já cria seu observer e tenta assiná-lo. Arcana não pode instalar uma segunda assinatura independente e presumir que ambos funcionarão.

**Recomendação:** dispatcher compartilhado, número limitado de subscribers, inscrição estável durante inicialização, unsubscribe sem espera e lifetime até saída do processo. Um único produtor de save entrega a cada módulo a mesma geração e resultado efetivamente escrito. Garantir que uma falha de Arcana não retire o observer do Workshop.

O evento atual `WriteCompleted` é posterior à gravação e fornece bytes efetivos. Ele serve para associar o sidecar; **não basta para remover efeitos temporários antes que sejam serializados**. O produtor real foi localizado em `src/runtime/FfxHooksDll/hooks/RonsoPoolRuntime.cpp`: `ReadShim` notifica após soltar o mutex; `WriteShim` prepara `IoWork.input/output`, escolhe a cópia de saída e chama `writeOriginal` exatamente uma vez. Somente depois notifica `WriteCompleted`. O filtro `IoCaller` aceita retorno de leitura RVA `0x2F0228` e escrita `0x2F06C5/0x2F0A45`. Esses números identificam callers já usados no source, não novos locais de detour provados nesta tarefa.

A projeção Arcana deve participar da preparação da cópia **antes** de `writeOriginal`, compondo com `SavePool`; a notificação pós-escrita usa os bytes realmente gravados para o journal/sidecar. Preservar `GetLastError`, epoch, tamanho, path/stream, ausência de retry e a semântica Ronso OFF/ON. O `PlySave` FFX em Fahrenheit contém `hp/mp/max_hp/max_mp`, stats derivados e autoeffects: é mais uma razão para verificar a serialização desses campos, não assumir que todo bônus temporário fica fora do save.

### Persistência proposta

- Ownership: uma tabela `card_key -> owner/slot` autoritativa, com projeção por ator; nunca duas tabelas independentes que possam divergir.
- Estado contém schema, pack identity/revision, modo, cartas adquiridas, slots, award receipts, geração e relação ao save nativo exato.
- `Preview` captura geração/roster/owner. `Commit` recaptura e valida, troca as duas propriedades numa operação e só publica após persistência do estado válido.
- Gravação nativa e sidecar usam journal/prepare/commit com hashes; falha entre os dois deixa diagnóstico recuperável, não um sucesso falso nem aplicação automática de sidecar antigo.
- Save As cria ramo de linhagem explícito. Load de arquivo desconhecido não associa coleção por proximidade de horário. New Game não herda o baralho anterior.
- OFF deixa save nativo carregável. Reativação após sessão sem Hook exige associação/reconciliação explícita quando o hash mudou; não há identificador mágico oculto a ser inventado em padding.
- Projeção de HP/MP: máximos/atributos vanilla devem ser reconstruídos a partir das fontes nativas; HP/MP corrente fora desses limites é normalizado na cópia de save. O par sidecar pode guardar o estado ampliado para uma retomada ON, mas deve provar ausência de cura/duplicação por load repetido.

O modelo offline de ownership valida a regra de coleção, não executa a gravadora de jogo. Integridade por SHA não comprova crash consistency; o harness precisa interromper cada etapa real de I/O.

## 7. Consumidores e convivência

Usar adaptadores no proprietário atual dos pontos, não novas instalações nos mesmos RVAs. `MinHookBatchCoordinator` tem limite por batch (`kMaximumTargets=16` no source consultado); agrupar uma instalação atômica e conferir ownership, sem aumentar constante por conveniência.

| Outro módulo | Contrato requerido |
|---|---|
| Workshop MOD-004/005 | Carta separada de peça/quinta habilidade; unificar eventos de save e fontes de efeitos, manter índices de equipamento intactos |
| MOD-002 / novas autoabilities | Referenciar chaves de handler e IDs configurados, sem reservar 78 linhas nem alterar registros 135–174 |
| Double/Triple Drop | Um multiplicador final `max`, mesmo conjunto de personagens elegíveis e limites do inventário; não aplicar no produtor e no consumidor novamente |
| MOD-007 | Um pipeline de afinidade/Imperil/cap; Tarot não muda u8 para u16 nem reimplementa clamp da Nova |
| Aeon Ascension | The World não autoriza os tetos exclusivos 999.999; nenhuma carta falsifica recibo de compra do Workshop |
| MOD-006 | Strings/nomes localizados e glyphs via contrato de idiomas; nome longo não é ID |
| Hooks ausentes/pack inválido | Nenhuma categoria extra no Equip; arquivos vanilla mantêm layout, path e semântica |

## 8. Confiança e próximos gates

| Claim | Evidência | Confiança | Conflito/limite | Próximo passo |
|---|---|---|---|---|
| FFX-2 separa acessórios de outros equipamentos | Source Fahrenheit + API do Fandom | Alta para os artefatos consultados | Estruturas Switch, jogo diferente | Criar contrato próprio no FFX PC |
| Controlador Equip é ponto de investigação concreto | CSV + entrada PE, jump table, calls | Alta para bytes; média para semântica completa | ABI do CSV incompleta | Callers/contexto e harness do adapter |
| Workshop desenha detalhes adicionais | Source atual e relatório histórico RT1 | Alta para existência no source | Não cria categoria Tarot | Provar novas linhas/input/preview |
| Recursos próprios podem usar renderer nativo ou D3D11 | Primitivas e criação SRV no source | Média, projeto plausível | Loader Tarot e atlas privado não provados | Uma imagem privada no Equip em harness e RT2 autorizado |
| Segundo observer independente conflita | Header de um ponteiro + Workshop Subscribe | Alta | Falha de coexistência se copiado ingenuamente | Dispatcher/projeção com regressão dos saves |
| Bin novo pode ser ignorado OFF | Arquitetura proposta de loader exclusivo | Projeto, não resultado medido | Mounter/packager não pode mapear path para kernel nativo | Testes OFF, pack ausente, versão/corrupção |

**Nenhuma nova DLL foi compilada/instalada; nenhum save ou kernel do jogo foi escrito; nenhum RT2 ocorreu neste estudo.** A publicação de docs e artes não altera esse estado.
