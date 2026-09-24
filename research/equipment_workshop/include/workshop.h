#pragma once
#include <cstddef>
#include <cstdint>

// Jarvis-HOOK: experimental shared model. No game memory, hooks or disk I/O.
// The packed v1 bridge is little-endian, bounded and checked by both hosts.
namespace workshop {
constexpr unsigned GearCount=200, ItemCount=112, NativeBytes=22;
constexpr std::uint16_t Empty=0x00FF;
enum class Op : std::uint32_t {
    Swap=1, Retire, Create, Reforge, Fuse, Expand, Clear, Evolve,
    Mode, Refine, UnlockFifth, SetFifth
};
enum class Error : int {
    Ok=0, InvalidState, Stale, InvalidRequest, EmptyPiece, Equipped,
    Protected, UnsupportedAbility, Materials, Maximum, Duplicate, NoChange
};
#pragma pack(push,1)
struct Piece {
    std::uint8_t native[NativeBytes];
    std::uint64_t id;
    std::uint8_t mode, rank, fifthUnlocked, ranks[5];
    std::uint64_t abilities[5];
    std::uint16_t fifth;
};
struct State {
    std::uint32_t version;
    std::uint64_t revision, nextId, rng, rolls;
    Piece pieces[GearCount];
    std::uint16_t items[ItemCount];
};
struct Request {
    Op op;
    std::uint64_t revision, pieceId, otherId;
    std::uint16_t slot, other, value;
    std::uint8_t from[2], to[2], count, policy;
    // Reforge/Create requires a host-verified template, never a model integer.
    std::uint8_t gearTemplate[NativeBytes];
};
struct Plan {
    State after;
    std::uint16_t costs[ItemCount];
    std::uint64_t chosenAbility;
};
#pragma pack(pop)
static_assert(sizeof(Piece)==80,"v1 piece wire size");
static_assert(sizeof(State)==16260,"v1 state wire size");
std::uint16_t Ability(const Piece&,unsigned slot);
bool SupportedRefinement(std::uint16_t ability);
Error Validate(const State&);
Error Import(const std::uint8_t* nativeRecords,const std::uint16_t* items,
             std::uint64_t seed,State& out);
Error Preview(const State&,const Request&,Plan&);
const char* Message(Error);
}

#ifdef _WIN32
#define WS_EXPORT extern "C" __declspec(dllexport)
#else
#define WS_EXPORT extern "C" __attribute__((visibility("default")))
#endif
WS_EXPORT int ws_import(const std::uint8_t*,const std::uint16_t*,std::uint64_t,workshop::State*);
WS_EXPORT int ws_validate(const workshop::State*);
WS_EXPORT int ws_plan(const workshop::State*,const workshop::Request*,workshop::Plan*);
WS_EXPORT const char* ws_message(int);
