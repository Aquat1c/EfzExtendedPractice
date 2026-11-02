#pragma once

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

// Presentation-only policy: may hide or fold rows, but must never alter what
// the runner grades.
namespace Mission::EntityPresentationPolicy {

struct Timing {
    int opensAfterAction = -1;
    int contactAfterAction = -1;
    int afterStep = -1;
    int afterStepContact = -1;
    int dueBeforeStep = -1;
    int dueBeforeStepContact = -1;
    bool comboEndAfter = false;
};

struct ExactProducerHit {
    int action = -1;
    bool exactSource = false;
    bool generated = false;
    bool resolvedContact = false;
    bool actionEligible = false;
    bool commandSource = false;
    bool ordinaryProjectile = false;
    bool producerMoveMatches = false;
    bool sourceSegmentMatches = true;
    bool sourceComboEndBeforeContact = false;
};

constexpr bool IsStructurallyStandalone(const Timing& timing) {
    if (timing.comboEndAfter) return true;
    if (timing.opensAfterAction < 0) return true;
    if (timing.contactAfterAction > timing.opensAfterAction) return true;
    if (timing.afterStep > timing.opensAfterAction) return true;
    if (timing.afterStepContact > 0) return true;
    return timing.dueBeforeStep > timing.opensAfterAction + 1;
}

constexpr bool CanInlineExactProducerHit(const ExactProducerHit& candidate,
                                         const Timing& timing) {
    if (candidate.action < 0 || !candidate.exactSource ||
        !candidate.generated ||
        !candidate.resolvedContact || !candidate.actionEligible ||
        !candidate.sourceSegmentMatches ||
        candidate.sourceComboEndBeforeContact ||
        (!candidate.commandSource && !candidate.ordinaryProjectile &&
         !candidate.producerMoveMatches)) {
        return false;
    }
    if (timing.opensAfterAction != candidate.action ||
        timing.contactAfterAction != candidate.action) {
        return false;
    }
    if (timing.afterStep > candidate.action ||
        (timing.afterStepContact > 0 &&
         timing.afterStep != candidate.action)) {
        return false;
    }
    if (timing.dueBeforeStep >= 0 &&
        timing.dueBeforeStep != candidate.action &&
        timing.dueBeforeStep != candidate.action + 1) {
        return false;
    }
    if (timing.dueBeforeStep == candidate.action &&
        timing.dueBeforeStepContact <= 0) {
        return false;
    }
    if (timing.dueBeforeStep == candidate.action + 1 &&
        timing.dueBeforeStepContact > 0) {
        return false;
    }
    Timing producerTiming = timing;
    if (producerTiming.afterStep == candidate.action) {
        producerTiming.afterStepContact = -1;
    }
    producerTiming.comboEndAfter = false;
    return !IsStructurallyStandalone(producerTiming);
}

constexpr bool PersistedSemanticMatchesSource(bool hasPersistedSemantic,
                                              bool exactSource,
                                              int persistedProducerMove,
                                              int sourceMove) {
    return hasPersistedSemantic &&
        (!exactSource || persistedProducerMove == sourceMove);
}

constexpr int SelectOwnerAction(bool exactSource, bool legacySource,
                                int semanticAction, int lifecycleAction,
                                int totalActions) {
    if (exactSource && semanticAction >= 0 &&
        semanticAction < totalActions) {
        return semanticAction;
    }
    if (legacySource && lifecycleAction >= 0 &&
        lifecycleAction < totalActions) {
        return lifecycleAction;
    }
    return -1;
}

constexpr bool ShouldDisplay(bool mapped, bool alwaysStandalone,
                             bool failed, const Timing& timing) {
    (void)failed;
    return !mapped || alwaysStandalone || IsStructurallyStandalone(timing);
}

constexpr bool ShouldDisplay(bool mapped, bool alwaysStandalone,
                             bool failed, const Timing& timing,
                             bool inlineExactProducerHit) {
    (void)mapped;
    (void)alwaysStandalone;
    (void)failed;
    (void)timing;
    return !inlineExactProducerHit;
}

struct Identity {
    const char* family = nullptr;
    int owner = 0;
    int target = 0;
    int segment = 0;
    int slot = -1;
    int generation = 0;
    int producerAction = -1;
    int contactAfterAction = -1;
    int afterStep = -1;
    int afterStepContact = -1;
    int dueStep = -1;
    int dueContact = -1;
};

constexpr bool SameInstance(const Identity& left, const Identity& right) {
    return left.slot >= 0 && right.slot >= 0 &&
           left.generation > 0 && right.generation > 0 &&
           left.slot == right.slot && left.generation == right.generation;
}

constexpr bool SameProducerEpisode(const Identity& left,
                                   const Identity& right) {
    return left.producerAction >= 0 &&
           left.producerAction == right.producerAction;
}

inline bool CanFold(const Identity& left, const Identity& right,
                    const char* leftResult, const char* rightResult) {
    if (!left.family || !right.family ||
        std::strcmp(left.family, right.family) != 0) {
        return false;
    }
    if (!leftResult || !rightResult ||
        std::strcmp(leftResult, rightResult) != 0) {
        return false;
    }
    return left.owner == right.owner && left.target == right.target &&
           left.segment == right.segment &&
           left.contactAfterAction == right.contactAfterAction &&
           left.afterStep == right.afterStep &&
           left.afterStepContact == right.afterStepContact &&
           left.dueStep == right.dueStep &&
           left.dueContact == right.dueContact &&
           (SameInstance(left, right) || SameProducerEpisode(left, right));
}

constexpr bool AllSatisfied(int satisfiedMembers, int memberCount) {
    return memberCount > 0 && satisfiedMembers == memberCount;
}

struct RequirementCounts {
    int recordedContacts = 0;
    int recordedComboHits = 0;
    int minimumContacts = 0;
    int minimumComboHits = 0;
    bool fanout = false;
};

constexpr int RequiredContacts(const RequirementCounts& counts) {
    return counts.fanout ? counts.minimumContacts : counts.recordedContacts;
}

constexpr int RequiredComboHits(const RequirementCounts& counts) {
    return counts.fanout ? counts.minimumComboHits : counts.recordedComboHits;
}

constexpr bool RequirementSatisfied(const RequirementCounts& counts,
                                    int contactsSeen, int comboHitsSeen) {
    return contactsSeen >= RequiredContacts(counts) &&
           comboHitsSeen >= RequiredComboHits(counts);
}

constexpr int CompactRequiredCount(int contactsRequired,
                                   int comboHitsRequired) {
    return contactsRequired > comboHitsRequired
        ? contactsRequired : comboHitsRequired;
}

inline std::string CompactCountSuffix(int contactsRequired,
                                      int comboHitsRequired) {
    const int required = CompactRequiredCount(contactsRequired,
                                              comboHitsRequired);
    if (required <= 1) return {};
    return " x" + std::to_string(required);
}

inline std::string CompactCountSuffix(const RequirementCounts& counts) {
    return CompactCountSuffix(RequiredContacts(counts),
                              RequiredComboHits(counts));
}

inline int DisplayAnchor(int contactAfterAction, int dueBeforeStep,
                         int afterStep, int totalActions) {
    if (totalActions < 0) return 0;
    if (dueBeforeStep >= 0 && dueBeforeStep < totalActions) {
        return dueBeforeStep;
    }
    if (contactAfterAction >= 0) {
        return (std::min)(contactAfterAction + 1, totalActions);
    }
    if (afterStep >= 0) {
        return (std::min)(afterStep + 1, totalActions);
    }
    return totalActions;
}

inline std::string FriendlyOutcomeLabel(const std::string& label,
                                        const std::string& outcome) {
    const std::string technical = " (" + outcome + ")";
    if (label.size() < technical.size() ||
        label.compare(label.size() - technical.size(), technical.size(),
                      technical) != 0) {
        return label;
    }
    std::string friendly = outcome;
    for (char& ch : friendly) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    std::string result = label;
    result.replace(result.size() - technical.size(), technical.size(),
                   " (" + friendly + ")");
    return result;
}

}
