#pragma once
#include "SeymourSessionCore.h"
namespace FfxHooks::SeymourSession {
void PrimeSaveIo(std::uintptr_t base,bool validateOnly);
bool PublisherReady() noexcept;
Token Capture(bool cleanup=false) noexcept;
bool Current(const Token&,bool cleanup=false) noexcept;
void RequestStop() noexcept;
bool Remove() noexcept;
}
