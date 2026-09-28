# MOD-008 — Recuperação e revisão da entrega

**Jarvis-HOOK · 27/09/2026 · pesquisa, modelo RT0 e artes conceituais.**

## Continuidade recuperada

A tarefa `01a0ced4-9fba-7a22-9466-d8897fbcba59`, “Organizar ideias de mods e hooks”,
terminou com erro de limite de contexto. O material do MOD-008 ficou salvo,
ainda sem commit, na branch `codex/mod-008-arcana-fayth-20260927`, worktree
`/home/wanderson/.codex/worktrees/mod-008-arcana-fayth/ffx-hooks`, base
`711e08507fb35ae85b319e31f1624789f0c22d77`. A continuação recuperou a tarefa e
selecionou esta entrega; não executou as etapas futuras do plano nativo.

O erro não indicava corrupção dos PNGs. O histórico contém uma falha anterior de
IPC de aproximadamente 275 MB contra limite de 64 MiB, durante o retorno das
gerações, e depois o erro explícito de contexto. Isso identifica os erros
registrados; não prova uma única causa interna do aplicativo. Na recuperação,
as imagens foram inspecionadas por arquivos e folhas de revisão, sem imprimir
base64 em mensagens de texto.

## Identidade preservada

- **Spira: Arcana of the Fayth**, com 22 Maiores e 56 Menores tradicionais.
- Molde original de marfim, azul e ouro: número em cima, nome embaixo.
- Ordem RWS: Louco 0, Força VIII, Justiça XI; as 16 figuras não recebem número.
- Item: número, nome tradicional e referência de FFX; figuras omitem o número.
- Uma cópia por save, qualquer personagem elegível, reserva global e transferência explícita.
- Modo A: duas cartas. Modo B: duas Maiores, uma Maior e duas Menores, ou três Menores.
- Equipamento no menu Equip nativo; pacote/estado próprios e módulo OFF por padrão.

As regras continuam no [design](<../mod-ideas/MOD 008 - ARCANA OF THE FAYTH.md>).
Não foram substituídas por um novo conceito ou por um menu em tecla F.

## Revisão das artes

Foram inspecionados os nomes, numerais, ausência de números nas figuras,
posição das faixas, famílias e coesão das **78 cartas**, em seis folhas de
revisão locais. Cartas com símbolos pequenos receberam leitura individual;
também foram abertos o verso e o ícone. Essa revisão é visual, não OCR nem teste
do renderer do jogo, e não garante ausência de qualquer detalhe artístico a refinar.

O Nove de Copas original tinha nove taças grandes mais uma pequena ao fundo.
A ferramenta `image_gen.imagegen` removeu somente a taça extra na versão
`044-cups-nine-v2.png`. A imagem selecionada tem nove taças, conserva IX, título,
personagem e moldura. O original permanece local; o recibo 044 conserva sua
geração e acrescenta prompt, origem e hashes da revisão. O catálogo e a galeria
apontam para v2; não contam as duas versões como cartas diferentes.

O conjunto selecionado contém **81 PNGs**: 78 cartas 1024×1536, molde, verso e
ícone. O ícone possui alpha real. O molde é referência visual e tem alpha
parcial; não foi declarado um overlay recortado pronto para o engine. Nenhum
PNG foi convertido, registrado ou carregado como textura de FFX.

[Galeria local](../../assets/mod-008-arcana/gallery.html) ·
[Manifesto com hashes](../../research/mod_008_arcana/assets-manifest.json) ·
[Direção de arte](<../mod-ideas/MOD 008 - DIRECAO DE ARTE.md>)

## Verificação e limites

| Claim | Comando/evidência | Resultado e confiança | Limite / próxima ação |
|---|---|---|---|
| Catálogo e regras de ownership coerentes | `python3 research/mod_008_arcana/probe_contracts.py` | PASS_RT0_MODEL_ONLY; 76.469 verificações, incluindo 76.076 trios | Modelo Python; sem callbacks nativos, parser de pack ou I/O de save |
| Backlog inclui MOD-008 | `python3 research/mod_ideas_precode/generate_ledger.py --check` | PASS 98/98 | Inclui duas linhas de template; não equivale a 98 mods implementados |
| Assets, origens, links e sintaxe | `python3 research/mod_008_arcana/verify_delivery.py --local-evidence` | Resultado final registrado em `delivery-validation.json` | RT0; não executa browser nem funções do jogo |
| Pesquisa permaneceu na mesma identidade | Rehash de 17 fontes, snapshot API e PE registrados | Conferência local pelo mesmo verificador | Hash confirma identidade; não promove RE estática para gameplay |
| Galeria referencia o baralho selecionado | 78 registros comparados ao catálogo; `node --check` | Dados/caminhos/sintaxe verificados | Navegação e responsividade em browser não testadas |

O contador antigo do ledger verificava 98 entradas, mas imprimia `97/97`.
A mensagem agora usa a contagem real de linhas. O consumidor em
`run_offline_suite.py` reconhece a mensagem de sucesso sem fixar um número
histórico. Apenas essa etapa do wrapper foi exercitada; a suíte histórica
completa, dependente de outro checkout do Editor, não foi rerodada nem alegada.

O navegador da tarefa anterior bloqueou a abertura do HTML local por política
de URLs. A recuperação não contornou esse bloqueio: conferiu os arquivos e a
sintaxe offline. A galeria é entregue como arquivo, sem afirmar teste de
interação no browser.

## Próxima etapa técnica

O [plano nativo](MOD_008_IMPLEMENTATION_PLAN_2026-09-27.md) continua pendente,
começando pelos contratos/pack e pelo barramento compartilhado de save. O
[estudo de injeção](MOD_008_NATIVE_INJECTION_2026-09-27.md) identifica a hipótese
de adapter do Equip, o observer único atual e as rotas possíveis de texturas.
O runtime a consultar fica no checkout atual de `ffx-hooks`; não copiar o
runtime antigo herdado por esta branch documental.

Esta entrega não alterou DLL, kernels, save ou Editor. Não houve build de DLL,
instalação, RT2, merge em main ou promoção para produção. A revisão foi feita
pelo próprio Jarvis; não é revisão independente de integração/release.
