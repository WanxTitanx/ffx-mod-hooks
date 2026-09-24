#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace FfxHooks::ArenaPositions
{

struct Point
{
    float x = 0.0f;
    float z = 0.0f;
};
struct Layout
{
    // An explicit Auto Arrange or Apply action enables this optional writer.
    bool enabled = false;
    bool automatic = false;
    std::uint8_t count = 0u;
    std::array<Point, 8u> points{};
};

// Exact dome02_00 PartyFront X/Z reference, verified against the local vanilla
// carrier. The runtime rechecks this reference before any position mutation.
inline constexpr std::array<Point, 7u> kPartyReference = {
    {{0, 0}, {0, 72}, {64, 36}, {64, -36}, {0, -72}, {-64, -36}, {-64, 36}}};
inline constexpr float kSideLimit = 160.0f;
inline constexpr float kNearLimit = 80.0f;
inline constexpr float kFarLimit = 200.0f;
inline constexpr float kMinimumSeparation = 24.0f;

enum class Issue : std::uint8_t
{
    None,
    Count,
    NonFinite,
    Bounds,
    Overlap,
    PartyOverlap
};

inline Issue Validate(const Layout &layout, std::uint8_t count) noexcept
{
    if (!layout.enabled)
        return Issue::None;
    if (count == 0u || count > 8u || layout.count != count)
        return Issue::Count;
    for (std::uint8_t i = 0; i < count; ++i)
    {
        const Point point = layout.points[i];
        if (!std::isfinite(point.x) || !std::isfinite(point.z))
            return Issue::NonFinite;
        if (point.x < -kSideLimit || point.x > kSideLimit || point.z < kNearLimit ||
            point.z > kFarLimit)
            return Issue::Bounds;
        const auto tooClose = [point](Point other) {
            const float dx = point.x - other.x, dz = point.z - other.z;
            return dx * dx + dz * dz < kMinimumSeparation * kMinimumSeparation;
        };
        for (std::uint8_t j = 0; j < i; ++j)
            if (tooClose(layout.points[j]))
                return Issue::Overlap;
        for (Point party : kPartyReference)
            if (tooClose(party))
                return Issue::PartyOverlap;
    }
    return Issue::None;
}

inline Layout Generate(std::uint8_t count) noexcept
{
    Layout layout{};
    if (count == 0u || count > 8u)
        return layout;
    layout.enabled = true;
    layout.automatic = true;
    layout.count = count;
    const unsigned front = count <= 4u ? count : (count + 1u) / 2u;
    for (unsigned i = 0; i < count; ++i)
    {
        const bool back = i >= front;
        const unsigned rowCount = back ? count - front : front;
        const unsigned column = back ? i - front : i;
        layout.points[i].x =
            (static_cast<float>(column) - static_cast<float>(rowCount - 1u) * 0.5f) * 56.0f;
        layout.points[i].z = count <= 4u ? 140.0f : (back ? 172.0f : 116.0f);
    }
    return layout;
}

inline bool Move(Layout *layout, std::uint8_t slot, float dx, float dz) noexcept
{
    if (!layout || !layout->enabled || slot >= layout->count || slot >= 8u)
        return false;
    Layout candidate = *layout;
    candidate.points[slot].x += dx;
    candidate.points[slot].z += dz;
    candidate.automatic = false;
    if (Validate(candidate, candidate.count) != Issue::None)
        return false;
    *layout = candidate;
    return true;
}

// Exact PE32 accessor at RVA 0x003AC000. The one absolute operand is rebased.
inline constexpr std::uint32_t kAccessorRva = 0x003AC000u;
inline constexpr std::size_t kAccessorSignatureBytes = 26u;
inline bool AccessorSignatureMatches(const std::uint8_t *bytes, std::size_t size,
                                     std::uintptr_t imageBase) noexcept
{
    if (!bytes || size < kAccessorSignatureBytes || imageBase == 0u ||
        imageBase > (std::numeric_limits<std::uint32_t>::max)() - 0x00D2A9B0u)
        return false;
    std::array<std::uint8_t, kAccessorSignatureBytes> expected{
        {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x8B, 0x4D, 0x10, 0x53, 0x8B, 0x1D, 0xB0,
         0xA9, 0x12, 0x01, 0x56, 0xC7, 0x45, 0xFC, 0,    0,    0,    0,    0x8B, 0xF3}};
    const auto global = static_cast<std::uint32_t>(imageBase + 0x00D2A9B0u);
    std::memcpy(expected.data() + 12u, &global, 4u);
    return std::memcmp(bytes, expected.data(), expected.size()) == 0;
}

} // namespace FfxHooks::ArenaPositions
