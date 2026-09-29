#pragma once
#include "SeymourCompatibilityCore.h"
#include <cstddef>

namespace FfxHooks::SeymourCompatibility {
enum class InstallState : unsigned {NotStarted,Off,ValidateOnly,Unavailable,Installed,RestorePending,Stopped};
struct Snapshot {
    InstallState state=InstallState::NotStarted;
    unsigned installed=0,requested=0,filtered=0,visibilityChanges=0;
    bool restorePending=false;
};
void Start(std::uintptr_t moduleBase,bool validateOnly,void (*log)(const char*));
void PresentTick(); // Config publication only, never game RAM writes.
void RequestStop() noexcept; // Loader-lock safe, no waiting or restoration.
bool RestoreAtBattleBoundary(); // Owning game thread, before actor/save lifetime ends.
bool Remove(); // Normal context; retains code and originals after publication.
Snapshot GetSnapshot() noexcept;
inline int MenuCount() noexcept {return 3;}
void MenuLabel(int row,char* output,std::size_t capacity);
bool MenuAction(int row);
void Detail(char* output,std::size_t capacity);
}

namespace FfxHooks {
// Published by the existing Seymour roster owner, not a second battle runtime.
bool CaptureSeymourCompatibilityScope(SeymourCompatibility::Scope*,bool cleanup) noexcept;
}
