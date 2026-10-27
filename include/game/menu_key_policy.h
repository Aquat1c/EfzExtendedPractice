#pragma once

// Battle-hotkey gate for the training-menu key. Game thread only.
struct MenuKeyLatch { bool escGuard = false; bool keyHeld = false; };
struct MenuKeyStep  { bool openMenu; bool passNative; };

// escOwned: Esc is bound to the menu or a Tutorial/Runner owns it.
inline MenuKeyStep StepMenuKey(MenuKeyLatch& l, bool uiVisible, bool escOwned, bool escDown, bool menuDown) {
    if (uiVisible) { l.escGuard = true; l.keyHeld = menuDown; return {false, false}; }
    if (!escDown) l.escGuard = false;
    if (escDown && l.escGuard) return {false, false};        // Esc that closed our UI never reaches EFZ
    if (escDown && escOwned) menuDown = true;
    if (menuDown) { const bool edge = !l.keyHeld; l.keyHeld = true; return {edge, false}; }
    l.keyHeld = false;
    return {false, true};
}
