#pragma once

#include <cstdint>

// Hooks EfzRevival's Practice save/load functions so the mod can track
// Revival-owned savestate activity and restore mod-side state around it; this
// is the mod's sole savestate path (save/load run through EfzRevival's native
// functions plus our mod-state capture/restore layer).
namespace SavestateHook {
    // True if both hooks were successfully installed.
    bool Install();

    void Uninstall();

    // Runs EfzRevival's native save/load on the captured Practice controller
    // (with the mod-state capture/restore layer). False if the hook isn't
    // installed or the Practice controller could not be resolved.
    bool TriggerSave();
    bool TriggerLoad();

    // J inlines native save/load inside its hotkey dispatcher; these callbacks
    // bracket it so mod-state tracking survives without a second hook at the
    // same address.
    uint8_t BeginInlinePracticeHotkey(void* practiceController, int key);
    void EndInlinePracticeHotkey(uint8_t actionMask);

    bool IsInstalled();
    // Game thread only: lazy Practice-battle install, retried at most every 5 s.
    void EnsureInstalled();

    unsigned int GetSaveCount();

    unsigned int GetLoadCount();
}
