#include "../include/utils/xinput_shim.h"
#include "../include/utils/controller_names.h"
#include "../include/core/logger.h"
#include "../include/core/globals.h"
#include "../include/utils/utilities.h"
#include "../include/input/generic_pad_axis_policy.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace XInputShim {
    std::atomic<bool> g_LogGenericPadInputDebug{false};
}

namespace {
    HMODULE g_xinput = nullptr;
    const char* g_name = nullptr;
    typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE*);
    typedef DWORD (WINAPI *PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    typedef void  (WINAPI *PFN_XInputEnable)(BOOL);
    PFN_XInputGetState pGetState = nullptr;
    PFN_XInputGetCapabilities pGetCaps = nullptr;
    PFN_XInputEnable pEnable = nullptr;
    std::array<XINPUT_CAPABILITIES, XInputShim::kControllerSlotCount> g_nativeCapabilities{};
    unsigned g_nativeCapabilitiesMask = 0;

    struct GenericPad {
        LPDIRECTINPUTDEVICE8 device = nullptr;
        std::string name;
        DIJOYSTATE2 rawState{};
        DIJOYSTATE2 neutralState{};
        XINPUT_STATE syntheticState{};
        DWORD packetCounter = 0;
        bool connected = false;
        DWORD povObjectCount = 0;
        bool neutralStateValid = false;
        GenericPadAxisPolicy::AxisPairCalibration leftStickCalibration{};
        GenericPadAxisPolicy::AxisPairCalibration rightStickCalibration{};
        GenericPadAxisPolicy::DigitalNeutralGate povDpadGate{};
        GenericPadAxisPolicy::DigitalNeutralGate buttonDpadGate{};
        WORD startupBlockedButtons = 0;
        int startupBlockWarmupFrames = 8;
        bool startupBlockedLeftTrigger = false;
        bool startupBlockedRightTrigger = false;
        int startupTriggerBlockWarmupFrames = 8;
        std::string lastInputDebugSummary;
        bool inputDebugWasEnabled = false;
        DWORD lastInputDebugLogTick = 0;
    };

    LPDIRECTINPUT8 g_directInput = nullptr;
    std::vector<GenericPad> g_genericPads;
    bool g_directInputInitAttempted = false;
    DWORD g_lastGenericEnumTick = 0;
    XInputShim::Snapshot g_cachedSnapshot{};
    std::string g_lastSlotSummary;
    std::string g_lastGenericInventorySummary;
    // Publication only: no XInput/DirectInput call or name lookup while held.
    std::mutex g_snapshotMutex;
    std::atomic<bool> g_watcherStarted{ false };
    std::atomic<bool> g_pollingActive{false};
    std::atomic<uint64_t> g_pollingGeneration{0};
    void EnsureWatcherStarted();

    // Game thread reads via try_lock; on contention it falls back to a stale copy.
    std::mutex g_namesMutex;
    char g_publishedNames[4][96] = {};
    bool g_publishedNamesValid[4] = { false, false, false, false };
    DWORD g_lastNamesPublishTick = 0;
    unsigned g_lastNamesPublishMask = 0xFFFFFFFFu;

    void PublishControllerNamesUnlocked(unsigned mask) {
        // Watcher thread, g_snapshotMutex NOT held; GetControllerNameForIndex may try_lock it.
        std::string names[4];
        for (int i = 0; i < 4; ++i) {
            if (((mask >> i) & 1u) == 0) {
                char buf[64];
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Pad %d (Disconnected)", i);
                names[i] = buf;
            } else {
                names[i] = ::GetControllerNameForIndex(i);
                if (names[i].empty()) {
                    char buf[64];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Controller %d", i);
                    names[i] = buf;
                }
            }
        }
        std::lock_guard<std::mutex> lock(g_namesMutex);
        for (int i = 0; i < 4; ++i) {
            _snprintf_s(g_publishedNames[i], sizeof(g_publishedNames[i]), _TRUNCATE,
                        "%s", names[i].c_str());
            g_publishedNamesValid[i] = true;
        }
    }

    HMODULE TryLoad(const char* dll) {
        HMODULE h = LoadLibraryA(dll);
        if (!h) return nullptr;
        auto gs = (PFN_XInputGetState)GetProcAddress(h, "XInputGetState");
        auto gc = (PFN_XInputGetCapabilities)GetProcAddress(h, "XInputGetCapabilities");
        auto en = (PFN_XInputEnable)GetProcAddress(h, "XInputEnable");
        if (!gs || !gc) {
            FreeLibrary(h);
            return nullptr;
        }
        pGetState = gs;
        pGetCaps = gc;
        pEnable = en;
        g_name = dll;
        return h;
    }

    HWND ResolveInputCooperativeWindow() {
        HWND hwnd = FindEFZWindow();
        if (!hwnd) hwnd = GetConsoleWindow();
        if (!hwnd) hwnd = GetDesktopWindow();
        return hwnd;
    }

    std::string NarrowDeviceName(const TCHAR* value) {
        if (!value || !*value) return "DirectInput Controller";
#if defined(UNICODE)
        int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 1) return "DirectInput Controller";
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, -1, &out[0], needed, nullptr, nullptr);
        if (!out.empty() && out.back() == '\0') out.pop_back();
        return out;
#else
        return std::string(value);
#endif
    }

    void ReleaseGenericPadsLocked() {
        for (GenericPad& pad : g_genericPads) {
            if (pad.device) {
                pad.device->Unacquire();
                pad.device->Release();
                pad.device = nullptr;
            }
        }
        g_genericPads.clear();
    }

    void ConfigureAxisRange(IDirectInputDevice8* device, DWORD offset) {
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(DIPROPRANGE);
        range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        range.diph.dwObj = offset;
        range.diph.dwHow = DIPH_BYOFFSET;
        range.lMin = -32768;
        range.lMax = 32767;
        device->SetProperty(DIPROP_RANGE, &range.diph);
    }

    // XInput-capable HID interfaces carry "IG_" in their device path.
    bool IsXInputDevice(IDirectInputDevice8* device) {
        DIPROPGUIDANDPATH prop{};
        prop.diph.dwSize = sizeof(DIPROPGUIDANDPATH);
        prop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        prop.diph.dwHow = DIPH_DEVICE;
        if (FAILED(device->GetProperty(DIPROP_GUIDANDPATH, &prop.diph))) return false;
        return wcsstr(prop.wszPath, L"IG_") != nullptr || wcsstr(prop.wszPath, L"ig_") != nullptr;
    }

    BOOL CALLBACK EnumGenericPadsCallback(const DIDEVICEINSTANCE* instance, VOID* context) {
        if (!XInputShim::IsPollingActive()) return DIENUM_STOP;
        auto* pads = static_cast<std::vector<GenericPad>*>(context);
        if (!pads || !g_directInput) return DIENUM_STOP;
        if (pads->size() >= 4) return DIENUM_STOP;

        LPDIRECTINPUTDEVICE8 device = nullptr;
        HRESULT hr = g_directInput->CreateDevice(instance->guidInstance, &device, nullptr);
        if (FAILED(hr) || !device) {
            return DIENUM_CONTINUE;
        }

        // Already polled through its native XInput slot; without XInput keep it as the only path.
        if (g_xinput && IsXInputDevice(device)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        hr = device->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hr)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        hr = device->SetCooperativeLevel(ResolveInputCooperativeWindow(), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        if (FAILED(hr)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        ConfigureAxisRange(device, DIJOFS_X);
        ConfigureAxisRange(device, DIJOFS_Y);
        ConfigureAxisRange(device, DIJOFS_Z);
        ConfigureAxisRange(device, DIJOFS_RX);
        ConfigureAxisRange(device, DIJOFS_RY);
        ConfigureAxisRange(device, DIJOFS_RZ);
        ConfigureAxisRange(device, DIJOFS_SLIDER(0));
        ConfigureAxisRange(device, DIJOFS_SLIDER(1));

        device->Acquire();

        GenericPad pad;
        pad.device = device;
        pad.name = NarrowDeviceName(instance->tszProductName);
        DIDEVCAPS caps{};
        caps.dwSize = sizeof(caps);
        if (SUCCEEDED(device->GetCapabilities(&caps))) {
            pad.povObjectCount = caps.dwPOVs;
        }
        pads->push_back(pad);
        return DIENUM_CONTINUE;
    }

    bool EnsureDirectInputInitializedLocked() {
        if (g_directInput) return true;
        if (g_directInputInitAttempted) return false;
        g_directInputInitAttempted = true;

        HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
            IID_IDirectInput8, reinterpret_cast<void**>(&g_directInput), nullptr);
        if (FAILED(hr) || !g_directInput) {
            LogOut("[GAMEPAD] DirectInput fallback initialization failed", true);
            return false;
        }

        LogOut("[GAMEPAD] DirectInput fallback initialized", true);
        return true;
    }

    void EnumerateGenericPadsLocked(bool forceLog) {
        if (!XInputShim::IsPollingActive()) return;
        if (!EnsureDirectInputInitializedLocked()) {
            return;
        }

        ReleaseGenericPadsLocked();
        g_lastGenericEnumTick = GetTickCount();

        std::vector<GenericPad> pads;
        g_directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumGenericPadsCallback, &pads, DIEDFL_ATTACHEDONLY);
        g_genericPads.swap(pads);

        if (g_genericPads.empty()) {
            if (g_lastGenericInventorySummary != "none") {
                g_lastGenericInventorySummary = "none";
            }
            if (forceLog) {
                LogOut("[GAMEPAD] No DirectInput fallback controllers detected", true);
            }
            return;
        }

        std::ostringstream oss;
        oss << g_genericPads.size();
        for (size_t i = 0; i < g_genericPads.size(); ++i) {
            oss << "|" << g_genericPads[i].name;
        }
        std::string inventory = oss.str();
        if (!forceLog && inventory == g_lastGenericInventorySummary) {
            return;
        }
        g_lastGenericInventorySummary = inventory;

        LogOut("[GAMEPAD] DirectInput fallback controllers detected: " + std::to_string(g_genericPads.size()), true);
        for (size_t i = 0; i < g_genericPads.size(); ++i) {
            LogOut("[GAMEPAD] DirectInput pad " + std::to_string(i) + ": " + g_genericPads[i].name, true);
        }
    }

    bool PollGenericPadLocked(GenericPad& pad) {
        if (!XInputShim::IsPollingActive()) return false;
        if (!pad.device) {
            pad.connected = false;
            return false;
        }

        HRESULT hr = pad.device->Poll();
        if (FAILED(hr)) {
            hr = pad.device->Acquire();
            if (FAILED(hr)) {
                pad.connected = false;
                return false;
            }
            hr = pad.device->Poll();
            if (FAILED(hr)) {
                pad.connected = false;
                return false;
            }
        }

        DIJOYSTATE2 state{};
        hr = pad.device->GetDeviceState(sizeof(DIJOYSTATE2), &state);
        if (FAILED(hr)) {
            pad.connected = false;
            return false;
        }

        pad.rawState = state;
        pad.connected = true;

        const bool hasButtons = [&]() {
            for (int i = 0; i < 128; ++i) {
                if (state.rgbButtons[i] & 0x80) return true;
            }
            return false;
        }();
        bool hasPov = false;
        for (int i = 0; i < 4; ++i) {
            if (GenericPadAxisPolicy::IsDirectionalPov(state.rgdwPOV[i])) {
                hasPov = true;
                break;
            }
        }
        // Calibration must not require all inputs released: one bogus held bit would park raw axes forever.
        if (!pad.neutralStateValid) {
            pad.neutralState = state;
            pad.neutralStateValid = true;
        }
        if (GenericPadAxisPolicy::ObserveNeutral(
                pad.leftStickCalibration, state.lX, state.lY)) {
            pad.neutralState.lX = state.lX;
            pad.neutralState.lY = state.lY;
        }
        if (GenericPadAxisPolicy::ObserveNeutral(
                pad.rightStickCalibration, state.lRx, state.lRy)) {
            pad.neutralState.lRx = state.lRx;
            pad.neutralState.lRy = state.lRy;
        }
        if (!hasButtons && !hasPov) {
            GenericPadAxisPolicy::RelaxNeutral(
                pad.leftStickCalibration, state.lX, state.lY);
            GenericPadAxisPolicy::RelaxNeutral(
                pad.rightStickCalibration, state.lRx, state.lRy);
            if (pad.leftStickCalibration.valid) {
                pad.neutralState.lX = pad.leftStickCalibration.neutralX;
                pad.neutralState.lY = pad.leftStickCalibration.neutralY;
            }
            if (pad.rightStickCalibration.valid) {
                pad.neutralState.lRx = pad.rightStickCalibration.neutralX;
                pad.neutralState.lRy = pad.rightStickCalibration.neutralY;
            }
        }
        return true;
    }

    static bool ButtonDown(const DIJOYSTATE2& state, int index) {
        return index >= 0 && index < 128 && (state.rgbButtons[index] & 0x80) != 0;
    }

    static std::uint8_t RawPovDirections(const DIJOYSTATE2& state) {
        std::uint8_t directions = 0;
        for (int i = 0; i < 4; ++i) {
            directions |= GenericPadAxisPolicy::PovDirections(state.rgdwPOV[i]);
        }
        return directions;
    }

    static std::uint8_t RawButtonDpadDirections(const DIJOYSTATE2& state) {
        std::uint8_t directions = 0;
        // Some DirectInput fight sticks expose dpad as buttons 12..15 instead of POV.
        if (ButtonDown(state, 12)) directions |= GenericPadAxisPolicy::PovUp;
        if (ButtonDown(state, 13)) directions |= GenericPadAxisPolicy::PovDown;
        if (ButtonDown(state, 14)) directions |= GenericPadAxisPolicy::PovLeft;
        if (ButtonDown(state, 15)) directions |= GenericPadAxisPolicy::PovRight;
        return directions;
    }

    static void MapDirectionsToDpad(std::uint8_t directions, WORD& buttons) {
        if (directions & GenericPadAxisPolicy::PovUp) buttons |= XINPUT_GAMEPAD_DPAD_UP;
        if (directions & GenericPadAxisPolicy::PovRight) buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        if (directions & GenericPadAxisPolicy::PovDown) buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (directions & GenericPadAxisPolicy::PovLeft) buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
    }

    static bool AxisPairActive(LONG x, LONG y, LONG threshold = 6000) {
        return x >= threshold || x <= -threshold || y >= threshold || y <= -threshold;
    }

    static LONG AxisDelta(LONG value, LONG neutral) {
        return value - neutral;
    }

    static LONG AxisAbs(LONG value) {
        return (value < 0) ? -value : value;
    }

    static void MapAxisPairToDpad(LONG x, LONG y, WORD& buttons, LONG threshold = 6000) {
        if (x <= -threshold) buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (x >= threshold) buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        if (y >= threshold) buttons |= XINPUT_GAMEPAD_DPAD_UP;
        if (y <= -threshold) buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    }

    static void ResolvePreferredLeftStickAxes(const GenericPad& pad, LONG& outX, LONG& outY) {
        outX = 0;
        outY = 0;
        if (!pad.leftStickCalibration.valid) return;
        outX = AxisDelta(pad.rawState.lX, pad.leftStickCalibration.neutralX);
        outY = -AxisDelta(pad.rawState.lY, pad.leftStickCalibration.neutralY);
    }

    static void MapAxisDpadFallback(const GenericPad& pad, WORD& buttons) {
        // Unsafe before a neutral sample: some leverless pads park unused axes at full extremes.
        if (!pad.leftStickCalibration.valid) {
            return;
        }
        const LONG x = AxisDelta(
            pad.rawState.lX, pad.leftStickCalibration.neutralX);
        const LONG y = -AxisDelta(
            pad.rawState.lY, pad.leftStickCalibration.neutralY);
        MapAxisPairToDpad(x, y, buttons);
    }

    static WORD ApplyStartupHeldButtonBlock(GenericPad& pad, WORD buttons) {
        if (pad.startupBlockWarmupFrames > 0) {
            const WORD newlyBlocked = static_cast<WORD>(buttons & ~pad.startupBlockedButtons);
            pad.startupBlockedButtons |= buttons;
            --pad.startupBlockWarmupFrames;
            if (newlyBlocked && detailedLogging.load()) {
                std::ostringstream oss;
                oss << "[GAMEPAD] DirectInput startup-held buttons masked for "
                    << pad.name << " buttons=0x" << std::hex << std::uppercase
                    << static_cast<unsigned>(newlyBlocked)
                    << std::dec << " until release";
                LogOut(oss.str(), true);
            }
        }

        if (pad.startupBlockedButtons == 0) {
            return buttons;
        }

        const WORD blockedStillHeld = static_cast<WORD>(buttons & pad.startupBlockedButtons);
        const WORD filtered = static_cast<WORD>(buttons & ~blockedStillHeld);
        pad.startupBlockedButtons = blockedStillHeld;
        return filtered;
    }

    static void ApplyStartupHeldTriggerBlock(GenericPad& pad, BYTE& leftTrigger, BYTE& rightTrigger) {
        constexpr BYTE kHeldThreshold = 30;
        const bool leftHeld = leftTrigger > kHeldThreshold;
        const bool rightHeld = rightTrigger > kHeldThreshold;

        if (pad.startupTriggerBlockWarmupFrames > 0) {
            const bool newlyBlockedLeft = leftHeld && !pad.startupBlockedLeftTrigger;
            const bool newlyBlockedRight = rightHeld && !pad.startupBlockedRightTrigger;
            pad.startupBlockedLeftTrigger = pad.startupBlockedLeftTrigger || leftHeld;
            pad.startupBlockedRightTrigger = pad.startupBlockedRightTrigger || rightHeld;
            --pad.startupTriggerBlockWarmupFrames;
            if ((newlyBlockedLeft || newlyBlockedRight) && detailedLogging.load()) {
                std::ostringstream oss;
                oss << "[GAMEPAD] DirectInput startup-held triggers masked for "
                    << pad.name
                    << " LT=" << static_cast<int>(leftTrigger)
                    << " RT=" << static_cast<int>(rightTrigger)
                    << " until release";
                LogOut(oss.str(), true);
            }
        }

        if (pad.startupBlockedLeftTrigger) {
            if (leftHeld) {
                leftTrigger = 0;
            } else {
                pad.startupBlockedLeftTrigger = false;
            }
        }
        if (pad.startupBlockedRightTrigger) {
            if (rightHeld) {
                rightTrigger = 0;
            } else {
                pad.startupBlockedRightTrigger = false;
            }
        }
    }

    static SHORT ClampAxisToShort(LONG value) {
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        return static_cast<SHORT>(value);
    }

    static BYTE TriggerFromAxisPositive(LONG value) {
        if (value <= 0) return 0;
        if (value >= 32767) return 255;
        return static_cast<BYTE>((value * 255) / 32767);
    }

    static bool RawStateHasInterestingInput(const GenericPad& pad) {
        const DIJOYSTATE2& state = pad.rawState;
        for (int i = 0; i < 128; ++i) {
            if (ButtonDown(state, i)) return true;
        }
        for (int i = 0; i < 4; ++i) {
            if (GenericPadAxisPolicy::IsDirectionalPov(state.rgdwPOV[i])) return true;
        }
        const LONG kAxisNoise = 6000;
        if (pad.leftStickCalibration.valid ||
            pad.rightStickCalibration.valid || pad.neutralStateValid) {
            return AxisPairActive(AxisDelta(state.lX, pad.neutralState.lX), AxisDelta(state.lY, pad.neutralState.lY), kAxisNoise)
                || AxisPairActive(AxisDelta(state.lRx, pad.neutralState.lRx), AxisDelta(state.lRy, pad.neutralState.lRy), kAxisNoise)
                || AxisAbs(AxisDelta(state.lZ, pad.neutralState.lZ)) >= kAxisNoise
                || AxisAbs(AxisDelta(state.lRz, pad.neutralState.lRz)) >= kAxisNoise
                || AxisAbs(AxisDelta(state.rglSlider[0], pad.neutralState.rglSlider[0])) >= kAxisNoise
                || AxisAbs(AxisDelta(state.rglSlider[1], pad.neutralState.rglSlider[1])) >= kAxisNoise;
        }
        return AxisPairActive(state.lX, state.lY, kAxisNoise)
            || AxisPairActive(state.lRx, state.lRy, kAxisNoise)
            || AxisAbs(state.lZ) >= kAxisNoise
            || AxisAbs(state.lRz) >= kAxisNoise
            || AxisAbs(state.rglSlider[0]) >= kAxisNoise
            || AxisAbs(state.rglSlider[1]) >= kAxisNoise;
    }

    static void MaybeLogGenericPadInput(GenericPad& pad, const XINPUT_STATE& state) {
        const bool enabled =
            XInputShim::g_LogGenericPadInputDebug.load(std::memory_order_relaxed);
        if (!enabled) {
            pad.inputDebugWasEnabled = false;
            return;
        }
        const bool justEnabled = !pad.inputDebugWasEnabled;
        if (justEnabled) {
            pad.inputDebugWasEnabled = true;
            pad.lastInputDebugSummary.clear();
        }

        std::ostringstream rawBtns;
        bool first = true;
        for (int i = 0; i < 128; ++i) {
            if (!ButtonDown(pad.rawState, i)) continue;
            if (!first) rawBtns << ",";
            rawBtns << i;
            first = false;
        }
        if (first) rawBtns << "-";

        std::ostringstream povs;
        for (int i = 0; i < 4; ++i) {
            if (i != 0) povs << ",";
            if (!GenericPadAxisPolicy::IsDirectionalPov(pad.rawState.rgdwPOV[i])) povs << "-";
            else povs << pad.rawState.rgdwPOV[i];
        }

        std::ostringstream oss;
        oss << "[GAMEPAD][GENERIC] " << pad.name
            << " interesting=" << (RawStateHasInterestingInput(pad) ? 1 : 0)
            << " povObjects=" << pad.povObjectCount
            << " rawButtons=[" << rawBtns.str() << "]"
            << " pov=[" << povs.str() << "]"
            << " axes=("
            << pad.rawState.lX << "," << pad.rawState.lY << "," << pad.rawState.lZ << ","
            << pad.rawState.lRx << "," << pad.rawState.lRy << "," << pad.rawState.lRz << ";"
            << pad.rawState.rglSlider[0] << "," << pad.rawState.rglSlider[1] << ")"
            << " deltaAxes=("
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.lX, pad.neutralState.lX) : pad.rawState.lX) << ","
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.lY, pad.neutralState.lY) : pad.rawState.lY) << ","
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.lZ, pad.neutralState.lZ) : pad.rawState.lZ) << ","
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.lRx, pad.neutralState.lRx) : pad.rawState.lRx) << ","
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.lRy, pad.neutralState.lRy) : pad.rawState.lRy) << ","
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.lRz, pad.neutralState.lRz) : pad.rawState.lRz) << ";"
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.rglSlider[0], pad.neutralState.rglSlider[0]) : pad.rawState.rglSlider[0]) << ","
            << (pad.neutralStateValid ? AxisDelta(pad.rawState.rglSlider[1], pad.neutralState.rglSlider[1]) : pad.rawState.rglSlider[1]) << ")"
            << " calibration{L="
            << (pad.leftStickCalibration.valid ? "ready" : "waiting")
            << "/" << static_cast<unsigned>(pad.leftStickCalibration.stableSamples)
            << "@" << pad.leftStickCalibration.neutralX << ","
            << pad.leftStickCalibration.neutralY
            << " R="
            << (pad.rightStickCalibration.valid ? "ready" : "waiting")
            << "/" << static_cast<unsigned>(pad.rightStickCalibration.stableSamples)
            << "@" << pad.rightStickCalibration.neutralX << ","
            << pad.rightStickCalibration.neutralY
            << " POV=" << (pad.povDpadGate.armed ? "ready" : "waiting")
            << "/" << static_cast<unsigned>(pad.povDpadGate.neutralSamples)
            << " BTN=" << (pad.buttonDpadGate.armed ? "ready" : "waiting")
            << "/" << static_cast<unsigned>(pad.buttonDpadGate.neutralSamples)
            << "}"
            << " startupBlock=0x" << std::hex << std::uppercase << pad.startupBlockedButtons
            << std::dec
            << " startupTrigBlock=" << (pad.startupBlockedLeftTrigger ? "L" : "-")
            << (pad.startupBlockedRightTrigger ? "R" : "-")
            << " synthetic{buttons=0x" << std::hex << std::uppercase << state.Gamepad.wButtons
            << std::dec
            << " LT=" << static_cast<int>(state.Gamepad.bLeftTrigger)
            << " RT=" << static_cast<int>(state.Gamepad.bRightTrigger)
            << " LX=" << state.Gamepad.sThumbLX
            << " LY=" << state.Gamepad.sThumbLY
            << " RX=" << state.Gamepad.sThumbRX
            << " RY=" << state.Gamepad.sThumbRY
            << "}";
        const std::string summary = oss.str();
        if (summary != pad.lastInputDebugSummary) {
            const DWORD now = GetTickCount();
            const bool syntheticChanged =
                memcmp(&state.Gamepad, &pad.syntheticState.Gamepad,
                       sizeof(XINPUT_GAMEPAD)) != 0;
            if (justEnabled || syntheticChanged ||
                pad.lastInputDebugLogTick == 0 ||
                (now - pad.lastInputDebugLogTick) >= 250) {
                pad.lastInputDebugSummary = summary;
                pad.lastInputDebugLogTick = now;
                LogOut(summary, true);
            }
        }
    }

    XINPUT_STATE BuildSyntheticState(GenericPad& pad) {
        XINPUT_STATE state{};
        WORD buttons = 0;

        // Conventional PlayStation/DirectInput mapping used by DualShock/DualSense style devices.
        if (ButtonDown(pad.rawState, 1)) buttons |= XINPUT_GAMEPAD_A; // Cross
        if (ButtonDown(pad.rawState, 2)) buttons |= XINPUT_GAMEPAD_B; // Circle
        if (ButtonDown(pad.rawState, 0)) buttons |= XINPUT_GAMEPAD_X; // Square
        if (ButtonDown(pad.rawState, 3)) buttons |= XINPUT_GAMEPAD_Y; // Triangle
        if (ButtonDown(pad.rawState, 4)) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
        if (ButtonDown(pad.rawState, 5)) buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        const auto anyButtonIn = [&](const int* indices, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                if (ButtonDown(pad.rawState, indices[i])) return true;
            }
            return false;
        };
        static const int kBackCandidates[] = { 8, 16, 24, 32, 40, 48, 56 };
        static const int kStartCandidates[] = { 9, 17, 25, 33, 41, 49, 57 };
        static const int kLeftThumbCandidates[] = { 10, 18, 26, 34, 42, 50, 58 };
        static const int kRightThumbCandidates[] = { 11, 19, 27, 35, 43, 51, 59 };
        if (anyButtonIn(kBackCandidates, sizeof(kBackCandidates) / sizeof(kBackCandidates[0]))) {
            buttons |= XINPUT_GAMEPAD_BACK;
        }
        if (anyButtonIn(kStartCandidates, sizeof(kStartCandidates) / sizeof(kStartCandidates[0]))) {
            buttons |= XINPUT_GAMEPAD_START;
        }
        if (anyButtonIn(kLeftThumbCandidates, sizeof(kLeftThumbCandidates) / sizeof(kLeftThumbCandidates[0]))) {
            buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
        }
        if (anyButtonIn(kRightThumbCandidates, sizeof(kRightThumbCandidates) / sizeof(kRightThumbCandidates[0]))) {
            buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
        }
        const std::uint8_t povDirections = RawPovDirections(pad.rawState);
        if (GenericPadAxisPolicy::ObserveDigitalNeutral(
                pad.povDpadGate, povDirections)) {
            MapDirectionsToDpad(povDirections, buttons);
        }
        if ((buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) == 0 &&
            GenericPadAxisPolicy::ShouldUseButtonDpadFallback(
                pad.povObjectCount)) {
            const std::uint8_t buttonDirections =
                RawButtonDpadDirections(pad.rawState);
            if (GenericPadAxisPolicy::ObserveDigitalNeutral(
                    pad.buttonDpadGate, buttonDirections)) {
                MapDirectionsToDpad(buttonDirections, buttons);
            }
        }
        if ((buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) == 0) {
            MapAxisDpadFallback(pad, buttons);
        }
        // DPad skips the startup-held filter; each direction source has its own neutral gate.
        constexpr WORD kDpadMask = XINPUT_GAMEPAD_DPAD_UP |
            XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
            XINPUT_GAMEPAD_DPAD_RIGHT;
        const WORD dpadButtons = static_cast<WORD>(buttons & kDpadMask);
        buttons = static_cast<WORD>(
            ApplyStartupHeldButtonBlock(
                pad, static_cast<WORD>(buttons & ~kDpadMask)) |
            dpadButtons);

        state.Gamepad.wButtons = buttons;
        BYTE leftTrigger = ButtonDown(pad.rawState, 6) ? 255 : 0;
        BYTE rightTrigger = ButtonDown(pad.rawState, 7) ? 255 : 0;
        if (leftTrigger == 0 && rightTrigger == 0 && pad.neutralStateValid) {
            // Some DirectInput pads expose analog triggers on Z/Rz instead of digital buttons.
            const LONG zDelta = AxisDelta(pad.rawState.lZ, pad.neutralState.lZ);
            const LONG rzDelta = AxisDelta(pad.rawState.lRz, pad.neutralState.lRz);
            leftTrigger = TriggerFromAxisPositive(-zDelta);
            rightTrigger = TriggerFromAxisPositive(zDelta);
            if (rightTrigger == 0) {
                rightTrigger = TriggerFromAxisPositive(rzDelta);
            }
        }
        ApplyStartupHeldTriggerBlock(pad, leftTrigger, rightTrigger);
        state.Gamepad.bLeftTrigger = leftTrigger;
        state.Gamepad.bRightTrigger = rightTrigger;
        LONG leftStickX = 0;
        LONG leftStickY = 0;
        ResolvePreferredLeftStickAxes(pad, leftStickX, leftStickY);
        state.Gamepad.sThumbLX = ClampAxisToShort(leftStickX);
        state.Gamepad.sThumbLY = ClampAxisToShort(leftStickY);
        if (pad.rightStickCalibration.valid) {
            state.Gamepad.sThumbRX = ClampAxisToShort(
                AxisDelta(pad.rawState.lRx, pad.rightStickCalibration.neutralX));
            state.Gamepad.sThumbRY = ClampAxisToShort(
                -AxisDelta(pad.rawState.lRy, pad.rightStickCalibration.neutralY));
        }

        if (memcmp(&state.Gamepad, &pad.syntheticState.Gamepad, sizeof(XINPUT_GAMEPAD)) != 0) {
            ++pad.packetCounter;
        }
        state.dwPacketNumber = pad.packetCounter;
        MaybeLogGenericPadInput(pad, state);
        pad.syntheticState = state;
        return state;
    }

    DWORD GetNativeState(DWORD idx, XINPUT_STATE* st) {
        if (!XInputShim::IsPollingActive()) return ERROR_DEVICE_NOT_CONNECTED;
        if (!g_xinput && !XInputShim::Init()) return ERROR_DEVICE_NOT_CONNECTED;
        if (!pGetState) return ERROR_DEVICE_NOT_CONNECTED;
        return pGetState(idx, st);
    }

    DWORD GetNativeCapabilities(DWORD idx, DWORD flags,
                                XINPUT_CAPABILITIES* caps) {
        if (!XInputShim::IsPollingActive()) return ERROR_DEVICE_NOT_CONNECTED;
        if (!g_xinput && !XInputShim::Init()) return ERROR_DEVICE_NOT_CONNECTED;
        if (!pGetCaps) return ERROR_DEVICE_NOT_CONNECTED;
        return pGetCaps(idx, flags, caps);
    }

    void LogSlotSummary(const XInputShim::Snapshot& snapshot);

    // Native polls can block seconds per disconnected slot; never hold g_snapshotMutex during them.
    static void PollNativeSnapshotUnlocked(std::array<XINPUT_STATE, 4>& outStates,
                                           unsigned& outMask) {
        outMask = 0;
        for (DWORD i = 0; i < 4; ++i) {
            if (GetNativeState(i, &outStates[i]) == ERROR_SUCCESS) {
                outMask |= (1u << i);
            } else {
                ZeroMemory(&outStates[i], sizeof(XINPUT_STATE));
            }
        }
    }

    static void RefreshNewNativeCapabilitiesUnlocked(unsigned nativeMask) {
        const unsigned newlyConnected = nativeMask & ~g_nativeCapabilitiesMask;
        const unsigned disconnected = g_nativeCapabilitiesMask & ~nativeMask;
        for (int i = 0; i < XInputShim::kControllerSlotCount; ++i) {
            const unsigned bit = 1u << i;
            if ((disconnected & bit) != 0) {
                g_nativeCapabilities[i] = {};
            }
            if ((newlyConnected & bit) == 0) continue;
            XINPUT_CAPABILITIES caps{};
            if (GetNativeCapabilities(i, XINPUT_FLAG_GAMEPAD, &caps) ==
                ERROR_SUCCESS) {
                g_nativeCapabilities[i] = caps;
            }
        }
        g_nativeCapabilitiesMask = nativeMask;
    }

    // Watcher thread only; publication never surrounds DirectInput work.
    void BuildSnapshot(const std::array<XINPUT_STATE, 4>& nativeStates,
                       unsigned nativeMask,
                       XInputShim::Snapshot& snapshot) {
        DWORD now = GetTickCount();

        const bool firstGenericEnum = (g_lastGenericEnumTick == 0);
        const bool noKnownGenericPads = g_genericPads.empty();
        if (firstGenericEnum || (noKnownGenericPads && (now - g_lastGenericEnumTick >= 5000))) {
            EnumerateGenericPadsLocked(firstGenericEnum);
        }

        auto collectGenericStates = [&]() {
            std::vector<std::pair<XINPUT_STATE, std::string>> out;
            out.reserve(g_genericPads.size());
            for (GenericPad& pad : g_genericPads) {
                if (!PollGenericPadLocked(pad)) {
                    continue;
                }
                out.emplace_back(BuildSyntheticState(pad), pad.name);
            }
            return out;
        };

        std::vector<std::pair<XINPUT_STATE, std::string>> genericStates = collectGenericStates();

        if (!g_genericPads.empty() && genericStates.empty() && (now - g_lastGenericEnumTick >= 1000)) {
            EnumerateGenericPadsLocked(false);
            genericStates = collectGenericStates();
        }

        snapshot = {};
        snapshot.nativeMask = nativeMask;

        for (int i = 0; i < 4; ++i) {
            if (((nativeMask >> i) & 1u) == 0) continue;
            snapshot.states[i] = nativeStates[i];
            snapshot.capabilities[i] = g_nativeCapabilities[i];
            snapshot.capabilitiesValid[i] =
                g_nativeCapabilities[i].Type != 0 ||
                g_nativeCapabilities[i].SubType != 0;
            snapshot.connectedMask |= (1u << i);
        }

        int nextGenericSlot = 0;
        for (const auto& entry : genericStates) {
            while (nextGenericSlot < 4 && ((snapshot.connectedMask >> nextGenericSlot) & 1u) != 0) {
                ++nextGenericSlot;
            }
            if (nextGenericSlot >= 4) break;

            snapshot.states[nextGenericSlot] = entry.first;
            snapshot.capabilities[nextGenericSlot].Type = XINPUT_DEVTYPE_GAMEPAD;
            snapshot.capabilities[nextGenericSlot].SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
            snapshot.capabilities[nextGenericSlot].Gamepad.wButtons = 0xFFFF;
            snapshot.capabilities[nextGenericSlot].Gamepad.bLeftTrigger = 0xFF;
            snapshot.capabilities[nextGenericSlot].Gamepad.bRightTrigger = 0xFF;
            snapshot.capabilities[nextGenericSlot].Gamepad.sThumbLX = 0x7FFF;
            snapshot.capabilities[nextGenericSlot].Gamepad.sThumbLY = 0x7FFF;
            snapshot.capabilities[nextGenericSlot].Gamepad.sThumbRX = 0x7FFF;
            snapshot.capabilities[nextGenericSlot].Gamepad.sThumbRY = 0x7FFF;
            snapshot.capabilitiesValid[nextGenericSlot] = true;
            snapshot.genericSlots[nextGenericSlot] = true;
            _snprintf_s(snapshot.slotNames[nextGenericSlot],
                        XInputShim::kControllerSlotNameCapacity, _TRUNCATE,
                        "%s", entry.second.c_str());
            snapshot.connectedMask |= (1u << nextGenericSlot);
            snapshot.genericMask |= (1u << nextGenericSlot);
            ++nextGenericSlot;
        }
    }

    void PublishSnapshot(XInputShim::Snapshot snapshot, uint64_t generation) {
        bool inventoryChanged = false;
        {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            if (!XInputShim::IsPollingActive() || generation != g_pollingGeneration.load()) return;
            inventoryChanged = snapshot.connectedMask != g_cachedSnapshot.connectedMask ||
                snapshot.nativeMask != g_cachedSnapshot.nativeMask ||
                snapshot.genericMask != g_cachedSnapshot.genericMask ||
                memcmp(snapshot.slotNames, g_cachedSnapshot.slotNames, sizeof(snapshot.slotNames)) != 0;
            snapshot.generation = g_cachedSnapshot.generation + 1;
            g_cachedSnapshot = snapshot;
        }
        if (inventoryChanged) LogSlotSummary(snapshot);
    }

    void ControllerWatcherThread() {
        LogOut("[GAMEPAD] Background controller watcher started", true);
        const uint64_t generation = g_pollingGeneration.load(std::memory_order_acquire);
        while (XInputShim::IsPollingActive() && generation == g_pollingGeneration.load()) {

            DWORD sleepMs = 16;
            std::array<XINPUT_STATE, 4> nativeStatesLocal{};
            unsigned nativeMaskLocal = 0;
            PollNativeSnapshotUnlocked(nativeStatesLocal, nativeMaskLocal);
            if (!XInputShim::IsPollingActive() || generation != g_pollingGeneration.load()) break;
            RefreshNewNativeCapabilitiesUnlocked(nativeMaskLocal);
            XInputShim::Snapshot nextSnapshot{};
            BuildSnapshot(nativeStatesLocal, nativeMaskLocal, nextSnapshot);
            const unsigned combinedMaskAfter = nextSnapshot.connectedMask;
            PublishSnapshot(nextSnapshot, generation);
            if (!XInputShim::IsPollingActive() || generation != g_pollingGeneration.load()) break;
            if (combinedMaskAfter == 0 && g_genericPads.empty()) {
                sleepMs = 250;
            } else {
                sleepMs = 16;
            }
            const DWORD nowTick = GetTickCount();
            const bool maskChanged = combinedMaskAfter != g_lastNamesPublishMask;
            const bool namesDue = g_lastNamesPublishTick == 0
                || (nowTick - g_lastNamesPublishTick) >= 2000
                || maskChanged;
            if (namesDue) {
                PublishControllerNamesUnlocked(combinedMaskAfter);
                g_lastNamesPublishTick = nowTick;
                g_lastNamesPublishMask = combinedMaskAfter;
            }
            if (!g_efzWindowActive.load(std::memory_order_relaxed)
                && !g_guiActive.load(std::memory_order_relaxed)) {
                sleepMs = (std::max)(sleepMs, static_cast<DWORD>(48));
            }
            Sleep(sleepMs);
        }
        ReleaseGenericPadsLocked();
        g_lastGenericEnumTick = 0;
        g_lastNamesPublishTick = 0;
        g_nativeCapabilitiesMask = 0;
        g_watcherStarted.store(false, std::memory_order_release);
        // A new session may have requested polling while an old driver call returned.
        if (XInputShim::IsPollingActive()) EnsureWatcherStarted();
    }

    void EnsureWatcherStarted() {
        if (!XInputShim::IsPollingActive()) return;
        bool expected = false;
        if (g_watcherStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            std::thread(ControllerWatcherThread).detach();
        }
    }

    void LogSlotSummary(const XInputShim::Snapshot& snapshot) {
        std::ostringstream oss;
        oss << "[GAMEPAD] Snapshot changed: combined=0x" << std::hex << std::uppercase << snapshot.connectedMask
            << " xinput=0x" << snapshot.nativeMask
            << " generic=0x" << snapshot.genericMask;
        for (int i = 0; i < 4; ++i) {
            if (!snapshot.IsConnected(i)) continue;
            oss << " slot" << std::dec << i << "="
                << (snapshot.genericSlots[i] ? "Generic:" : "XInput:");
            if (snapshot.slotNames[i][0] != '\0') {
                oss << snapshot.slotNames[i];
            } else {
                oss << (snapshot.genericSlots[i] ? "DirectInput Controller" : "Pad");
            }
        }
        std::string summary = oss.str();
        if (summary != g_lastSlotSummary) {
            g_lastSlotSummary = summary;
            LogOut(summary, true);
        }
    }
}

namespace XInputShim {
    bool IsPollingActive() {
        return g_pollingActive.load(std::memory_order_acquire) &&
            !g_onlineModeActive.load(std::memory_order_acquire) &&
            !g_isShuttingDown.load(std::memory_order_acquire);
    }

    void SetPollingActive(bool active) {
        active = active && !g_onlineModeActive.load() && !g_isShuttingDown.load();
        if (g_pollingActive.load(std::memory_order_acquire) == active) return;
        {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            if (g_pollingActive.load() == active) return;
            g_pollingActive.store(active, std::memory_order_release);
            g_pollingGeneration.fetch_add(1, std::memory_order_acq_rel);
            g_cachedSnapshot = {};
        }
        if (active) EnsureWatcherStarted();
    }

    bool Init() {
        EnsureWatcherStarted();
        if (g_xinput) return true;
        const char* cands[] = { "xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll" };
        for (const char* c : cands) {
            HMODULE h = TryLoad(c);
            if (h) {
                g_xinput = h;
                LogOut(std::string("[XINPUT] Loaded ") + c, true);
                return true;
            }
        }
        LogOut("[XINPUT] No XInput DLL found; gamepad features will use fallback paths only.", true);
        return false;
    }

    bool IsLoaded() { return g_xinput != nullptr; }
    const char* LoadedDllName() { return g_name ? g_name : "(none)"; }

    DWORD GetState(DWORD idx, XINPUT_STATE* st) {
        if (!st || idx > 3) return ERROR_DEVICE_NOT_CONNECTED;
        EnsureWatcherStarted();
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        if (!snapshot.CopyState(static_cast<int>(idx), *st)) {
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        return ERROR_SUCCESS;
    }

    DWORD GetCapabilities(DWORD idx, DWORD flags, XINPUT_CAPABILITIES* caps) {
        if (!caps || idx > 3) return ERROR_DEVICE_NOT_CONNECTED;
        EnsureWatcherStarted();
        (void)flags;
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        if (!snapshot.IsConnected(static_cast<int>(idx))) {
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        if (snapshot.capabilitiesValid[idx]) {
            *caps = snapshot.capabilities[idx];
            return ERROR_SUCCESS;
        }

        // Synthesized from the published state; driver calls from UI threads can block for seconds.
        ZeroMemory(caps, sizeof(*caps));
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Flags = 0;
        caps->Gamepad.wButtons = 0xFFFF;
        caps->Gamepad.bLeftTrigger = 0xFF;
        caps->Gamepad.bRightTrigger = 0xFF;
        caps->Gamepad.sThumbLX = 0xFF;
        caps->Gamepad.sThumbLY = 0xFF;
        caps->Gamepad.sThumbRX = 0xFF;
        caps->Gamepad.sThumbRY = 0xFF;
        return ERROR_SUCCESS;
    }

    void Enable(BOOL en) {
        if (!IsPollingActive()) return;
        if (!g_xinput && !Init()) return;
        if (pEnable) pEnable(en);
    }

    void RefreshSnapshotOncePerFrame() {
        EnsureWatcherStarted();
    }

    void CopySnapshot(Snapshot& out) {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        if (!IsPollingActive()) { out = {}; return; }
        out = g_cachedSnapshot;
    }

    bool CopyCachedState(int index, XINPUT_STATE& out) {
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        return snapshot.CopyState(index, out);
    }

    bool IsPadConnectedCached(int index) {
        if (index < 0 || index > 3) return false;
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        return snapshot.IsConnected(index);
    }

    unsigned GetConnectedMaskCached() {
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        return snapshot.connectedMask;
    }
    unsigned GetNativeConnectedMaskCached() {
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        return snapshot.nativeMask;
    }
    unsigned GetGenericConnectedMaskCached() {
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        return snapshot.genericMask;
    }

    bool GetPublishedControllerName(int index, char* buf, size_t bufLen) {
        if (!buf || bufLen == 0) return false;
        if (index < 0 || index > 3) {
            buf[0] = '\0';
            return false;
        }
        // try_lock only; on contention fall back rather than stall the render thread.
        std::unique_lock<std::mutex> lock(g_namesMutex, std::try_to_lock);
        if (lock.owns_lock() && g_publishedNamesValid[index]) {
            _snprintf_s(buf, bufLen, _TRUNCATE, "%s", g_publishedNames[index]);
            return true;
        }
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        if (!snapshot.IsConnected(index)) {
            _snprintf_s(buf, bufLen, _TRUNCATE, "Pad %d (Disconnected)", index);
        } else {
            _snprintf_s(buf, bufLen, _TRUNCATE, "Controller %d", index);
        }
        return false;
    }

    bool IsGenericFallbackSlot(int index) {
        if (index < 0 || index > 3) return false;
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        return snapshot.genericSlots[index];
    }

    std::string GetSlotDisplayName(int index) {
        if (index < 0 || index > 3) return std::string();
        Snapshot snapshot{};
        CopySnapshot(snapshot);
        return snapshot.slotNames[index];
    }
}
