#pragma once

#include <string>

namespace Mission::TutorialStatePolicy {

// `distance` is |P1.x - P2.x| from XPOS doubles (+0x20); `player` is ignored. `ic` is latch +0x120
// (0 = red, nonzero = blue), exposed as 0/1.
inline bool IsCoherentField(const std::string& field) {
    return field == "sp" || field == "rf" || field == "guard" ||
           field == "hp" || field == "distance" || field == "ic";
}

// `untech` is the signed 16-bit counter at +0x124. A zero counter does not mean "can recover":
// EFZ has additional recovery gates.
inline bool IsJuggleField(const std::string& field) {
    return field == "untech" || field == "launched" || field == "airtech" ||
           field == "downed";
}

inline bool IsKnownField(const std::string& field) {
    return IsCoherentField(field) || IsJuggleField(field);
}

inline bool IsKnownOperator(const std::string& op) {
    return op == "ge" || op == "le" || op == "gt" || op == "lt" ||
           op == "eq";
}

inline bool Compare(double observed, const std::string& op, double target) {
    if (op == "ge") return observed >= target;
    if (op == "le") return observed <= target;
    if (op == "gt") return observed > target;
    if (op == "lt") return observed < target;
    if (op == "eq") return observed == target;
    return false;
}

inline bool IsUntechActive(double untech) { return untech > 0.0; }
inline bool IsUntechEmpty(double untech) { return untech <= 0.0; }

inline bool IsWakeExit(bool wasGroundtech, bool isGroundtech) {
    return wasGroundtech && !isGroundtech;
}

inline bool IsWithinWakeWindow(int elapsedTicks, int maxTicks) {
    return maxTicks > 0 && elapsedTicks >= 0 && elapsedTicks <= maxTicks;
}

inline int BranchPhaseForOutcomeEdge(bool hitEdge, bool blockEdge) {
    return hitEdge ? 1 : blockEdge ? 2 : 3;
}

}
