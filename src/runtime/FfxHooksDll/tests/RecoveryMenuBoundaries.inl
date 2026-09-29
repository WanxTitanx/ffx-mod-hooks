// These endpoints are outside the Workshop/F8 harness. Recovery's own actual-
// source adapter suites exercise them; this host must never access game memory.
#include "../hooks/SeymourCompatibilityHook.h"
#include "../hooks/SeymourOverdriveHook.h"
#include "../hooks/SeymourGearPresentationHook.h"
#include "../hooks/SeymourGearSortHook.h"
#include "../hooks/SeymourPersistentRosterHook.h"
#include "../hooks/SeymourMenuListHook.h"
#include "../hooks/SphereGridProgress8Runtime.h"
namespace PhotoMode {
int MenuCount() noexcept {return 26;}
void MenuLabel(int,char* out,std::size_t size) noexcept {if(out&&size)*out=0;}
bool MenuAction(int) noexcept {return false;}
void Detail(char* out,std::size_t size) noexcept {if(out&&size)*out=0;}
}
namespace FfxHooks::SeymourCompatibility {
void MenuLabel(int,char* out,std::size_t size){if(out&&size)*out=0;}
bool MenuAction(int){return false;}
void Detail(char* out,std::size_t size){if(out&&size)*out=0;}
}
#define RECOVERY_MENU_BOUNDARY(ns) namespace FfxHooks::ns { \
void MenuLabel(char* out,std::size_t size){if(out&&size)*out=0;} \
bool MenuAction(){return false;} \
void Detail(char* out,std::size_t size){if(out&&size)*out=0;} }
RECOVERY_MENU_BOUNDARY(SeymourOverdrive)
RECOVERY_MENU_BOUNDARY(SeymourGearPresentation)
RECOVERY_MENU_BOUNDARY(SeymourGearSort)
RECOVERY_MENU_BOUNDARY(SeymourPersistentRoster)
RECOVERY_MENU_BOUNDARY(SeymourMenuList)
RECOVERY_MENU_BOUNDARY(SphereGridProgress8Runtime)
#undef RECOVERY_MENU_BOUNDARY
