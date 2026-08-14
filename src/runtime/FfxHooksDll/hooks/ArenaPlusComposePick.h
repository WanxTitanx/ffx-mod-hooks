#pragma once

#ifdef FFXHOOKS_HAVE_POLYHOOK

#include <windows.h>

typedef void (*ArenaPlusComposeLogFn)(const char* fmt, ...);

enum class ArenaPlusComposePollKind : int {
    None = 0,
    Nav,
    Back,
    Launch,        /* fresh subprocess --compose completed */
    LaunchCached,  /* manifest + carrier bin match tier — skip compose */
};

struct ArenaPlusComposePollResult {
    ArenaPlusComposePollKind what = ArenaPlusComposePollKind::None;
    int combo = -1;
};

void ArenaPlusComposePick_SetLog(ArenaPlusComposeLogFn fn);
void ArenaPlusComposePick_SetModule(HMODULE module);

bool ArenaPlusComposePick_IsCustomMixCombo(int combo);
bool ArenaPlusComposePick_IsEnabled();
bool ArenaPlusComposePick_IsActive();
bool ArenaPlusComposePick_IsBusy();

bool ArenaPlusComposePick_Open(int combo);
void ArenaPlusComposePick_Close();
void ArenaPlusComposePick_Tick();

ArenaPlusComposePollResult ArenaPlusComposePick_PollMenu();

int ArenaPlusComposePick_PendingGilCost();

/* Custom Mix: override field/group/formation from compose_last.json (scenario launch route). */
bool ArenaPlusComposePick_ApplyLaunchRouteOverride(
    int combo,
    int* field,
    int* group,
    int* formation,
    char* backdropBattleId,
    int backdropBattleIdCap,
    int* backdropBattlefieldId);

#endif // FFXHOOKS_HAVE_POLYHOOK
