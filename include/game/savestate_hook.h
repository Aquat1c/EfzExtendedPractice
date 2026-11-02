#pragma once

#include <cstdint>

// Hooks Revival's Practice save/load so mod-side state is captured and restored around them.
// This is the mod's only savestate path.
namespace SavestateHook {
    bool Install();

    void Uninstall();

    bool TriggerSave();
    bool TriggerLoad();

    // J inlines save/load inside its hotkey dispatcher; these callbacks bracket it so mod-state
    // tracking survives without a second hook at the same address.
    uint8_t BeginInlinePracticeHotkey(void* practiceController, int key);
    void EndInlinePracticeHotkey(uint8_t actionMask);

    bool IsInstalled();
    // Game thread only: lazy Practice-battle install, retried at most every 5 s.
    void EnsureInstalled();

    unsigned int GetSaveCount();

    unsigned int GetLoadCount();
}
