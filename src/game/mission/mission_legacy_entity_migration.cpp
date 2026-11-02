#include "../../../include/game/mission/mission_legacy_entity_migration.h"

#include "../../../include/game/collision_display.h"
#include "../../../include/game/mission/entity_notation_tables.h"
#include "../../../include/game/mission/mission_entity_review_policy.h"
#include "../../../include/game/mission/recorder_entity_trace.h"
#include "../../../include/game/mission/snowbunny_transition_policy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Mission::LegacyEntityMigration {
namespace {

using json = nlohmann::json;
using LifecycleKind = ::Mission::EntitySchedulePolicy::LifecycleKind;

constexpr const char* kMarkerV1 = "runtime:entity-contact-schedule-v1";
constexpr const char* kMarkerV2 = "runtime:entity-contact-schedule-v2";
constexpr const char* kMarkerV3 = "runtime:entity-contact-schedule-v3";
constexpr const char* kMarkerV4 = "runtime:entity-lifecycle-schedule-v4";
constexpr const char* kMarkerV5 = "runtime:entity-fanout-schedule-v5";
constexpr const char* kSnowbunnyProducerMismatchReview =
    "entity contacts need review: entity contact pattern has no "
    "contact-linked lifecycle transition";
constexpr const char* kLegacySuffix =
    " had no gradeable contact; author a lifecycle objective or retake it";
constexpr std::size_t kMaxAuthoredActions =
    ::Mission::RecorderEntityTrace::kTraceEventCapacity;
constexpr std::size_t kMaxJsonlRowBytes = 64u * 1024u;
constexpr std::uint64_t kMaxSidecarBytes = 16u * 1024u * 1024u;

struct Metadata {
    bool seen = false;
    int version = 0;
    int slotCapacity = 0;
    int playersSampled = 0;
    int eventCount = -1;
    int droppedEvents = -1;
    std::string p1Resource;
    std::string p2Resource;
    bool entityContactHookObserved = false;
    bool entityContactHookComplete = false;
    bool contactEpochDiscontinuity = true;
    bool directContactHookObserved = false;
    bool contactJournalOverflow = true;
    bool p1EntityProbeIncomplete = true;
    bool p2EntityProbeIncomplete = true;
    bool p1AllocationCursorAmbiguous = true;
    bool p2AllocationCursorAmbiguous = true;
    bool requiresEntityAttributionReview = false;
    bool containsUnclassifiedContact = true;
    bool usedLegacyContactAttribution = true;
};

struct ActionEvidence {
    bool seen = false;
    int move = -1;
    int expectedAttackMask = -1;
    int effectiveFrame = -1;
    int actionOrder = -1;
    int battleBatch = -1;
};

struct LifecycleEvidence {
    std::size_t row = 0;
    std::string event;
    std::string resource;
    int player = 0;
    int slot = -1;
    int generation = 0;
    int pattern = -1;
    int priorPattern = -1;
    int effectiveFrame = -1;
    int observedAfterStep = -1;
    int observedAfterActionOrder = -1;
    bool attack = false;
};

struct ContactEvidence {
    std::size_t row = 0;
    std::string resource;
    std::string source;
    std::string result;
    int player = 0;
    int defender = 0;
    int slot = -1;
    int generation = 0;
    int pattern = -1;
    int effectiveFrame = -1;
    int observedAfterStep = -1;
    int observedAfterActionOrder = -1;
    int sequence = -1;
    int battleBatch = -1;
    int attributedStep = -2;
    int attackerMove = -1;
    int comboBefore = -1;
    int comboAfter = -1;
    int hpBefore = -1;
    int hpAfter = -1;
    bool comboEndAfter = false;
};

struct LegacyReview {
    std::string reason;
    int pattern = -1;
    int slot = -1;
    int generation = 0;
};

struct PromotedObjective {
    EntityLifecycleRequirement requirement;
    int effectiveFrame = -1;
};

LifecycleKind KindForEvent(const std::string& event);

Report Reject(const std::string& diagnostic) {
    Report report;
    report.result = Result::EvidenceRejected;
    report.diagnostic = diagnostic;
    return report;
}

bool IsRecordedMissionPath(const std::string& sourcePath) {
    std::string normalized = sourcePath;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    const std::size_t directory = normalized.find("\\_recorded\\");
    if (directory == std::string::npos) return false;
    const std::size_t basename = normalized.find_last_of('\\');
    if (basename == std::string::npos) return false;
    return normalized.compare(basename + 1, 9, "recorded_") == 0;
}

std::string SidecarPath(const std::string& sourcePath) {
    const std::size_t dot = sourcePath.find_last_of('.');
    if (dot == std::string::npos) return {};
    return sourcePath.substr(0, dot) + ".entities.jsonl";
}

bool ExactBool(const json& value, const char* key, bool expected) {
    const auto it = value.find(key);
    return it != value.end() && it->is_boolean() &&
           it->get<bool>() == expected;
}

bool ReadInt(const json& value, const char* key, int& out) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer()) return false;
    if (it->is_number_unsigned()) {
        const auto number = it->get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(
                         (std::numeric_limits<int>::max)())) {
            return false;
        }
        out = static_cast<int>(number);
        return true;
    }
    const auto number = it->get<std::int64_t>();
    if (number < (std::numeric_limits<int>::min)() ||
        number > (std::numeric_limits<int>::max)()) {
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

bool ReadNullableInt(const json& value, const char* key, int& out) {
    const auto it = value.find(key);
    if (it == value.end()) return false;
    if (it->is_null()) {
        out = -1;
        return true;
    }
    return ReadInt(value, key, out);
}

bool ReadString(const json& value, const char* key, std::string& out) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

bool ReadOptionalInt(const json& value, const char* key, int& out) {
    const auto it = value.find(key);
    return it == value.end() || ReadInt(value, key, out);
}

bool IsCommittedResult(const std::string& result) {
    return result == "hit" || result == "block" ||
           result == "recoil_guard" || result == "throw" ||
           result == "special_hit" || result == "guard_point";
}

bool ResultMatchesRequirement(const std::string& requirement,
                              const std::string& observed) {
    if (requirement == "hit") {
        return observed == "hit" || observed == "special_hit";
    }
    if (requirement == "special") return observed == "special_hit";
    return requirement == observed;
}

std::string ExactLegacyReason(int pattern, int slot, int generation) {
    return "attack entity setup #" + std::to_string(pattern) +
           " (slot " + std::to_string(slot) + ", generation " +
           std::to_string(generation) + ")" + kLegacySuffix;
}

bool ParseExactLegacyReason(const std::string& reason, LegacyReview& out) {
    if (!::Mission::EntityReviewPolicy::ParseLegacyUngradedLifecycleReason(
            reason, out.pattern, out.slot, out.generation)) {
        return false;
    }
    if (reason != ExactLegacyReason(out.pattern, out.slot, out.generation)) {
        return false;
    }
    out.reason = reason;
    return true;
}

bool ReadMetadata(const json& value, Metadata& out) {
    std::string schema;
    std::string frameUnit;
    if (out.seen || !ReadString(value, "schema", schema) ||
        schema != "efz_recorder_entity_trace" ||
        !ReadInt(value, "version", out.version) ||
        !ReadInt(value, "slotCapacity", out.slotCapacity) ||
        !ReadInt(value, "playersSampled", out.playersSampled) ||
        !ReadInt(value, "eventCount", out.eventCount) ||
        !ReadInt(value, "droppedEvents", out.droppedEvents) ||
        !ReadString(value, "effectiveFrameUnit", frameUnit) ||
        frameUnit != "freeze-excluded EFZ internal ticks" ||
        !ReadString(value, "p1Resource", out.p1Resource) ||
        !ReadString(value, "p2Resource", out.p2Resource) ||
        !ExactBool(value, "authoring_only", true) ||
        !ExactBool(value, "sampled", true) ||
        !ExactBool(value, "unordered", true) ||
        !ExactBool(value, "non_strict", true)) {
        return false;
    }
    auto readFlag = [&value](const char* key, bool& target) {
        const auto it = value.find(key);
        if (it == value.end() || !it->is_boolean()) return false;
        target = it->get<bool>();
        return true;
    };
    if (!readFlag("entityContactHookObserved",
                  out.entityContactHookObserved) ||
        !readFlag("entityContactHookComplete",
                  out.entityContactHookComplete) ||
        !readFlag("contactEpochDiscontinuity",
                  out.contactEpochDiscontinuity) ||
        !readFlag("directContactHookObserved",
                  out.directContactHookObserved) ||
        !readFlag("contactJournalOverflow",
                  out.contactJournalOverflow) ||
        !readFlag("p1EntityProbeIncomplete",
                  out.p1EntityProbeIncomplete) ||
        !readFlag("p2EntityProbeIncomplete",
                  out.p2EntityProbeIncomplete) ||
        !readFlag("p1AllocationCursorAmbiguous",
                  out.p1AllocationCursorAmbiguous) ||
        !readFlag("p2AllocationCursorAmbiguous",
                  out.p2AllocationCursorAmbiguous) ||
        !readFlag("requiresEntityAttributionReview",
                  out.requiresEntityAttributionReview) ||
        !readFlag("containsUnclassifiedContact",
                  out.containsUnclassifiedContact) ||
        !readFlag("usedLegacyContactAttribution",
                  out.usedLegacyContactAttribution)) {
        return false;
    }
    int ignoredCharacter = -1;
    if (!ReadInt(value, "p1CharacterId", ignoredCharacter) ||
        ignoredCharacter < 0 ||
        !ReadInt(value, "p2CharacterId", ignoredCharacter) ||
        ignoredCharacter < 0) {
        return false;
    }
    out.seen = true;
    return true;
}

bool ReadAction(const json& value, std::vector<ActionEvidence>& actions) {
    int step = -1;
    ActionEvidence parsed;
    if (!ReadInt(value, "step", step) || step < 0 ||
        step >= static_cast<int>(actions.size()) ||
        actions[static_cast<std::size_t>(step)].seen ||
        !ReadInt(value, "move", parsed.move) ||
        !ReadInt(value, "expectedAttackMask", parsed.expectedAttackMask) ||
        !ReadInt(value, "effectiveFrame", parsed.effectiveFrame) ||
        !ReadInt(value, "actionOrder", parsed.actionOrder) ||
        !ReadOptionalInt(value, "battleBatch", parsed.battleBatch) ||
        parsed.effectiveFrame < 0) {
        return false;
    }
    parsed.seen = true;
    actions[static_cast<std::size_t>(step)] = parsed;
    return true;
}

bool ReadLifecycle(const json& value, std::size_t row,
                   LifecycleEvidence& out) {
    std::string evidence;
    out.row = row;
    if (!ReadString(value, "event", out.event) ||
        !ReadString(value, "evidence", evidence) ||
        evidence != "ring_sample" ||
        !ReadString(value, "resource", out.resource) ||
        !ReadInt(value, "player", out.player) ||
        !ReadNullableInt(value, "slot", out.slot) ||
        !ReadInt(value, "generation", out.generation) ||
        !ReadNullableInt(value, "pattern", out.pattern) ||
        !ReadNullableInt(value, "priorPattern", out.priorPattern) ||
        !ReadInt(value, "effectiveFrame", out.effectiveFrame) ||
        !ReadInt(value, "observedAfterStep", out.observedAfterStep) ||
        !ReadInt(value, "observedAfterActionOrder",
                 out.observedAfterActionOrder) ||
        !ExactBool(value, "authoring_only", true) ||
        !ExactBool(value, "sampled", true) ||
        !ExactBool(value, "unordered", true) ||
        !ExactBool(value, "non_strict", true)) {
        return false;
    }
    const auto attack = value.find("attack");
    if (attack == value.end() || !attack->is_boolean()) return false;
    out.attack = attack->get<bool>();
    return out.effectiveFrame >= 0 && KindForEvent(out.event) !=
        LifecycleKind::None;
}

bool ReadContact(const json& value, std::size_t row,
                 ContactEvidence& out) {
    std::string evidence;
    out.row = row;
    if (!ReadString(value, "evidence", evidence) ||
        evidence != "resolver_hook" ||
        !ReadString(value, "resource", out.resource) ||
        !ReadString(value, "source", out.source) ||
        !ReadString(value, "result", out.result) ||
        !ReadInt(value, "player", out.player) ||
        !ReadInt(value, "defender", out.defender) ||
        !ReadNullableInt(value, "slot", out.slot) ||
        !ReadInt(value, "generation", out.generation) ||
        !ReadNullableInt(value, "pattern", out.pattern) ||
        !ReadInt(value, "effectiveFrame", out.effectiveFrame) ||
        !ReadInt(value, "observedAfterStep", out.observedAfterStep) ||
        !ReadInt(value, "observedAfterActionOrder",
                 out.observedAfterActionOrder) ||
        !ReadOptionalInt(value, "sequence", out.sequence) ||
        !ReadOptionalInt(value, "batch", out.battleBatch) ||
        !ReadOptionalInt(value, "attributedStep", out.attributedStep) ||
        !ReadOptionalInt(value, "attackerMove", out.attackerMove) ||
        !ReadOptionalInt(value, "comboBefore", out.comboBefore) ||
        !ReadOptionalInt(value, "comboAfter", out.comboAfter) ||
        !ReadOptionalInt(value, "hpBefore", out.hpBefore) ||
        !ReadOptionalInt(value, "hpAfter", out.hpAfter) ||
        !ExactBool(value, "authoring_only", true) ||
        !ExactBool(value, "sampled", false) ||
        !ExactBool(value, "unordered", false) ||
        !ExactBool(value, "non_strict", true)) {
        return false;
    }
    const auto comboEnd = value.find("comboEndAfter");
    if (comboEnd == value.end() || !comboEnd->is_boolean()) return false;
    out.comboEndAfter = comboEnd->get<bool>();
    return out.effectiveFrame >= 0 && IsCommittedResult(out.result) &&
           (out.source == "direct" || out.source == "entity") &&
           (out.player == 1 || out.player == 2) &&
           (out.defender == 1 || out.defender == 2);
}

bool MetadataIsReliable(const Metadata& metadata, const Mission& mission) {
    return metadata.seen &&
           metadata.version ==
               ::Mission::EntityReviewPolicy::
                   kLifecycleMigrationSidecarSchemaVersion &&
           metadata.slotCapacity == static_cast<int>(
               CollisionDisplay::kProjectileRingSlotCapacity) &&
           metadata.playersSampled == 2 && metadata.eventCount >= 0 &&
           metadata.droppedEvents == 0 &&
           metadata.p1Resource == mission.player.character &&
           metadata.p2Resource == mission.dummy.character &&
           metadata.entityContactHookObserved &&
           metadata.entityContactHookComplete &&
           !metadata.contactEpochDiscontinuity &&
           metadata.directContactHookObserved &&
           !metadata.contactJournalOverflow &&
           !metadata.p1EntityProbeIncomplete &&
           !metadata.p2EntityProbeIncomplete &&
           !metadata.p1AllocationCursorAmbiguous &&
           !metadata.p2AllocationCursorAmbiguous &&
           !metadata.containsUnclassifiedContact &&
           !metadata.usedLegacyContactAttribution;
}

LifecycleKind KindForEvent(const std::string& event) {
    if (event == "spawn") return LifecycleKind::Spawn;
    if (event == "morph") return LifecycleKind::Morph;
    if (event == "baseline") return LifecycleKind::Baseline;
    if (event == "despawn") return LifecycleKind::Despawn;
    return LifecycleKind::None;
}

bool SameInstance(const LifecycleEvidence& left,
                  const LifecycleEvidence& right) {
    return left.player == right.player && left.slot == right.slot &&
           left.generation == right.generation;
}

const LifecycleEvidence* FindContactProducer(
    const ContactEvidence& contact,
    const std::vector<LifecycleEvidence>& lifecycles,
    bool& ambiguous) {
    ambiguous = false;
    const LifecycleEvidence* producer = nullptr;
    for (const LifecycleEvidence& candidate : lifecycles) {
        const LifecycleKind kind = KindForEvent(candidate.event);
        if (candidate.player != contact.player ||
            candidate.slot != contact.slot ||
            candidate.generation != contact.generation ||
            (kind != LifecycleKind::Baseline &&
             kind != LifecycleKind::Spawn && kind != LifecycleKind::Morph) ||
            candidate.effectiveFrame > contact.effectiveFrame ||
            (candidate.effectiveFrame == contact.effectiveFrame &&
             candidate.row >= contact.row)) {
            continue;
        }
        if (!producer ||
            candidate.effectiveFrame > producer->effectiveFrame) {
            producer = &candidate;
            ambiguous = false;
        } else if (candidate.effectiveFrame == producer->effectiveFrame) {
            ambiguous = true;
        }
    }
    return ambiguous ? nullptr : producer;
}

bool ContactMatchesRequirement(
    const ContactEvidence& contact,
    const LifecycleEvidence& producer,
    const EntityContactRequirement& requirement) {
    return requirement.owner == 1 && requirement.target == 2 &&
           requirement.slot == contact.slot &&
           requirement.generation == contact.generation &&
           requirement.patterns.size() == 1 &&
           requirement.patterns.front() == contact.pattern &&
           ResultMatchesRequirement(requirement.result, contact.result) &&
           requirement.producerLifecycle == producer.event &&
           requirement.producerPattern == producer.pattern &&
           requirement.producerPriorPattern == producer.priorPattern &&
           requirement.opensAfterAction == producer.observedAfterStep &&
           requirement.contactAfterAction == contact.observedAfterStep;
}

struct SnowbunnyResolvedProducer {
    LifecycleEvidence producer;
    ::Mission::SnowbunnyTransitionPolicy::BridgeKind bridge =
        ::Mission::SnowbunnyTransitionPolicy::BridgeKind::None;
};

const LifecycleEvidence* FindPriorLifecycle(
    const LifecycleEvidence& lifecycle,
    const std::vector<LifecycleEvidence>& lifecycles,
    bool& ambiguous) {
    ambiguous = false;
    const LifecycleEvidence* prior = nullptr;
    for (const LifecycleEvidence& candidate : lifecycles) {
        if (!SameInstance(candidate, lifecycle) ||
            candidate.effectiveFrame >= lifecycle.effectiveFrame) {
            continue;
        }
        if (!prior || candidate.effectiveFrame > prior->effectiveFrame) {
            prior = &candidate;
            ambiguous = false;
        } else if (candidate.effectiveFrame == prior->effectiveFrame) {
            ambiguous = true;
        }
    }
    return ambiguous ? nullptr : prior;
}

bool ProvenMorphPrior(const LifecycleEvidence& morph,
                      const std::vector<LifecycleEvidence>& lifecycles) {
    if (morph.event != "morph" || morph.priorPattern <= 0) return false;
    bool ambiguous = false;
    const LifecycleEvidence* prior =
        FindPriorLifecycle(morph, lifecycles, ambiguous);
    return !ambiguous && prior && prior->event != "despawn" &&
           prior->pattern == morph.priorPattern;
}

bool ResolveSnowbunnyProducer(
    const ContactEvidence& contact,
    const std::vector<LifecycleEvidence>& lifecycles,
    SnowbunnyResolvedProducer& out) {
    using BridgeKind =
        ::Mission::SnowbunnyTransitionPolicy::BridgeKind;
    const auto* lane =
        ::Mission::SnowbunnyTransitionPolicy::LaneForContact(
            static_cast<std::uint16_t>(contact.pattern));
    if (!lane) return false;

    bool ambiguous = false;
    const LifecycleEvidence* nearest =
        FindContactProducer(contact, lifecycles, ambiguous);
    if (ambiguous || !nearest || nearest->event != "morph") return false;

    if (nearest->pattern == contact.pattern) {
        if (nearest->priorPattern != lane->controllerPattern ||
            !ProvenMorphPrior(*nearest, lifecycles)) {
            return false;
        }
        out.producer = *nearest;
        out.bridge = BridgeKind::None;
        return true;
    }

    const BridgeKind bridge =
        ::Mission::SnowbunnyTransitionPolicy::ClassifyBridge(
            static_cast<std::uint16_t>(nearest->priorPattern),
            static_cast<std::uint16_t>(nearest->pattern),
            static_cast<std::uint16_t>(contact.pattern));
    if (bridge == BridgeKind::None || nearest->row >= contact.row ||
        nearest->effectiveFrame != contact.effectiveFrame ||
        nearest->observedAfterStep != contact.observedAfterStep ||
        nearest->observedAfterActionOrder !=
            contact.observedAfterActionOrder ||
        !ProvenMorphPrior(*nearest, lifecycles)) {
        return false;
    }

    if (bridge == BridgeKind::DirectPostContactRetirement) {
        bool priorAmbiguous = false;
        const LifecycleEvidence* contactProducer =
            FindPriorLifecycle(*nearest, lifecycles, priorAmbiguous);
        if (priorAmbiguous || !contactProducer ||
            contactProducer->event != "morph" ||
            contactProducer->pattern != contact.pattern ||
            contactProducer->priorPattern != lane->controllerPattern ||
            !ProvenMorphPrior(*contactProducer, lifecycles)) {
            return false;
        }
        out.producer = *contactProducer;
    } else {
        out.producer = *nearest;
        out.producer.pattern = contact.pattern;
        out.producer.priorPattern = lane->controllerPattern;
        out.producer.attack = true;
    }
    out.bridge = bridge;
    return true;
}

bool LoadSnowbunnySidecar(
    const Mission& mission, const std::string& sourcePath,
    Metadata& metadata, std::vector<ActionEvidence>& actions,
    std::vector<LifecycleEvidence>& lifecycles,
    std::vector<ContactEvidence>& contacts, std::string& error) {
    const std::string sidecarPath = SidecarPath(sourcePath);
    std::ifstream input(sidecarPath, std::ios::binary | std::ios::ate);
    if (sidecarPath.empty() || !input.is_open()) {
        error = "the adjacent recorder entity trace is missing";
        return false;
    }
    const std::streamoff sidecarSize = input.tellg();
    if (sidecarSize < 0 ||
        static_cast<std::uint64_t>(sidecarSize) > kMaxSidecarBytes) {
        error = "the recorder entity trace exceeds the safe size limit";
        return false;
    }
    input.seekg(0, std::ios::beg);
    if (!input.good()) {
        error = "the recorder entity trace could not be rewound";
        return false;
    }

    actions.assign(mission.steps.size(), ActionEvidence{});
    std::size_t traceRecordCount = 0;
    std::size_t row = 0;
    try {
        std::string line;
        while (std::getline(input, line)) {
            ++row;
            if (line.empty()) {
                error = "the recorder trace contains an empty row";
                return false;
            }
            if (line.size() > kMaxJsonlRowBytes ||
                row > 1u + actions.size() +
                          ::Mission::RecorderEntityTrace::
                              kTraceEventCapacity) {
                error = "the recorder trace exceeds its bounded row budget";
                return false;
            }
            const json value = json::parse(line);
            if (!value.is_object()) {
                error = "the recorder trace contains a non-object row";
                return false;
            }
            std::string record;
            if (!ReadString(value, "record", record)) {
                error = "a recorder trace row has no record type";
                return false;
            }
            if (record == "metadata") {
                if (!ReadMetadata(value, metadata)) {
                    error = "the recorder trace metadata is incomplete";
                    return false;
                }
            } else if (record == "action_order") {
                if (!ReadAction(value, actions)) {
                    error = "the recorder action order is ambiguous";
                    return false;
                }
            } else if (record == "entity_lifecycle") {
                LifecycleEvidence lifecycle;
                if (!ReadLifecycle(value, row, lifecycle)) {
                    error = "an entity lifecycle row is incomplete";
                    return false;
                }
                lifecycles.push_back(std::move(lifecycle));
                ++traceRecordCount;
            } else if (record == "contact") {
                ContactEvidence contact;
                if (!ReadContact(value, row, contact)) {
                    error = "a contact row is incomplete or unclassified";
                    return false;
                }
                contacts.push_back(std::move(contact));
                ++traceRecordCount;
            } else {
                error = "the recorder trace contains an unknown row type";
                return false;
            }
        }
    } catch (const std::exception&) {
        error = "the recorder entity trace is malformed";
        return false;
    }
    if (!input.eof() && input.fail()) {
        error = "the recorder entity trace could not be read completely";
        return false;
    }
    if (!MetadataIsReliable(metadata, mission) ||
        !metadata.requiresEntityAttributionReview ||
        metadata.eventCount < 0 ||
        metadata.eventCount > static_cast<int>(
            ::Mission::RecorderEntityTrace::kTraceEventCapacity) ||
        static_cast<std::size_t>(metadata.eventCount) != traceRecordCount) {
        error = "the recorder entity trace is incomplete or unreliable";
        return false;
    }

    for (const LifecycleEvidence& lifecycle : lifecycles) {
        const std::string& expectedResource = lifecycle.player == 1
            ? metadata.p1Resource : metadata.p2Resource;
        const LifecycleKind kind = KindForEvent(lifecycle.event);
        if ((lifecycle.player != 1 && lifecycle.player != 2) ||
            lifecycle.resource != expectedResource || lifecycle.slot < 0 ||
            lifecycle.slot >= metadata.slotCapacity ||
            lifecycle.generation <= 0 ||
            !::Mission::EntityLifecyclePolicy::ValidIdentity(
                lifecycle.slot, lifecycle.generation, kind,
                lifecycle.pattern, lifecycle.priorPattern,
                kind == LifecycleKind::Baseline
                    ? -1 : lifecycle.observedAfterStep)) {
            error = "an entity lifecycle row has invalid identity";
            return false;
        }
        if (kind == LifecycleKind::Baseline &&
            (lifecycle.observedAfterStep != -1 ||
             lifecycle.observedAfterActionOrder != 0 ||
             lifecycle.effectiveFrame != 0)) {
            error = "the entity baseline has an action owner";
            return false;
        }
    }
    int priorActionFrame = -1;
    int priorActionBatch = -1;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const ActionEvidence& action = actions[index];
        const auto& step = mission.steps[index];
        if (!action.seen || step.moveIds.empty() ||
            action.move != step.moveIds.front() ||
            action.expectedAttackMask != step.expectedAttackMask ||
            action.actionOrder != static_cast<int>(index) + 1 ||
            action.effectiveFrame < priorActionFrame ||
            action.battleBatch < priorActionBatch) {
            error = "the recorder action order does not match the mission";
            return false;
        }
        priorActionFrame = action.effectiveFrame;
        priorActionBatch = action.battleBatch;
    }
    for (const LifecycleEvidence& lifecycle : lifecycles) {
        const LifecycleKind kind = KindForEvent(lifecycle.event);
        if (kind == LifecycleKind::Baseline) continue;
        if (lifecycle.observedAfterStep == -1) {
            if (lifecycle.observedAfterActionOrder != 0) {
                error = "an entity lifecycle row has an invalid pre-action gate";
                return false;
            }
            continue;
        }
        if (lifecycle.observedAfterStep < -1 ||
            lifecycle.observedAfterStep >= static_cast<int>(actions.size()) ||
            lifecycle.observedAfterActionOrder !=
                lifecycle.observedAfterStep + 1 ||
            lifecycle.effectiveFrame < actions[static_cast<std::size_t>(
                lifecycle.observedAfterStep)].effectiveFrame) {
            error = "an entity lifecycle row has an invalid action gate";
            return false;
        }
    }
    int priorSequence = 0;
    int priorContactFrame = -1;
    int priorContactBatch = -1;
    for (const ContactEvidence& contact : contacts) {
        const std::string& expectedResource = contact.player == 1
            ? metadata.p1Resource : metadata.p2Resource;
        if (contact.resource != expectedResource || contact.sequence <= 0 ||
            contact.sequence <= priorSequence || contact.battleBatch < 0 ||
            contact.effectiveFrame < priorContactFrame ||
            contact.battleBatch < priorContactBatch ||
            contact.comboBefore < 0 || contact.comboAfter < 0 ||
            contact.hpBefore < 0 || contact.hpAfter < 0 ||
            contact.observedAfterStep < -1 ||
            contact.observedAfterStep >= static_cast<int>(actions.size()) ||
            contact.observedAfterActionOrder !=
                contact.observedAfterStep + 1 ||
            (contact.observedAfterStep >= 0 &&
             contact.effectiveFrame < actions[static_cast<std::size_t>(
                 contact.observedAfterStep)].effectiveFrame) ||
            (contact.source == "entity" &&
             (contact.slot < 0 || contact.slot >= metadata.slotCapacity ||
              contact.generation <= 0 || contact.pattern <= 0 ||
              contact.attributedStep != -1 ||
              contact.attackerMove != contact.pattern)) ||
            (contact.source == "direct" &&
             (contact.slot >= 0 || contact.generation != 0 ||
              contact.pattern >= 0 || contact.attributedStep < 0 ||
              contact.attributedStep >= static_cast<int>(actions.size()) ||
              contact.attributedStep != contact.observedAfterStep ||
              contact.attackerMove != actions[static_cast<std::size_t>(
                  contact.attributedStep)].move))) {
            error = "a contact row has invalid exact ordering evidence";
            return false;
        }
        priorSequence = contact.sequence;
        priorContactFrame = contact.effectiveFrame;
        priorContactBatch = contact.battleBatch;
    }
    return true;
}

const char* ContactRequirementName(const std::string& result) {
    if (result == "hit") return "hit";
    if (result == "special_hit") return "special";
    if (result == "block") return "block";
    if (result == "recoil_guard") return "recoil_guard";
    if (result == "throw") return "throw";
    if (result == "guard_point") return "guard_point";
    return nullptr;
}

bool IsHitResult(const std::string& result) {
    return result == "hit" || result == "special_hit";
}

int SnowbunnySegmentAtContact(const Mission& mission,
                              int precedingDirectStep,
                              const ContactEvidence& contact) {
    int segment = 0;
    for (int index = 0; index < static_cast<int>(mission.steps.size());
         ++index) {
        if (!mission.steps[static_cast<std::size_t>(index)].comboEndAfter) {
            continue;
        }
        if (index < precedingDirectStep ||
            (index == precedingDirectStep &&
             ::Mission::EntitySchedulePolicy::OrderedContactBeginsNewCombo(
                 contact.comboBefore, contact.comboAfter))) {
            ++segment;
        }
    }
    return segment;
}

int SnowbunnyCastMove(const Mission& mission, int producerAction) {
    for (int index = producerAction; index >= 0; --index) {
        if (mission.steps[static_cast<std::size_t>(index)].moveIds.empty()) {
            continue;
        }
        const int move = mission.steps[static_cast<std::size_t>(index)]
                             .moveIds.front();
        if (move >= 306 && move <= 308) return move;
    }
    return -1;
}

std::string SnowbunnyNotation(int castMove, const char* outcome) {
    if (castMove < 306 || castMove > 308 || !outcome) return {};
    std::string notation = "641236";
    notation.push_back(static_cast<char>('A' + castMove - 306));
    notation += " (";
    notation += outcome;
    notation += ")";
    return notation;
}

Report RepairGeneratedSnowbunnyContacts(Mission& mission,
                                        const std::string& sourcePath) {
    const int mismatchReviews = static_cast<int>(std::count(
        mission.reviewRequired.begin(), mission.reviewRequired.end(),
        kSnowbunnyProducerMismatchReview));
    if (mismatchReviews == 0 || mission.player.character != "nayukib" ||
        !IsRecordedMissionPath(sourcePath)) {
        return {};
    }
    if (mismatchReviews != 1 || mission.format != 1 ||
        mission.savestate.empty() || mission.steps.empty() ||
        mission.steps.size() > kMaxAuthoredActions ||
        !mission.strictEntityContacts || !mission.entityContacts.empty() ||
        mission.entityLifecycles.empty()) {
        return Reject("the Snowbunny recording does not have the known broken shape");
    }

    int v4Markers = 0;
    int otherMarkers = 0;
    for (const std::string& review : mission.reviewRequired) {
        if (review == kMarkerV4) ++v4Markers;
        else if (review == kMarkerV1 || review == kMarkerV2 ||
                 review == kMarkerV3 || review == kMarkerV5) {
            ++otherMarkers;
        }
    }
    if (v4Markers != 1 || otherMarkers != 0) {
        return Reject("the Snowbunny recording has an ambiguous runtime marker");
    }

    for (const EntityLifecycleRequirement& lifecycle :
         mission.entityLifecycles) {
        const auto* lane =
            ::Mission::SnowbunnyTransitionPolicy::LaneForContact(
                static_cast<std::uint16_t>(lifecycle.pattern));
        const int producerMove =
            lifecycle.opensAfterAction >= 0 &&
                    lifecycle.opensAfterAction <
                        static_cast<int>(mission.steps.size()) &&
                    !mission.steps[static_cast<std::size_t>(
                        lifecycle.opensAfterAction)].moveIds.empty()
                ? mission.steps[static_cast<std::size_t>(
                      lifecycle.opensAfterAction)].moveIds.front()
                : -1;
        const bool historicalGenericNotation =
            lifecycle.notation == "641236 (SETUP)";
        const bool generatedNotation =
            ::Mission::EntityNames::IsGeneratedContactNotation(
                "nayukib", lifecycle.pattern, "SETUP", 1,
                lifecycle.notation, producerMove);
        if (!lane || lifecycle.owner != 1 || lifecycle.slot < 0 ||
            lifecycle.generation <= 0 || lifecycle.lifecycle != "morph" ||
            lifecycle.priorPattern != lane->controllerPattern ||
            lifecycle.opensAfterAction < 0 ||
            lifecycle.opensAfterAction >=
                static_cast<int>(mission.steps.size()) ||
            (!historicalGenericNotation && !generatedNotation)) {
            return Reject("the Snowbunny recording contains an unrelated lifecycle objective");
        }
    }

    Metadata metadata;
    std::vector<ActionEvidence> actions;
    std::vector<LifecycleEvidence> lifecycles;
    std::vector<ContactEvidence> contacts;
    std::string sidecarError;
    if (!LoadSnowbunnySidecar(mission, sourcePath, metadata, actions,
                             lifecycles, contacts, sidecarError)) {
        return Reject(sidecarError);
    }

    struct DirectOrder {
        int sequence = 0;
        int step = -1;
        int ordinal = 0;
    };
    std::vector<int> directTotals(mission.steps.size(), 0);
    std::vector<DirectOrder> directOrder;
    for (const ContactEvidence& contact : contacts) {
        if (contact.player != 1 || contact.defender != 2) {
            return Reject("the Snowbunny trace contains unrelated contact ownership");
        }
        if (contact.source == "entity") {
            if (!::Mission::SnowbunnyTransitionPolicy::LaneForContact(
                    static_cast<std::uint16_t>(contact.pattern))) {
                return Reject("the Snowbunny trace contains an unrelated learner entity contact");
            }
            continue;
        }
        const int step = contact.attributedStep;
        DirectOrder order;
        order.sequence = contact.sequence;
        order.step = step;
        order.ordinal = ++directTotals[static_cast<std::size_t>(step)];
        directOrder.push_back(order);
    }

    std::vector<EntityContactRequirement> repaired;
    std::vector<int> directSeen(mission.steps.size(), 0);
    int precedingDirectStep = -1;
    int precedingDirectOrdinal = -1;
    int compiledSegment = 0;
    int entityBoundariesSeen = 0;
    for (const ContactEvidence& contact : contacts) {
        if (contact.source == "direct") {
            precedingDirectStep = contact.attributedStep;
            precedingDirectOrdinal = ++directSeen[
                static_cast<std::size_t>(precedingDirectStep)];
            continue;
        }

        SnowbunnyResolvedProducer resolved;
        if (!ResolveSnowbunnyProducer(contact, lifecycles, resolved) ||
            resolved.producer.observedAfterStep < 0 ||
            resolved.producer.observedAfterStep >
                contact.observedAfterStep ||
            resolved.producer.observedAfterActionOrder >
                contact.observedAfterActionOrder) {
            return Reject("a Snowbunny contact has no exact or curated producer bridge");
        }
        const char* result = ContactRequirementName(contact.result);
        if (!result) {
            return Reject("a Snowbunny contact has an unsupported result");
        }

        EntityContactRequirement requirement;
        requirement.owner = 1;
        requirement.target = 2;
        requirement.slot = contact.slot;
        requirement.generation = contact.generation;
        requirement.patterns = {contact.pattern};
        requirement.result = result;
        requirement.contactsRequired = 1;
        requirement.comboHitsRequired =
            ::Mission::EntitySchedulePolicy::OrderedHitContribution(
                IsHitResult(contact.result), contact.comboBefore,
                contact.comboAfter);
        requirement.producerLifecycle = "morph";
        requirement.producerPattern = contact.pattern;
        requirement.producerPriorPattern =
            resolved.producer.priorPattern;
        requirement.opensAfterAction =
            resolved.producer.observedAfterStep;
        requirement.contactAfterAction = contact.observedAfterStep;
        requirement.afterStep = precedingDirectStep;

        const bool precedingVariableMultiHit = precedingDirectStep >= 0 &&
            mission.steps[static_cast<std::size_t>(precedingDirectStep)]
                .directContact &&
            mission.steps[static_cast<std::size_t>(precedingDirectStep)]
                    .hitsRequired > 1;
        if (precedingDirectStep >= 0 && precedingDirectOrdinal > 0 &&
            precedingDirectOrdinal < directTotals[
                static_cast<std::size_t>(precedingDirectStep)]) {
            requirement.afterStepContact = precedingVariableMultiHit
                ? 1 : precedingDirectOrdinal;
        }
        const int observedSegment = SnowbunnySegmentAtContact(
            mission, precedingDirectStep, contact);
        requirement.segment =
            ::Mission::EntitySchedulePolicy::CompiledSegment(
                observedSegment, entityBoundariesSeen, compiledSegment);
        compiledSegment = requirement.segment;
        requirement.damage = (std::max)(0,
                                         contact.hpBefore - contact.hpAfter);
        requirement.comboEndAfter = contact.comboEndAfter;

        int followingAction = -1;
        for (std::size_t step = 0; step < actions.size(); ++step) {
            if (::Mission::EntitySchedulePolicy::RecordedActionFollowsContact(
                    static_cast<std::uint32_t>(actions[step].battleBatch),
                    static_cast<std::uint32_t>(actions[step].actionOrder),
                    static_cast<std::uint32_t>(contact.battleBatch),
                    static_cast<std::uint32_t>(
                        contact.observedAfterActionOrder))) {
                followingAction = static_cast<int>(step);
                break;
            }
        }
        int followingDirectStep = -1;
        int followingDirectOrdinal = -1;
        for (const DirectOrder& direct : directOrder) {
            if (direct.sequence <= contact.sequence) continue;
            followingDirectStep = direct.step;
            followingDirectOrdinal = direct.ordinal;
            break;
        }
        if (followingDirectStep >= 0 &&
            mission.steps[static_cast<std::size_t>(followingDirectStep)]
                .directContact &&
            mission.steps[static_cast<std::size_t>(followingDirectStep)]
                    .hitsRequired > 1 &&
            followingDirectOrdinal > 1) {
            followingDirectOrdinal = -1;
        }
        const auto due =
            ::Mission::EntitySchedulePolicy::SelectDueBarrier(
                followingAction, followingDirectStep,
                followingDirectOrdinal);
        requirement.dueBeforeStep = due.step;
        requirement.dueBeforeStepContact = due.contact;

        const int anchorFrame = actions[static_cast<std::size_t>(
            requirement.opensAfterAction)].effectiveFrame;
        const std::int64_t delay =
            static_cast<std::int64_t>(contact.effectiveFrame) - anchorFrame;
        if (delay < 0 || delay >
                ((std::numeric_limits<int>::max)() - 30) / 2) {
            return Reject("a Snowbunny contact delay cannot be represented safely");
        }
        requirement.maxDelay = static_cast<int>(delay * 2 + 30);
        const int castMove = SnowbunnyCastMove(
            mission, requirement.opensAfterAction);
        const char* outcome = contact.result == "special_hit"
            ? "SPECIAL HIT" : contact.result == "hit"
            ? "HIT" : contact.result == "block"
            ? "BLOCK" : contact.result == "recoil_guard"
            ? "RG" : contact.result == "throw"
            ? "THROW" : "GUARD POINT";
        requirement.notation = SnowbunnyNotation(castMove, outcome);
        if (requirement.notation.empty()) {
            return Reject("a Snowbunny contact has no proven 641236 cast");
        }
        repaired.push_back(std::move(requirement));
        if (contact.comboEndAfter) {
            ++entityBoundariesSeen;
            compiledSegment = repaired.back().segment + 1;
        }
    }
    if (repaired.empty()) {
        return Reject("the Snowbunny trace has no learner entity contacts");
    }

    for (const EntityLifecycleRequirement& lifecycle :
         mission.entityLifecycles) {
        const bool superseded = std::any_of(
            repaired.begin(), repaired.end(),
            [&lifecycle](const EntityContactRequirement& contact) {
                return contact.slot == lifecycle.slot &&
                       contact.generation == lifecycle.generation &&
                       contact.patterns.size() == 1 &&
                       contact.patterns.front() == lifecycle.pattern &&
                       contact.producerLifecycle == lifecycle.lifecycle &&
                       contact.producerPattern == lifecycle.pattern &&
                       contact.producerPriorPattern ==
                           lifecycle.priorPattern &&
                       contact.opensAfterAction ==
                           lifecycle.opensAfterAction;
            });
        if (!superseded) {
            return Reject("a false Snowbunny lifecycle has no exact replacement contact");
        }
    }

    Mission upgraded = mission;
    upgraded.entityContacts = std::move(repaired);
    upgraded.entityLifecycles.clear();
    std::vector<std::string> reviewsAfter;
    for (const std::string& review : upgraded.reviewRequired) {
        if (review == kSnowbunnyProducerMismatchReview ||
            review == kMarkerV4) {
            continue;
        }
        reviewsAfter.push_back(review);
    }
    reviewsAfter.push_back(kMarkerV3);
    upgraded.reviewRequired = std::move(reviewsAfter);
    upgraded.strictEntityContacts = true;
    const int added = static_cast<int>(upgraded.entityContacts.size());
    mission = std::move(upgraded);

    Report report;
    report.result = Result::Upgraded;
    report.objectivesAdded = added;
    report.diagnostic = "reconstructed " + std::to_string(added) +
        " exact Snowbunny contact objective(s) from the v5 recorder trace";
    return report;
}

}

Report UpgradeLegacyLifecycleReviews(Mission& mission,
                                     const std::string& sourcePath) {
    const Report snowbunnyRepair =
        RepairGeneratedSnowbunnyContacts(mission, sourcePath);
    if (snowbunnyRepair.result != Result::NotApplicable) {
        return snowbunnyRepair;
    }
    if (!IsRecordedMissionPath(sourcePath) || mission.format != 1 ||
        mission.savestate.empty() || !mission.entityLifecycles.empty()) {
        return {};
    }
    if (mission.steps.empty() ||
        mission.steps.size() > kMaxAuthoredActions) {
        return Reject("the legacy mission has an unsupported action count");
    }

    std::vector<LegacyReview> reviews;
    std::set<std::string> uniqueReviewReasons;
    for (const std::string& reason : mission.reviewRequired) {
        LegacyReview review;
        if (ParseExactLegacyReason(reason, review) &&
            !::Mission::EntityReviewPolicy::IsObsoletePostContactReview(
                mission, reason)) {
            if (!uniqueReviewReasons.insert(reason).second) {
                return Reject("the legacy lifecycle review is duplicated");
            }
            reviews.push_back(std::move(review));
        }
    }
    if (reviews.empty()) return {};

    int markerCount = 0;
    int markerVersion = 0;
    for (const std::string& reason : mission.reviewRequired) {
        int version = 0;
        if (reason == kMarkerV1) version = 1;
        else if (reason == kMarkerV2) version = 2;
        else if (reason == kMarkerV3) version = 3;
        else if (reason == kMarkerV4) version = 4;
        else if (reason == kMarkerV5) version = 5;
        if (version != 0) {
            ++markerCount;
            markerVersion = version;
        }
    }
    const bool contactSchedule = !mission.entityContacts.empty();
    if (markerCount > 1 || markerVersion == 1 || markerVersion == 2 ||
        markerVersion == 4 || markerVersion == 5 ||
        (contactSchedule &&
         (markerVersion != 3 || !mission.strictEntityContacts)) ||
        (!contactSchedule &&
         (markerVersion != 0 || mission.strictEntityContacts))) {
        return Reject("the legacy mission does not have a migratable v3 schedule");
    }

    const std::string sidecarPath = SidecarPath(sourcePath);
    std::ifstream input(sidecarPath, std::ios::binary | std::ios::ate);
    if (sidecarPath.empty() || !input.is_open()) {
        return Reject("the adjacent recorder entity trace is missing");
    }
    const std::streamoff sidecarSize = input.tellg();
    if (sidecarSize < 0 ||
        static_cast<std::uint64_t>(sidecarSize) > kMaxSidecarBytes) {
        return Reject("the recorder entity trace exceeds the safe size limit");
    }
    input.seekg(0, std::ios::beg);
    if (!input.good()) {
        return Reject("the recorder entity trace could not be rewound");
    }

    Metadata metadata;
    std::vector<ActionEvidence> actions(mission.steps.size());
    std::vector<LifecycleEvidence> lifecycles;
    std::vector<ContactEvidence> contacts;
    std::size_t traceRecordCount = 0;
    std::size_t row = 0;
    try {
        std::string line;
        while (std::getline(input, line)) {
            ++row;
            if (line.empty()) return Reject("the recorder trace contains an empty row");
            if (line.size() > kMaxJsonlRowBytes ||
                row > 1u + actions.size() +
                          ::Mission::RecorderEntityTrace::
                              kTraceEventCapacity) {
                return Reject("the recorder trace exceeds its bounded row budget");
            }
            const json value = json::parse(line);
            if (!value.is_object()) {
                return Reject("the recorder trace contains a non-object row");
            }
            std::string record;
            if (!ReadString(value, "record", record)) {
                return Reject("a recorder trace row has no record type");
            }
            if (record == "metadata") {
                if (!ReadMetadata(value, metadata)) {
                    return Reject("the recorder trace metadata is incomplete");
                }
            } else if (record == "action_order") {
                if (!ReadAction(value, actions)) {
                    return Reject("the recorder action order is ambiguous");
                }
            } else if (record == "entity_lifecycle") {
                LifecycleEvidence lifecycle;
                if (!ReadLifecycle(value, row, lifecycle)) {
                    return Reject("an entity lifecycle row is incomplete");
                }
                lifecycles.push_back(std::move(lifecycle));
                ++traceRecordCount;
            } else if (record == "contact") {
                ContactEvidence contact;
                if (!ReadContact(value, row, contact)) {
                    return Reject("a contact row is incomplete or unclassified");
                }
                contacts.push_back(std::move(contact));
                ++traceRecordCount;
            } else {
                return Reject("the recorder trace contains an unknown row type");
            }
        }
    } catch (const std::exception&) {
        return Reject("the recorder entity trace is malformed");
    }
    if (!input.eof() && input.fail()) {
        return Reject("the recorder entity trace could not be read completely");
    }
    if (!MetadataIsReliable(metadata, mission) ||
        metadata.eventCount < 0 ||
        metadata.eventCount > static_cast<int>(
            ::Mission::RecorderEntityTrace::kTraceEventCapacity) ||
        static_cast<std::size_t>(metadata.eventCount) != traceRecordCount) {
        return Reject("the recorder entity trace is incomplete or unreliable");
    }
    for (const LifecycleEvidence& lifecycle : lifecycles) {
        const std::string& expectedResource = lifecycle.player == 1
            ? metadata.p1Resource : metadata.p2Resource;
        const LifecycleKind kind = KindForEvent(lifecycle.event);
        if ((lifecycle.player != 1 && lifecycle.player != 2) ||
            lifecycle.resource != expectedResource || lifecycle.slot < 0 ||
            lifecycle.slot >= metadata.slotCapacity ||
            lifecycle.generation <= 0 ||
            !::Mission::EntityLifecyclePolicy::ValidIdentity(
                lifecycle.slot, lifecycle.generation, kind,
                lifecycle.pattern, lifecycle.priorPattern,
                kind == LifecycleKind::Baseline
                    ? -1 : lifecycle.observedAfterStep)) {
            return Reject("an entity lifecycle row has invalid identity");
        }
        if (kind == LifecycleKind::Baseline &&
            (lifecycle.observedAfterStep != -1 ||
             lifecycle.observedAfterActionOrder != 0 ||
             lifecycle.effectiveFrame != 0)) {
            return Reject("the entity baseline has an action owner");
        }
    }
    for (const ContactEvidence& contact : contacts) {
        const std::string& expectedResource = contact.player == 1
            ? metadata.p1Resource : metadata.p2Resource;
        if (contact.resource != expectedResource ||
            (contact.source == "entity" &&
             (contact.slot < 0 || contact.slot >= metadata.slotCapacity ||
              contact.generation <= 0 || contact.pattern <= 0)) ||
            (contact.source == "direct" &&
             (contact.slot >= 0 || contact.generation != 0 ||
              contact.pattern >= 0))) {
            return Reject("a contact row has invalid producer identity");
        }
    }

    int priorActionFrame = -1;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const ActionEvidence& action = actions[index];
        const auto& step = mission.steps[index];
        if (!action.seen || step.moveIds.empty() ||
            action.move != step.moveIds.front() ||
            action.expectedAttackMask != step.expectedAttackMask ||
            action.actionOrder != static_cast<int>(index) + 1 ||
            action.effectiveFrame < priorActionFrame) {
            return Reject("the recorder action order does not match the mission");
        }
        priorActionFrame = action.effectiveFrame;
    }

    for (const LifecycleEvidence& lifecycle : lifecycles) {
        const LifecycleKind kind = KindForEvent(lifecycle.event);
        if (kind == LifecycleKind::Baseline) continue;
        // -1/0 is the valid "before the first authored action" sentinel.
        if (lifecycle.observedAfterStep == -1) {
            if (lifecycle.observedAfterActionOrder != 0) {
                return Reject("an entity lifecycle row has an invalid pre-action gate");
            }
            continue;
        }
        if (lifecycle.observedAfterStep < -1 ||
            lifecycle.observedAfterStep >= static_cast<int>(actions.size()) ||
            lifecycle.observedAfterActionOrder !=
                lifecycle.observedAfterStep + 1) {
            return Reject("an entity lifecycle row has an invalid action gate");
        }
        const ActionEvidence& action = actions[static_cast<std::size_t>(
            lifecycle.observedAfterStep)];
        if (lifecycle.effectiveFrame < action.effectiveFrame) {
            return Reject("an entity lifecycle predates its authored action");
        }
    }
    for (const ContactEvidence& contact : contacts) {
        if (contact.observedAfterStep < -1 ||
            contact.observedAfterStep >= static_cast<int>(actions.size()) ||
            contact.observedAfterActionOrder !=
                contact.observedAfterStep + 1) {
            return Reject("a contact row has an invalid action gate");
        }
        if (contact.observedAfterStep >= 0 &&
            contact.effectiveFrame < actions[static_cast<std::size_t>(
                contact.observedAfterStep)].effectiveFrame) {
            return Reject("a contact predates its observed action");
        }
    }

    std::vector<int> contactRequirement(contacts.size(), -1);
    std::vector<std::size_t> learnerEntityContacts;
    for (std::size_t contactIndex = 0; contactIndex < contacts.size();
         ++contactIndex) {
        const ContactEvidence& contact = contacts[contactIndex];
        if (contact.source != "entity" || contact.player != 1 ||
            contact.defender != 2) {
            continue;
        }
        if (!learnerEntityContacts.empty() &&
            contacts[learnerEntityContacts.back()].effectiveFrame >
                contact.effectiveFrame) {
            return Reject("learner entity contacts are not chronological");
        }
        learnerEntityContacts.push_back(contactIndex);
    }

    std::size_t nextContact = 0;
    for (std::size_t requirementIndex = 0;
         requirementIndex < mission.entityContacts.size();
         ++requirementIndex) {
        const EntityContactRequirement& requirement =
            mission.entityContacts[requirementIndex];
        if (requirement.contactsRequired < 1) {
            return Reject("the strict contact schedule has an empty run");
        }
        bool observedComboEnd = false;
        for (int ordinal = 0; ordinal < requirement.contactsRequired;
             ++ordinal) {
            if (nextContact >= learnerEntityContacts.size()) {
                return Reject("the strict contact schedule is not fully backed by the sidecar");
            }
            const std::size_t contactIndex =
                learnerEntityContacts[nextContact++];
            const ContactEvidence& contact = contacts[contactIndex];
            bool producerAmbiguous = false;
            const LifecycleEvidence* producer = FindContactProducer(
                contact, lifecycles, producerAmbiguous);
            if (producerAmbiguous || !producer ||
                producer->pattern != contact.pattern ||
                !ContactMatchesRequirement(contact, *producer,
                                           requirement)) {
                return Reject("a real entity contact is absent from its exact strict run");
            }
            contactRequirement[contactIndex] =
                static_cast<int>(requirementIndex);
            observedComboEnd = observedComboEnd || contact.comboEndAfter;
        }
        if (observedComboEnd != requirement.comboEndAfter) {
            return Reject("the strict contact run has a mismatching combo boundary");
        }
    }
    if (nextContact != learnerEntityContacts.size()) {
        return Reject("a real entity contact is absent from the strict schedule");
    }

    std::vector<PromotedObjective> promoted;
    std::set<std::string> resolvedReasons;
    for (const LegacyReview& review : reviews) {
        std::vector<const LifecycleEvidence*> matches;
        for (const LifecycleEvidence& lifecycle : lifecycles) {
            if (lifecycle.player == 1 &&
                lifecycle.resource == mission.player.character &&
                lifecycle.slot == review.slot &&
                lifecycle.generation == review.generation &&
                lifecycle.pattern == review.pattern && lifecycle.attack &&
                (lifecycle.event == "spawn" ||
                 lifecycle.event == "morph")) {
                matches.push_back(&lifecycle);
            }
        }
        if (matches.size() != 1) {
            return Reject("a legacy lifecycle review has no unique sidecar episode");
        }
        const LifecycleEvidence& lifecycle = *matches.front();
        if (lifecycle.observedAfterStep < 0 ||
            lifecycle.observedAfterStep >= static_cast<int>(actions.size()) ||
            lifecycle.observedAfterActionOrder !=
                lifecycle.observedAfterStep + 1) {
            return Reject("the lifecycle action gate is invalid");
        }
        const ActionEvidence& action = actions[static_cast<std::size_t>(
            lifecycle.observedAfterStep)];
        const int producerMove = mission.steps[static_cast<std::size_t>(
            lifecycle.observedAfterStep)].moveIds.front();
        const auto* semantic = ::Mission::EntityNames::LookupSemantic(
            mission.player.character.c_str(), lifecycle.pattern,
            producerMove);
        const bool resolvedContactSemantic = semantic &&
            semantic->disposition ==
                ::Mission::EntityNames::LifecycleDisposition::ContactEffect;

        if (lifecycle.event == "morph") {
            const LifecycleEvidence* prior = nullptr;
            for (const LifecycleEvidence& candidate : lifecycles) {
                if (!SameInstance(candidate, lifecycle) ||
                    candidate.effectiveFrame >= lifecycle.effectiveFrame) {
                    continue;
                }
                if (!prior || candidate.effectiveFrame > prior->effectiveFrame) {
                    prior = &candidate;
                } else if (prior &&
                           candidate.effectiveFrame == prior->effectiveFrame) {
                    return Reject("the lifecycle prior state is ambiguous");
                }
            }
            if (!prior || prior->event == "despawn" ||
                prior->pattern != lifecycle.priorPattern) {
                return Reject("the lifecycle prior state is not proven");
            }
        }

        int nextTransitionFrame = (std::numeric_limits<int>::max)();
        for (const LifecycleEvidence& candidate : lifecycles) {
            if (!SameInstance(candidate, lifecycle) ||
                candidate.row == lifecycle.row ||
                candidate.effectiveFrame < lifecycle.effectiveFrame) {
                continue;
            }
            if (candidate.effectiveFrame == lifecycle.effectiveFrame) {
                return Reject("the lifecycle episode boundary is ambiguous");
            }
            nextTransitionFrame = (std::min)(nextTransitionFrame,
                                              candidate.effectiveFrame);
        }
        bool linkedContact = false;
        for (const ContactEvidence& contact : contacts) {
            if (contact.source == "entity" && contact.player == 1 &&
                contact.defender == 2 && contact.slot == lifecycle.slot &&
                contact.generation == lifecycle.generation &&
                contact.pattern == lifecycle.pattern &&
                (contact.effectiveFrame > lifecycle.effectiveFrame ||
                 (contact.effectiveFrame == lifecycle.effectiveFrame &&
                  contact.row > lifecycle.row)) &&
                contact.effectiveFrame < nextTransitionFrame) {
                linkedContact = true;
                break;
            }
        }

        ::Mission::EntityReviewPolicy::LegacyLifecycleMigrationEvidence
            evidence;
        evidence.metadataSeen = metadata.seen;
        evidence.schemaVersion = metadata.version;
        evidence.slotCapacity = metadata.slotCapacity;
        evidence.exactSavestate = !mission.savestate.empty();
        evidence.entityContactHookComplete =
            metadata.entityContactHookComplete;
        evidence.contactEpochDiscontinuity =
            metadata.contactEpochDiscontinuity;
        evidence.contactJournalOverflow = metadata.contactJournalOverflow;
        evidence.droppedEvents = metadata.droppedEvents;
        evidence.p1EntityProbeIncomplete =
            metadata.p1EntityProbeIncomplete;
        evidence.p1AllocationCursorAmbiguous =
            metadata.p1AllocationCursorAmbiguous;
        evidence.requiresEntityAttributionReview =
            metadata.requiresEntityAttributionReview;
        evidence.containsUnclassifiedContact =
            metadata.containsUnclassifiedContact;
        evidence.usedLegacyContactAttribution =
            metadata.usedLegacyContactAttribution;
        evidence.metadataP1Resource = metadata.p1Resource;
        evidence.sampled = true;
        evidence.eventResource = lifecycle.resource;
        evidence.owner = lifecycle.player;
        evidence.slot = lifecycle.slot;
        evidence.generation = lifecycle.generation;
        evidence.kind = KindForEvent(lifecycle.event);
        evidence.pattern = lifecycle.pattern;
        evidence.priorPattern = lifecycle.priorPattern;
        evidence.opensAfterAction = lifecycle.observedAfterStep;
        evidence.authoredActionCount = static_cast<int>(actions.size());
        evidence.actionStartEffectiveFrame = action.effectiveFrame;
        evidence.lifecycleEffectiveFrame = lifecycle.effectiveFrame;
        evidence.attack = lifecycle.attack;
        evidence.semanticPromisesContact = resolvedContactSemantic;
        evidence.hasLinkedCommittedContact = linkedContact;
        if (!::Mission::EntityReviewPolicy::
                CanPromoteLegacyUngradedLifecycle(
                    mission.player.character, review.pattern, review.slot,
                    review.generation, evidence)) {
            return Reject("the lifecycle review lacks exact promotable evidence");
        }

        const std::int64_t delay =
            static_cast<std::int64_t>(lifecycle.effectiveFrame) -
            static_cast<std::int64_t>(action.effectiveFrame);
        if (delay < 0 || delay >
                ((std::numeric_limits<int>::max)() - 30) / 2) {
            return Reject("the lifecycle delay cannot be represented safely");
        }

        PromotedObjective objective;
        objective.effectiveFrame = lifecycle.effectiveFrame;
        objective.requirement.notation =
            ::Mission::EntityNames::FormatSemanticContact(
                mission.player.character.c_str(), lifecycle.pattern,
                "SETUP", producerMove);
        if (objective.requirement.notation.empty()) {
            return Reject("the lifecycle objective has no resolved notation");
        }
        objective.requirement.owner = 1;
        objective.requirement.slot = lifecycle.slot;
        objective.requirement.generation = lifecycle.generation;
        objective.requirement.lifecycle = lifecycle.event;
        objective.requirement.pattern = lifecycle.pattern;
        objective.requirement.priorPattern = lifecycle.priorPattern;
        objective.requirement.opensAfterAction =
            lifecycle.observedAfterStep;
        objective.requirement.maxDelay = static_cast<int>(delay * 2 + 30);
        objective.requirement.segment = 0;
        for (int stepIndex = 0;
             stepIndex < lifecycle.observedAfterStep; ++stepIndex) {
            if (mission.steps[static_cast<std::size_t>(stepIndex)]
                    .comboEndAfter) {
                ++objective.requirement.segment;
            }
        }
        for (std::size_t requirementIndex = 0;
             requirementIndex < mission.entityContacts.size();
             ++requirementIndex) {
            if (!mission.entityContacts[requirementIndex].comboEndAfter) {
                continue;
            }
            bool boundaryBefore = false;
            for (std::size_t contactIndex = 0;
                 contactIndex < contacts.size(); ++contactIndex) {
                if (contactRequirement[contactIndex] ==
                        static_cast<int>(requirementIndex) &&
                    contacts[contactIndex].comboEndAfter &&
                    (contacts[contactIndex].effectiveFrame <
                         lifecycle.effectiveFrame ||
                     (contacts[contactIndex].effectiveFrame ==
                          lifecycle.effectiveFrame &&
                      contacts[contactIndex].row < lifecycle.row))) {
                    boundaryBefore = true;
                    break;
                }
            }
            if (boundaryBefore) ++objective.requirement.segment;
        }
        promoted.push_back(std::move(objective));
        resolvedReasons.insert(review.reason);
    }

    if (promoted.empty()) {
        return Reject("no lifecycle objectives were reconstructed");
    }
    std::sort(promoted.begin(), promoted.end(),
              [](const PromotedObjective& left,
                 const PromotedObjective& right) {
                  if (left.requirement.opensAfterAction !=
                      right.requirement.opensAfterAction) {
                      return left.requirement.opensAfterAction <
                             right.requirement.opensAfterAction;
                  }
                  return left.effectiveFrame < right.effectiveFrame;
              });

    Mission upgraded = mission;
    upgraded.entityLifecycles.clear();
    for (PromotedObjective& objective : promoted) {
        upgraded.entityLifecycles.push_back(
            std::move(objective.requirement));
    }
    std::vector<std::string> reviewsAfter;
    for (const std::string& reason : upgraded.reviewRequired) {
        if (resolvedReasons.find(reason) != resolvedReasons.end() ||
            reason == kMarkerV1 || reason == kMarkerV2 ||
            reason == kMarkerV3 || reason == kMarkerV4) {
            continue;
        }
        reviewsAfter.push_back(reason);
    }
    reviewsAfter.push_back(kMarkerV4);
    upgraded.reviewRequired = std::move(reviewsAfter);
    upgraded.strictEntityContacts = true;
    mission = std::move(upgraded);

    Report report;
    report.result = Result::Upgraded;
    report.objectivesAdded = static_cast<int>(promoted.size());
    report.diagnostic = "reconstructed " +
        std::to_string(report.objectivesAdded) +
        " exact entity lifecycle objective(s) from the v5 recorder trace";
    return report;
}

}
