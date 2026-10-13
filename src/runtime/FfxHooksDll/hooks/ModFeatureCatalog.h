#pragma once
#include "../shared/Config.h"
#include "FahrenheitCoexistenceCore.h"
#include <array>

namespace FfxHooks::ModFeatures {
enum class Feature : unsigned { Core,Tactics,Gravity,MagicBdl,Spira,Ascension,WeaponStrikes,NulSpells,OriginalPs2Rng,ButterflyAssist,ChocoboAssist,CaptureCascade,ItemMpRegen,OmegaUltima,OmegaT7,OmegaLastSeal,Count };
#define FFXHOOKS_REFORGE_GROUPS_V1 1
enum class MenuGroup : unsigned {Additional,Equipment,Capture,Resources,Omega};
inline constexpr MenuGroup GroupFor(Feature feature) noexcept {
    switch(feature){
    case Feature::Spira:case Feature::Ascension:return MenuGroup::Equipment;
    case Feature::CaptureCascade:return MenuGroup::Capture;
    case Feature::ItemMpRegen:return MenuGroup::Resources;
    case Feature::OmegaUltima:case Feature::OmegaT7:case Feature::OmegaLastSeal:return MenuGroup::Omega;
    default:return MenuGroup::Additional;
    }
}
// Menu placement is independent of stable feature IDs and startup resolution.
struct Entry {const char* label;Config::BoolGateSpec gate;const char* help;bool additionalMenu=true;};
inline constexpr std::array<Entry,static_cast<unsigned>(Feature::Count)> Entries{{
    {"Elemental Dominion: Core",{"elemental.core","f8_authority.elemental_core",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - 8 native + 2 built-in custom elements."},
    {"Elemental Dominion: Tactics",{"elemental.tactics","f8_authority.elemental_tactics",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Imperil, Ward and Nul per target action."},
    {"Elemental Dominion: Gravity",{"elemental.gravity","f8_authority.elemental_gravity",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Boss max HP /16; immunity; nonlethal."},
    {"Magic Break Damage Limit",{"elemental.magic_bdl","f8_authority.elemental_magic_bdl",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Offensive magic BDL cap: 999999 per HP hit."},
    {"Spira Reforge abilities",{"spira.enabled","f8_authority.spira_enabled",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Spira + Tarot; Warden is Auron-only."},
    {"Aeon Ascension upgrades",{"aeon_ascension.enabled","f8_authority.aeon_ascension_enabled",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Paid Aeon caps; no HP or MP refill."},
    {"Holy / Shadow weapon effects",{"weapon_strike_vfx.enabled","f8_authority.weapon_strike_vfx",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Holy/Shadow VFX; no element granted."},
    {"Elemental Nul spells",{"elemental.nul_spells","f8_authority.elemental_nul_spells",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Six Nuls; learn through Sphere Grid."},
    {"Original PS2 RNG (Experimental)",{"rng.original_ps2",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Experimental PS2 RNG; NTSC clock model."},
    {"Butterfly hunt assist",{"minigames.butterflies",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - 60/80s hunts; catch all 7 blue butterflies.",false},
    {"Chocobo challenge assist",{"minigames.chocobos",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Easier goals, -5/+1s, slower Remiem rival.",false},
    {"Spira Reforged: Capture Cascade",{"spira.capture_cascade",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Dark Aeon victories awaken T7 regions."},
    {"Spira Reforged: MP item regeneration",{"spira.item_mp_regen",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - MP Cost 0 items: 1% max MP each ally turn."},
    {"Spira Reforged: Ultima Echo encounters",{"spira.omega_ultima",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Rare Ultima Echo after Ultima's defeat."},
    {"Spira Reforged: Omega Seventh Seal",{"spira.omega_t7",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - One changing T7 mark per Omega creature."},
    {"Spira Reforged: Omega Last Seal",{"spira.omega_last_seal",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - T9 Omega Weapon with Custodians and Echoes."},
}};
inline bool Enabled(Feature feature){
    const auto index=static_cast<unsigned>(feature);
    return index<Entries.size()&&Coexistence::FeatureAllowed(Entries[index].gate.canonicalKey)&&
        Config::ResolveBoolGate(Entries[index].gate).value;
}
} // namespace FfxHooks::ModFeatures
