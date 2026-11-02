#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <string>

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

#ifndef VK_GAMEPAD_A
#define VK_GAMEPAD_A                       0x5800
#define VK_GAMEPAD_B                       0x5801
#define VK_GAMEPAD_X                       0x5802
#define VK_GAMEPAD_Y                       0x5803
#define VK_GAMEPAD_RIGHT_SHOULDER          0x5804
#define VK_GAMEPAD_LEFT_SHOULDER           0x5805
#define VK_GAMEPAD_LEFT_TRIGGER            0x5806
#define VK_GAMEPAD_RIGHT_TRIGGER           0x5807
#define VK_GAMEPAD_DPAD_UP                 0x5808
#define VK_GAMEPAD_DPAD_DOWN               0x5809
#define VK_GAMEPAD_DPAD_LEFT               0x580A
#define VK_GAMEPAD_DPAD_RIGHT              0x580B
#define VK_GAMEPAD_MENU                    0x580C
#define VK_GAMEPAD_VIEW                    0x580D
#define VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON  0x580E
#define VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0x580F
#endif

#ifndef XINPUT_DEVSUBTYPE_WHEEL
#define XINPUT_DEVSUBTYPE_WHEEL            0x02
#define XINPUT_DEVSUBTYPE_ARCADE_STICK     0x03
#define XINPUT_DEVSUBTYPE_FLIGHT_STICK     0x04
#define XINPUT_DEVSUBTYPE_DANCE_PAD        0x05
#define XINPUT_DEVSUBTYPE_GUITAR           0x06
#define XINPUT_DEVSUBTYPE_GUITAR_ALTERNATE 0x07
#define XINPUT_DEVSUBTYPE_DRUM_KIT         0x08
#define XINPUT_DEVSUBTYPE_GUITAR_BASS      0x0B
#endif

namespace XPCompat {

bool IsEnabled();
unsigned long long GetTickCount64Compat();
std::string GetRuntimeSummary();

}
