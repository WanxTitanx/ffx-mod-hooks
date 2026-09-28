#!/usr/bin/env python3
"""Build the proposed Tarot design catalog and individual image prompts. No game I/O."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
ART_REVISIONS = {"cups.nine": "v2"}

# name | Portuguese name | Spira scene | proposed mechanics | inspiration | phase
MAJORS = """The Fool|O Louco|Tidus at the edge of a sunlit Besaid cliff, small white seabird, endless sea, beginning a pilgrimage|First eligible action has 25% less CTB recovery; Evasion +20; Sensor|Sprint Shoes|arrival
The Magician|O Mago|Lulu weaving four luminous elemental spheres above a ceremonial altar in a ruined Spiran temple|Magic +20%; Half MP Cost for Black Magic|Tarot Card, Gold Hairpin|midgame
The High Priestess|A Sacerdotisa|Yuna between two moonlit temple columns, luminous fayth water at her feet|White Magic healing +25%; Half MP Cost for White Magic|White Lore|midgame
The Empress|A Imperatriz|a serene female fayth presiding over the abundant Moonflow, white flowers and pyreflies|Max HP +40%; Auto-Regen|Recovery Bracer|midgame
The Emperor|O Imperador|a solemn armored guardian on a stone throne before Bevelle, two ancient banners|Auto-Protect; Auto-Shell; Defense and Magic Defense +15%|Defense Bracer|late
The Hierophant|O Hierofante|an elderly Spiran temple teacher blessing two pilgrims before a glowing fayth seal|Darkness, Silence, Sleep, Poison and Confusion proof; begin battle with 3 Focus stacks|Ribbon|late
The Lovers|Os Enamorados|Tidus and Yuna reaching toward each other above the Macalania spring, two intertwined pyrefly streams|Direct White Magic healing to another ally also heals wearer for 25% of actual HP restored, limited to 10% wearer max HP per action; White Magic MP cost -25%|White Tome|midgame
The Chariot|O Carro|a luminous ceremonial chariot drawn by two opposing aeon-like beasts across the Calm Lands|Auto-Haste; First Strike|Speed Bracer|late
Strength|A Forca|a gentle summoner calming a mighty horned fiery beast without chains, crimson dusk|Strength +25%; Piercing|Champion Belt|late
The Hermit|O Eremita|Auron carrying a blue pyrefly lantern through the snowy paths of Mount Gagazet|Max MP +60%; restore 5% max MP at the start of each genuine wearer turn|Wizard Bracelet|late
Wheel of Fortune|A Roda da Fortuna|an ancient Al Bhed wheel of destiny turning above Bikanel dunes, ten engraved spokes|Luck +15; Double Drop, using the shared highest-only drop multiplier|Key to Success|late
Justice|A Justica|a robed Spiran guardian holding balanced scales and a vertical crystal sword at Bevelle|Counterattack and Magic Counter; Defense +15%; at most one retaliation per triggering action|Black Belt|late
The Hanged Man|O Enforcado|a willing pilgrim suspended upside down in a luminous underwater fayth vision, calm expression|Positive HP damage received x0.80; wearer CTB recovery x1.25; Defend restores 10% max MP once per action|Adamantite|late
Death|A Morte|a silent armored ferryman on the Farplane shore, falling petals becoming pyreflies, a distant sunrise|Deathstrike at base 100% subject to native immunity; defeating a hostile target restores 20% wearer max HP once per action|Mortal Shock|late
Temperance|A Temperanca|Yuna pouring a stream of luminous water between two cups at the Moonflow shore|Half MP Cost; wearer recovery items heal 25% more HP or MP|Gold Hairpin|late
The Devil|O Diabo|a towering sinister fayth apparition above two pilgrims bound by loose golden chains in a ruined temple|Positive HP damage dealt x1.25 and received x1.25; neither modifier affects fixed fractional damage or healing|Bloodlust|challenge
The Tower|A Torre|a tall Bevelle temple spire struck by lightning, two cloaked silhouettes escaping, fragments and pyreflies|Ordinary attack adds Armor Break and Mental Break at base 50%, 3 target turns, respecting immunity; Strength and Magic +10%|Power Gloves|challenge
The Star|A Estrela|a young Spiran pilgrim kneeling beside a starlit lake pouring two luminous vessels, eight stars|Auto-Regen; restore 5% max MP at each genuine wearer turn; Magic Defense +20%|Heady Perfume|late
The Moon|A Lua|a great moon above Macalania, two guardian beasts on either side of a luminous winding path|Auto-Reflect; Evasion +30; Sleep and Confusion proof|Star Bracer|late
The Sun|O Sol|a joyful young pilgrim riding a chocobo through golden Besaid flowers beneath an enormous sun|Add Holy to ordinary attacks; Holy HP damage +25%; Magic +20%|Force of Nature|challenge
Judgement|O Julgamento|Yuna sending luminous souls toward the Farplane while a celestial horn-shaped fayth shines above|Once per battle, survive the first otherwise lethal eligible HP hit at 1 HP and recover 25% max HP; Overdrive positive HP damage +20%|Angel Earrings|challenge
The World|O Mundo|a circular wreath of Spiran flora around a luminous fayth dancer, four elemental aeon silhouettes at corners|Break HP Limit to 99999 and Break Damage Limit to the shared normal BDL cap; Max MP +40%; never grants Aeon-exclusive limits|Enterprise, Invincible|completion"""

# rank | scene | proposed mechanics | FFX-2 inspiration | phase
MINORS = {
"wands": ("Paus", "amber, crimson and ember gold", """Ace|one ornate summoner staff emerging from a pyrefly cloud above Kilika|Firestrike; Magic +5%|Fiery Gleam|arrival
Two|two crossed summoner staves at a snowy Macalania gate|Icestrike; Magic +5%|Icy Gleam|early
Three|three staves on a stormlit Djose balcony|Lightningstrike; Magic +5%|Lightning Gleam|early
Four|four flower-wrapped staves forming a canopy at Besaid beach|Waterstrike; Magic +5%|Watery Gleam|early
Five|five trainee temple acolytes sparring with staves in Kilika|Darktouch: base 50%, 3 target turns|Blind Shock|early
Six|a victorious pilgrim holding one staff while five others line a temple road|Silencetouch: base 50%, 3 target turns|Mute Shock|early
Seven|a pilgrim holding one staff on a ledge above six challengers|Sleeptouch: base 50%, 3 target turns|Dream Shock|early
Eight|eight luminous staves flying diagonally above the Thunder Plains|Slowtouch: base 50%, 3 target turns|Lag Shock|midgame
Nine|an exhausted guardian beside nine planted staves at a ruined gate|Poisontouch: base 50%, 3 target turns|Venom Shock|midgame
Ten|a pilgrim carrying ten heavy ceremonial staves toward Zanarkand|Strength +10%; Piercing|Power Wrist|midgame
Page|a young Spiran apprentice studying a single luminous staff in Bikanel|Magic +10%|Amulet|early
Knight|a mounted chocobo guardian holding a flaming staff amid dunes|Fire Ward and Ice Ward|Red Ring, White Ring|midgame
Queen|a Spiran queen holding a staff and a white Besaid flower, seated with a small black cat|Max MP +20%; Magic +5%|Silver Bracer|midgame
King|a Spiran king on a volcanic stone throne holding a luminous staff, salamander motif|Fire and Ice positive HP damage +10%; once per hit even if both elements match|Soul of Thamasa|late"""),
"cups": ("Copas", "turquoise, pearl and silver", """Ace|one silver cup overflowing into a Besaid spring, white lotus blossoms|Max HP +10%|Iron Bangle|arrival
Two|two pilgrims exchanging cups beside the Moonflow, entwined pyrefly streams|Max MP +10%|Silver Bracer|early
Three|three joyful Spiran dancers raising cups in a village celebration|Poisonproof|Star Pendant|early
Four|a contemplative pilgrim beneath a tree, three cups on the ground and a fourth offered by light|Silenceproof|White Cape|early
Five|a cloaked pilgrim before three spilled cups and two standing cups beside a bridge|Sleepproof|Twist Headband|early
Six|two young temple apprentices exchanging a flower-filled cup among six vessels|Darkproof|Silver Glasses|early
Seven|seven cups in a luminous Farplane mist, each holding a different dreamlike object|Stoneproof|Gold Anklet|midgame
Eight|a pilgrim leaving eight carefully stacked cups under a crescent moon at Gagazet|Confuseproof|Black Choker|midgame
Nine|a contented Spiran host seated before nine ceremonial cups|SOS Regen while native critical-HP condition holds|Regen Bangle|midgame
Ten|a family beneath a rainbow of ten cups over Besaid village|Max HP +20%; direct White Magic HP healing +10%|Titanium Bangle|midgame
Page|a young temple healer looking at a fish emerging from a silver cup|Direct White Magic HP healing +10%|White Lore|early
Knight|a chocobo rider carrying a silver cup across a shallow Moonflow crossing|MP cost -25%|Gold Hairpin|midgame
Queen|a meditative Spiran healer on a shell-carved throne beside the ocean holding a covered cup|Auto-Regen|Recovery Bracer|late
King|a serene Spiran ruler on a throne amid ocean waves with a cup and a short scepter|Auto-Protect|Shining Bracer|late"""),
"swords": ("Espadas", "steel blue, indigo and violet", """Ace|one crystal sword piercing a pyrefly halo over Mount Gagazet|Strength +10%|Wristband|arrival
Two|a blindfolded guardian holding two crossed swords at a moonlit shoreline|Defense +10%|Mythril Gloves|early
Three|three swords through a luminous red crystal heart suspended in rain, symbolic not gore|Poisonstrike: base 100%, 3 target turns|Venom Shock|midgame
Four|a stone guardian resting under three mounted swords, a fourth on the altar base|Sleepstrike: base 100%, 3 target turns|Dream Shock|midgame
Five|a solemn Al Bhed scavenger gathering five swords after an abandoned contest|Silencestrike: base 100%, 3 target turns|Mute Shock|midgame
Six|a small Moonflow ferry carrying six upright swords toward a misty shore|Darkstrike: base 100%, 3 target turns|Blind Shock|midgame
Seven|a nimble Al Bhed thief carrying five swords away from two planted in a desert camp|Master Thief; Accuracy +5|Amulet|midgame
Eight|a loosely bound pilgrim standing within eight upright swords in shallow water|Slowstrike: base 100%, 3 target turns|Lag Shock|late
Nine|a worried pilgrim sitting awake below nine swords arranged on a temple wall|Deathtouch: base 25%; native immunity still applies|Mortal Shock|late
Ten|ten swords planted around a fallen guardian silhouette while dawn rises, no blood|Counterattack|Black Belt|late
Page|an alert young guardian holding a sword against a windy Calm Lands sky|Accuracy +10|Gauntlets|early
Knight|an armored chocobo rider charging through wind with a raised sword|Evade and Counter; never grants guaranteed evasion beyond native behavior|Sprint Shoes|late
Queen|a Spiran queen on a high stone throne holding a vertical sword, one hand open|Magic Counter|Star Bracer|late
King|a stern Spiran judge on a throne with an upright sword and butterfly engravings|Random critical chance +10 percentage points, clamped by critical policy; Luck +5|Amulet|late"""),
"pentacles": ("Ouros", "jade, sand and antique gold", """Ace|one golden fayth-inscribed disk held by a luminous hand over a Besaid garden|Gil rewards +25%; party highest-only multiplier|Key to Success|arrival
Two|an Al Bhed traveler juggling two golden disks joined by a pyrefly infinity ribbon|AP gained by wearer +25%; No AP remains authoritative|AP Egg|early
Three|three Spiran artisans collaborating on a temple wall bearing three golden disks|Wearer recovery items restore 25% more HP or MP|Key to Success|early
Four|a merchant seated at a Luca gate protecting four golden disks|Defense +15%|Diamond Gloves|midgame
Five|two weary travelers outside a warmly lit temple window containing five disks|Magic Defense +15%|Mystery Veil|midgame
Six|a kindly Spiran merchant distributing coins under balanced scales and six golden disks|Defeating a hostile target restores 5% wearer max MP once per action|Wizard Bracelet|midgame
Seven|a patient farmer studying seven golden disks growing in a Besaid grove|Lightning Ward and Water Ward|Yellow Ring, Blue Ring|midgame
Eight|an Al Bhed artisan engraving the eighth of eight golden disks in a workshop|Random encounter pressure x0.5 using the shared encounter policy; scripted battles unchanged|Charm Bangle|midgame
Nine|an independent Spiran noble in a lush garden with nine disks and a tame seabird|One opening Overdrive grant of 10 percentage points per genuine battle; no re-equip or retry duplication|Sprint Shoes|late
Ten|a multigenerational Spiran family under an arch decorated with ten disks|Max HP +15%; Max MP +15%|Iron Bangle, Silver Bracer|midgame
Page|a young Al Bhed scholar examining one golden disk above green fields|Auto-Potion using native consumable and trigger rules|Cat's Bell|early
Knight|a steady armored chocobo rider carrying a single golden disk across farmland|Auto-Shell|Moon Bracer|late
Queen|a Spiran matron holding a golden disk on a garden throne with a small desert hare|Auto-Phoenix using native consumable and trigger rules|Angel Earrings|late
King|a wealthy Spiran ruler on a carved throne surrounded by vines and golden disks|Darkness, Silence, Sleep and Poison proof|Ribbon|late""")}

PHASES = {
    "arrival": "Starter or early pilgrimage milestone; grant once per save lineage.",
    "early": "Early exploration or bestiary milestone; later recovery route remains available.",
    "midgame": "Temple, crafting or optional combat milestone; never permanently missable.",
    "late": "Post-airship sidequest or Monster Arena milestone with a guaranteed reward.",
    "challenge": "Named optional challenge, guaranteed once; no random duplicate farming.",
    "completion": "Deck/pilgrimage completion milestone with an explicit preview of requirements."
}

ART_BASE = """Use case: stylized-concept. Asset: ONE illustrated Tarot card for the original Final Fantasy X inspired deck Spira: Arcana of the Fayth. Portrait 2:3, straight-on full card filling the canvas. The supplied original frame-v1 is a style and geometry reference: preserve its refined ivory, ocean-blue and restrained gold ornamental border, aqua pyrefly details, TOP circular numeral cartouche and BOTTOM ivory name plaque. The numeral is ABOVE the illustration; the traditional card name is BELOW it, clearly legible and spelled exactly. Create a rich hand-painted Japanese fantasy game illustration inside the frame. No extra text, logo or watermark. Do not show multiple cards or a tabletop. Luminous atmospheric Spira scenery, delicate painterly textures, strong readable silhouette, elegant symbolism. Art is original interpretation, not a screenshot or copied official card. """

MAJOR_REFERENCES = ["Tidus", "Lulu", "Yuna", "Moonflow", "Bevelle", "Yevon", "Macalania Spring", "Calm Lands", "Ifrit", "Auron", "Al Bhed", "Bevelle Guardians", "Fayth", "Farplane", "Moonflow", "Anima", "Bevelle Temple", "Macalania Lake", "Macalania Woods", "Chocobo", "Sending", "Spira"]
MINOR_REFERENCES = {
    "wands": ["Kilika", "Macalania", "Djose", "Besaid", "Kilika Temple", "Pilgrimage", "Gagazet", "Thunder Plains", "Zanarkand Ruins", "Zanarkand", "Bikanel", "Chocobo", "Besaid Flora", "Ifrit"],
    "cups": ["Besaid Spring", "Moonflow", "Luca", "Kilika Woods", "Moonflow Crossing", "Besaid Temple", "Farplane", "Gagazet", "Luca Tavern", "Besaid Village", "Temple Acolyte", "Shoopuf Crossing", "Besaid Coast", "Spiran Sea"],
    "swords": ["Gagazet", "Besaid Coast", "Macalania Crystal", "Fayth Chamber", "Al Bhed", "Moonflow Ferry", "Rikku", "Calm Lands", "Zanarkand Pilgrim", "Fallen Guardians", "Calm Lands", "Chocobo Knights", "Spiran Guardians", "Bevelle"],
    "pentacles": ["Besaid", "Al Bhed", "Djose Temple", "Rin", "Gagazet Pilgrims", "Oaka", "Besaid Grove", "Rikku", "Luca", "Besaid Village", "Al Bhed Scholar", "Chocobo", "Bikanel", "Rin Travel Agency"]
}
PT_RANKS = ["As", "Dois", "Tres", "Quatro", "Cinco", "Seis", "Sete", "Oito", "Nove", "Dez", "Pajem", "Cavaleiro", "Rainha", "Rei"]
ROMANS = ["0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX", "XXI"]

def build():
    cards = []
    for index, line in enumerate(MAJORS.splitlines()):
        name, pt, scene, mechanics, inspiration, phase = line.split("|")
        cards.append({"id":index, "key":"major."+name.lower().replace(" ","_"), "arcana":"major", "rank":index,
                      "name_en":name, "name_pt":pt, "scene":scene, "mechanics_proposed":mechanics,
                      "ffx2_inspiration":inspiration.split(", "), "phase":phase, "suit":None,
                      "visual_palette":"ocean blue, pearl, antique gold; dramatic celestial light"})
    for suit, (pt, palette, rows) in MINORS.items():
        for rank, line in enumerate(rows.splitlines(), start=1):
            title, scene, mechanics, inspiration, phase = line.split("|")
            cards.append({"id":len(cards), "key":suit+"."+title.lower(), "arcana":"minor", "rank":rank,
                          "name_en":f"{title} of {suit.title()}", "name_pt_suit":pt, "scene":scene,
                          "mechanics_proposed":mechanics, "ffx2_inspiration":inspiration.split(", "), "phase":phase,
                          "suit":suit, "visual_palette":palette})
    for c in cards:
        c["ffx_reference"] = MAJOR_REFERENCES[c["rank"]] if c["arcana"] == "major" else MINOR_REFERENCES[c["suit"]][c["rank"]-1]
        c["printed_number"] = ROMANS[c["rank"]] if c["arcana"] == "major" or c["rank"] <= 10 else None
        c["printed_name"] = c["name_en"].upper()
        c["item_name_en"] = " - ".join(x for x in [c["printed_number"], c["name_en"], c["ffx_reference"]] if x)
        if c["arcana"] == "minor": c["name_pt"] = PT_RANKS[c["rank"]-1]+" de "+c["name_pt_suit"]
        c.update(asset=f"assets/mod-008-arcana/concepts/cards/{c['id']:03d}-{c['key'].replace('.','-')}.png",
                 enabled_by_default=False, character_whitelist=None, copies_per_save=1,
                 balance_status="PROPOSED_UNTESTED", effect_implementation="NOT_IMPLEMENTED",
                 acquisition_proposed=PHASES[c['phase']],
                 rank_policy="Major Arcana have the strongest default individual effects; minors specialize or combine in mode B.")
        if c["key"] in ART_REVISIONS:
            c["asset"] = c["asset"].removesuffix(".png") + "-" + ART_REVISIONS[c["key"]] + ".png"
        number = 'Top medallion text (verbatim): "'+c["printed_number"]+'". ' if c["printed_number"] is not None else 'This is a traditional court card: NO numeral anywhere. The top medallion contains only a small ornamental suit symbol. '
        c["prompt"] = ART_BASE+number+'Bottom name plaque text (verbatim): "'+c["printed_name"]+'". Card subject: '+c["name_en"]+". Scene: "+c["scene"]+". Accent palette: "+c["visual_palette"]+". Keep traditional Tarot symbols legible and the border consistent with the reference."
    balance = json.loads((OUT/"effects.v1.json").read_text())
    assert [row["id"] for row in balance["cards"]] == list(range(78))
    for c, row in zip(cards, balance["cards"]):
        if "description" in row:
            c["mechanics_proposed"] = row["description"]
            c["ffx2_inspiration"] = row["inspiration"]
            c["balance_revision"] = balance["balance_revision"]
            c["effect_implementation"] = "TYPED_RUNTIME_HANDLERS_PLAYTEST_PENDING"
    doc = {"schema":"jarvis.arcana.design.v1", "runtime_compatible":False,
           "mod":"MOD-008", "name":"Spira: Arcana of the Fayth", "default_mode":"A",
           "tarot_convention":"Rider-Waite-Smith; Fool 0, Strength VIII, Justice XI; minor pips I-X; court titles without printed numbers, as explicitly selected by the user.",
           "modes":{"A":{"slots":2,"major_weight":1,"minor_weight":1,"budget":2},
                    "B":{"slots":3,"major_weight":2,"minor_weight":1,"budget":4}},
           "cards":cards}
    (OUT/"cards.proposed.json").write_text(json.dumps(doc,ensure_ascii=False,indent=2)+"\n")
    prompts=[{"id":c["id"],"key":c["key"],"asset":c["asset"],"prompt":c["prompt"]} for c in cards]
    (OUT/"art-prompts.json").write_text(json.dumps(prompts,ensure_ascii=False,indent=2)+"\n")
    lines=["# MOD-008 — Catálogo das 78 cartas", "", f"**Jarvis-HOOK · balanceamento v{balance['balance_revision']}, sujeito a playtest.**", "",
           "As definições tipadas estão em `research/mod_008_arcana/effects.v1.json`; IDs, nomes, artes e regras de capacidade permanecem estáveis.", "",
           "Os números são ponto de partida editável. Inspiração identifica uma família de efeito do FFX-2; não significa equivalência exata nem que o acessório original concede todos os efeitos desta carta.", "",
           "Maiores: efeitos individuais mais fortes por padrão. Menores: especialização, progressão e combinações do modo B. Nenhuma carta é exclusiva de personagem. IDs pertencem ao registro Arcana; nunca são IDs de `a_ability.bin`.", "",
           "Nomes de item: `numero - nome tradicional - referencia de FFX`. Nas 16 figuras, o numero e omitido por escolha explicita do usuario. O ID abaixo nao e a numeracao do Tarot.", "",
           "| ID / chave | Nome completo do item | Efeito proposto (contrato em inglês) | Inspiração FFX-2 | Aquisição |", "|---|---|---|---|---|"]
    for c in cards:
        lines.append(f"| {c['id']:02d} / `{c['key']}` | **{c['item_name_en']}** | {c['mechanics_proposed']} | {', '.join(c['ffx2_inspiration'])} | {c['phase']} |")
    lines += ["", "## Regras comuns", "", "- Percentuais, imunidades, CTB, uso de itens e anti-recursão seguem o documento principal. Strike/Touch são chances-base propostas, nunca promessa de ignorar a resistência do alvo.",
              "- As fases acima preservam a direção de progressão; os requisitos concretos estão em `research/mod_008_arcana/acquisition-v1.md`.",
              "- `runtime_compatible: false` identifica o catálogo de design. O runtime usa o cabeçalho compilado e handlers tipados, sem interpretar prosa.",
              "- Todas as ilustrações são conceitos. O manifesto visual distingue arquivos gerados de assets convertidos, carregados e vistos no jogo.", ""]
    (ROOT/"docs/mod-ideas/MOD 008 - CATALOGO DAS 78 CARTAS.md").write_text("\n".join(lines))
    print(f"Wrote {len(cards)} card definitions and separate prompts.")

if __name__ == "__main__":
    build()
