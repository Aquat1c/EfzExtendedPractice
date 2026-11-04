#pragma once
#define DIRECTINPUT_VERSION 0x0800 // Define before including dinput.h
#include <windows.h>
#include <atomic>
#include <thread>
#include <dinput.h>
#include <vector>
#include <string>
#include <sstream>
extern LPDIRECTINPUT8 g_pDI;
extern LPDIRECTINPUTDEVICE8 g_pKeyboard;

#include "../core/di_keycodes.h"

extern std::atomic<bool> autoAirtechEnabled;
extern std::atomic<bool> autoJumpEnabled;
extern std::atomic<int> autoAirtechDirection;
extern std::atomic<int> jumpDirection;
extern std::atomic<int> jumpTarget;
extern std::atomic<bool> menuOpen;
bool IsKeyMonitoringRunning();
void StopKeyMonitoring();

extern bool g_directInputAvailable;

extern bool g_directInputReadOnly;
static std::atomic<bool> globalF1ThreadRunning{true};

void MonitorKeys();
void RestartKeyMonitoring();
bool InitDirectInput(HINSTANCE hInstance);
void CleanupDirectInput();
bool ReadDirectInputKeyboardState(BYTE* keyboardState);
bool IsDIKeyPressed(BYTE* keyboardState, DWORD dikCode);
bool InitDirectInputReadOnly(HINSTANCE hInstance);
bool PollDirectInputState(BYTE* keyboardState);

void DetectKeyBindingsWithDI();
void DetectKeyBindingsWithWinAPI();

int MapEFZKeyToVK(unsigned short efzKey);
bool ReadKeyMappingsFromIni();
bool ReadDirectInputKeyboardState(BYTE* keyboardState);

void StartHotkeyCooldown();

int MapEFZKeyToVK(unsigned short efzKey);
extern std::atomic<int> frameCount;
extern std::atomic<int> startFrameCount;

struct GamepadDevice {
    LPDIRECTINPUTDEVICE8 device;
    DIJOYSTATE2 state;
    DIJOYSTATE2 prevState;
    bool connected;
    std::string name;
};

extern GamepadDevice g_gamepads[MAX_CONTROLLERS];
extern int g_gamepadCount;

bool InitDirectInputGamepads();
BOOL CALLBACK EnumGamepadsCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext);
bool UpdateGamepadState(int gamepadIndex);
bool IsGamepadButtonPressed(int gamepadIndex, int buttonIndex);
bool IsGamepadButtonHeld(int gamepadIndex, int buttonIndex);
bool IsGamepadAxisActive(int gamepadIndex, int axisIndex, int threshold = 500);
void GlobalF1MonitorThread();