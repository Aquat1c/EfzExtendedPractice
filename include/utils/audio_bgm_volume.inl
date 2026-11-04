enum class BgmRawState { Unobserved, Known, AwaitingNativeSample };

struct BgmOutputVolume {
    void* manager = nullptr;
    void* buffer = nullptr;
    unsigned short index = kInvalidBufferIndex;
    int rawLevel = 0;
    BgmRawState rawState = BgmRawState::Unobserved;
    bool gainMayBeApplied = false;
};

// Audio-boundary protected. Native track loads reset reused slots and addresses.
BgmOutputVolume g_bgmOutputVolume;

void ResetBgmOutputVolume() {
    g_bgmOutputVolume = {};
}

bool BindBgmOutput(void* soundManagerPtr, unsigned short bufferIndex) {
    void* buffer = nullptr;
    if (!ReadSoundBufferPointer(soundManagerPtr, bufferIndex, buffer)) return false;
    if (g_bgmOutputVolume.manager != soundManagerPtr
        || g_bgmOutputVolume.buffer != buffer || g_bgmOutputVolume.index != bufferIndex) {
        g_bgmOutputVolume = {soundManagerPtr, buffer, bufferIndex};
    }
    return true;
}

void ObserveBgmVolume(void* soundManagerPtr, unsigned short bufferIndex, int rawLevel,
                      const AudioSettingsView& view) {
    if (!BindBgmOutput(soundManagerPtr, bufferIndex)) return;
    if (!view.trainingOwnsBgmGain) {
        g_bgmOutputVolume.rawState = BgmRawState::AwaitingNativeSample;
        return;
    }
    g_bgmOutputVolume.rawLevel = rawLevel;
    g_bgmOutputVolume.rawState = BgmRawState::Known;
    g_bgmOutputVolume.gainMayBeApplied = true;
}

void ApplyBgmVolumeToBuffer(void* soundManagerPtr, unsigned short bufferIndex,
                            const AudioSettingsView& view) {
    if (!view.trainingOwnsBgmGain || !BindBgmOutput(soundManagerPtr, bufferIndex)) return;
    if (g_bgmOutputVolume.rawState != BgmRawState::Known) {
        if (g_bgmOutputVolume.rawState == BgmRawState::AwaitingNativeSample
            || !GetBufferVolume(soundManagerPtr, bufferIndex, g_bgmOutputVolume.rawLevel)) {
            if (view.bgmPercent == 0
                && SetBufferVolume(soundManagerPtr, bufferIndex, kMinDirectSoundVolume)) {
                g_bgmOutputVolume.rawState = BgmRawState::AwaitingNativeSample;
            }
            RequestAudioApply();
            return;
        }
        g_bgmOutputVolume.rawState = BgmRawState::Known;
    }
    const int volume = ApplyAudioLaneGain(view, true, g_bgmOutputVolume.rawLevel);
    if (SetBufferVolume(soundManagerPtr, bufferIndex, volume)) {
        g_bgmOutputVolume.gainMayBeApplied = true;
    } else {
        RequestAudioApply();
    }
}

void ApplyBgmVolumeToGameSystem(uintptr_t gameSystemPtr, const AudioSettingsView& view) {
    if (!view.trainingOwnsBgmGain || !gameSystemPtr) return;
    void* soundManagerPtr = nullptr;
    uint16_t bufferIndex = kInvalidBufferIndex;
    if (!ReadPointer(gameSystemPtr + kSoundManagerOffset, soundManagerPtr)
        || !SafeReadMemory(gameSystemPtr + kBgmBufferOffset, &bufferIndex, sizeof(bufferIndex))) return;
    ApplyBgmVolumeToBuffer(soundManagerPtr, bufferIndex, view);
}

void ApplyVolumeBeforePlayback(void* soundManagerPtr, unsigned short bufferIndex,
                               const AudioSettingsView& view) {
    if (!soundManagerPtr || bufferIndex >= kSoundManagerBufferCount) return;
    uintptr_t gameSystemPtr = 0;
    if (IsCurrentBgmBuffer(soundManagerPtr, bufferIndex, gameSystemPtr)) {
        ApplyBgmVolumeToBuffer(soundManagerPtr, bufferIndex, view);
    } else if (view.trainingOwnsSeGain) {
        SetBufferVolume(soundManagerPtr, bufferIndex,
                        ApplyAudioLaneGain(view, false, kDefaultSeDirectSoundVolume));
    }
}

void RetireBgmVolume(const AudioSettingsView& view) {
    uintptr_t gameSystemPtr = 0;
    void* buffer = nullptr;
    if (view.trainingOwnsBgmGain && g_bgmOutputVolume.gainMayBeApplied
        && g_bgmOutputVolume.rawState == BgmRawState::Known
        && IsCurrentBgmBuffer(g_bgmOutputVolume.manager, g_bgmOutputVolume.index, gameSystemPtr)
        && ReadSoundBufferPointer(g_bgmOutputVolume.manager, g_bgmOutputVolume.index, buffer)
        && buffer == g_bgmOutputVolume.buffer) {
        SetBufferVolume(g_bgmOutputVolume.manager, g_bgmOutputVolume.index,
                        g_bgmOutputVolume.rawLevel);
    }
    // Revival's native SetVolume can finish after its callback releases the boundary.
    g_bgmOutputVolume.rawState = BgmRawState::AwaitingNativeSample;
    g_bgmOutputVolume.gainMayBeApplied = false;
    RequestAudioApply();
}
