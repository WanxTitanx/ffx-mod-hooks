#pragma once
#include "TextLanguagePayload.h"

namespace FfxHooks::TextLanguage {
struct PackIo {
    void* context = nullptr;
    bool (*read)(void*, const Resource&, bool source, Bytes&) = nullptr;
    bool (*sha256)(void*, const Bytes&, std::string&) = nullptr;
    // Optional: reported once for each text/UI-texture resource left native.
    void (*retain)(void*, const Resource&, const char* reason) = nullptr;
    // A 32-bit native kernel loader adapter is active (see NativeKernelLimit).
    bool extendedKernelTables = false;
};
struct PreparedPack {
    Manifest manifest;
    // Exactly the manifest order, immutable after admission. A retained entry has
    // no payload: its native/mod file stays in charge of that request.
    std::vector<Bytes> resources;
    std::vector<std::uint8_t> retained;
    std::size_t retainedCount = 0;
    // Conservative allowance for admitted kernel text growth, including native
    // 16-byte file and 64-byte accounting alignment. Retained files contribute zero.
    std::uint32_t kernelBudgetGrowth = 0;
};
// Text and UI-texture resources are independent: one whose native source changed
// (e.g. a mod redeploy) or that the native loader cannot hold is retained native
// instead of rejecting the pack. Font pages, pack payload integrity and the absence
// of any matching text still reject the whole pack.
// An unsuccessful admission does not mutate an already prepared pack.
bool AdmitPack(std::string_view json, const PackIo&, PreparedPack&, std::string& error);
}
