#pragma once

#include <cstdint>

namespace AudioControl {

// Revival hooks playSoundBuffer and setSoundVolume during delayed startup, so that pair waits
// for positive final-host resolution. Deferred is safe to poll if Revival can be injected later.
enum class HookInstallPhase {
    CommonOnly,
    HostResolved,
};

enum class HookInstallResult {
    Deferred,
    Ready,
    Suppressed,
    Failed,
};

HookInstallResult InstallHooks(uintptr_t efzBase, HookInstallPhase phase);
const char* HookInstallResultName(HookInstallResult result);
// Removes the Revival callback detours. Callers under DllMain's loader lock
// must pass false and retain the module reference until process exit.
void ShutdownHooks(bool releaseRevivalModuleReference);
bool PlayBackgroundMusic(uintptr_t gameSystemPtr, unsigned short trackNumber);
void ApplyConfiguredVolumesNow();
void SetVolumeApplicationReady(bool ready);
bool IsVolumeApplicationReady();
bool IsGameSoundSystemReady(uintptr_t gameSystemPtr = 0);
bool IsCommonSoundEffectReady(uintptr_t gameSystemPtr, unsigned short soundIndex);
bool EnableVolumeApplicationIfSoundReady(uintptr_t gameSystemPtr = 0, const char* reason = nullptr);

int GetConfiguredBgmVolumePercent();
int GetConfiguredSeVolumePercent();
int GetConfiguredBgmDirectSoundVolume();
int GetConfiguredSeDirectSoundVolume();

}
