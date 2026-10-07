#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "../hooks/OriginalPs2RngCore.h"
#include "OriginalPs2RngReference.generated.h"
#include "MusicPeFixture.inc"
namespace R = FfxHooks::OriginalPs2Rng;
namespace V = OriginalPs2RngReference;
static unsigned checks = 0, failures = 0;
static void Check(bool ok, const char* label) {
    ++checks;
    if (!ok && ++failures < 12) std::printf("FAIL %s\n", label);
}
struct Profile { unsigned init, clock, indexed, normalStep, auxiliaryStep, counter;
                 unsigned normal, auxiliary, channels, multipliers, xors; };
static void ReturnValue(unsigned char* at, std::uint32_t low, std::uint32_t high = 0) {
    const unsigned char stub[] = {0xB8,0,0,0,0,0xBA,0,0,0,0,0xC3};
    std::memcpy(at,stub,sizeof(stub));std::memcpy(at+1,&low,4);std::memcpy(at+6,&high,4);
    FlushInstructionCache(GetCurrentProcess(),at,sizeof(stub));
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto file = Read(argv[1]);
    if (file.size() < 0x1000) return 2;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    if (dos->e_lfanew <= 0 || static_cast<std::size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>file.size()) return 2;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data()+dos->e_lfanew);
    const bool steam = nt->FileHeader.TimeDateStamp == 0x6AA2219Cu;
    if (!steam && nt->FileHeader.TimeDateStamp != 0x55D2F3CCu) return 2;
    const Profile p = steam
        ? Profile{0x398890,0x398950,0x3988F0,0x3989A0,0x3989D0,0x487E10,0x842200,0x842204,0xD35EE0,0x842208,0x842318}
        : Profile{0x3988A0,0x398960,0x398900,0x3989B0,0x3989E0,0x487D80,0x8421F0,0x8421F4,0xD35ED8,0x8421F8,0x842308};
    auto* image = MapPe(file);
    if (!image) return 2;
    Check(std::memcmp(image+p.multipliers,V::tables.multipliers.data(),68*4)==0,
          "native PC multipliers equal the pinned PS2 table");
    Check(std::memcmp(image+p.xors,V::tables.xorValues.data(),68*2)==0,
          "native PC XOR values equal the pinned PS2 table");
    using Init = unsigned(__cdecl*)(unsigned);
    using Next = unsigned(__cdecl*)();
    using Indexed = unsigned(__cdecl*)(unsigned);
    const auto init = reinterpret_cast<Init>(image+p.init);
    const auto normal = reinterpret_cast<Next>(image+p.normalStep);
    const auto auxiliary = reinterpret_cast<Next>(image+p.auxiliaryStep);
    const auto indexed = reinterpret_cast<Indexed>(image+p.indexed);
    auto* nativeChannels = reinterpret_cast<std::uint32_t*>(image+p.channels);
    auto& nativeNormal = *reinterpret_cast<std::uint32_t*>(image+p.normal);
    auto& nativeAuxiliary = *reinterpret_cast<std::uint32_t*>(image+p.auxiliary);
    // Only private fixture inputs are stubbed. The initializer and all three
    // generators run their actual executable bytes; the game entrypoint never runs.
    for (unsigned d = 0; d < 256; ++d) {
        ReturnValue(image+p.clock,d+1u);
        for (unsigned frame : {0u,1u,256u,65535u,0x80000000u,0xFFFFFFFFu}) {
            *reinterpret_cast<std::uint32_t*>(image+p.channels-4)=0xA55A1234u;
            *reinterpret_cast<std::uint32_t*>(image+p.channels+68*4)=0x5AA54321u;
            auto expected=R::Initialize(static_cast<std::uint8_t>(d),frame).state;
            (void)init(frame);
            Check(nativeNormal==expected.normal && nativeAuxiliary==expected.auxiliary,
                  "native initializer preserves both complete globals and warm-up order");
            Check(std::memcmp(nativeChannels,expected.channels.data(),68*4)==0,
                  "every actual native initial channel equals the pure model");
            Check(*reinterpret_cast<std::uint32_t*>(image+p.channels-4)==0xA55A1234u &&
                  *reinterpret_cast<std::uint32_t*>(image+p.channels+68*4)==0x5AA54321u,
                  "initialization does not overrun 68 native channels");
            for (unsigned channel=0;channel<68;++channel) {
                std::uint32_t result=0;R::AdvanceChannel(expected,V::tables,channel,result);
                Check(indexed(channel)==result && nativeChannels[channel]==expected.channels[channel],
                      "native indexed return and full stored state match for all 68 channels");
            }
            Check(normal()==R::AdvanceNormal(expected.normal) && nativeNormal==expected.normal,
                  "native normal generator matches after the channel expansion");
            ReturnValue(image+p.counter,frame,0xFEDCBA98u);
            Check(auxiliary()==R::AdvanceAuxiliary(expected.auxiliary,frame) && nativeAuxiliary==expected.auxiliary,
                  "native auxiliary generator uses the low counter word and retains full state");
        }
    }
    std::printf("ORIGINAL_PS2_RNG_NATIVE_MATH_RT1 %s %u/%u passed (input stubs; no game entrypoint)\n",
                steam?"steam":"legacy",checks-failures,checks);
    VirtualFree(image,0,MEM_RELEASE);
    return failures?1:0;
}
