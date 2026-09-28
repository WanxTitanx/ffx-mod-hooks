# MOD-008 — Catálogo das 78 cartas

**Jarvis-HOOK · balanceamento v4, sujeito a playtest.**

As definições tipadas estão em `research/mod_008_arcana/effects.v1.json`; IDs, nomes, artes e regras de capacidade permanecem estáveis.

Os números são ponto de partida editável. Inspiração identifica uma família de efeito do FFX-2; não significa equivalência exata nem que o acessório original concede todos os efeitos desta carta.

Maiores: efeitos individuais mais fortes por padrão. Menores: especialização, progressão e combinações do modo B. Nenhuma carta é exclusiva de personagem. IDs pertencem ao registro Arcana; nunca são IDs de `a_ability.bin`.

Nomes de item: `numero - nome tradicional - referencia de FFX`. Nas 16 figuras, o numero e omitido por escolha explicita do usuario. O ID abaixo nao e a numeracao do Tarot.

| ID / chave | Nome completo do item | Efeito proposto (contrato em inglês) | Inspiração FFX-2 | Aquisição |
|---|---|---|---|---|
| 00 / `major.the_fool` | **0 - The Fool - Tidus** | First Strike; first CTB recovery -35%; Evasion +20; Sensor | Sprint Shoes | arrival |
| 01 / `major.the_magician` | **I - The Magician - Lulu** | Magic +30%; Half MP for Black Magic; Fire/Ice damage +10%; Fire/Ice Wards | Tarot Card, Gold Hairpin | midgame |
| 02 / `major.the_high_priestess` | **II - The High Priestess - Yuna** | White Magic healing +40%; Half MP for White Magic; Silenceproof | White Lore | midgame |
| 03 / `major.the_empress` | **III - The Empress - Moonflow** | Max HP +40%; Auto-Regen | Recovery Bracer | midgame |
| 04 / `major.the_emperor` | **IV - The Emperor - Bevelle** | Auto-Protect; Auto-Shell; Defense/Magic Defense +20% | Defense Bracer | late |
| 05 / `major.the_hierophant` | **V - The Hierophant - Yevon** | Darkness/Silence/Sleep/Poison/Confusion/Death proof; start with Focus x5; Defend restores 3% MP | Ribbon | late |
| 06 / `major.the_lovers` | **VI - The Lovers - Macalania Spring** | White Magic on another ally shares 25% actual healing, capped at 10% wearer HP per action; White Magic MP cost -25%; healing +15% | White Tome | midgame |
| 07 / `major.the_chariot` | **VII - The Chariot - Calm Lands** | Auto-Haste; First Strike; CTB recovery -10%; Strength +15% | Speed Bracer | late |
| 08 / `major.strength` | **VIII - Strength - Ifrit** | Strength +30%; Piercing; Critical chance +10% | Champion Belt | late |
| 09 / `major.the_hermit` | **IX - The Hermit - Auron** | Max MP +60%; restore 1% MP each turn; Auto-Shell | Wizard Bracelet | late |
| 10 / `major.wheel_of_fortune` | **X - Wheel of Fortune - Al Bhed** | Double Drops; Luck +20; AP +25%; Gil +25% (drop/gil use highest rate) | Key to Success | late |
| 11 / `major.justice` | **XI - Justice - Bevelle Guardians** | Counterattack; Magic Counter; Defense +20%; Accuracy +20 | Black Belt | late |
| 12 / `major.the_hanged_man` | **XII - The Hanged Man - Fayth** | Incoming HP damage -20%; CTB recovery +25%; Defend restores 3% MP; Auto-Protect | Adamantite | late |
| 13 / `major.death` | **XIII - Death - Farplane** | Deathstrike 100%; Deathproof; HP damage +20% against Death-immune targets; a kill restores 20% HP and 10% MP once per action | Mortal Shock, Angel Earrings | late |
| 14 / `major.temperance` | **XIV - Temperance - Moonflow** | Half MP; recovery items +50%; White Magic healing +15%; Poisonproof | Gold Hairpin | late |
| 15 / `major.the_devil` | **XV - The Devil - Anima** | Outgoing HP damage +40%; incoming HP damage +25%; kills restore 10% HP once per action | Bloodlust | challenge |
| 16 / `major.the_tower` | **XVI - The Tower - Bevelle Temple** | Attack: Armor/Mental Break 50% for 3 turns; Strength/Magic +15%; Piercing | Power Gloves | challenge |
| 17 / `major.the_star` | **XVII - The Star - Macalania Lake** | Auto-Regen; restore 1% MP each turn; Magic Defense +20%; White Magic healing +20% | Heady Perfume | late |
| 18 / `major.the_moon` | **XVIII - The Moon - Macalania Woods** | Auto-Reflect; Evade and Counter; Evasion +30; Sleep/Confusion proof; Sleepstrike 100%; Confusetouch 50%; incoming HP damage -10% | Star Bracer, Black Choker, Moon Bracer | late |
| 19 / `major.the_sun` | **XIX - The Sun - Chocobo** | Holystrike; Holy damage +25%; Holy Ward; Magic +20%; Auto-Protect | Force of Nature | challenge |
| 20 / `major.judgement` | **XX - Judgement - Sending** | Survive one lethal HP hit per battle, then recover 25% HP; Overdrive damage +30%; Deathproof | Angel Earrings | challenge |
| 21 / `major.the_world` | **XXI - The World - Spira** | Break HP/Damage Limit; HP +50%; MP +40%; Overdrive HP damage +50%; CTB recovery -15%; incoming HP damage -10%; Auto-Regen | Enterprise, Invincible | completion |
| 22 / `wands.ace` | **I - Ace of Wands - Kilika** | Firestrike; Fire Ward; Strength +10%; Poisontouch 25% | Fiery Gleam | arrival |
| 23 / `wands.two` | **II - Two of Wands - Macalania** | Icestrike; Ice Ward; Magic +10%; Slowtouch 25% | Icy Gleam | early |
| 24 / `wands.three` | **III - Three of Wands - Djose** | Lightningstrike; Lightning Ward; Accuracy +15; first CTB recovery -10% | Lightning Gleam | early |
| 25 / `wands.four` | **IV - Four of Wands - Besaid** | Waterstrike; Water Ward; HP +15%; Defend restores 1% MP | Watery Gleam | early |
| 26 / `wands.five` | **V - Five of Wands - Kilika Temple** | Darktouch 50%; Darkproof; Magic +10% | Blind Shock | early |
| 27 / `wands.six` | **VI - Six of Wands - Pilgrimage** | Silencetouch 50%; Silenceproof; Magic +10% | Mute Shock | early |
| 28 / `wands.seven` | **VII - Seven of Wands - Gagazet** | Sleeptouch 50%; Sleepproof; first CTB recovery -10% | Dream Shock | early |
| 29 / `wands.eight` | **VIII - Eight of Wands - Thunder Plains** | Slowtouch 50%; Slowproof; MP cost -10% | Lag Shock | midgame |
| 30 / `wands.nine` | **IX - Nine of Wands - Zanarkand Ruins** | Poisontouch 50% for 3 turns; Poisonproof; Magic +15% | Venom Shock | midgame |
| 31 / `wands.ten` | **X - Ten of Wands - Zanarkand** | Strength +15%; Piercing; Critical chance +5% | Power Wrist | midgame |
| 32 / `wands.page` | **Page of Wands - Bikanel** | Magic +15%; Half MP for Black Magic | Amulet | early |
| 33 / `wands.knight` | **Knight of Wands - Chocobo** | Fire/Ice damage +10%; Fire/Ice Wards; Defense +15% | Red Ring, White Ring | midgame |
| 34 / `wands.queen` | **Queen of Wands - Besaid Flora** | MP +25%; Magic +10%; restore 1% MP each turn | Silver Bracer | midgame |
| 35 / `wands.king` | **King of Wands - Ifrit** | Fire/Ice damage +20%; Fire/Ice Wards; Magic +15%; Half MP for Black Magic | Soul of Thamasa | late |
| 36 / `cups.ace` | **I - Ace of Cups - Besaid Spring** | Max HP +20% | Iron Bangle | arrival |
| 37 / `cups.two` | **II - Two of Cups - Moonflow** | MP +20%; Defend restores 1% MP | Silver Bracer | early |
| 38 / `cups.three` | **III - Three of Cups - Luca** | Poisonproof; Poisontouch 50% for 3 turns; SOS Regen | Star Pendant | early |
| 39 / `cups.four` | **IV - Four of Cups - Kilika Woods** | Silenceproof; Silencetouch 50%; White Magic MP cost -10% | White Cape | early |
| 40 / `cups.five` | **V - Five of Cups - Moonflow Crossing** | Sleepproof; Sleeptouch 50%; MP +15% | Twist Headband | early |
| 41 / `cups.six` | **VI - Six of Cups - Besaid Temple** | Darkproof; Darktouch 50%; Accuracy +15 | Silver Glasses | early |
| 42 / `cups.seven` | **VII - Seven of Cups - Farplane** | Stoneproof; Stonetouch 30%; Defense +10% | Gold Anklet | midgame |
| 43 / `cups.eight` | **VIII - Eight of Cups - Gagazet** | Confuseproof; Confusetouch 30%; Magic Defense +15% | Black Choker | midgame |
| 44 / `cups.nine` | **IX - Nine of Cups - Luca Tavern** | SOS Regen; HP +20%; Defend restores 3% MP | Regen Bangle | midgame |
| 45 / `cups.ten` | **X - Ten of Cups - Besaid Village** | HP +25%; White Magic healing +15%; Auto-Phoenix | Titanium Bangle | midgame |
| 46 / `cups.page` | **Page of Cups - Temple Acolyte** | White Magic healing +15%; Silenceproof; SOS Regen | White Lore | early |
| 47 / `cups.knight` | **Knight of Cups - Shoopuf Crossing** | MP cost -25%; Auto-Shell; Magic Defense +10% | Gold Hairpin | midgame |
| 48 / `cups.queen` | **Queen of Cups - Besaid Coast** | Auto-Regen; White Magic healing +20%; Half MP for White Magic | Recovery Bracer | late |
| 49 / `cups.king` | **King of Cups - Spiran Sea** | Auto-Protect; HP +25%; recovery items +25% | Shining Bracer | late |
| 50 / `swords.ace` | **I - Ace of Swords - Gagazet** | Strength +15%; Piercing; Accuracy +10 | Wristband | arrival |
| 51 / `swords.two` | **II - Two of Swords - Besaid Coast** | Defense +15%; Auto-Protect; Evasion +10 | Mythril Gloves | early |
| 52 / `swords.three` | **III - Three of Swords - Macalania Crystal** | Poisonstrike 100% for 3 turns; Poisonproof; outgoing HP damage +5% | Venom Shock | midgame |
| 53 / `swords.four` | **IV - Four of Swords - Fayth Chamber** | Sleepstrike 100%; Sleepproof; first CTB recovery -10% | Dream Shock | midgame |
| 54 / `swords.five` | **V - Five of Swords - Al Bhed** | Silencestrike 100%; Silenceproof; Magic Defense +15% | Mute Shock | midgame |
| 55 / `swords.six` | **VI - Six of Swords - Moonflow Ferry** | Darkstrike 100%; Darkproof; Evasion +15 | Blind Shock | midgame |
| 56 / `swords.seven` | **VII - Seven of Swords - Rikku** | Master Thief; Accuracy +20; first CTB recovery -15% | Amulet | midgame |
| 57 / `swords.eight` | **VIII - Eight of Swords - Calm Lands** | Slowstrike 100%; Slowproof; first CTB recovery -10% | Lag Shock | late |
| 58 / `swords.nine` | **IX - Nine of Swords - Zanarkand Pilgrim** | Deathtouch 25%; Deathproof; kills restore 10% MP once per action | Mortal Shock | late |
| 59 / `swords.ten` | **X - Ten of Swords - Fallen Guardians** | Counterattack; Strength/Defense +15%; HP +10% | Black Belt | late |
| 60 / `swords.page` | **Page of Swords - Calm Lands** | Accuracy +25; Sensor; Critical chance +5% | Gauntlets | early |
| 61 / `swords.knight` | **Knight of Swords - Chocobo Knights** | Evade and Counter; Evasion +20; Auto-Protect | Sprint Shoes | late |
| 62 / `swords.queen` | **Queen of Swords - Spiran Guardians** | Magic Counter; Magic Defense +20%; Half MP for Black Magic | Star Bracer | late |
| 63 / `swords.king` | **King of Swords - Bevelle** | Critical chance +15%; Luck +15; Strength +15%; Overdrive damage +10% | Amulet | late |
| 64 / `pentacles.ace` | **I - Ace of Pentacles - Besaid** | Gil +50% (highest-only with native rates); Master Thief | Key to Success | arrival |
| 65 / `pentacles.two` | **II - Two of Pentacles - Al Bhed** | AP +35%; Gil +25%; Luck +10 (native No AP still wins) | AP Egg | early |
| 66 / `pentacles.three` | **III - Three of Pentacles - Djose Temple** | Recovery items +50%; Auto-Potion; HP +20% | Key to Success | early |
| 67 / `pentacles.four` | **IV - Four of Pentacles - Rin** | Defense +25%; Auto-Protect; HP +10% | Diamond Gloves | midgame |
| 68 / `pentacles.five` | **V - Five of Pentacles - Gagazet Pilgrims** | Magic Defense +25%; Auto-Shell; MP +15% | Mystery Veil | midgame |
| 69 / `pentacles.six` | **VI - Six of Pentacles - Oaka** | Kills restore 10% HP and 10% MP once per action; Piercing | Wizard Bracelet | midgame |
| 70 / `pentacles.seven` | **VII - Seven of Pentacles - Besaid Grove** | Lightning/Water damage +15%; Lightning/Water Wards; MP +20% | Yellow Ring, Blue Ring | midgame |
| 71 / `pentacles.eight` | **VIII - Eight of Pentacles - Rikku** | AP +75%; MP cost -25%; begin battle with Focus x1 (native No AP still wins) | Charm Bangle, AP Egg, Key to Success | midgame |
| 72 / `pentacles.nine` | **IX - Nine of Pentacles - Luca** | Begin battle with +15% Overdrive; Overdrive damage +10%; first CTB recovery -10% | Sprint Shoes | late |
| 73 / `pentacles.ten` | **X - Ten of Pentacles - Besaid Village** | HP/MP +25%; Auto-Regen; incoming HP damage -5% | Iron Bangle, Silver Bracer | midgame |
| 74 / `pentacles.page` | **Page of Pentacles - Al Bhed Scholar** | Auto-Potion; recovery items +25%; Defense +15% | Cat's Bell | early |
| 75 / `pentacles.knight` | **Knight of Pentacles - Chocobo** | Auto-Shell; MP +20%; Magic Counter | Moon Bracer | late |
| 76 / `pentacles.queen` | **Queen of Pentacles - Bikanel** | Auto-Phoenix; White Magic healing +20%; HP +20% | Angel Earrings | late |
| 77 / `pentacles.king` | **King of Pentacles - Rin Travel Agency** | Darkness/Silence/Sleep/Poison proof; matching touches 30% (Poison: 3 turns) | Ribbon | late |

## Regras comuns

- Percentuais, imunidades, CTB, uso de itens e anti-recursão seguem o documento principal. Strike/Touch são chances-base propostas, nunca promessa de ignorar a resistência do alvo.
- As fases acima preservam a direção de progressão; os requisitos concretos estão em `research/mod_008_arcana/acquisition-v1.md`.
- `runtime_compatible: false` identifica o catálogo de design. O runtime usa o cabeçalho compilado e handlers tipados, sem interpretar prosa.
- Todas as ilustrações são conceitos. O manifesto visual distingue arquivos gerados de assets convertidos, carregados e vistos no jogo.
