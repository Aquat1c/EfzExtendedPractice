#pragma once
#include <windows.h>
#include <Xinput.h>
#include "config_policy.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace XInputShim {
    inline bool BindingDown(const XINPUT_STATE& state, int mask) {
        const XINPUT_GAMEPAD& pad = state.Gamepad;
        return ConfigPolicy::PadBindingDown(mask, pad.wButtons, pad.bLeftTrigger, pad.bRightTrigger,
                                            pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.sThumbRY);
    }

    constexpr int kControllerSlotCount = 4;
    constexpr std::size_t kControllerSlotNameCapacity = 96;

    struct Snapshot {
        unsigned connectedMask = 0;
        unsigned nativeMask = 0;
        unsigned genericMask = 0;
        DWORD generation = 0;
        XINPUT_STATE states[kControllerSlotCount] = {};
        XINPUT_CAPABILITIES capabilities[kControllerSlotCount] = {};
        bool capabilitiesValid[kControllerSlotCount] = {};
        bool genericSlots[kControllerSlotCount] = {};
        char slotNames[kControllerSlotCount][kControllerSlotNameCapacity] = {};

        bool IsConnected(int index) const {
            return index >= 0 && index < kControllerSlotCount &&
                   ((connectedMask >> index) & 1u) != 0;
        }
        bool CopyState(int index, XINPUT_STATE& out) const {
            if (!IsConnected(index)) {
                ZeroMemory(&out, sizeof(out));
                return false;
            }
            out = states[index];
            return true;
        }
    };

    extern std::atomic<bool> g_LogGenericPadInputDebug;

    bool Init();
    // Published by the Practice Battle owner; getters cannot enable polling.
    void SetPollingActive(bool active);
    bool IsPollingActive();
    bool IsLoaded();
    const char* LoadedDllName();

    DWORD GetState(DWORD dwUserIndex, XINPUT_STATE* pState);
    DWORD GetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCaps);
    void  Enable(BOOL enable);

    // The watcher performs all XInput/DirectInput hardware work away from the
    // render thread, then publishes under a short copy-only mutex.
    void RefreshSnapshotOncePerFrame();
    void CopySnapshot(Snapshot& out);
    bool CopyCachedState(int index, XINPUT_STATE& out);
    bool IsPadConnectedCached(int index);
    unsigned GetConnectedMaskCached();
    unsigned GetNativeConnectedMaskCached();
    unsigned GetGenericConnectedMaskCached();
    bool IsGenericFallbackSlot(int index);
    std::string GetSlotDisplayName(int index);

    bool GetPublishedControllerName(int index, char* buf, size_t bufLen);
}
