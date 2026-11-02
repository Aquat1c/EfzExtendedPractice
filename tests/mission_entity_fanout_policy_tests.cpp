#include "game/mission/mission_entity_fanout_policy.h"

#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

}

int main() {
    using namespace Mission::EntityFanoutPolicy;

    Check(IsFlexibleFanout("shiori", 435, 312),
          "ground/air A producer is curated");
    Check(IsFlexibleFanout("SHIORI", 435, 317),
          "resource matching is ASCII case-insensitive");
    Check(!IsFlexibleFanout("shiori", 436, 312),
          "non-attacking companion child is excluded");
    Check(!IsFlexibleFanout("shiori", 435, 311) &&
              !IsFlexibleFanout("shiori", 435, 318),
          "only the six 2141236 producer move IDs are admitted");
    Check(!IsFlexibleFanout("sayuri", 435, 312),
          "same numeric pattern on another resource is not relaxed");

    Check(!IsFlexibleFanout("nanase", 405, 261) &&
              !IsFlexibleFanout("nanase2", 408, 261),
          "Rumi volley children stay strict until volley identity is recorded");

    Check(!IsFlexibleFanout("akane", 406, 300) &&
              !IsFlexibleFanout("mizukab", 409, 255) &&
              !IsFlexibleFanout("nayukib", 404, 306),
          "unrelated morph loops and persistent summons remain strict");

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "mission entity fanout policy tests passed\n";
    return 0;
}
