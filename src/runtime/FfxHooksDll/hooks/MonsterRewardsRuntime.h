#pragma once
#include "MonsterRewardsCore.h"

namespace FfxHooks::MonsterRewards {
using RuntimeLog=void(*)(const char*);
enum class Source : unsigned {Unavailable,ModFile,LiveActor};
struct Preview {
    unsigned species=0,apMultiplier=1,gilMultiplier=1,globalAp=1,globalGil=1;
    char name[64]{};
    BaseRewards base{};
    Source source=Source::Unavailable;
    bool apValid=true,gilValid=true,globalApValid=true,globalGilValid=true,running=false;
    Calculation ap{},overkillAp{},gil{};
};
bool Prepare(std::uintptr_t image,bool validateOnly,RuntimeLog log);
void TickMainThread() noexcept;
void RequestStop() noexcept;
bool Installed() noexcept;
unsigned Count() noexcept;
bool SpeciesAt(unsigned row,unsigned& species) noexcept;
bool ReadPreview(unsigned species,Preview& output) noexcept;
bool SaveMultiplier(unsigned species,Kind kind,unsigned value);
const char* Detail() noexcept;
}
