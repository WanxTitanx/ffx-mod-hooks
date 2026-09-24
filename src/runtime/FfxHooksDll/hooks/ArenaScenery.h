#pragma once
#include "ArenaPositionLayout.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::ArenaScenery {
enum class Camera : std::uint8_t { Arena = 0, Tactical = 1 };
inline bool ValidCamera(Camera camera) noexcept {
    return camera == Camera::Arena || camera == Camera::Tactical;
}
// Zero preserves the original carrier and old version-1 presets. IDs are native
// battlefield resource selectors, not encounter tokens or arbitrary field IDs.
enum class Choice : std::uint8_t {
    Carrier, MacalaniaForest, MacalaniaOpen, MacalaniaOpen2,
    Cavern, CavernWide, CavernAlt, Bikanel, MushroomRock
};
struct Info { const char* key; const char* label; std::uint16_t battlefieldId; const char* source=nullptr; };
inline constexpr Info kChoices[] = {
    {"carrier", "Zanarkand Dome", 0},
    {"macalania_forest", "Macalania Forest", 1044},
    {"macalania_open", "Macalania Open", 1046},
    {"macalania_open2", "Macalania Open 2", 1046},
    {"cavern", "Calm Lands Cavern", 1080},
    {"cavern_wide", "Calm Lands Cavern (wide)", 1080},
    {"cavern_alt", "Calm Lands (alt)", 1080},
    {"bikanel", "Bikanel Desert", 1049},
    {"remiem", "Mushroom Rock Road", 1035},
#include "ArenaSceneryCatalog.inc"
};
inline constexpr unsigned kChoiceCount = static_cast<unsigned>(sizeof(kChoices)/sizeof(kChoices[0]));
inline const Info* Get(Choice choice) noexcept {
    const auto index = static_cast<std::size_t>(choice);
    return index < kChoiceCount ? &kChoices[index] : nullptr;
}
inline const char* ProgramSource(Choice choice) noexcept {
    constexpr const char* names[]={"kino00_00","mcfr00_00","mcyt00_00","mcyt00_21","nagi05_24","nagi05_24","nagi05_24","bika02_01","kino00_00"};
    const auto* info=Get(choice);
    return !info?nullptr:info->source?info->source:names[static_cast<unsigned>(choice)];
}
inline const char* ExportCarrier(Choice choice) noexcept {
    return choice==Choice::Carrier?"dome02_00":ProgramSource(choice);
}
inline bool Parse(const char* key, Choice* out) noexcept {
    if (!key || !out) return false;
    for (unsigned i=0; i<kChoiceCount; ++i)
        if (std::strcmp(key,kChoices[i].key)==0) { *out=static_cast<Choice>(i); return true; }
    return false;
}
inline unsigned Count(unsigned requiredSlots) noexcept {
    return requiredSlots==3 ? 4u : requiredSlots==4 || requiredSlots==5 ? 3u : kChoiceCount;
}
inline Choice At(unsigned requiredSlots, unsigned index) noexcept {
    constexpr Choice x3[] = {Choice::MacalaniaForest,Choice::MacalaniaOpen,Choice::MacalaniaOpen2,Choice::MushroomRock};
    constexpr Choice x4[] = {Choice::Cavern,Choice::Bikanel,Choice::MushroomRock};
    constexpr Choice x5[] = {Choice::CavernWide,Choice::CavernAlt,Choice::MushroomRock};
    if (index>=Count(requiredSlots)) return Choice::Carrier;
    return requiredSlots==3 ? x3[index] : requiredSlots==4 ? x4[index] : requiredSlots==5 ? x5[index] : static_cast<Choice>(index);
}
inline Choice Default(unsigned requiredSlots) noexcept {
    return requiredSlots==3 ? Choice::MacalaniaOpen2 : requiredSlots==4 ? Choice::Cavern : requiredSlots==5 ? Choice::CavernWide : Choice::Carrier;
}
inline ArenaPositions::Layout Generate(Choice choice, std::uint8_t count) noexcept {
    if (!Get(choice)) return {};
    auto layout = ArenaPositions::Generate(count);
    float width = 1.0f, depth = 0.0f;
    switch (choice) {
    case Choice::MacalaniaForest: width=0.75f; depth=-12.0f; break;
    case Choice::MacalaniaOpen2: width=1.10f; depth=8.0f; break;
    case Choice::CavernWide: width=1.25f; depth=10.0f; break;
    case Choice::CavernAlt: width=0.90f; depth=-8.0f; break;
    case Choice::Bikanel: width=1.20f; depth=8.0f; break;
    case Choice::MushroomRock: width=1.35f; depth=12.0f; break;
    default: break;
    }
    // Variants share a native terrain resource but retain distinct Auto Arrange
    // spacing. Manual coordinates are kept when switching scenery.
    for (unsigned i=0; i<layout.count; ++i) {
        layout.points[i].x *= width;
        layout.points[i].z += depth;
    }
    return layout;
}
} // namespace FfxHooks::ArenaScenery
