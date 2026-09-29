#include "WeaponStrikeVfxRuntime.h"
#include "WeaponStrikeVfxCore.h"
#include "WeaponStrikeVfxEvidence.generated.h"
#include "NativeUiHookSupport.h"
#include "EquipmentWorkshopStore.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <intrin.h>

namespace FfxHooks::WeaponStrikeVfx {
namespace {
static_assert(sizeof(void*) == 4, "The admitted game and effect VM are x86");
constexpr std::uint32_t kProducer = 0x39ED60, kCleanup = 0x3FB090;
constexpr std::uint32_t kRoot = 0xD2A95C, kPool = 0xEA4080, kSlots = 0xEA40C0;
constexpr std::uint32_t kActorTable = 0xD334CC, kActorStride = 0xF90;
constexpr std::uint32_t kCodeOffset = 0x140, kCodeBytes = 0x1BB0;
constexpr EquipmentWorkshop::Hash kCodeHash{{
    0x70,0x60,0xA7,0xE6,0x39,0x6A,0xF9,0x23,0xD8,0x59,0xA3,0xE3,0x27,0x84,0xCA,0x70,
    0xD9,0x18,0x45,0xAA,0x32,0x7E,0xF3,0x10,0x15,0x37,0x45,0xE8,0x88,0x81,0x5A,0x45}};
enum class Status { Off, ValidateOnly, Unsupported, Waiting, Active, ResourceMismatch, Capacity, Stopped };
static_assert(std::atomic<bool>::is_always_lock_free && std::atomic<Status>::is_always_lock_free,
              "DLL detach may only publish lock-free stop state");
std::uintptr_t base = 0;
void* originals[2]{};
void(*logger)(const char*) = nullptr;
std::atomic<bool> installed{false}, stopRequested{false};
std::atomic<unsigned> ownerThread{0}, cleanupDepth{0}, epoch{0};
std::atomic<Status> status{Status::Off};
// Only the admitted native battle thread touches state and its resource cache.
State state{};
unsigned observedEpoch = 0;
std::uint32_t rejectedRoot = 0;
bool ticking = false;

bool Range(std::uintptr_t pointer, std::size_t bytes) noexcept {
    return pointer >= 0x10000 && bytes && pointer <= UINT32_MAX - bytes;
}
bool Writable(std::uintptr_t pointer, std::size_t bytes) noexcept {
    if (!Range(pointer, bytes)) return false;
    const auto end = pointer + bytes;
    while (pointer < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(pointer), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if (next <= pointer) return false;
        pointer = next;
    }
    return true;
}
template<class T> bool Read(std::uintptr_t pointer, T& value) noexcept {
    return Range(pointer, sizeof(value)) && NativeUiSupport::Copy(&value, reinterpret_cast<const void*>(pointer), sizeof(value));
}
template<class T> bool Write(std::uintptr_t pointer, const T& value) noexcept {
    return Range(pointer, sizeof(value)) && NativeUiSupport::Copy(reinterpret_cast<void*>(pointer), &value, sizeof(value));
}
void Notice(const char* message) noexcept { if (logger) logger(message); }
bool Actor(unsigned actor, std::uintptr_t pointer, std::uint8_t& mask) noexcept {
    std::uint32_t table = 0; std::uint16_t identity = 0xFFFF; std::uint8_t hidden = 1;
    return actor < kActors && Read(base + kActorTable, table) && Range(table, kActors * kActorStride) &&
        pointer == table + actor * kActorStride && Read(pointer + 0xE, identity) && identity == actor &&
        Read(pointer + 0xDCD, hidden) && !hidden && Read(pointer + 0x5D9, mask);
}
bool Resource(std::uint32_t root) noexcept {
    if (!Range(root, 0x50FF0) || rejectedRoot == root) return false;
    std::uint32_t scripts = 0, smoke = 0, sparkle = 0;
    // Relocated canonical OEF pointers. Defer while the loader has not published
    // them; once present, verify the immutable bytecode separately from relocation.
    if (!Read(root + 0x60, scripts) || scripts != root + kCodeOffset ||
        !Read(root + 0x74, smoke) || smoke != root + 0x1D40 ||
        !Read(root + 0x80, sparkle) || sparkle != root + 0x2B70) return false;
    std::array<unsigned char, kCodeBytes> bytes{};
    EquipmentWorkshop::Hash actual{};
    if (!NativeUiSupport::Copy(bytes.data(), reinterpret_cast<void*>(scripts), bytes.size())) return false;
    try { if (!EquipmentWorkshop::Fingerprint(bytes.data(), bytes.size(), actual)) return false; }
    catch (...) { return false; }
    if (actual != kCodeHash) {
        rejectedRoot = root; status = Status::ResourceMismatch;
        Notice("[ffx-hooks] Holy/Shadow VFX: unsupported common battle bytecode; native effects retained\n");
        return false;
    }
    return true;
}
struct Pool {
    std::uint32_t address = 0, records = 0, active = 0, draw = 0, arena = 0;
    std::uint16_t capacity = 0;
};
bool ReadPool(Pool& pool) noexcept {
    return Read(base + kPool, pool.address) && Range(pool.address, 0xB0) &&
        Read(pool.address + 32, pool.records) && Read(pool.address + 16, pool.active) &&
        Read(pool.address + 28, pool.draw) && Read(pool.address + 52, pool.capacity) &&
        pool.capacity >= 2 && pool.capacity <= 512 &&
        Read(pool.address + 168, pool.arena) && Range(pool.records, pool.capacity * 256) &&
        Range(pool.active, pool.capacity * 2) && Range(pool.draw, pool.capacity * 2);
}
bool ListSpace(std::uint32_t list, unsigned capacity) noexcept {
    for (unsigned i = 0; i + 1 < capacity; ++i) {
        std::int16_t value = -1;
        if (!Read(list + i * 2, value)) return false;
        if (value < 0) return true;
        if (static_cast<unsigned>(value) >= capacity) return false;
    }
    return false;
}
bool RecordSpace(const Pool& pool) noexcept {
    if (!ListSpace(pool.active, pool.capacity) || !ListSpace(pool.draw, pool.capacity)) return false;
    for (unsigned i = 0; i + 1 < pool.capacity; ++i) {
        std::uint32_t program = 0;
        if (!Read(pool.records + i * 256, program)) return false;
        if (!program) return true;
    }
    return false;
}
bool RootRegistered(const Pool& pool) noexcept {
    for (unsigned i = 0; i < 16; ++i) {
        std::uint32_t root = 0;
        if (!Read(pool.address + 96 + i * 4, root)) return false;
        if (root == state.root) return true;
    }
    return false;
}
bool SlotSpace() noexcept {
    for (unsigned i = 1; i < 512; ++i) {
        std::uint32_t key = 0;
        if (!Read(base + kSlots + i * 32, key)) return false;
        if (key == UINT32_MAX) return true;
    }
    return false;
}
// Distinguish a missing key from an unreadable/duplicate slot table. A failed
// read must never be mistaken for permission to reuse a native key.
int Find(std::uint32_t key, std::uint32_t& record, unsigned& slot) noexcept {
    bool found = false;
    for (unsigned i = 0; i < 512; ++i) {
        std::uint32_t candidate = 0;
        if (!Read(base + kSlots + i * 32, candidate)) return -1;
        if (candidate != key) continue;
        if (found || !Read(base + kSlots + i * 32 + 4, record)) return -1;
        found = true; slot = i;
    }
    return found ? 1 : 0;
}
bool ProgramOwned(std::uint32_t address) noexcept {
    if (!address) return true;
    for (const auto& program : kPrograms) {
        const auto begin = reinterpret_cast<std::uintptr_t>(program.words.data());
        if (address >= begin && address < begin + program.count * 2 && !(address & 1)) return true;
    }
    return false;
}
struct NativeIo {
    bool Alive(const Handle& handle) noexcept {
        const auto record = handle.record;
        std::uint32_t buffer = 0, key = 0, slotRecord = 0;
        std::uint16_t recordSlot = 0; std::uint8_t type = 0;
        if (!handle.key || !Read(record + 0x14, recordSlot) || recordSlot >= 512 ||
            !Read(base + kSlots + recordSlot * 32, key) || key != handle.key ||
            !Read(base + kSlots + recordSlot * 32 + 4, slotRecord) || slotRecord != record ||
            !Read(record + 0xBC, buffer) || buffer != handle.buffer ||
            !Read(record + 0xBB, type) || type != 9) return false;
        for (unsigned i = 0; i < 4; ++i) {
            std::uint32_t program = 0;
            if (!Read(record + i * 4, program) || !ProgramOwned(program)) return false;
        }
        return true;
    }
    bool Stop(const Handle& handle) noexcept {
        if (!Alive(handle)) return false;
        std::uint32_t record = 0; unsigned slot = 0;
        if (Find(handle.key, record, slot) != 1 || record != handle.record) return false;
        reinterpret_cast<void(__cdecl*)(std::uint32_t)>(base + 0x3FC370)(handle.key);
        return true;
    }
    Handle Spawn(unsigned actor, std::uintptr_t pointer, Visual visual) noexcept {
        Pool pool{};
        if (!ReadPool(pool) || !RootRegistered(pool) || !RecordSpace(pool) || !SlotSpace() ||
            !Writable(pool.address, 0xB0) || !Writable(pool.records, pool.capacity * 256) ||
            !Writable(pool.active, pool.capacity * 2) || !Writable(pool.draw, pool.capacity * 2) ||
            !Writable(base + kSlots, 512 * 32) || !Writable(pointer + 0xE28, 2)) {
            status = Status::Capacity; return {};
        }
        const auto allocation = PlanAllocation(pool.arena, kParticleBytes,
            [](std::uint32_t address, HeapNode& node) noexcept { return Read(address, node); });
        if (!allocation.address || !Writable(allocation.address, kParticleBytes) ||
            !Writable(pool.arena - 512 * 16, 512 * 16)) { status = Status::Capacity; return {}; }
        std::uint16_t sequence = 0;
        if (!Read(pointer + 0xE28, sequence)) return {};
        std::uint32_t key = 0;
        // Use the native sequence domain; private counters could collide with a
        // later vanilla aura. This field is a volatile effect key, not save data.
        for (unsigned tries = 0; tries < 512; ++tries) {
            key = ((sequence++ & 0xFFFu) << 16) | 0xD00u | actor;
            std::uint32_t occupied = 0; unsigned slot = 0;
            const auto found = Find(key, occupied, slot);
            if (found < 0) return {};
            if (found == 0) break;
            key = 0;
        }
        if (!key) return {};
        auto allocate = reinterpret_cast<std::uint32_t(__cdecl*)(std::uint32_t, unsigned)>(base + 0x3FF6A0);
        auto release = reinterpret_cast<void(__cdecl*)(std::uint32_t, std::uint32_t)>(base + 0x3FF0F0);
        const auto buffer = allocate(pool.arena, kParticleBytes);
        if (buffer != allocation.address) {
            // A violated native allocation contract disables the adapter. Never
            // guess the ownership of a foreign pointer or run a particle program.
            stopRequested = true; status = Status::Unsupported; return {};
        }
        if (!Write(pointer + 0xE28, sequence)) { release(pool.arena, buffer); return {}; }
        // Registration is synchronous and does not execute bytecode. Borrow only
        // the canonical resource association; publish our program before returning
        // to the game's VM. None of actor+0xE90..0xE9F is written here.
        reinterpret_cast<int(__cdecl*)(std::uint32_t, int, int, std::uint32_t)>(base + 0x3FD710)(state.root, 0, 60, key);
        std::uint32_t record = 0; unsigned slot = 0;
        if (Find(key, record, slot) != 1 || record < pool.records ||
            record >= pool.records + pool.capacity * 256 || ((record - pool.records) & 255)) {
            release(pool.arena, buffer); stopRequested = true; return {};
        }
        // Equivalent to native type-9 construction, with allocation failure and
        // side-list capacity checked before any native mutation.
        std::array<unsigned char, 8> drawHeader{};
        NativeUiSupport::Copy(drawHeader.data(), reinterpret_cast<void*>(record + 0xC4), drawHeader.size());
        const auto bytes = MakeRing(drawHeader);
        NativeUiSupport::Copy(reinterpret_cast<void*>(buffer), bytes.data(), bytes.size());
        Write(record + 0xA8, buffer);
        Write(record + 0xBC, buffer);
        const std::uint8_t type = 9; Write(record + 0xBB, type);
        const auto index = static_cast<std::uint16_t>((record - pool.records) / 256);
        reinterpret_cast<int(__cdecl*)(std::uint32_t, std::uint16_t)>(base + 0x400C80)(pool.address, index);
        const auto pc = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kPrograms[static_cast<unsigned>(visual)].words.data()));
        Write(record, pc);
        status = Status::Active;
        return {key, record, buffer, false};
    }
};

void Tick() noexcept {
    if (!installed.load() || cleanupDepth.load() || ownerThread.load() != GetCurrentThreadId() || ticking) return;
    ticking = true;
    const auto currentEpoch = epoch.load();
    std::uint32_t root = 0;
    if (!Read(base + kRoot, root)) root = 0;
    if (observedEpoch != currentEpoch || (state.root && state.root != root)) {
        state = {}; rejectedRoot = 0; observedEpoch = currentEpoch;
    }
    if (!state.root && root && !stopRequested.load() && Resource(root)) state.root = root;
    if (state.root && root == state.root) {
        NativeIo io;
        for (unsigned actor = 0; actor < kActors; ++actor) {
            auto pointer = state.actors[actor].actor;
            std::uint8_t mask = 0;
            if (!Actor(actor, pointer, mask)) mask = 0;
            ReconcileActor(state, actor, pointer, mask, !stopRequested.load(), io);
        }
    }
    ticking = false;
}
int __cdecl ProducerShim(unsigned actor, void* pointer) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base;
    const bool nativeCaller = caller == 0x393B98 || caller == 0x3A726A || caller == 0x3B7D70;
    const auto result = reinterpret_cast<int(__cdecl*)(unsigned, void*)>(originals[0])(actor, pointer);
    std::uint8_t mask = 0;
    if (nativeCaller && !cleanupDepth.load() && !stopRequested.load() && Actor(actor, reinterpret_cast<std::uintptr_t>(pointer), mask)) {
        unsigned unbound = 0;
        ownerThread.compare_exchange_strong(unbound, GetCurrentThreadId());
        if (ownerThread.load() == GetCurrentThreadId()) {
            Tick();
            // Capture only after generation invalidation, including Holy-only
            // actors for which the vanilla four-bit loop created no effect.
            if (state.root) {
                NativeIo io;
                ReconcileActor(state, actor, reinterpret_cast<std::uintptr_t>(pointer), mask, true, io);
            } else {
                state.actors[actor].actor = reinterpret_cast<std::uintptr_t>(pointer);
            }
        }
    }
    return result;
}
void __cdecl CleanupShim() {
    cleanupDepth.fetch_add(1); epoch.fetch_add(1);
    reinterpret_cast<void(__cdecl*)()>(originals[1])();
    cleanupDepth.fetch_sub(1);
}
} // namespace

bool Start(std::uintptr_t module, bool enabled, bool validateOnly, void(*log)(const char*)) {
    if (installed.load()) return true;
    if (!enabled) { status = Status::Off; return false; }
    if (validateOnly) { status = Status::ValidateOnly; return false; }
    if (!NativeUiSupport::Profile(module, Evidence::spans)) { status = Status::Unsupported; return false; }
    base = module; logger = log; stopRequested = false;
    const std::uint32_t rvas[] = {kProducer, kCleanup};
    void* shims[] = {reinterpret_cast<void*>(&ProducerShim), reinterpret_cast<void*>(&CleanupShim)};
    if (!NativeUiSupport::Install(base, rvas, shims, originals, MinHookBatch::Owner::WeaponStrikeVfx,
                                 reinterpret_cast<const void*>(&Start))) { status = Status::Unsupported; return false; }
    installed = true; status = Status::Waiting;
    Notice("[ffx-hooks] Holy/Shadow weapon VFX: installed, waiting for admitted native battle resource\n");
    return true;
}
void TickMainThread() noexcept { Tick(); }
void RequestStop() noexcept { stopRequested = true; status = Status::Stopped; }
bool Installed() noexcept { return installed.load(); }
const char* Detail() noexcept {
    switch (status.load()) {
    case Status::Off: return "Off";
    case Status::ValidateOnly: return "Validation only";
    case Status::Unsupported: return "Unsupported executable or native contract";
    case Status::ResourceMismatch: return "Unsupported battle effect resource";
    case Status::Capacity: return "Waiting for native effect capacity";
    case Status::Active: return "Holy/Shadow weapon effects active";
    case Status::Stopped: return "Stopped; native cleanup owns pending effects";
    default: return "Waiting for a supported battle";
    }
}
} // namespace FfxHooks::WeaponStrikeVfx
