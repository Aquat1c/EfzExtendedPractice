#pragma once

#include "game/mission/entity_notation_tables.h"
#include "game/mission/mission_data.h"
#include "game/mission/mission_entity_lifecycle_policy.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace Mission::EntityReviewPolicy {

// Authoring evidence, not a runtime contract: defaults reject promotion so a
// partial/older sidecar cannot silently make a recording playable.
struct LegacyLifecycleMigrationEvidence {
    bool metadataSeen = false;
    int schemaVersion = 0;
    int slotCapacity = 0;
    bool exactSavestate = false;
    bool entityContactHookComplete = false;
    bool contactEpochDiscontinuity = true;
    bool contactJournalOverflow = true;
    int droppedEvents = -1;
    bool p1EntityProbeIncomplete = true;
    bool p1AllocationCursorAmbiguous = true;
    bool requiresEntityAttributionReview = true;
    bool containsUnclassifiedContact = true;
    bool usedLegacyContactAttribution = true;
    std::string metadataP1Resource;

    bool sampled = false;
    std::string eventResource;
    int owner = 0;
    int slot = -1;
    int generation = 0;
    EntitySchedulePolicy::LifecycleKind kind =
        EntitySchedulePolicy::LifecycleKind::None;
    int pattern = -1;
    int priorPattern = -1;
    int opensAfterAction = -1;
    int authoredActionCount = 0;
    int actionStartEffectiveFrame = -1;
    int lifecycleEffectiveFrame = -1;
    bool attack = false;
    bool semanticPromisesContact = false;
    bool hasLinkedCommittedContact = true;
};

constexpr int kLifecycleMigrationSidecarSchemaVersion = 5;

inline bool CanPromoteLegacyUngradedLifecycle(
    const std::string& missionP1Resource,
    int reviewPattern, int reviewSlot, int reviewGeneration,
    const LegacyLifecycleMigrationEvidence& evidence) {
    if (!evidence.metadataSeen ||
        evidence.schemaVersion != kLifecycleMigrationSidecarSchemaVersion ||
        evidence.slotCapacity <= 0 || !evidence.exactSavestate ||
        !evidence.entityContactHookComplete ||
        evidence.contactEpochDiscontinuity ||
        evidence.contactJournalOverflow || evidence.droppedEvents != 0 ||
        evidence.p1EntityProbeIncomplete ||
        evidence.p1AllocationCursorAmbiguous ||
        evidence.containsUnclassifiedContact ||
        evidence.usedLegacyContactAttribution) {
        return false;
    }
    if (missionP1Resource.empty() ||
        evidence.metadataP1Resource != missionP1Resource ||
        evidence.eventResource != missionP1Resource ||
        evidence.owner != 1 || !evidence.sampled || !evidence.attack ||
        !evidence.semanticPromisesContact ||
        evidence.hasLinkedCommittedContact) {
        return false;
    }
    if (reviewPattern < 0 || reviewSlot < 0 || reviewGeneration <= 0 ||
        evidence.pattern != reviewPattern || evidence.slot != reviewSlot ||
        evidence.generation != reviewGeneration ||
        evidence.slot >= evidence.slotCapacity) {
        return false;
    }
    if ((evidence.kind != EntitySchedulePolicy::LifecycleKind::Spawn &&
         evidence.kind != EntitySchedulePolicy::LifecycleKind::Morph) ||
        evidence.opensAfterAction < 0 ||
        evidence.authoredActionCount <= 0 ||
        evidence.opensAfterAction >= evidence.authoredActionCount ||
        evidence.actionStartEffectiveFrame < 0 ||
        evidence.lifecycleEffectiveFrame <
            evidence.actionStartEffectiveFrame) {
        return false;
    }
    return EntityLifecyclePolicy::ValidIdentity(
        evidence.slot, evidence.generation, evidence.kind,
        evidence.pattern, evidence.priorPattern,
        evidence.opensAfterAction);
}

inline bool ParseLegacyUngradedLifecycleReason(const std::string& reason,
                                               int& pattern, int& slot,
                                               int& generation) {
    const std::size_t hash = reason.find(" entity setup #");
    const std::size_t slotAt = reason.find(" (slot ");
    const std::size_t generationAt = reason.find(", generation ");
    if (hash == std::string::npos || slotAt == std::string::npos ||
        generationAt == std::string::npos || hash + 15 >= slotAt) {
        return false;
    }
    const std::size_t patternStart = hash + 15;
    const std::size_t slotStart = slotAt + 7;
    const std::size_t generationStart = generationAt + 13;
    try {
        pattern = std::stoi(reason.substr(patternStart,
                                          slotAt - patternStart));
        slot = std::stoi(reason.substr(slotStart,
                                      generationAt - slotStart));
        generation = std::stoi(reason.substr(generationStart));
    } catch (...) {
        return false;
    }
    return pattern >= 0 && slot >= 0 && generation > 0;
}

inline bool IsObsoletePostContactReview(const ::Mission::Mission& mission,
                                        const std::string& reason) {
    int pattern = -1;
    int slot = -1;
    int generation = 0;
    if (!ParseLegacyUngradedLifecycleReason(
            reason, pattern, slot, generation)) {
        return false;
    }
    const auto* recovery = ::Mission::EntityNames::LookupSemantic(
        mission.player.character.c_str(), pattern);
    if (!recovery || !::Mission::EntityNames::IsPostContactRecovery(
                         recovery->disposition)) {
        return false;
    }
    for (const auto& requirement : mission.entityContacts) {
        if (requirement.slot != slot ||
            requirement.generation != generation) {
            continue;
        }
        const int producerMove =
            requirement.opensAfterAction >= 0 &&
            requirement.opensAfterAction <
                static_cast<int>(mission.steps.size()) &&
            !mission.steps[static_cast<std::size_t>(
                 requirement.opensAfterAction)].moveIds.empty()
                ? mission.steps[static_cast<std::size_t>(
                      requirement.opensAfterAction)].moveIds.front()
                : -1;
        for (int contactPattern : requirement.patterns) {
            const auto* contact = ::Mission::EntityNames::LookupSemantic(
                mission.player.character.c_str(), contactPattern,
                producerMove);
            if (contact &&
                ::Mission::EntityNames::LifecyclePromisesContact(
                    contact->disposition) &&
                std::strcmp(contact->family, recovery->family) == 0) {
                return true;
            }
        }
    }
    return false;
}

}
