#include "input/efz_input_bindings.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool value, const char* message) {
    if (value) return;
    std::cerr << "efz_input_bindings_tests: " << message << '\n';
    std::exit(1);
}

bool KeyPlanHas(const EFZInputBindings::FirstRunKeyboardPlan& plan, int key) {
    const int values[] = {
        plan.teleport, plan.recordPosition, plan.savestateSave,
        plan.savestateLoad, plan.savestatePrevious, plan.savestateNext,
        plan.switchPlayers, plan.macroRecord, plan.macroPlay, plan.macroSlot,
        plan.uiAccept, plan.uiRefresh, plan.uiExit,
        plan.framestepPause, plan.framestepStep,
    };
    for (int value : values) if (value == key) return true;
    return false;
}

}

int main() {
    using namespace EFZInputBindings;
    std::array<std::uint8_t, kActiveKeyIniBytes> bytes{};
    // P1 keyboard arrows/Z/X/C/A.
    const std::uint8_t p1Codes[8] = {200,208,203,205,44,45,46,30};
    for (std::size_t i = 0; i < 8; ++i) bytes[i * 2 + 1] = p1Codes[i];
    // P2 joystick 1 axes and buttons 0..3. File controller codes are one-based.
    const std::uint8_t p2Codes[8] = {17,18,19,20,1,2,3,4};
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[16 + i * 2] = 1;
        bytes[16 + i * 2 + 1] = p2Codes[i];
    }

    BindingSet bindings{};
    Require(DecodeActiveKeyIni(bytes.data(), bytes.size(), bindings) &&
            bindings.valid, "valid active binding block did not decode");
    Require(bindings.players[0].actions[0].device == DeviceKind::Keyboard &&
            bindings.players[0].actions[0].code == 200,
            "keyboard binding was not kept typed");
    Require(bindings.players[1].actions[0].device == DeviceKind::Joystick1 &&
            bindings.players[1].actions[0].code == 16 &&
            bindings.players[1].actions[4].code == 0,
            "controller one-based code/device decoding drifted");
    Require(ConventionalPadMask(bindings.players[1].actions[4]) == kPadX &&
            ConventionalPadMask(bindings.players[1].actions[5]) == kPadA,
            "DirectInput face-button reservations do not match shim mapping");
    Require(!DecodeActiveKeyIni(bytes.data(), 31, bindings),
            "truncated active binding block was accepted");

    Require(DecodeActiveKeyIni(bytes.data(), bytes.size(), bindings),
            "binding block could not be decoded a second time");
    std::array<bool, 256> reservedDik{};
    std::array<int, 24> reservedPads{};
    std::size_t reservedPadCount = 0;
    CollectReservations(bindings, reservedDik, reservedPads, reservedPadCount);
    Require(reservedDik[44] && reservedDik[205] && reservedPadCount == 4 &&
            Contains(reservedPads.data(), reservedPadCount, kPadA) &&
            Contains(reservedPads.data(), reservedPadCount, kPadB) &&
            Contains(reservedPads.data(), reservedPadCount, kPadX) &&
            Contains(reservedPads.data(), reservedPadCount, kPadY),
            "both players' keyboard/controller reservations were not collected");

    // Runtime converts DIK reservations to VK before this policy is called.
    std::array<bool, 256> reservedVk{};
    reservedVk['1'] = true;
    reservedVk['U'] = true;
    reservedVk['P'] = true;
    const FirstRunKeyboardPlan keys = BuildFirstRunKeyboardPlan(reservedVk);
    Require(!KeyPlanHas(keys, '1') && !KeyPlanHas(keys, 'U') &&
            !KeyPlanHas(keys, 'P'),
            "first-run keyboard plan reused an EFZ key");
    const int keyValues[] = {
        keys.teleport, keys.recordPosition, keys.savestateSave,
        keys.savestateLoad, keys.savestatePrevious, keys.savestateNext,
        keys.switchPlayers, keys.macroRecord, keys.macroPlay, keys.macroSlot,
        keys.uiAccept, keys.uiRefresh, keys.uiExit,
        keys.framestepPause, keys.framestepStep,
    };
    for (std::size_t i = 0; i < sizeof(keyValues)/sizeof(keyValues[0]); ++i) {
        Require(keyValues[i] > 0, "first-run keyboard pool unexpectedly exhausted");
        for (std::size_t j = i + 1; j < sizeof(keyValues)/sizeof(keyValues[0]); ++j) {
            Require(keyValues[i] != keyValues[j],
                    "first-run keyboard plan assigned one key twice");
        }
    }

    const int padReservations[] = {kPadX, kPadA, kPadB, kPadY};
    const FirstRunGamepadPlan pads = BuildFirstRunGamepadPlan(
        padReservations, sizeof(padReservations)/sizeof(padReservations[0]));
    const int padValues[] = {
        pads.teleport, pads.savePosition, pads.switchPlayers,
        pads.swapPositions, pads.macroRecord, pads.macroPlay,
        pads.macroSlot, pads.toggleMenu,
    };
    for (std::size_t i = 0; i < sizeof(padValues)/sizeof(padValues[0]); ++i) {
        Require(padValues[i] != kPadX && padValues[i] != kPadA &&
                padValues[i] != kPadB && padValues[i] != kPadY,
                "first-run gamepad plan reused an EFZ attack button");
        for (std::size_t j = i + 1; j < sizeof(padValues)/sizeof(padValues[0]); ++j) {
            if (padValues[i] >= 0 && padValues[j] >= 0) {
                Require(padValues[i] != padValues[j],
                        "first-run gamepad plan assigned one control twice");
            }
        }
    }
    Require(pads.topTabPrevious < 0 && pads.topTabNext < 0 &&
            pads.subTabPrevious < 0 && pads.subTabNext < 0,
            "first-run UI shortcuts consumed gameplay shoulders");
    return 0;
}
