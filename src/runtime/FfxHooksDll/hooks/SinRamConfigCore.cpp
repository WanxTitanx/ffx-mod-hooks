#include "SinRamConfigCore.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <cstdio>
#include "SinSpreadCore.h"

namespace FfxHooks::SinRamConfig {
namespace {

struct ParsedKey {
    char bytes[32] = {};
    std::size_t length = 0;
    bool nonAsciiOrTruncated = false;
};

struct ParsedNumber {
    std::uint64_t magnitude = 0;
    bool negative = false;
    bool nonIntegral = false;
    bool overflow = false;
};

char FoldAscii(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

bool EqualsIgnoreAsciiCase(const ParsedKey& key, const char* canonical) {
    if (!canonical || key.nonAsciiOrTruncated) return false;
    const std::size_t length = std::strlen(canonical);
    if (key.length != length) return false;
    for (std::size_t i = 0; i < length; ++i) {
        if (FoldAscii(key.bytes[i]) != FoldAscii(canonical[i])) return false;
    }
    return true;
}

void AppendAsciiKeyByte(ParsedKey* key, char value) {
    if (!key) return;
    if (key->length < sizeof(key->bytes)) {
        key->bytes[key->length] = value;
    } else {
        key->nonAsciiOrTruncated = true;
    }
    ++key->length;
}

void MarkNonAsciiKey(ParsedKey* key) {
    if (key) key->nonAsciiOrTruncated = true;
}

class Parser {
public:
    Parser(const char* document, std::size_t length)
        : begin_(document), current_(document), end_(document ? document + length : nullptr) {}

    ParseResult Parse(SinRam::Config* output) {
        SinRam::Config parsed{};
        SkipWhitespace();
        if (!Consume('{')) return Result(Code::Malformed, false);

        bool sinRamSeen = false;
        SkipWhitespace();
        if (!Consume('}')) {
            while (true) {
                ParsedKey key{};
                if (!ParseString(&key)) return Result(Code::Malformed, sinRamSeen);
                SkipWhitespace();
                if (!Consume(':')) return Result(Code::Malformed, sinRamSeen);
                SkipWhitespace();

                if (EqualsIgnoreAsciiCase(key, "sinRam")) {
                    if (sinRamSeen) return Result(Code::DuplicateKey, true);
                    sinRamSeen = true;
                    const Code code = ParseSinRamObject(&parsed);
                    if (code != Code::Ok) return Result(code, true);
                } else {
                    const Code code = SkipValue(2);
                    if (code != Code::Ok) return Result(code, sinRamSeen);
                }

                SkipWhitespace();
                if (Consume('}')) break;
                if (!Consume(',')) return Result(Code::Malformed, sinRamSeen);
                SkipWhitespace();
            }
        }

        SkipWhitespace();
        if (current_ != end_) return Result(Code::Malformed, sinRamSeen);
        *output = parsed;
        return Result(Code::Ok, sinRamSeen);
    }

private:
    ParseResult Result(Code code, bool present) const {
        ParseResult result{};
        result.code = code;
        result.offset = begin_ && current_
            ? static_cast<std::size_t>(current_ - begin_)
            : 0;
        result.present = present;
        return result;
    }

    void SkipWhitespace() {
        while (current_ != end_ &&
               (*current_ == ' ' || *current_ == '\t' || *current_ == '\r' ||
                *current_ == '\n')) {
            ++current_;
        }
    }

    bool Consume(char expected) {
        if (current_ == end_ || *current_ != expected) return false;
        ++current_;
        return true;
    }

    bool MatchLiteral(const char* literal) {
        const std::size_t length = std::strlen(literal);
        if (static_cast<std::size_t>(end_ - current_) < length ||
            std::memcmp(current_, literal, length) != 0) {
            return false;
        }
        current_ += length;
        return true;
    }

    static bool IsHex(char value) {
        return (value >= '0' && value <= '9') ||
               (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    }

    static std::uint16_t HexValue(char value) {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint16_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint16_t>(10 + value - 'a');
        }
        return static_cast<std::uint16_t>(10 + value - 'A');
    }

    bool ParseHexQuad(std::uint16_t* valueOut) {
        if (!valueOut || static_cast<std::size_t>(end_ - current_) < 4) return false;
        std::uint16_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (!IsHex(*current_)) return false;
            value = static_cast<std::uint16_t>((value << 4u) | HexValue(*current_));
            ++current_;
        }
        *valueOut = value;
        return true;
    }

    bool ConsumeRawUtf8(std::uint8_t lead) {
        std::size_t continuationCount = 0;
        std::uint8_t secondMinimum = 0x80u;
        std::uint8_t secondMaximum = 0xBFu;
        if (lead >= 0xC2u && lead <= 0xDFu) {
            continuationCount = 1;
        } else if (lead >= 0xE0u && lead <= 0xEFu) {
            continuationCount = 2;
            if (lead == 0xE0u) secondMinimum = 0xA0u;
            if (lead == 0xEDu) secondMaximum = 0x9Fu;
        } else if (lead >= 0xF0u && lead <= 0xF4u) {
            continuationCount = 3;
            if (lead == 0xF0u) secondMinimum = 0x90u;
            if (lead == 0xF4u) secondMaximum = 0x8Fu;
        } else {
            return false;
        }

        if (static_cast<std::size_t>(end_ - current_) < continuationCount) return false;
        for (std::size_t i = 0; i < continuationCount; ++i) {
            const std::uint8_t continuation =
                static_cast<std::uint8_t>(*current_++);
            if (continuation < 0x80u || continuation > 0xBFu) return false;
            if (i == 0 &&
                (continuation < secondMinimum || continuation > secondMaximum)) {
                return false;
            }
        }
        return true;
    }

    bool ParseUnicodeEscape(ParsedKey* key) {
        std::uint16_t first = 0;
        if (!ParseHexQuad(&first)) return false;
        if (first >= 0xD800u && first <= 0xDBFFu) {
            if (static_cast<std::size_t>(end_ - current_) < 2 ||
                current_[0] != '\\' || current_[1] != 'u') {
                return false;
            }
            current_ += 2;
            std::uint16_t second = 0;
            if (!ParseHexQuad(&second) || second < 0xDC00u || second > 0xDFFFu) {
                return false;
            }
            MarkNonAsciiKey(key);
            return true;
        }
        if (first >= 0xDC00u && first <= 0xDFFFu) return false;
        if (first <= 0x7Fu) {
            AppendAsciiKeyByte(key, static_cast<char>(first));
        } else {
            MarkNonAsciiKey(key);
        }
        return true;
    }

    bool ParseString(ParsedKey* key) {
        if (!Consume('"')) return false;
        while (current_ != end_) {
            const std::uint8_t value = static_cast<std::uint8_t>(*current_++);
            if (value == '"') return true;
            if (value < 0x20u) return false;
            if (value == '\\') {
                if (current_ == end_) return false;
                const char escaped = *current_++;
                switch (escaped) {
                    case '"': AppendAsciiKeyByte(key, '"'); break;
                    case '\\': AppendAsciiKeyByte(key, '\\'); break;
                    case '/': AppendAsciiKeyByte(key, '/'); break;
                    case 'b': AppendAsciiKeyByte(key, '\b'); break;
                    case 'f': AppendAsciiKeyByte(key, '\f'); break;
                    case 'n': AppendAsciiKeyByte(key, '\n'); break;
                    case 'r': AppendAsciiKeyByte(key, '\r'); break;
                    case 't': AppendAsciiKeyByte(key, '\t'); break;
                    case 'u':
                        if (!ParseUnicodeEscape(key)) return false;
                        break;
                    default: return false;
                }
                continue;
            }
            if (value < 0x80u) {
                AppendAsciiKeyByte(key, static_cast<char>(value));
                continue;
            }
            if (!ConsumeRawUtf8(value)) return false;
            MarkNonAsciiKey(key);
        }
        return false;
    }

    Code ParseNumber(ParsedNumber* output) {
        if (!output || current_ == end_) return Code::Malformed;
        ParsedNumber parsed{};
        if (*current_ == '-') {
            parsed.negative = true;
            ++current_;
        }
        if (current_ == end_ || *current_ < '0' || *current_ > '9') {
            return Code::Malformed;
        }

        if (*current_ == '0') {
            ++current_;
            if (current_ != end_ && *current_ >= '0' && *current_ <= '9') {
                return Code::Malformed;
            }
        } else {
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') {
                const std::uint32_t digit = static_cast<std::uint32_t>(*current_ - '0');
                if (parsed.magnitude >
                    ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10u) {
                    parsed.overflow = true;
                } else if (!parsed.overflow) {
                    parsed.magnitude = parsed.magnitude * 10u + digit;
                }
                ++current_;
            }
        }

        if (current_ != end_ && *current_ == '.') {
            parsed.nonIntegral = true;
            ++current_;
            if (current_ == end_ || *current_ < '0' || *current_ > '9') {
                return Code::Malformed;
            }
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') {
                ++current_;
            }
        }
        if (current_ != end_ && (*current_ == 'e' || *current_ == 'E')) {
            parsed.nonIntegral = true;
            ++current_;
            if (current_ != end_ && (*current_ == '+' || *current_ == '-')) ++current_;
            if (current_ == end_ || *current_ < '0' || *current_ > '9') {
                return Code::Malformed;
            }
            while (current_ != end_ && *current_ >= '0' && *current_ <= '9') {
                ++current_;
            }
        }
        *output = parsed;
        return Code::Ok;
    }

    Code SkipValue(std::size_t depth) {
        if (current_ == end_) return Code::Malformed;
        if (*current_ == '"') return ParseString(nullptr) ? Code::Ok : Code::Malformed;
        if (*current_ == '{') return SkipObject(depth);
        if (*current_ == '[') return SkipArray(depth);
        if (*current_ == 't') return MatchLiteral("true") ? Code::Ok : Code::Malformed;
        if (*current_ == 'f') return MatchLiteral("false") ? Code::Ok : Code::Malformed;
        if (*current_ == 'n') return MatchLiteral("null") ? Code::Ok : Code::Malformed;
        ParsedNumber number{};
        return ParseNumber(&number);
    }

    Code SkipObject(std::size_t depth) {
        if (depth > kMaximumJsonDepth) return Code::DepthExceeded;
        if (!Consume('{')) return Code::Malformed;
        SkipWhitespace();
        if (Consume('}')) return Code::Ok;
        while (true) {
            if (!ParseString(nullptr)) return Code::Malformed;
            SkipWhitespace();
            if (!Consume(':')) return Code::Malformed;
            SkipWhitespace();
            const Code code = SkipValue(depth + 1);
            if (code != Code::Ok) return code;
            SkipWhitespace();
            if (Consume('}')) return Code::Ok;
            if (!Consume(',')) return Code::Malformed;
            SkipWhitespace();
        }
    }

    Code SkipArray(std::size_t depth) {
        if (depth > kMaximumJsonDepth) return Code::DepthExceeded;
        if (!Consume('[')) return Code::Malformed;
        SkipWhitespace();
        if (Consume(']')) return Code::Ok;
        while (true) {
            const Code code = SkipValue(depth + 1);
            if (code != Code::Ok) return code;
            SkipWhitespace();
            if (Consume(']')) return Code::Ok;
            if (!Consume(',')) return Code::Malformed;
            SkipWhitespace();
        }
    }

    Code ParseBooleanValue(bool* output) {
        if (!output) return Code::InvalidArgument;
        if (MatchLiteral("true")) {
            *output = true;
            return Code::Ok;
        }
        if (MatchLiteral("false")) {
            *output = false;
            return Code::Ok;
        }
        const Code skipped = SkipValue(3);
        return skipped == Code::Ok ? Code::WrongType : skipped;
    }

    Code ParseThreatLevel(int* output) {
        if (!output) return Code::InvalidArgument;
        if (current_ != end_ && (*current_ == '-' || (*current_ >= '0' && *current_ <= '9'))) {
            ParsedNumber number{};
            const Code code = ParseNumber(&number);
            if (code != Code::Ok) return code;
            if (number.nonIntegral) return Code::WrongType;
            if (number.overflow || (number.negative && number.magnitude != 0u) ||
                number.magnitude > 2u) {
                return Code::OutOfRange;
            }
            *output = static_cast<int>(number.magnitude);
            return Code::Ok;
        }
        const Code skipped = SkipValue(3);
        return skipped == Code::Ok ? Code::WrongType : skipped;
    }

    Code ParseUnsignedValue(std::uint32_t* output) {
        if (current_==end_ || (*current_!='-' && (*current_<'0' || *current_>'9'))) {
            const Code skipped=SkipValue(3);return skipped==Code::Ok?Code::WrongType:skipped;
        }
        ParsedNumber number{};const Code code=ParseNumber(&number);
        if(code!=Code::Ok)return code;
        if(number.nonIntegral)return Code::WrongType;
        if(number.overflow || (number.negative && number.magnitude!=0) || number.magnitude>0xFFFFFFFFull)return Code::OutOfRange;
        *output=static_cast<std::uint32_t>(number.magnitude);return Code::Ok;
    }

    Code ParseSinRamObject(SinRam::Config* output) {
        if (!output) return Code::InvalidArgument;
        if (current_ == end_ || *current_ != '{') {
            const Code skipped = SkipValue(2);
            return skipped == Code::Ok ? Code::WrongType : skipped;
        }
        if (!Consume('{')) return Code::Malformed;

        SinRam::Config parsed{};
        bool enabledSeen = false;
        bool threatSeen = false;
        bool distributionSeen=false,seedSeen=false;
        SkipWhitespace();
        if (!Consume('}')) {
            while (true) {
                ParsedKey key{};
                if (!ParseString(&key)) return Code::Malformed;
                SkipWhitespace();
                if (!Consume(':')) return Code::Malformed;
                SkipWhitespace();

                Code code = Code::Ok;
                if (EqualsIgnoreAsciiCase(key, "enabled")) {
                    if (enabledSeen) return Code::DuplicateKey;
                    enabledSeen = true;
                    code = ParseBooleanValue(&parsed.enabled);
                } else if (EqualsIgnoreAsciiCase(key, "threatLevel")) {
                    if (threatSeen) return Code::DuplicateKey;
                    threatSeen = true;
                    code = ParseThreatLevel(&parsed.threatLevel);
                } else if (EqualsIgnoreAsciiCase(key,"distribution")) {
                    if(distributionSeen)return Code::DuplicateKey;
                    distributionSeen=true;parsed.seeded=true;
                    std::uint32_t value=0;code=ParseUnsignedValue(&value);
                    if(code==Code::Ok && !SinSpread::ValidDistribution(value))code=Code::OutOfRange;
                    parsed.distribution=value;
                } else if (EqualsIgnoreAsciiCase(key,"seed")) {
                    if(seedSeen)return Code::DuplicateKey;
                    seedSeen=true;parsed.seeded=true;code=ParseUnsignedValue(&parsed.seed);
                } else {
                    return Code::UnknownKey;
                }
                if (code != Code::Ok) return code;

                SkipWhitespace();
                if (Consume('}')) break;
                if (!Consume(',')) return Code::Malformed;
                SkipWhitespace();
            }
        }
        *output = parsed;
        return Code::Ok;
    }

    const char* begin_ = nullptr;
    const char* current_ = nullptr;
    const char* end_ = nullptr;
};

}  // namespace

ParseResult ParseDocument(
    const char* document, std::size_t length, SinRam::Config* output) {
    if (output) *output = {};
    if (!document || !output) return {Code::InvalidArgument, 0, false};
    if (length > kMaximumDocumentBytes) return {Code::TooLarge, 0, false};

    // WHY: parsing into a local value prevents a partial member from arming S.I.N.
    // when a later member, trailing byte, or unrelated JSON value is invalid.
    return Parser(document, length).Parse(output);
}

SerializeResult SerializeValue(
    const SinRam::Config& config, char* output, std::size_t capacity) {
    if (output && capacity > 0) output[0] = '\0';
    if (!output || capacity == 0) return {Code::InvalidArgument, 0};
    if (config.seeded) {
        if(!SinSpread::ValidDistribution(config.distribution))return {Code::OutOfRange,0};
        const int length=std::snprintf(output,capacity,"{\"enabled\":%s,\"distribution\":%u,\"seed\":%u}",
            config.enabled?"true":"false",config.distribution,static_cast<unsigned>(config.seed));
        if(length<0 || static_cast<std::size_t>(length)>=capacity){output[0]=0;return {Code::OutputTooSmall,0};}
        return {Code::Ok,static_cast<std::size_t>(length)};
    }
    if (config.threatLevel < 0 || config.threatLevel > 2) {
        return {Code::OutOfRange, 0};
    }

    const char* prefix = config.enabled
        ? "{\"enabled\":true,\"threatLevel\":"
        : "{\"enabled\":false,\"threatLevel\":";
    const std::size_t prefixLength = std::strlen(prefix);
    const std::size_t required = prefixLength + 2;
    if (capacity <= required) return {Code::OutputTooSmall, 0};

    std::memcpy(output, prefix, prefixLength);
    output[prefixLength] = static_cast<char>('0' + config.threatLevel);
    output[prefixLength + 1] = '}';
    output[required] = '\0';
    return {Code::Ok, required};
}

}  // namespace FfxHooks::SinRamConfig
