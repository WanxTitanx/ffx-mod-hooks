#pragma once
#include "TextLanguagePack.h"
#include <cstdint>
#include <string>
#include <vector>

namespace FfxHooks::TextLanguage {
// Pins a complete package for one process lifetime. Native streams receive
// independent read cursors on those pinned files, never shared DuplicateHandles.
class Files {
public:
    Files() = default;
    ~Files();
    Files(const Files&) = delete;
    Files& operator=(const Files&) = delete;
    bool Initialize(const std::wstring& directory, std::string& error);
    bool Read(const Resource&, Bytes&) const;
    std::uintptr_t Open(const Resource&) const;
    const std::string& Json() const { return json_; }
    const Manifest& Description() const { return manifest_; }
private:
    void Close();
    std::vector<std::uintptr_t> directories_, files_;
    std::string json_;
    Manifest manifest_;
};
bool Fingerprint(const Bytes&, std::string&);
}
