#pragma once
#include "ArcanaCore.h"
namespace FfxHooks::Arcana::Acquisition {
inline constexpr std::size_t kPayloadBytes=0x6034;
const char* Requirement(unsigned card) noexcept;
unsigned Reconcile(State&,const unsigned char* nativePayload,std::size_t size) noexcept;
}
