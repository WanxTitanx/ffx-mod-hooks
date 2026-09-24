#pragma once

namespace FfxHooks {

using DashLogFn = void (*)(const char* message);
struct DashShortcutSample {bool down=false,mismatch=false;};
using DashShortcutReader=DashShortcutSample(*)();
void Dash_SetShortcutReader(DashShortcutReader);

struct DashHotkeyState {
    bool keyWasDown = false;
    bool chordSuppressed = false;
};

inline bool DashConsumeFocusedHotkeyEdge(
    DashHotkeyState* state,
    bool foreground,
    bool keyDown,
    bool modifiersDown) noexcept {
    if (!state) return false;
    if (!keyDown) state->chordSuppressed = false;
    if (keyDown && modifiersDown) state->chordSuppressed = true;
    const bool edge = foreground && keyDown && !state->keyWasDown &&
                      !state->chordSuppressed;
    state->keyWasDown = keyDown;
    return edge;
}

bool Dash_Install(DashLogFn log);
void Dash_Uninstall();
void Dash_Tick(bool foreground);
bool Dash_F8Pressed();

} // namespace FfxHooks
