#pragma once
// Jarvis-HOOK: native encounter IDs and the shipped v1 package IDs are different domains.
namespace FfxHooks::OmegaEncounter {
inline constexpr unsigned NativeField(unsigned package) noexcept {
    return package==1076?590:package==1077?591:0;
}
inline constexpr unsigned PackageField(unsigned native) noexcept {
    return native==590?1076:native==591?1077:0;
}
inline constexpr bool Eligible(unsigned field,unsigned formation) noexcept {
    return (field==590&&formation<=18&&formation!=10)||(field==591&&formation<=8);
}
inline constexpr bool PackageEligible(unsigned field,unsigned formation) noexcept {
    return Eligible(NativeField(field),formation);
}
}
