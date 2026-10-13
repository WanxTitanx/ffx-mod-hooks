#pragma once
#include "ArcanaCore.h"
#include <cstdint>

namespace FfxHooks::Arcana {
// Only the profile-gated original-challenge observer issues these receipts.
// Generic acquisition and menu code cannot grant these cards through Award.
struct VictoryReceipt {
    std::uint64_t session=0,battle=0;
    unsigned field=0,formation=0,monster=0,stage=0;
    bool original=false,victory=false;
};
inline unsigned VictoryCard(const VictoryReceipt& r) noexcept {
    if(!r.session||!r.battle||!r.original||!r.victory)return kCardCount;
    if(r.monster==100)return r.field==1077&&r.formation==10?kOmegaWeaponCard:kCardCount;
    if(r.monster<334||r.monster>343)return kCardCount;
    if(r.monster==336&&r.stage!=2)return kCardCount;
    if(r.monster==340&&r.stage!=5)return kCardCount;
    return kDarkAeonFirstCard+r.monster-334;
}
}
