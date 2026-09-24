#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace FfxHooks::F8Ui {

enum class CloseKind : uint8_t { None = 0, Confirm, Cancel };

struct CloseEvent {
    CloseKind kind;
    int row;
};

// The submenu input and pump share one main-thread latch so close kind and row cannot diverge.
class CloseLatch {
public:
    void Reset() noexcept { pending_ = {CloseKind::None, -1}; }
    void RequestConfirm(int row) noexcept { pending_ = {CloseKind::Confirm, row}; }
    void RequestCancel() noexcept { pending_ = {CloseKind::Cancel, -1}; }

    CloseEvent Consume() noexcept {
        const CloseEvent event = pending_;
        Reset();
        return event;
    }

private:
    CloseEvent pending_{CloseKind::None, -1};
};

// This portable draft owns no persistence. Keeping save outside the state object makes Confirm
// the only possible disk edge and lets Back/F8 discard edits without compensating writes.
class ScalarEditor {
public:
    bool Begin(int row, int value, int minimum, int maximum) noexcept {
        if (row < 0 || minimum > maximum) return false;
        row_ = row;
        minimum_ = minimum;
        maximum_ = maximum;
        draft_ = Clamp(value);
        active_ = true;
        return true;
    }

    bool Active() const noexcept { return active_; }
    int Row() const noexcept { return active_ ? row_ : -1; }
    int Draft() const noexcept { return draft_; }

    bool Adjust(int delta) noexcept {
        if (!active_) return false;
        const int64_t candidate = static_cast<int64_t>(draft_) + delta;
        draft_ = candidate < minimum_ ? minimum_
               : candidate > maximum_ ? maximum_
               : static_cast<int>(candidate);
        return true;
    }

    bool Confirm(int* valueOut) noexcept {
        if (!active_ || !valueOut) return false;
        *valueOut = draft_;
        Cancel();
        return true;
    }

    void Cancel() noexcept {
        active_ = false;
        row_ = -1;
        draft_ = 0;
        minimum_ = 0;
        maximum_ = 0;
    }

private:
    int Clamp(int value) const noexcept {
        if (value < minimum_) return minimum_;
        if (value > maximum_) return maximum_;
        return value;
    }

    bool active_ = false;
    int row_ = -1;
    int draft_ = 0;
    int minimum_ = 0;
    int maximum_ = 0;
};

enum class PendingOpenDisposition : uint8_t {
    Allocate = 0,
    RejectOther,
    RejectDirectF8,
};

struct LegacyModalOwners {
    bool maechenBlocks = false;
    bool nativeMenuOwned = false;
    bool arenaMenuOwned = false;
    bool sinMenuOwned = false;
    bool f7MenuOwned = false;
    bool composeOwned = false;
    bool heldActionOwned = false;
};

inline bool LegacyModalAllocationIdle(const LegacyModalOwners& owners) noexcept {
    return !owners.maechenBlocks && !owners.nativeMenuOwned &&
           !owners.arenaMenuOwned && !owners.sinMenuOwned &&
           !owners.f7MenuOwned && !owners.composeOwned &&
           !owners.heldActionOwned;
}

enum class ArenaOpenResult : uint8_t {
    NoRequest = 0,
    DeferredBlocked,
    Handled,
};

template <typename Handler>
inline ArenaOpenResult TryHandleArenaOpenRequest(bool requestPending,
                                                 bool allocationIdle,
                                                 Handler handle) {
    if (!requestPending) return ArenaOpenResult::NoRequest;
    if (!allocationIdle) return ArenaOpenResult::DeferredBlocked;
    return handle() ? ArenaOpenResult::Handled
                    : ArenaOpenResult::DeferredBlocked;
}

class AtomicOpenLatch {
public:
    bool Load() const noexcept { return value_.load(std::memory_order_acquire); }
    void Store(bool value) noexcept { value_.store(value, std::memory_order_release); }
    bool Exchange(bool value) noexcept {
        return value_.exchange(value, std::memory_order_acq_rel);
    }

private:
    std::atomic<bool> value_{false};
};

inline PendingOpenDisposition DecidePendingOpen(bool idle,
                                                bool directF8Flags) noexcept {
    if (idle) return PendingOpenDisposition::Allocate;
    return directF8Flags ? PendingOpenDisposition::RejectDirectF8
                         : PendingOpenDisposition::RejectOther;
}

// This rollback deliberately owns only portable F8 pre-open state. Shared
// force/menu ownership belongs to the Pump arbiter that rejected the request.
inline void RollbackRejectedDirectOpen(ScalarEditor* editor, bool* mouseWasDown,
                                       CloseLatch* closeLatch) noexcept {
    if (editor) editor->Cancel();
    if (mouseWasDown) *mouseWasDown = false;
    if (closeLatch) closeLatch->Reset();
}

inline float ResolveSelectionRowY(bool snapToSelection, float currentY,
                                  float selectedY) noexcept {
    if (snapToSelection || currentY < 0.0f) return selectedY;
    return currentY + (selectedY - currentY) * 0.30f;
}

// The native detail line is intentionally single-line; reject expansion instead of silently
// consuming another scarce game text slot or drawing through the footer.
static constexpr std::size_t TechnicalStatusCharacterBudget = 95;

struct TechnicalStatusParts {
    const char* requestEffective;
    const char* live;
    const char* applied;
    const char* edit;
};

// WHY (R7-UX-B1): the join used to fail all-or-nothing, collapsing the whole line to
// STATUS UNAVAILABLE precisely when the diagnostic mattered (e.g. an external override
// plus a rejected edit). Lowest-priority tokens are dropped first — edit, then applied,
// then live — so the request/effective half (which names the blocking artifact) always
// survives inside the 95-char budget.
inline bool BuildTechnicalStatus(const TechnicalStatusParts& parts, char* out,
                                 std::size_t outSize) noexcept {
    if (!out || outSize == 0) return false;
    for (int drop = 3; drop >= 0; --drop) {
        const char* const ordered[] = {
            parts.requestEffective, parts.live, parts.applied, parts.edit,
        };
        out[0] = '\0';
        std::size_t used = 0;
        bool fits = true;
        for (int i = 0; i < 4; ++i) {
            const char* token = ordered[i];
            if (!token || !token[0]) continue;
            if (i > drop) continue;  // tail tokens dropped first: edit, applied, live
            const std::size_t separatorLength = used == 0 ? 0u : 3u;
            const std::size_t tokenLength = std::strlen(token);
            if (separatorLength > TechnicalStatusCharacterBudget - used ||
                tokenLength > TechnicalStatusCharacterBudget - used - separatorLength ||
                separatorLength + tokenLength >= outSize - used) {
                fits = false;
                break;
            }
            if (separatorLength != 0) {
                std::memcpy(out + used, " | ", separatorLength);
                used += separatorLength;
            }
            std::memcpy(out + used, token, tokenLength);
            used += tokenLength;
            out[used] = '\0';
        }
        if (fits) return true;
    }
    out[0] = '\0';
    return false;
}

enum class FooterMode : uint8_t { Toggle = 0, Configure, ScalarEdit, Bulk };

inline const char* FooterText(FooterMode mode) noexcept {
    switch (mode) {
        case FooterMode::Toggle:
            return "U/D Navigate | L/R Tabs | Confirm Toggle | Back/F8 Exit";
        case FooterMode::Configure:
            return "U/D Navigate | L/R Tabs | Confirm Configure | Back/F8 Exit";
        case FooterMode::ScalarEdit:
            return "L/R +/-1 | U/D +/-10 | Confirm Save | Back Cancel | F8 Exit";
        case FooterMode::Bulk:
            return "U/D Navigate | L/R Tabs | Confirm Apply Tab | Back/F8 Exit";
        default:
            return "";
    }
}

enum class SpeedIndicatorLabelMode : uint8_t {
    Armed = 0,
    StandardBoost,
    FastFieldScenesArmed,
    FastFieldScenes,
    Paused,
    Conflict,
    Unavailable,
    MovieArmed,
    MovieApplied,
};

static constexpr std::size_t SpeedIndicatorCharacterBudget = 63;

inline bool BuildSpeedIndicatorLabel(unsigned factor, SpeedIndicatorLabelMode mode,
                                     char* out, std::size_t outSize,const char* shortcut="Ctrl+Shift+K") noexcept {
    if (!out || outSize == 0) return false;
    const char* format = nullptr;
    switch (mode) {
        case SpeedIndicatorLabelMode::Armed:
            format = "Speed Hack %ux - Armed [%s]";
            break;
        case SpeedIndicatorLabelMode::StandardBoost:
            format = "Speed Hack %ux - Standard boost ARMED";
            break;
        case SpeedIndicatorLabelMode::FastFieldScenesArmed:
            format = "Speed Hack %ux - Fast field scenes ARMED";
            break;
        case SpeedIndicatorLabelMode::FastFieldScenes:
            format = "Speed Hack %ux - Fast field scenes APPLIED";
            break;
        case SpeedIndicatorLabelMode::Paused:
            format = "Speed Hack %ux - Native booster unavailable";
            break;
        case SpeedIndicatorLabelMode::Conflict:
            format = "Speed Hack %ux - Conflict";
            break;
        case SpeedIndicatorLabelMode::Unavailable:
            format = "Speed Hack %ux - Unavailable";
            break;
        case SpeedIndicatorLabelMode::MovieArmed:
            format = "Speed Hack %ux - FMV ARMED";
            break;
        case SpeedIndicatorLabelMode::MovieApplied:
            format = "Speed Hack %ux - FMV APPLIED";
            break;
        default:
            out[0] = '\0';
            return false;
    }
    const int written = std::snprintf(out, outSize, format, factor,shortcut?shortcut:"Unassigned");
    if (written < 0 || static_cast<std::size_t>(written) > SpeedIndicatorCharacterBudget ||
        static_cast<std::size_t>(written) >= outSize) {
        out[0] = '\0';
        return false;
    }
    return true;
}

enum class SwitchboardRowKind : uint8_t {
    Invalid = 0,
    AuroraActor,
    AuroraDetail,
    Refresh,
    Plugin,
};

struct SwitchboardRow {
    SwitchboardRowKind kind;
    int pluginIndex;
};

class SwitchboardRowMapper {
public:
    SwitchboardRowMapper(bool developerEnabled, int pluginCount) noexcept
        : developerEnabled_(developerEnabled), pluginCount_(pluginCount > 0 ? pluginCount : 0) {}

    int Count() const noexcept {
        return pluginCount_ + 1 + (developerEnabled_ ? 2 : 0);
    }

    SwitchboardRow At(int row) const noexcept {
        if (row < 0 || row >= Count()) return {SwitchboardRowKind::Invalid, -1};
        if (developerEnabled_) {
            if (row == 0) return {SwitchboardRowKind::AuroraActor, -1};
            if (row == 1) return {SwitchboardRowKind::AuroraDetail, -1};
            row -= 2;
        }
        if (row == 0) return {SwitchboardRowKind::Refresh, -1};
        return {SwitchboardRowKind::Plugin, row - 1};
    }

private:
    bool developerEnabled_;
    int pluginCount_;
};

inline const char* SwitchboardStaticLabel(SwitchboardRowKind kind) noexcept {
    switch (kind) {
        case SwitchboardRowKind::AuroraActor: return "Aurora actor labels";
        case SwitchboardRowKind::AuroraDetail: return "Aurora detail labels";
        case SwitchboardRowKind::Refresh: return "Refresh plugin list";
        default: return nullptr;
    }
}

inline bool ConvertClientScreenPointToMenu(
    int screenX, int screenY, int clientLeft, int clientTop,
    int clientWidth, int clientHeight, float menuWidth, float menuHeight,
    float* menuX, float* menuY) noexcept {
    if (!menuX || !menuY || clientWidth <= 0 || clientHeight <= 0 ||
        menuWidth <= 0.0f || menuHeight <= 0.0f) {
        return false;
    }
    *menuX = (static_cast<float>(screenX - clientLeft) /
              static_cast<float>(clientWidth)) * menuWidth;
    *menuY = (static_cast<float>(screenY - clientTop) /
              static_cast<float>(clientHeight)) * menuHeight;
    return true;
}

using ShowCursorAdapter = int (*)(bool show, void* context) noexcept;

class CursorVisibilityBalance {
public:
    bool Acquire(ShowCursorAdapter showCursor, void* context) noexcept {
        if (!showCursor) return false;
        if (ownedIncrements_ != 0) return true;
        int balance = -1;
        do {
            // Parenthesize max so this portable header remains safe after Win32 macro definitions.
            if (ownedIncrements_ == (std::numeric_limits<uint32_t>::max)()) return false;
            balance = showCursor(true, context);
            ++ownedIncrements_;
        } while (balance < 0);
        return true;
    }

    bool Release(ShowCursorAdapter showCursor, void* context) noexcept {
        if (ownedIncrements_ == 0) return true;
        if (!showCursor) return false;
        while (ownedIncrements_ != 0) {
            showCursor(false, context);
            --ownedIncrements_;
        }
        return true;
    }

    uint32_t OwnedIncrements() const noexcept { return ownedIncrements_; }

private:
    uint32_t ownedIncrements_ = 0;
};

struct Layout {
    static constexpr float TabLeft = 0.190f;
    static constexpr float TabWidth = 0.620f;
    static constexpr float TabGap = 0.006f;
    // Keep tabs inside the panel while preserving the nine-row budget before technical detail.
    static constexpr float TabTop = 0.211f;
    static constexpr float TabHeight = 0.034f;
    static constexpr float RowTop = 0.259f;
    static constexpr float RowStep = 0.061f;
    static constexpr float RowHeight = 0.054f;
    static constexpr float DetailTop = 0.817f;
    static constexpr float MainPanelTop = 0.200f;
    static constexpr float MainPanelHeight = 0.670f;
    static constexpr float FooterTop = 0.887f;
    static constexpr int VisibleRows = 9;
};

} // namespace FfxHooks::F8Ui
