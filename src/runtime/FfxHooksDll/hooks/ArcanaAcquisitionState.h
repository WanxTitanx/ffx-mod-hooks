#pragma once
#include <array>
#include <cstdint>

namespace FfxHooks::Arcana::Acquisition {
// Per-save authored objectives. Native story, inventory and minigame bits are
// read-only; these twelve bytes belong exclusively to the Arcana sidecar.
using Progress=std::array<std::uint8_t,12>;
enum ProgressSlot : unsigned {TempleRecords,SupplyBundles,HomePages,MedicineCards,Pyreflies,IfritEmblems,ChocoboPosts,CoastVisits,
    VillageLedger,AlBhedSymbols,DjoseKits,TheaterInscriptions};
inline constexpr bool ValidProgress(const Progress& value) noexcept {
    const auto route=value[ChocoboPosts];
    return value[TempleRecords]<=3&&value[SupplyBundles]<=3&&value[HomePages]<=3&&value[MedicineCards]<=4&&
        value[Pyreflies]<=7&&value[IfritEmblems]<=7&&route<=31&&value[CoastVisits]<=3&&
        value[VillageLedger]<=3&&value[AlBhedSymbols]<=3&&value[DjoseKits]<=3&&value[TheaterInscriptions]<=3&&
        (!(route&7)||(route&8))&&(!(route&16)||(route&15)==15);
}
}
