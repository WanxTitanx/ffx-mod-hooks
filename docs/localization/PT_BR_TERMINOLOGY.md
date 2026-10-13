# Jarvis-HOOK: Brazilian Portuguese terminology

Reviewed on 2026-10-13. These terms describe FFX mechanics. A similar name in
FFVII does not imply that FFX shares its timing, targeting or battle rules.

The official [Rebirth equipment article](https://blog.br.playstation.com/2024/03/21/final-fantasy-vii-rebirth-a-evolucao-dos-poligonos-de-1997-ao-remake/)
uses **espaços** for equipment slots. General equipment vocabulary also follows
the [PlayStation Rebirth guide](https://www.playstation.com/pt-br/games/final-fantasy-vii-rebirth/everything-you-need-to-know-about-final-fantasy-vii-rebirth/).
Specific effect names below are editorial choices informed by the existing
FFX translation, not a claim that every spell name is an official FFVII term.

| English | PT-BR | Context |
|---|---|---|
| Workshop / Equipment Workshop | Oficina / Oficina de equipamentos | Native game menu |
| Workshop settings | Configurações da Oficina | Activation, recipes and development preferences |
| Customize | Personalizar | Original FFX command |
| Weapon / Armor | Arma / Armadura | Equipment type |
| Auto-ability | Habilidade automática | Equipment effect, not a command |
| Slot | Espaço | Native and additional ability slots |
| Refine / Refinement | Refinar / Refinamento | Existing equipment ranks |
| Reforge | Reforjar | Change equipment character/type |
| Fuse equipment | Fundir equipamentos | Consumes the donor only after confirmation |
| Evolve an ability | Aprimorar habilidade | Existing recipe upgrade |
| Expand slots | Ampliar espaços | Native slots; distinguish the fifth slot |
| Protect | Proteção | Physical damage reduction |
| Shell | Proteção mágica | Magic damage reduction |
| Haste / Slow | Aceleração / Lentidão | CTB speed |
| Regen / Reflect | Regeneração / Reflexo | Preserve each native mechanic |
| Reflex | Reflexos | Evasion buff; distinct from Reflect |
| Cheer / Jinx | Animar / Má sorte | Existing native command terminology |
| Follow Up | Ataque complementar | Single-target allied action follow-up |
| P-Trade / M-Trade | Troca física / Troca mágica | Preserve the opposing damage tradeoff |
| Elude | Evasão defensiva | Evasion while defending |
| Darkness (status) | Cegueira | Never substitute the dark element |
| Dark (element) | Trevas | Distinct from Cegueira |
| Holy | Sagrado | Element |
| Aero / Wind | Vento | Element; Aero may remain a spell proper name |
| Element Ward | Proteção contra [elemento] | Elemental resistance |
| Status Ward | Resistência a [status] | Status resistance |
| Proof | Imunidade a [elemento/status] | Preserve exceptional bypass mechanics |
| Eater | Absorção de [elemento] | Damage converted to HP |
| Strike / Touch | Golpe / Toque | Distinct effects and proc rates |
| Auto-[effect] | [Efeito] automático/automática | Automatic status |
| SOS [effect] | [Efeito] de emergência | Critical-HP trigger |
| Break Damage Limit | Superar limite de dano | Equipment cap |
| Break HP Limit / Break MP Limit | Superar limite de HP / Superar limite de MP | Vanilla meanings only |
| Break Limits | Superar limites de HP/MP | Only a verified combined effect |
| Power Break | Enfraquecer força | Command; state: Força reduzida |
| Magic Break | Enfraquecer magia | Command; state: Magia reduzida |
| Armor Break | Romper defesa | Command; state: Defesa rompida |
| Mental Break | Romper defesa mágica | Command; state: Defesa mágica rompida |
| Full Break | Debilitação total | Preserve all four component effects |
| Overdrive | Overdrive | Do not rename to FFVII Limit Break |
| HP / MP / AP / Gil | HP / MP / AP / Gil | Preserve established abbreviations |

## Application rules

- Resolve a modified ability by its current admitted record. Do not infer its
  effect from an old vanilla name or ID. In particular, combined HP/MP effects,
  disabled One MP Cost and disabled Break MP Limit retain their actual meaning.
- A translated display name must not break a runtime's identity guard. Admit
  reviewed exact glyph aliases together with the existing payload/range checks.
- Translate format templates before inserting values. Preserve placeholder
  order, numerical values, controls, proper names and user-supplied identities.
  Never truncate a UTF-8 label using a byte-based printf precision.
- Keep the paired native font and exact source/output fingerprints. A translation
  must not overwrite a newer combat payload, silently create a table row or
  discard a source identity mismatch.
- Power Sphere and Strength Sphere currently collide in the inherited pack.
  Do not rename only the Workshop material: any future distinction must cover
  the item bank, recipes, command help and Sphere Grid together.
- PT-BR is the reviewed target of this pass. New non-PT catalog entries retain
  English fallback until separately translated; existing other-locale entries
  are preserved.

## Remaining editorial scope

The broader equipment/item terminology audit remains open. The inherited Power
Sphere / Strength Sphere collision requires a coordinated item/recipe/Grid edit.
ESC/Flash screens use separate consumers and are not covered by the native
kernel pack. This document is not a claim that every screen is fully translated.
