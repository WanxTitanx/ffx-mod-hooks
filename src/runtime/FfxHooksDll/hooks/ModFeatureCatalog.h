#pragma once
#include "../shared/Config.h"
#include <array>

namespace FfxHooks::ModFeatures {
enum class Feature : unsigned { Core,Tactics,Gravity,MagicBdl,Spira,Ascension,Count };
struct Entry {const char* label;Config::BoolGateSpec gate;const char* help;};
inline constexpr std::array<Entry,static_cast<unsigned>(Feature::Count)> Entries{{
    {"Elemental Dominion: Core",{"elemental.core","f8_authority.elemental_core",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Extended elements; matching pack required."},
    {"Elemental Dominion: Tactics",{"elemental.tactics","f8_authority.elemental_tactics",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Imperil, Ward and Nul per target action."},
    {"Elemental Dominion: Gravity",{"elemental.gravity","f8_authority.elemental_gravity",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Boss max HP /16; immunity; nonlethal."},
    {"Magic Break Damage Limit",{"elemental.magic_bdl","f8_authority.elemental_magic_bdl",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Offensive magic BDL cap: 999999 per HP hit."},
    {"Spira Reforge abilities",{"spira.enabled","f8_authority.spira_enabled",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Spira effects; Warden is Auron-only."},
    {"Aeon Ascension upgrades",{"aeon_ascension.enabled","f8_authority.aeon_ascension_enabled",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,false},
     "RESTART REQUIRED - Paid Aeon caps; no HP or MP refill."}
}};
inline bool Enabled(Feature feature){
    const auto index=static_cast<unsigned>(feature);
    return index<Entries.size()&&Config::ResolveBoolGate(Entries[index].gate).value;
}
} // namespace FfxHooks::ModFeatures
