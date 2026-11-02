#pragma once

#include <cstdint>

#include "../core/constants.h"

namespace CharacterActionCatalog {

constexpr uint8_t kA = 1u << 0;
constexpr uint8_t kB = 1u << 1;
constexpr uint8_t kC = 1u << 2;
constexpr uint8_t kD = 1u << 3;
constexpr uint8_t kABC = kA | kB | kC;
constexpr uint8_t kABCD = kABC | kD;

// Concrete pool indices 0..82 are the shipped positional ABI.  New recipes
// are appended only; never reorder these values or per-entry delays will drift.
constexpr int kLegacyPoolCount = 83;
constexpr int kPool1X = 83;
constexpr int kPool3X = 87;
constexpr int kPoolJ2X = 91;
constexpr int kPoolJ6X = 95;
constexpr int kPool66X = 99;
constexpr int kPool662X = 103;
constexpr int kPool664X = 107;
constexpr int kPoolKaoriRecoilDuck = 111;
constexpr int kPoolCount = 112;

constexpr bool IsKnownCharacter(int charId) {
    return charId >= CHAR_ID_AKANE && charId <= CHAR_ID_KANO;
}

constexpr bool IsAlwaysAvailableAction(int action) {
    return action == ACTION_JUMP || action == ACTION_BACKDASH ||
           action == ACTION_FORWARD_DASH || action == ACTION_BLOCK;
}

constexpr bool HasFinalMemoryInput(int charId) {
    return IsKnownCharacter(charId) &&
           charId != CHAR_ID_EXNANASE &&
           charId != CHAR_ID_UNKNOWN_BOSS;
}

constexpr uint8_t BasicNormalMask(int charId, int action) {
    if (action < ACTION_5A || action > ACTION_JD) return 0;
    const int button = action & 3;
    if (button < 3) return static_cast<uint8_t>(1u << button);

    // S/D is not universal; these match CharacterSupportsDIntent in the
    // native input hook.
    if (action == ACTION_JD) {
        return (charId == CHAR_ID_MIO || charId == CHAR_ID_AYU ||
                charId == CHAR_ID_UNKNOWN_BOSS ||
                charId == CHAR_ID_UNKNOWN) ? kD : 0;
    }
    if (action == ACTION_2D) {
        return charId == CHAR_ID_NAYUKI ? kD : 0;
    }
    if (action != ACTION_5D) return 0;
    return (charId == CHAR_ID_MINAGI || charId == CHAR_ID_MIO ||
            charId == CHAR_ID_NANASE || charId == CHAR_ID_NAYUKI ||
            charId == CHAR_ID_SHIORI || charId == CHAR_ID_MAI ||
            charId == CHAR_ID_UNKNOWN_BOSS ||
            charId == CHAR_ID_UNKNOWN || charId == CHAR_ID_KANO) ? kD : 0;
}

constexpr uint8_t CommandNormalMask(int charId, int action) {
    switch (charId) {
        case CHAR_ID_AKANE:
            return action == ACTION_J6X ? kC : 0;
        case CHAR_ID_AYU:
            return action == ACTION_6A ? 0 :
                   action == ACTION_6B ? kB :
                   action == ACTION_6C ? kC :
                   action == ACTION_J2X ? kC : 0;
        case CHAR_ID_EXNANASE:
            return action == ACTION_6C ? kC : 0;
        case CHAR_ID_KANO:
            return action == ACTION_6B ? kB :
                   action == ACTION_6C ? kC :
                   action == ACTION_3X ? kC :
                   action == ACTION_J2X ? kABC : 0;
        case CHAR_ID_MAI:
            return action == ACTION_6A ? kA :
                   action == ACTION_6B ? kB :
                   action == ACTION_6C ? kC :
                   action == ACTION_4C ? kC :
                   (action == ACTION_4D || action == ACTION_6D) ? kD : 0;
        case CHAR_ID_MINAGI:
            return (action == ACTION_4D || action == ACTION_6D) ? kD : 0;
        case CHAR_ID_MAKOTO:
            return action == ACTION_6B ? kB :
                   action == ACTION_J2X ? kC : 0;
        case CHAR_ID_MAYU:
            return action == ACTION_6B ? kB :
                   action == ACTION_1X ? kB : 0;
        case CHAR_ID_MIO:
            return action == ACTION_6C ? kC :
                   action == ACTION_4B ? kB :
                   action == ACTION_J2X ? (kB | kC) : 0;
        case CHAR_ID_MISHIO:
            return action == ACTION_6B ? kB :
                   action == ACTION_6C ? kC : 0;
        case CHAR_ID_MISUZU:
            return action == ACTION_6C ? kC : 0;
        case CHAR_ID_MIZUKA:
            return action == ACTION_6B ? kB : 0;
        case CHAR_ID_UNKNOWN:
            return action == ACTION_J6X ? kC : 0;
        case CHAR_ID_NAYUKI:
            return action == ACTION_6B ? kB : 0;
        default:
            return 0;
    }
}

constexpr uint8_t DashNormalMask(int charId, int action) {
    const bool standard =
        charId == CHAR_ID_AKANE || charId == CHAR_ID_EXNANASE ||
        charId == CHAR_ID_IKUMI || charId == CHAR_ID_MAKOTO ||
        charId == CHAR_ID_MINAGI || charId == CHAR_ID_MISAKI ||
        charId == CHAR_ID_MISHIO || charId == CHAR_ID_MIZUKA ||
        charId == CHAR_ID_NANASE ||
        charId == CHAR_ID_NAYUKIB || charId == CHAR_ID_SHIORI ||
        charId == CHAR_ID_UNKNOWN || charId == CHAR_ID_KANO;
    if (standard && (action == ACTION_66X || action == ACTION_662X)) {
        return kABC;
    }
    if (charId == CHAR_ID_KAORI) {
        if (action == ACTION_66X) return kABC;
        if (action == ACTION_662X) return kC;
    }
    if (charId == CHAR_ID_MIO) {
        if (action == ACTION_66X || action == ACTION_662X) return kABC;
    }
    // Boss UNKNOWN (resource "mizuka"): 66A-C confirmed, 662 family unverified
    // and not advertised. Not CHAR_ID_MIZUKA ("nagamori").
    if (charId == CHAR_ID_UNKNOWN_BOSS && action == ACTION_66X) return kABC;
    if (charId == CHAR_ID_MAYU && action == ACTION_664X) return kABC;
    return 0;
}

constexpr uint8_t SpecialMask(int charId, int action) {
    switch (charId) {
        case CHAR_ID_AKANE:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_412) ? kABC : 0;
        case CHAR_ID_AKIKO:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_41236) ? kABC : 0;
        case CHAR_ID_AYU:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB) ? kABC : 0;
        case CHAR_ID_EXNANASE:
            return (action == ACTION_41236 || action == ACTION_QCB ||
                    action == ACTION_DP) ? kABC : 0;
        case CHAR_ID_IKUMI:
            if (action == ACTION_41236) return kC;
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB) ? kABC : 0;
        case CHAR_ID_KANNA:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_41236) ? kABC : 0;
        case CHAR_ID_KANO:
            if (action == ACTION_QCF) return kABCD;
            return (action == ACTION_QCB || action == ACTION_421) ? kABC : 0;
        case CHAR_ID_KAORI:
            return (action == ACTION_DP || action == ACTION_QCB) ? kABC : 0;
        case CHAR_ID_MAI:
            if (action == ACTION_QCF || action == ACTION_QCB ||
                action == ACTION_412) return kABCD;
            if (action == ACTION_22) return kD;
            return action == ACTION_DP ? kABC : 0;
        case CHAR_ID_MAKOTO:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_412) ? kABC : 0;
        case CHAR_ID_MAYU:
            if (action == ACTION_41236 || action == ACTION_QCB) return kABCD;
            return (action == ACTION_QCF || action == ACTION_DP) ? kABC : 0;
        case CHAR_ID_MINAGI:
            return (action == ACTION_QCF || action == ACTION_QCB ||
                    action == ACTION_421 || action == ACTION_41236 ||
                    action == ACTION_DP) ? kABC : 0;
        case CHAR_ID_MIO:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_412) ? kABC : 0;
        case CHAR_ID_MISAKI:
            if (action == ACTION_22) return kA | kB;
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB) ? kABC : 0;
        case CHAR_ID_MISHIO:
            if (action == ACTION_22) return kA | kB;
            return (action == ACTION_DP || action == ACTION_QCF ||
                    action == ACTION_QCB || action == ACTION_412) ? kABC : 0;
        case CHAR_ID_MISUZU:
            return (action == ACTION_QCB || action == ACTION_QCF ||
                    action == ACTION_41236 || action == ACTION_421 ||
                    action == ACTION_DP) ? kABC : 0;
        case CHAR_ID_MIZUKA:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_412) ? kABC : 0;
        case CHAR_ID_UNKNOWN_BOSS:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_41236) ? kABC : 0;
        case CHAR_ID_NANASE:
            return (action == ACTION_QCF || action == ACTION_QCB ||
                    action == ACTION_DP || action == ACTION_41236) ? kABC : 0;
        case CHAR_ID_NAYUKI:
            if (action == ACTION_QCF) return kABCD;
            return (action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_41236) ? kABC : 0;
        case CHAR_ID_NAYUKIB:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_412) ? kABC : 0;
        case CHAR_ID_SAYURI:
            return (action == ACTION_QCF || action == ACTION_QCB ||
                    action == ACTION_DP || action == ACTION_41236 ||
                    action == ACTION_412) ? kABC : 0;
        case CHAR_ID_SHIORI:
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_412) ? kABC : 0;
        case CHAR_ID_UNKNOWN:
            if (action == ACTION_22) return kD;
            return (action == ACTION_QCF || action == ACTION_DP ||
                    action == ACTION_QCB || action == ACTION_421 ||
                    action == ACTION_41236) ? kABC : 0;
        default:
            return 0;
    }
}

constexpr uint8_t SuperMask(int charId, int action) {
    switch (charId) {
        case CHAR_ID_AKANE:
        case CHAR_ID_AKIKO:
        case CHAR_ID_AYU:
        case CHAR_ID_MAKOTO:
            return (action == ACTION_214214 || action == ACTION_236236 ||
                    action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_EXNANASE:
            return (action == ACTION_214214 || action == ACTION_236236) ? kABC : 0;
        case CHAR_ID_IKUMI:
            return (action == ACTION_214214 || action == ACTION_2141236 ||
                    action == ACTION_4123641236) ? kABC : 0;
        case CHAR_ID_KANNA:
            return action == ACTION_236236 ? (kA | kB) : 0;
        case CHAR_ID_KANO:
            if (action == ACTION_2141236) return kABC;
            if (action == ACTION_22) return kB;
            return (action == ACTION_214214 || action == ACTION_236236 ||
                    action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_KAORI:
            return (action == ACTION_214214 || action == ACTION_236236 ||
                    action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_MAI:
            if (action == ACTION_236236) return kABC;
            return action == ACTION_214214 ? kABC : 0;
        case CHAR_ID_MAYU:
            return (action == ACTION_214214 || action == ACTION_2141236 ||
                    action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_MINAGI:
            return (action == ACTION_214214 || action == ACTION_2141236 ||
                    action == ACTION_236236) ? kABC : 0;
        case CHAR_ID_MIO:
            return (action == ACTION_214214 || action == ACTION_236236) ? kABC : 0;
        case CHAR_ID_MISAKI:
            return (action == ACTION_236236 || action == ACTION_6321463214) ? kABC : 0;
        case CHAR_ID_MISHIO:
            return (action == ACTION_214214 || action == ACTION_236236 ||
                    action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_MISUZU:
            return (action == ACTION_214214 || action == ACTION_236236 ||
                    action == ACTION_463214) ? kABC : 0;
        case CHAR_ID_MIZUKA:
            if (action == ACTION_214214) return kABC;
            return (action == ACTION_236236 || action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_UNKNOWN_BOSS:
            if (action == ACTION_214214 || action == ACTION_463214) return kC;
            return (action == ACTION_236236 || action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_NANASE:
            if (action == ACTION_4123641236) return kABC;
            return (action == ACTION_214214 || action == ACTION_236236) ? kABC : 0;
        case CHAR_ID_NAYUKI:
            return (action == ACTION_236236 || action == ACTION_214214) ? kABC : 0;
        case CHAR_ID_NAYUKIB:
            return (action == ACTION_214214 || action == ACTION_236236 ||
                    action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_SAYURI:
            return (action == ACTION_236236 || action == ACTION_2141236 ||
                    action == ACTION_6321463214) ? kABC : 0;
        case CHAR_ID_SHIORI:
            return (action == ACTION_214214 || action == ACTION_2141236 ||
                    action == ACTION_641236) ? kABC : 0;
        case CHAR_ID_UNKNOWN:
            return (action == ACTION_236236 || action == ACTION_463214) ? kABC : 0;
        default:
            return 0;
    }
}

constexpr uint8_t StrengthMask(int charId, int action) {
    if (action == ACTION_KAORI_RECOIL_DUCK) {
        return charId == CHAR_ID_KAORI ? kA : 0;
    }
    if (!IsKnownCharacter(charId)) return kABCD;
    if (action == ACTION_FINAL_MEMORY) return HasFinalMemoryInput(charId) ? kA : 0;
    if (IsAlwaysAvailableAction(action)) return kABCD;
    const uint8_t normal = BasicNormalMask(charId, action);
    if (normal) return normal;
    const uint8_t command = CommandNormalMask(charId, action);
    if (command) return command;
    const uint8_t dash = DashNormalMask(charId, action);
    if (dash) return dash;
    const uint8_t special = SpecialMask(charId, action);
    if (special) return special;
    return SuperMask(charId, action);
}

constexpr bool IsAvailable(int charId, int action, int strength) {
    if (action == ACTION_FINAL_MEMORY) return HasFinalMemoryInput(charId);
    if (action == ACTION_JUMP ||
        action == ACTION_BACKDASH || action == ACTION_FORWARD_DASH ||
        action == ACTION_BLOCK) return true;
    if (strength < 0 || strength > 3) return false;
    return (StrengthMask(charId, action) & (1u << strength)) != 0;
}

constexpr bool AnyAvailable(int charId, int action) {
    return StrengthMask(charId, action) != 0;
}

constexpr int FirstAvailableStrength(int charId, int action, int preferred = 0) {
    const uint8_t mask = StrengthMask(charId, action);
    if (mask == 0) return 0;
    if (preferred >= 0 && preferred < 4 && (mask & (1u << preferred))) return preferred;
    for (int i = 0; i < 4; ++i) if (mask & (1u << i)) return i;
    return 0;
}

constexpr int NextAvailableStrength(int charId, int action, int current, int direction) {
    const uint8_t mask = StrengthMask(charId, action);
    if (mask == 0 || direction == 0) return FirstAvailableStrength(charId, action, current);
    int value = current;
    for (int i = 0; i < 4; ++i) {
        value = (value + (direction > 0 ? 1 : 3)) & 3;
        if (mask & (1u << value)) return value;
    }
    return FirstAvailableStrength(charId, action, current);
}

constexpr bool PoolIndexToAction(int index, int& action, int& strength) {
    if (index < 0 || index >= kPoolCount) return false;
    if (index <= 19) { action = index; strength = index & 3; return true; }
    if (index >= 20 && index <= 43) {
        constexpr int actions[6] = {ACTION_QCF, ACTION_DP, ACTION_QCB,
                                    ACTION_421, ACTION_412, ACTION_22};
        const int local = index - 20;
        action = actions[local / 4]; strength = local & 3; return true;
    }
    if (index >= 44 && index <= 75) {
        constexpr int actions[8] = {ACTION_41236, ACTION_2141236, ACTION_236236,
                                    ACTION_214214, ACTION_641236, ACTION_463214,
                                    ACTION_4123641236, ACTION_6321463214};
        const int local = index - 44;
        action = actions[local / 4]; strength = local & 3; return true;
    }
    if (index == 76) { action = ACTION_FINAL_MEMORY; strength = 0; return true; }
    if (index >= 77 && index <= 79) { action = ACTION_JUMP; strength = index - 77; return true; }
    if (index == 80) { action = ACTION_BACKDASH; strength = 0; return true; }
    if (index == 81) { action = ACTION_FORWARD_DASH; strength = 0; return true; }
    if (index == 82) { action = ACTION_BLOCK; strength = 0; return true; }
    if (index == kPoolKaoriRecoilDuck) {
        action = ACTION_KAORI_RECOIL_DUCK;
        strength = 0;
        return true;
    }
    constexpr int appendedActions[7] = {ACTION_1X, ACTION_3X, ACTION_J2X,
                                        ACTION_J6X, ACTION_66X, ACTION_662X,
                                        ACTION_664X};
    const int local = index - kPool1X;
    action = appendedActions[local / 4]; strength = local & 3;
    return true;
}

constexpr bool IsPoolIndexAvailable(int charId, int index) {
    int action = 0;
    int strength = 0;
    return PoolIndexToAction(index, action, strength) &&
           IsAvailable(charId, action, strength);
}

}
