#include "MinigameAssistCore.h"
#include "MinigameAssistCatalog.generated.h"

namespace FfxHooks::MinigameAssist {
const std::array<Script,kScriptCount>& Scripts() noexcept {return Data::scripts;}
const std::array<Rule,kRuleCount>& Rules() noexcept {return Data::rules;}
const Rule* FindRule(unsigned script,std::uint32_t offset,std::uint32_t decoded,unsigned options) noexcept {
    if(script>=kScriptCount||!(options&Data::scripts[script].option)||(decoded>>24)!=0x2E)return nullptr;
    for(const auto& rule:Data::rules)
        if(rule.script==script&&rule.offset==offset&&decoded==(0x2E000000u|rule.original))return &rule;
    return nullptr;
}
}
