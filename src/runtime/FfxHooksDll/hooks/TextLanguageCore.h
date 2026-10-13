#include "../shared/ExecutableProfile.h"
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace FfxHooks::TextLanguage {
#ifdef FFXHOOKS_TARGET_STEAM_20261001
inline constexpr char ExecutableSha256[] = "0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d";
#else
inline constexpr char ExecutableSha256[] = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced";
#endif
inline constexpr std::size_t MaxManifestBytes = 1024u * 1024u;
inline constexpr std::uint32_t MaxResourceBytes = 64u * 1024u * 1024u;
inline constexpr std::uint64_t MaxPackBytes = 256ull * 1024ull * 1024ull;
inline constexpr std::uint32_t HookApi = 4;
enum class Family { Menu, Battle, Metrics, Atlas, Event, Graphic };
enum class TextContainer { Indexed, Field, Macro };
struct TextLayout {
    TextContainer container = TextContainer::Indexed;
    Family family = Family::Menu;
    std::uint32_t minimumApi = 0;
    std::uint16_t stride = 0;
    std::uint8_t slots = 0, offsetStep = 0;
};
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
    std::uint32_t profile = 1;
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
// Only exact, byte-verified text containers receive a layout. Input is canonical.
// Failure leaves the caller's descriptor unchanged.
bool DescribeTextRequest(std::string_view request, TextLayout& output);
bool CanonicalRequest(std::string_view path, std::string& output);
// Actual File Loader/VBF member: ffx_ps2/... for PS2 data, ffx_data/... for
// Phyre assets. The package's FFX_Data namespace is not a PS2 directory.
bool NativeSourceRequest(std::string_view path, std::string& output);
const Resource* Resolve(const Manifest&, std::string_view request, std::uint32_t nativeLocale, bool readOnly);
// FFX_Kernel_LoadFileToTable (legacy 0x381E00, Steam 0x381D40) copies each table
// through a 0x25800-byte buffer filled by 0x382EB0/0x382DF0, which rounds the size
// up to 16 bytes. command/ply_rom/a_ability/item keep that rounded size in a 16-bit
// global and allocate/copy only size & 0xFFFF bytes; a larger table is truncated.
inline constexpr std::uint32_t KernelStagingBytes = 0x25800u;
inline constexpr std::uint32_t KernelU16Limit = 0xFFF0u;
// Loader cases whose size the optional 32-bit adapter owns. ply_rom (case 1) keeps
// its native zero-size fallback; monmagic1/2 (cases 4/5) are already 32-bit.
struct KernelTable { int kind; const char* leaf; };
inline constexpr KernelTable ExtendedKernelTables[] = {{0, "command.bin"}, {2, "a_ability.bin"}, {3, "item.bin"}};
// Largest payload the native kernel loader holds for a canonical request, or 0.
// `extended` is true only while the 32-bit loader adapter owns the tables above.
std::uint32_t NativeKernelLimit(std::string_view request, bool extended);
// Bank decision for one adapter load. A bank the adapter allocated is reused while
// the new table fits, as the native loader does. Otherwise a fresh bank is
// allocated and the old one is retained, never freed: cached entry and string
// pointers may still reference it. A bank of unknown capacity (allocated before
// the adapter owned the table) is passed as 0 and is never written.
struct KernelBankPlan { bool allocate = false, retainOld = false; };
KernelBankPlan PlanKernelBank(bool bankPresent, std::uint32_t knownCapacity, std::uint32_t size);
bool EncodeLiteral(const Font&, std::string_view utf8, std::size_t capacity,
                   std::vector<std::uint8_t>& output, std::string& error);
// Plain name literals only. Control streams and unassigned glyphs are rejected.
bool DecodeLiteral(const Font&,const std::uint8_t*,std::size_t,std::string&);
} // namespace FfxHooks::TextLanguage
