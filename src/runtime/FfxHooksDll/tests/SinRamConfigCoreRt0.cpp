#include "../hooks/SinRamConfigCore.h"
#include "../hooks/F7DifficultyCore.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

namespace {

using FfxHooks::SinRam::Config;
using FfxHooks::SinRamConfig::Code;
using FfxHooks::SinRamConfig::ParseDocument;
using FfxHooks::SinRamConfig::ParseResult;
using FfxHooks::SinRamConfig::SerializeResult;
using FfxHooks::SinRamConfig::SerializeValue;

using ParseSignature = ParseResult (*)(const char*, std::size_t, Config*);
static_assert(
    std::is_same<decltype(&ParseDocument), ParseSignature>::value,
    "the portable parser must own only the S.I.N. output capability");

int g_passed = 0;
int g_failed = 0;

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL: %s\n", message);
}

ParseResult Parse(const std::string& document, Config* output) {
    return ParseDocument(document.data(), document.size(), output);
}

void ExpectDefault(const Config& config, const char* message) {
    Expect(!config.enabled && config.threatLevel == 0, message);
}

void TestDefaultsAndUnrelatedF7Members() {
    Config config{true, 2};
    ParseResult result = Parse("{}", &config);
    Expect(result.code == Code::Ok && !result.present,
           "an absent sinRam object must be a valid absent result");
    ExpectDefault(config, "an absent sinRam object must publish OFF/T0 defaults");

    const std::string unrelated =
        R"({"version":1,"diff_enabled":true,"areas":[{"fieldRow":310}],"music_lock":-1})";
    config = {true, 2};
    result = Parse(unrelated, &config);
    Expect(result.code == Code::Ok && !result.present,
           "valid unrelated F7 members must not be interpreted as S.I.N. state");
    ExpectDefault(config, "unrelated F7 members must leave S.I.N. at OFF/T0");

    result = Parse(R"({"sinRam":{}})", &config);
    Expect(result.code == Code::Ok && result.present,
           "an empty sinRam object must be present and valid");
    ExpectDefault(config, "missing sinRam members must independently use defaults");

    result = Parse(R"({"sinRam":{"enabled":true}})", &config);
    Expect(result.code == Code::Ok && result.present && config.enabled &&
               config.threatLevel == 0,
           "a missing threatLevel must default to T0 without discarding enabled");

    result = Parse(R"({"sinRam":{"threatLevel":2}})", &config);
    Expect(result.code == Code::Ok && result.present && !config.enabled &&
               config.threatLevel == 2,
           "a missing enabled member must default OFF without discarding threatLevel");
}

void TestCanonicalAndCaseInsensitiveKeys() {
    Config config{};
    ParseResult result = Parse(
        R"({"sinRam":{"enabled":true,"threatLevel":2}})", &config);
    Expect(result.code == Code::Ok && result.present && config.enabled &&
               config.threatLevel == 2,
           "the two canonical persisted members must parse exactly");

    result = Parse(
        R"({"SINRAM":{"ENABLED":false,"THREATLEVEL":1}})", &config);
    Expect(result.code == Code::Ok && result.present && !config.enabled &&
               config.threatLevel == 1,
           "ASCII case variants must share one semantic key identity");

    result = Parse(
        R"({"\u0073inRam":{"\u0065nabled":true,"threat\u004cevel":1}})",
        &config);
    Expect(result.code == Code::Ok && result.present && config.enabled &&
               config.threatLevel == 1,
           "ASCII Unicode escapes must not evade semantic key recognition");
}

void TestDuplicateAndUnknownKeysFailClosed() {
    struct IndependentState {
        Config sin{true, 2};
        bool difficultyValid = true;
    } state;

    const char* duplicateDocuments[] = {
        R"({"sinRam":{},"sinRam":{}})",
        R"({"sinRam":{},"SINRAM":{}})",
        R"({"sinRam":{"enabled":true,"enabled":false}})",
        R"({"sinRam":{"enabled":true,"ENABLED":false}})",
        R"({"sinRam":{"threatLevel":1,"THREATLEVEL":2}})",
    };
    for (const char* document : duplicateDocuments) {
        state.sin = {true, 2};
        const ParseResult result = Parse(document, &state.sin);
        Expect(result.code == Code::DuplicateKey,
               "exact and case-insensitive duplicate S.I.N. keys must be rejected");
        ExpectDefault(state.sin,
                      "a duplicate-key failure must publish only neutral S.I.N. state");
        Expect(state.difficultyValid,
               "a S.I.N. duplicate-key failure must not invalidate Difficulty");
    }

    state.sin = {true, 2};
    const ParseResult unknown = Parse(
        R"({"sinRam":{"enabled":true,"threatLevel":1,"future":0}})",
        &state.sin);
    Expect(unknown.code == Code::UnknownKey,
           "the sinRam object must reject members outside its exact two-key contract");
    ExpectDefault(state.sin, "an unknown S.I.N. member must fail to OFF/T0");
    Expect(state.difficultyValid,
           "an unknown S.I.N. member must not change Difficulty validity");
}

void TestDifficultyAndSinParseIndependently() {
    const std::string invalidSin =
        R"({"diff_enabled":true,"diff_hpMul":1500,"music_lock":16,"force_repeat":3,"sinRam":{"enabled":true,"threatLevel":3}})";
    Config sin{true, 2};
    FfxHooks::F7Difficulty::DifficultyConfig difficulty =
        FfxHooks::F7Difficulty::MakeNeutralConfig();
    const ParseResult sinResult = Parse(invalidSin, &sin);
    const FfxHooks::F7Difficulty::ConfigResult difficultyResult =
        FfxHooks::F7Difficulty::ParseConfig(
            invalidSin.data(), invalidSin.size(), &difficulty);
    Expect(sinResult.code == Code::OutOfRange && !sin.enabled &&
               sin.threatLevel == 0,
           "an invalid sinRam member must fail only its own parser to OFF/T0");
    Expect(difficultyResult.code == FfxHooks::F7Difficulty::ConfigCode::Ok &&
               difficulty.global.enabled && difficulty.global.hpMul == 1500,
           "an invalid sinRam member must leave valid Difficulty data usable");

    const std::string invalidDifficulty =
        R"({"diff_enabled":true,"diff_elemWeak":32,"sinRam":{"enabled":true,"threatLevel":2}})";
    sin = {};
    difficulty = FfxHooks::F7Difficulty::MakeNeutralConfig();
    const ParseResult validSin = Parse(invalidDifficulty, &sin);
    const FfxHooks::F7Difficulty::ConfigResult invalidDiff =
        FfxHooks::F7Difficulty::ParseConfig(
            invalidDifficulty.data(), invalidDifficulty.size(), &difficulty);
    Expect(validSin.code == Code::Ok && sin.enabled && sin.threatLevel == 2,
           "valid sinRam data must survive an independently invalid Difficulty member");
    Expect(invalidDiff.code != FfxHooks::F7Difficulty::ConfigCode::Ok,
           "the Difficulty parser must still reject its own invalid multiplier");
}

void TestWrongTypesAndRangesFailClosed() {
    const char* wrongTypeDocuments[] = {
        R"({"sinRam":null})",
        R"({"sinRam":[]})",
        R"({"sinRam":{"enabled":1}})",
        R"({"sinRam":{"enabled":"true"}})",
        R"({"sinRam":{"enabled":null}})",
        R"({"sinRam":{"threatLevel":true}})",
        R"({"sinRam":{"threatLevel":"1"}})",
        R"({"sinRam":{"threatLevel":1.0}})",
        R"({"sinRam":{"threatLevel":1e0}})",
    };
    for (const char* document : wrongTypeDocuments) {
        Config config{true, 2};
        const ParseResult result = Parse(document, &config);
        Expect(result.code == Code::WrongType,
               "boolean, integer, and object type mismatches must be distinguished and rejected");
        ExpectDefault(config, "a S.I.N. type failure must publish OFF/T0");
    }

    const char* outOfRangeDocuments[] = {
        R"({"sinRam":{"threatLevel":-1}})",
        R"({"sinRam":{"threatLevel":3}})",
        R"({"sinRam":{"threatLevel":999999999999999999999999999999}})",
    };
    for (const char* document : outOfRangeDocuments) {
        Config config{true, 2};
        const ParseResult result = Parse(document, &config);
        Expect(result.code == Code::OutOfRange,
               "threatLevel values outside zero through two must be rejected without clamping");
        ExpectDefault(config, "an out-of-range S.I.N. value must publish OFF/T0");
    }
}

void TestMalformedDepthAndSizeLimits() {
    const char* malformedDocuments[] = {
        "",
        R"({"sinRam":{"enabled":true,}})",
        R"({"sinRam":{"enabled":true}} trailing)",
        R"({"sinRam":{"enabled":tru}})",
        R"({"other":01})",
        R"({"other":"\uD800"})",
        R"({"sinRam":{"enabled":true})",
    };
    for (const char* document : malformedDocuments) {
        Config config{true, 2};
        const ParseResult result = Parse(document, &config);
        Expect(result.code == Code::Malformed,
               "malformed JSON and trailing bytes must be rejected");
        ExpectDefault(config, "malformed JSON must publish OFF/T0");
    }

    std::string embeddedNul = R"({"sinRam":{}})";
    embeddedNul.push_back('\0');
    embeddedNul.push_back('x');
    Config config{true, 2};
    ParseResult result = Parse(embeddedNul, &config);
    Expect(result.code == Code::Malformed,
           "the supplied byte length must expose embedded NUL and trailing garbage");
    ExpectDefault(config, "an embedded-NUL failure must publish OFF/T0");

    std::string maximumDepth = R"({"other":)";
    for (std::size_t i = 1; i < FfxHooks::SinRamConfig::kMaximumJsonDepth; ++i) {
        maximumDepth.push_back('[');
    }
    maximumDepth.push_back('0');
    for (std::size_t i = 1; i < FfxHooks::SinRamConfig::kMaximumJsonDepth; ++i) {
        maximumDepth.push_back(']');
    }
    maximumDepth.push_back('}');
    result = Parse(maximumDepth, &config);
    Expect(result.code == Code::Ok,
           "the documented maximum JSON container depth must remain usable");

    std::string excessiveDepth = R"({"other":)";
    for (std::size_t i = 0; i < FfxHooks::SinRamConfig::kMaximumJsonDepth; ++i) {
        excessiveDepth.push_back('[');
    }
    excessiveDepth.push_back('0');
    for (std::size_t i = 0; i < FfxHooks::SinRamConfig::kMaximumJsonDepth; ++i) {
        excessiveDepth.push_back(']');
    }
    excessiveDepth.push_back('}');
    result = Parse(excessiveDepth, &config);
    Expect(result.code == Code::DepthExceeded,
           "JSON nesting beyond the fixed parser stack budget must fail closed");
    ExpectDefault(config, "a depth failure must publish OFF/T0");

    std::string maximumSize = "{}";
    maximumSize.append(
        FfxHooks::SinRamConfig::kMaximumDocumentBytes - maximumSize.size(), ' ');
    result = Parse(maximumSize, &config);
    Expect(result.code == Code::Ok,
           "a valid document exactly at the byte cap must parse");

    maximumSize.push_back(' ');
    config = {true, 2};
    result = Parse(maximumSize, &config);
    Expect(result.code == Code::TooLarge,
           "a document one byte beyond the fixed cap must be rejected before parsing");
    ExpectDefault(config, "an oversized document must publish OFF/T0");

    config = {true, 2};
    result = ParseDocument(nullptr, 0, &config);
    Expect(result.code == Code::InvalidArgument,
           "a null input pointer must be rejected as an API error");
    ExpectDefault(config, "an invalid input pointer must still neutralize S.I.N. output");
}

void TestDeterministicValueSerializationAndRoundTrip() {
    char output[64] = {};
    Config config{};
    SerializeResult serialized = SerializeValue(config, output, sizeof(output));
    const char* expectedDefault = R"({"enabled":false,"threatLevel":0})";
    Expect(serialized.code == Code::Ok &&
               serialized.length == std::strlen(expectedDefault) &&
               std::strcmp(output, expectedDefault) == 0,
           "default serialization must emit one deterministic nested JSON value");

    config = {true, 2};
    serialized = SerializeValue(config, output, sizeof(output));
    const char* expectedEnabled = R"({"enabled":true,"threatLevel":2})";
    Expect(serialized.code == Code::Ok &&
               serialized.length == std::strlen(expectedEnabled) &&
               std::strcmp(output, expectedEnabled) == 0,
           "enabled T2 serialization must use canonical order and spelling");

    const std::string document =
        std::string(R"({"version":1,"sinRam":)") +
        std::string(output, serialized.length) +
        R"(,"diff_enabled":true})";
    Config roundTripped{};
    const ParseResult parsed = Parse(document, &roundTripped);
    Expect(parsed.code == Code::Ok && parsed.present && roundTripped.enabled &&
               roundTripped.threatLevel == 2,
           "the serialized value must splice into the existing atomic F7 root object");

    char exactWithoutTerminator[32] = {'x'};
    serialized = SerializeValue(
        config, exactWithoutTerminator, sizeof(exactWithoutTerminator));
    Expect(serialized.code == Code::OutputTooSmall && serialized.length == 0 &&
               exactWithoutTerminator[0] == '\0',
           "serialization must reserve a terminator and clear a too-small destination");

    config.threatLevel = 3;
    output[0] = 'x';
    serialized = SerializeValue(config, output, sizeof(output));
    Expect(serialized.code == Code::OutOfRange && serialized.length == 0 &&
               output[0] == '\0',
           "serialization must reject an invalid in-memory threat level without clamping");

    serialized = SerializeValue(Config{}, nullptr, 0);
    Expect(serialized.code == Code::InvalidArgument && serialized.length == 0,
           "serialization must reject a missing destination");
}

}  // namespace

int main() {
    TestDefaultsAndUnrelatedF7Members();
    TestCanonicalAndCaseInsensitiveKeys();
    TestDuplicateAndUnknownKeysFailClosed();
    TestDifficultyAndSinParseIndependently();
    TestWrongTypesAndRangesFailClosed();
    TestMalformedDepthAndSizeLimits();
    TestDeterministicValueSerializationAndRoundTrip();

    std::printf(
        "S.I.N. RAM config core RT0: %d/%d passed\n",
        g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
