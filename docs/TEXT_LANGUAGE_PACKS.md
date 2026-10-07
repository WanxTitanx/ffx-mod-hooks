# Separate game-language packs — API 4

FFX Hooks supplies a text/font resource loader, not a complete game translation.
The PT-BR game translation is not part of v0.6.0-beta.4 and is still being tested.
The nine-language Hooks interface is configured separately from game text/audio.

A compatible pack resides beside the game executable at
`_isolated/languages/pt-BR/manifest.json`, with the text/font/graphics paths named
by that manifest. The original native text locale must remain English (ID 1).
Choose `F8 > System > Text languages > Portuguese (Brazil)` only after a matching
pack is supplied, and restart FFX. Returning to native text also requires a
restart. Voices, native locale IDs and save headers are not repurposed.

## Authoring contract

- Schema/API pairs 1/1, 2/2, 3/3 and 4/4 are admitted by their resource contracts.
- API 4 permits bounded container-backed text growth, preserving source control
  arguments, speaker prefixes and page allocation. Choice/position scripts keep
  exact control/newline order. The limit remains 2,048 encoded bytes per edited
  script, u16 text offsets, 4,096 resources, 64 MiB per resource and a 256 MiB
  combined source/output admission working set. A width expansion still needs
  visual acceptance inside the game.
- The optional Western v2 font adds tilde vowels and ordinals in examined spare
  cells. All five metric/atlas resources are bound and admitted together; native
  glyphs and allocation sizes remain intact.
- UI texture replacement uses the closed source/output hash catalogue in
  `TextLanguageGraphicsCatalog.h`. No arbitrary image is admitted by an extension
  or a self-declared hash. The public source contains metadata, not these pixels.
- Mixed lockit, Flash and alternate inpc resources remain extraction/reference
  only. No complete translation coverage is advertised.
- Source identity and the exact supported executable/profile are verified before
  publication at the native font boundary. Unsupported, missing or changed
  inputs preserve native routing with a diagnostic. Published font/text resources
  stay paired until process restart; hot replacement is unsupported.

See the [producer guide](../tools/text_languages/README.md),
[API 4 schema](../contracts/text-languages.v4.schema.json), and
[authoring recipe schema](../contracts/text-languages.recipe.schema.json).
Private executable/game/font fixtures and the PT-BR translation are excluded
from source archives, CI uploads and public downloads.

## Português (Brasil)

O Hooks carrega pacotes de texto/fonte separados. A tradução PT-BR do jogo não
acompanha esta release e ainda está em testes. A interface do próprio Hooks em
nove idiomas é uma configuração diferente. Sem pacote compatível, mantenha o
texto nativo. Uma futura instalação do pacote exige texto original em inglês,
seleção em `F8 > System > Text languages` e reinício completo do jogo.

A admissão verifica arquivos, fonte e executável. Ela não comprova o encaixe de
todas as frases na tela. Áudio e identidade nativa de saves são preservados.
