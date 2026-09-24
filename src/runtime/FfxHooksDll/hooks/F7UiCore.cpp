#include "F7UiCore.h"

#include <cstring>

namespace FfxHooks::F7Ui {
namespace {

constexpr int kDefaultHotkey = 0x76;  // VK_F7

int ClampInt(int value, int minimum, int maximum) noexcept {
    if (minimum > maximum) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

}  // namespace

CloseEffects CloseModal(
    ModalState& state,
    CloseSource source,
    CloseDestination destination) noexcept {
    (void)source;
    CloseEffects effects{};
    effects.closeMenu = state.open;
    effects.releaseInputBlock = state.inputBlockOwned;
    effects.releaseCursor = state.cursorOwned;
    effects.clearNativeGate = state.nativeGateOwned;
    effects.releaseForceGate = state.forceGateOwned;
    effects.cancelDraft = state.draftActive;
    effects.clearNavigation = state.selection != 0 || state.firstVisible != 0 ||
                              state.submenu != -1;
    effects.changed = effects.closeMenu || effects.releaseInputBlock || effects.releaseCursor ||
                      effects.clearNativeGate || effects.releaseForceGate || effects.cancelDraft ||
                      effects.clearNavigation;
    effects.returnToHub = effects.changed && destination == CloseDestination::Hub;
    if (!effects.changed) return effects;

    state.open = false;
    state.inputBlockOwned = false;
    state.cursorOwned = false;
    state.nativeGateOwned = false;
    state.forceGateOwned = false;
    state.draftActive = false;
    state.selection = 0;
    state.firstVisible = 0;
    state.submenu = -1;
    ++state.cleanupGeneration;
    return effects;
}

bool AdmitInput(bool enabled, bool foreground, bool modalOwned) noexcept {
    return enabled && foreground && modalOwned;
}

HotkeyResolution ResolveNativeMenuHotkey(int requestedVirtualKey) noexcept {
    HotkeyResolution result{};
    result.effectiveVirtualKey = requestedVirtualKey;
    if (requestedVirtualKey <= 0 || requestedVirtualKey >= 256) {
        result.reason = HotkeyReason::Invalid;
    } else if (requestedVirtualKey == 0x77) {
        result.reason = HotkeyReason::F8Reserved;
    } else if (requestedVirtualKey == 0x2D) {
        result.reason = HotkeyReason::InsertReserved;
    } else if (requestedVirtualKey == 0x78) {
        result.reason = HotkeyReason::F9Reserved;
    } else if (requestedVirtualKey == 0x7B) {
        result.reason = HotkeyReason::F12Reserved;
    } else if (requestedVirtualKey == 0x4B) {
        // A plain-VK configuration cannot distinguish K from Ctrl+Shift+K, so
        // reserving K prevents F7 from stealing the Speed Hack chord.
        result.reason = HotkeyReason::SpeedChordReserved;
    } else {
        result.reason = HotkeyReason::None;
        return result;
    }
    result.effectiveVirtualKey = kDefaultHotkey;
    return result;
}

const char* HotkeyReasonText(HotkeyReason reason) noexcept {
    switch (reason) {
        case HotkeyReason::None: return "";
        case HotkeyReason::Invalid: return "Invalid hotkey; remapped to F7";
        case HotkeyReason::F8Reserved: return "F8 is reserved; remapped to F7";
        case HotkeyReason::InsertReserved: return "Insert is reserved; remapped to F7";
        case HotkeyReason::F9Reserved: return "F9 is reserved; remapped to F7";
        case HotkeyReason::F12Reserved: return "F12 is reserved; remapped to F7";
        case HotkeyReason::SpeedChordReserved:
            return "K is reserved for Ctrl+Shift+K; remapped to F7";
        default: return "Reserved hotkey; remapped to F7";
    }
}

int AdjustNumeric(
    int current,
    int direction,
    int fineStep,
    int coarseStep,
    int minimum,
    int maximum) noexcept {
    if (minimum > maximum || fineStep <= 0 || coarseStep <= 0 || direction == 0) {
        return ClampInt(current, minimum, maximum);
    }
    const int step = direction == -2 || direction == 2 ? coarseStep : fineStep;
    const int sign = direction < 0 ? -1 : 1;
    const long long candidate = static_cast<long long>(current) +
                                static_cast<long long>(sign) * step;
    if (candidate < static_cast<long long>(minimum)) return minimum;
    if (candidate > static_cast<long long>(maximum)) return maximum;
    return static_cast<int>(candidate);
}

void SeedPointerForDestination(PointerState& state, bool physicalButtonDown) noexcept {
    // Do not infer a release from object replacement. A held source click owns
    // no destination action until an observed release arms the next edge.
    state.positionKnown = false;
    state.buttonDown = physicalButtonDown;
    state.suppressUntilRelease = physicalButtonDown;
}

PointerDecision ObservePointer(PointerState& state, const PointerSample& sample) noexcept {
    PointerDecision decision{};
    if (!sample.valid) return decision;

    decision.moved = state.positionKnown && (sample.x != state.x || sample.y != state.y);
    if (state.suppressUntilRelease) {
        if (!sample.buttonDown) state.suppressUntilRelease = false;
    } else {
        decision.pressAdmitted = sample.buttonDown && !state.buttonDown;
    }
    decision.applyHover = decision.moved || sample.wheelSteps != 0 || decision.pressAdmitted;

    state.positionKnown = true;
    state.buttonDown = sample.buttonDown;
    state.x = sample.x;
    state.y = sample.y;
    return decision;
}

ListPointerResolution ResolveListPointerInput(
    int displayedSelection,
    const Hit& hit,
    const PointerDecision& pointer) noexcept {
    ListPointerResolution result{};
    result.selection = displayedSelection;
    const bool validHit = hit.kind != HitKind::None && hit.index >= 0;
    if (pointer.applyHover && validHit) result.selection = hit.index;
    result.confirm = pointer.pressAdmitted && validHit;
    // Only a click on an actionable hit owns the whole frame. Movement and
    // wheel hover remain deterministic while controller navigation stays live.
    result.ownsDirectionalFrame = result.confirm;
    return result;
}

int ResolveDirectionalInput(int direction, bool pointerOwnsFrame) noexcept {
    return pointerOwnsFrame ? 0 : direction;
}

Hit HitTestRows(
    float x,
    float y,
    const ListGeometry& geometry,
    int firstVisible,
    int rowCount) noexcept {
    if (geometry.width <= 0.0f || geometry.step <= 0.0f ||
        geometry.rowHeight <= 0.0f || geometry.rowHeight > geometry.step ||
        geometry.visibleRows <= 0 || firstVisible < 0 || rowCount <= 0 ||
        x < geometry.left || x >= geometry.left + geometry.width || y < geometry.top) {
        return {};
    }
    const float relativeY = y - geometry.top;
    const int visibleIndex = static_cast<int>(relativeY / geometry.step);
    if (visibleIndex < 0 || visibleIndex >= geometry.visibleRows) return {};
    const float offset = relativeY - static_cast<float>(visibleIndex) * geometry.step;
    if (offset < 0.0f || offset >= geometry.rowHeight) return {};
    const int row = firstVisible + visibleIndex;
    if (row < 0 || row >= rowCount) return {};
    return Hit{HitKind::Row, row};
}

Hit HitTestTabs(float x, float y, const TabGeometry& geometry) noexcept {
    if (geometry.count <= 0 || geometry.width <= 0.0f || geometry.height <= 0.0f ||
        geometry.gap < 0.0f || x < geometry.left || x >= geometry.left + geometry.width ||
        y < geometry.top || y >= geometry.top + geometry.height) {
        return {};
    }
    const float totalGap = geometry.gap * static_cast<float>(geometry.count - 1);
    const float tabWidth = (geometry.width - totalGap) / static_cast<float>(geometry.count);
    if (tabWidth <= 0.0f) return {};
    const float cellWidth = tabWidth + geometry.gap;
    const float relativeX = x - geometry.left;
    const int tab = static_cast<int>(relativeX / cellWidth);
    if (tab < 0 || tab >= geometry.count) return {};
    const float offset = relativeX - static_cast<float>(tab) * cellWidth;
    if (offset < 0.0f || offset >= tabWidth) return {};
    return Hit{HitKind::Tab, tab};
}

void ScrollList(
    int wheelSteps,
    int rowCount,
    int visibleRows,
    int& selection,
    int& firstVisible) noexcept {
    if (rowCount <= 0 || visibleRows <= 0) {
        selection = 0;
        firstVisible = 0;
        return;
    }
    selection = ClampInt(selection + wheelSteps, 0, rowCount - 1);
    const int maxTop = rowCount > visibleRows ? rowCount - visibleRows : 0;
    if (selection < firstVisible) firstVisible = selection;
    if (selection >= firstVisible + visibleRows) firstVisible = selection - visibleRows + 1;
    firstVisible = ClampInt(firstVisible, 0, maxTop);
}

void CopyBoundedStatus(const char* source, char* destination, std::size_t capacity) noexcept {
    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    if (!source) return;
    const std::size_t sourceLength = std::strlen(source);
    if (sourceLength < capacity) {
        std::memcpy(destination, source, sourceLength + 1);
        return;
    }
    const std::size_t payload = capacity - 1;
    if (payload >= 3) {
        const std::size_t prefix = payload - 3;
        if (prefix > 0) std::memcpy(destination, source, prefix);
        destination[prefix] = '.';
        destination[prefix + 1] = '.';
        destination[prefix + 2] = '.';
        destination[payload] = '\0';
        return;
    }
    if (payload > 0) std::memcpy(destination, source, payload);
    destination[payload] = '\0';
}

}  // namespace FfxHooks::F7Ui
