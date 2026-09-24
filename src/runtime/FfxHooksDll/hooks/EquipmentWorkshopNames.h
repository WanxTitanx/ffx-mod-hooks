#pragma once
namespace FfxHooks::EquipmentWorkshop::Names {
inline constexpr const char* abilities[]={
 "Sensor",
 "First Strike",
 "Initiative",
 "Counterattack",
 "Evade & Counter",
 "Magic Counter",
 "Magic Booster",
 "Alchemy",
 "Auto-Potion",
 "Auto-Med",
 "Auto-Phoenix",
 "Piercing",
 "Half MP Cost",
 "One MP Cost",
 "Double Overdrive",
 "Triple Overdrive",
 "SOS Overdrive",
 "Overdrive to AP",
 "Double AP",
 "Triple AP",
 "No AP",
 "Pickpocket",
 "Master Thief",
 "Break HP Limit",
 "Break MP Limit",
 "Break Damage Limit",
 "Gillionaire",
 "HP Stroll",
 "MP Stroll",
 "No Encounters",
 "Firestrike",
 "Fire Ward",
 "Fireproof",
 "Fire Eater",
 "Icestrike",
 "Ice Ward",
 "Iceproof",
 "Ice Eater",
 "Lightningstrike",
 "Lightning Ward",
 "Lightningproof",
 "Lightning Eater",
 "Waterstrike",
 "Water Ward",
 "Waterproof",
 "Water Eater",
 "Deathstrike",
 "Deathtouch",
 "Deathproof",
 "Death Ward",
 "Zombiestrike",
 "Zombietouch",
 "Zombieproof",
 "Zombie Ward",
 "Stonestrike",
 "Stonetouch",
 "Stoneproof",
 "Stone Ward",
 "Poisonstrike",
 "Poisontouch",
 "Poisonproof",
 "Poison Ward",
 "Sleepstrike",
 "Sleeptouch",
 "Sleepproof",
 "Sleep Ward",
 "Silencestrike",
 "Silencetouch",
 "Silenceproof",
 "Silence Ward",
 "Darkstrike",
 "Darktouch",
 "Darkproof",
 "Dark Ward",
 "Slowstrike",
 "Slowtouch",
 "Slowproof",
 "Slow Ward",
 "Confuseproof",
 "Confuse Ward",
 "Beserkproof",
 "Beserk Ward",
 "Curseproof",
 "Curse Ward",
 "Auto-Shell",
 "Auto-Protect",
 "Auto-Haste",
 "Auto-Regen",
 "Auto-Reflect",
 "SOS Shell",
 "SOS Protect",
 "SOS Haste",
 "SOS Regen",
 "SOS Reflect",
 "SOS NulTide",
 "SOS NulFrost",
 "SOS NulShock",
 "SOS NulBlaze",
 "Strength +3%",
 "Strength +5%",
 "Strength +10%",
 "Strength +20%",
 "Magic +3%",
 "Magic +5%",
 "Magic +10%",
 "Magic +20%",
 "Defense +3%",
 "Defense +5%",
 "Defense +10%",
 "Defense +20%",
 "Magic Def +3%",
 "Magic Def +5%",
 "Magic Def +10%",
 "Magic Def +20%",
 "HP +5%",
 "HP +10%",
 "HP +20%",
 "HP +30%",
 "MP +5%",
 "MP +10%",
 "MP +20%",
 "MP +30%",
 "Capture",
 "Aeon Immunities",
 "Distill Power",
 "Distill Mana",
 "Distill Speed",
 "Distill Ability",
 "Ribbon",
 "Double Drop",
 "Triple Drop",
};
inline const char* Item(unsigned id){switch(id){
case 0:return "Potion";
case 2:return "X-Potion";
case 6:return "Phoenix Down";
case 7:return "Mega Phoneix";
case 8:return "Elixir";
case 14:return "Holy Water";
case 15:return "Remedy";
case 16:return "Power Distiller";
case 17:return "Mana Distiller";
case 18:return "Speed Distiller";
case 19:return "Ability Distiller";
case 21:return "Healing Water";
case 23:return "Antartic Wind";
case 24:return "Artic Wind";
case 25:return "Ice Gem";
case 26:return "Bomb Fragment";
case 27:return "Bomb Core";
case 28:return "Fire Gem";
case 29:return "Electro Marble";
case 30:return "Lightning Marble";
case 31:return "Lightning Gem";
case 32:return "Fish Scale";
case 33:return "Dragon Scale";
case 34:return "Water Gem";
case 38:return "Dream Powder";
case 39:return "Silence Grenade";
case 40:return "Smoke Bomb";
case 45:return "Poison Fang";
case 46:return "Silver Hourglass";
case 47:return "Gold Hourglass";
case 49:return "Petrify Grenade";
case 50:return "Farplane Shadow";
case 51:return "Farplane Wind";
case 52:return "Designer Waller";
case 53:return "Dark Matter";
case 54:return "Chocobo Feather";
case 55:return "Chocobo Wing";
case 56:return "Lunar Curtain";
case 57:return "Light Curtain";
case 58:return "Star Curtain";
case 59:return "Healing Spring";
case 60:return "Mana Spring";
case 63:return "Purifying Salt";
case 66:return "Twin Stars";
case 69:return "Three Stars";
case 70:return "Power Sphere";
case 71:return "Mana Sphere";
case 72:return "Speed Sphere";
case 73:return "Ability Sphere";
case 74:return "Fortune Sphere";
case 75:return "Attribute Sphere";
case 76:return "Special Sphere";
case 78:return "Wht Magic Sphere";
case 79:return "Blk Magic Sphere";
case 80:return "Master Sphere";
case 81:return "Lv.1 Key Sphere";
case 82:return "Lv.2 Key Sphere";
case 83:return "Lv.3 Key Sphere";
case 84:return "Lv.4 Key Sphere";
case 85:return "HP Sphere";
case 86:return "MP Sphere";
case 87:return "Strength Sphere";
case 88:return "Defense Sphere";
case 89:return "Magic Sphere";
case 90:return "Magic Def Sphere";
case 92:return "Evasion Sphere";
case 95:return "Clear Sphere";
case 100:return "Map";
case 102:return "Musk";
case 103:return "Hypello Potion";
case 105:return "Pendulum";
case 106:return "Amulet";
case 107:return "Door to Tomorrow";
case 108:return "Wings to Discovery";
case 109:return "Gambler's Spirit";
case 111:return "Winning Formula";
default:return "Material";}}
inline const char* Ability(unsigned word){return word>=0x8000&&word<0x8083?abilities[word-0x8000]:"Empty";}
inline constexpr const char* owners[]={"Tidus","Yuna","Auron","Kimahri","Wakka","Lulu","Rikku"};
}
