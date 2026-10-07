#include "../hooks/F8RuntimeCore.h"
#if __has_include("../hooks/ExecutableStartupGate.h")
#include "../hooks/ExecutableStartupGate.h"
#else
// Reproduce the missing common admission boundary without inventing a mock PE parser.
namespace FfxHooks::ExecutableStartup {
bool CanStart(bool, const std::uint8_t* header, std::size_t size,
              F8Runtime::ExecutableIdentity* identity) noexcept {
    return F8Runtime::ParseExecutableIdentity(header, size, identity) == F8Runtime::ProfileResult::Supported;
}
}
#endif
#include <array>
#include <cstdio>
#include <cstring>
#include "ExecutableFixtureIdentity.h"

namespace {
int checks = 0, failures = 0;
void Expect(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", label); }
}
template<typename T>
void Put(std::array<std::uint8_t, 0x1000>& image, std::size_t at, T value) {
    std::memcpy(image.data() + at, &value, sizeof(value));
}
std::array<std::uint8_t, 0x1000> SupportedHeader() {
    std::array<std::uint8_t, 0x1000> image{};
    image[0] = 'M'; image[1] = 'Z';
    Put<std::uint32_t>(image, 0x3c, 0x80);
    image[0x80] = 'P'; image[0x81] = 'E';
    Put<std::uint16_t>(image, 0x84, 0x14c);
    Put<std::uint32_t>(image, 0x88, ExecutableFixtureIdentity::Timestamp);
    Put<std::uint16_t>(image, 0x94, 0xe0);
    Put<std::uint16_t>(image, 0x98, 0x10b);
    Put<std::uint32_t>(image, 0x98 + 56, 0x237d000);
    return image;
}
}

int main() {
    using FfxHooks::ExecutableStartup::CanStart;
    auto header = SupportedHeader();
    FfxHooks::F8Runtime::ExecutableIdentity identity{};
    Expect(CanStart(true, header.data(), header.size(), &identity), "the exact selected FFX host is admitted");
    Expect(!CanStart(false, header.data(), header.size(), &identity), "matching PE in a non-FFX host must reject all feature startup");
    Expect(!CanStart(false, nullptr, 0, nullptr), "absent FFX host must reject without reading a header");
    const auto otherTimestamp=::FfxHooks::ExecutableProfile::Steam20261001?0x55D2F3CCu:0x6AA2219Cu;
    Put<std::uint32_t>(header, 0x88, otherTimestamp);
    Expect(!CanStart(true, header.data(), header.size(), &identity), "another build cannot use the selected native addresses");
    Expect(identity.timestamp == otherTimestamp, "rejected build retains its identity for diagnostics");
    header = SupportedHeader();
    Put<std::uint16_t>(header, 0x84, 0x8664);
    Expect(!CanStart(true, header.data(), header.size(), &identity), "wrong machine fails closed");
    header = SupportedHeader();
    Put<std::uint32_t>(header, 0x98 + 56, 0x237c000);
    Expect(!CanStart(true, header.data(), header.size(), &identity), "wrong image extent fails closed");
    header = SupportedHeader(); header[0] = 'N';
    Expect(!CanStart(true, header.data(), header.size(), &identity), "malformed DOS header fails closed");
    header = SupportedHeader();
    Expect(!CanStart(true, header.data(), 0x40, &identity), "truncated header fails closed");
    Expect(!CanStart(true, nullptr, 0, nullptr), "unreadable FFX header fails closed");
    std::printf("STARTUP ADMISSION RT0: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
