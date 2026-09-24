#include <cstdio>
#include <cstdint>

#if __has_include("../hooks/ArenaMixPolicy.h")
#include "../hooks/ArenaMixPolicy.h"

using namespace FfxHooks::CustomMixUltra;
namespace Policy = FfxHooks::ArenaMix;
static int passed = 0;
static int failed = 0;
static void Expect(bool condition, const char* message) {
    if (condition) ++passed;
    else { ++failed; std::printf("FAIL: %s\n", message); }
}

int main() {
    Policy::Rules rules{};
    for (unsigned index = 0; index < 8; ++index) {
        const auto choice = static_cast<MonsterChoice>(index);
        Expect(!Policy::ChoiceUnlocked(choice, rules), "default progression locks every undefeated boss");
        rules.defeatedMask = static_cast<std::uint16_t>(1u << index);
        Expect(Policy::ChoiceUnlocked(choice, rules), "defeating a boss unlocks that boss");
        Expect(!Policy::ChoiceUnlocked(static_cast<MonsterChoice>((index + 1) % 8), rules),
               "one defeat cannot unlock another boss");
        rules.defeatedMask = 0;
        rules.bypass = true;
        Expect(Policy::ChoiceUnlocked(choice, rules), "explicit bypass unlocks valid symbolic bosses");
        rules.bypass = false;
    }
    rules.bypass = true;
    Expect(!Policy::ChoiceUnlocked(static_cast<MonsterChoice>(8), rules), "bypass never admits Penance/raw IDs");
    Expect(!Policy::ChoiceUnlocked(static_cast<MonsterChoice>(255), rules), "invalid choices remain rejected");
    SelectionInput selection{};
    Expect(!Policy::CanLaunch(selection, rules), "empty selection cannot launch with bypass");
    selection.activationCount = 3;
    selection.activations[0] = MonsterChoice::Valefor;
    selection.activations[1] = MonsterChoice::Ifrit;
    selection.activations[2] = MonsterChoice::Valefor;
    rules.bypass = false;
    rules.defeatedMask = 1;
    Expect(!Policy::CanLaunch(selection, rules), "launch rechecks every boss after bypass is disabled");
    rules.defeatedMask = 3;
    rules.requiredSlots = 3;
    Expect(Policy::CanLaunch(selection, rules), "x3 accepts three unlocked slots including repeated bosses");
    rules.requiredSlots = 4;
    Expect(!Policy::CanLaunch(selection, rules), "x4 cannot launch three slots");
    Expect(Policy::CanAdd(selection, MonsterChoice::Ifrit, rules), "x4 accepts its fourth single boss");
    Expect(!Policy::CanAdd(selection, MonsterChoice::Magus, rules), "locked Magus cannot be added");
    rules.bypass = true;
    Expect(!Policy::CanAdd(selection, MonsterChoice::Magus, rules), "bypass cannot overflow the chosen x4 capacity");
    rules.requiredSlots = 0;
    Expect(Policy::CanAdd(selection, MonsterChoice::Magus, rules), "Ultra accepts Magus when three positions fit");
    selection = {};
    selection.activationCount = 1;
    selection.activations[0] = MonsterChoice::Magus;
    rules.requiredSlots = 3;
    Expect(Policy::CanLaunch(selection, rules), "Magus occupies exactly three positions in x3");
    Expect(!Policy::CanAdd(selection, MonsterChoice::Valefor, rules), "full x3 preserves its draft");
    rules.requiredSlots = 5;
    Expect(!Policy::CanLaunch(selection, rules), "x5 requires five expanded positions");
    selection.activationCount = 3;
    selection.activations[1] = MonsterChoice::Valefor;
    selection.activations[2] = MonsterChoice::Ifrit;
    Expect(Policy::CanLaunch(selection, rules), "Magus plus two single bosses fills x5");
    rules.requiredSlots = 9;
    Expect(!Policy::CanLaunch(selection, rules) && !Policy::CanAdd(selection, MonsterChoice::Ifrit, rules),
           "invalid editor capacity cannot broaden the writer boundary");
    rules.requiredSlots = 0;
    selection = {};
    selection.activationCount = 8;
    Expect(Policy::CanLaunch(selection, rules), "Ultra accepts eight repeated unlocked bosses");
    Expect(!Policy::CanAdd(selection, MonsterChoice::Ifrit, rules), "Ultra rejects a ninth boss");
    rules.bypass = false;
    rules.defeatedMask = 0;
    Expect(!Policy::CanLaunch(selection, rules), "a new save cannot inherit the previous save's unlocks");
    const auto dingo=static_cast<MonsterChoice>(0x109u);
    rules={};
    Expect(!Policy::ChoiceUnlocked(dingo,rules),"ordinary monsters wait for the shared Custom Mix unlock");
    rules.defeatedMask=1;
    Expect(Policy::ChoiceUnlocked(dingo,rules),"ordinary monsters do not require captures after Mix unlock");
    unsigned creations=0;
    for(const auto& entry:FfxHooks::ArenaMonsters::kEntries) {
        SelectionInput one{};one.activationCount=1;one.activations[0]=entry.choice;
        const auto result=BuildSelection(one);
        Expect((result.result==SelectionResult::Ready)==(entry.count!=0),"catalog support is checked before any writer");
        if(entry.unlockIndex<0)continue;
        ++creations;rules={};rules.defeatedMask=1;
        const auto bit=std::uint64_t{1}<<entry.unlockIndex;
        Expect(!Policy::ChoiceUnlocked(entry.choice,rules),"unreadable arena byte cannot unlock a creation");
        rules.arenaKnownMask=bit;
        Expect(!Policy::ChoiceUnlocked(entry.choice,rules),"known zero arena byte stays locked");
        rules.arenaUnlockedMask=bit;
        Expect(Policy::ChoiceUnlocked(entry.choice,rules),"its own arena flag unlocks a creation");
        rules.arenaUnlockedMask=std::uint64_t{1}<<((entry.unlockIndex+1)%35);
        Expect(!Policy::ChoiceUnlocked(entry.choice,rules),"another creation flag never leaks an unlock");
        rules.bypass=true;
        Expect(Policy::ChoiceUnlocked(entry.choice,rules),"explicit progression bypass still works");
    }
    Expect(creations==35,"all 35 area/species/original creation flags are mapped");
    rules={};rules.bypass=true;rules.requiredSlots=3;
    Expect(!Policy::ChoiceUnlocked(dingo,rules),"normal Mix retains its Dark-only roster");
    Expect(!Policy::ChoiceUnlocked(static_cast<MonsterChoice>(0x1158u),rules),"native IDs are not symbolic tokens");
    std::printf("Arena Mix policy RT0: %d/%d passed\n", passed, passed + failed);
    return failed ? 1 : 0;
}
#else
int main() {
    std::puts("FAIL: Arena Mix has no progression/capacity policy for the RAM editor");
    return 1;
}
#endif
