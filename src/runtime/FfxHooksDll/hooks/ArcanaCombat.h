#pragma once
#include <cstdint>
#include "SinProducerCore.h"
namespace FfxHooks::Arcana::Combat {
bool Start(std::uintptr_t,bool requested,bool validateOnly,void(*log)(const char*));
void Stop() noexcept;
bool Active() noexcept;
unsigned PartyDropMultiplier() noexcept;
bool OwnsNaturalProducer(std::uintptr_t) noexcept;
bool OwnsRewardProducer(std::uintptr_t) noexcept;
bool AttachSinObservers(std::uintptr_t,const SinProducer::Observers*) noexcept;
void DetachSinObservers(const SinProducer::Observers*) noexcept;
}
