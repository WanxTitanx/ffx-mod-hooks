# MOD-008 — Plano de implementação e handoff

**Jarvis-HOOK.** Planejamento para o runtime atual; etapas abaixo ainda não executadas como Hook.

**Docs/assets:** `/home/wanderson/.codex/worktrees/mod-008-arcana-fayth/ffx-hooks`, `codex/mod-008-arcana-fayth-20260927`. **Runtime:** `/home/wanderson/Documents/ffx-hooks`, `main` `f2308ddd`. **Editor:** `/home/wanderson/Documents/ffx-editor-main`, `codexclaudiocodeffxeditor`. Ler o [design](<../mod-ideas/MOD 008 - ARCANA OF THE FAYTH.md>) e a [evidência](MOD_008_NATIVE_INJECTION_2026-09-27.md) antes de implementar.

## Arquitetura pretendida

```text
ArcanaSettings --startup admission--> ArcanaRuntime
ArcanaPack ----validated snapshot---> ArcanaCatalog
Native Equip <----> ArcanaEquipAdapter <----> ArcanaTransaction
                                            |    |
                           ArcanaStore <-----+    +----> EffectSources
                           shared save bus              |
                           vanilla projection           v
                                                Existing field/battle/
ArcanaAssets --> render-thread resource cache    reward/cap owners
                 --> Equip preview
```

Os nomes acima são módulos propostos, não arquivos existentes. Dependências não podem criar ciclos: Store não chama UI; renderer não lê disco nem modifica coleção; o agregador consome snapshot imutável e não roda transações.

## T0 — Fixar o contrato e o perfil

- Revalidar main/PE/branch/source antes da implementação; gerar spans com relocação e inventário de owners atuais.
- Configuração proposta: `arcana.enabled=false`, `arcana.default_mode=A`; capacidade de pack e roster explícitas. Resolver ambiente/INI/default conforme o sistema atual, sem chaves paralelas divergentes.
- Slot A: 2, B: 3 com custo Maior2/Menor1/orçamento4. Zero cartas é válido; Major22/Minor56 e corte sem número são invariantes de conteúdo.
- Criar tipos separados `CardKey`, `CardId`, `ActorId`, `EquipmentIndex`. Nunca converter um CardId para equipment word.
- **Aceite:** contratos de estado/arquivo versionados, nenhuma dependência obrigatória de MOD-002/004/005/007 só para abrir o catálogo.

## T1 — Pack, coleção e transações isoladas

- Parser limitado, sem execução de texto/script. Rejeitar schema futuro, duplicata, efeito desconhecido, recurso inválido e referência fora do pack.
- Estado único de ownership, projeção de slots, revision monotônica. Preview e confirmação usam token; recaptura antes de commit.
- Equip, unequip, transfer e troca A/B são atômicos. Um swap opcional deve validar os dois destinos; transferência simples não faz troca implícita de carta substituída.
- Awards idempotentes e coleção não perdível. Não abrir inventory nativo para guardar cartas.
- **RT0/RT1 exigidos:** limites A/B, IDs repetidos, mesma carta em dois atores, slot inválido, estado velho, troca modo inválida, owner temporariamente ausente, crash/retry, perda/corrupção de arquivo, overflow/tamanho/path traversal.
- **Aceite:** um comando rejeitado preserva todo o estado; não apenas o ator em foco.

## T2 — Save compartilhado e projeção vanilla

- Ler o produtor real de I/O usado por `NativeSaveEvents`; não inserir um segundo Hook na mesma rotina.
- Substituir o observer único por dispatcher limitado, cobrindo Workshop + Arcana em ambos os orders de start/stop. Manter rollback e teste dos subscribers já existentes.
- Determinar todos os campos temporários que chegam à serialização: máximos/current HP/MP, stats, autoeffects, CTB se aplicável. Projetar somente a cópia que será escrita, sem remover efeitos do combate em memória por um frame.
- Preparar extensão, gravar nativo, confirmar sidecar contra bytes efetivos. Guardar journal e recuperar par exato. Não retry automático que regrave o save após falha parcial de outro módulo.
- **RT1 exigido:** todos os pontos de interrupção antes/depois de prepare/native-write/sidecar-rename/commit; save-as; caminhos Unicode; cold-load; new-game; copiar apenas save; copiar save+sidecar; OFF save, depois ON.
- **Aceite:** save produzido ON continua estruturalmente vanilla e carregável OFF; nenhuma carta vira autoability/equipamento nativo. Preservar progresso legítimo do jogo.

## T3 — Corte vertical no Equip nativo

- Fechar assinatura/contexto/callers de RVA `0x4CEFF0`. Tratar `context+0x1C` como estrutura própria até provar relação com a shell.
- Implementar apenas uma carta de teste de HP +10%, inicialmente. Entrar por Menu → Equip; adicionar linhas de Tarot com state/input/draw próprios no mesmo fluxo.
- Preservar branches arma/armadura, Workshop labels, seletores, painéis e tipos 0/1. Não passar índices de Tarot para `MsEquipSaveWeapon`.
- Preview de identidade, equipamento atual, novo efeito e confirmação. Frame seguinte revalida antes de mutation.
- **RT1 exigido:** controlador e lista reais em harness quando viável; teclado/controle, press/repeat, cancel/back, actor-switch, transições, refcount/teardown e OFF; substituições do fixture documentadas.
- **Aceite:** teste do adapter mede callbacks e roteamento, não só compara strings do source. RT2 posterior confirma foco/posição real.

## T4 — Ícone, verso e preview de imagem

- Escolher loader nativo com registro privado ou cache D3D11 contextual. Fechar ownership de textura/contexto/reset; atlas ID arbitrário não é recurso registrado.
- Converter assets de conceito para o formato efetivamente aceito pelo loader; preservar masters. Não reaproveitar textura vanilla em uso.
- Nome/numero da imagem: medallion top + plaque bottom; cortes sem número. Não derivar número do index 0–77.
- Upload em render thread; decode em worker; cache limitado a selecionada/vizinhas; stale requests rejeitados por geração.
- **RT1/visual exigidos:** imagem ausente/truncada, dimensão inesperada, tipo/alpha correto, pending upload, resize/device-loss, menu aberto/fechado repetidamente, aspect ratio e ordem/clipping.
- **Aceite:** uma carta é visível no painel Equip sem fullscreen overlay, troca correta ao navegar, imagem liberada/reusada sem vazamento. Setenta e oito PNGs no disco não atendem sozinhos a este gate.

## T5 — Efeitos por capacidade, não por 78 Hooks

1. **Stats/flags nativas:** atributos e booleanos, Element Strike/Ward, proofs e SOS. Agregação idempotente campo/batalha/preview e save.
2. **Consumíveis e reação:** Counter/Magic Counter/Evade & Counter, Auto-Potion/Phoenix; seguir regras e consumo nativos, impedir callback recursivo.
3. **Recursos e condições:** turn-start MP, kill heal, Lovers, Judgement. Usar action/turn/battle IDs; sem credit por overkill, cura, miss, aliado/objeto ou evento repetido.
4. **CTB/MP/recompensas:** classificar command families; No AP, One MP/Spellspring, rank e drops respeitados; integrar policies existentes.
5. **Dano/elementos/tetos:** um pipeline com MOD-007/Nova/Workshop/Aeons; validar HP/MP/CTB, dano/cura, multihit e Fury. The World concede BDL normal, não novos Breaks Aeon.

Cada handler publica sua capability somente depois dos testes. Uma carta que exige handler ausente fica indisponível com motivo; não aplicar apenas metade dos efeitos e anunciar sucesso.

**Aceite:** matriz por família/efeito e pares de módulos, incluindo desligado/desligado, ligado/desligado, desligado/ligado e ambos ligados. Dano calculado dentro de cap não significa renderer de seis dígitos validado.

## T6 — Aquisição e conteúdo completo

- Resolver IDs/flags reais dos milestones propostos e callbacks de recompensa. Dados do catálogo não contêm endereços de eventos fictícios.
- Cada carta tem caminho garantido e recuperação para saves avançados; recompensa de The World não exige possuir The World.
- Exibir nomes longos, referências de FFX, fontes traduzidas, slots A/B e motivos de carta reservada. Ícone e rótulo distinguem naipe/categoria sem depender só da cor.
- Abrir menu com 0/1/78 cartas, muitos owners, último item e troca rápida de preview. Validar coleção importada/corrompida e imagens faltantes.
- **Aceite:** catálogo completo no jogo, referências cruzadas sem duplicatas e aquisição sem perda definitiva.

## T7 — Entrega exata para a lane Editor

Tag **`[DERIVADO DE MOD-008]`** em todas as opções dependentes. Entregar os schemas executáveis, não os arquivos `*.proposed.json` como se fossem runtime. Exemplo mínimo e exemplo completo; IDs/chaves, versões/capabilities, regras A/B, ownership, localização e asset compiler. Preview de diferenças e diretório de exportação independente.

Regressão obrigatória: abrir/exportar projeto vanilla sem instalar Hook, sem nova linha em kernels, sem converter save e sem esconder as ferramentas vanilla existentes. Um pacote de mod inválido não deve quebrar a abertura de um projeto vanilla.

## T8 — RT2 e publicação do módulo

Seguir `docs/RT2_PROTOCOL.md`: autorização específica de deploy/RT2, save descartável, identidade de DLL/config/PE/pack, casos limitados, logs e restauração. Casos mínimos: OFF; ON modo A; ON modo B; transferência; mudança B→A; save/load; ausência do Hook/pack; coexistência Workshop/Drop/Elemental Dominion; preview de cada naipe; alta resolução e controle.

Compilar/provar em harness precede esse passo. Publicar esta branch de ideias e artes não é promover uma DLL para produção.

## Padrão de relatório por etapa

`Claim → comando/artefato/hash → camada RT0/RT1/RT2 → resultado → conflito/limitação → próxima ação`.

Sem subagentes nesta tarefa. Revisão própria não equivale a revisão independente; se o projeto exigir gate independente para integração/release, ele permanece explícito para a lane executora.
