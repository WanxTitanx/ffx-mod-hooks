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
    Protected, UnsupportedAbility, Materials, Maximum, Duplicate, NoChange,
    Gil, InvalidPolicy, Locked, IncompatibleAbility
};
// Transient economy settings, never added to the persisted v1 equipment state.
struct Policy {
    std::uint32_t mode=2, baseItem=70, baseAmount=1, refinementDivisor=60;
    std::uint32_t fusionDivisor=3, fusionGilPerAbility=10000, modRecipeQuantity=30;
    std::uint32_t devFreeMaterials=0, devFreeGil=0, devIgnoreProgression=0;
};
// Read-only progression and equipped-slot identities from the native save.
// Kept out of the persisted v1 state and included in every reviewed transaction.
struct AeonProgress {
    std::uint32_t obtained=0, crests=0;
    std::uint8_t gear[20]={255,255,255,255,255,255,255,255,255,255,
                           255,255,255,255,255,255,255,255,255,255};
};
#pragma pack(push,1)
// Transient, host-validated extension identity. A zero word is an unavailable row.
// The proof binds the current loaded kernel and mapping, not the player's save.
struct CatalogEntry {std::uint16_t word=0,kind=0,item=0,reserved=0;};
struct Catalog {std::uint64_t proof=0;CatalogEntry entries[13]{};};
#pragma pack(pop)
struct Economy {Policy policy{};std::uint32_t gil=0, customizeUnlocked=0;AeonProgress aeons{};Catalog catalog{};};
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
    // Requirements cover all alternative winners; costs debit only the winner.
    // B previews must not disclose costs/chosenAbility or the after-image.
    std::uint16_t requirements[ItemCount];
    std::uint32_t gilBefore, gilCost, gilDebit, customizeUnlocked;
    Policy policy;
    AeonProgress aeons;
    Catalog catalog;
};
#pragma pack(pop)
static_assert(sizeof(Piece)==80,"v1 piece wire size");
static_assert(sizeof(State)==16260,"v1 state wire size");
static_assert(sizeof(Policy)==40 && sizeof(AeonProgress)==28 && sizeof(Catalog)==112 && sizeof(Economy)==188 && sizeof(Plan)==16912,"v5 transient wire size; persisted v1 is unchanged");
bool IsAeon(const Piece&);
bool ProtectedAbility(const Piece&,unsigned slot);
unsigned AeonCrest(unsigned owner);
const char* AeonRequirement(unsigned owner);
bool ReadAeonProgress(const std::uint8_t* payload,std::size_t size,AeonProgress&);
Error AeonAccess(const Piece&,unsigned slot,const AeonProgress&);
std::uint16_t Ability(const Piece&,unsigned slot);
unsigned AbilityRank(const Piece&,unsigned slot);
bool ValidPolicy(Policy);
bool ValidCatalog(const Catalog&);
const CatalogEntry* FindCatalogEntry(std::uint16_t ability,const Catalog*);
bool ValidFifthWord(std::uint16_t ability);
bool CustomizeCost(std::uint16_t ability,Policy,unsigned& item,unsigned& quantity,bool& native,const Catalog* catalog=nullptr);
bool RefinementCost(std::uint16_t ability,unsigned nextRank,Policy,unsigned& item,unsigned& quantity,const Catalog* catalog=nullptr);
bool SupportedRefinement(std::uint16_t ability,const Catalog* catalog=nullptr);
bool SupportedFifth(std::uint16_t ability,const Catalog* catalog=nullptr);
unsigned FifthSphere(unsigned owner);
std::uint32_t RefinementGil(unsigned nextTotal);
bool NativeCustomizeUnlocked(unsigned story);
bool NativeSlotsFilled(const Piece&);
bool FifthCost(const Piece&,std::uint16_t,Policy,unsigned& item,unsigned& quantity,const Catalog* catalog=nullptr);
Error CustomizeEligibility(const Piece&,unsigned slot,std::uint16_t ability,const Catalog* catalog=nullptr);
bool GenericRefinement(std::uint16_t ability,const Catalog* catalog=nullptr);
// Mutates a private 108-byte ability view, never a global kernel row.
void RefineAbilityRow(std::uint16_t ability,unsigned rank,std::uint8_t* row,const Catalog* catalog=nullptr);
Error Validate(const State&);
Error Import(const std::uint8_t* nativeRecords,const std::uint16_t* items,
             std::uint64_t seed,State& out);
Error Preview(const State&,const Request&,Plan&,Economy economy={});
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
WS_EXPORT int ws_plan_economy(const workshop::State*,const workshop::Request*,const workshop::Economy*,workshop::Plan*);
WS_EXPORT int ws_plan_economy_v3(const workshop::State*,const workshop::Request*,const workshop::Economy*,workshop::Plan*);
WS_EXPORT int ws_plan_economy_v4(const workshop::State*,const workshop::Request*,const workshop::Economy*,workshop::Plan*);
WS_EXPORT int ws_plan_economy_v5(const workshop::State*,const workshop::Request*,const workshop::Economy*,workshop::Plan*);
WS_EXPORT int ws_aeon_progress(const std::uint8_t*,std::size_t,workshop::AeonProgress*);
WS_EXPORT int ws_aeon_access(const workshop::Piece*,unsigned,const workshop::AeonProgress*);
WS_EXPORT unsigned ws_plan_abi();
WS_EXPORT unsigned ws_customize_unlocked(unsigned story);
// Additive read-only catalog query; it does not expose the transient plan layout.
WS_EXPORT unsigned ws_fifth_cost(unsigned kind,unsigned word,const workshop::Policy*,unsigned* item,unsigned* quantity);
WS_EXPORT int ws_customize_eligibility(const workshop::Piece*,unsigned slot,unsigned word);
WS_EXPORT const char* ws_message(int);

WS_EXPORT unsigned ws_fifth_cost_v5(unsigned,unsigned,const workshop::Policy*,const workshop::Catalog*,unsigned*,unsigned*);
WS_EXPORT int ws_customize_eligibility_v5(const workshop::Piece*,unsigned,unsigned,const workshop::Catalog*);
