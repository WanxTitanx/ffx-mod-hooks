#pragma once
#include "TextLanguageCore.h"
#include <array>
namespace FfxHooks::TextLanguage {
using Bytes = std::vector<std::uint8_t>;
using Advances = std::array<std::uint8_t, 256>;
bool ValidateTextReplacement(std::string_view request, const Bytes& source,
 const Bytes& replacement, const Font& font, const Advances& advances, std::string& error, std::uint32_t api = 3);
}
