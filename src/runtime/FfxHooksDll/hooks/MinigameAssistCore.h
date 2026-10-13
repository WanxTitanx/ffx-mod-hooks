#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::MinigameAssist {
enum Option : unsigned {Butterflies=1,Chocobos=2};
struct Script {
    const char* field;
    std::uint16_t room;
    unsigned option;
    std::uint32_t codeOffset,codeBytes;
    std::array<unsigned char,32> codeHash;
};
struct Rule {std::uint8_t script;std::uint32_t offset;std::uint16_t original,replacement;const char* purpose;};
inline constexpr std::size_t kScriptCount=4,kRuleCount=39,kMaximumCodeBytes=256*1024;
const std::array<Script,kScriptCount>& Scripts() noexcept;
const std::array<Rule,kRuleCount>& Rules() noexcept;
const Rule* FindRule(unsigned script,std::uint32_t offset,std::uint32_t decoded,unsigned options) noexcept;
}
