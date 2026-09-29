#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::WeaponStrikeVfx {

inline constexpr unsigned kActors = 7, kVisuals = 2, kParticles = 16;
inline constexpr unsigned kParticleBytes = 0x70 + kParticles * 0x50;
using ParticleRing = std::array<unsigned char, kParticleBytes>;
inline ParticleRing MakeRing(const std::array<unsigned char, 8>& nativeDrawHeader) {
    ParticleRing ring{};
    const std::uint16_t count = kParticles;
    std::memcpy(ring.data(), &count, 2);
    std::memcpy(ring.data() + 4, nativeDrawHeader.data(), nativeDrawHeader.size());
    return ring;
}
enum class Visual : unsigned { Holy, Shadow };
struct Style {
    const char* name;
    std::uint8_t element, texture, red, green, blue, alpha;
    std::uint16_t size;
    std::int16_t sizeStep, rise, rotation;
    std::uint8_t lifetime, interval;
};
inline constexpr std::array<Style, kVisuals> kStyles{{
    {"Holystrike",   0x10, 4, 128, 110, 52, 112, 192, -3, -192,  32, 24, 2},
    {"Shadowstrike", 0x80, 1,  70,  32,128,  96, 288,  8,   64, -16, 20, 3}
}};

struct Program {
    std::array<std::uint16_t, 128> words{};
    std::size_t count = 0, particle = 0, child = 0;
    constexpr void Add(std::uint16_t value) { words[count++] = value; }
    constexpr void Relative(std::size_t operand, std::size_t opcode, std::size_t target) {
        words[operand] = static_cast<std::uint16_t>(static_cast<int>(target * 2) - static_cast<int>(opcode * 2));
    }
};

// Authored OPU + type-9 particle programs, not copies of game assets. See the
// exact-PE native-handler evidence in HOLY_SHADOW_WEAPON_VFX_2026_09_29.md.
// The 0x9099 instruction has THREE operands: interpolation field and two weapon
// endpoints. 0x800E/0x8001 here are bone IDs, not separate vector instructions.
constexpr Program MakeProgram(Visual visual) {
    const auto& s = kStyles[static_cast<unsigned>(visual)];
    Program p{};
    p.Add(0x0809);                          // Native four-tick startup delay.
    // The adapter constructs the type-9 ring before publishing this program.
    // Native 0x008F would allocate later without checking heap exhaustion.
    const auto allocate = p.count;
    p.Add(0x8026); p.Add(0);                 // Child script in this OPU record.
    p.Add(0x8064);                          // Native effect-slot actor mapping.
    p.Add(0x0052); p.Add(0x000C);            // Native visibility value 127.
    p.Add(0x502F); p.Add(0x4000);            // Wait for native stop flag.
    p.Add(0x002A);                          // Cancel child scripts.
    p.Add(0x4009);                          // Drain 32 ticks (> both lifetimes).
    p.Add(0x007F); p.Add(0x0000);            // Native finalize / record cleanup.

    p.child = p.count;
    const auto pause = p.count;
    p.Add(0x0052); p.Add(0x0013); p.Add(0);  // Preserve native particle pause gate.
    p.Add(0x800A); p.Add(0x0080);            // Weapon interpolation midpoint.
    p.Add(0x8012); p.Add(0x0080);            // Scatter along the weapon segment.
    p.Add(0x9099); p.Add(0x003C); p.Add(0x800E); p.Add(0x8001);
    const auto emit = p.count;
    p.Add(0x208F); p.Add(0); p.Add(0xFF00);  // Emit into SELF, never native ID 0x20.
    p.Add(static_cast<std::uint16_t>((s.interval << 9) | 0x09));
    const auto repeat = p.count;
    p.Add(0x0002); p.Add(0);
    const auto wait = p.count;
    p.Add(0x0409);
    const auto retry = p.count;
    p.Add(0x0002); p.Add(0);

    p.particle = p.count;
    p.Add(0x0007); p.Add(s.texture);         // Resident sparkle / smoke AN2.
    p.Add(0x000B);                          // RGBA bytes, neutral intensity = 128.
    p.Add(static_cast<std::uint16_t>(s.red | (s.green << 8)));
    p.Add(static_cast<std::uint16_t>(s.blue | (s.alpha << 8)));
    p.Add(0x100B); p.Add(0); p.Add(0xFC00); // Alpha -4/tick; no RGB underflow.
    p.Add(0x7005); p.Add(8); p.Add(8); p.Add(8);
    p.Add(0x2104); p.Add(static_cast<std::uint16_t>(s.rise));
    p.Add(0x0009); p.Add(s.size);
    p.Add(0x1009); p.Add(static_cast<std::uint16_t>(s.sizeStep));
    p.Add(0x3009); p.Add(static_cast<std::uint16_t>(s.rotation));
    p.Add(static_cast<std::uint16_t>((s.lifetime << 8) | 0x01));
    p.Add(0x0000);

    p.Relative(allocate + 1, allocate, p.child);
    p.Relative(pause + 2, pause, wait);
    p.Relative(emit + 1, emit, p.particle);
    p.Relative(repeat + 1, repeat, p.child);
    p.Relative(retry + 1, retry, p.child);
    return p;
}
inline constexpr std::array<Program, kVisuals> kPrograms{{MakeProgram(Visual::Holy), MakeProgram(Visual::Shadow)}};
static_assert(kPrograms[0].count < 128 && kPrograms[1].count < 128);
static_assert(kStyles[0].lifetime < 32 && kStyles[1].lifetime < 32);
static_assert((kStyles[0].lifetime + kStyles[0].interval - 1) / kStyles[0].interval < kParticles);
static_assert((kStyles[1].lifetime + kStyles[1].interval - 1) / kStyles[1].interval < kParticles);

struct Handle { std::uint32_t key = 0, record = 0, buffer = 0; bool stopping = false; };
struct ActorState { std::uintptr_t actor = 0; std::array<Handle, kVisuals> handles{}; };
struct State { std::uint32_t root = 0; std::array<ActorState, kActors> actors{}; };

struct HeapNode {
    std::uint32_t begin = 0, end = 0;
    std::uint16_t previous = 0, next = 0, high = 0, count = 0;
};
static_assert(sizeof(HeapNode) == 16);
struct HeapPlan { std::uint32_t address = 0; std::uint16_t node = 0; };

// RVA 0x3FF6A0 deliberately faults on heap exhaustion and does not check whether
// growing data reaches its descending metadata. Refuse BEFORE calling it. The
// native main thread owns the heap for the complete check/allocation interval.
template<class Read>
HeapPlan PlanAllocation(std::uint32_t arena, std::uint32_t bytes, Read&& read) {
    constexpr unsigned limit = 512;
    if (arena < 0x10000 || (arena & 15) || !bytes || bytes > 0x10000) return {};
    const auto aligned = (bytes + 15u) & ~15u;
    HeapNode head{};
    if (!read(arena - 16, head) || head.high >= limit - 1 ||
        head.count > (head.high ? head.high - 1u : 0u) || head.previous || head.begin > head.end || head.begin < 0x10000) return {};
    const auto lastCandidate = head.high > 1 ? head.high + 1u : 2u;
    unsigned freeIndex = 2;
    for (; freeIndex <= lastCandidate; ++freeIndex) {
        HeapNode node{};
        if (!read(arena - freeIndex * 16, node)) return {};
        if (!node.previous) break;
    }
    if (freeIndex > lastCandidate) return {};
    const unsigned high = head.high > freeIndex ? head.high : freeIndex;
    const auto ceiling = arena - high * 16;
    if (head.end > ceiling) return {};
    std::array<bool, limit> seen{};
    seen[1] = true;
    auto previous = head;
    unsigned previousIndex = 1, active = 0;
    std::uint32_t selected = 0;
    while (previous.next) {
        const auto index = previous.next;
        if (index < 2 || index > head.high || seen[index] || index == freeIndex) return {};
        seen[index] = true;
        HeapNode current{};
        if (!read(arena - index * 16, current) || current.previous != previousIndex ||
            current.begin < previous.end || current.end < current.begin || current.end > ceiling) return {};
        if (!selected && current.begin - previous.end >= aligned) selected = previous.end;
        previous = current;
        previousIndex = index;
        ++active;
    }
    if (active != head.count) return {};
    if (!selected && ceiling - previous.end >= aligned) selected = previous.end;
    return selected ? HeapPlan{selected, static_cast<std::uint16_t>(freeIndex)} : HeapPlan{};
}

// The IO boundary owns native calls and must execute only on the admitted game
// thread. Dead handles are discarded; a stopping handle is never resurrected.
template<class Io>
void ReconcileActor(State& state, unsigned actor, std::uintptr_t pointer,
                    std::uint8_t mask, bool enabled, Io& io) {
    if (actor >= kActors) return;
    auto& current = state.actors[actor];
    const bool replaced = current.actor != 0 && current.actor != pointer;
    for (unsigned i = 0; i < kVisuals; ++i) {
        auto& handle = current.handles[i];
        if (handle.key && !io.Alive(handle)) handle = {};
        const bool wanted = enabled && pointer && (mask & kStyles[i].element) != 0;
        if (handle.key && (replaced || !wanted) && !handle.stopping) {
            if (io.Stop(handle)) handle.stopping = true;
        }
        if (!handle.key && wanted && !replaced) handle = io.Spawn(actor, pointer, static_cast<Visual>(i));
    }
    current.actor = pointer;
}
} // namespace FfxHooks::WeaponStrikeVfx
