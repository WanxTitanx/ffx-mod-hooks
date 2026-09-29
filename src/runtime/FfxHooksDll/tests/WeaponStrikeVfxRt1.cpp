#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "PrivatePeFixture.h"
#include "../hooks/WeaponStrikeVfxRuntime.h"
#include "../hooks/WeaponStrikeVfxCore.h"
#include "../hooks/EquipmentWorkshopStore.h"

// Keep this harness independent of Workshop save/store behavior. Production
// links the existing fingerprint provider; the fixture uses the same SHA-256.
namespace FfxHooks::EquipmentWorkshop {
bool Fingerprint(const void* data, std::size_t size, Hash& out) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    const auto result = BCryptHash(algorithm, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)),
                                  static_cast<ULONG>(size), out.data(), static_cast<ULONG>(out.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result >= 0;
}
}
namespace V = FfxHooks::WeaponStrikeVfx;
static std::uintptr_t image = 0;
static std::array<unsigned char, 0xB0> pool{};
alignas(16) static std::array<unsigned char, 512 * 256> records{};
static std::array<std::int16_t, 512> active{}, draw{};
static std::array<unsigned char, V::kActors * 0xF90> actors{};
alignas(16) static std::array<unsigned char, 0x60000> heap{};
static std::vector<unsigned char> resource;
static unsigned registrations = 0, cleanups = 0;
static std::vector<int> vanillaPrograms;
template<class T> static void Put(std::uintptr_t at, T value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value)); }
template<class T> static T Get(std::uintptr_t at) { T value{}; std::memcpy(&value, reinterpret_cast<void*>(at), sizeof(value)); return value; }
template<class T> static std::uintptr_t Ptr(T& value) { return reinterpret_cast<std::uintptr_t>(value.data()); }
static std::uint32_t Arena() { return static_cast<std::uint32_t>(Ptr(heap) + heap.size()); }
static std::uintptr_t Slot(unsigned i) { return image + 0xEA40C0 + i * 32; }
static void Initialize() {
    active.fill(-1); draw.fill(-1); records.fill(0); pool.fill(0); actors.fill(0);
    std::fill(heap.begin(), heap.end(), 0);
    Put(Ptr(pool) + 16, static_cast<std::uint32_t>(Ptr(active)));
    Put(Ptr(pool) + 28, static_cast<std::uint32_t>(Ptr(draw)));
    Put(Ptr(pool) + 32, static_cast<std::uint32_t>(Ptr(records)));
    Put<std::uint16_t>(Ptr(pool) + 52, 512);
    Put(Ptr(pool) + 96, static_cast<std::uint32_t>(Ptr(resource)));
    Put(Ptr(pool) + 168, Arena());
    Put<std::uint32_t>(Arena() - 16, static_cast<std::uint32_t>(Ptr(heap)));
    Put<std::uint32_t>(Arena() - 12, static_cast<std::uint32_t>(Ptr(heap)));
    for (unsigned i = 0; i < 512; ++i) {
        std::memset(reinterpret_cast<void*>(Slot(i)), 0, 32); Put<std::uint32_t>(Slot(i), UINT32_MAX);
    }
    Put(image + 0xEA4080, static_cast<std::uint32_t>(Ptr(pool)));
    Put(image + 0xD2A95C, static_cast<std::uint32_t>(Ptr(resource)));
    Put(image + 0xD334CC, static_cast<std::uint32_t>(Ptr(actors)));
    Put(Ptr(resource) + 0x60, static_cast<std::uint32_t>(Ptr(resource) + 0x140));
    Put(Ptr(resource) + 0x74, static_cast<std::uint32_t>(Ptr(resource) + 0x1D40));
    Put(Ptr(resource) + 0x80, static_cast<std::uint32_t>(Ptr(resource) + 0x2B70));
    for (unsigned actor = 0; actor < V::kActors; ++actor) {
        const auto p = Ptr(actors) + actor * 0xF90;
        Put<std::uint16_t>(p + 0xE, static_cast<std::uint16_t>(actor));
        for (unsigned n = 0; n < 4; ++n) Put<std::uint32_t>(p + 0xE90 + n * 4, 0xDEAD0000 + n);
    }
    registrations = 0;
}
static int __cdecl Register(std::uint32_t root, int table, int program, std::uint32_t key) {
    assert(root == Ptr(resource) && table == 0 && program == 60);
    const auto index = ++registrations;
    assert(index < 511);
    const auto record = Ptr(records) + index * 256;
    Put(Slot(index), key); Put(Slot(index) + 4, static_cast<std::uint32_t>(record));
    Put<std::uint32_t>(record, root + 0x1284);
    Put<std::uint16_t>(record + 0x14, static_cast<std::uint16_t>(index));
    Put<std::uint32_t>(record + 0xC8, 0x80808080);
    active[index - 1] = static_cast<std::int16_t>(index);
    return static_cast<int>(index);
}
static int __cdecl VanillaQueue(std::uint32_t root, int table, int program, std::uint32_t) {
    assert(root == Ptr(resource) && table == 0); vanillaPrograms.push_back(program); return 1;
}
static void __cdecl Cleanup() {
    ++cleanups;
    for (unsigned i = 1; i <= registrations; ++i) {
        const auto record = Ptr(records) + i * 256;
        const auto buffer = Get<std::uint32_t>(record + 0xBC);
        if (buffer) reinterpret_cast<void(__cdecl*)(std::uint32_t, std::uint32_t)>(image + 0x3FF0F0)(Arena(), buffer);
        Put<std::uint32_t>(record, 0); Put<std::uint32_t>(record + 0xBC, 0);
        Put<std::uint32_t>(Slot(i), UINT32_MAX);
    }
    Put<std::uint32_t>(image + 0xD2A95C, 0);
}
static void Redirect(std::uintptr_t address, const void* function) {
    unsigned char bytes[5] = {0xE9};
    const auto delta = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(function) - address - 5);
    std::memcpy(bytes + 1, &delta, 4);
    std::memcpy(reinterpret_cast<void*>(address), bytes, 5);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 5);
}
using CallProducer = int(__cdecl*)();
static CallProducer Caller(std::uint32_t callRva, unsigned actor) {
    const auto entry = image + callRva - 10;
    auto* code = reinterpret_cast<unsigned char*>(entry);
    code[0] = 0x68; Put<std::uint32_t>(entry + 1, static_cast<std::uint32_t>(Ptr(actors) + actor * 0xF90));
    code[5] = 0x68; Put<std::uint32_t>(entry + 6, actor);
    code[10] = 0xE8; Put<std::int32_t>(entry + 11, static_cast<std::int32_t>(0x39ED60 - callRva - 5));
    code[15] = 0x83; code[16] = 0xC4; code[17] = 8; code[18] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), code, 19);
    return reinterpret_cast<CallProducer>(entry);
}
static DWORD WINAPI WrongThread(void* call) { reinterpret_cast<CallProducer>(call)(); V::TickMainThread(); return 0; }
int main(int argc, char** argv) {
    assert(argc == 4);
    const char* mode = argv[3];
    const auto module = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    assert(module && PrivatePeFixture::NormalizeRelocations(module));
    image = reinterpret_cast<std::uintptr_t>(module);
    DWORD old = 0;
    assert(VirtualProtect(module, 0x237D000, PAGE_EXECUTE_READWRITE, &old));
    std::ifstream input(argv[2], std::ios::binary);
    resource.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    assert(resource.size() == 330720);
    Initialize();
    if (!std::strcmp(mode, "off") || !std::strcmp(mode, "validate")) {
        std::array<unsigned char, 32> before{}; std::memcpy(before.data(), reinterpret_cast<void*>(image + 0x39ED60), 32);
        assert(!V::Start(image, std::strcmp(mode, "off") != 0, !std::strcmp(mode, "validate"), nullptr));
        assert(!std::memcmp(before.data(), reinterpret_cast<void*>(image + 0x39ED60), 32) && !V::Installed());
    } else if (!std::strcmp(mode, "signature")) {
        *reinterpret_cast<unsigned char*>(image + 0x410D40) ^= 1;
        assert(!V::Start(image, true, false, nullptr) && !V::Installed());
    } else {
        // The real producer, detours, native allocator, draw-list insertion and
        // stop routine execute. GPU/resource registration and battle cleanup are
        // fixture boundaries; no game process is launched.
        std::array<unsigned char, V::kActors * 0xF90> vanillaExpected{};
        if (!std::strcmp(mode, "vanilla")) {
            Redirect(image + 0x3FC200, reinterpret_cast<void*>(&VanillaQueue));
            Put<std::uint8_t>(Ptr(actors) + 0x5D9, 0x0F);
            std::memset(reinterpret_cast<void*>(Ptr(actors) + 0xE90), 0, 16);
            const auto native = reinterpret_cast<int(__cdecl*)(unsigned, void*)>(image + 0x39ED60);
            assert(native(0, actors.data()) == 4);
            assert((vanillaPrograms == std::vector<int>{60,61,62,63}));
            vanillaExpected = actors;
            Initialize(); vanillaPrograms.clear();
        }
        assert(V::Start(image, true, false, nullptr));
        Redirect(image + 0x3FD710, reinterpret_cast<void*>(&Register));
        // Keep the cleanup detour's original trampoline; its relocated initial
        // call reaches a private callback at the native FreeAllWeaponEffects site.
        // Replace the original body's tail with return after the first call.
        Redirect(image + 0x3FCA10, reinterpret_cast<void*>(&Cleanup));
        *reinterpret_cast<unsigned char*>(image + 0x3FB096) = 0x5E; // pop esi
        *reinterpret_cast<unsigned char*>(image + 0x3FB097) = 0xC3; // ret
        FlushInstructionCache(GetCurrentProcess(), module, 0x237D000);
        const auto actor = Ptr(actors);
        Put<std::uint8_t>(actor + 0x5D9, 0x90);
        if (!std::strcmp(mode, "vanilla")) {
            Put<std::uint8_t>(actor + 0x5D9, 0x0F);
            std::memset(reinterpret_cast<void*>(actor + 0xE90), 0, 16);
            assert(Caller(0x393B93, 0)() == 4 && actors == vanillaExpected && registrations == 0);
            assert((vanillaPrograms == std::vector<int>{60,61,62,63}));
            Put<std::uint8_t>(actor + 0x5D9, 0);
            Put<std::uint8_t>(actor + 0x5EC, 100); // Blindness Darkstrike does not request elemental Shadow.
            V::TickMainThread(); assert(registrations == 0);
            std::printf("WeaponStrikeVfx RT1 vanilla: PASS (four programs, return, actor bytes and blindness preserved)\n");
            return 0;
        }
        if (!std::strcmp(mode, "foreign")) resource[0x1284] ^= 1;
        if (!std::strcmp(mode, "oom")) {
            Put<std::uint32_t>(Arena() - 16, Arena() - 64);
            Put<std::uint32_t>(Arena() - 12, Arena() - 64);
        }
        if (!std::strcmp(mode, "readonly")) {
            DWORD ignored = 0; assert(VirtualProtect(reinterpret_cast<void*>(Slot(0)), 0x4000, PAGE_READONLY, &ignored));
        }
        auto before = actors;
        const auto producer = reinterpret_cast<int(__cdecl*)(unsigned, void*)>(image + 0x39ED60);
        if (!std::strcmp(mode, "caller")) assert(producer(0, reinterpret_cast<void*>(actor)) == 0);
        else assert(Caller(0x393B93, 0)() == 0);
        const bool rejected = !std::strcmp(mode, "foreign") || !std::strcmp(mode, "oom") || !std::strcmp(mode, "readonly") || !std::strcmp(mode, "caller");
        if (rejected) {
            assert(registrations == 0 && actors == before && Get<std::uint16_t>(Arena() - 2) == 0);
        } else {
            assert(registrations == 2 && draw[0] == 1 && draw[1] == 2 && Get<std::uint16_t>(Arena() - 2) == 2);
            before[0xE28] = actors[0xE28]; before[0xE29] = actors[0xE29];
            assert(actors == before); // Four native slots and every other actor byte are intact.
            for (unsigned i = 1; i <= 2; ++i) {
                const auto record = Ptr(records) + i * 256;
                assert(Get<std::uint8_t>(record + 0xBB) == 9);
                const auto buffer = Get<std::uint32_t>(record + 0xA8);
                assert(buffer == Get<std::uint32_t>(record + 0xBC) && Get<std::uint16_t>(buffer) == 16);
                assert(Get<std::uint32_t>(buffer + 8) == 0x80808080);
            }
            V::TickMainThread(); assert(registrations == 2);
            if (!std::strcmp(mode, "epoch")) {
                reinterpret_cast<void(__cdecl*)()>(image + 0x3FB090)();
                assert(cleanups == 1 && Get<std::uint16_t>(Arena() - 2) == 0);
                Initialize(); Put<std::uint8_t>(Ptr(actors) + 0x5D9, 0x90);
                assert(Caller(0x393B93, 0)() == 0 && registrations == 2);
                assert(Get<std::uint16_t>(Arena() - 2) == 2); // Same root/pool addresses, new battle generation.
            }
            if (!std::strcmp(mode, "thread")) {
                Put<std::uint8_t>(Ptr(actors) + 0xF90 + 0x5D9, 0x90);
                const auto workerCall = Caller(0x3A7265, 1);
                auto thread = CreateThread(nullptr, 0, &WrongThread, reinterpret_cast<void*>(workerCall), 0, nullptr);
                assert(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread);
                assert(registrations == 2 && Get<std::uint16_t>(Ptr(actors) + 0xF90 + 0xE28) == 0);
            }
            Put<std::uint8_t>(actor + 0x5D9, 0x10);
            V::TickMainThread();
            assert(!(Get<std::uint16_t>(Ptr(records) + 256 + 0xB0) & 0x4000));
            assert(Get<std::uint16_t>(Ptr(records) + 512 + 0xB0) & 0x4000);
            V::RequestStop(); V::TickMainThread();
            assert(Get<std::uint16_t>(Ptr(records) + 256 + 0xB0) & 0x4000);
            reinterpret_cast<void(__cdecl*)()>(image + 0x3FB090)();
            assert(cleanups == (!std::strcmp(mode, "epoch") ? 2u : 1u) && Get<std::uint16_t>(Arena() - 2) == 0);
        }
    }
    std::printf("WeaponStrikeVfx RT1 %s: PASS\n", mode);
}
