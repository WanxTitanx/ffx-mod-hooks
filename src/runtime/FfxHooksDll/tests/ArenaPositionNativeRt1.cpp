#include "../hooks/CustomMixRuntime.h"
#include "../hooks/MinHookBatchCoordinator.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <MinHook.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

using namespace FfxHooks::CustomMixUltra;
namespace R = FfxHooks::CustomMixUltra::Runtime;
namespace P = FfxHooks::ArenaPositions;
static int checks = 0, failures = 0;
static void Check(bool ok, const char *label)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", label);
    }
}
static std::vector<unsigned char> Read(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static unsigned char *MapPe(const std::vector<unsigned char> &file)
{
    if (file.size() < 0x1000u)
        return nullptr;
    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(file.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > file.size())
        return nullptr;
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(file.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.SizeOfImage < 0x00D2C264u ||
        nt->OptionalHeader.SizeOfHeaders > file.size())
        return nullptr;
    auto *image = static_cast<unsigned char *>(VirtualAlloc(
        nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!image)
        return nullptr;
    std::memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (static_cast<uint64_t>(section[i].PointerToRawData) + section[i].SizeOfRawData >
                file.size() ||
            static_cast<uint64_t>(section[i].VirtualAddress) + section[i].SizeOfRawData >
                nt->OptionalHeader.SizeOfImage)
            return nullptr;
        std::memcpy(image + section[i].VirtualAddress, file.data() + section[i].PointerToRawData,
                    section[i].SizeOfRawData);
    }
    const uint32_t delta =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image)) - nt->OptionalHeader.ImageBase;
    const auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (static_cast<uint64_t>(dir.VirtualAddress) + dir.Size > nt->OptionalHeader.SizeOfImage)
        return nullptr;
    for (uint32_t cursor = 0; cursor < dir.Size;)
    {
        auto *block =
            reinterpret_cast<IMAGE_BASE_RELOCATION *>(image + dir.VirtualAddress + cursor);
        if (block->SizeOfBlock < 8u || block->SizeOfBlock > dir.Size - cursor)
            return nullptr;
        auto *fix = reinterpret_cast<uint16_t *>(block + 1);
        for (uint32_t i = 0; i < (block->SizeOfBlock - 8u) / 2u; ++i)
        {
            const auto type = fix[i] >> 12;
            if (type == IMAGE_REL_BASED_ABSOLUTE)
                continue;
            const uint32_t rva = block->VirtualAddress + (fix[i] & 0xFFFu);
            if (type != IMAGE_REL_BASED_HIGHLOW || rva > nt->OptionalHeader.SizeOfImage - 4u)
                return nullptr;
            *reinterpret_cast<uint32_t *>(image + rva) += delta;
        }
        cursor += block->SizeOfBlock;
    }
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    // No entry point, TLS callback, imported function or game loop is invoked.
    return image;
}
static bool Nested(void *, int *result)
{
    *result = -31;
    return true;
}
using Getter = int(__cdecl *)(int, uintptr_t, int, int, int, float *);
static Getter probeOriginal = nullptr;
static unsigned probeCalls = 0;
static int __cdecl Probe(int mode, uintptr_t actor, int area, int role, int slot, float *output)
{
    ++probeCalls;
    return probeOriginal(mode, actor, area, role, slot, output);
}
static void CheckNativeHookActivation(uintptr_t base)
{
    namespace B = FfxHooks::MinHookBatch;
    const uintptr_t target = base + P::kAccessorRva;
    const auto getter = reinterpret_cast<Getter>(target);
    float before[4] = {}, hooked[4] = {}, after[4] = {};
    const int expected = getter(0, 0, 0, 5, 0, before);
    Check(B::EnsureProcessInitialized() == B::InitializationResult::Ready,
          "real MinHook is initialized through the production coordinator");
    const auto create = MH_CreateHook(reinterpret_cast<void *>(target),
                                     reinterpret_cast<void *>(&Probe),
                                     reinterpret_cast<void **>(&probeOriginal));
    Check(create == MH_OK, "real MinHook can create the exact relocated native accessor");
    if (create != MH_OK)
        return;
    const auto enabled = B::EnableBatch(&B::ProcessCoordinator(), B::RuntimeBatchIo(),
                                       B::Owner::ArenaPositions, &target, 1u);
    Check(enabled.result == B::BatchResult::Applied && enabled.queuedEnableCount == 1u,
          "ArenaPositions owner activates the actual native accessor through MinHook");
    Check(getter(0, 0, 0, 5, 0, hooked) == expected && probeCalls == 1u &&
              std::memcmp(before, hooked, sizeof(before)) == 0,
          "enabled native detour reaches its trampoline exactly once with unchanged ABI/data");
    const auto retired = B::NeutralizeBatch(&B::ProcessCoordinator(), B::RuntimeBatchIo(),
                                           B::Owner::ArenaPositions, &target, 1u);
    Check(retired.result == B::BatchResult::Neutralized && retired.exactDisabled,
          "ArenaPositions owner retires the real hook through the production coordinator");
    Check(getter(0, 0, 0, 5, 0, after) == expected && probeCalls == 1u &&
              std::memcmp(before, after, sizeof(before)) == 0 &&
              P::AccessorSignatureMatches(reinterpret_cast<const uint8_t *>(target),
                                          P::kAccessorSignatureBytes, base),
          "retirement restores native bytes and stops routing calls through the detour");
    // This fixture owns a private PE and has no concurrent game callbacks or DLL unload.
    Check(MH_RemoveHook(reinterpret_cast<void *>(target)) == MH_OK,
          "isolated harness releases its retired fixture trampoline");
    Check(MH_Uninitialize() == MH_OK, "isolated harness releases its private MinHook heap");
}
using SceneConsumer = int(__cdecl*)();
static int __cdecl CaptureSceneArgument(int value) { return value; }
struct SceneContext { SceneConsumer first; int observed = -1; };
static bool ReadSceneDuringInit(void* raw, int* result) {
    auto& context = *static_cast<SceneContext*>(raw);
    context.observed = context.first();
    *result = -31;
    return true;
}
static void CheckSceneryConsumers(unsigned char* image, unsigned char* carrier) {
    namespace S = FfxHooks::ArenaScenery;
    const auto base = reinterpret_cast<uintptr_t>(image);
    auto* field = reinterpret_cast<uint32_t*>(image + 0xD2C254u);
    image[0xD2C258u] = image[0xD2C259u] = 0;
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 64u,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(code != nullptr, "private native scene-consumer fragments allocate");
    if (!code) return;
    // Reviewed native consumers, copied from the relocated exact PE. Replace only
    // the scene-loader call with a value-capture stub; no renderer or game loop runs.
    Check(image[0x383FC0u]==0x0F && image[0x383FC1u]==0xBF &&
          image[0x383FC7u]==0x50 && image[0x383FC8u]==0xE8 &&
          image[0x383236u]==0x8B && image[0x38323Fu]==0x81,
          "native InitScene and caller consumer opcodes match the reviewed evidence");
    std::memcpy(code, image + 0x383FC0u, 13u);
    const uint32_t call = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&CaptureSceneArgument) -
        reinterpret_cast<uintptr_t>(code + 13u));
    std::memcpy(code + 9u, &call, 4u);
    const unsigned char tail[] = {0x83,0xC4,0x04,0xC3};
    std::memcpy(code + 13u, tail, sizeof(tail));
    std::memcpy(code + 32u, image + 0x383236u, 6u); // mov ecx,[native battlefield word]
    std::memcpy(code + 38u, image + 0x38323Fu, 6u); // and ecx,0x3ff
    code[44]=0x8B;code[45]=0xC1;code[46]=0xC3;     // return the native resource selector
    FlushInstructionCache(GetCurrentProcess(), code, 64u);
    const auto first = reinterpret_cast<SceneConsumer>(code);
    const auto second = reinterpret_cast<SceneConsumer>(code + 32u);
    for (unsigned i=1; i<S::kChoiceCount; ++i) {
        *field = 0x00470460u;
        R::StartProduction(base,true,false);
        SelectionInput selection{};selection.activationCount=3;selection.scenery=static_cast<S::Choice>(i);
        Check(R::ProductionArmSelection(selection,10), "selected arena arms through production runtime");
        SceneContext context{first};
        const auto result=R::RunProductionBattle({&context,&ReadSceneDuringInit},11);
        const auto desired=S::Get(selection.scenery)->battlefieldId;
        Check(result.transaction.result==TransactionResult::Restored && context.observed==desired &&
              result.originalResult==-31 && result.transaction.battlefield.active,
              "native InitScene consumer receives selected scenery and preserves original return");
        Check(second()==(desired&0x3FF) && (*field>>16)==71u,
              "later native caller loads selected scenery after formation restoration; routing preserved");
        R::ProductionRequestStop();R::ProductionResetAfterDrain();
        Check(*field==0x00470460u,"drained production teardown restores its owned uint16 scenery");
    }
    SelectionInput selection{};selection.activationCount=3;selection.scenery=S::Choice::Bikanel;
    R::StartProduction(base,true,false);*field=0x00480460u;
    Check(R::ProductionArmSelection(selection,20),"wrong-field fixture arms a value-only request");
    SceneContext wrong{first};const auto rejected=R::RunProductionBattle({&wrong,&ReadSceneDuringInit},21);
    Check(rejected.transaction.result==TransactionResult::BattlefieldUnavailable && *field==0x00480460u,
          "noncarrier field context cannot borrow scenery write authority");
    R::ProductionRequestStop();R::ProductionResetAfterDrain();
    R::StartProduction(base,true,false);*field=0x00470460u;
    Check(R::ProductionArmSelection(selection,30),"foreign-write fixture arms");
    SceneContext foreign{first};R::RunProductionBattle({&foreign,&ReadSceneDuringInit},31);
    *field=0x00470430u;R::ProductionRequestStop();R::ProductionResetAfterDrain();
    Check(*field==0x00470430u,"teardown preserves a foreign native scenery change");
    R::StartProduction(base,true,false);*field=0x00470460u;
    Check(R::ProductionArmSelection(selection,40),"native-next-battle fixture arms");
    SceneContext next{first};R::RunProductionBattle({&next,&ReadSceneDuringInit},41);
    const auto retained=*field;R::RunProductionBattle({nullptr,&Nested},42);
    R::ProductionRequestStop();R::ProductionResetAfterDrain();
    Check(*field==retained,"a new native battle selecting the same value takes ownership without stale restore");
    R::StartProduction(base,true,false);selection.scenery=static_cast<S::Choice>(0xFFu);
    Check(!R::ProductionArmSelection(selection,50),"unknown scenery rejects before native queue/write");
    R::ProductionRequestStop();R::ProductionResetAfterDrain();
    Check(*reinterpret_cast<uint32_t*>(image+0xD2A9A8u)==reinterpret_cast<uintptr_t>(carrier),
          "scenery selection never replaces the borrowed carrier allocation");
    VirtualFree(code,0,MEM_RELEASE);
}
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    auto file = Read(argv[1]);
    auto bytes = Read(argv[2]);
    auto *image = MapPe(file);
    auto *carrier = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, 0x5000u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(image && carrier && bytes.size() == 0x4428u,
          "private native getter fixture maps the reviewed PE32 and exact carrier");
    if (!image || !carrier || bytes.size() != 0x4428u)
        return 2;
    std::memcpy(carrier, bytes.data(), bytes.size());
    const auto base = reinterpret_cast<uintptr_t>(image);
    *reinterpret_cast<uint16_t *>(image + 0xD2A9A6u) = 0x4428u;
    *reinterpret_cast<uint32_t *>(image + 0xD2A9A8u) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(carrier));
    *reinterpret_cast<uint32_t *>(image + 0xD2A9B0u) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(carrier + 0x3F88u));
    std::memcpy(image + 0xD2C25Au, "dome02_00", 10u);
    Check(P::AccessorSignatureMatches(image + 0x3AC000u, 26u, base),
          "native accessor signature accepts relocation");
    for (unsigned i = 0; i < 26; ++i)
    {
        image[0x3AC000u + i] ^= 1u;
        Check(!P::AccessorSignatureMatches(image + 0x3AC000u, 26u, base),
              "every signature byte is significant");
        image[0x3AC000u + i] ^= 1u;
    }
    CheckNativeHookActivation(base);
    const auto getter = reinterpret_cast<Getter>(image + 0x3AC000u);
    R::StartProduction(base, true, false);
    SelectionInput selection{};
    selection.activationCount = 8;
    selection.positions = P::Generate(8);
    Check(R::ProductionArmSelection(selection, 10u), "valid positioned mix arms");
    const auto battle = R::RunProductionBattle({nullptr, &Nested}, 11u);
    Check(battle.originalResultAvailable && battle.originalResult == -31 &&
              battle.transaction.restored,
          "mix initialization restores its borrowed buffer and preserves native int result");
    Check(std::memcmp(carrier, bytes.data(), bytes.size()) == 0,
          "no battle bytes remain modified after initialization");
    for (int slot = 0; slot < 8; ++slot)
    {
        float output[4] = {};
        const int result = getter(0, 0, 0, 5, slot, output);
        const float y = output[1], w = output[3];
        Check(result == 0, "actual native role5 getter accepts the exact slot ABI");
        Check(R::ProductionPositionRead(result, 0, 0, 0, 5, slot, output) &&
                  output[0] == selection.positions.points[slot].x &&
                  output[2] == selection.positions.points[slot].z && output[1] == y &&
                  output[3] == w,
              "late position reads receive the chosen X/Z while native Y/W survive");
    }
    std::array<unsigned char, 0x700> actor{};
    *reinterpret_cast<uint16_t *>(actor.data() + 0xE) = 0x114Eu;
    actor[0x6D4] = 0xFF;
    float output[4] = {};
    int result = getter(0, reinterpret_cast<uintptr_t>(actor.data()), 0, 5, 0, output);
    Check(R::ProductionPositionRead(result, 0, reinterpret_cast<uintptr_t>(actor.data()), 0, 5, 0,
                                    output),
          "matching live monster actors use the selected position");
    *reinterpret_cast<uint16_t *>(actor.data() + 0xE) = 0x114Fu;
    Check(!R::ProductionPositionRead(0, 0, reinterpret_cast<uintptr_t>(actor.data()), 0, 5, 0,
                                     output),
          "another actor ID cannot borrow slot authority");
    *reinterpret_cast<uint16_t *>(actor.data() + 0xE) = 0x114Eu;
    actor[0x6D4] = 0;
    Check(!R::ProductionPositionRead(0, 0, reinterpret_cast<uintptr_t>(actor.data()), 0, 5, 0,
                                     output),
          "cached animation/world positions remain native");
    for (const auto args : std::array<std::array<int, 5>, 6>{{{{-1, 0, 0, 5, 0}},
                                                              {{0, 1, 0, 5, 0}},
                                                              {{0, 0, 1, 5, 0}},
                                                              {{0, 0, 0, 3, 0}},
                                                              {{0, 0, 0, 5, -1}},
                                                              {{0, 0, 0, 5, 8}}}})
        Check(!R::ProductionPositionRead(args[0], args[1], 0, args[2], args[3], args[4], output),
              "error/setter/party/other-area/out-of-range reads pass through");
    bool otherThread = true;
    std::thread thread([&] { otherThread = R::ProductionPositionRead(0, 0, 0, 0, 5, 0, output); });
    thread.join();
    Check(!otherThread, "another thread cannot use battle position ownership");
    image[0xD2C25Au] = 'x';
    Check(!R::ProductionPositionRead(0, 0, 0, 0, 5, 0, output),
          "a changed encounter identity closes readback");
    image[0xD2C25Au] = 'd';
    Check(
        !R::ProductionPositionRead(0, 0, 0, 0, 5, 0, reinterpret_cast<float *>(carrier + 0x41B8u)),
        "output aliases cannot turn a read override into a persistent carrier write");
    float nativeSet[4] = {30, 7, 150, 9};
    result = getter(1, 0, 0, 5, 0, nativeSet);
    R::ProductionPositionSet(0, 0);
    getter(0, 0, 0, 5, 0, output);
    Check(result == 0 && !R::ProductionPositionRead(0, 0, 0, 0, 5, 0, output) && output[0] == 30 &&
              output[2] == 150,
          "a native battle-script position setter takes ownership back from the custom layout");
    getter(0, 0, 0, 5, 1, output);
    Check(R::ProductionPositionRead(0, 0, 0, 0, 5, 1, output),
          "a native setter does not disarm unrelated monster slots");
    std::memcpy(carrier, bytes.data(), bytes.size());
    R::RunProductionBattle({nullptr, &Nested}, 12u);
    Check(!R::ProductionPositionRead(0, 0, 0, 0, 5, 0, output),
          "the next battle cannot inherit the old mix even at the same carrier address");
    Check(R::ProductionArmSelection(selection, 20u), "a fresh generation rearms normally");
    R::RunProductionBattle({nullptr, &Nested}, 21u);
    R::ProductionClearPositionBattle();
    Check(!R::ProductionPositionRead(0, 0, 0, 0, 5, 0, output),
          "OFF/nonbattle transitions clear position ownership");
    Check(R::ProductionArmSelection(selection, 30u), "final stop fixture rearms");
    R::RunProductionBattle({nullptr, &Nested}, 31u);
    R::ProductionRequestStop();
    Check(!R::ProductionPositionRead(0, 0, 0, 0, 5, 0, output),
          "loader-safe stop closes the late-reader path");
    R::ProductionResetAfterDrain();
    Check(std::memcmp(carrier, bytes.data(), bytes.size()) == 0,
          "all native getter experiments preserve the full carrier asset");
    CheckSceneryConsumers(image, carrier);
    Check(std::memcmp(carrier, bytes.data(), bytes.size()) == 0,
          "all scenery experiments preserve the complete carrier asset");
    VirtualFree(carrier, 0, MEM_RELEASE);
    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("ArenaPositionNativeRt1: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
