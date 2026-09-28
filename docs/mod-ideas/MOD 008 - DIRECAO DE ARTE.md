# MOD-008 — Direção de arte e produção do baralho

**Jarvis-HOOK.** Repo `/home/wanderson/.codex/worktrees/mod-008-arcana-fayth/ffx-hooks`, branch `codex/mod-008-arcana-fayth-20260927`.

## Direção escolhida

**Spira: Arcana of the Fayth** — pintura fantástica com ornamentos de templos, água, flores e pyreflies. Moldura de marfim envelhecido, azul oceano e ouro discreto. O baralho deve parecer uma coleção ritual de Spira, com a identidade tradicional do Tarot legível.

O usuário escolheu o **molde original `frame-v1.png`**: número no medalhão de cima, nome tradicional na faixa de baixo. A experiência transitória com faixa superior não é o molde aprovado. O nome do item no menu acrescenta a referência de FFX, separada do texto da ilustração.

## Convenções fechadas

- 22 Maiores, 56 Menores; Wands/Paus, Cups/Copas, Swords/Espadas, Pentacles/Ouros.
- Ordem Rider–Waite–Smith: Fool0, StrengthVIII, JusticeXI, WorldXXI. Menores I–X; Page/Knight/Queen/King sem número impresso por decisão do usuário.
- Número corresponde ao Tarot, não ao ID de dados 0–77.
- Referências principais: Tidus/Fool, Lulu/Magician, Yuna/High Priestess, Ifrit/Strength, Auron/Hermit. Outras cartas recorrem a locais, peregrinos, Fayth, Al Bhed e símbolos de Spira.
- As cartas são universais para equipamento: retratar Lulu não restringe a carta a Lulu.
- Textos das artes são em inglês nesta versão. Catálogo também conserva nomes em português e chaves de localização; o futuro pack deve permitir arte/texto por idioma sem perder identidade.

## Entregáveis visuais

| Asset | Papel |
|---|---|
| `assets/mod-008-arcana/concepts/frame-v1.png` | Molde original selecionado, ainda sem arte/texto |
| `assets/mod-008-arcana/concepts/cards/*.png` | 78 ilustrações individuais com nome/número corretos |
| `assets/mod-008-arcana/concepts/card-back-v1.png` | Verso próprio para cartas não reveladas |
| `assets/mod-008-arcana/concepts/tarot-icon-v1.png` | Ícone de carta para lista/menu |
| `research/mod_008_arcana/art-prompts.json` | Um prompt por carta, com texto exato e direção de cena |
| `research/mod_008_arcana/generation-receipts/` | Origem de cada geração, prompt efetivo e arquivo selecionado |
| `research/mod_008_arcana/assets-manifest.json` | Inventário verificável de arquivos produzidos e seus hashes/dimensões |

As contagens no manifesto são a autoridade para conclusão de geração. Esses PNGs são **conceitos/masters**, não texturas já registradas no engine. Não incluir drafts rejeitados no manifesto final como cartas prontas.

## Paleta e famílias

| Grupo | Acento na ilustração | Intenção |
|---|---|---|
| Maiores | Ouro/azul com luz celestial mais dramática | Efeitos fortes, figuras ou marcos importantes |
| Wands | Âmbar, vermelho e brasa | Vontade, elementos, magia ofensiva |
| Cups | Turquesa, pérola e prata | Cura, vínculos, recursos e proteção |
| Swords | Aço, índigo e violeta | Técnica, debuffs, reação e precisão |
| Pentacles | Jade, areia e ouro antigo | Corpo, recursos, trabalho e estabilidade |

A moldura permanece coesa. Não mudar todo o contorno por naipe; a cor é apoio, o nome/símbolo continuam identificáveis sem ela.

## Produção e revisão

Geração pelo **`image_gen.imagegen` embutido**, uma chamada por asset, usando o molde como referência. As primeiras cartas sem texto foram corrigidas pelo mesmo gerador, preservando sua ilustração. Nenhuma arte foi substituída por SVG, CSS ou desenho procedural.

Revisar cada carta: nome/numeral/posição, naipe, coerência com a cena, anatomia, objetos cortados e leitura pequena. Dez espadas devem ser dez espadas; numeração correta não compensa símbolo errado. Ilustrações conceituais podem exigir ajustes antes da conversão final; registrar divergências de forma explícita, sem chamar todo PNG de asset pronto para runtime.

Para o runtime, separar texto localizável de imagem quando o renderer permitir. O PNG conceitual com lettering serve como referência de composição; um export futuro pode montar a mesma aparência a partir de arte sem lettering e labels do idioma. Isso evita gerar um baralho inteiro de novo só para traduzir os nomes. Não usar OCR de arte como fonte de nomes/IDs.

## Ícone e preview

Ícone: silhueta vertical de uma carta com espiral/pyrefly, alto contraste e fundo transparente quando aceito pelo renderer. Validar a 32/48 px; o master quadrado pode ser grande, mas a versão runtime precisa ser reduzida e conferida. Não publicar um quadriculado pintado como se fosse canal alpha.

Preview: 2:3, sem esticar; nome legível e número visível. Uma carta selecionada, vizinhas em cache, fallback para imagem ausente. Evitar carregar os 78 masters na memória de uma vez. Normalizar o layout pelas escalas do menu, respeitando a transformação final da imagem.

## Gate de integração visual

Revisão de entrega em 27/09/2026: as 78 cartas foram inspecionadas em folhas de revisão; o Nove de Copas usa `044-cups-nine-v2.png`, corrigido pelo gerador para remover uma taça extra. O original foi preservado localmente e o recibo 044 registra a revisão. Ver [relatório de recuperação e limites](../research/MOD_008_DELIVERY_REVIEW_2026-09-27.md). As contagens e hashes selecionados estão no manifesto; a revisão visual não converte os conceitos em texturas nativas.

Conversão DDS/Phyre ou upload D3D11, atlas/UV, mipmaps, alpha e orçamento só ficam fechados depois de escolhido/provado o loader descrito na [pesquisa de injeção](../research/MOD_008_NATIVE_INJECTION_2026-09-27.md). Uma galeria local não comprova clipping, textura ou legibilidade dentro do FFX. RT2 precisa mostrar o Equip com carta, controle de foco e retorno ao menu original.
