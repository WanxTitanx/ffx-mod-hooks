#pragma once
#include "workshop.h"
#include <array>
#include <thread>

namespace workshop {
// Default-OFF adapter for an isolated native host. A production hook must first
// prove inventory/save lifecycle observation; this class installs no detour.
class Effects {
public:
    bool Begin(bool enabled,bool profileVerified,const State& state);
    void End();
    bool Gear(unsigned inventorySlot,const std::uint8_t* currentNative,
              std::array<std::uint8_t,24>& out) const;
    bool AbilityRow(unsigned inventorySlot,unsigned abilitySlot,
                    const std::uint8_t* nativeRow,std::array<std::uint8_t,108>& out) const;
    int AfterStatus(int nativeDamage,bool nativeStatusApplied,bool magic,
                    unsigned weaponSlot,unsigned armorSlot,unsigned actorOwner) const;
private:
    bool Active() const;
    bool active_=false;
    State state_{};
    std::thread::id owner_{};
};
}
