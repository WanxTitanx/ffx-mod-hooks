#include "ArcanaCombatCore.h"
#include <algorithm>

namespace FfxHooks::Arcana::CombatCore {
Facts Classify(const unsigned char* row,std::size_t size,unsigned id,unsigned weaponElement,unsigned weaponFormula) noexcept {
    Facts facts;if(!row||size<96)return facts;
    facts.black=row[24]==1;facts.white=row[24]==2;facts.overdrive=row[24]==4;
    facts.item=(id&0xF000u)==0x2000u;facts.ordinary=id==0||id==0x3000u;
    const bool weapon=(row[30]&4)!=0;
    facts.elements=static_cast<std::uint8_t>(weapon?weaponElement:row[45]);
    const unsigned formula=weapon?weaponFormula:row[40];
    switch(formula){
    case 5:case 8:case 10:case 11:case 12:case 13:case 16:facts.fractional=true;break;
    case 0:case 6:case 9:case 21:case 22:case 23:facts.fixed=true;break;
    default:break;
    }
    return facts;
}
void Ledger::BeginBattle(std::uint64_t epoch) noexcept {
    if(battle==epoch)return;
    *this={};battle=epoch;
}
Action* Ledger::BeginAction(unsigned actor,std::uint64_t token) noexcept {
    if(!battle||actor>=kActorCount||!token)return nullptr;
    auto& action=actions[actor];if(action.token!=token){action={};action.token=token;}
    return &action;
}
std::uint32_t Ledger::ShareHealing(unsigned actor,std::uint64_t token,std::uint32_t actual,std::uint32_t maxHp,unsigned percent,unsigned capPercent) noexcept {
    auto* action=BeginAction(actor,token);
    if(!action||!actual||!percent||percent>100||capPercent>100)return 0;
    const auto cap=std::uint64_t(maxHp)*capPercent/100;
    if(action->shared>=cap)return 0;
    action->healed+=actual;
    const auto total=(std::min)(cap,action->healed*percent/100);
    const auto added=static_cast<std::uint32_t>(total-action->shared);
    action->shared=static_cast<std::uint32_t>(total);return added;
}
bool Ledger::Kill(unsigned actor,std::uint64_t token) noexcept {
    auto* action=BeginAction(actor,token);if(!action||action->kill)return false;
    action->kill=true;return true;
}
bool Ledger::Survive(unsigned actor) noexcept {
    if(!battle||actor>=kActorCount||judgement[actor])return false;
    judgement[actor]=true;return true;
}
}
