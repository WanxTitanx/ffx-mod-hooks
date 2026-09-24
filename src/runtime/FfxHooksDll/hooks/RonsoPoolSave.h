#pragma once
#include "RonsoPoolCore.h"
#include <array>

namespace FfxHooks::RonsoPool {
inline constexpr size_t kSaveSize=26880, kPayloadSize=25848,
    kSaveCharge=22529, kSaveMaximum=22530;
using SaveImage=std::array<uint8_t,kSaveSize>;
struct SavedOwner {
    uint8_t originalMax=0, charge=0, maximum=0;
};
struct SaveSession {
    uint8_t originalMax=0, dormant=0;
    bool owned=false;
    uint16_t scene=UINT16_MAX;
};
enum class SaveDecision { Native, Converted, Invalid, OwnershipConflict };
uint16_t SaveChecksum(const SaveImage&) noexcept;
bool IsValidSave(const SaveImage&) noexcept;
void SealSave(SaveImage&) noexcept;
// Metadata must already be bound to the canonical path and exact input hash.
SaveDecision LoadPool(bool active,const SaveImage&,const SavedOwner* verified,
                      uint16_t scene,SaveSession*,SaveImage*) noexcept;
SaveDecision SavePool(bool active,const SaveSession&,const SaveImage&,
                      SaveImage*,SavedOwner*,bool* needsOwner) noexcept;
void ObserveScene(SaveSession*,uint16_t scene) noexcept;
}
