#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::RonsoPool {
inline constexpr uint8_t kCapacity = 200;
inline constexpr uint16_t kCharacter = 3, kFirstCommand = 104, kLastCommand = 115,
    kHeaderCommand = 282;
enum class Availability { Native, Block, Allow };
struct Facts {
    bool enabled=false, stopping=false, ownedPartyActor=false, learned=false,
        nativeRestrictionsAllow=false;
    uint16_t characterId=0, commandId=0;
    uint8_t charge=0, commandCost=0;
};
struct Decision { Availability availability=Availability::Native; bool useCapacity200=false; };
Decision Evaluate(const Facts&) noexcept;
Availability EvaluateHeader(const Facts&, const std::array<Facts,12>&) noexcept;
struct Owner {
    uint64_t generation=0;
    uint32_t actorToken=0;
    uint8_t originalMax=0;
    bool active=false, conflict=false;
};
bool CanRestore(const Owner&, uint64_t generation, uint32_t actorToken,
                uint8_t observedMax) noexcept;
}
