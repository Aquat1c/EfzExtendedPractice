#include "../src/game/collision_display.cpp"
#include <cstdio>

namespace {
Config::Settings settings{};
int failures = 0;
int nativeReads = 0;
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
}

namespace Config {
const Settings& GetSettings() { return settings; }
void SetSetting(const std::string&, const std::string&, const std::string&) {}
}
void LogOut(const std::string&, bool) {}
int MapDIKToVK(int) { return 0; }
uintptr_t GetEFZBase() { ++nativeReads; return 0; }
uintptr_t GetPlayerBase(int) { ++nativeReads; return 0; }
bool SafeReadMemory(uintptr_t, void*, size_t) { ++nativeReads; return false; }
GameMode GetCurrentGameMode(uint8_t*) { ++nativeReads; return GameMode::Practice; }
bool IsNetplaySuspendActive() { return false; }
bool IsNetplaySessionActive() { return false; }

int main() {
    using namespace CollisionDisplay;
    Check(settings.collisionDisplayEnabled, "master visibility defaults on");
    settings.collisionDisplayHitboxes = true;
    settings.collisionDisplayCollisionBoxes = true;
    settings.collisionDisplayProjectileInteractions = true;
    settings.collisionDisplayP2Hitboxes = true;
    settings.collisionDisplayP1CollisionBoxes = true;
    settings.collisionDisplayProjectileOrigins = true;
    const uint8_t selectedMask = CurrentLayerMaskFromSettings();
    Check(selectedMask == 13 && IsAnyLayerEnabled(), "selected layers are visible");

    settings.collisionDisplayEnabled = false;
    Check(!IsAnyLayerEnabled(), "master hide suppresses render admission");
    Check(CurrentLayerMaskFromSettings() == selectedMask, "master hide retains layer selections");
    Check(!PlayerLayerFilterEnabledFromSettings(0, 1) && PlayerLayerFilterEnabledFromSettings(0, 2),
        "master hide retains player filters");
    g_overlayBoxes.emplace_back();
    g_frameRects.emplace_back();
    RebuildFrame();
    Check(GetOverlayBoxCount() == 0 && g_frameRects.empty(), "hidden rebuild clears stale boxes");
    Check(nativeReads == 0, "hidden rebuild does not read native game state");

    settings.collisionDisplayEnabled = true;
    Check(IsAnyLayerEnabled() && CurrentLayerMaskFromSettings() == selectedMask,
        "master show restores selected layers");
    Check(!PlayerLayerFilterEnabledFromSettings(0, 1) && PlayerLayerFilterEnabledFromSettings(0, 2),
        "master show restores the same player filters");
    settings.collisionDisplayHitboxes = false;
    settings.collisionDisplayCollisionBoxes = false;
    Check(IsAnyLayerEnabled(), "projectile-only selection remains supported");
    settings.collisionDisplayProjectileOrigins = false;
    Check(!IsAnyLayerEnabled(), "no projectile sub-layers means no overlay");
    Check(GetStatus().projectileInteractionsEnabled, "status retains saved selections");
    std::printf("collision visibility: %d failures\n", failures);
    return failures ? 1 : 0;
}
