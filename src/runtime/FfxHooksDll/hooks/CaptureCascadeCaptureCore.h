#pragma once
#include "CaptureCascadeCore.h"
namespace FfxHooks::CaptureCascade {
struct CaptureEvidence {
    bool enabled=false,bound=false,nativeCaller=false,battle=false,matchingActor=false,death=false;
    unsigned slot=0,monster=0,arena=0,marker=0;
};
inline unsigned CapturedFamily(const CaptureEvidence& e) noexcept {
    if(!e.enabled||!e.bound||!e.nativeCaller||!e.battle||!e.matchingActor||!e.death||
       e.slot<18||e.slot>=26||e.arena!=255||e.marker!=1||e.monster<=0x1000||e.monster>=0x1400)return kNoFamily;
    return FamilyForMonster(e.monster-0x1000);
}
}
