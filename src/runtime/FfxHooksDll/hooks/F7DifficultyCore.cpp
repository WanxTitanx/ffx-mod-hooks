#include "F7DifficultyCore.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace FfxHooks::F7Difficulty {
namespace {

constexpr int32_t kMultiplierMinimum = 100;
constexpr int32_t kVitalMultiplierMaximum = 10000;
constexpr int32_t kStatMultiplierMaximum = 5000;
constexpr uint32_t kStatusMaskMaximum = 0x01FFFFFFu;
constexpr uint8_t kElementMaskMaximum = 0x1Fu;
constexpr size_t kMaximumJsonDepth = 8;

enum PresetField : uint8_t {
    FieldEnabled = 0,
    FieldHp,
    FieldMp,
    FieldStr,
    FieldDef,
    FieldMag,
    FieldMdf,
    FieldAgi,
    FieldAcc,
    FieldEva,
    FieldLck,
    FieldOverkill,
    FieldAutoStatus,
    FieldElemWeak,
    FieldElemResist,
    FieldElemAbsorb,
    FieldStatusResist,
};

bool Equals(const char* left, const char* right) {
    return left && right && std::strcmp(left, right) == 0;
}

class JsonParser {
public:
    JsonParser(const char* json, size_t length)
        : begin_(json), current_(json), end_(json ? json + length : nullptr) {}

    ConfigResult Parse(DifficultyConfig* output) {
        DifficultyConfig parsed = MakeNeutralConfig();
        if (!begin_ || !output) return Result(ConfigCode::InvalidArgument);
        SkipWhitespace();
        if (!Consume('{')) return Result(ConfigCode::Malformed);

        uint64_t seen = 0;
        SkipWhitespace();
        if (!Consume('}')) {
            while (true) {
                char key[80] = {};
                if (!ParseString(key, sizeof(key))) return Result(ConfigCode::Malformed);
                SkipWhitespace();
                if (!Consume(':')) return Result(ConfigCode::Malformed);
                SkipWhitespace();

                if (Equals(key, "diffByArea")) {
                    if (!MarkSeen(0, &seen)) return Result(ConfigCode::DuplicateKey);
                    if (!ParseBoolean(&parsed.byArea)) return Result(ConfigCode::Malformed);
                } else if (Equals(key, "areas")) {
                    if (!MarkSeen(1, &seen)) return Result(ConfigCode::DuplicateKey);
                    const ConfigCode areaCode = ParseAreas(&parsed);
                    if (areaCode != ConfigCode::Ok) return Result(areaCode);
                } else if (std::strncmp(key, "diff_", 5) == 0) {
                    const ConfigCode fieldCode = ParsePresetField(key + 5, &parsed.global, &seen, 2);
                    if (fieldCode == ConfigCode::InvalidArgument) {
                        if (!SkipValue(0)) return Result(ConfigCode::Malformed);
                    } else if (fieldCode != ConfigCode::Ok) {
                        return Result(fieldCode);
                    }
                } else if (!SkipValue(0)) {
                    return Result(ConfigCode::Malformed);
                }

                SkipWhitespace();
                if (Consume('}')) break;
                if (!Consume(',')) return Result(ConfigCode::Malformed);
                SkipWhitespace();
            }
        }
        SkipWhitespace();
        if (current_ != end_) return Result(ConfigCode::Malformed);
        *output = parsed;
        return Result(ConfigCode::Ok);
    }

private:
    ConfigResult Result(ConfigCode code) const {
        ConfigResult result{};
        result.code = code;
        result.offset = begin_ && current_ ? static_cast<size_t>(current_ - begin_) : 0;
        result.clamped = clamped_;
        return result;
    }

    void SkipWhitespace() {
        while (current_ != end_ &&
               (*current_ == ' ' || *current_ == '\t' || *current_ == '\r' || *current_ == '\n')) {
            ++current_;
        }
    }

    bool Consume(char expected) {
        if (current_ == end_ || *current_ != expected) return false;
        ++current_;
        return true;
    }

    bool MatchLiteral(const char* literal) {
        const size_t length = std::strlen(literal);
        if (static_cast<size_t>(end_ - current_) < length ||
            std::memcmp(current_, literal, length) != 0) {
            return false;
        }
        current_ += length;
        return true;
    }

    bool ParseString(char* output, size_t capacity) {
        if (!Consume('"')) return false;
        size_t used = 0;
        while (current_ != end_) {
            unsigned char ch = static_cast<unsigned char>(*current_++);
            if (ch == '"') {
                if (output) {
                    if (used >= capacity) return false;
                    output[used] = '\0';
                }
                return true;
            }
            if (ch < 0x20) return false;
            if (ch == '\\') {
                if (current_ == end_) return false;
                ch = static_cast<unsigned char>(*current_++);
                switch (ch) {
                    case '"': case '\\': case '/': break;
                    case 'b': ch = '\b'; break;
                    case 'f': ch = '\f'; break;
                    case 'n': ch = '\n'; break;
                    case 'r': ch = '\r'; break;
                    case 't': ch = '\t'; break;
                    case 'u': {
                        for (int i = 0; i < 4; ++i) {
                            if (current_ == end_ || !std::isxdigit(static_cast<unsigned char>(*current_))) return false;
                            ++current_;
                        }
                        ch = '?';
                        break;
                    }
                    default: return false;
                }
            }
            if (output) {
                if (used + 1 >= capacity) return false;
                output[used] = static_cast<char>(ch);
            }
            ++used;
        }
        return false;
    }

    bool ParseBoolean(bool* output) {
        if (!output) return false;
        if (MatchLiteral("true")) {
            *output = true;
            return true;
        }
        if (MatchLiteral("false")) {
            *output = false;
            return true;
        }
        return false;
    }

    bool ParseInteger(int64_t* output) {
        if (!output || current_ == end_) return false;
        bool negative = false;
        if (*current_ == '-') {
            negative = true;
            ++current_;
        }
        if (current_ == end_ || !std::isdigit(static_cast<unsigned char>(*current_))) return false;
        if (*current_ == '0' && current_ + 1 != end_ &&
            std::isdigit(static_cast<unsigned char>(current_[1]))) {
            return false;
        }

        uint64_t value = 0;
        const uint64_t limit = negative
            ? static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) + 1u
            : static_cast<uint64_t>((std::numeric_limits<int64_t>::max)());
        while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_))) {
            const uint32_t digit = static_cast<uint32_t>(*current_ - '0');
            if (value > (limit - digit) / 10u) return false;
            value = value * 10u + digit;
            ++current_;
        }
        if (negative) {
            *output = value == limit ? (std::numeric_limits<int64_t>::min)()
                                     : -static_cast<int64_t>(value);
        } else {
            *output = static_cast<int64_t>(value);
        }
        return true;
    }

    bool SkipNumber() {
        int64_t ignored = 0;
        const char* start = current_;
        if (!ParseInteger(&ignored)) return false;
        if (current_ != end_ && *current_ == '.') {
            ++current_;
            if (current_ == end_ || !std::isdigit(static_cast<unsigned char>(*current_))) return false;
            while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_))) ++current_;
        }
        if (current_ != end_ && (*current_ == 'e' || *current_ == 'E')) {
            ++current_;
            if (current_ != end_ && (*current_ == '+' || *current_ == '-')) ++current_;
            if (current_ == end_ || !std::isdigit(static_cast<unsigned char>(*current_))) return false;
            while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_))) ++current_;
        }
        return current_ != start;
    }

    bool SkipValue(size_t depth) {
        if (depth >= kMaximumJsonDepth || current_ == end_) return false;
        if (*current_ == '"') return ParseString(nullptr, 0);
        if (*current_ == '{') {
            ++current_;
            SkipWhitespace();
            if (Consume('}')) return true;
            while (true) {
                if (!ParseString(nullptr, 0)) return false;
                SkipWhitespace();
                if (!Consume(':')) return false;
                SkipWhitespace();
                if (!SkipValue(depth + 1)) return false;
                SkipWhitespace();
                if (Consume('}')) return true;
                if (!Consume(',')) return false;
                SkipWhitespace();
            }
        }
        if (*current_ == '[') {
            ++current_;
            SkipWhitespace();
            if (Consume(']')) return true;
            while (true) {
                if (!SkipValue(depth + 1)) return false;
                SkipWhitespace();
                if (Consume(']')) return true;
                if (!Consume(',')) return false;
                SkipWhitespace();
            }
        }
        if (*current_ == 't') return MatchLiteral("true");
        if (*current_ == 'f') return MatchLiteral("false");
        if (*current_ == 'n') return MatchLiteral("null");
        return SkipNumber();
    }

    bool MarkSeen(size_t bit, uint64_t* seen) {
        if (!seen || bit >= 64) return false;
        const uint64_t mask = uint64_t{1} << bit;
        if ((*seen & mask) != 0) return false;
        *seen |= mask;
        return true;
    }

    ConfigCode ParseMultiplier(int32_t* output, int32_t maximum) {
        int64_t value = 0;
        if (!ParseInteger(&value)) return ConfigCode::Malformed;
        if (value < kMultiplierMinimum) {
            value = kMultiplierMinimum;
            clamped_ = true;
        }
        if (value > maximum) {
            value = maximum;
            clamped_ = true;
        }
        *output = static_cast<int32_t>(value);
        return ConfigCode::Ok;
    }

    ConfigCode ParseMask(uint32_t maximum, uint32_t* output) {
        int64_t value = 0;
        if (!ParseInteger(&value)) return ConfigCode::Malformed;
        if (value < 0 || static_cast<uint64_t>(value) > maximum) return ConfigCode::OutOfRange;
        *output = static_cast<uint32_t>(value);
        return ConfigCode::Ok;
    }

    ConfigCode ParseStatusArray(std::array<uint8_t, kStatusCount>* output) {
        if (!output || !Consume('[')) return ConfigCode::Malformed;
        output->fill(0);
        size_t count = 0;
        SkipWhitespace();
        if (Consume(']')) return ConfigCode::Ok;
        while (true) {
            int64_t value = 0;
            if (!ParseInteger(&value)) return ConfigCode::Malformed;
            if (count >= kStatusCount) return ConfigCode::TooManyArrayItems;
            if (value < 0 || value > 255) return ConfigCode::OutOfRange;
            (*output)[count++] = static_cast<uint8_t>(value);
            SkipWhitespace();
            if (Consume(']')) return ConfigCode::Ok;
            if (!Consume(',')) return ConfigCode::Malformed;
            SkipWhitespace();
        }
    }

    ConfigCode ParsePresetField(
        const char* key, Preset* preset, uint64_t* seen, size_t bitBase) {
        if (!key || !preset || !seen) return ConfigCode::InvalidArgument;
        PresetField field{};
        if (Equals(key, "enabled")) field = FieldEnabled;
        else if (Equals(key, "hpMul")) field = FieldHp;
        else if (Equals(key, "mpMul")) field = FieldMp;
        else if (Equals(key, "strMul")) field = FieldStr;
        else if (Equals(key, "defMul")) field = FieldDef;
        else if (Equals(key, "magMul")) field = FieldMag;
        else if (Equals(key, "mdfMul")) field = FieldMdf;
        else if (Equals(key, "agiMul")) field = FieldAgi;
        else if (Equals(key, "accMul")) field = FieldAcc;
        else if (Equals(key, "evaMul")) field = FieldEva;
        else if (Equals(key, "lckMul")) field = FieldLck;
        else if (Equals(key, "overkillMul")) field = FieldOverkill;
        else if (Equals(key, "autoStatus")) field = FieldAutoStatus;
        else if (Equals(key, "elemWeak")) field = FieldElemWeak;
        else if (Equals(key, "elemResist")) field = FieldElemResist;
        else if (Equals(key, "elemAbsorb")) field = FieldElemAbsorb;
        else if (Equals(key, "statusResist")) field = FieldStatusResist;
        else return ConfigCode::InvalidArgument;

        if (!MarkSeen(bitBase + static_cast<size_t>(field), seen)) return ConfigCode::DuplicateKey;
        switch (field) {
            case FieldEnabled:
                return ParseBoolean(&preset->enabled) ? ConfigCode::Ok : ConfigCode::Malformed;
            case FieldHp: return ParseMultiplier(&preset->hpMul, kVitalMultiplierMaximum);
            case FieldMp: return ParseMultiplier(&preset->mpMul, kVitalMultiplierMaximum);
            case FieldStr: return ParseMultiplier(&preset->strMul, kStatMultiplierMaximum);
            case FieldDef: return ParseMultiplier(&preset->defMul, kStatMultiplierMaximum);
            case FieldMag: return ParseMultiplier(&preset->magMul, kStatMultiplierMaximum);
            case FieldMdf: return ParseMultiplier(&preset->mdfMul, kStatMultiplierMaximum);
            case FieldAgi: return ParseMultiplier(&preset->agiMul, kStatMultiplierMaximum);
            case FieldAcc: return ParseMultiplier(&preset->accMul, kStatMultiplierMaximum);
            case FieldEva: return ParseMultiplier(&preset->evaMul, kStatMultiplierMaximum);
            case FieldLck: return ParseMultiplier(&preset->lckMul, kStatMultiplierMaximum);
            case FieldOverkill: return ParseMultiplier(&preset->overkillMul, kVitalMultiplierMaximum);
            case FieldAutoStatus: return ParseMask(kStatusMaskMaximum, &preset->autoStatusMask);
            case FieldElemWeak:
            case FieldElemResist:
            case FieldElemAbsorb: {
                uint32_t value = 0;
                const ConfigCode code = ParseMask(kElementMaskMaximum, &value);
                if (code != ConfigCode::Ok) return code;
                uint8_t* destination = field == FieldElemWeak ? &preset->elemWeak
                    : field == FieldElemResist ? &preset->elemResist : &preset->elemAbsorb;
                *destination = static_cast<uint8_t>(value);
                return ConfigCode::Ok;
            }
            case FieldStatusResist: return ParseStatusArray(&preset->statusResist);
            default: return ConfigCode::InvalidArgument;
        }
    }

    ConfigCode ParseArea(AreaRule* output) {
        if (!output || !Consume('{')) return ConfigCode::Malformed;
        AreaRule parsed{};
        uint64_t seen = 0;
        SkipWhitespace();
        if (!Consume('}')) {
            while (true) {
                char key[80] = {};
                if (!ParseString(key, sizeof(key))) return ConfigCode::Malformed;
                SkipWhitespace();
                if (!Consume(':')) return ConfigCode::Malformed;
                SkipWhitespace();
                if (Equals(key, "enabled")) {
                    if (!MarkSeen(0, &seen)) return ConfigCode::DuplicateKey;
                    if (!ParseBoolean(&parsed.enabled)) return ConfigCode::Malformed;
                } else if (Equals(key, "fieldRow")) {
                    if (!MarkSeen(1, &seen)) return ConfigCode::DuplicateKey;
                    int64_t value = 0;
                    if (!ParseInteger(&value)) return ConfigCode::Malformed;
                    if (value < -1 || value > (std::numeric_limits<int32_t>::max)()) return ConfigCode::OutOfRange;
                    parsed.fieldRow = static_cast<int32_t>(value);
                } else {
                    const ConfigCode fieldCode = ParsePresetField(key, &parsed.preset, &seen, 2);
                    if (fieldCode == ConfigCode::InvalidArgument) {
                        if (!SkipValue(1)) return ConfigCode::Malformed;
                    } else if (fieldCode != ConfigCode::Ok) {
                        return fieldCode;
                    }
                }
                SkipWhitespace();
                if (Consume('}')) break;
                if (!Consume(',')) return ConfigCode::Malformed;
                SkipWhitespace();
            }
        }
        parsed.preset.enabled = parsed.enabled;
        *output = parsed;
        return ConfigCode::Ok;
    }

    ConfigCode ParseAreas(DifficultyConfig* output) {
        if (!output || !Consume('[')) return ConfigCode::Malformed;
        SkipWhitespace();
        if (Consume(']')) return ConfigCode::Ok;
        while (true) {
            if (output->areaCount >= kMaxAreaRules) return ConfigCode::TooManyAreaRules;
            const ConfigCode code = ParseArea(&output->areas[output->areaCount]);
            if (code != ConfigCode::Ok) return code;
            ++output->areaCount;
            SkipWhitespace();
            if (Consume(']')) return ConfigCode::Ok;
            if (!Consume(',')) return ConfigCode::Malformed;
            SkipWhitespace();
        }
    }

    const char* begin_ = nullptr;
    const char* current_ = nullptr;
    const char* end_ = nullptr;
    bool clamped_ = false;
};

class JsonWriter {
public:
    JsonWriter(char* output, size_t capacity) : output_(output), capacity_(capacity) {
        if (output_ && capacity_ > 0) output_[0] = '\0';
    }

    bool Append(const char* format, ...) {
        if (!output_ || used_ >= capacity_) return false;
        va_list arguments;
        va_start(arguments, format);
        const int count = std::vsnprintf(output_ + used_, capacity_ - used_, format, arguments);
        va_end(arguments);
        if (count < 0 || static_cast<size_t>(count) >= capacity_ - used_) return false;
        used_ += static_cast<size_t>(count);
        return true;
    }

    size_t Used() const { return used_; }

private:
    char* output_ = nullptr;
    size_t capacity_ = 0;
    size_t used_ = 0;
};

bool IsPresetSerializable(const Preset& preset) {
    const int32_t vital[] = {preset.hpMul, preset.mpMul, preset.overkillMul};
    for (int32_t value : vital) {
        if (value < kMultiplierMinimum || value > kVitalMultiplierMaximum) return false;
    }
    const int32_t stats[] = {preset.strMul, preset.defMul, preset.magMul, preset.mdfMul,
                             preset.agiMul, preset.accMul, preset.evaMul, preset.lckMul};
    for (int32_t value : stats) {
        if (value < kMultiplierMinimum || value > kStatMultiplierMaximum) return false;
    }
    return preset.autoStatusMask <= kStatusMaskMaximum &&
           preset.elemWeak <= kElementMaskMaximum &&
           preset.elemResist <= kElementMaskMaximum &&
           preset.elemAbsorb <= kElementMaskMaximum;
}

bool AppendPreset(JsonWriter& writer, const Preset& preset, const char* prefix, const char* indent) {
    if (!IsPresetSerializable(preset)) return false;
    if (!writer.Append("%s\"%senabled\":%s,\n", indent, prefix, preset.enabled ? "true" : "false") ||
        !writer.Append("%s\"%shpMul\":%d,\n", indent, prefix, preset.hpMul) ||
        !writer.Append("%s\"%smpMul\":%d,\n", indent, prefix, preset.mpMul) ||
        !writer.Append("%s\"%sstrMul\":%d,\n", indent, prefix, preset.strMul) ||
        !writer.Append("%s\"%sdefMul\":%d,\n", indent, prefix, preset.defMul) ||
        !writer.Append("%s\"%smagMul\":%d,\n", indent, prefix, preset.magMul) ||
        !writer.Append("%s\"%smdfMul\":%d,\n", indent, prefix, preset.mdfMul) ||
        !writer.Append("%s\"%sagiMul\":%d,\n", indent, prefix, preset.agiMul) ||
        !writer.Append("%s\"%saccMul\":%d,\n", indent, prefix, preset.accMul) ||
        !writer.Append("%s\"%sevaMul\":%d,\n", indent, prefix, preset.evaMul) ||
        !writer.Append("%s\"%slckMul\":%d,\n", indent, prefix, preset.lckMul) ||
        !writer.Append("%s\"%soverkillMul\":%d,\n", indent, prefix, preset.overkillMul) ||
        !writer.Append("%s\"%sautoStatus\":%u,\n", indent, prefix, preset.autoStatusMask) ||
        !writer.Append("%s\"%selemWeak\":%u,\n", indent, prefix, static_cast<unsigned>(preset.elemWeak)) ||
        !writer.Append("%s\"%selemResist\":%u,\n", indent, prefix, static_cast<unsigned>(preset.elemResist)) ||
        !writer.Append("%s\"%selemAbsorb\":%u,\n", indent, prefix, static_cast<unsigned>(preset.elemAbsorb)) ||
        !writer.Append("%s\"%sstatusResist\":[", indent, prefix)) {
        return false;
    }
    for (size_t i = 0; i < preset.statusResist.size(); ++i) {
        if (!writer.Append("%s%u", i == 0 ? "" : ",",
                           static_cast<unsigned>(preset.statusResist[i]))) return false;
    }
    return writer.Append("]");
}

bool AppendArea(JsonWriter& writer, const AreaRule& area) {
    Preset preset = area.preset;
    preset.enabled = area.enabled;
    if (!writer.Append("    {\n      \"enabled\":%s,\n      \"fieldRow\":%d,\n",
                       area.enabled ? "true" : "false", area.fieldRow)) return false;
    // Avoid serializing a second enabled key inside an area object.
    if (!IsPresetSerializable(preset) ||
        !writer.Append("      \"hpMul\":%d,\n      \"mpMul\":%d,\n", preset.hpMul, preset.mpMul) ||
        !writer.Append("      \"strMul\":%d,\n      \"defMul\":%d,\n", preset.strMul, preset.defMul) ||
        !writer.Append("      \"magMul\":%d,\n      \"mdfMul\":%d,\n", preset.magMul, preset.mdfMul) ||
        !writer.Append("      \"agiMul\":%d,\n      \"accMul\":%d,\n", preset.agiMul, preset.accMul) ||
        !writer.Append("      \"evaMul\":%d,\n      \"lckMul\":%d,\n", preset.evaMul, preset.lckMul) ||
        !writer.Append("      \"overkillMul\":%d,\n", preset.overkillMul) ||
        !writer.Append("      \"autoStatus\":%u,\n", preset.autoStatusMask) ||
        !writer.Append("      \"elemWeak\":%u,\n      \"elemResist\":%u,\n      \"elemAbsorb\":%u,\n",
                       static_cast<unsigned>(preset.elemWeak),
                       static_cast<unsigned>(preset.elemResist),
                       static_cast<unsigned>(preset.elemAbsorb)) ||
        !writer.Append("      \"statusResist\":[")) return false;
    for (size_t i = 0; i < preset.statusResist.size(); ++i) {
        if (!writer.Append("%s%u", i == 0 ? "" : ",",
                           static_cast<unsigned>(preset.statusResist[i]))) return false;
    }
    return writer.Append("]\n    }");
}

bool EqualsIgnoreCase(char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
}

} // namespace

Preset MakeNeutralPreset() {
    return Preset{};
}

DifficultyConfig MakeNeutralConfig() {
    return DifficultyConfig{};
}

ConfigResult ParseConfig(const char* json, size_t length, DifficultyConfig* output) {
    if (output) *output = MakeNeutralConfig();
    if (!json || !output) return {ConfigCode::InvalidArgument, 0, false};
    if (length > kMaxJsonBytes) return {ConfigCode::TooLarge, 0, false};
    return JsonParser(json, length).Parse(output);
}

ConfigResult SerializeConfig(
    const DifficultyConfig& config, char* output, size_t capacity, size_t* lengthOut) {
    if (lengthOut) *lengthOut = 0;
    if (!output || capacity == 0 || !lengthOut) return {ConfigCode::InvalidArgument, 0, false};
    if (config.areaCount > kMaxAreaRules || !IsPresetSerializable(config.global)) {
        return {ConfigCode::OutOfRange, 0, false};
    }

    JsonWriter writer(output, capacity);
    if (!writer.Append("{\n  \"version\":1,\n  \"diffByArea\":%s,\n",
                       config.byArea ? "true" : "false") ||
        !AppendPreset(writer, config.global, "diff_", "  ") ||
        !writer.Append(",\n  \"areas\":[\n")) {
        return {ConfigCode::OutputTooSmall, writer.Used(), false};
    }
    for (size_t i = 0; i < config.areaCount; ++i) {
        if (!AppendArea(writer, config.areas[i]) ||
            !writer.Append("%s\n", i + 1 < config.areaCount ? "," : "")) {
            return {ConfigCode::OutputTooSmall, writer.Used(), false};
        }
    }
    if (!writer.Append("  ]\n}\n")) {
        return {ConfigCode::OutputTooSmall, writer.Used(), false};
    }
    if (writer.Used() > kMaxJsonBytes) return {ConfigCode::TooLarge, writer.Used(), false};
    *lengthOut = writer.Used();
    return {ConfigCode::Ok, writer.Used(), false};
}

bool IsAllowedPersistencePath(const char* path) {
    if (!path || !*path) return false;
    char normalized[1024] = {};
    size_t length = 0;
    for (; path[length] != '\0'; ++length) {
        if (length + 1 >= sizeof(normalized)) return false;
        normalized[length] = path[length] == '\\' ? '/' : path[length];
    }
    normalized[length] = '\0';

    size_t segmentStart = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || normalized[i] == '/') {
            if (i - segmentStart == 2 && normalized[segmentStart] == '.' &&
                normalized[segmentStart + 1] == '.') return false;
            segmentStart = i + 1;
        }
    }

    constexpr char suffix[] = "/modules/config/f7_inlive.json";
    constexpr size_t suffixLength = sizeof(suffix) - 1;
    if (length < suffixLength) return false;
    const size_t offset = length - suffixLength;
    for (size_t i = 0; i < suffixLength; ++i) {
        if (!EqualsIgnoreCase(normalized[offset + i], suffix[i])) return false;
    }
    return true;
}

PersistenceCode ReadDocument(
    const PersistenceIo& io, const char* path, char* output, size_t capacity, size_t* lengthOut) {
    if (lengthOut) *lengthOut = 0;
    if (!io.read || !output || capacity == 0 || !lengthOut) return PersistenceCode::InvalidArgument;
    if (!IsAllowedPersistencePath(path)) return PersistenceCode::PathRejected;
    const size_t readCapacity = capacity - 1 < kMaxJsonBytes ? capacity - 1 : kMaxJsonBytes;
    size_t length = 0;
    if (!io.read(io.context, path, output, readCapacity, &length)) return PersistenceCode::IoError;
    if (length > readCapacity || length > kMaxJsonBytes) return PersistenceCode::TooLarge;
    output[length] = '\0';
    *lengthOut = length;
    return PersistenceCode::Ok;
}

PersistenceCode WriteDocument(
    const PersistenceIo& io, const char* path, const char* data, size_t length) {
    if (!io.writeAtomic || (!data && length != 0)) return PersistenceCode::InvalidArgument;
    if (!IsAllowedPersistencePath(path)) return PersistenceCode::PathRejected;
    if (length > kMaxJsonBytes) return PersistenceCode::TooLarge;
    return io.writeAtomic(io.context, path, data, length)
        ? PersistenceCode::Ok : PersistenceCode::IoError;
}

namespace {

constexpr uint32_t kBattleFieldRequestMask = 0x0FFFFFFFu;
constexpr uint64_t kBattleFieldValid = uint64_t{1} << 35;
constexpr uint64_t kBattleFieldRequestShift = 36;

uint32_t DecodeBattleFieldRequest(uint64_t encoded) {
    return static_cast<uint32_t>(encoded >> kBattleFieldRequestShift) &
           kBattleFieldRequestMask;
}

}  // namespace

BattleFieldRequest BattleFieldPublication::BeginRequest() {
    uint32_t request = 0;
    while (request == 0) {
        request = (nextRequest_.fetch_add(1, std::memory_order_acq_rel) + 1u) &
                  kBattleFieldRequestMask;
    }
    // One atomic state represents both an uncommitted lease and its optional ticket.
    // A stale commit therefore cannot race a newer Begin and overwrite that newer route.
    pending_.store(
        static_cast<uint64_t>(request) << kBattleFieldRequestShift,
        std::memory_order_release);
    return request;
}

bool BattleFieldPublication::Commit(
    BattleFieldRequest request, int32_t fieldRow, BattleFieldSource source) {
    const uint8_t sourceValue = static_cast<uint8_t>(source);
    if (request == 0 || fieldRow < 0 || source == BattleFieldSource::Missing ||
        sourceValue > static_cast<uint8_t>(BattleFieldSource::CustomMix)) {
        return false;
    }
    const uint64_t uncommitted =
        static_cast<uint64_t>(request) << kBattleFieldRequestShift;
    const uint64_t encoded =
        uncommitted |
        kBattleFieldValid |
        (static_cast<uint64_t>(sourceValue) << 32) |
        static_cast<uint32_t>(fieldRow);
    uint64_t expected = uncommitted;
    return pending_.compare_exchange_strong(
        expected, encoded, std::memory_order_acq_rel,
        std::memory_order_acquire);
}

void BattleFieldPublication::Cancel(BattleFieldRequest request) {
    if (request == 0) return;
    uint64_t encoded = pending_.load(std::memory_order_acquire);
    while (DecodeBattleFieldRequest(encoded) == request &&
           !pending_.compare_exchange_weak(
               encoded, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
}

void BattleFieldPublication::Publish(int32_t fieldRow, BattleFieldSource source) {
    const BattleFieldRequest request = BeginRequest();
    if (!Commit(request, fieldRow, source)) Cancel(request);
}

BattleFieldTicket BattleFieldPublication::Consume() {
    const uint64_t encoded = pending_.exchange(0, std::memory_order_acq_rel);
    if ((encoded & kBattleFieldValid) == 0) return {};
    const uint32_t request = DecodeBattleFieldRequest(encoded);
    if (request == 0) return {};
    BattleFieldTicket ticket{};
    ticket.fieldRow = static_cast<int32_t>(static_cast<uint32_t>(encoded));
    ticket.source = static_cast<BattleFieldSource>((encoded >> 32) & 0x07u);
    ticket.request = request;
    return ticket;
}

void EncounterCaptureSuppression::Begin() {
    uint32_t depth = depth_.load(std::memory_order_acquire);
    while (depth != UINT32_MAX && !depth_.compare_exchange_weak(
               depth, depth + 1u, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
}

void EncounterCaptureSuppression::End() {
    uint32_t depth = depth_.load(std::memory_order_acquire);
    while (depth != 0 && !depth_.compare_exchange_weak(
               depth, depth - 1u, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
}

bool EncounterCaptureSuppression::Active() const {
    return depth_.load(std::memory_order_acquire) != 0;
}

namespace {

constexpr size_t kRuntimeFieldCount = 44;
constexpr size_t kMaxHpField = 0;
constexpr size_t kMaxMpField = 1;
constexpr size_t kOverkillField = 2;
constexpr size_t kFirstStatField = 3;
constexpr size_t kCurrentHpField = 11;
constexpr size_t kCurrentMpField = 12;
constexpr size_t kElementAbsorbField = 13;
constexpr size_t kElementIgnoreField = 14;
constexpr size_t kElementResistField = 15;
constexpr size_t kElementWeakField = 16;
constexpr size_t kInnateAutoFirstField = 17;
constexpr size_t kInnateAutoDurationField = 18;
constexpr size_t kFirstStatusResistanceField = 19;

bool IsAutoStatusField(size_t fieldIndex) {
    return fieldIndex == kInnateAutoFirstField || fieldIndex == kInnateAutoDurationField;
}

bool IsDynamicField(size_t fieldIndex) {
    return fieldIndex == kCurrentHpField || fieldIndex == kCurrentMpField;
}

struct HighlowOperand {
    size_t offset;
    uint32_t targetRva;
};

template <size_t PrefixSize, size_t OperandCount>
bool BuildLoadedHighlowPrefix(
    const std::array<uint8_t, PrefixSize>& preferredPrefix,
    uintptr_t loadedImageBase,
    const std::array<HighlowOperand, OperandCount>& operands,
    std::array<uint8_t, PrefixSize>* loadedPrefix) {
    if (!loadedPrefix || loadedImageBase == 0 ||
        loadedImageBase > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    *loadedPrefix = preferredPrefix;
    const uint32_t base32 = static_cast<uint32_t>(loadedImageBase);
    for (const HighlowOperand& operand : operands) {
        if (operand.offset > PrefixSize || PrefixSize - operand.offset < sizeof(uint32_t) ||
            operand.targetRva > std::numeric_limits<uint32_t>::max() - base32) {
            return false;
        }

        // WHY: PE HIGHLOW relocations rewrite the complete abs32 operand in loaded memory.
        // Disk SHA identity selects the executable, but only this loaded-base reconstruction
        // can prove the bytes MinHook will actually decode when ASLR moves the image.
        const uint32_t loadedAddress = base32 + operand.targetRva;
        loadedPrefix->at(operand.offset) = static_cast<uint8_t>(loadedAddress & 0xFFu);
        loadedPrefix->at(operand.offset + 1u) =
            static_cast<uint8_t>((loadedAddress >> 8u) & 0xFFu);
        loadedPrefix->at(operand.offset + 2u) =
            static_cast<uint8_t>((loadedAddress >> 16u) & 0xFFu);
        loadedPrefix->at(operand.offset + 3u) =
            static_cast<uint8_t>((loadedAddress >> 24u) & 0xFFu);
    }
    return true;
}

struct FieldSpec {
    size_t offset;
    uint8_t width;
};

constexpr std::array<FieldSpec, kRuntimeFieldCount> MakeRuntimeFields() {
    std::array<FieldSpec, kRuntimeFieldCount> fields = {{
    {kMaxHpOffset, 4},
    {kMaxMpOffset, 4},
    {kOverkillOffset, 4},
    {kStatOffsets[0], 1},
    {kStatOffsets[1], 1},
    {kStatOffsets[2], 1},
    {kStatOffsets[3], 1},
    {kStatOffsets[4], 1},
    {kStatOffsets[5], 1},
    {kStatOffsets[6], 1},
    {kStatOffsets[7], 1},
    {kCurrentHpOffset, 4},
    {kCurrentMpOffset, 4},
    {kElementAbsorbOffset, 1},
    {kElementIgnoreOffset, 1},
    {kElementResistOffset, 1},
    {kElementWeakOffset, 1},
    {kInnateAutoFirstOffset, 2},
    {kInnateAutoDurationOffset, 2},
    }};
    for (size_t i = 0; i < kStatusCount; ++i)
        fields[kFirstStatusResistanceField + i] = {kStatusResistanceOffset + i, 1};
    return fields;
}
constexpr auto kRuntimeFields = MakeRuntimeFields();

bool ReadValue(const MemoryIo& io, uintptr_t address, const FieldSpec& field, uint32_t* output) {
    if (!io.read || !output || address == 0) return false;
    uint32_t value = 0;
    if (!io.read(io.context, address + field.offset, &value, field.width)) return false;
    *output = value;
    return true;
}

bool WriteValue(const MemoryIo& io, uintptr_t address, const FieldSpec& field, uint32_t value) {
    return io.write && address != 0 &&
           io.write(io.context, address + field.offset, &value, field.width);
}

uint32_t ScaleDword(uint32_t baseline, int32_t multiplier) {
    if (baseline == 0) return 0;
    const uint64_t product = static_cast<uint64_t>(baseline) *
                             static_cast<uint32_t>(multiplier);
    uint64_t scaled = (product + 500u) / 1000u;
    constexpr uint64_t maximum = 0x7FFFFFFFu;
    if (scaled == 0) scaled = 1;
    if (scaled > maximum) scaled = maximum;
    return static_cast<uint32_t>(scaled);
}

uint32_t PreserveRatio(uint32_t current, uint32_t maximum, uint32_t scaledMaximum) {
    if (current == 0 || scaledMaximum == 0) return 0;
    if (maximum == 0) return scaledMaximum;
    const uint64_t numerator = static_cast<uint64_t>(current) * scaledMaximum;
    uint64_t scaled = (numerator + maximum / 2u) / maximum;
    if (scaled > scaledMaximum) scaled = scaledMaximum;
    return static_cast<uint32_t>(scaled);
}

uint32_t ScaleByte(uint32_t baseline, int32_t multiplier) {
    if (baseline == 0) return 0;
    uint64_t scaled = (static_cast<uint64_t>(baseline) *
                       static_cast<uint32_t>(multiplier) + 500u) / 1000u;
    if (scaled == 0) scaled = 1;
    if (scaled > 0xFFu) scaled = 0xFFu;
    return static_cast<uint32_t>(scaled);
}

const Preset& SelectPreset(const DifficultyConfig& config, int32_t fieldRow) {
    if (config.byArea) {
        for (size_t i = 0; i < config.areaCount && i < config.areas.size(); ++i) {
            const AreaRule& area = config.areas[i];
            if (area.enabled && area.fieldRow == fieldRow) return area.preset;
        }
        // A fieldRow of -1 is the explicit by-area fallback, not an additive layer.
        for (size_t i = 0; i < config.areaCount && i < config.areas.size(); ++i) {
            const AreaRule& area = config.areas[i];
            if (area.enabled && area.fieldRow == -1) return area.preset;
        }
    }
    return config.global;
}

std::array<uint32_t, kRuntimeFieldCount> DesiredValues(
    const std::array<uint32_t, kRuntimeFieldCount>& baseline, const Preset& preset) {
    std::array<uint32_t, kRuntimeFieldCount> desired = baseline;
    desired[kMaxHpField] = ScaleDword(baseline[kMaxHpField], preset.hpMul);
    desired[kMaxMpField] = ScaleDword(baseline[kMaxMpField], preset.mpMul);
    desired[kOverkillField] = ScaleDword(baseline[kOverkillField], preset.overkillMul);
    const std::array<int32_t, kStatCount> multipliers = {{
        preset.strMul, preset.defMul, preset.magMul, preset.mdfMul,
        preset.agiMul, preset.lckMul, preset.evaMul, preset.accMul,
    }};
    for (size_t i = 0; i < kStatCount; ++i) {
        desired[kFirstStatField + i] = ScaleByte(baseline[kFirstStatField + i], multipliers[i]);
    }
    // Native damage uses four independent masks. A selected affinity must also
    // clear a competing native immunity; unselected elements retain vanilla.
    const uint32_t absorb = preset.elemAbsorb & 0x1Fu;
    const uint32_t resist = preset.elemResist & ~absorb & 0x1Fu;
    const uint32_t weak = preset.elemWeak & ~(absorb | resist) & 0x1Fu;
    const uint32_t selected = absorb | resist | weak;
    desired[kElementAbsorbField] = (baseline[kElementAbsorbField] & ~selected) | absorb;
    desired[kElementIgnoreField] = baseline[kElementIgnoreField] & ~selected;
    desired[kElementResistField] = (baseline[kElementResistField] & ~selected) | resist;
    desired[kElementWeakField] = (baseline[kElementWeakField] & ~selected) | weak;
    desired[kInnateAutoFirstField] = baseline[kInnateAutoFirstField] | (preset.autoStatusMask & 0xFFFu);
    desired[kInnateAutoDurationField] = baseline[kInnateAutoDurationField] | ((preset.autoStatusMask >> 12u) & 0x1FFFu);
    for (size_t i = 0; i < kStatusCount; ++i) {
        if (preset.statusResist[i] > baseline[kFirstStatusResistanceField + i])
            desired[kFirstStatusResistanceField + i] = preset.statusResist[i];
    }
    // WHY: current HP/MP stay at their baseline placeholders here. The owned
    // maximum/current transaction below samples the live numerator and performs
    // the only ratio calculation after every structural stage is complete.
    return desired;
}

void FinishResult(RuntimeResult* result, bool restoring, bool hadActors) {
    if (result->faults != 0) {
        result->code = ResultCode::Fault;
    } else if (result->ownershipLost != 0) {
        result->code = ResultCode::OwnershipLost;
    } else if (restoring && hadActors) {
        result->code = ResultCode::Restored;
    } else if (hadActors) {
        result->code = ResultCode::Applied;
    } else {
        result->code = ResultCode::NoActors;
    }
}

} // namespace

void Runtime::BeginGeneration(uint64_t generation) {
    generation_ = generation;
    generationStarted_ = true;
    snapshots_ = {};
}

bool Runtime::AdmitAutoStatus(
    const MemoryIo& io, ActorSnapshot& snapshot,
    const std::array<uint32_t, kRuntimeFieldCount>& desired, RuntimeResult& result) {
    if (snapshot.autoRefreshFailed) {
        ++result.faults;
        return false;
    }
    bool needed = snapshot.autoRefreshPending;
    for (size_t field : {kInnateAutoFirstField, kInnateAutoDurationField}) {
        needed = needed || desired[field] != snapshot.baseline[field] ||
                 snapshot.ownership[field] != Ownership::Unowned;
    }
    if (!needed) return false;
    if (!io.refreshAutoStatus) {
        ++result.faults;
        return false;
    }
    // Admit both words before changing either: a foreign edit in the other word
    // must not leave a half-published AUTO configuration behind.
    for (size_t field : {kInnateAutoFirstField, kInnateAutoDurationField}) {
        if (snapshot.ownership[field] == Ownership::Lost) {
            ++result.ownershipLost;
            return false;
        }
        uint32_t current = 0;
        if (!ReadValue(io, snapshot.address, kRuntimeFields[field], &current)) {
            ++result.faults;
            return false;
        }
        const uint32_t expected = snapshot.ownership[field] == Ownership::Unowned
            ? snapshot.baseline[field] : snapshot.lastApplied[field];
        if (current != expected &&
            !(snapshot.ownership[field] == Ownership::Indeterminate &&
              current == snapshot.pendingTarget[field])) {
            snapshot.ownership[kInnateAutoFirstField] = Ownership::Lost;
            snapshot.ownership[kInnateAutoDurationField] = Ownership::Lost;
            ++result.ownershipLost;
            return false;
        }
    }
    return true;
}

void Runtime::RefreshAutoStatus(
    const MemoryIo& io, ActorSnapshot& snapshot, RuntimeResult& result) {
    if (!snapshot.autoRefreshPending || snapshot.autoRefreshFailed) return;
    snapshot.autoRefreshPending = false;
    bool certain = true;
    for (size_t field : {kInnateAutoFirstField, kInnateAutoDurationField}) {
        certain = certain && snapshot.ownership[field] != Ownership::Indeterminate &&
                  snapshot.ownership[field] != Ownership::Lost;
    }
    if (!certain || !io.refreshAutoStatus ||
        !io.refreshAutoStatus(io.context, snapshot.address)) {
        // A native fault can have altered the game's temporary-status backups.
        // Retrying that pair would erase their provenance, so fail this generation.
        snapshot.autoRefreshFailed = true;
        snapshot.ownership[kInnateAutoFirstField] = Ownership::Lost;
        snapshot.ownership[kInnateAutoDurationField] = Ownership::Lost;
        ++result.faults;
        return;
    }
    ++result.autoStatusRefreshed;
}

RuntimeResult Runtime::Update(
    const MemoryIo& io, const DifficultyConfig& config, bool configValid,
    int32_t fieldRow, const ActorRef* actors, size_t actorCount) {
    return UpdateComposed(
        io, config, configValid, fieldRow, SinRam::RuntimeRequest{}, actors, actorCount);
}

RuntimeResult Runtime::UpdateComposed(
    const MemoryIo& io, const DifficultyConfig& config, bool configValid,
    int32_t fieldRow, const SinRam::RuntimeRequest& request,
    const ActorRef* actors, size_t actorCount) {
    RuntimeResult result{};
    if (!configValid) {
        result.code = ResultCode::InvalidConfig;
        return result;
    }
    if (!io.read || !io.write) {
        result.code = ResultCode::Unavailable;
        return result;
    }
    const Preset& preset = SelectPreset(config, fieldRow);
    if (preset.enabled && preset.autoStatusMask != 0 && !io.refreshAutoStatus) {
        result.code = ResultCode::Unavailable;
        return result;
    }
    const bool sinRequested = request.config.enabled;
    if (!preset.enabled && !sinRequested) return Restore(io);
    if (!generationStarted_ || !actors || actorCount == 0) {
        result.code = ResultCode::NoActors;
        return result;
    }

    for (size_t actorIndex = 0; actorIndex < actorCount; ++actorIndex) {
        const ActorRef& actor = actors[actorIndex];
        if (actor.address == 0 || actor.slot >= snapshots_.size()) continue;

        uint16_t formation = 0xFFFFu;
        if (!io.read(io.context, actor.address + kFormationIdOffset,
                     &formation, sizeof(formation))) {
            ++result.faults;
            continue;
        }
        if (formation == 0xFFFFu) {
            // The sentinel means this slot no longer identifies the captured actor.
            snapshots_[actor.slot] = {};
            continue;
        }
        ++result.actorsSeen;

        ActorSnapshot& snapshot = snapshots_[actor.slot];
        if (!snapshot.captured || snapshot.address != actor.address ||
            snapshot.formation != formation) {
            ActorSnapshot fresh{};
            fresh.address = actor.address;
            fresh.formation = formation;
            bool captured = true;
            for (size_t fieldIndex = 0; fieldIndex < kRuntimeFields.size(); ++fieldIndex) {
                if (!ReadValue(io, actor.address, kRuntimeFields[fieldIndex],
                               &fresh.baseline[fieldIndex])) {
                    captured = false;
                    ++result.faults;
                    break;
                }
            }
            if (!captured) continue;
            fresh.captured = true;
            fresh.lastApplied = fresh.baseline;
            fresh.pendingTarget = fresh.baseline;
            snapshot = fresh;
        }

        auto desired = preset.enabled
            ? DesiredValues(snapshot.baseline, preset)
            : snapshot.baseline;
        if (request.config.enabled && (!request.scriptManaged || (request.scriptActorMask&(1u<<actor.slot))!=0)) {
            SinRam::DifficultyValues afterDifficulty{};
            afterDifficulty.maxHp = desired[kMaxHpField];
            afterDifficulty.overkill = desired[kOverkillField];
            for (size_t stat = 0; stat < kStatCount; ++stat) {
                afterDifficulty.stats[stat] = static_cast<uint8_t>(
                    desired[kFirstStatField + stat]);
            }

            const SinRam::StructuralScalePlan plan =
                SinRam::BuildRuntimeStructuralScalePlan(
                    request, generation_, fieldRow, formation, afterDifficulty);
            if (plan.admitted) {
                // WHY: +0x594/+0x5A4 are signed dwords in FFX.exe 78CE....
                // Gate the closed plan before Runtime acquires ownership or writes.
                if (!SinRam::StructuralWritebackInDomain(plan.writeback)) {
                    ++result.faults;
                    continue;
                }
                desired[kMaxHpField] = plan.writeback.maxHp;
                desired[kOverkillField] = plan.writeback.overkill;
                for (size_t stat = 0; stat < kStatCount; ++stat) {
                    desired[kFirstStatField + stat] = plan.writeback.stats[stat];
                }
            }
        }
        const bool autoAdmitted = AdmitAutoStatus(io, snapshot, desired, result);
        struct DynamicObservation {
            bool valid = false;
            uint32_t maximum = 0;
            uint32_t current = 0;
        } dynamic[2]{};
        const size_t maximumFields[2] = {kMaxHpField, kMaxMpField};
        const size_t currentFields[2] = {kCurrentHpField, kCurrentMpField};
        for (size_t pair = 0; pair < 2; ++pair) {
            dynamic[pair].valid =
                ReadValue(io, snapshot.address, kRuntimeFields[maximumFields[pair]],
                          &dynamic[pair].maximum) &&
                ReadValue(io, snapshot.address, kRuntimeFields[currentFields[pair]],
                          &dynamic[pair].current);
            if (!dynamic[pair].valid) ++result.faults;
            if (snapshot.dynamicTransactions[pair].pending) {
                snapshot.dynamicTransactions[pair].targetMaximum =
                    desired[maximumFields[pair]];
            }
        }
        for (size_t fieldIndex = 0; fieldIndex < kRuntimeFields.size(); ++fieldIndex) {
            if (IsDynamicField(fieldIndex)) continue;
            Ownership& ownership = snapshot.ownership[fieldIndex];
            if (IsAutoStatusField(fieldIndex) && (!autoAdmitted || snapshot.autoRefreshFailed)) continue;
            if (fieldIndex >= kElementAbsorbField && ownership == Ownership::Unowned &&
                desired[fieldIndex] == snapshot.baseline[fieldIndex]) continue;
            if (ownership == Ownership::Lost) {
                ++result.ownershipLost;
                continue;
            }

            uint32_t current = 0;
            if (!ReadValue(io, snapshot.address, kRuntimeFields[fieldIndex], &current)) {
                ++result.faults;
                continue;
            }
            if (ownership == Ownership::Indeterminate) {
                if (current == snapshot.pendingTarget[fieldIndex]) {
                    snapshot.lastApplied[fieldIndex] = current;
                    ownership = current == snapshot.baseline[fieldIndex]
                        ? Ownership::Unowned : Ownership::Owned;
                } else if (current == snapshot.lastApplied[fieldIndex]) {
                    ownership = current == snapshot.baseline[fieldIndex]
                        ? Ownership::Unowned : Ownership::Owned;
                } else {
                    ownership = Ownership::Lost;
                    ++result.ownershipLost;
                    continue;
                }
            }
            const uint32_t expected = ownership == Ownership::Owned
                ? snapshot.lastApplied[fieldIndex] : snapshot.baseline[fieldIndex];
            if (current != expected) {
                ownership = Ownership::Lost;
                ++result.ownershipLost;
                continue;
            }
            if (current == desired[fieldIndex]) continue;

            size_t dynamicPair = 2;
            if (fieldIndex == kMaxHpField) dynamicPair = 0;
            if (fieldIndex == kMaxMpField) dynamicPair = 1;
            if (dynamicPair < 2) {
                DynamicPairTransaction& transaction =
                    snapshot.dynamicTransactions[dynamicPair];
                if (transaction.pending) {
                    if (!dynamic[dynamicPair].valid) continue;
                    if (dynamic[dynamicPair].current != transaction.sourceCurrent) {
                        // WHY: a config edit can request another maximum while the previous
                        // pair still awaits current reconciliation. Validate its live numerator
                        // first, so even gameplay equal to the old target blocks every new write.
                        ownership = Ownership::Lost;
                        snapshot.ownership[currentFields[dynamicPair]] = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                        continue;
                    }
                } else {
                    // A maximum must never move without retaining the denominator and
                    // gameplay current that existed before this paired transaction.
                    if (!dynamic[dynamicPair].valid) continue;
                    transaction.pending = true;
                    transaction.sourceMaximum = dynamic[dynamicPair].maximum;
                    transaction.sourceCurrent = dynamic[dynamicPair].current;
                }
                transaction.targetMaximum = desired[fieldIndex];
            }

            snapshot.pendingTarget[fieldIndex] = desired[fieldIndex];
            ownership = Ownership::Indeterminate;
            if (IsAutoStatusField(fieldIndex)) snapshot.autoRefreshPending = true;
            const bool writeOk = WriteValue(
                io, snapshot.address, kRuntimeFields[fieldIndex], desired[fieldIndex]);
            uint32_t readback = 0;
            const bool readbackOk = ReadValue(
                io, snapshot.address, kRuntimeFields[fieldIndex], &readback);
            if (!writeOk || !readbackOk || readback != desired[fieldIndex]) {
                ++result.faults;
                if (IsAutoStatusField(fieldIndex) &&
                    (!readbackOk || (writeOk && readback != desired[fieldIndex]))) {
                    snapshot.autoRefreshPending = false;
                    snapshot.autoRefreshFailed = true;
                    snapshot.ownership[kInnateAutoFirstField] = Ownership::Lost;
                    snapshot.ownership[kInnateAutoDurationField] = Ownership::Lost;
                    ++result.ownershipLost;
                    continue;
                }
                if (dynamicPair < 2) {
                    DynamicPairTransaction& transaction =
                        snapshot.dynamicTransactions[dynamicPair];
                    Ownership& currentOwnership =
                        snapshot.ownership[currentFields[dynamicPair]];
                    if (!readbackOk) {
                        // WHY: after any maximum write attempt, an unavailable readback cannot
                        // distinguish unchanged, applied, or third-party state. No current write
                        // is safe, and a later numeric match cannot repair the missing causality.
                        ownership = Ownership::Lost;
                        currentOwnership = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                    } else if (readback == desired[fieldIndex]) {
                        snapshot.lastApplied[fieldIndex] = readback;
                        ownership = readback == snapshot.baseline[fieldIndex]
                            ? Ownership::Unowned : Ownership::Owned;
                    } else if (readback == expected) {
                        if (writeOk) {
                            // WHY: success followed by the old value means the write was
                            // reverted or raced. Treat it as interference, never as evidence
                            // that a stale pair transaction remains retryable.
                            ownership = Ownership::Lost;
                            currentOwnership = Ownership::Lost;
                            transaction = {};
                            ++result.ownershipLost;
                        } else {
                            ownership = expected == snapshot.baseline[fieldIndex]
                                ? Ownership::Unowned : Ownership::Owned;
                            // KEY: MemoryIo's false-write contract plus an exact readable
                            // prevalue proves this attempt did not publish. Recapture the live
                            // numerator on the next call before retrying the maximum.
                            transaction = {};
                        }
                    } else {
                        ownership = Ownership::Lost;
                        currentOwnership = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                    }
                    continue;
                }
                if (readbackOk) {
                    if (readback == desired[fieldIndex]) {
                        snapshot.lastApplied[fieldIndex] = readback;
                        ownership = readback == snapshot.baseline[fieldIndex]
                            ? Ownership::Unowned : Ownership::Owned;
                    } else if (readback == expected) {
                        ownership = expected == snapshot.baseline[fieldIndex]
                            ? Ownership::Unowned : Ownership::Owned;
                    } else {
                        ownership = Ownership::Lost;
                        ++result.ownershipLost;
                    }
                }
                continue;
            }

            snapshot.lastApplied[fieldIndex] = desired[fieldIndex];
            ownership = desired[fieldIndex] == snapshot.baseline[fieldIndex]
                ? Ownership::Unowned : Ownership::Owned;
            ++result.fieldsWritten;
        }
        for (size_t pair = 0; pair < 2; ++pair) {
            const size_t maximumField = maximumFields[pair];
            const size_t currentField = currentFields[pair];
            DynamicPairTransaction& transaction = snapshot.dynamicTransactions[pair];
            if (!transaction.pending) {
                continue;
            }
            if (snapshot.ownership[maximumField] == Ownership::Lost) {
                snapshot.ownership[currentField] = Ownership::Lost;
                transaction = {};
                continue;
            }
            uint32_t resultingMaximum = 0;
            if (!ReadValue(io, snapshot.address, kRuntimeFields[maximumField],
                           &resultingMaximum)) {
                ++result.faults;
                continue;
            }
            if (resultingMaximum != transaction.targetMaximum) continue;
            const uint32_t desiredCurrent = PreserveRatio(
                transaction.sourceCurrent, transaction.sourceMaximum, resultingMaximum);
            Ownership& ownership = snapshot.ownership[currentField];
            uint32_t observedCurrent = 0;
            if (!ReadValue(io, snapshot.address, kRuntimeFields[currentField],
                           &observedCurrent)) {
                ++result.faults;
                continue;
            }
            if (transaction.currentWritePending) {
                const bool priorWriteApplied =
                    observedCurrent == transaction.currentWriteTarget;
                const bool priorWriteDidNotApply =
                    observedCurrent == transaction.currentObservedBeforeWrite;
                if (!priorWriteApplied && !priorWriteDidNotApply) {
                    // WHY: a third value is gameplay or foreign state published after the
                    // failed/ambiguous write. Retrying the stale target would erase it, so the
                    // entire structural pair is relinquished without another write.
                    snapshot.ownership[maximumField] = Ownership::Lost;
                    ownership = Ownership::Lost;
                    transaction = {};
                    ++result.ownershipLost;
                    continue;
                }
                transaction.currentWritePending = false;
            }
            if (observedCurrent == desiredCurrent) {
                snapshot.lastApplied[currentField] = observedCurrent;
                snapshot.pendingTarget[currentField] = observedCurrent;
                ownership = Ownership::Owned;
                transaction = {};
                continue;
            }
            if (observedCurrent != transaction.sourceCurrent) {
                // WHY: the maximum already moved, but gameplay changed the numerator before
                // this first current write. Reusing the captured ratio would overwrite live
                // damage/healing/resource spend, so relinquish the whole structural pair.
                snapshot.ownership[maximumField] = Ownership::Lost;
                ownership = Ownership::Lost;
                transaction = {};
                ++result.ownershipLost;
                continue;
            }

            snapshot.pendingTarget[currentField] = desiredCurrent;
            ownership = Ownership::Indeterminate;
            transaction.currentWritePending = true;
            transaction.currentObservedBeforeWrite = observedCurrent;
            transaction.currentWriteTarget = desiredCurrent;
            const bool writeOk = WriteValue(
                io, snapshot.address, kRuntimeFields[currentField], desiredCurrent);
            uint32_t readback = 0;
            const bool readbackOk = ReadValue(
                io, snapshot.address, kRuntimeFields[currentField], &readback);
            if (!writeOk || !readbackOk || readback != desiredCurrent) {
                ++result.faults;
                if (!readbackOk) {
                    // FIX: a current write followed by unavailable readback is permanently
                    // ambiguous. Even the old numeric value can later be an ABA produced by
                    // gameplay, so no retry may treat it as proof that our write did not apply.
                    snapshot.ownership[maximumField] = Ownership::Lost;
                    ownership = Ownership::Lost;
                    transaction = {};
                    ++result.ownershipLost;
                } else {
                    if (readback == desiredCurrent) {
                        snapshot.lastApplied[currentField] = readback;
                        ownership = Ownership::Owned;
                        transaction = {};
                    } else if (readback != observedCurrent || writeOk) {
                        // WHY: a third value is interference, and a reported-success write
                        // returning to its prevalue is a revert/ABA rather than safe retry proof.
                        snapshot.ownership[maximumField] = Ownership::Lost;
                        ownership = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                    }
                }
                continue;
            }
            snapshot.lastApplied[currentField] = desiredCurrent;
            ownership = Ownership::Owned;
            transaction = {};
            ++result.fieldsWritten;
        }
        RefreshAutoStatus(io, snapshot, result);
    }

    FinishResult(&result, false, result.actorsSeen != 0);
    return result;
}

RuntimeResult Runtime::Restore(const MemoryIo& io) {
    RuntimeResult result{};
    if (!io.read || !io.write) {
        result.code = ResultCode::Unavailable;
        return result;
    }
    for (ActorSnapshot& snapshot : snapshots_) {
        if (!snapshot.captured) continue;
        uint16_t formation = 0xFFFFu;
        if (!io.read(
                io.context, snapshot.address + kFormationIdOffset,
                &formation, sizeof(formation))) {
            ++result.faults;
            continue;
        }
        if (formation != snapshot.formation) {
            // WHY: a slot can be reused before OFF. Matching modified bytes are not proof of
            // ownership when the read-only formation identity says this is a different actor.
            snapshot = {};
            ++result.ownershipLost;
            continue;
        }
        ++result.actorsSeen;
        const bool autoAdmitted = AdmitAutoStatus(io, snapshot, snapshot.baseline, result);
        struct DynamicObservation {
            bool valid = false;
            uint32_t maximum = 0;
            uint32_t current = 0;
        } dynamic[2]{};
        const size_t maximumFields[2] = {kMaxHpField, kMaxMpField};
        const size_t currentFields[2] = {kCurrentHpField, kCurrentMpField};
        for (size_t pair = 0; pair < 2; ++pair) {
            dynamic[pair].valid =
                ReadValue(io, snapshot.address, kRuntimeFields[maximumFields[pair]],
                          &dynamic[pair].maximum) &&
                ReadValue(io, snapshot.address, kRuntimeFields[currentFields[pair]],
                          &dynamic[pair].current);
            if (!dynamic[pair].valid) ++result.faults;
            if (snapshot.dynamicTransactions[pair].pending) {
                snapshot.dynamicTransactions[pair].targetMaximum =
                    snapshot.baseline[maximumFields[pair]];
            }
        }
        for (size_t fieldIndex = 0; fieldIndex < kRuntimeFields.size(); ++fieldIndex) {
            if (IsDynamicField(fieldIndex)) continue;
            Ownership& ownership = snapshot.ownership[fieldIndex];
            if (ownership == Ownership::Lost) {
                ++result.ownershipLost;
                continue;
            }
            if (ownership == Ownership::Unowned) continue;
            if (IsAutoStatusField(fieldIndex) && (!autoAdmitted || snapshot.autoRefreshFailed)) continue;

            uint32_t current = 0;
            if (!ReadValue(io, snapshot.address, kRuntimeFields[fieldIndex], &current)) {
                ++result.faults;
                continue;
            }
            if (ownership == Ownership::Indeterminate) {
                if (current == snapshot.pendingTarget[fieldIndex]) {
                    snapshot.lastApplied[fieldIndex] = current;
                    ownership = current == snapshot.baseline[fieldIndex]
                        ? Ownership::Unowned : Ownership::Owned;
                } else if (current == snapshot.lastApplied[fieldIndex]) {
                    ownership = current == snapshot.baseline[fieldIndex]
                        ? Ownership::Unowned : Ownership::Owned;
                } else {
                    ownership = Ownership::Lost;
                    ++result.ownershipLost;
                    continue;
                }
            }
            if (current != snapshot.lastApplied[fieldIndex]) {
                ownership = Ownership::Lost;
                ++result.ownershipLost;
                continue;
            }
            if (current == snapshot.baseline[fieldIndex]) {
                ownership = Ownership::Unowned;
                continue;
            }

            size_t dynamicPair = 2;
            if (fieldIndex == kMaxHpField) dynamicPair = 0;
            if (fieldIndex == kMaxMpField) dynamicPair = 1;
            if (dynamicPair < 2) {
                DynamicPairTransaction& transaction =
                    snapshot.dynamicTransactions[dynamicPair];
                if (transaction.pending) {
                    if (!dynamic[dynamicPair].valid) continue;
                    if (dynamic[dynamicPair].current != transaction.sourceCurrent) {
                        // WHY: OFF validates pending current evidence before any further
                        // maximum restore. A prior target reached by gameplay is not our token.
                        ownership = Ownership::Lost;
                        snapshot.ownership[currentFields[dynamicPair]] = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                        continue;
                    }
                } else {
                    // OFF preserves the live gameplay ratio that existed before the
                    // structural maximum restore, even when its current write must retry.
                    if (!dynamic[dynamicPair].valid) continue;
                    transaction.pending = true;
                    transaction.sourceMaximum = dynamic[dynamicPair].maximum;
                    transaction.sourceCurrent = dynamic[dynamicPair].current;
                }
                transaction.targetMaximum = snapshot.baseline[fieldIndex];
            }

            const uint32_t expected = snapshot.lastApplied[fieldIndex];
            snapshot.pendingTarget[fieldIndex] = snapshot.baseline[fieldIndex];
            ownership = Ownership::Indeterminate;
            if (IsAutoStatusField(fieldIndex)) snapshot.autoRefreshPending = true;
            const bool writeOk = WriteValue(
                io, snapshot.address, kRuntimeFields[fieldIndex], snapshot.baseline[fieldIndex]);
            uint32_t readback = 0;
            const bool readbackOk = ReadValue(
                io, snapshot.address, kRuntimeFields[fieldIndex], &readback);
            if (!writeOk || !readbackOk || readback != snapshot.baseline[fieldIndex]) {
                ++result.faults;
                if (IsAutoStatusField(fieldIndex) &&
                    (!readbackOk || (writeOk && readback != snapshot.baseline[fieldIndex]))) {
                    snapshot.autoRefreshPending = false;
                    snapshot.autoRefreshFailed = true;
                    snapshot.ownership[kInnateAutoFirstField] = Ownership::Lost;
                    snapshot.ownership[kInnateAutoDurationField] = Ownership::Lost;
                    ++result.ownershipLost;
                    continue;
                }
                if (dynamicPair < 2) {
                    DynamicPairTransaction& transaction =
                        snapshot.dynamicTransactions[dynamicPair];
                    Ownership& currentOwnership =
                        snapshot.ownership[currentFields[dynamicPair]];
                    if (!readbackOk) {
                        // WHY: OFF cannot infer whether the structural maximum restored when
                        // its post-write canary is unavailable. Retaining a stale numerator
                        // would make a later retry capable of erasing gameplay state.
                        ownership = Ownership::Lost;
                        currentOwnership = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                    } else if (readback == snapshot.baseline[fieldIndex]) {
                        snapshot.lastApplied[fieldIndex] = readback;
                        ownership = Ownership::Unowned;
                    } else if (readback == expected) {
                        if (writeOk) {
                            // WHY: a successful restore that reads as the former maximum was
                            // reverted or raced. OFF must not retry through that interference.
                            ownership = Ownership::Lost;
                            currentOwnership = Ownership::Lost;
                            transaction = {};
                            ++result.ownershipLost;
                        } else {
                            ownership = Ownership::Owned;
                            // KEY: a false write plus an exact readable prevalue is the only
                            // definitely-unchanged outcome. Fresh capture on the next call then
                            // preserves intervening healing, damage, or resource spend.
                            transaction = {};
                        }
                    } else {
                        ownership = Ownership::Lost;
                        currentOwnership = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                    }
                    continue;
                }
                if (readbackOk) {
                    if (readback == snapshot.baseline[fieldIndex]) {
                        snapshot.lastApplied[fieldIndex] = readback;
                        ownership = Ownership::Unowned;
                    } else if (readback == expected) {
                        ownership = Ownership::Owned;
                    } else {
                        ownership = Ownership::Lost;
                        ++result.ownershipLost;
                    }
                }
                continue;
            }
            ownership = Ownership::Unowned;
            snapshot.lastApplied[fieldIndex] = snapshot.baseline[fieldIndex];
            ++result.fieldsRestored;
        }
        for (size_t pair = 0; pair < 2; ++pair) {
            const size_t maximumField = maximumFields[pair];
            const size_t currentField = currentFields[pair];
            DynamicPairTransaction& transaction = snapshot.dynamicTransactions[pair];
            if (!transaction.pending) {
                continue;
            }
            if (snapshot.ownership[maximumField] == Ownership::Lost) {
                snapshot.ownership[currentField] = Ownership::Lost;
                transaction = {};
                continue;
            }
            uint32_t resultingMaximum = 0;
            if (!ReadValue(io, snapshot.address, kRuntimeFields[maximumField],
                           &resultingMaximum)) {
                ++result.faults;
                continue;
            }
            if (resultingMaximum != transaction.targetMaximum) continue;
            const uint32_t desiredCurrent = PreserveRatio(
                transaction.sourceCurrent, transaction.sourceMaximum, resultingMaximum);
            Ownership& ownership = snapshot.ownership[currentField];
            uint32_t observedCurrent = 0;
            if (!ReadValue(io, snapshot.address, kRuntimeFields[currentField],
                           &observedCurrent)) {
                ++result.faults;
                continue;
            }
            if (transaction.currentWritePending) {
                const bool priorWriteApplied =
                    observedCurrent == transaction.currentWriteTarget;
                const bool priorWriteDidNotApply =
                    observedCurrent == transaction.currentObservedBeforeWrite;
                if (!priorWriteApplied && !priorWriteDidNotApply) {
                    // WHY: OFF cannot guess whether a third current value came from damage,
                    // healing, resource spend, or another writer. Relinquishing both values
                    // prevents a maximum restore from invalidating that live current state.
                    snapshot.ownership[maximumField] = Ownership::Lost;
                    ownership = Ownership::Lost;
                    transaction = {};
                    ++result.ownershipLost;
                    continue;
                }
                transaction.currentWritePending = false;
            }
            if (observedCurrent == desiredCurrent) {
                snapshot.lastApplied[currentField] = observedCurrent;
                snapshot.pendingTarget[currentField] = observedCurrent;
                ownership = Ownership::Unowned;
                transaction = {};
                continue;
            }
            if (observedCurrent != transaction.sourceCurrent) {
                // WHY: OFF must not overwrite a live numerator that changed after the
                // structural maximum moved. There is no causal token that can classify the
                // change, so the only reversible action is to relinquish the entire pair.
                snapshot.ownership[maximumField] = Ownership::Lost;
                ownership = Ownership::Lost;
                transaction = {};
                ++result.ownershipLost;
                continue;
            }

            snapshot.pendingTarget[currentField] = desiredCurrent;
            ownership = Ownership::Indeterminate;
            transaction.currentWritePending = true;
            transaction.currentObservedBeforeWrite = observedCurrent;
            transaction.currentWriteTarget = desiredCurrent;
            const bool writeOk = WriteValue(
                io, snapshot.address, kRuntimeFields[currentField], desiredCurrent);
            uint32_t readback = 0;
            const bool readbackOk = ReadValue(
                io, snapshot.address, kRuntimeFields[currentField], &readback);
            if (!writeOk || !readbackOk || readback != desiredCurrent) {
                ++result.faults;
                if (!readbackOk) {
                    // FIX: an unreadable post-restore current is ambiguous even if it later
                    // equals the old value. Fail closed now so an ABA cannot authorize a stale
                    // restore write on a subsequent call.
                    snapshot.ownership[maximumField] = Ownership::Lost;
                    ownership = Ownership::Lost;
                    transaction = {};
                    ++result.ownershipLost;
                } else {
                    if (readback == desiredCurrent) {
                        snapshot.lastApplied[currentField] = readback;
                        ownership = Ownership::Unowned;
                        transaction = {};
                    } else if (readback != observedCurrent || writeOk) {
                        // WHY: OFF cannot distinguish a successful write reverted to prevalue
                        // from gameplay ABA; both outcomes permanently invalidate the pair.
                        snapshot.ownership[maximumField] = Ownership::Lost;
                        ownership = Ownership::Lost;
                        transaction = {};
                        ++result.ownershipLost;
                    }
                }
                continue;
            }
            snapshot.lastApplied[currentField] = desiredCurrent;
            ownership = Ownership::Unowned;
            transaction = {};
            ++result.fieldsRestored;
        }
        RefreshAutoStatus(io, snapshot, result);
    }
    FinishResult(&result, true, result.actorsSeen != 0);
    return result;
}

bool Runtime::HasOwnedOrIndeterminateFields() const {
    for (const ActorSnapshot& snapshot : snapshots_) {
        if (!snapshot.captured) continue;
        if (snapshot.autoRefreshPending || snapshot.autoRefreshFailed) return true;
        for (Ownership ownership : snapshot.ownership) {
            if (ownership == Ownership::Owned || ownership == Ownership::Indeterminate)
                return true;
        }
    }
    return false;
}

bool ValidateAutoStatusEvidence(
    const uint8_t* removeBytes, size_t removeLength,
    const uint8_t* applyBytes, size_t applyLength) {
    return removeBytes && applyBytes && removeLength == kAutoStatusRemoveBody.size() &&
           applyLength == kAutoStatusApplyBody.size() &&
           std::memcmp(removeBytes, kAutoStatusRemoveBody.data(), removeLength) == 0 &&
           std::memcmp(applyBytes, kAutoStatusApplyBody.data(), applyLength) == 0;
}

AdapterGateCode ValidateAdapterEvidence(const AdapterEvidence& evidence) {
    if (!evidence.identity || evidence.loadedImageBase == 0 ||
        !evidence.resolverBytes || !evidence.initSceneBytes ||
        !evidence.initializerBytes || !evidence.accessorBytes) {
        return AdapterGateCode::InvalidArgument;
    }
    const ExecutableIdentity& identity = *evidence.identity;
    if (identity.machine != kSupportedMachine) return AdapterGateCode::WrongMachine;
    if (identity.timestamp != kSupportedTimestamp) return AdapterGateCode::WrongTimestamp;
    if (identity.sizeOfImage != kSupportedSizeOfImage) return AdapterGateCode::WrongImageSize;
    if (identity.imageBase != kSupportedImageBase) return AdapterGateCode::WrongImageBase;
    if (identity.sha256 != kSupportedSha256) return AdapterGateCode::WrongSha256;
    if (evidence.resolverLength < kResolveEncounterPrefix.size() ||
        std::memcmp(evidence.resolverBytes, kResolveEncounterPrefix.data(),
                    kResolveEncounterPrefix.size()) != 0) {
        return AdapterGateCode::ResolverSignatureMismatch;
    }

    constexpr std::array<HighlowOperand, 3> initSceneOperands = {{
        {2u, 0x00D2A9A8u},
        {13u, 0x00D2C9D9u},
        {20u, 0x00D2A9ACu},
    }};
    constexpr std::array<HighlowOperand, 1> initializerOperands = {{
        {10u, 0x00D36000u},
    }};
    constexpr std::array<HighlowOperand, 1> accessorOperands = {{
        {15u, 0x00D34460u},
    }};
    std::array<uint8_t, kInitSystemScenePreferredPrefix.size()> expectedInitScene{};
    std::array<uint8_t, kActorInitializerPrefix.size()> expectedInitializer{};
    std::array<uint8_t, kActorAccessorSignature.size()> expectedAccessor{};
    if (!BuildLoadedHighlowPrefix(
            kInitSystemScenePreferredPrefix, evidence.loadedImageBase,
            initSceneOperands, &expectedInitScene) ||
        !BuildLoadedHighlowPrefix(
            kActorInitializerPrefix, evidence.loadedImageBase,
            initializerOperands, &expectedInitializer) ||
        !BuildLoadedHighlowPrefix(
            kActorAccessorSignature, evidence.loadedImageBase,
            accessorOperands, &expectedAccessor)) {
        return AdapterGateCode::InvalidArgument;
    }
    if (evidence.initSceneLength < expectedInitScene.size() ||
        std::memcmp(evidence.initSceneBytes, expectedInitScene.data(),
                    expectedInitScene.size()) != 0) {
        return AdapterGateCode::InitSceneSignatureMismatch;
    }
    if (evidence.initializerLength < expectedInitializer.size() ||
        std::memcmp(evidence.initializerBytes, expectedInitializer.data(),
                    expectedInitializer.size()) != 0) {
        return AdapterGateCode::InitializerSignatureMismatch;
    }
    if (evidence.accessorLength < expectedAccessor.size() ||
        std::memcmp(evidence.accessorBytes, expectedAccessor.data(),
                    expectedAccessor.size()) != 0) {
        return AdapterGateCode::AccessorSignatureMismatch;
    }
    return AdapterGateCode::Supported;
}

bool ValidatePopulateEvidence(const uint8_t* bytes,size_t length,uintptr_t loadedBase) {
    if(!bytes || length!=kActorPopulatePreferredBody.size())return false;
    constexpr std::array<HighlowOperand,4> operands = {{{4,0x00D2A929},{10,0x00D2A8E0},
        {67,0x00D2A8E0},{80,0x00D2A929}}};
    std::array<uint8_t,kActorPopulatePreferredBody.size()> expected{};
    return BuildLoadedHighlowPrefix(kActorPopulatePreferredBody,loadedBase,operands,&expected) &&
        std::memcmp(bytes,expected.data(),expected.size())==0;
}

bool ShouldInstallAtStartup(bool minHookReady, bool validateOnly) {
    // WHY: validation-only proves profiles and build wiring; it must never turn that proof run
    // into a live detour, open callback admission, or make a Difficulty RAM write possible.
    return minHookReady && !validateOnly;
}

InstallResult HookTransaction::Install(
    const HookIo& io, const AdapterEvidence& evidence, uintptr_t target,
    void* detour, void** originalOut) {
    if (installed_.load(std::memory_order_acquire)) {
        return {AdapterGateCode::Installed, true};
    }
    if (created_) return {AdapterGateCode::HookRollbackFailed, false};
    if (originalOut) *originalOut = nullptr;
    const AdapterGateCode gate = ValidateAdapterEvidence(evidence);
    if (gate != AdapterGateCode::Supported) return {gate, false};
    if (!io.create || !io.enable || !io.disable || !io.remove || target == 0 ||
        !detour || !originalOut) {
        return {AdapterGateCode::InvalidArgument, false};
    }
    if (!io.create(io.context, target, detour, originalOut)) {
        *originalOut = nullptr;
        return {AdapterGateCode::HookCreateFailed, false};
    }
    target_ = target;
    created_ = true;
    // Once activation is attempted, another thread may already have crossed the patched
    // machine prologue even if the backend reports failure. The target and trampoline then
    // become process-lifetime storage; only admission close plus disable is safe.
    everReachable_ = true;
    quiesced_ = false;
    if (!io.enable(io.context, target)) {
        return {AdapterGateCode::HookRollbackFailed, false};
    }
    installed_ = true;
    return {AdapterGateCode::Installed, true};
}

bool HookTransaction::Remove(const HookIo& io) {
    if (!created_) return true;
    if (target_ == 0) return false;
    if (everReachable_) {
        if (quiesced_) return true;
        if (!io.disable || !io.disable(io.context, target_)) return false;
        installed_ = false;
        quiesced_ = true;
        return true;
    }
    if (!io.remove) return false;
    const bool removed = io.remove(io.context, target_);
    if (removed) {
        created_ = false;
        installed_ = false;
        target_ = 0;
    }
    return removed;
}

namespace {

bool ValidDifficultyDetourInputs(
    const DifficultyDetourIo& hookIo,
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount>& specs,
    const DifficultyDetourOwner* owner) {
    if (!hookIo.create || !hookIo.remove || !owner) return false;
    for (size_t index = 0; index < specs.size(); ++index) {
        const DifficultyDetourSpec& spec = specs[index];
        if (spec.target == 0u || !spec.detour || !spec.originalOut) return false;
        for (size_t previous = 0; previous < index; ++previous) {
            if (specs[previous].target == spec.target) return false;
        }
        if (owner->created[index] && owner->targets[index] != spec.target) return false;
    }
    return true;
}

bool AnyDifficultyDetourCreated(const DifficultyDetourOwner& owner) {
    for (const bool created : owner.created) {
        if (created) return true;
    }
    return false;
}

bool AllDifficultyDetoursCreated(const DifficultyDetourOwner& owner) {
    for (const bool created : owner.created) {
        if (!created) return false;
    }
    return true;
}

bool RemoveCreateOnlyDifficultyDetours(
    const DifficultyDetourIo& hookIo,
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount>& specs,
    DifficultyDetourOwner* owner) {
    bool complete = true;
    for (size_t reverse = specs.size(); reverse != 0u; --reverse) {
        const size_t index = reverse - 1u;
        if (!owner->created[index]) continue;
        if (!hookIo.remove(hookIo.context, owner->targets[index])) {
            complete = false;
            continue;
        }
        owner->created[index] = false;
        owner->targets[index] = 0u;
        *specs[index].originalOut = nullptr;
    }
    if (!AnyDifficultyDetourCreated(*owner)) {
        owner->active = false;
        owner->retainedInert = false;
    }
    return complete;
}

DifficultyDetourCode MapCoordinatorPreApplyFailure(
    MinHookBatch::BatchResult result) {
    switch (result) {
        case MinHookBatch::BatchResult::NotInitialized:
            return DifficultyDetourCode::CoordinatorNotReady;
        case MinHookBatch::BatchResult::Busy:
            return DifficultyDetourCode::CoordinatorBusy;
        case MinHookBatch::BatchResult::Poisoned:
            return DifficultyDetourCode::CoordinatorPoisoned;
        default:
            return DifficultyDetourCode::InvalidArgument;
    }
}

} // namespace

DifficultyDetourResult InstallDifficultyDetours(
    const DifficultyDetourIo& hookIo,
    MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo,
    MinHookBatch::NeutralizationFence fence,
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount>& specs,
    DifficultyDetourOwner* owner) {
    DifficultyDetourResult result{};
    if (!coordinator || !ValidDifficultyDetourInputs(hookIo, specs, owner)) {
        return result;
    }
    if (owner->active) {
        result.code = DifficultyDetourCode::Installed;
        return result;
    }

    const MinHookBatch::Snapshot snapshot = MinHookBatch::GetSnapshot(*coordinator);
    if (snapshot.state == MinHookBatch::State::Poisoned || owner->coordinatorPoisoned) {
        owner->coordinatorPoisoned = true;
        result.code = DifficultyDetourCode::CoordinatorPoisoned;
        return result;
    }
    if (!snapshot.initialized) {
        result.code = DifficultyDetourCode::CoordinatorNotReady;
        return result;
    }
    if (snapshot.state != MinHookBatch::State::Idle) {
        result.code = DifficultyDetourCode::CoordinatorBusy;
        return result;
    }

    for (size_t index = 0; index < specs.size(); ++index) {
        if (owner->created[index]) continue;
        *specs[index].originalOut = nullptr;
        if (!hookIo.create(
                hookIo.context, specs[index].target, specs[index].detour,
                specs[index].originalOut)) {
            const bool rolledBack = RemoveCreateOnlyDifficultyDetours(
                hookIo, specs, owner);
            result.code = rolledBack
                ? DifficultyDetourCode::HookCreateFailed
                : DifficultyDetourCode::HookRollbackFailed;
            return result;
        }
        owner->targets[index] = specs[index].target;
        owner->created[index] = true;
    }
    if (!AllDifficultyDetoursCreated(*owner)) {
        result.code = DifficultyDetourCode::HookRollbackFailed;
        return result;
    }

    result.batch = MinHookBatch::EnableBatch(
        coordinator, batchIo, MinHookBatch::Owner::Difficulty,
        owner->targets.data(), owner->targets.size(), fence);
    owner->applyAttempted = owner->applyAttempted || result.batch.applyAttempted;
    owner->mayHaveRun = owner->mayHaveRun || result.batch.mayHaveRun;

    switch (result.batch.result) {
        case MinHookBatch::BatchResult::Applied:
            owner->active = true;
            owner->retainedInert = false;
            result.code = DifficultyDetourCode::Installed;
            return result;
        case MinHookBatch::BatchResult::EnableFailedNeutralized:
            owner->active = false;
            owner->retainedInert = true;
            result.code = DifficultyDetourCode::EnableFailedRetained;
            return result;
        case MinHookBatch::BatchResult::Poisoned:
            owner->coordinatorPoisoned = true;
            if (result.batch.neutralized) {
                owner->active = false;
                owner->retainedInert = true;
            } else {
                // A failed neutralization cannot prove that no machine prologue is reachable.
                owner->active = true;
                owner->retainedInert = false;
            }
            result.code = DifficultyDetourCode::CoordinatorPoisoned;
            return result;
        default:
            break;
    }

    const DifficultyDetourCode failure = MapCoordinatorPreApplyFailure(result.batch.result);
    if (!owner->applyAttempted && !owner->mayHaveRun) {
        if (!RemoveCreateOnlyDifficultyDetours(hookIo, specs, owner)) {
            result.code = DifficultyDetourCode::HookRollbackFailed;
            return result;
        }
    }
    if (failure == DifficultyDetourCode::CoordinatorPoisoned) {
        owner->coordinatorPoisoned = true;
    }
    result.code = failure;
    return result;
}

DifficultyDetourResult RetireDifficultyDetours(
    const DifficultyDetourIo& hookIo,
    MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo,
    MinHookBatch::NeutralizationFence fence,
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount>& specs,
    DifficultyDetourOwner* owner) {
    DifficultyDetourResult result{};
    if (!ValidDifficultyDetourInputs(hookIo, specs, owner)) return result;
    if (!AnyDifficultyDetourCreated(*owner)) {
        result.code = DifficultyDetourCode::Removed;
        return result;
    }

    if (!owner->applyAttempted && !owner->mayHaveRun) {
        result.code = RemoveCreateOnlyDifficultyDetours(hookIo, specs, owner)
            ? DifficultyDetourCode::Removed
            : DifficultyDetourCode::TeardownRetryRequired;
        return result;
    }

    if (!coordinator) return result;
    const MinHookBatch::Snapshot snapshot = MinHookBatch::GetSnapshot(*coordinator);
    if (snapshot.state == MinHookBatch::State::Poisoned || owner->coordinatorPoisoned) {
        owner->coordinatorPoisoned = true;
        result.code = DifficultyDetourCode::CoordinatorPoisoned;
        return result;
    }
    if (owner->retainedInert && !owner->active) {
        result.code = DifficultyDetourCode::RetainedInert;
        return result;
    }
    if (!snapshot.initialized) {
        result.code = DifficultyDetourCode::CoordinatorNotReady;
        return result;
    }
    if (snapshot.state != MinHookBatch::State::Idle) {
        result.code = DifficultyDetourCode::CoordinatorBusy;
        return result;
    }

    result.batch = MinHookBatch::NeutralizeBatch(
        coordinator, batchIo, MinHookBatch::Owner::Difficulty,
        owner->targets.data(), owner->targets.size(), fence);
    owner->applyAttempted = owner->applyAttempted || result.batch.applyAttempted;
    owner->mayHaveRun = owner->mayHaveRun || result.batch.mayHaveRun;
    if (result.batch.result == MinHookBatch::BatchResult::Neutralized) {
        owner->active = false;
        owner->retainedInert = true;
        result.code = DifficultyDetourCode::RetainedInert;
        return result;
    }
    if (result.batch.result == MinHookBatch::BatchResult::Poisoned) {
        owner->coordinatorPoisoned = true;
        if (result.batch.neutralized) {
            owner->active = false;
            owner->retainedInert = true;
        }
        result.code = DifficultyDetourCode::CoordinatorPoisoned;
        return result;
    }
    result.code = result.batch.result == MinHookBatch::BatchResult::Busy
        ? DifficultyDetourCode::CoordinatorBusy
        : result.batch.result == MinHookBatch::BatchResult::NotInitialized
            ? DifficultyDetourCode::CoordinatorNotReady
            : DifficultyDetourCode::TeardownRetryRequired;
    return result;
}

InitializerResult RunInitializerPostOriginal(
    Runtime& runtime, uint64_t generation, const DifficultyConfig& config,
    bool configValid, int32_t fieldRow, const InitializerIo& io) {
    return RunInitializerPostOriginal(
        runtime, generation, config, configValid, fieldRow,
        SinRam::RuntimeRequest{}, io);
}

InitializerResult RunInitializerPostOriginal(
    Runtime& runtime, uint64_t generation, const DifficultyConfig& config,
    bool configValid, int32_t fieldRow, const SinRam::RuntimeRequest& sinRequest,
    const InitializerIo& io) {
    InitializerResult result{};
    if (!io.callOriginal) {
        result.runtime.code = ResultCode::Unavailable;
        return result;
    }

    result.originalReturn = io.callOriginal(io.context);
    result.originalCalled = true;
    // The unique actor initializer runs in scene-init state 0x11. Disassembly proves the
    // incremental 18-combatant populate loop runs later in state 0x12, so actor bytes here
    // still belong to the previous/pre-populate table. Begin the ownership generation now,
    // but leave the first enumeration, capture, and write to the post-populate retry.
    runtime.BeginGeneration(generation);
    result.runtime.code = ResultCode::NoActors;
    (void)config;
    (void)configValid;
    (void)fieldRow;
    (void)sinRequest;
    return result;
}

} // namespace FfxHooks::F7Difficulty
