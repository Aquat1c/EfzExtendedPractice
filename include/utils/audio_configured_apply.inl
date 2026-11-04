void ImplApplyConfiguredVolumesNow(const AudioSettingsView& view) {
    if (RuntimeAudioControlSuppressed("ApplyConfiguredVolumesNow")) {
        return;
    }
    if (!RuntimeVolumeApplicationReady()) {
        TryEnableVolumeApplication(0, "ApplyConfiguredVolumesNow");
    }
    if (!RuntimeVolumeApplicationReady()) return;

    const uintptr_t gameSystemPtr = AudioGameSystem();
    if (gameSystemPtr) {
        SehApplyBgmVolumeToGameSystem(gameSystemPtr, view);
        SehApplyCommonSeVolumeToGameSystem(gameSystemPtr, view);
    }
    SehApplyCharacterSeVolumesNow(view);
}

void ApplyConfiguredVolumesNow() {
    // Lock order: gameplay handoff, then audio ownership.
    g_p2ControlMutex.lock();
    __try {
        if (g_onlineModeActive.load(std::memory_order_acquire) || IsNetplaySessionActive()) {
            RequestAudioApply();
            return;
        }
        EFZ_AUDIO_CALLBACK(ImplApplyConfiguredVolumesNow(ReadAudioSettings()));
    }
    __finally { g_p2ControlMutex.unlock(); }
}
