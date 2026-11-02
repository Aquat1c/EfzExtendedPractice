#pragma once
#include "../include/utils/utilities.h"

#include <string>

namespace CharacterSettings {
    std::string GetCharacterName(int charID);

    std::string GetCharacterInternalName(int charID);
    
    int GetCharacterID(const std::string& name);
    
    bool IsCharacter(const std::string& name, int charID);
    
    void UpdateCharacterIDs(DisplayData& data);
    
    void ReadCharacterValues(uintptr_t base, DisplayData& data, bool forceRead = false);
    
    void ApplyCharacterValues(uintptr_t base, const DisplayData& data, bool forceApply = false);

    // Per-tick enforcement (no threads); call periodically (~16 Hz) from the
    // main monitor thread.
    void TickCharacterEnforcements(uintptr_t base, const DisplayData& data);

    void ApplyRumiModeNow(int playerIndex, bool barehanded);

    // Clears cached per-character pointers (recomputed on the next Read/Apply);
    // call when (re)entering a valid game mode to avoid stale addresses.
    void InvalidateAllCharacterPointerCaches();
}
