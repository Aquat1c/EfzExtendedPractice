#pragma once
// Nanase/Ikumi runtime tables can swap without changing character ID; GetClass is merged
// metadata, not proof of the table active on this tick.
#include <vector>

namespace Mission::MoveData {

enum class Cls {
    Unknown, System, A, B, C, Command, Special, Super, Projectile, SuperEntity, Entity
};

namespace Detail {

constexpr int MergePriority(Cls c, int id) {
    if (id >= 400) {
        return c == Cls::SuperEntity ? 4
             : c == Cls::Projectile  ? 3
             : c == Cls::Entity      ? 2
             : c == Cls::Unknown     ? 0 : 1;
    }
    return c == Cls::Super   ? 7
         : c == Cls::Special ? 6
         : c == Cls::Command ? 5
         : c == Cls::C       ? 4
         : c == Cls::B       ? 3
         : c == Cls::A       ? 2
         : c == Cls::System  ? 1 : 0;
}

constexpr Cls MergeProfileClass(Cls existing, Cls incoming, int id) {
    return MergePriority(incoming, id) > MergePriority(existing, id)
        ? incoming : existing;
}

constexpr bool MergeProfileAttack(bool existing, bool incoming) {
    return existing || incoming;
}

constexpr bool IsCommonSystemHelperCandidateId(int moveId) {
    return moveId >= 495 && moveId <= 499;
}

constexpr bool LifecycleRequiresGrade(Cls cls, bool attack, int moveId) {
    return attack || cls == Cls::Projectile || cls == Cls::SuperEntity ||
           (cls == Cls::Unknown && moveId >= 400 &&
            !IsCommonSystemHelperCandidateId(moveId));
}

static_assert(MergeProfileClass(Cls::Super, Cls::System, 301) == Cls::Super,
              "alternate union must retain special/super capture");
static_assert(MergeProfileClass(Cls::Projectile, Cls::Entity, 421) == Cls::Projectile,
              "alternate union must retain attack-capable entity class");
static_assert(LifecycleRequiresGrade(Cls::Unknown, false, 405),
              "unmapped combat-ring patterns must not silently become motion-only");
static_assert(LifecycleRequiresGrade(Cls::Projectile, false, 405),
              "projectile class remains conservative without an attack bit");
static_assert(!LifecycleRequiresGrade(Cls::Entity, false, 405),
              "known inert helpers may remain lifecycle-only setup");
static_assert(!LifecycleRequiresGrade(Cls::Unknown, false, 497),
              "shared system helpers retain their presentation-only exception");

}

bool EnsureLoaded(int charId);

Cls         GetClass(int charId, int moveId);
const char* ClassName(Cls c);
bool        IsAttack(int charId, int moveId);
bool        IsSpecialOrSuper(int charId, int moveId);
// Metadata query only; never gate ring-event eligibility on it (unknown >=400 must still be captured).
bool        IsKnownEntityPattern(int charId, int moveId);
bool        IsCommonSystemHelperCandidate(int moveId);

const std::vector<int>& Specials(int charId);
const std::vector<int>& EntityPatterns(int charId);
const std::vector<int>& AttackEntityPatterns(int charId);

}
