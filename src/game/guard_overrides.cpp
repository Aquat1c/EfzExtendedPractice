#include "../include/core/constants.h"
#include "../include/game/guard_overrides.h"
#include "../include/core/memory.h"

namespace GuardOverrides {

static inline bool InRange(int v, int a, int b) { return v >= a && v <= b; }

bool IsGroundedOverhead(int charId, int moveId, uintptr_t attackerBase) {
    switch (charId) {
        case CHAR_ID_MIZUKA:
            // Mizuka Nagamori: 6B
            if (moveId == 216) return true;
            break;
        case CHAR_ID_MINAGI:
            // Minagi: 66C
            if (moveId == 232) return true;
            break;
        case CHAR_ID_KAORI:
            // Kaori: 214A/B/C
            if (InRange(moveId, 253, 255)) return true;
            break;
        case CHAR_ID_MISHIO:
            // Mishio: Fire element only, Fire 623B (move 274)
            if (moveId == 274 && attackerBase) {
                int elem = 0;
                if (SafeReadMemory(attackerBase + MISHIO_ELEMENT_OFFSET, &elem, sizeof(elem))) {
                    if (elem == MISHIO_ELEM_FIRE) return true;
                }
            }
            break;
        case CHAR_ID_IKUMI:
            // Ikumi: 66C (232), and 623A/B/C (256-258)
            if (moveId == 232 || InRange(moveId, 256, 258)) return true;
            break;
        case CHAR_ID_SAYURI:
            // Sayuri: 2nd hit of 623C is high; use provided moveId 264
            if (moveId == 264) return true;
            break;
        case CHAR_ID_NANASE:
            // Rumi: 236236X without shinai (303-305), 4123641236 with shinai (308-310)
            if (InRange(moveId, 303, 305) || InRange(moveId, 308, 310)) return true;
            break;
        case CHAR_ID_MIO:
            // Mio: 236236X (303-305)
            if (InRange(moveId, 303, 305)) return true;
            break;
        case CHAR_ID_MISUZU:
            // Misuzu: 6C (moveId 211), failsafe since the move can hit if she's on the ground already.
            if (moveId == 211) return true;
            break;
        case CHAR_ID_AKIKO:
            // Akiko: 236236A-C
            if (InRange(moveId, 300, 302)) return true;
            break;
        case CHAR_ID_NAYUKIB:
            // Nayuki (awake): 662C (moveId 235)/236c(moveid 255)
            if ((moveId == 235) || (moveId == 255)) return true;
            break;
        default:
            break;
    }
    return false;
}

bool IsAirborneLow(int charId, int moveId) {
    switch (charId) {
        case CHAR_ID_MAKOTO:
            // Makoto: 236236A/B/C (moveIds 303-305) - hits low even from air
            if (InRange(moveId, 303, 305)) return true;
            break;
        default:
            break;
    }
    return false;
}

} // namespace GuardOverrides
