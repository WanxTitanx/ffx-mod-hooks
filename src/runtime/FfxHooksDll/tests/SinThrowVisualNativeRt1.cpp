#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "MusicPeFixture.inc"

static unsigned checks, failures, positionCalls, boneCalls, boneIndex;
static void* expectedActor;
static void(__cdecl* nativePosition)(void*, float*, float*, float*);
static void Check(bool ok, const char* label) {
    ++checks;
    if (!ok && ++failures <= 12) std::printf("FAIL %s\n", label);
}
static void __cdecl Position(void* actor, float* x, float* y, float* z) {
    ++positionCalls;
    Check(actor == expectedActor, "native block forwards the actual caster object");
    nativePosition(actor, x, y, z);
}
static void __cdecl Bone(void*, unsigned index, void*, float* out) {
    ++boneCalls; boneIndex = index;
    out[0] = out[1] = out[2] = 0;
}
static unsigned RunTimer(void* code, unsigned actor) {
    unsigned result;
    __asm {
        push ebx
        mov eax, actor
        call code
        mov result, ebx
        pop ebx
    }
    return result;
}
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const auto game = Read(argv[1]);
    if (game.size() < 0x1000) return 2;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game.data());
    if (dos->e_lfanew <= 0 || static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > game.size()) return 2;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(game.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 0x6AA2219Cu ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return 2;
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    void* positionCode = nullptr;
    for (unsigned n = 0; n < nt->FileHeader.NumberOfSections; ++n) {
        const auto& s = sections[n];
        if (0x42AC90u < s.VirtualAddress || 0x42AC90u + 44u > s.VirtualAddress + s.SizeOfRawData) continue;
        const size_t at = s.PointerToRawData + 0x42AC90u - s.VirtualAddress;
        if (at + 44u > game.size()) return 2;
        positionCode = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!positionCode) return 2;
        // This exact leaf contains only local branches and actor-relative reads.
        std::memcpy(positionCode, game.data() + at, 44);
    }
    if (!positionCode) return 2;
    nativePosition = reinterpret_cast<decltype(nativePosition)>(positionCode);
    FlushInstructionCache(GetCurrentProcess(), positionCode, 44);
    for (int arg = 2; arg < argc; ++arg) {
        auto* image = MapPe(Read(argv[arg]));
        if (!image) return 2;
        // Private native blocks only: no DLL entrypoint, imports, renderer or game.
        auto* blocks = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!blocks) return 2;
        std::memcpy(blocks, image + 0x1397, 7); blocks[7] = 0xC3;
        std::memcpy(blocks + 64, image + 0x18CA, 38);
        const unsigned char finish[] = {0x83, 0xC4, 0x10, 0xC3};
        std::memcpy(blocks + 64 + 38, finish, sizeof(finish));
        std::uintptr_t host[0xB40 / 4] = {};
        host[0x418 / 4] = reinterpret_cast<std::uintptr_t>(&Position);
        host[0x3B4 / 4] = reinterpret_cast<std::uintptr_t>(&Bone);
        *reinterpret_cast<void**>(image + 0x900F0) = host;
        expectedActor = image + 0x10000;
        const float xyz[] = {11.25f, -23.5f, 47.75f};
        std::memcpy(static_cast<unsigned char*>(expectedActor) + 12, xyz, sizeof(xyz));
        *reinterpret_cast<void**>(image + 0x1D5170) = expectedActor;
        FlushInstructionCache(GetCurrentProcess(), blocks, 128);
        for (unsigned actor = 0; actor < 31; ++actor) {
            Check(RunTimer(blocks, actor) == 28, "owned monster timer is bounded independently of actor index");
            *reinterpret_cast<unsigned*>(image + 0x1D0498) = actor;
            positionCalls = boneCalls = boneIndex = 0;
            reinterpret_cast<void(__cdecl*)()>(blocks + 64)();
            Check(positionCalls == 1 && boneCalls == 0,
                  "owned origin uses actor position, never a player bone table");
            const auto* out = reinterpret_cast<float*>(image + 0x1D0480);
            Check(out[0] == 11.25f && out[1] == -23.5f && out[2] == 47.75f,
                  "all three output components and cdecl stack survive relocated execution");
            if (actor == 20 && boneCalls) std::printf("legacy actor20 bone selector=%u\n", boneIndex);
        }
        VirtualFree(blocks, 0, MEM_RELEASE); VirtualFree(image, 0, MEM_RELEASE);
    }
    VirtualFree(positionCode, 0, MEM_RELEASE);
    std::printf("SIN_THROW_VISUAL_NATIVE_RT1 %u/%u passed (native blocks; no graphics lifecycle)\n",
                checks - failures, checks);
    return failures ? 1 : 0;
}
