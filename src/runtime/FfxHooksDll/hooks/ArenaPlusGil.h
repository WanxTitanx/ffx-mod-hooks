#pragma once

#include <stdint.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK

/* Arena+ Gil economy — per-Dark-Aeon costs + pick-key sums (anti-farm). */

bool ArenaPlus_IsChargeGilEnabled();

int ArenaPlus_GilCostForDarkIndex(int dark);

int ArenaPlus_GilCostForPickKey(const char* key);

int ArenaPlus_GilCostSumPickKeys(const char* const* keys, int count);

bool ArenaPlus_ReadGilForCompose(uint32_t* gil, uint32_t* status, uint32_t* err);

#endif // FFXHOOKS_HAVE_POLYHOOK
