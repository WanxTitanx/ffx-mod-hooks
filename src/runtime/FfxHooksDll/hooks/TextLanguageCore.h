#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace FfxHooks::TextLanguage {
inline constexpr char ExecutableSha256[] = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced";
inline constexpr std::size_t MaxManifestBytes = 1024u * 1024u;
inline constexpr std::uint32_t MaxResourceBytes = 64u * 1024u * 1024u;
inline constexpr std::uint64_t MaxPackBytes = 256ull * 1024ull * 1024ull;
inline constexpr std::uint32_t HookApi = 2;
enum class Family { Menu, Battle, Metrics, Atlas, Event };
struct Resource {
    std::string id, request, path, sourceSha256, sha256, font;
    Family family = Family::Menu;
    std::uint32_t sourceSize = 0, size = 0;
};
struct Glyph {
    std::uint32_t unicode = 0;
    std::uint8_t code = 0, width = 0;
};
struct Font {
    std::string id, metrics;
    std::vector<std::string> atlases;
    std::vector<Glyph> glyphs;
};
struct Manifest {
    std::uint32_t schemaVersion = 1, hookApi = 1;
    std::string locale, displayName, packVersion;
    std::uint32_t baseLocale = 1;
    std::vector<Resource> resources;
    std::vector<Font> fonts;
};
// Parsing and encoding publish their output only after every check succeeds.
bool ParseManifest(std::string_view json, Manifest& output, std::string& error);
bool SafeRelativePath(std::string_view path);
// Normalized HD event tables also supply authored scene subtitles.
bool IsEventRequest(std::string_view request);
bool CanonicalRequest(std::string_view path, std::string& output);
const Resource* Resolve(const Manifest&, std::string_view request, std::uint32_t nativeLocale, bool readOnly);
bool EncodeLiteral(const Font&, std::string_view utf8, std::size_t capacity,
                   std::vector<std::uint8_t>& output, std::string& error);
} // namespace FfxHooks::TextLanguage
