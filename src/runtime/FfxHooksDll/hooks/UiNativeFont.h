#pragma once
// Jarvis-HOOK: immutable, DLL-owned copies of the installed game's font pixels.
// No native font table, save, resource route or game language is changed.
#include "UiLanguage.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace FfxHooks::UiNativeFont {
inline constexpr unsigned GlyphWidth=56,GlyphHeight=72;
struct Glyph {
    std::array<std::uint32_t,GlyphWidth*GlyphHeight> pixels{}; // straight ARGB
    float advance=0; // native 512x416 coordinate space
};
class Library {
    std::unordered_map<std::uint32_t,Glyph> glyphs_;
public:
    static std::unique_ptr<Library> Load(const std::filesystem::path& archive,UiLanguage::Locale locale);
    const Glyph* Find(std::uint32_t scalar) const noexcept {
        const auto at=glyphs_.find(scalar);return at==glyphs_.end()?nullptr:&at->second;
    }
    std::size_t Size() const noexcept {return glyphs_.size();}
};

#ifdef _WIN32
using LogFn=void(*)(const char*);
// Configure once on the existing worker. Request only queues bounded read-only
// work; Present never reads/decompresses archives or waits for a worker.
void Configure(const std::filesystem::path& archive,LogFn log=nullptr) noexcept;
void Request(UiLanguage::Locale locale) noexcept;
const Library* Current(UiLanguage::Locale locale) noexcept;
bool Loading(UiLanguage::Locale locale) noexcept;
void Stop() noexcept;
#endif
}
