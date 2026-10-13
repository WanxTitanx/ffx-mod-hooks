#pragma once
namespace FfxHooks::SinSpread {
struct ThreatColors {unsigned top,bottom,marker;};
// ARGB for NativeMenu::DrawSolidRect. T labels remain the primary tier identity.
inline constexpr ThreatColors ThreatPalette(unsigned threat) noexcept {
    constexpr ThreatColors tiers[]={
        {0x60243242u,0x501B2431u,0x80778295u}, // T0: neutral
        {0x7026414Au,0x601C3138u,0x8062CBC5u}, // T1: teal
        {0x703D315Bu,0x602D2443u,0x80949EF2u}, // T2: violet
        {0x70574522u,0x60403318u,0x80E8C260u}, // T3: gold
        {0x70603A25u,0x60442A1Du,0x80F3A066u}, // T4: orange
        {0x705C2930u,0x60421F25u,0x80EE7D86u}, // T5: red
        {0x70562B4Fu,0x6040203Bu,0x80DF8BCEu}, // T6: magenta
        {0x70603D4Fu,0x60462B39u,0xFFFFAACDu}, // T7: awakened rose
        {0x70604634u,0x60463225u,0xFFFFBA86u}, // T8: awakened amber
        {0x705A4D36u,0x60423827u,0xFFFFD991u}, // T9: awakened gold
        {0x70605058u,0x60463840u,0xFFFFE8F1u}, // T10: pale rose
    };
    return tiers[threat<11?threat:0];
}
}
