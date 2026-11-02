#pragma once

#include <cctype>
#include <string>

// Curated exceptions only; persistent traps, summons, deterministic multi-hit
// phases, and successive casts must retain their exact producer obligations.
namespace Mission::EntityFanoutPolicy {

inline bool EqualsAsciiCaseInsensitive(const std::string& left,
                                       const char* right) {
    if (!right) return false;
    std::size_t index = 0;
    for (; index < left.size() && right[index] != '\0'; ++index) {
        const unsigned char leftChar =
            static_cast<unsigned char>(left[index]);
        const unsigned char rightChar =
            static_cast<unsigned char>(right[index]);
        if (std::tolower(leftChar) != std::tolower(rightChar)) return false;
    }
    return index == left.size() && right[index] == '\0';
}

constexpr bool IsShiori2141236ProducerMove(int moveId) {
    return moveId >= 312 && moveId <= 317;
}

constexpr bool IsShiori2141236AttackChild(int pattern) {
    return pattern == 435;
}

inline bool IsFlexibleFanout(const std::string& resourceName,
                             int attackChildPattern,
                             int producerMoveId) {
    return EqualsAsciiCaseInsensitive(resourceName, "shiori") &&
           IsShiori2141236AttackChild(attackChildPattern) &&
           IsShiori2141236ProducerMove(producerMoveId);
}

}
