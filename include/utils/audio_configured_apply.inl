void ImplApplyConfiguredVolumesNow(const AudioSettingsView& view) {
    if (RuntimeAudioControlSuppressed("ApplyConfiguredVolumesNow")) {
        return;
    }
    if (!RuntimeVolumeApplicationReady()) {
        TryEnableVolumeApplication(0, "ApplyConfiguredVolumesNow");
    }
    if (!RuntimeVolumeApplicationReady()) {
        RequestAudioApply();
        return;
    }

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
        EFZ_AUDIO_CALLBACK(ImplApplyConfiguredVolumesNow(ReadAdmittedAudioSettings()));
    }
    __finally { g_p2ControlMutex.unlock(); }
}

void ImplSuspendForNetplay(const AudioSettingsView& view) {
    SetAudioGainAdmitted(false);
    RetireBgmVolume(view);
    MinHookUtils::SetNativePassthrough(true);
}

void ImplResumeFromNetplay() {
    SetAudioGainAdmitted(true);
    RequestAudioApply();
    MinHookUtils::SetNativePassthrough(false);
}

void ResumeFromNetplay() {
    g_p2ControlMutex.lock();
    __try {
        EFZ_AUDIO_CALLBACK(ImplResumeFromNetplay());
    }
    __finally { g_p2ControlMutex.unlock(); }
}

void SuspendForNetplay() {
    g_p2ControlMutex.lock();
    __try {
        EFZ_AUDIO_CALLBACK(ImplSuspendForNetplay(ReadAudioSettings()));
    }
    __finally { g_p2ControlMutex.unlock(); }
}
