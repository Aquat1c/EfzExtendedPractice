#include "../include/utils/bgm_control.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/core/constants.h"
#include "../include/gui/overlay.h"
#include "../include/core/globals.h"  
#include "../include/utils/audio_control.h"
#include <thread>
#include <chrono>
#include <atomic>

constexpr uintptr_t BGM_SLOT_OFFSET   = 0xF26;
constexpr uintptr_t SOUND_MANAGER_PTR = 0x8;

typedef int   (__thiscall* StopBGMFunc)(uintptr_t gameSystemPtr);

extern uintptr_t GetEFZBase();

static std::atomic<unsigned int> g_lastBgmTrack{0xFFFFu};

bool StopBGM(uintptr_t gameSystemPtr) {
    if (!gameSystemPtr) return false;
    uintptr_t efzBase = GetEFZBase();
    if (!efzBase) {
        LogOut("[BGM] Could not get EFZ base address!", true);
        return false;
    }
    StopBGMFunc stopBGM = (StopBGMFunc)(efzBase + 0x6A10); // 0x406A10 RVA
    LogOut("[BGM] Calling game's stopBackgroundMusic...", true);
    stopBGM(gameSystemPtr);
    // Track 150 is EFZ's established "OFF" selection.
    SetLastBgmTrack(150);
    LogOut("[BGM] Called stopBackgroundMusic.", true);
    return true;
}

bool PlayBGM(uintptr_t gameSystemPtr, unsigned short trackNumber) {
    if (!gameSystemPtr) return false;
    LogOut("[BGM] Calling game's playBackgroundMusic with track " + std::to_string(trackNumber), true);
    if (!AudioControl::PlayBackgroundMusic(gameSystemPtr, trackNumber)) {
        LogOut("[BGM] playBackgroundMusic request failed for track " + std::to_string(trackNumber), true);
        return false;
    }
    SetLastBgmTrack(trackNumber);
    LogOut("[BGM] playBackgroundMusic request succeeded.", true);
    return true;
}

int GetBGMBufferIndex(uintptr_t gameStatePtr) {
    uint16_t slot = 0;
    SafeReadMemory(gameStatePtr + 0xF26, &slot, sizeof(uint16_t));
    return static_cast<int>(slot);
}

int GetBGMSlot(uintptr_t gameStatePtr) {
    return GetBGMBufferIndex(gameStatePtr);
}

int GetBGMVolume(uintptr_t gameStatePtr) {
    int vol = 0;
    SafeReadMemory(gameStatePtr + 0xF2C, &vol, sizeof(int));
    return vol;
}

// Stubs retained for ABI compatibility.
void SetBGMSuppressed(bool /*suppress*/) {}
bool IsBGMSuppressed() { return false; }

unsigned short GetLastBgmTrack() {
    return static_cast<unsigned short>(g_lastBgmTrack.load(std::memory_order_acquire));
}

void SetLastBgmTrack(unsigned short track) {
    g_lastBgmTrack.store(track, std::memory_order_release);
}

bool SetBGMVolumeViaGame(uintptr_t gameSystemPtr, int volumeLevel) {
    if (!gameSystemPtr) return false;
    uintptr_t efzBase = GetEFZBase();
    if (!efzBase) return false;

    void* soundManagerPtr = *(void**)(gameSystemPtr + 0x8);
    unsigned short bufferIndex = *(unsigned short*)(gameSystemPtr + 0xF26);
    if (!soundManagerPtr || bufferIndex == 150) return false;

    typedef int (__thiscall* SetSoundVolumeFunc)(void* soundManagerPtr, unsigned short bufferIndex, int volumeLevel);
    SetSoundVolumeFunc setSoundVolume = (SetSoundVolumeFunc)(efzBase + 0xE5D0); // 0x40E5D0 RVA

    int result = setSoundVolume(soundManagerPtr, bufferIndex, volumeLevel);
    LogOut("[BGM] Set volume via game function: buffer=" + std::to_string(bufferIndex) + ", vol=" + std::to_string(volumeLevel) + ", result=" + std::to_string(result), true);
    return result == 0;
}

// Stubs retained for binary compatibility.
void StartBGMSuppressionPoller() {}
void StopBGMSuppressionPoller() {}
