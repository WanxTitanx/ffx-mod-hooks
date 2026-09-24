#pragma once
#include <array>
#include <cstdint>

namespace FfxHooks::SinNatural {
// Exact 78CE... PE. Walking calls MsBattleEncountExe, not MsBattleLabelExe.
inline constexpr std::uintptr_t kStepRva=0x00380DE0u;
inline constexpr std::uint32_t kCallerRva=0x00471CEFu;
inline constexpr std::uintptr_t kFieldRowRva=0x00D2C256u;
inline constexpr std::array<std::uint8_t,24> kPrefix={{
    0x55,0x8B,0xEC,0x83,0xEC,0x10,0x53,0x56,0xFF,0x75,0x08,0xBE,
    0x01,0x00,0x00,0x00,0xE8,0xEB,0xC3,0x01,0x00,0x83,0xC4,0x04,
}};
inline constexpr std::array<std::uint8_t,5> kCall={{0xE8,0xF1,0xF0,0xF0,0xFF}};
struct Evidence {
    std::uint32_t caller=0;
    int result=0,field=-1,group=-1;
    std::uint16_t nativeFieldRow=0xFFFFu;
    std::uint8_t selectedGroup=0xFFu,formation=0xFFu;
};
inline bool Admitted(const Evidence& value){
    return value.caller==kCallerRva && value.result==-1 && value.field>=0 && value.field<=0xFFFF &&
        value.group>=0 && value.group<0xFF && value.nativeFieldRow!=0xFFFFu &&
        value.selectedGroup==static_cast<std::uint8_t>(value.group) && value.formation!=0xFFu;
}
} // namespace FfxHooks::SinNatural
