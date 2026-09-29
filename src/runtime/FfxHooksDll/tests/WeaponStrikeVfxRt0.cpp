#include "../hooks/WeaponStrikeVfxCore.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace FfxHooks::WeaponStrikeVfx;

struct Io {
    unsigned spawned = 0, stopped = 0;
    bool alive = true, failSpawn = false, failStop = false;
    bool Alive(const Handle&) { return alive; }
    bool Stop(const Handle&) { ++stopped; return !failStop; }
    Handle Spawn(unsigned, std::uintptr_t, Visual) {
        ++spawned;
        return failSpawn ? Handle{} : Handle{spawned, 0x200000 + spawned * 256, 0x300000 + spawned * 4096, false};
    }
};
int main(int argc, char**) {
    if (argc > 1) {
        std::printf("[");
        for (unsigned i = 0; i < kVisuals; ++i) {
            const auto& p = kPrograms[i];
            std::printf("%s{\"name\":\"%s\",\"child\":%zu,\"particle\":%zu,\"words\":[",i ? "," : "",kStyles[i].name,p.child,p.particle);
            for (std::size_t j = 0; j < p.count; ++j) std::printf("%s%u",j ? "," : "",p.words[j]);
            std::printf("],\"ring_hex\":\"");
            const auto ring=MakeRing({{0,0,0,0,128,128,128,128}});
            for(auto byte:ring)std::printf("%02x",byte);
            std::printf("\"}");
        }
        std::printf("]\n");
        return 0;
    }
    unsigned checks = 0;
    for (unsigned mask = 0; mask <= 255; ++mask) {
        State state{}; Io io;
        ReconcileActor(state, 0, 0x100000, static_cast<std::uint8_t>(mask), false, io);
        assert(io.spawned == 0); ++checks;
        ReconcileActor(state, 0, 0x100000, static_cast<std::uint8_t>(mask), true, io);
        const unsigned expected = ((mask & 0x10) ? 1u : 0u) + ((mask & 0x80) ? 1u : 0u);
        assert(io.spawned == expected); ++checks;
        ReconcileActor(state, 0, 0x100000, static_cast<std::uint8_t>(mask), true, io);
        assert(io.spawned == expected); ++checks;
        ReconcileActor(state, 0, 0x100000, 0, true, io);
        assert(io.stopped == expected); ++checks;
        ReconcileActor(state, 0, 0x100000, static_cast<std::uint8_t>(mask), true, io);
        assert(io.spawned == expected); ++checks; // Never resurrect a draining handle.
    }
    {
        State state{}; Io io;
        ReconcileActor(state, kActors, 0x100000, 0x90, true, io);
        assert(io.spawned == 0); ++checks;
        ReconcileActor(state, 0, 0x100000, 0x90, true, io);
        ReconcileActor(state, 0, 0x110000, 0x90, true, io);
        assert(io.spawned == 2 && io.stopped == 2); ++checks;
        io.alive = false;
        ReconcileActor(state, 0, 0x110000, 0x90, true, io);
        assert(io.spawned == 4); ++checks;
    }
    {
        State state{}; Io io; io.failSpawn = true;
        ReconcileActor(state, 0, 0x100000, 0x90, true, io);
        assert(!state.actors[0].handles[0].key && !state.actors[0].handles[1].key); ++checks;
        io.failSpawn = false;
        ReconcileActor(state, 0, 0x100000, 0x10, true, io);
        io.failStop = true;
        ReconcileActor(state, 0, 0x100000, 0, false, io);
        assert(!state.actors[0].handles[0].stopping); ++checks;
        io.failStop = false;
        ReconcileActor(state, 0, 0x100000, 0, false, io);
        assert(state.actors[0].handles[0].stopping && io.stopped == 2); ++checks;
    }
    for (const auto& s : kStyles) {
        assert(s.alpha > 4 * s.lifetime && static_cast<int>(s.size) + s.sizeStep * s.lifetime > 0); ++checks;
    }
    {
        constexpr std::uint32_t arena = 0x20000, begin = 0x10000;
        std::array<HeapNode, 512> nodes{};
        nodes[1] = {begin, begin, 0, 0, 0, 0}; // Native InitTransformChain starts with high-water zero.
        const auto read = [&](std::uint32_t address, HeapNode& node) {
            if (address >= arena || (arena - address) % 16 || (arena - address) / 16 >= nodes.size()) return false;
            node = nodes[(arena - address) / 16]; return true;
        };
        auto p = PlanAllocation(arena, kParticleBytes, read);
        assert(p.address == begin && p.node == 2); ++checks;
        nodes[1].high = 1; // Freeing the final node leaves high-water one.
        assert(PlanAllocation(arena, kParticleBytes, read).address == begin); ++checks;
        nodes[1] = {begin, begin, 0, 2, 2, 1};
        nodes[2] = {begin + 4096, begin + 8192, 1, 0, 0, 0};
        p = PlanAllocation(arena, kParticleBytes, read);
        assert(p.address == begin && p.node == 3); ++checks; // Native first-fit gap.
        nodes[2].previous = 3;
        assert(!PlanAllocation(arena, kParticleBytes, read).address); ++checks;
        nodes[2].previous = 1; nodes[2].next = 2;
        assert(!PlanAllocation(arena, kParticleBytes, read).address); ++checks;
        nodes[2].next = 0; nodes[2].begin = begin; nodes[2].end = arena - 48;
        assert(!PlanAllocation(arena, kParticleBytes, read).address); ++checks; // Exhaustion must not reach native allocator.
        nodes[2].end = arena - 32;
        assert(!PlanAllocation(arena, kParticleBytes, read).address); ++checks; // Metadata growth collision.
        assert(!PlanAllocation(0x100, kParticleBytes, read).address); ++checks;
        assert(!PlanAllocation(arena, 0xFFFFFFFF, read).address); ++checks;
    }
    std::printf("WeaponStrikeVfx RT0: %u checks passed\n", checks);
}
