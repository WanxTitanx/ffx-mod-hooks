#pragma once

#include <cstddef>
#include <cstdint>

namespace FfxHooks::F7Ui {

enum class CloseSource : std::uint8_t {
    BackRow,
    Cancel,
    Hotkey,
    FocusLost,
    Stop,
};

enum class CloseDestination : std::uint8_t {
    Hub,
    Game,
};

// This portable mirror records only resources owned by the F7 modal shell. The
// Win32 adapter performs each effect returned by CloseModal and never guesses
// ownership from a process-global cursor counter or another menu's gate.
struct ModalState {
    bool open = false;
    bool inputBlockOwned = false;
    bool cursorOwned = false;
    bool nativeGateOwned = false;
    bool forceGateOwned = false;
    bool draftActive = false;
    int selection = 0;
    int firstVisible = 0;
    int submenu = -1;
    std::uint32_t cleanupGeneration = 0;
};

struct CloseEffects {
    bool changed = false;
    bool closeMenu = false;
    bool releaseInputBlock = false;
    bool releaseCursor = false;
    bool clearNativeGate = false;
    bool releaseForceGate = false;
    bool cancelDraft = false;
    bool clearNavigation = false;
    bool returnToHub = false;
};

CloseEffects CloseModal(
    ModalState& state,
    CloseSource source,
    CloseDestination destination) noexcept;

bool AdmitInput(bool enabled, bool foreground, bool modalOwned) noexcept;

enum class HotkeyReason : std::uint8_t {
    None,
    Invalid,
    F8Reserved,
    InsertReserved,
    F9Reserved,
    F12Reserved,
    SpeedChordReserved,
};

struct HotkeyResolution {
    int effectiveVirtualKey = 0x76;  // VK_F7 without a Win32 header dependency.
    HotkeyReason reason = HotkeyReason::None;
};

HotkeyResolution ResolveNativeMenuHotkey(int requestedVirtualKey) noexcept;
const char* HotkeyReasonText(HotkeyReason reason) noexcept;

// direction is -1/+1 for fine adjustment and -2/+2 for coarse adjustment.
// Repeated calls intentionally remain clamped so the adapter may feed the
// game's controller/keyboard repeat stream without a second timing policy.
int AdjustNumeric(
    int current,
    int direction,
    int fineStep,
    int coarseStep,
    int minimum,
    int maximum) noexcept;

enum class HitKind : std::uint8_t {
    None,
    Row,
    Tab,
};

struct Hit {
    HitKind kind = HitKind::None;
    int index = -1;
};

// Pointer state is intentionally independent from modal resource ownership.
// A destination may be allocated while the click that selected it is still
// physically held, so modal teardown must not manufacture a new rising edge.
struct PointerState {
    bool positionKnown = false;
    bool buttonDown = false;
    bool suppressUntilRelease = false;
    float x = 0.0f;
    float y = 0.0f;
};

struct PointerSample {
    bool valid = false;
    bool buttonDown = false;
    int wheelSteps = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct PointerDecision {
    bool moved = false;
    bool applyHover = false;
    bool pressAdmitted = false;
};

struct ListPointerResolution {
    int selection = 0;
    bool confirm = false;
    bool ownsDirectionalFrame = false;
};

void SeedPointerForDestination(PointerState& state, bool physicalButtonDown) noexcept;
PointerDecision ObservePointer(PointerState& state, const PointerSample& sample) noexcept;
ListPointerResolution ResolveListPointerInput(
    int displayedSelection,
    const Hit& hit,
    const PointerDecision& pointer) noexcept;
int ResolveDirectionalInput(int direction, bool pointerOwnsFrame) noexcept;

struct ListGeometry {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float step = 0.0f;
    float rowHeight = 0.0f;
    int visibleRows = 0;
};

struct TabGeometry {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float gap = 0.0f;
    int count = 0;
};

Hit HitTestRows(
    float x,
    float y,
    const ListGeometry& geometry,
    int firstVisible,
    int rowCount) noexcept;
Hit HitTestTabs(float x, float y, const TabGeometry& geometry) noexcept;
void ScrollList(
    int wheelSteps,
    int rowCount,
    int visibleRows,
    int& selection,
    int& firstVisible) noexcept;

void CopyBoundedStatus(const char* source, char* destination, std::size_t capacity) noexcept;

}  // namespace FfxHooks::F7Ui
