int AdjustVolumeForBuffer(void* soundManagerPtr, unsigned short bufferIndex, int volumeLevel, const AudioSettingsView& view) {
    if (!soundManagerPtr || bufferIndex >= kSoundManagerBufferCount ||
        !IsSoundBufferReadyForOps(reinterpret_cast<uintptr_t>(soundManagerPtr), bufferIndex, false, true)) return volumeLevel;
    uintptr_t gameSystemPtr = 0;
    return ApplyAudioLaneGain(view, IsCurrentBgmBuffer(soundManagerPtr, bufferIndex, gameSystemPtr), volumeLevel);
}
