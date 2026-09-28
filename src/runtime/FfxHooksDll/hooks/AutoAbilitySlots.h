#pragma once

// Jarvis-HOOK: stable default reservations shared by independent mod consumers.
// Allowing a slot is not identity, payload, owner or purchase authorization.
namespace FfxHooks::AutoAbilitySlots {
inline constexpr unsigned VanguardFirst=135,VanguardLast=147;
inline constexpr unsigned AscensionFirst=148,AscensionLast=174,ExtendedFirst=175;
inline constexpr unsigned Maximum=4095;
inline constexpr bool Vanguard(unsigned id) noexcept {
    return id>=VanguardFirst&&id<=Maximum&&(id<=VanguardLast||id>=ExtendedFirst);
}
inline constexpr bool Ascension(unsigned index,unsigned id) noexcept {
    return index<=AscensionLast-AscensionFirst&&id<=Maximum&&
        (id==AscensionFirst+index||id>=ExtendedFirst);
}
} // namespace FfxHooks::AutoAbilitySlots
