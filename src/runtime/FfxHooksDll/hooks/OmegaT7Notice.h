#pragma once
#include "ArcanaFieldPresentation.h"
namespace FfxHooks::OmegaT7::Notice {
void Start() noexcept;
void Stop() noexcept;
void RequestReview() noexcept;
const Arcana::FieldPresentation::SupplementalSource& Source() noexcept;
}
