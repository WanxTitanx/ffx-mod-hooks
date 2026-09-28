#pragma once
#include "TextLanguagePayload.h"

namespace FfxHooks::TextLanguage {
struct PackIo {
    void* context = nullptr;
    bool (*read)(void*, const Resource&, bool source, Bytes&) = nullptr;
    bool (*sha256)(void*, const Bytes&, std::string&) = nullptr;
};
struct PreparedPack {
    Manifest manifest;
    // Exactly the manifest order, immutable after admission.
    std::vector<Bytes> resources;
};
// An unsuccessful admission does not mutate an already prepared pack.
bool AdmitPack(std::string_view json, const PackIo&, PreparedPack&, std::string& error);
}
