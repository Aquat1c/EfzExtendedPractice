#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace EFZInputBindings {

enum class DeviceKind : std::uint8_t {
    None = 0,
    Keyboard,
    Joystick1,
    Joystick2,
};

enum class Action : std::uint8_t {
    Up = 0,
    Down,
    Left,
    Right,
    A,
    B,
    C,
    D,
    Count,
};

struct Binding {
    DeviceKind device = DeviceKind::None;
    std::uint8_t code = 0;
    std::uint16_t engineValue = 0;
    bool valid = false;
};

struct PlayerBindings {
    std::array<Binding, static_cast<std::size_t>(Action::Count)> actions{};
};

struct BindingSet {
    std::array<PlayerBindings, 2> players{};
    bool valid = false;
};

constexpr std::size_t kActiveKeyIniBytes = 32;

inline Binding DecodeKeyIniPair(std::uint8_t deviceByte,
                                std::uint8_t storedCode) {
    Binding result{};
    if (deviceByte == 0) {
        // DIK 0 is not a bindable keyboard key in EFZ and is used by empty
        // slots in the stock file.
        if (storedCode == 0) return result;
        result.device = DeviceKind::Keyboard;
        result.code = storedCode;
        result.engineValue = storedCode;
        result.valid = true;
        return result;
    }
    if ((deviceByte == 1 || deviceByte == 2) && storedCode != 0) {
        // EFZ stores controller codes one-based in key.ini, then subtracts one
        // while building its live 0x100/0x200 control-map WORD.
        result.device = deviceByte == 1
            ? DeviceKind::Joystick1 : DeviceKind::Joystick2;
        result.code = static_cast<std::uint8_t>(storedCode - 1);
        result.engineValue = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(deviceByte) << 8) | result.code);
        result.valid = result.code <= 19; // 0..15 buttons, 16..19 directions
        return result;
    }
    return result;
}

inline bool DecodeActiveKeyIni(const std::uint8_t* bytes, std::size_t size,
                               BindingSet& output) {
    output = BindingSet{};
    if (!bytes || size < kActiveKeyIniBytes) return false;
    for (std::size_t player = 0; player < output.players.size(); ++player) {
        for (std::size_t action = 0;
             action < static_cast<std::size_t>(Action::Count); ++action) {
            const std::size_t offset = player * 16 + action * 2;
            output.players[player].actions[action] =
                DecodeKeyIniPair(bytes[offset], bytes[offset + 1]);
        }
    }
    output.valid = true;
    return true;
}

// XInput-compatible masks used by the mod's controller settings.  Kept here
// as numeric policy constants so parsing/tests do not depend on Windows SDK.
constexpr int kPadStart = 0x0010;
constexpr int kPadBack = 0x0020;
constexpr int kPadL3 = 0x0040;
constexpr int kPadR3 = 0x0080;
constexpr int kPadLB = 0x0100;
constexpr int kPadRB = 0x0200;
constexpr int kPadA = 0x1000;
constexpr int kPadB = 0x2000;
constexpr int kPadX = 0x4000;
constexpr int kPadY = 0x8000;
constexpr int kPadLT = 0x10000;
constexpr int kPadRT = 0x20000;

inline int ConventionalPadMask(const Binding& binding) {
    if (!binding.valid || binding.device == DeviceKind::Keyboard) return -1;
    constexpr int map[12] = {
        kPadX, kPadA, kPadB, kPadY, kPadLB, kPadRB,
        kPadLT, kPadRT, kPadBack, kPadStart, kPadL3, kPadR3,
    };
    return binding.code < 12 ? map[binding.code] : -1;
}

struct FirstRunKeyboardPlan {
    int teleport = -1;
    int recordPosition = -1;
    int savestateSave = -1;
    int savestateLoad = -1;
    int savestatePrevious = -1;
    int savestateNext = -1;
    int switchPlayers = -1;
    int macroRecord = -1;
    int macroPlay = -1;
    int macroSlot = -1;
    int uiAccept = -1;
    int uiRefresh = -1;
    int uiExit = -1;
    int framestepPause = -1;
    int framestepStep = -1;
};

struct FirstRunGamepadPlan {
    int teleport = -1;
    int savePosition = -1;
    int switchPlayers = -1;
    int swapPositions = -1;
    int macroRecord = -1;
    int macroPlay = -1;
    int macroSlot = -1;
    int toggleMenu = -1;
    // D-pad focus navigation covers every browser/menu.  Leaving these
    // disabled avoids assigning the same shoulder to gameplay and UI roles.
    int topTabPrevious = -1;
    int topTabNext = -1;
    int subTabPrevious = -1;
    int subTabNext = -1;
};

inline void CollectReservations(const BindingSet& bindings,
                                std::array<bool, 256>& keyboard,
                                std::array<int, 24>& pads,
                                std::size_t& padCount) {
    keyboard.fill(false);
    pads.fill(-1);
    padCount = 0;
    if (!bindings.valid) return;
    auto reservePad = [&](int mask) {
        if (mask < 0) return;
        for (std::size_t i = 0; i < padCount; ++i) {
            if (pads[i] == mask) return;
        }
        if (padCount < pads.size()) pads[padCount++] = mask;
    };
    for (const PlayerBindings& player : bindings.players) {
        for (const Binding& binding : player.actions) {
            if (!binding.valid) continue;
            if (binding.device == DeviceKind::Keyboard) {
                keyboard[binding.code] = true; // DIK reservation; VK is added by runtime
                continue;
            }
            // DirectInput face-button numbering is not portable (DualSense
            // reports Square/Cross/Circle/Triangle as 0/1/2/3; Xbox-style
            // reports A/B/X/Y in those slots) and key.ini stores only the raw
            // number.  Any used face slot reserves the whole cluster so a
            // first-run hotkey cannot collide across controllers.
            if (binding.code < 4) {
                reservePad(kPadA);
                reservePad(kPadB);
                reservePad(kPadX);
                reservePad(kPadY);
                continue;
            }
            const int mask = ConventionalPadMask(binding);
            reservePad(mask);
        }
    }
}

inline bool Contains(const int* values, std::size_t count, int value) {
    for (std::size_t i = 0; i < count; ++i) {
        if (values[i] == value) return true;
    }
    return false;
}

inline int ChooseUniqueKey(int preferred,
                           const std::array<bool, 256>& reservedVk,
                           std::array<int, 32>& used,
                           std::size_t& usedCount) {
    constexpr int candidates[] = {
        // 5 and 6 are the stats page keys, which stay hardwired.
        '1','2','3','4','7','8','9','0',
        'U','J','L','I','O','K','E','R','Q','P','T','Y','G','H','N','M','V','B','F','D','S','W',
        0xBC,0xBE,0x20,0x24,0x23,0x2D,0x2E,
    };
    auto allowed = [&](int key) {
        return key > 0 && key < 256 && !reservedVk[key] &&
               !Contains(used.data(), usedCount, key);
    };
    if (allowed(preferred)) {
        used[usedCount++] = preferred;
        return preferred;
    }
    for (int key : candidates) {
        if (!allowed(key)) continue;
        used[usedCount++] = key;
        return key;
    }
    return -1;
}

inline FirstRunKeyboardPlan BuildFirstRunKeyboardPlan(
    const std::array<bool, 256>& reservedVk) {
    std::array<int, 32> used{};
    std::size_t usedCount = 0;
    FirstRunKeyboardPlan plan{};
    plan.teleport = ChooseUniqueKey('1', reservedVk, used, usedCount);
    plan.recordPosition = ChooseUniqueKey('2', reservedVk, used, usedCount);
    plan.savestateSave = ChooseUniqueKey('U', reservedVk, used, usedCount);
    plan.savestateLoad = ChooseUniqueKey('J', reservedVk, used, usedCount);
    plan.savestatePrevious = ChooseUniqueKey(0xBC, reservedVk, used, usedCount);
    plan.savestateNext = ChooseUniqueKey(0xBE, reservedVk, used, usedCount);
    plan.switchPlayers = ChooseUniqueKey('L', reservedVk, used, usedCount);
    plan.macroRecord = ChooseUniqueKey('I', reservedVk, used, usedCount);
    plan.macroPlay = ChooseUniqueKey('O', reservedVk, used, usedCount);
    plan.macroSlot = ChooseUniqueKey('K', reservedVk, used, usedCount);
    plan.uiAccept = ChooseUniqueKey('E', reservedVk, used, usedCount);
    plan.uiRefresh = ChooseUniqueKey('R', reservedVk, used, usedCount);
    plan.uiExit = ChooseUniqueKey('Q', reservedVk, used, usedCount);
    plan.framestepPause = ChooseUniqueKey(0x20, reservedVk, used, usedCount);
    plan.framestepStep = ChooseUniqueKey('P', reservedVk, used, usedCount);
    return plan;
}

inline int ChooseUniquePad(int preferred, const int* reserved,
                           std::size_t reservedCount,
                           std::array<int, 12>& used,
                           std::size_t& usedCount) {
    constexpr int candidates[] = {
        kPadStart, kPadBack, kPadL3, kPadR3, kPadLB, kPadRB,
        kPadLT, kPadRT, kPadX, kPadY,
    };
    auto allowed = [&](int mask) {
        return mask >= 0 && !Contains(reserved, reservedCount, mask) &&
               !Contains(used.data(), usedCount, mask);
    };
    if (allowed(preferred)) {
        used[usedCount++] = preferred;
        return preferred;
    }
    for (int mask : candidates) {
        if (!allowed(mask)) continue;
        used[usedCount++] = mask;
        return mask;
    }
    return -1;
}

inline FirstRunGamepadPlan BuildFirstRunGamepadPlan(
    const int* reserved, std::size_t reservedCount) {
    std::array<int, 12> used{};
    std::size_t usedCount = 0;
    FirstRunGamepadPlan plan{};
    // Menu access and recording are prioritized when a controller has very
    // few buttons left after EFZ's own actions are reserved.
    plan.toggleMenu = ChooseUniquePad(kPadStart, reserved, reservedCount, used, usedCount);
    plan.macroRecord = ChooseUniquePad(kPadLB, reserved, reservedCount, used, usedCount);
    plan.macroPlay = ChooseUniquePad(kPadRT, reserved, reservedCount, used, usedCount);
    plan.macroSlot = ChooseUniquePad(kPadLT, reserved, reservedCount, used, usedCount);
    plan.teleport = ChooseUniquePad(kPadBack, reserved, reservedCount, used, usedCount);
    plan.savePosition = ChooseUniquePad(kPadL3, reserved, reservedCount, used, usedCount);
    plan.switchPlayers = ChooseUniquePad(kPadRB, reserved, reservedCount, used, usedCount);
    plan.swapPositions = ChooseUniquePad(kPadR3, reserved, reservedCount, used, usedCount);
    return plan;
}

bool LoadActiveKeyIni(BindingSet& output, std::string& sourcePath,
                      std::string& error);

} // namespace EFZInputBindings
