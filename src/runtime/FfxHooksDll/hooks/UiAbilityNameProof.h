#pragma once
#include "LocalizedAbilityNames.generated.h"
#include "UiAbilityAliases.generated.h"
namespace FfxHooks::UiAbilityNameProof {
// Only display names are extended. Callers still prove the command/equipment
// ID, native payload, owner and consumer before admitting any effect.
inline bool Matches(unsigned id,const unsigned char* name,std::size_t available) noexcept {
    return LocalizedAbilityNames::Matches(id,name,available)||UiAbilityAliases::Matches(id,name,available);
}
}
