#include "../include/utils/bgm_control.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/utils/minhook_utils.h"
#include <atomic>
#include <cstring>

extern uintptr_t GetEFZBase();

namespace {
using PlayBGMFunc = void(__thiscall*)(uintptr_t, unsigned short);
using StopBGMFunc = int(__thiscall*)(uintptr_t);
constexpr uintptr_t kPlayBgmRva = 0x68B0;
std::atomic<unsigned int> g_lastBgmTrack{0xFFFFu};
PlayBGMFunc g_originalPlayBgm = nullptr;
PracticeHooks::CallbackTicket g_trackObserverTicket;

bool CallPlayBgm(PlayBGMFunc play, uintptr_t gameSystem, unsigned short track) {
    if (!play) return false;
    __try {
        play(gameSystem, track);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall ObserveBgmTrack(uintptr_t gameSystem, void*, unsigned short track) {
    auto execution = MinHookUtils::EnterExecution(g_trackObserverTicket);
    if (CallPlayBgm(g_originalPlayBgm, gameSystem, track) && execution.Admitted()) {
        SetLastBgmTrack(track);
    }
}
}

bool InstallBgmTrackObserver() {
    const uintptr_t base = GetEFZBase();
    if (!base) return false;
    const unsigned char expected[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34};
    unsigned char entry[sizeof(expected)]{};
    // E9 accepts an existing peer hook; the owned gate bypasses this observer online.
    if (!SafeReadMemory(base + kPlayBgmRva, entry, sizeof(entry))
        || (std::memcmp(entry, expected, sizeof(entry)) != 0 && entry[0] != 0xE9)) {
        LogOut("[BGM] Track observer rejected an unknown playback entry", true);
        return false;
    }
    return MinHookUtils::CreateAndEnableHook(
        reinterpret_cast<void*>(base + kPlayBgmRva), reinterpret_cast<void*>(&ObserveBgmTrack),
        reinterpret_cast<void**>(&g_originalPlayBgm), kBgmTrackObserverOwner, "playBackgroundMusic",
        nullptr, nullptr, &g_trackObserverTicket, true);
}

bool StopBGM(uintptr_t gameSystemPtr) {
    if (!gameSystemPtr) return false;
    uintptr_t efzBase = GetEFZBase();
    if (!efzBase) {
        LogOut("[BGM] Could not get EFZ base address!", true);
        return false;
    }
    StopBGMFunc stopBGM = (StopBGMFunc)(efzBase + 0x6A10); // VA 0x406A10
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
    const uintptr_t base = GetEFZBase();
    if (!base) return false;
    const auto play = g_originalPlayBgm ? g_originalPlayBgm
                                      : reinterpret_cast<PlayBGMFunc>(base + kPlayBgmRva);
    if (!CallPlayBgm(play, gameSystemPtr, trackNumber)) {
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

int GetBGMVolume(uintptr_t gameStatePtr) {
    int vol = 0;
    SafeReadMemory(gameStatePtr + 0xF2C, &vol, sizeof(int));
    return vol;
}

unsigned short GetLastBgmTrack() {
    return static_cast<unsigned short>(g_lastBgmTrack.load(std::memory_order_acquire));
}

void SetLastBgmTrack(unsigned short track) {
    g_lastBgmTrack.store(track, std::memory_order_release);
}
