#pragma once
#include "ArcanaCore.h"

namespace FfxHooks::Arcana::CombatCore {
struct Facts {bool black=false,white=false,item=false,ordinary=false,overdrive=false,fixed=false,fractional=false;std::uint8_t elements=0;};
Facts Classify(const unsigned char* command,std::size_t size,unsigned id,unsigned weaponElement,unsigned weaponFormula) noexcept;
struct Action {
    std::uint64_t token=0;
    std::uint64_t healed=0;
    std::uint32_t shared=0;
    bool kill=false,defend=false;
};
struct Ledger {
    std::uint64_t battle=0;
    std::array<bool,kActorCount> judgement{},firstAction{},opening{};
    std::array<Action,kActorCount> actions{};
    void BeginBattle(std::uint64_t epoch) noexcept;
    Action* BeginAction(unsigned actor,std::uint64_t token) noexcept;
    std::uint32_t ShareHealing(unsigned actor,std::uint64_t token,std::uint32_t actual,std::uint32_t maxHp,unsigned percent,unsigned capPercent) noexcept;
    bool Kill(unsigned actor,std::uint64_t token) noexcept;
    bool Survive(unsigned actor) noexcept;
};
}
