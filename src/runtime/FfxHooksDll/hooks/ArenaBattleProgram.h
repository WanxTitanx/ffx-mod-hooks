#pragma once
#include "CustomMixUltraCore.h"
#include <cstdint>
#include <string>
#include <vector>

namespace FfxHooks::ArenaBattleProgram {
// Game-derived scripts stay in a private exact-hash bundle, never in source.
bool Load(const char* path, std::string* error = nullptr);
bool LoadForCurrentModule(std::string* error = nullptr);
bool Ready() noexcept;
struct Encounter {
    std::array<char,16> name{};
    std::uint16_t scenery=0xffffu;
    std::uint8_t count=0;
    std::uint8_t flags=0;
    std::array<std::uint16_t,8> monsters{};
};
// Immutable private archive metadata; no game script is exposed as editable input.
const std::vector<Encounter>& Encounters() noexcept;
bool UseEncounter(const Encounter&,const CustomMixUltra::SelectionInput& current,
                  CustomMixUltra::SelectionInput* output) noexcept;
struct Geometry {
    float originX=0, originZ=0, forwardX=0, forwardZ=1;
    std::uint32_t slotsOffset=0, areaOffset=0, positionsOffset=0;
    std::uint32_t nativePositions=0;
    const char* sourceName=nullptr;
};
bool Describe(ArenaScenery::Choice scenery, ArenaScenery::Camera camera, Geometry* out) noexcept;
void ToWorld(const Geometry&, float x, float z, float* wx, float* wz) noexcept;
void ToRelative(const Geometry&, float wx, float wz, float* x, float* z) noexcept;
unsigned PartyPreview(ArenaScenery::Choice, ArenaScenery::Camera,
                      std::array<ArenaPositions::Point,7>* points) noexcept;
unsigned MonsterPreview(const CustomMixUltra::SelectionInput&,std::array<ArenaPositions::Point,8>* points) noexcept;
bool Build(const CustomMixUltra::SelectionInput&, std::vector<std::uint8_t>* output,
           std::string* error = nullptr);

struct Frame {
    std::vector<std::uint8_t> bytes;
    CustomMixUltra::SelectionInput selection{};
    Geometry geometry{};
    bool published=false;
};
// Reserve before the native queue. Published data is retained for the process:
// native workers borrow these pointers even after the Hooks admission closes.
Frame* Reserve(const CustomMixUltra::SelectionInput&) noexcept;
void ReleaseUnpublished(Frame*) noexcept;
bool Matches(const Frame*, const CustomMixUltra::SelectionInput&) noexcept;
inline constexpr unsigned kMaximumFrames=256u;
} // namespace FfxHooks::ArenaBattleProgram
