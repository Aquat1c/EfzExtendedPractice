#pragma once

#include <string>
#include <cstdint>

namespace ExtendedConfigBridge {

struct Status {
    bool modLoaded = false;
    bool sharedConfigFound = false;
    bool audioShared = false;
    bool controlsShared = false;
    bool bgmVolumeAvailable = false;
    bool seVolumeAvailable = false;
    int bgmVolumePercent = 100;
    int seVolumePercent = 100;
    int version = 0;
    int audioRevision = 0;
    int controlsRevision = 0;
    std::string audioCompatibilityError;
    std::string moduleName;
    std::string modulePath;
    std::string sharedConfigPath;
};

// Control plane only: reads the fixed location admitted during setup.
bool Refresh(bool force = false);

bool ImportAudioSettingsIfAvailable(bool force = false);

// Publishes into the extension-owned shared config; never touches KEY.ini.
bool PublishAudioSettings(int bgmPercent, int sePercent);
bool PublishAudioLaneSetting(bool bgm, int percent);

bool InitializeAudioControl();
void SignalAudioControlStop();
void StopAudioControl();
bool RequestTrainingGainOwnership(uint32_t lanes, int bgmPercent, int sePercent);
Status GetStatus();
bool IsSharedAudioActiveCached();
bool IsSharedAudioActive();

}
