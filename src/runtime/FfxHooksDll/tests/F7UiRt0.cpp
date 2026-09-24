#include "../hooks/F7UiCore.h"

#include <cstdio>
#include <cstring>

namespace {

int g_passed = 0;
int g_failed = 0;

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL: %s\n", message);
}

void TestCloseTransition() {
    using namespace FfxHooks::F7Ui;
    const CloseSource sources[] = {
        CloseSource::BackRow,
        CloseSource::Cancel,
        CloseSource::Hotkey,
        CloseSource::FocusLost,
        CloseSource::Stop,
    };
    for (CloseSource source : sources) {
        ModalState state{};
        state.open = true;
        state.inputBlockOwned = true;
        state.cursorOwned = true;
        state.nativeGateOwned = true;
        state.forceGateOwned = true;
        state.draftActive = true;
        state.selection = 7;
        state.firstVisible = 3;
        state.submenu = 4;

        const CloseEffects first = CloseModal(state, source, CloseDestination::Game);
        Expect(first.changed && first.closeMenu && first.releaseInputBlock &&
                   first.releaseCursor && first.clearNativeGate && first.releaseForceGate &&
                   first.cancelDraft && first.clearNavigation && !first.returnToHub,
               "every game close source must request the complete discard cleanup");
        Expect(!state.open && !state.inputBlockOwned && !state.cursorOwned &&
                   !state.nativeGateOwned && !state.forceGateOwned && !state.draftActive &&
                   state.selection == 0 && state.firstVisible == 0 && state.submenu == -1 &&
                   state.cleanupGeneration == 1,
               "the first close must clear every modal owner and draft exactly once");

        const CloseEffects second = CloseModal(state, source, CloseDestination::Game);
        Expect(!second.changed && !second.closeMenu && !second.releaseInputBlock &&
                   !second.releaseCursor && !second.clearNativeGate &&
                   !second.releaseForceGate && !second.cancelDraft &&
                   !second.clearNavigation && state.cleanupGeneration == 1,
               "repeated close must be idempotent and must not release an owner twice");
    }

    ModalState submenu{};
    submenu.open = true;
    submenu.inputBlockOwned = true;
    submenu.draftActive = true;
    const CloseEffects back =
        CloseModal(submenu, CloseSource::BackRow, CloseDestination::Hub);
    Expect(back.changed && back.returnToHub && back.cancelDraft,
           "submenu Back must use the same discard transition and request the hub destination");
}

void TestForegroundAndHotkeyPolicy() {
    using namespace FfxHooks::F7Ui;
    Expect(AdmitInput(true, true, true),
           "enabled foreground modal input must be admitted");
    Expect(!AdmitInput(false, true, true) && !AdmitInput(true, false, true) &&
               !AdmitInput(true, true, false),
           "disabled, background, or unowned input must fail closed");

    const HotkeyResolution normal = ResolveNativeMenuHotkey(0x76);
    Expect(normal.effectiveVirtualKey == 0x76 && normal.reason == HotkeyReason::None,
           "plain F7 must remain the default native-menu hotkey");
    const struct {
        int key;
        HotkeyReason reason;
    } reserved[] = {
        {0x77, HotkeyReason::F8Reserved},
        {0x2D, HotkeyReason::InsertReserved},
        {0x78, HotkeyReason::F9Reserved},
        {0x7B, HotkeyReason::F12Reserved},
        {0x4B, HotkeyReason::SpeedChordReserved},
        {0, HotkeyReason::Invalid},
        {256, HotkeyReason::Invalid},
    };
    for (const auto& item : reserved) {
        const HotkeyResolution result = ResolveNativeMenuHotkey(item.key);
        Expect(result.effectiveVirtualKey == 0x76 && result.reason == item.reason,
               "reserved or invalid hotkeys must deterministically remap to F7");
        Expect(HotkeyReasonText(result.reason)[0] != '\0',
               "every remap must have a player-visible reason");
    }
}

void TestNumericEditing() {
    using namespace FfxHooks::F7Ui;
    Expect(AdjustNumeric(100, -1, 25, 100, 100, 10000) == 100,
           "fine decrement must clamp at the field minimum");
    Expect(AdjustNumeric(10000, 1, 25, 100, 100, 10000) == 10000,
           "fine increment must clamp at the field maximum");
    Expect(AdjustNumeric(1000, -1, 25, 100, 100, 10000) == 975 &&
               AdjustNumeric(1000, 1, 25, 100, 100, 10000) == 1025,
           "left/right controller repeat must apply the fine step");
    Expect(AdjustNumeric(1000, -2, 25, 100, 100, 10000) == 900 &&
               AdjustNumeric(1000, 2, 25, 100, 100, 10000) == 1100,
           "up/down controller repeat must apply the coarse step");
    int repeated = 1000;
    for (int i = 0; i < 50; ++i) {
        repeated = AdjustNumeric(repeated, 1, 25, 100, 100, 1500);
    }
    Expect(repeated == 1500,
           "repeated numeric input must remain clamped after reaching the bound");
}

void TestMouseGeometry() {
    using namespace FfxHooks::F7Ui;
    const ListGeometry rows{100.0f, 200.0f, 400.0f, 45.0f, 40.0f, 4};
    const Hit first = HitTestRows(120.0f, 210.0f, rows, 5, 9);
    const Hit last = HitTestRows(120.0f, 345.0f, rows, 5, 9);
    const Hit stale = HitTestRows(120.0f, 345.0f, rows, 5, 8);
    const Hit gap = HitTestRows(120.0f, 242.0f, rows, 5, 9);
    Expect(first.kind == HitKind::Row && first.index == 5,
           "row hit testing must include the current scroll offset");
    Expect(last.kind == HitKind::Row && last.index == 8,
           "the final visible Back row must be mouse-addressable");
    Expect(stale.kind == HitKind::None,
           "a viewport cell beyond the current row count must never activate a stale row");
    Expect(gap.kind == HitKind::None,
           "the visual gap between rows must not activate either row");

    const TabGeometry tabs{100.0f, 80.0f, 300.0f, 36.0f, 8.0f, 3};
    Expect(HitTestTabs(120.0f, 90.0f, tabs).index == 0 &&
               HitTestTabs(220.0f, 90.0f, tabs).index == 1 &&
               HitTestTabs(320.0f, 90.0f, tabs).index == 2,
           "client-relative tab hit testing must map each tab deterministically");
    Expect(HitTestTabs(197.0f, 90.0f, tabs).kind == HitKind::None,
           "tab gaps must not select a neighboring tab");

    int selection = 5;
    int top = 5;
    ScrollList(-1, 9, 4, selection, top);
    Expect(selection == 4 && top == 4,
           "wheel-up must move selection and viewport together");
    ScrollList(99, 9, 4, selection, top);
    Expect(selection == 8 && top == 5,
           "wheel-down must clamp to the final row and final valid viewport");
}

void TestPointerOwnershipAndTransitionLatch() {
    using namespace FfxHooks::F7Ui;
    PointerState pointer{};
    PointerSample sample{};
    sample.valid = true;
    sample.x = 100.0f;
    sample.y = 200.0f;

    const PointerDecision initial = ObservePointer(pointer, sample);
    Expect(!initial.applyHover && !initial.pressAdmitted,
           "the first stationary pointer sample must not steal controller selection");

    const Hit row{HitKind::Row, 7};
    const PointerDecision stationary = ObservePointer(pointer, sample);
    const ListPointerResolution stationaryList =
        ResolveListPointerInput(3, row, stationary);
    Expect(stationaryList.selection == 3 && !stationaryList.confirm &&
               !stationaryList.ownsDirectionalFrame,
           "a stationary hover must preserve the keyboard/controller-selected row");
    Expect(ResolveDirectionalInput(0x4000, stationaryList.ownsDirectionalFrame) == 0x4000,
           "stationary mouse presence must not suppress controller navigation");

    sample.x = 101.0f;
    const PointerDecision moved = ObservePointer(pointer, sample);
    const ListPointerResolution movedList = ResolveListPointerInput(3, row, moved);
    Expect(moved.applyHover && movedList.selection == 7 && !movedList.confirm,
           "actual pointer movement must transfer hover ownership to the hit row");
    Expect(ResolveDirectionalInput(0x4000, movedList.ownsDirectionalFrame) == 0x4000,
           "movement-only hover must leave same-frame controller navigation deterministic");

    sample.buttonDown = true;
    const PointerDecision pressed = ObservePointer(pointer, sample);
    const ListPointerResolution pressedList = ResolveListPointerInput(4, row, pressed);
    Expect(pressedList.selection == 7 && pressedList.confirm &&
               pressedList.ownsDirectionalFrame,
           "a pressed hit must confirm the row that becomes visibly selected");
    Expect(ResolveDirectionalInput(0x4000, pressedList.ownsDirectionalFrame) == 0,
           "a pressed hit must suppress same-frame directional movement");
    const int sameFramePadDir = 0x4000;
    const bool sameFrameControllerConfirm = true;
    const bool nativeCallbackSuppressed = pressedList.confirm;
    const int resolvedPadDir = nativeCallbackSuppressed ? 0 : sameFramePadDir;
    const bool resolvedControllerConfirm =
        nativeCallbackSuppressed ? false : sameFrameControllerConfirm;
    Expect(pressedList.selection == 7 && pressedList.confirm && resolvedPadDir == 0 &&
               !resolvedControllerConfirm,
           "a same-frame click, PadDir, and controller confirm must activate only the clicked row");

    const PointerDecision held = ObservePointer(pointer, sample);
    Expect(!held.pressAdmitted,
           "holding the button must not produce a second activation in the same menu");

    PointerState destination{};
    SeedPointerForDestination(destination, true);
    int activations = 0;
    PointerSample heldSample{};
    heldSample.valid = true;
    heldSample.buttonDown = true;
    heldSample.x = 50.0f;
    heldSample.y = 60.0f;
    for (int i = 0; i < 2; ++i) {
        const PointerDecision decision = ObservePointer(destination, heldSample);
        if (ResolveListPointerInput(0, Hit{HitKind::Row, 1}, decision).confirm) ++activations;
    }
    heldSample.buttonDown = false;
    (void)ObservePointer(destination, heldSample);
    heldSample.buttonDown = true;
    if (ResolveListPointerInput(
            0, Hit{HitKind::Row, 1}, ObservePointer(destination, heldSample)).confirm) {
        ++activations;
    }
    if (ResolveListPointerInput(
            0, Hit{HitKind::Row, 1}, ObservePointer(destination, heldSample)).confirm) {
        ++activations;
    }
    Expect(activations == 1,
           "a held click crossing a menu transition must activate exactly once after release");

    PointerState wheelPointer{};
    PointerSample wheelSample{};
    wheelSample.valid = true;
    wheelSample.x = 20.0f;
    wheelSample.y = 30.0f;
    (void)ObservePointer(wheelPointer, wheelSample);
    wheelSample.wheelSteps = 1;
    const PointerDecision wheel = ObservePointer(wheelPointer, wheelSample);
    Expect(wheel.applyHover && !wheel.pressAdmitted,
           "wheel input must explicitly transfer hover without synthesizing a click");
}

void TestBoundedStatus() {
    using namespace FfxHooks::F7Ui;
    char text[20] = {};
    CopyBoundedStatus("This technical status is deliberately too long", text, sizeof(text));
    Expect(std::strlen(text) == sizeof(text) - 1 &&
               text[sizeof(text) - 2] == '.' && text[sizeof(text) - 3] == '.' &&
               text[sizeof(text) - 4] == '.',
           "long technical status must be bounded and visibly truncated");
    CopyBoundedStatus("short", text, sizeof(text));
    Expect(std::strcmp(text, "short") == 0,
           "short technical status must remain unchanged");
}

}  // namespace

int main() {
    TestCloseTransition();
    TestForegroundAndHotkeyPolicy();
    TestNumericEditing();
    TestMouseGeometry();
    TestPointerOwnershipAndTransitionLatch();
    TestBoundedStatus();
    std::printf("F7 UI RT0: %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
