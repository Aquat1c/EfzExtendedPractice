bool ReadSoundBufferPointer(void* soundManagerPtr, unsigned short bufferIndex, void*& outBufferPtr) {
    outBufferPtr = nullptr;
    if (!soundManagerPtr || bufferIndex >= kSoundManagerBufferCount) {
        return false;
    }

    uintptr_t bufferPtr = 0;
    const uintptr_t tableEntry = reinterpret_cast<uintptr_t>(soundManagerPtr)
        + kSoundManagerBufferTableOffset
        + sizeof(uintptr_t) * bufferIndex;
    if (!ReadChecked(tableEntry, bufferPtr) || !bufferPtr) {
        return false;
    }

    outBufferPtr = reinterpret_cast<void*>(bufferPtr);
    return true;
}

bool SetBufferVolume(void* soundManagerPtr, unsigned short bufferIndex, int directSoundVolume) {
    if (RuntimeAudioControlSuppressed("buffer volume write requested")) {
        return false;
    }
    if (!soundManagerPtr || bufferIndex >= kSoundManagerBufferCount) {
        return false;
    }

    void* soundBufferPtr = nullptr;
    if (!ReadSoundBufferPointer(soundManagerPtr, bufferIndex, soundBufferPtr)) {
        return false;
    }

    uintptr_t vtable = 0;
    uintptr_t setVolumeMethod = 0;
    if (!ReadChecked(reinterpret_cast<uintptr_t>(soundBufferPtr), vtable)
        || !vtable
        || !ReadChecked(vtable + kDsBufferSetVolumeVtableOffset, setVolumeMethod)
        || !IsExecutableAddress(setVolumeMethod)) {
        return false;
    }

    auto setVolume = reinterpret_cast<DirectSoundBufferSetVolumeFn>(setVolumeMethod);
    int result = -1;
    __try {
        result = setVolume(soundBufferPtr, directSoundVolume);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return result == 0;
}
