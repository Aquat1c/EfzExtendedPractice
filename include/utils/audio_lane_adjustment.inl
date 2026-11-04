int AdjustVolumeForBuffer(void* soundManagerPtr, unsigned short bufferIndex, int volumeLevel, const AudioSettingsView& view) {
    if (!soundManagerPtr || bufferIndex >= kSoundManagerBufferCount ||
        !IsSoundBufferReadyForOps(reinterpret_cast<uintptr_t>(soundManagerPtr), bufferIndex, false, true)) return volumeLevel;
    uintptr_t gameSystemPtr = 0;
    const bool bgm = IsCurrentBgmBuffer(soundManagerPtr, bufferIndex, gameSystemPtr);
    if (bgm) ObserveBgmVolume(soundManagerPtr, bufferIndex, volumeLevel, view);
    return ApplyAudioLaneGain(view, bgm, volumeLevel);
}
