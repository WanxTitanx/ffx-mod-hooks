// Jarvis-HOOK: exact regional-name proof against a supplied immutable authored bank.
#include "../hooks/LocalizedAbilityNames.generated.h"
#include "../hooks/SpiraAbilityCatalog.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace L = FfxHooks::LocalizedAbilityNames;
namespace S = FfxHooks::SpiraAbilities;
static unsigned checks = 0, failures = 0;
static void Check(bool value, const char* label) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); } }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> source((std::istreambuf_iterator<char>(input)), {});
    unsigned last = 0; std::size_t pool = 0;
    if (!S::Table(source.data(), source.size(), last, pool) || last < 174) return 2;
    for (const auto& alias : L::Aliases) {
        const auto* name = reinterpret_cast<const unsigned char*>(alias.bytes);
        Check(L::Matches(alias.defaultId, name, alias.length + 1), "regional alias accepted");
        Check(!L::Matches(alias.defaultId, name, alias.length), "unterminated/truncated alias refused");
        Check(!L::Matches(alias.defaultId + 1, name, alias.length + 1), "alias cannot switch effect identity");
        Check(!L::Matches(alias.defaultId, nullptr, alias.length + 1), "null script refused");
        if (alias.defaultId < S::FirstId) continue;
        auto bank = source;
        const auto offset = bank.size() - pool;
        bank.insert(bank.end(), name, name + alias.length + 1);
        auto* row = bank.data() + 20 + alias.defaultId * 108;
        row[0] = static_cast<unsigned char>(offset); row[1] = static_cast<unsigned char>(offset >> 8);
        const auto mapping = S::DefaultMapping();
        const auto index = alias.defaultId - S::FirstId;
        auto bindings = S::Validate(bank.data(), bank.size(), mapping, nullptr, 0, S::NameEncoding::Latin, S::AllConsumers);
        Check(bindings[index].rowVerified, "localized runtime catalog proof");
        row[0x67] ^= 1;
        bindings = S::Validate(bank.data(), bank.size(), mapping, nullptr, 0, S::NameEncoding::Latin, S::AllConsumers);
        Check(!bindings[index].rowVerified && bindings[index].code == S::MappingCode::PayloadMismatch, "translation cannot authorize altered payload");
    }
    std::printf("LocalizedAbilityNamesRt0 checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
