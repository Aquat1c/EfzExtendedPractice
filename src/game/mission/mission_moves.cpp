#include "../../../include/game/mission/mission_moves.h"
#include "../../../include/core/constants.h"

#include <unordered_map>

namespace Mission::Moves {

namespace {

struct Entry { const char* notation; bool movement; };

const std::unordered_map<int, Entry>& Table() {
    static const std::unordered_map<int, Entry> t = {
        {GROUND_IC_ID, {"IC",   false}},
        {AIR_IC_ID,    {"j.IC", false}},

        {STRAIGHT_JUMP_ID,     {"8",   true}},
        {FORWARD_JUMP_ID,      {"9",   true}},
        {BACKWARD_JUMP_ID,     {"7",   true}},
        {DOUBLE_JUMP_NEUTRAL_ID,{"dj8",true}},
        {DOUBLE_JUMP_FWD_ID,   {"dj9", true}},
        {DOUBLE_JUMP_BACK_ID,  {"dj7", true}},
        {165,                  {"j.66", true}}, // forward airdash
        {166,                  {"j.44", true}}, // backward airdash
        {FORWARD_DASH_START_ID,{"66",  true}},  // 163 forward dash (Kaori = 250)
        {164,                  {"44",  true}},  // 164 backdash (universal incl. Kaori)

        {200, {"5A", false}},
        {201, {"5B", false}},   // close 5B
        {202, {"5B", false}},   // far 5B (distinct ID, same notation)
        {203, {"5C", false}},
        {204, {"2A", false}},
        {205, {"2B", false}},
        {206, {"2C", false}},

        {207, {"j.A", false}},
        {208, {"j.B", false}},
        {209, {"j.C", false}},
    };
    return t;
}

}

const char* Notation(int moveId) {
    const auto& t = Table();
    auto it = t.find(moveId);
    return it != t.end() ? it->second.notation : "";
}

bool IsKnownComboMove(int moveId) { return Table().count(moveId) != 0; }

bool IsMovementMove(int moveId) {
    const auto& t = Table();
    auto it = t.find(moveId);
    return it != t.end() && it->second.movement;
}

}
