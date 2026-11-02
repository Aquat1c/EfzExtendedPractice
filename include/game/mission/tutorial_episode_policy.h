#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "core/constants.h"
#include "mission_sequence_policy.h"

namespace Mission::TutorialEpisodePolicy {

inline bool IsInjectableAction(const std::string& action) {
    static const char* const kActions[] = {
        "jump",
        "5A", "5B", "5C", "2A", "2B", "2C", "6A", "6B", "6C",
        "66A", "66B", "66C", "662A", "662B", "662C", "5A>5B>2C",
        "j.A", "j.B", "j.C",
        "236A", "236B", "236C", "623A", "623B", "623C",
        "214A", "214B", "214C",
        "22A", "22B", "22C", "41236A", "41236B", "41236C",
    };
    for (const char* known : kActions) if (action == known) return true;
    return false;
}

inline bool IsDashNormalAction(const std::string& action) {
    return action == "66A" || action == "66B" || action == "66C" ||
           action == "662A" || action == "662B" || action == "662C";
}

// 163 = forward dash, Kaori uses 250; 178 is also accepted. Backdash 164 is excluded.
inline bool IsForwardDashApproachState(short moveId) {
    return moveId == 163 || moveId == 178 || moveId == 250;
}

inline bool IsContactChainAction(const std::string& action) {
    return action == "5A>5B>2C";
}

inline bool IsSupportedScriptAction(const std::string& action,
                                    std::size_t stepIndex) {
    if (action.empty() || !IsInjectableAction(action) || action == "jump" ||
        IsContactChainAction(action)) {
        return false;
    }
    return !IsDashNormalAction(action) || stepIndex == 0;
}

inline bool IsScriptAttackInstanceEdge(short currentMove, short currentFrame,
                                       short previousMove, short previousFrame) {
    return currentMove >= 200 &&
           ::Mission::SequencePolicy::IsMoveInstanceEdge(
               currentMove, currentFrame, previousMove, previousFrame);
}

inline bool ScriptCancelDelaySatisfied(int elapsedTicks,
                                       int destinationWaitTicks,
                                       bool freezeActive) {
    return !freezeActive && elapsedTicks >= destinationWaitTicks;
}

inline bool CanInjectContactChainFollowup(bool contactFreezeSeen,
                                          bool freezeActive) {
    return contactFreezeSeen && !freezeActive;
}

inline int RecoilGuardAnswerDelayTicks(short recoilGuardMove) {
    switch (recoilGuardMove) {
        case RG_STAND_ID:  return RG_STAND_FREEZE_DURATION;
        case RG_CROUCH_ID: return RG_CROUCH_FREEZE_DURATION;
        case RG_AIR_ID:    return RG_AIR_FREEZE_DURATION;
        default:           return -1;
    }
}

inline bool RecoilGuardAnswerDelaySatisfied(short recoilGuardMove,
                                            int elapsedTicks) {
    const int required = RecoilGuardAnswerDelayTicks(recoilGuardMove);
    return required >= 0 && elapsedTicks >= required;
}

inline bool IsSupportedTrigger(const std::string& trigger) {
    return trigger == "afterBlock" || trigger == "onRG" ||
           trigger == "afterHitstun" || trigger == "afterAirtech" ||
           trigger == "onWakeup";
}

inline bool IsSupportedRepeatMode(const std::string& mode) {
    return mode == "loop" || mode == "once" || mode == "afterFailure";
}

inline bool IsSupportedApproach(const std::string& approach) {
    return approach == "dash" || approach == "none";
}

inline bool IsSupportedTelegraph(const std::string& telegraph) {
    return telegraph.empty() || telegraph == "jump";
}

// First-frame wakeup actions come from AutoAction's pre-buffer; tutorial configures and observes
// that producer but must not lease P2 control or the motion/immediate/freeze writers.
inline bool UsesNativeWakeProducer(const std::string& kind,
                                   const std::string& start,
                                   const std::string& trigger) {
    return kind == "macro" && start == "trigger" && trigger == "onWakeup";
}

inline bool NeedsTutorialWriterLeases(const std::string& kind,
                                      const std::string& start,
                                      const std::string& trigger) {
    return !UsesNativeWakeProducer(kind, start, trigger);
}

inline bool SuppressUserAutoActions(bool tutorialActive,
                                    bool tutorialWakeLeaseActive) {
    return tutorialActive && !tutorialWakeLeaseActive;
}

inline int ResolveRuntimeAutoActionTarget(bool tutorialActive,
                                          bool tutorialWakeLeaseActive,
                                          int ordinaryTarget) {
    if (tutorialWakeLeaseActive) return 2;
    if (tutorialActive) return 0;
    return ordinaryTarget;
}

inline bool ExpectedAttackMatches(const std::vector<int>& expectedMoveIds,
                                  short moveId) {
    if (moveId < 200) return false;
    if (expectedMoveIds.empty()) return true;
    for (int expected : expectedMoveIds) {
        if (expected == static_cast<int>(moveId)) return true;
    }
    return false;
}

inline bool WakeAutoActionForAction(const std::string& action,
                                    int& outAction, int& outStrength) {
    if (action.size() < 2) return false;
    const char btn = action.back();
    const int idx = btn == 'A' ? 0 : btn == 'B' ? 1 : btn == 'C' ? 2 : -1;
    if (idx < 0) return false;
    const std::string head = action.substr(0, action.size() - 1);
    outStrength = idx;
    if (head == "5")     { outAction = ACTION_5A + idx;  return true; }
    if (head == "2")     { outAction = ACTION_2A + idx;  return true; }
    if (head == "6")     { outAction = ACTION_6A + idx;  return true; }
    if (head == "4")     { outAction = ACTION_4A + idx;  return true; }
    if (head == "j.")    { outAction = ACTION_JA + idx;  return true; }
    if (head == "236")   { outAction = ACTION_QCF;       return true; }
    if (head == "623")   { outAction = ACTION_DP;        return true; }
    if (head == "214")   { outAction = ACTION_QCB;       return true; }
    if (head == "22")    { outAction = ACTION_22;        return true; }
    if (head == "41236") { outAction = ACTION_SUPER1;    return true; }
    return false;
}

// EFZ's Practice stance selector: 0 = standing, 2 = crouching (native setting;
// no synthetic P2 direction needed).
inline int FixedGuardPracticeMode(const std::string& clip) {
    return clip == "crouch" ? 2 : 0;
}

inline bool HasTimedCue(const std::string& text, int leadTicks) {
    return !text.empty() && leadTicks > 0;
}

inline bool CueStartsOnArm(const std::string& start,
                           const std::string& trigger = std::string()) {
    return start != "trigger" || trigger == "onWakeup";
}

inline bool CueStartsOnTrigger(const std::string& start, bool triggerEdge) {
    return start == "trigger" && triggerEdge;
}

inline std::vector<std::size_t> BuildBalancedVariantAssignment(
        uint32_t& seed, std::size_t memberCount, std::size_t variantCount) {
    std::vector<std::size_t> out;
    if (memberCount == 0 || variantCount == 0) return out;
    out.reserve(memberCount);
    std::vector<std::size_t> cycle(variantCount);
    for (std::size_t i = 0; i < variantCount; ++i) cycle[i] = i;
    if (seed == 0) seed = 1;
    while (out.size() < memberCount) {
        for (std::size_t i = variantCount; i > 1; --i) {
            seed = seed * 1664525u + 1013904223u;
            const std::size_t pick = (seed >> 8) % i;
            std::swap(cycle[i - 1], cycle[pick]);
        }
        for (std::size_t index : cycle) {
            if (out.size() == memberCount) break;
            out.push_back(index);
        }
    }
    return out;
}

inline bool ParseP1MovePredicate(const std::string& predicate, int& moveIdOut) {
    static const std::string kPrefix = "p1move:";
    if (predicate.rfind(kPrefix, 0) != 0 || predicate.size() == kPrefix.size()) {
        return false;
    }
    for (std::size_t i = kPrefix.size(); i < predicate.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(predicate[i]))) return false;
    }
    const long parsed = std::strtol(predicate.c_str() + kPrefix.size(), nullptr, 10);
    if (parsed < 0 || parsed > 65535) return false;
    moveIdOut = static_cast<int>(parsed);
    return true;
}

}
