# Contrato de patch notes — Studio, Hooks, Launcher e Spira Reforged

Regra vigente de 2026-10-03. Cada repositório mantém uma cópia deste contrato
para que a preparação de uma release não dependa de outro checkout.
Aplica-se às notas públicas e ao repasse para o site, não aos logs ou relatórios
técnicos internos. Leia quando preparar notas, changelog de produto ou release.

## Idiomas e responsabilidades

- Toda nota entregue para revisão, GitHub Releases ou integração no site deve
  conter **inglês e português do Brasil completos e equivalentes**. Não entregar
  só um resumo traduzido, link para o outro idioma ou texto a traduzir depois.
- O repositório que produz a mudança prepara e revisa os dois textos. Inglês
  primeiro e português depois, com os mesmos fatos, seções, itens, valores,
  requisitos e problemas conhecidos. Pode começar o rascunho em qualquer deles.
- O coordenador responsável pelo site recebe esse par EN/PT-BR e cuida dos
  **sete idiomas restantes**: espanhol, francês, italiano, alemão, japonês,
  coreano e chinês tradicional. O produtor não precisa preparar nove versões.
- O site preserva o par revisado, usa a base de nomes oficiais e confere todas
  as traduções antes de disponibilizar a nota. O uso de Luna/subagentes segue
  a autorização vigente da tarefa; este documento não concede delegação,
  chamadas a providers, gasto ou publicação por conta própria.

## Uma nota por produto e versão

| Produto | Categoria no site | Regra de origem |
| --- | --- | --- |
| Studio / Editor | `studio` | Mudanças do editor. A próxima versão publicada será oficial/estável. |
| Hooks | `hooks` | Mudanças do runtime. Preservar o estágio e o sufixo reais da versão, inclusive beta. |
| Launcher | `launcher` | Instalação, atualização, inicialização e backups; separar a versão do Launcher das versões dos produtos instalados. |
| Spira Reforged | `spira-reforged` | Conteúdo e balanceamento do mod, com versão do pacote próprio, mesmo quando os arquivos ficam no repo do editor. |

Uma entrega envolvendo dois produtos gera duas notas, cada uma com sua versão.
Não criar uma nota de Studio para registrar apenas uma mudança de Spira, Hooks
ou Launcher. Documentação e regras de agentes, por si sós, não geram release,
bump nem novidade de produto.

## Onde guardar e como reutilizar

1. Guarde a nota bilíngue da release em
   `docs/release-notes/<categoria>/<versao>.md` no repositório responsável.
   Use o número real; datas ainda não confirmadas ficam identificadas no
   rascunho e não podem ser apresentadas como publicação concluída.
2. No Studio, mantenha o resumo humano correspondente em `changelogUS.md` (EN)
   e `CHANGELOG.md` (PT). Em repos com um único `CHANGELOG.md`, use as duas
   seções de idioma ou aponte para a nota bilíngue completa. Não criar um
   changelog artificial quando o repo já usa apenas notas por release.
3. No **GitHub Releases**, reutilize esse mesmo par, com `English` primeiro e
   `Português (Brasil)` depois. Confira o destino público no contrato/pipeline
   de publicação do produto; repo de código, espelho e archive operacional
   podem ser destinos diferentes. Não usar o diário técnico como corpo da release.
4. Para o **site**, entregue os dois textos e os dados de origem: categoria,
   versão, estágio, tag/commit ou pacote conferido, data e URL de publicação
   quando existentes. Evidência técnica fica separada do texto do leitor.
   O site adapta a nota para `data/updates/releases.json`, completa os idiomas
   e valida conforme `docs/UPDATE_NOTES_GUIDE.md` do repositório do site.

O histórico técnico antigo permanece preservado. O catálogo antigo do site
fica em **Beta Update**; notas novas não devem ser despejadas nesse gerador.
Uma beta atual de Hooks pode ter uma nota nova, com o selo correto, sem se
tornar parte do arquivo antigo. Não reclassificar releases antigas retroativamente.

## Como escrever

- Escreva para quem joga ou usa a ferramenta: o que mudou, onde aparece e se
  a pessoa precisa fazer algo. Título curto; resumo de uma ou duas frases.
- Escolha até três destaques. Use apenas as seções necessárias: Novidades,
  Melhorias, Correções, Balanceamento e Problemas conhecidos. Uma nota inaugural
  pode incluir O que está incluído, deixando claro que é um inventário cumulativo.
- Nas versões seguintes, descreva o que mudou desde a anterior do mesmo produto;
  não repita toda a apresentação do produto como se tudo fosse novidade.
- Uma mudança por item. Números antes/depois e motivo breve ajudam quando
  comprovados; não invente desempenho, compatibilidade ou benefícios.
- Não publicar lanes, IDs internos, offsets, nomes de classes/arquivos, hashes,
  logs, contagens de testes, provas RT0/RT1/RT2 ou relatos de trabalho de agentes.
  Instalação avançada e evidência detalhada podem ter um link separado.
- Explique os limites em linguagem comum. Código presente, asset preparado,
  build aprovado e recurso disponível ao jogador são estados diferentes.
  Uma opção desligada, experimental ou dependente de outro pacote deve ser descrita assim.
- Preserve os nomes públicos dos produtos e consulte os nomes oficiais do jogo.
  Sem tradução confirmada, mantenha o nome inglês; não invente nomes para
  habilidades, cartas, efeitos ou recursos sem nome. Localize a apresentação
  dos números sem mudar seus valores, versões ou atalhos.
- Preserve os textos antigos. Novas informações corrigem a nota correspondente
  de forma rastreável, sem apagar limitações ou reescrever o passado silenciosamente.

## Modelo bilíngue

Copie só as seções necessárias, preencha os campos e remova os exemplos antes
da publicação. Os títulos e conteúdos dos dois idiomas devem corresponder.

```markdown
# <Produto> <versão>

## English
**<Short title in English>**

<Summary in one or two sentences.>

### Improvements
- <What changed and what the player or user will notice.>

### Known issues
- <A real limitation and a workaround, if one exists.>

## Português (Brasil)
**<Título equivalente em português>**

<Resumo equivalente em uma ou duas frases.>

### Melhorias
- <A mesma mudança e o efeito para quem joga ou usa o produto.>

### Problemas conhecidos
- <A mesma limitação e a alternativa, quando houver.>
```

## Antes de entregar

Confira paridade EN/PT-BR, nomes, números, atalhos, versão, estágio, plataformas,
requisitos e limites contra a release/pacote correto. Não usar um checkout dirty
mais novo para descrever um artefato antigo sem distinguir os dois.
As notas não substituem gates de release, revisão independente exigida,
permissões de publicação ou evidência de funcionamento. Preparar os textos não
autoriza commit, push, merge, GitHub Release, deploy do site ou execução no jogo.
