#include "../include/game/collision_box_policy.h"

#include <cstdio>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

} // namespace

int main() {
    using namespace CollisionBoxPolicy;

    CHECK(!CharacterHitBoxActive(0x00, 0));
    CHECK(CharacterHitBoxActive(0x00, 1));
    CHECK(CharacterHitBoxActive(0x40, 0));
    CHECK(CharacterHitBoxActive(0x40, 1));
    CHECK(!CharacterHitBoxActive(0xBF, 0));

    CHECK(CharacterHurtBoxesShown(0x00, 0));
    CHECK(CharacterHurtBoxesShown(0xDF, 0));
    CHECK(!CharacterHurtBoxesShown(0x20, 0));
    CHECK(!CharacterHurtBoxesShown(0x00, 1));

    CHECK(ProjectileHitBoxActive(0x00, 1, 0));
    CHECK(ProjectileHitBoxActive(0xDF, 1, 0));
    CHECK(ProjectileHitBoxActive(0x00, -1, 0));
    CHECK(!ProjectileHitBoxActive(0x20, 1, 0));
    CHECK(!ProjectileHitBoxActive(0x00, 0, 0));
    CHECK(!ProjectileHitBoxActive(0x00, 1, 1));

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("collision_box_policy_tests: all checks passed\n");
    return 0;
}
