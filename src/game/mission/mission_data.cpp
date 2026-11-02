#include "../../../include/game/mission/mission_data.h"
#include "../../../include/game/mission/contact_event.h"
#include "../../../include/game/mission/entity_command_origin_policy.h"
#include "../../../include/game/mission/entity_notation_tables.h"
#include "../../../include/game/mission/mission_entity_fanout_policy.h"
#include "../../../include/game/mission/mission_entity_presentation_policy.h"
#include "../../../include/game/mission/mission_entity_review_policy.h"
#include "../../../include/game/mission/mission_legacy_semantic_source_policy.h"
#include "../../../include/game/mission/mission_sequence_policy.h"
#include "../../../include/game/mission/tutorial_episode_policy.h"
#include "../../../include/game/mission/tutorial_state_policy.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <fstream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>

using nlohmann::json;

namespace Mission {

const char* StepReqToString(StepReq req) {
    switch (req) {
        case StepReq::Move: return "move";
        case StepReq::Hits: return "hits";
        case StepReq::Connect: return "connect";
        case StepReq::Land: default: return "land";
    }
}
StepReq StepReqFromString(const std::string& s) {
    if (s == "move") return StepReq::Move;
    if (s == "hits") return StepReq::Hits;
    if (s == "connect") return StepReq::Connect;
    return StepReq::Land;
}

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(f);
}

EntityCommandOrigin ParseEntityCommandOrigin(const json& j) {
    EntityCommandOrigin origin;
    origin.present = true;
    origin.slot = j.value("slot", -1);
    origin.generation = j.value("generation", 0);
    origin.rootPattern = j.value("rootPattern", -1);
    origin.activationPattern = j.value("activationPattern", -1);
    return origin;
}

bool ValidateEntityCommandOriginJson(const json& j, std::string& detailOut) {
    detailOut.clear();
    if (!j.is_object()) {
        detailOut = "is not an object";
        return false;
    }
    const char* const fields[] = {
        "slot", "generation", "rootPattern", "activationPattern"
    };
    for (const char* field : fields) {
        if (!j.contains(field) || !j[field].is_number_integer()) {
            detailOut = std::string("has missing or non-integer ") + field;
            return false;
        }
    }
    const int slot = j["slot"].get<int>();
    const int generation = j["generation"].get<int>();
    const int rootPattern = j["rootPattern"].get<int>();
    const int activationPattern = j["activationPattern"].get<int>();
    if (slot < 0 ||
        slot >= ::Mission::EntityCommandOriginPolicy::
                    kEntityRingSlotCapacity ||
        generation <= 0 || rootPattern <= 0 ||
        rootPattern > 0xFFFF || activationPattern <= 0 ||
        activationPattern > 0xFFFF) {
        detailOut = "has an out-of-range identity field";
        return false;
    }
    return true;
}

Step ParseStep(const json& j) {
    Step s;
    s.notation = j.value("notation", std::string());
    if (j.contains("ids") && j["ids"].is_array()) {
        for (const auto& id : j["ids"]) {
            if (id.is_number_integer()) s.moveIds.push_back(id.get<int>());
        }
    }
    if (j.contains("entityCommand") && j["entityCommand"].is_object()) {
        s.entityCommand = ParseEntityCommandOrigin(j["entityCommand"]);
    }
    s.expectedAttackMask = j.value("expectedAttackMask", 0);
    s.req = StepReqFromString(j.value("req", std::string("land")));
    s.hitsRequired = j.value("hits", 1);
    s.directContact = j.value("contactSource", std::string()) == "direct";
    s.allowPartialHits = j.value("allowPartialHits", false);
    s.contactResult = j.value("contactResult", std::string());
    s.optional = j.value("optional", false);
    s.maxDelay = j.value("maxDelay", 0);
    s.maxGap = j.value("maxGap", 0);
    s.comboEndAfter = j.value("comboEndAfter", j.value("requireComboEndAfter", false));
    if (j.contains("comboEnd") && j["comboEnd"].is_string()) {
        const std::string v = j["comboEnd"].get<std::string>();
        if (v == "must" || v == "required") s.comboEndAfter = true;
    }
    s.charState = j.value("charState", -1);
    s.damage = j.value("damage", 0);
    return s;
}

EntityContactFanoutMember ParseEntityContactFanoutMember(const json& j) {
    EntityContactFanoutMember member;
    member.slot = j.value("slot", -1);
    member.generation = j.value("generation", 0);
    if (j.contains("patterns") && j["patterns"].is_array()) {
        for (const auto& pattern : j["patterns"]) {
            if (pattern.is_number_integer()) {
                member.patterns.push_back(pattern.get<int>());
            }
        }
    } else if (j.contains("pattern") &&
               j["pattern"].is_number_integer()) {
        member.patterns.push_back(j["pattern"].get<int>());
    }
    member.producerLifecycle =
        j.value("producerLifecycle", std::string());
    member.producerPattern = j.value("producerPattern", -1);
    member.producerPriorPattern = j.value("producerPriorPattern", -1);
    member.contactsObserved = j.value("contactsObserved", 1);
    member.comboHitsObserved = j.value("comboHitsObserved", 0);
    member.damageObserved = j.value("damageObserved", 0);
    return member;
}

EntityContactRequirement ParseEntityContact(const json& j) {
    EntityContactRequirement e;
    e.notation = j.value("notation", std::string());
    e.owner = j.value("owner", 1);
    e.target = j.value("target", 2);
    e.slot = j.value("slot", -1);
    e.generation = j.value("generation", 0);
    if (j.contains("patterns") && j["patterns"].is_array()) {
        for (const auto& pattern : j["patterns"]) {
            if (pattern.is_number_integer()) {
                e.patterns.push_back(pattern.get<int>());
            }
        }
    } else if (j.contains("pattern") && j["pattern"].is_number_integer()) {
        e.patterns.push_back(j["pattern"].get<int>());
    }
    e.result = j.value("result", std::string("hit"));
    e.contactsRequired = j.value("contactsRequired", j.value("count", 1));
    e.comboHitsRequired = j.value("comboHitsRequired", 0);
    e.minimumContactsRequired = j.value("minimumContactsRequired", 0);
    e.minimumComboHitsRequired = j.value("minimumComboHitsRequired", 0);
    if (j.contains("fanoutMembers") && j["fanoutMembers"].is_array()) {
        for (const auto& member : j["fanoutMembers"]) {
            if (member.is_object()) {
                e.fanoutMembers.push_back(
                    ParseEntityContactFanoutMember(member));
            }
        }
    }
    e.producerLifecycle = j.value("producerLifecycle", std::string());
    e.producerPattern = j.value("producerPattern", -1);
    e.producerPriorPattern = j.value("producerPriorPattern", -1);
    e.opensAfterAction = j.value("opensAfterAction", j.value("armOnStep", -1));
    e.semanticSourceAction = j.value(
        "semanticSourceAction",
        ::Mission::SemanticSourcePolicy::kLegacyAbsent);
    e.semanticSourceMove = j.value(
        "semanticSourceMove",
        ::Mission::SemanticSourcePolicy::kLegacyAbsent);
    e.contactAfterAction = j.value("contactAfterAction", -1);
    e.afterStep = j.value("afterStep", -1);
    e.afterStepContact = j.value("afterStepContact", -1);
    e.dueBeforeStep = j.value("dueBeforeStep", j.value("beforeStep", -1));
    e.dueBeforeStepContact = j.value("dueBeforeStepContact", -1);
    e.segment = j.value("segment", 0);
    e.maxDelay = j.value("maxDelay", 0);
    e.damage = j.value("damage", 0);
    e.comboEndAfter = j.value("comboEndAfter", false);
    return e;
}

bool ValidateEntityContactSemanticSourceJson(const json& j,
                                             std::string& detailOut) {
    detailOut.clear();
    const bool hasAction = j.contains("semanticSourceAction");
    const bool hasMove = j.contains("semanticSourceMove");
    if (hasAction != hasMove) {
        detailOut = "has only one semantic source field";
        return false;
    }
    if (!hasAction) return true;
    if (!j["semanticSourceAction"].is_number_integer() ||
        !j["semanticSourceMove"].is_number_integer()) {
        detailOut = "has non-integer semantic source provenance";
        return false;
    }
    const int action = j["semanticSourceAction"].get<int>();
    const int move = j["semanticSourceMove"].get<int>();
    // -2 means field absence and is never serialized.
    if (::Mission::SemanticSourcePolicy::IsLegacyAbsent(action, move) ||
        !::Mission::SemanticSourcePolicy::ValidPersistedPair(action, move)) {
        detailOut = "has invalid semantic source provenance";
        return false;
    }
    return true;
}

bool ValidateEntityContactFanoutJson(const json& j,
                                     std::string& detailOut) {
    detailOut.clear();
    const bool hasMembers = j.contains("fanoutMembers");
    if (!hasMembers) {
        if (j.contains("minimumContactsRequired") ||
            j.contains("minimumComboHitsRequired")) {
            detailOut = "has fanout minima without fanoutMembers";
            return false;
        }
        return true;
    }
    if (!j["fanoutMembers"].is_array() ||
        j["fanoutMembers"].size() < 2) {
        detailOut = "has fewer than two fanoutMembers";
        return false;
    }
    if (!j.contains("minimumContactsRequired") ||
        !j["minimumContactsRequired"].is_number_integer() ||
        !j.contains("minimumComboHitsRequired") ||
        !j["minimumComboHitsRequired"].is_number_integer()) {
        detailOut = "has missing or non-integer fanout minima";
        return false;
    }
    const int minimumContacts = j["minimumContactsRequired"].get<int>();
    const int minimumComboHits = j["minimumComboHitsRequired"].get<int>();
    const int observedContacts = j.value("contactsRequired", 1);
    const int observedComboHits = j.value("comboHitsRequired", 0);
    if (minimumContacts < 1 || minimumComboHits < 0 ||
        observedContacts < minimumContacts ||
        observedComboHits < minimumComboHits ||
        j.value("slot", -1) != -1 || j.value("generation", 0) != 0) {
        detailOut = "has inconsistent fanout minima or aggregate identity";
        return false;
    }

    std::set<std::pair<int, int>> identities;
    for (std::size_t index = 0; index < j["fanoutMembers"].size(); ++index) {
        const json& member = j["fanoutMembers"][index];
        if (!member.is_object()) {
            detailOut = "has a non-object fanout member";
            return false;
        }
        const char* requiredIntegerFields[] = {
            "slot", "generation", "producerPattern", "contactsObserved",
            "comboHitsObserved"};
        for (const char* field : requiredIntegerFields) {
            if (!member.contains(field) ||
                !member[field].is_number_integer()) {
                detailOut = std::string("has fanout member with missing or non-integer ") +
                    field;
                return false;
            }
        }
        if (!member.contains("producerLifecycle") ||
            !member["producerLifecycle"].is_string() ||
            !member.contains("patterns") ||
            !member["patterns"].is_array() ||
            member["patterns"].empty()) {
            detailOut = "has fanout member with incomplete producer identity";
            return false;
        }
        const int slot = member["slot"].get<int>();
        const int generation = member["generation"].get<int>();
        const int producerPattern = member["producerPattern"].get<int>();
        const int contacts = member["contactsObserved"].get<int>();
        const int comboHits = member["comboHitsObserved"].get<int>();
        const int damage = member.value("damageObserved", 0);
        const int priorPattern = member.value("producerPriorPattern", -1);
        const std::string lifecycle =
            member["producerLifecycle"].get<std::string>();
        if (slot < 0 || generation <= 0 || producerPattern <= 0 ||
            contacts < 0 || comboHits < 0 || damage < 0 ||
            !identities.emplace(slot, generation).second ||
            (lifecycle != "baseline" && lifecycle != "spawn" &&
             lifecycle != "morph") ||
            (lifecycle == "morph"
                 ? priorPattern <= 0 || priorPattern == producerPattern
                 : priorPattern >= 0)) {
            detailOut = "has invalid fanout member lineage or observed counts";
            return false;
        }
        for (const json& pattern : member["patterns"]) {
            if (!pattern.is_number_integer() || pattern.get<int>() <= 0 ||
                pattern.get<int>() > 65535) {
                detailOut = "has invalid fanout member pattern";
                return false;
            }
        }
    }
    return true;
}

EntityLifecycleRequirement ParseEntityLifecycle(const json& j) {
    EntityLifecycleRequirement e;
    e.notation = j.value("notation", std::string());
    e.owner = j.value("owner", 1);
    e.slot = j.value("slot", -1);
    e.generation = j.value("generation", 0);
    e.lifecycle = j.value("lifecycle", std::string());
    e.pattern = j.value("pattern", -1);
    e.priorPattern = j.value("priorPattern", -1);
    e.opensAfterAction = j.value("opensAfterAction", -1);
    e.segment = j.value("segment", 0);
    e.maxDelay = j.value("maxDelay", 0);
    return e;
}

bool ValidateEntityLifecycleJson(const json& j, std::string& detailOut) {
    detailOut.clear();
    const auto requireInteger = [&](const char* field) {
        if (!j.contains(field) || j[field].is_number_integer()) return true;
        detailOut = std::string("has non-integer ") + field;
        return false;
    };
    if (j.contains("notation") && !j["notation"].is_string()) {
        detailOut = "has non-string notation";
        return false;
    }
    if (j.contains("lifecycle") && !j["lifecycle"].is_string()) {
        detailOut = "has non-string lifecycle";
        return false;
    }
    if (!requireInteger("owner") || !requireInteger("slot") ||
        !requireInteger("generation") || !requireInteger("pattern") ||
        !requireInteger("priorPattern") ||
        !requireInteger("opensAfterAction") || !requireInteger("segment") ||
        !requireInteger("maxDelay")) {
        return false;
    }
    const int owner = j.value("owner", 1);
    const int slot = j.value("slot", -1);
    const int generation = j.value("generation", 0);
    const int pattern = j.value("pattern", -1);
    const int priorPattern = j.value("priorPattern", -1);
    const int opensAfterAction = j.value("opensAfterAction", -1);
    const int segment = j.value("segment", 0);
    const int maxDelay = j.value("maxDelay", 0);
    if (owner < 1 || owner > 2) {
        detailOut = "has owner outside 1..2";
        return false;
    }
    if (slot < -1 || generation < 0 || pattern < -1 || pattern > 65535 ||
        priorPattern < -1 || priorPattern > 65535 ||
        opensAfterAction < -1 || segment < 0 || maxDelay < 0) {
        detailOut = "has an out-of-range numeric field";
        return false;
    }
    if (j.contains("lifecycle")) {
        const std::string lifecycle = j["lifecycle"].get<std::string>();
        if (!lifecycle.empty() && lifecycle != "baseline" &&
            lifecycle != "spawn" && lifecycle != "morph" &&
            lifecycle != "despawn") {
            detailOut = "has unsupported lifecycle kind";
            return false;
        }
    }
    return true;
}

void NormalizeFlexibleEntityContactBarriers(Mission& mission) {
    for (EntityContactRequirement& requirement : mission.entityContacts) {
        if (requirement.afterStepContact > 0 && requirement.afterStep >= 0 &&
            requirement.afterStep < static_cast<int>(mission.steps.size()) &&
            mission.steps[static_cast<std::size_t>(requirement.afterStep)]
                .allowPartialHits) {
            const Step& step = mission.steps[
                static_cast<std::size_t>(requirement.afterStep)];
            requirement.afterStepContact =
                requirement.afterStepContact < step.hitsRequired ? 1 : -1;
        }
        if (requirement.dueBeforeStepContact > 1 &&
            requirement.dueBeforeStep >= 0 &&
            requirement.dueBeforeStep < static_cast<int>(mission.steps.size()) &&
            mission.steps[static_cast<std::size_t>(requirement.dueBeforeStep)]
                .allowPartialHits) {
            requirement.dueBeforeStepContact = -1;
        }
    }
}

const char* EntityContactOutcome(const std::string& result) {
    if (result == "hit") return "HIT";
    if (result == "special") return "SPECIAL HIT";
    if (result == "block") return "BLOCK";
    if (result == "recoil_guard") return "RG";
    if (result == "throw") return "THROW";
    if (result == "guard_point") return "GUARD POINT";
    return "CONTACT";
}

bool HasExactCuratedFanoutIdentity(
    const EntityContactRequirement& requirement, int pattern) {
    if (requirement.fanoutMembers.empty() || requirement.slot != -1 ||
        requirement.generation != 0 ||
        requirement.minimumContactsRequired < 1) {
        return false;
    }
    std::set<std::pair<int, int>> identities;
    for (const EntityContactFanoutMember& member :
         requirement.fanoutMembers) {
        if (member.slot < 0 || member.generation <= 0 ||
            member.patterns.size() != 1 ||
            member.patterns.front() != pattern ||
            member.producerLifecycle != "spawn" ||
            member.producerPattern != pattern ||
            member.producerPriorPattern >= 0 ||
            !identities.emplace(member.slot, member.generation).second) {
            return false;
        }
    }
    return true;
}

void PromoteLegacySemanticSources(Mission& mission) {
    using namespace ::Mission::EntityNames;
    using namespace ::Mission::EntityPresentationPolicy;
    using ::Mission::LegacySemanticSourcePolicy::AccumulateMove;
    using ::Mission::LegacySemanticSourcePolicy::MoveSelection;
    using ::Mission::LegacySemanticSourcePolicy::SelectMove;

    const char* character = mission.player.character.c_str();
    for (EntityContactRequirement& requirement : mission.entityContacts) {
        if (!::Mission::SemanticSourcePolicy::IsLegacyAbsent(
                requirement.semanticSourceAction,
                requirement.semanticSourceMove) ||
            requirement.patterns.size() != 1 ||
            requirement.owner != 1 || requirement.target != 2 ||
            requirement.producerLifecycle != "spawn" ||
            requirement.producerPriorPattern >= 0) {
            continue;
        }

        const int pattern = requirement.patterns.front();
        const bool fanout = !requirement.fanoutMembers.empty();
        const bool exactIdentity = fanout
            ? HasExactCuratedFanoutIdentity(requirement, pattern)
            : requirement.slot >= 0 && requirement.generation > 0;
        const int sourceAction = requirement.opensAfterAction;
        if (!exactIdentity || pattern <= 0 ||
            requirement.producerPattern != pattern ||
            sourceAction < 0 ||
            sourceAction >= static_cast<int>(mission.steps.size()) ||
            requirement.contactAfterAction != sourceAction) {
            continue;
        }

        const Step& source =
            mission.steps[static_cast<std::size_t>(sourceAction)];
        if (source.optional || source.moveIds.empty()) continue;

        MoveSelection selection;
        for (const int move : source.moveIds) {
            const SemanticNote* exact =
                LookupExactProducerSemantic(character, pattern, move);
            const bool exactContact = exact &&
                HasResolvedContactPresentation(exact->disposition) &&
                (!fanout || ::Mission::EntityFanoutPolicy::IsFlexibleFanout(
                    mission.player.character, pattern, move));
            selection = AccumulateMove(selection, move, exactContact);
        }

        const SemanticNote* wildcard =
            LookupWildcardSemantic(character, pattern);
        const bool allowOrdinaryWildcard = !fanout && wildcard &&
            wildcard->role == PresentationRole::InlineProjectile &&
            HasResolvedContactPresentation(wildcard->disposition) &&
            !HasProducerQualifiedPersistentContactVariant(character,
                                                          pattern);
        const int sourceMove = SelectMove(selection, allowOrdinaryWildcard);
        if (sourceMove <= 0) continue;

        const SemanticNote* exact =
            LookupExactProducerSemantic(character, pattern, sourceMove);
        const SemanticNote* semantic = exact ? exact : wildcard;
        if (!semantic ||
            !HasResolvedContactPresentation(semantic->disposition)) {
            continue;
        }

        const char* outcome = EntityContactOutcome(requirement.result);
        const int presentedContacts = fanout
            ? requirement.minimumContactsRequired
            : requirement.contactsRequired;
        const bool generated = requirement.notation.empty() ||
            LookupSemanticForGeneratedContactNotation(
                character, pattern, outcome, presentedContacts,
                requirement.notation) != nullptr ||
            IsGeneratedContactNotation(character, pattern, outcome,
                                       presentedContacts,
                                       requirement.notation, sourceMove);

        Timing timing;
        timing.opensAfterAction = sourceAction;
        timing.contactAfterAction = requirement.contactAfterAction;
        timing.afterStep = requirement.afterStep;
        timing.afterStepContact = requirement.afterStepContact;
        timing.dueBeforeStep = requirement.dueBeforeStep;
        timing.dueBeforeStepContact = requirement.dueBeforeStepContact;
        timing.comboEndAfter = requirement.comboEndAfter;

        ExactProducerHit candidate;
        candidate.action = sourceAction;
        candidate.exactSource = true;
        candidate.generated = generated;
        candidate.resolvedContact = true;
        candidate.actionEligible = true;
        candidate.ordinaryProjectile =
            semantic->role == PresentationRole::InlineProjectile;
        candidate.producerMoveMatches = exact &&
            exact->producerMove == sourceMove;
        if (!CanInlineExactProducerHit(candidate, timing)) continue;

        requirement.semanticSourceAction = sourceAction;
        requirement.semanticSourceMove = sourceMove;
    }
}

constexpr const char* kEntityScheduleRuntimeMarkerV3 =
    "runtime:entity-contact-schedule-v3";
constexpr const char* kEntityScheduleRuntimeMarkerV4 =
    "runtime:entity-lifecycle-schedule-v4";
constexpr const char* kEntityScheduleRuntimeMarkerV5 =
    "runtime:entity-fanout-schedule-v5";

int FanoutProducerMove(const Mission& mission,
                       int producerAction,
                       int attackChildPattern) {
    if (producerAction < 0 ||
        producerAction >= static_cast<int>(mission.steps.size())) {
        return -1;
    }
    const Step& producer =
        mission.steps[static_cast<std::size_t>(producerAction)];
    for (const int moveId : producer.moveIds) {
        if (EntityFanoutPolicy::IsFlexibleFanout(
                mission.player.character, attackChildPattern, moveId)) {
            return moveId;
        }
    }
    return -1;
}

bool HasUpgradeableFanoutMarker(const Mission& mission) {
    int upgradeableMarkers = 0;
    for (const std::string& review : mission.reviewRequired) {
        if (review == kEntityScheduleRuntimeMarkerV5) return false;
        if (review == kEntityScheduleRuntimeMarkerV3 ||
            review == kEntityScheduleRuntimeMarkerV4) {
            ++upgradeableMarkers;
        }
    }
    return upgradeableMarkers == 1;
}

bool IsExactFanoutContactCandidate(
    const Mission& mission,
    const EntityContactRequirement& requirement) {
    return requirement.fanoutMembers.empty() &&
           requirement.minimumContactsRequired == 0 &&
           requirement.minimumComboHitsRequired == 0 &&
           requirement.owner == 1 && requirement.target == 2 &&
           requirement.slot >= 0 && requirement.generation > 0 &&
           requirement.patterns.size() == 1 &&
           requirement.patterns.front() == 435 &&
           requirement.result == "hit" &&
           requirement.contactsRequired == 1 &&
           requirement.comboHitsRequired == 1 &&
           requirement.producerLifecycle == "spawn" &&
           requirement.producerPattern == 435 &&
           requirement.producerPriorPattern < 0 &&
           FanoutProducerMove(mission, requirement.opensAfterAction,
                              requirement.patterns.front()) >= 0;
}

bool SameFanoutEpisode(const EntityContactRequirement& left,
                       const EntityContactRequirement& right) {
    return left.owner == right.owner && left.target == right.target &&
           left.result == right.result &&
           left.producerLifecycle == right.producerLifecycle &&
           left.producerPattern == right.producerPattern &&
           left.producerPriorPattern == right.producerPriorPattern &&
           left.opensAfterAction == right.opensAfterAction &&
           left.semanticSourceAction == right.semanticSourceAction &&
           left.semanticSourceMove == right.semanticSourceMove &&
           left.contactAfterAction == right.contactAfterAction &&
           left.afterStep == right.afterStep &&
           left.afterStepContact == right.afterStepContact &&
           left.dueBeforeStep == right.dueBeforeStep &&
           left.dueBeforeStepContact == right.dueBeforeStepContact &&
           left.segment == right.segment;
}

bool SameFanoutIdentity(const EntityContactFanoutMember& member,
                        int slot,
                        int generation) {
    return member.slot == slot && member.generation == generation;
}

EntityContactFanoutMember FanoutMemberFromContact(
    const EntityContactRequirement& requirement) {
    EntityContactFanoutMember member;
    member.slot = requirement.slot;
    member.generation = requirement.generation;
    member.patterns = requirement.patterns;
    member.producerLifecycle = requirement.producerLifecycle;
    member.producerPattern = requirement.producerPattern;
    member.producerPriorPattern = requirement.producerPriorPattern;
    member.contactsObserved = requirement.contactsRequired;
    member.comboHitsObserved = requirement.comboHitsRequired;
    member.damageObserved = requirement.damage;
    return member;
}

bool IsFanoutSiblingLifecycle(
    const Mission& mission,
    const EntityContactRequirement& episode,
    const EntityLifecycleRequirement& lifecycle) {
    if (lifecycle.owner != episode.owner || lifecycle.slot < 0 ||
        lifecycle.generation <= 0 || lifecycle.lifecycle != "spawn" ||
        lifecycle.pattern != 435 || lifecycle.priorPattern >= 0 ||
        lifecycle.opensAfterAction != episode.opensAfterAction ||
        lifecycle.segment != episode.segment ||
        FanoutProducerMove(mission, lifecycle.opensAfterAction,
                           lifecycle.pattern) < 0) {
        return false;
    }
    return std::none_of(
        episode.fanoutMembers.begin(), episode.fanoutMembers.end(),
        [&](const EntityContactFanoutMember& member) {
            return SameFanoutIdentity(member, lifecycle.slot,
                                      lifecycle.generation);
        });
}

EntityContactFanoutMember FanoutMemberFromLifecycle(
    const EntityLifecycleRequirement& lifecycle) {
    EntityContactFanoutMember member;
    member.slot = lifecycle.slot;
    member.generation = lifecycle.generation;
    member.patterns = {lifecycle.pattern};
    member.producerLifecycle = lifecycle.lifecycle;
    member.producerPattern = lifecycle.pattern;
    member.producerPriorPattern = lifecycle.priorPattern;
    member.contactsObserved = 0;
    member.comboHitsObserved = 0;
    member.damageObserved = 0;
    return member;
}

int SaturatingAdd(int left, int right) {
    if (right <= 0) return left;
    if (left > (std::numeric_limits<int>::max)() - right) {
        return (std::numeric_limits<int>::max)();
    }
    return left + right;
}

bool HasFanoutMarkerV5(const Mission& mission) {
    int v5Markers = 0;
    for (const std::string& review : mission.reviewRequired) {
        if (review == kEntityScheduleRuntimeMarkerV5) {
            ++v5Markers;
        } else if (review == kEntityScheduleRuntimeMarkerV3 ||
                   review == kEntityScheduleRuntimeMarkerV4) {
            return false;
        }
    }
    return v5Markers == 1;
}

bool RepairSplitFlexibleFanoutBoundaryV5(Mission& mission) {
    if (!mission.strictEntityContacts || !HasFanoutMarkerV5(mission)) {
        return false;
    }

    bool changed = false;
    for (std::size_t index = 0; index + 1 < mission.entityContacts.size();) {
        EntityContactRequirement& episode = mission.entityContacts[index];
        const EntityContactRequirement& sibling =
            mission.entityContacts[index + 1];
        const int producerMove = FanoutProducerMove(
            mission, episode.opensAfterAction,
            episode.patterns.size() == 1 ? episode.patterns.front() : -1);
        const bool curatedEpisode = !episode.fanoutMembers.empty() &&
            episode.minimumContactsRequired == 1 &&
            episode.minimumComboHitsRequired == 1 &&
            episode.owner == 1 && episode.target == 2 &&
            episode.slot == -1 && episode.generation == 0 &&
            episode.patterns.size() == 1 &&
            episode.patterns.front() == 435 &&
            episode.result == "hit" &&
            episode.producerLifecycle == "spawn" &&
            episode.producerPattern == 435 &&
            episode.producerPriorPattern < 0 && producerMove >= 0;
        if (!curatedEpisode || episode.comboEndAfter ||
            !sibling.comboEndAfter ||
            !IsExactFanoutContactCandidate(mission, sibling) ||
            !SameFanoutEpisode(episode, sibling)) {
            ++index;
            continue;
        }

        const bool duplicateIdentity = std::any_of(
            episode.fanoutMembers.begin(), episode.fanoutMembers.end(),
            [&](const EntityContactFanoutMember& member) {
                return SameFanoutIdentity(member, sibling.slot,
                                          sibling.generation);
            });
        if (duplicateIdentity) {
            ++index;
            continue;
        }

        episode.fanoutMembers.push_back(FanoutMemberFromContact(sibling));
        episode.contactsRequired = SaturatingAdd(
            episode.contactsRequired, sibling.contactsRequired);
        episode.comboHitsRequired = SaturatingAdd(
            episode.comboHitsRequired, sibling.comboHitsRequired);
        episode.damage = SaturatingAdd(episode.damage, sibling.damage);
        episode.maxDelay = (std::max)(episode.maxDelay, sibling.maxDelay);
        episode.comboEndAfter = true;
        mission.entityContacts.erase(mission.entityContacts.begin() +
                                     static_cast<std::ptrdiff_t>(index + 1));
        changed = true;
        ++index;
    }
    return changed;
}

bool UpgradeFanoutMarker(Mission& mission) {
    bool changed = false;
    std::vector<std::string> reviews;
    reviews.reserve(mission.reviewRequired.size());
    for (const std::string& review : mission.reviewRequired) {
        if (review == kEntityScheduleRuntimeMarkerV3 ||
            review == kEntityScheduleRuntimeMarkerV4) {
            changed = true;
            continue;
        }
        reviews.push_back(review);
    }
    reviews.push_back(kEntityScheduleRuntimeMarkerV5);
    mission.reviewRequired = std::move(reviews);
    return changed;
}

bool NormalizeFlexibleEntityFanoutEpisodesImpl(Mission& mission) {
    if (!mission.strictEntityContacts) {
        return false;
    }
    if (HasFanoutMarkerV5(mission)) {
        return RepairSplitFlexibleFanoutBoundaryV5(mission);
    }
    if (!HasUpgradeableFanoutMarker(mission)) return false;

    bool changed = false;
    std::vector<EntityContactRequirement> normalized;
    normalized.reserve(mission.entityContacts.size());
    std::vector<bool> consumed(mission.entityContacts.size(), false);
    std::vector<bool> blockedByDuplicateIdentity(
        mission.entityContacts.size(), false);
    for (std::size_t begin = 0; begin < mission.entityContacts.size();
         ++begin) {
        if (consumed[begin]) continue;
        const EntityContactRequirement& first = mission.entityContacts[begin];
        if (blockedByDuplicateIdentity[begin] ||
            !IsExactFanoutContactCandidate(mission, first)) {
            normalized.push_back(first);
            continue;
        }

        std::vector<std::size_t> episodeContacts;
        episodeContacts.push_back(begin);
        std::set<std::pair<int, int>> identities;
        identities.emplace(first.slot, first.generation);
        bool duplicateIdentity = false;
        for (std::size_t index = begin + 1;
             index < mission.entityContacts.size(); ++index) {
            const EntityContactRequirement& candidate =
                mission.entityContacts[index];
            if (!IsExactFanoutContactCandidate(mission, candidate) ||
                !SameFanoutEpisode(first, candidate)) {
                continue;
            }
            episodeContacts.push_back(index);
            if (!identities.emplace(candidate.slot,
                                    candidate.generation).second) {
                duplicateIdentity = true;
            }
        }
        if (duplicateIdentity) {
            for (const std::size_t index : episodeContacts) {
                blockedByDuplicateIdentity[index] = true;
            }
            normalized.push_back(first);
            continue;
        }

        EntityContactRequirement episode = first;
        episode.slot = -1;
        episode.generation = 0;
        episode.minimumContactsRequired = 1;
        episode.minimumComboHitsRequired = 1;
        episode.contactsRequired = 0;
        episode.comboHitsRequired = 0;
        episode.damage = 0;
        episode.maxDelay = 0;
        episode.comboEndAfter = false;
        episode.fanoutMembers.clear();
        for (const std::size_t index : episodeContacts) {
            const EntityContactRequirement& requirement =
                mission.entityContacts[index];
            episode.fanoutMembers.push_back(
                FanoutMemberFromContact(requirement));
            episode.contactsRequired = SaturatingAdd(
                episode.contactsRequired, requirement.contactsRequired);
            episode.comboHitsRequired = SaturatingAdd(
                episode.comboHitsRequired, requirement.comboHitsRequired);
            episode.damage = SaturatingAdd(episode.damage,
                                           requirement.damage);
            episode.maxDelay = (std::max)(episode.maxDelay,
                                          requirement.maxDelay);
            episode.comboEndAfter = episode.comboEndAfter ||
                requirement.comboEndAfter;
        }

        std::vector<bool> absorbLifecycle(
            mission.entityLifecycles.size(), false);
        if (!duplicateIdentity) {
            for (std::size_t lifecycleIndex = 0;
                 lifecycleIndex < mission.entityLifecycles.size();
                 ++lifecycleIndex) {
                const EntityLifecycleRequirement& lifecycle =
                    mission.entityLifecycles[lifecycleIndex];
                if (!IsFanoutSiblingLifecycle(mission, episode, lifecycle)) {
                    continue;
                }
                episode.fanoutMembers.push_back(
                    FanoutMemberFromLifecycle(lifecycle));
                episode.maxDelay = (std::max)(episode.maxDelay,
                                              lifecycle.maxDelay);
                absorbLifecycle[lifecycleIndex] = true;
            }
        }

        if (episode.fanoutMembers.size() < 2) {
            normalized.push_back(first);
            continue;
        }

        normalized.push_back(std::move(episode));
        for (std::size_t index = 1; index < episodeContacts.size(); ++index) {
            consumed[episodeContacts[index]] = true;
        }
        if (std::find(absorbLifecycle.begin(), absorbLifecycle.end(), true) !=
            absorbLifecycle.end()) {
            std::vector<EntityLifecycleRequirement> lifecycles;
            lifecycles.reserve(mission.entityLifecycles.size());
            for (std::size_t lifecycleIndex = 0;
                 lifecycleIndex < mission.entityLifecycles.size();
                 ++lifecycleIndex) {
                if (!absorbLifecycle[lifecycleIndex]) {
                    lifecycles.push_back(
                        mission.entityLifecycles[lifecycleIndex]);
                }
            }
            mission.entityLifecycles = std::move(lifecycles);
        }
        changed = true;
    }

    if (!changed) return false;
    mission.entityContacts = std::move(normalized);
    UpgradeFanoutMarker(mission);
    return true;
}

std::map<std::string, int> ParseResources(const json& j) {
    std::map<std::string, int> out;
    if (j.contains("resources") && j["resources"].is_object()) {
        for (auto it = j["resources"].begin(); it != j["resources"].end(); ++it) {
            if (it.value().is_number_integer()) out[it.key()] = it.value().get<int>();
        }
    }
    return out;
}

PlayerSetup ParsePlayer(const json& j) {
    PlayerSetup p;
    if (!j.is_object()) return p;
    p.character = j.value("character", std::string());
    if (j.contains("pos") && j["pos"].is_object()) {
        p.hasPos = true;
        p.posX = j["pos"].value("x", 0.0);
        p.posY = j["pos"].value("y", 0.0);
    } else if (j.contains("pos")) {
        p.hasPos = true;
        p.posX = j.value("pos", 0.0);
    }
    p.palette = j.value("palette", 0);
    p.rf = j.value("rf", -1);
    p.meter = j.value("meter", -1);
    p.hp = j.value("hp", -1);
    p.blueIC = j.value("blueIC", -1);
    p.guard = j.value("guard", -1);
    p.rfLock = j.value("rfLock", false);
    p.resources = ParseResources(j);
    return p;
}

DummySetup ParseDummy(const json& j) {
    DummySetup d;
    if (!j.is_object()) return d;
    d.character = j.value("character", std::string());
    if (j.contains("pos") && j["pos"].is_object()) {
        d.hasPos = true;
        d.posX = j["pos"].value("x", 0.0);
        d.posY = j["pos"].value("y", 0.0);
    }
    d.palette = j.value("palette", 0);
    d.rf = j.value("rf", -1);
    d.meter = j.value("meter", -1);
    d.hp = j.value("hp", -1);
    d.blueIC = j.value("blueIC", -1);
    d.guard = j.value("guard", -1);
    d.rfLock = j.value("rfLock", false);
    d.crouch = j.value("crouch", false);
    d.jump = j.value("jump", false);
    d.airtech = j.value("airtech", std::string("none"));
    d.resources = ParseResources(j);
    return d;
}

ScoreTier ParseScore(const json& j) {
    ScoreTier t;
    t.minHits = j.value("minHits", j.value("min_hits", 0));
    t.minDamage = j.value("minDamage", j.value("min_damage", 0));
    t.maxAttempts = j.value("maxAttempts", j.value("max_attempts", 0));
    t.label = j.value("label", std::string());
    return t;
}

json StepToJson(const Step& s) {
    json j;
    j["notation"] = s.notation;
    if (!s.moveIds.empty()) j["ids"] = s.moveIds;
    if (s.entityCommand.present) {
        j["entityCommand"] = {
            {"slot", s.entityCommand.slot},
            {"generation", s.entityCommand.generation},
            {"rootPattern", s.entityCommand.rootPattern},
            {"activationPattern", s.entityCommand.activationPattern}
        };
    }
    if (s.expectedAttackMask != 0) {
        j["expectedAttackMask"] = s.expectedAttackMask;
    }
    j["req"] = StepReqToString(s.req);
    if (s.req == StepReq::Hits) j["hits"] = s.hitsRequired;
    if (s.directContact) j["contactSource"] = "direct";
    // Always write allowPartialHits for direct multi-hit steps; omitting false would migrate a
    // strict step to the flexible policy.
    if (s.directContact && s.req == StepReq::Hits && s.hitsRequired > 1) {
        j["allowPartialHits"] = s.allowPartialHits;
    }
    if (s.directContact && !s.contactResult.empty()) {
        j["contactResult"] = s.contactResult;
    }
    if (s.optional) j["optional"] = true;
    if (s.maxDelay > 0) j["maxDelay"] = s.maxDelay;
    if (s.maxGap > 0) j["maxGap"] = s.maxGap;
    if (s.comboEndAfter) j["comboEndAfter"] = true;
    if (s.charState >= 0) j["charState"] = s.charState;
    if (s.damage > 0) j["damage"] = s.damage;
    return j;
}

json EntityContactFanoutMemberToJson(
    const EntityContactFanoutMember& member) {
    json j;
    j["slot"] = member.slot;
    j["generation"] = member.generation;
    j["patterns"] = member.patterns;
    j["producerLifecycle"] = member.producerLifecycle;
    j["producerPattern"] = member.producerPattern;
    if (member.producerPriorPattern >= 0) {
        j["producerPriorPattern"] = member.producerPriorPattern;
    }
    j["contactsObserved"] = member.contactsObserved;
    j["comboHitsObserved"] = member.comboHitsObserved;
    if (member.damageObserved > 0) {
        j["damageObserved"] = member.damageObserved;
    }
    return j;
}

json EntityContactToJson(const EntityContactRequirement& e) {
    json j;
    if (!e.notation.empty()) j["notation"] = e.notation;
    j["owner"] = e.owner;
    j["target"] = e.target;
    j["slot"] = e.slot;
    if (e.generation > 0) j["generation"] = e.generation;
    j["patterns"] = e.patterns;
    j["result"] = e.result;
    j["contactsRequired"] = e.contactsRequired;
    if (e.comboHitsRequired > 0) j["comboHitsRequired"] = e.comboHitsRequired;
    if (!e.fanoutMembers.empty()) {
        j["minimumContactsRequired"] = e.minimumContactsRequired;
        j["minimumComboHitsRequired"] = e.minimumComboHitsRequired;
        j["fanoutMembers"] = json::array();
        for (const EntityContactFanoutMember& member : e.fanoutMembers) {
            j["fanoutMembers"].push_back(
                EntityContactFanoutMemberToJson(member));
        }
    }
    if (!e.producerLifecycle.empty() || e.producerPattern >= 0 ||
        e.producerPriorPattern >= 0) {
        j["producerLifecycle"] = e.producerLifecycle;
        j["producerPattern"] = e.producerPattern;
        if (e.producerPriorPattern >= 0) {
            j["producerPriorPattern"] = e.producerPriorPattern;
        }
    }
    j["opensAfterAction"] = e.opensAfterAction;
    if (!::Mission::SemanticSourcePolicy::IsLegacyAbsent(
            e.semanticSourceAction, e.semanticSourceMove)) {
        j["semanticSourceAction"] = e.semanticSourceAction;
        j["semanticSourceMove"] = e.semanticSourceMove;
    }
    if (e.contactAfterAction >= 0) {
        j["contactAfterAction"] = e.contactAfterAction;
    }
    j["afterStep"] = e.afterStep;
    if (e.afterStepContact > 0) {
        j["afterStepContact"] = e.afterStepContact;
    }
    if (e.dueBeforeStep >= 0) j["dueBeforeStep"] = e.dueBeforeStep;
    if (e.dueBeforeStep >= 0 && e.dueBeforeStepContact >= 0) {
        j["dueBeforeStepContact"] = e.dueBeforeStepContact;
    }
    if (e.segment > 0) j["segment"] = e.segment;
    if (e.maxDelay > 0) j["maxDelay"] = e.maxDelay;
    if (e.damage > 0) j["damage"] = e.damage;
    if (e.comboEndAfter) j["comboEndAfter"] = true;
    return j;
}

json EntityLifecycleToJson(const EntityLifecycleRequirement& e) {
    json j;
    if (!e.notation.empty()) j["notation"] = e.notation;
    j["owner"] = e.owner;
    j["slot"] = e.slot;
    if (e.generation > 0) j["generation"] = e.generation;
    if (!e.lifecycle.empty()) j["lifecycle"] = e.lifecycle;
    if (e.pattern >= 0) j["pattern"] = e.pattern;
    if (e.priorPattern >= 0) j["priorPattern"] = e.priorPattern;
    j["opensAfterAction"] = e.opensAfterAction;
    if (e.segment > 0) j["segment"] = e.segment;
    if (e.maxDelay > 0) j["maxDelay"] = e.maxDelay;
    return j;
}

bool IsGeneratedRecordingPath(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return name.rfind("recorded_", 0) == 0;
}

bool HasLegacyUngradedLifecycleReview(const Mission& mission) {
    for (const std::string& reason : mission.reviewRequired) {
        int pattern = -1;
        int slot = -1;
        int generation = 0;
        if (::Mission::EntityReviewPolicy::
                ParseLegacyUngradedLifecycleReason(
                    reason, pattern, slot, generation) &&
            !::Mission::EntityReviewPolicy::IsObsoletePostContactReview(
                mission, reason)) {
            return true;
        }
    }
    return false;
}

json PlayerToJson(const PlayerSetup& p) {
    json j;
    if (!p.character.empty()) j["character"] = p.character;
    if (p.hasPos) j["pos"] = { {"x", p.posX}, {"y", p.posY} };
    j["palette"] = p.palette;
    if (p.rf >= 0) j["rf"] = p.rf;
    if (p.meter >= 0) j["meter"] = p.meter;
    if (p.hp >= 0) j["hp"] = p.hp;
    if (p.blueIC >= 0) j["blueIC"] = p.blueIC;
    if (p.guard >= 0) j["guard"] = p.guard;
    if (p.rfLock) j["rfLock"] = true;
    if (!p.resources.empty()) j["resources"] = p.resources;
    return j;
}

json DummyToJson(const DummySetup& d) {
    json j;
    if (!d.character.empty()) j["character"] = d.character;
    if (d.hasPos) j["pos"] = { {"x", d.posX}, {"y", d.posY} };
    j["palette"] = d.palette;
    if (d.rf >= 0) j["rf"] = d.rf;
    if (d.meter >= 0) j["meter"] = d.meter;
    if (d.hp >= 0) j["hp"] = d.hp;
    if (d.blueIC >= 0) j["blueIC"] = d.blueIC;
    if (d.guard >= 0) j["guard"] = d.guard;
    if (d.rfLock) j["rfLock"] = true;
    if (d.crouch) j["crouch"] = true;
    if (d.jump) j["jump"] = true;
    if (d.airtech != "none") j["airtech"] = d.airtech;
    if (!d.resources.empty()) j["resources"] = d.resources;
    return j;
}

json ScoreToJson(const ScoreTier& t) {
    json j;
    if (t.minHits > 0) j["minHits"] = t.minHits;
    if (t.minDamage > 0) j["minDamage"] = t.minDamage;
    if (t.maxAttempts > 0) j["maxAttempts"] = t.maxAttempts;
    if (!t.label.empty()) j["label"] = t.label;
    return j;
}

ContactPredicate ParseContactPredicate(const json& j) {
    ContactPredicate c;
    if (j.is_string()) {
        c.result = j.get<std::string>();
        return c;
    }
    if (!j.is_object()) return c;
    c.attacker = j.value("attacker", std::string("learner"));
    c.target = j.value("target", c.attacker == "learner"
                                     ? std::string("dummy") : std::string("learner"));
    c.source = j.value("source", std::string("direct"));
    c.result = j.value("result", std::string());
    c.targetStateBefore = j.value("targetStateBefore", std::string());
    c.count = j.value("count", 1);
    if (j.contains("moveIds") && j["moveIds"].is_array()) {
        for (const auto& v : j["moveIds"]) {
            if (v.is_number_integer()) c.moveIds.push_back(v.get<int>());
        }
    }
    return c;
}

json ContactPredicateToJson(const ContactPredicate& c) {
    json j = {
        {"attacker", c.attacker}, {"target", c.target},
        {"source", c.source}, {"result", c.result}
    };
    if (!c.moveIds.empty()) j["moveIds"] = c.moveIds;
    if (!c.targetStateBefore.empty()) {
        j["targetStateBefore"] = c.targetStateBefore;
    }
    if (c.count != 1) j["count"] = c.count;
    return j;
}

LessonPage ParseLessonPage(const json& j) {
    LessonPage p;
    p.id = j.value("id", std::string());
    p.title = j.value("title", std::string());
    p.text = j.value("text", std::string());
    p.showHud = j.value("showHud", std::string());
    return p;
}

LessonAction ParseLessonAction(const json& j) {
    LessonAction a;
    a.id = j.value("id", std::string());
    a.notation = j.value("notation", std::string());
    if (j.contains("moveIds") && j["moveIds"].is_array()) {
        for (const auto& v : j["moveIds"]) {
            if (v.is_number_integer()) a.moveIds.push_back(v.get<int>());
        }
    }
    if (j.contains("fromMoveIds") && j["fromMoveIds"].is_array()) {
        for (const auto& v : j["fromMoveIds"]) {
            if (v.is_number_integer()) a.fromMoveIds.push_back(v.get<int>());
        }
    }
    a.inputMask = j.value("inputMask", 0);
    a.req = j.value("req", std::string("move"));
    a.hitsRequired = j.value("hits", 1);
    a.minGap = j.value("minGap", 0);
    a.maxGap = j.value("maxGap", 0);
    if (j.contains("afterWake") && j["afterWake"].is_object()) {
        a.afterWakeMaxTicks = j["afterWake"].value("maxTicks", 0);
    }
    if (j.contains("dummyState")) {
        if (j["dummyState"].is_string()) {
            a.dummyState = j["dummyState"].get<std::string>();
        } else if (j["dummyState"].is_array()) {
            for (const auto& v : j["dummyState"]) {
                if (v.is_number_integer()) a.dummyStateMoveIds.push_back(v.get<int>());
            }
        }
    }
    if (j.contains("withinWhiffWindow") && j["withinWhiffWindow"].is_object()) {
        a.whiffWindowMaxTicks = j["withinWhiffWindow"].value("maxTicks", 0);
    }
    a.duringGap = j.value("duringGap", false);
    if (j.contains("contact") && (j["contact"].is_object() || j["contact"].is_string())) {
        a.hasContact = true;
        a.contact = ParseContactPredicate(j["contact"]);
    }
    if (j.contains("flickerIC") && j["flickerIC"].is_object()) {
        a.hasFlickerIC = true;
        a.flickerProjectilePattern =
            j["flickerIC"].value("projectilePattern", 0);
        a.flickerMinDistance = j["flickerIC"].value("minDistance", 0);
    }
    return a;
}

LessonTask ParseLessonTask(const json& j) {
    LessonTask t;
    t.id = j.value("id", std::string());
    t.kind = j.value("kind", std::string("combat"));
    t.label = j.value("label", std::string());
    t.prompt = j.value("prompt", std::string());
    t.checkpoint = j.value("checkpoint", std::string("lessonStart"));
    t.script = j.value("script", std::string());
    t.continuity = j.value("continuity", std::string("independent"));
    if (j.contains("moveIds") && j["moveIds"].is_array()) {
        for (const auto& v : j["moveIds"]) if (v.is_number_integer()) t.moveIds.push_back(v.get<int>());
    }
    t.inputMask = j.value("inputMask", 0);
    t.req = j.value("req", std::string("move"));
    t.hitsRequired = j.value("hits", 1);
    if (j.contains("sequence") && j["sequence"].is_array()) {
        for (const auto& a : j["sequence"]) {
            if (a.is_object()) t.sequence.push_back(ParseLessonAction(a));
        }
    }
    if (j.contains("options") && j["options"].is_array()) {
        for (const auto& o : j["options"]) {
            ChoiceOption c;
            c.id = o.value("id", std::string());
            c.label = o.value("label", std::string());
            t.options.push_back(std::move(c));
        }
    }
    if (j.contains("acceptedOptionIds") && j["acceptedOptionIds"].is_array()) {
        for (const auto& v : j["acceptedOptionIds"]) if (v.is_string()) t.acceptedOptionIds.push_back(v.get<std::string>());
    }
    if (j.contains("feedback") && j["feedback"].is_object()) {
        const auto& f = j["feedback"];
        t.successText = f.value("success", std::string());
        t.failureText = f.value("failure", std::string());
        for (auto it = f.begin(); it != f.end(); ++it) {
            if (it.key() != "success" && it.key() != "failure" && it.value().is_string()) {
                t.optionFeedback[it.key()] = it.value().get<std::string>();
            }
        }
    }
    t.demo = j.value("demo", std::string());
    if (j.contains("contact") && (j["contact"].is_object() || j["contact"].is_string())) {
        t.hasContact = true;
        t.contact = ParseContactPredicate(j["contact"]);
    }
    t.completionHoldTicks = j.value("completionHoldTicks", -1);
    t.restoreOnSuccess = j.value("restoreOnSuccess", false);
    if (j.contains("afterWake") && j["afterWake"].is_object()) {
        t.afterWakeMaxTicks = j["afterWake"].value("maxTicks", 0);
    }
    if (j.contains("dummyState")) {
        if (j["dummyState"].is_string()) {
            t.dummyState = j["dummyState"].get<std::string>();
        } else if (j["dummyState"].is_array()) {
            for (const auto& v : j["dummyState"]) {
                if (v.is_number_integer()) t.dummyStateMoveIds.push_back(v.get<int>());
            }
        }
    }
    if (j.contains("withinWhiffWindow") && j["withinWhiffWindow"].is_object()) {
        t.whiffWindowMaxTicks = j["withinWhiffWindow"].value("maxTicks", 0);
    }
    t.duringGap = j.value("duringGap", false);
    if (j.contains("absence") && j["absence"].is_object()) {
        const auto& ab = j["absence"];
        t.hasAbsence = true;
        if (ab.contains("forbid")) {
            if (ab["forbid"].is_string()) {
                t.absenceForbid = ab["forbid"].get<std::string>();
            } else if (ab["forbid"].is_array()) {
                for (const auto& v : ab["forbid"]) {
                    if (v.is_number_integer()) t.absenceForbidIds.push_back(v.get<int>());
                }
            }
        }
        t.absenceStart = ab.value("start", std::string("launched"));
        t.absenceFailOnHit = ab.value("failOnHit", false);
        t.absenceMinBlocks = ab.value("minBlocks", 0);
        if (ab.contains("end")) {
            if (ab["end"].is_string()) {
                t.absenceEnd = ab["end"].get<std::string>();
            } else if (ab["end"].is_object()) {
                t.absenceEnd = "ticks";
                t.absenceTicks = ab["end"].value("ticks", 0);
            }
        }
    }
    t.endsCombo = j.value("endsCombo", false);
    if (j.contains("compare") && j["compare"].is_object()) {
        const auto& c = j["compare"];
        t.hasCompare = true;
        t.compareVs = c.value("vs", std::string());
        t.compareMetric = c.value("metric", std::string());
        t.compareOp = c.value("op", std::string());
    }
    if (j.contains("branch") && j["branch"].is_object()) {
        const auto& b = j["branch"];
        t.hasBranch = true;
        if (b.contains("starterMoveIds") && b["starterMoveIds"].is_array()) {
            for (const auto& v : b["starterMoveIds"]) {
                if (v.is_number_integer()) t.branchStarterIds.push_back(v.get<int>());
            }
        }
        if (b.contains("onHit") && b["onHit"].is_array()) {
            for (const auto& a : b["onHit"]) {
                if (a.is_object()) t.branchOnHit.push_back(ParseLessonAction(a));
            }
        }
        t.branchOnBlockHoldTicks = b.value("onBlockHoldTicks", 0);
    }
    if (j.contains("goalState") && j["goalState"].is_object()) {
        const auto& g = j["goalState"];
        t.hasGoalState = true;
        t.goalState.field = g.value("field", std::string());
        t.goalState.player = g.value("player", 1);
        t.goalState.op = g.value("op", std::string("ge"));
        t.goalState.value = g.value("value", 0);
        t.goalState.requiresBlock = g.value("requiresBlock", false);
    }
    if (j.contains("projectileInterception") &&
        j["projectileInterception"].is_object()) {
        const auto& e = j["projectileInterception"];
        t.hasProjectileInterception = true;
        t.incomingProjectilePattern = e.value("incomingPattern", 0);
        t.guardProjectilePattern = e.value("guardPattern", 0);
    }
    if (j.contains("pos") && j["pos"].is_object()) {
        t.hasPos = true;
        t.posX = j["pos"].value("x", 0.0);
        t.posY = j["pos"].value("y", 0.0);
    }
    if (j.contains("dummyPos") && j["dummyPos"].is_object()) {
        t.hasDummyPos = true;
        t.dummyPosX = j["dummyPos"].value("x", 0.0);
        t.dummyPosY = j["dummyPos"].value("y", 0.0);
    }
    if (j.contains("seed") && j["seed"].is_object()) {
        auto parseSeed = [](const json& s, TaskStateSeed& out) {
            out.rf = s.value("rf", -1);
            out.blueIC = s.value("blueIC", -1);
            out.meter = s.value("meter", -1);
            out.hp = s.value("hp", -1);
            out.guard = s.value("guard", -1);
            out.has = out.rf >= 0 || out.blueIC >= 0 ||
                      out.meter >= 0 || out.hp >= 0 || out.guard >= 0;
        };
        const auto& seed = j["seed"];
        if (seed.contains("player") && seed["player"].is_object()) {
            parseSeed(seed["player"], t.playerSeed);
        }
        if (seed.contains("dummy") && seed["dummy"].is_object()) {
            parseSeed(seed["dummy"], t.dummySeed);
        }
    }
    return t;
}

DummyEpisode ParseEpisode(const json& j) {
    DummyEpisode e;
    e.id = j.value("id", std::string());
    e.ownerTask = j.value("ownerTask", std::string());
    e.kind = j.value("kind", std::string("macro"));
    e.clip = j.value("clip", std::string());
    e.action = j.value("action", std::string());
    if (j.contains("expectedMoveIds") && j["expectedMoveIds"].is_array()) {
        for (const auto& id : j["expectedMoveIds"]) {
            if (id.is_number_integer()) e.expectedMoveIds.push_back(id.get<int>());
        }
    }
    e.approach = j.value("approach", std::string("dash"));
    e.telegraph = j.value("telegraph", std::string());
    e.trigger = j.value("trigger", std::string());
    e.reactDelay = j.value("reactDelay", 30);
    if (j.contains("start")) {
        if (j["start"].is_string()) {
            e.start = j["start"].get<std::string>();
        } else if (j["start"].is_object()) {
            e.start = j["start"].value("kind", std::string("playerState"));
            e.predicate = j["start"].value("predicate", std::string());
        }
    }
    e.predicate = j.value("predicate", e.predicate);
    if (j.contains("cue") && j["cue"].is_object()) {
        e.cueText = j["cue"].value("text", std::string());
        e.cueLead = j["cue"].value("lead", 45);
    }
    if (j.contains("repeat") && j["repeat"].is_object()) {
        e.repeatMode = j["repeat"].value("mode", std::string("afterFailure"));
        e.repeatDelay = j["repeat"].value("delay", 120);
    }
    e.pausePolicy = j.value("pausePolicy", std::string("missionFreeze"));
    e.completeOn = j.value("completeOn", std::string("actionEnd"));
    if (j.contains("variants") && j["variants"].is_array()) {
        for (const auto& v : j["variants"]) {
            if (v.is_string()) e.variants.push_back(v.get<std::string>());
        }
    }
    e.variantGroup = j.value("variantGroup", std::string());
    if (j.contains("script") && j["script"].is_array()) {
        for (const auto& st : j["script"]) {
            if (!st.is_object()) continue;
            DummyEpisode::ScriptStep step;
            step.action = st.value("action", std::string());
            step.waitTicks = st.value("waitTicks", 0);
            step.gapTicks = st.value("gapTicks", 0);
            step.cancel = st.value("cancel", false);
            e.script.push_back(step);
        }
    }
    return e;
}

json LessonPageToJson(const LessonPage& p) {
    json j;
    j["id"] = p.id;
    if (!p.title.empty()) j["title"] = p.title;
    j["text"] = p.text;
    if (!p.showHud.empty()) j["showHud"] = p.showHud;
    return j;
}

json LessonActionToJson(const LessonAction& a) {
    json j;
    j["id"] = a.id;
    if (!a.notation.empty()) j["notation"] = a.notation;
    if (!a.moveIds.empty()) j["moveIds"] = a.moveIds;
    if (!a.fromMoveIds.empty()) j["fromMoveIds"] = a.fromMoveIds;
    if (a.inputMask != 0) j["inputMask"] = a.inputMask;
    if (a.req != "move") j["req"] = a.req;
    if (a.hitsRequired > 1) j["hits"] = a.hitsRequired;
    if (a.minGap > 0) j["minGap"] = a.minGap;
    if (a.maxGap > 0) j["maxGap"] = a.maxGap;
    if (a.afterWakeMaxTicks > 0) j["afterWake"] = { {"maxTicks", a.afterWakeMaxTicks} };
    if (!a.dummyState.empty()) j["dummyState"] = a.dummyState;
    else if (!a.dummyStateMoveIds.empty()) j["dummyState"] = a.dummyStateMoveIds;
    if (a.whiffWindowMaxTicks > 0) j["withinWhiffWindow"] = { {"maxTicks", a.whiffWindowMaxTicks} };
    if (a.duringGap) j["duringGap"] = true;
    if (a.hasContact) j["contact"] = ContactPredicateToJson(a.contact);
    if (a.hasFlickerIC) {
        j["flickerIC"] = {
            {"projectilePattern", a.flickerProjectilePattern},
            {"minDistance", a.flickerMinDistance}
        };
    }
    return j;
}

json LessonTaskToJson(const LessonTask& t) {
    json j;
    j["id"] = t.id;
    j["kind"] = t.kind;
    if (!t.label.empty()) j["label"] = t.label;
    if (!t.prompt.empty()) j["prompt"] = t.prompt;
    if (t.checkpoint != "lessonStart") j["checkpoint"] = t.checkpoint;
    if (!t.script.empty()) j["script"] = t.script;
    if (t.continuity != "independent") j["continuity"] = t.continuity;
    if (!t.moveIds.empty()) j["moveIds"] = t.moveIds;
    if (t.inputMask != 0) j["inputMask"] = t.inputMask;
    if (t.req != "move") j["req"] = t.req;
    if (t.hitsRequired > 1) j["hits"] = t.hitsRequired;
    if (!t.sequence.empty()) {
        json actions = json::array();
        for (const auto& a : t.sequence) actions.push_back(LessonActionToJson(a));
        j["sequence"] = actions;
    }
    if (!t.options.empty()) {
        json arr = json::array();
        for (const auto& o : t.options) arr.push_back({ {"id", o.id}, {"label", o.label} });
        j["options"] = arr;
    }
    if (!t.acceptedOptionIds.empty()) j["acceptedOptionIds"] = t.acceptedOptionIds;
    json fb;
    if (!t.successText.empty()) fb["success"] = t.successText;
    if (!t.failureText.empty()) fb["failure"] = t.failureText;
    for (const auto& kv : t.optionFeedback) fb[kv.first] = kv.second;
    if (!fb.empty()) j["feedback"] = fb;
    if (!t.demo.empty()) j["demo"] = t.demo;
    if (t.hasContact) j["contact"] = ContactPredicateToJson(t.contact);
    if (t.completionHoldTicks >= 0) j["completionHoldTicks"] = t.completionHoldTicks;
    if (t.restoreOnSuccess) j["restoreOnSuccess"] = true;
    if (t.afterWakeMaxTicks > 0) j["afterWake"] = { {"maxTicks", t.afterWakeMaxTicks} };
    if (!t.dummyState.empty()) j["dummyState"] = t.dummyState;
    else if (!t.dummyStateMoveIds.empty()) j["dummyState"] = t.dummyStateMoveIds;
    if (t.whiffWindowMaxTicks > 0) j["withinWhiffWindow"] = { {"maxTicks", t.whiffWindowMaxTicks} };
    if (t.duringGap) j["duringGap"] = true;
    if (t.hasAbsence) {
        json ab = json::object();
        if (!t.absenceForbid.empty()) ab["forbid"] = t.absenceForbid;
        else if (!t.absenceForbidIds.empty()) ab["forbid"] = t.absenceForbidIds;
        ab["start"] = t.absenceStart;
        if (t.absenceEnd == "ticks") ab["end"] = { {"ticks", t.absenceTicks} };
        else ab["end"] = t.absenceEnd;
        if (t.absenceFailOnHit) ab["failOnHit"] = true;
        if (t.absenceMinBlocks > 0) ab["minBlocks"] = t.absenceMinBlocks;
        j["absence"] = ab;
    }
    if (t.endsCombo) j["endsCombo"] = true;
    if (t.hasCompare) {
        json c = json::object();
        c["vs"] = t.compareVs;
        c["metric"] = t.compareMetric;
        if (!t.compareOp.empty()) c["op"] = t.compareOp;
        j["compare"] = c;
    }
    if (t.hasBranch) {
        json b = json::object();
        b["starterMoveIds"] = t.branchStarterIds;
        json onHit = json::array();
        for (const auto& a : t.branchOnHit) onHit.push_back(LessonActionToJson(a));
        b["onHit"] = onHit;
        b["onBlockHoldTicks"] = t.branchOnBlockHoldTicks;
        j["branch"] = b;
    }
    if (t.hasGoalState) {
        j["goalState"] = { {"field", t.goalState.field}, {"player", t.goalState.player},
                           {"op", t.goalState.op}, {"value", t.goalState.value} };
        if (t.goalState.requiresBlock) j["goalState"]["requiresBlock"] = true;
    }
    if (t.hasProjectileInterception) {
        j["projectileInterception"] = {
            {"incomingPattern", t.incomingProjectilePattern},
            {"guardPattern", t.guardProjectilePattern}
        };
    }
    if (t.hasPos) j["pos"] = { {"x", t.posX}, {"y", t.posY} };
    if (t.hasDummyPos) {
        j["dummyPos"] = { {"x", t.dummyPosX}, {"y", t.dummyPosY} };
    }
    if (t.playerSeed.has || t.dummySeed.has) {
        auto seedJson = [](const TaskStateSeed& s) {
            json o = json::object();
            if (s.rf >= 0) o["rf"] = s.rf;
            if (s.blueIC >= 0) o["blueIC"] = s.blueIC;
            if (s.meter >= 0) o["meter"] = s.meter;
            if (s.hp >= 0) o["hp"] = s.hp;
            if (s.guard >= 0) o["guard"] = s.guard;
            return o;
        };
        json seed = json::object();
        if (t.playerSeed.has) seed["player"] = seedJson(t.playerSeed);
        if (t.dummySeed.has) seed["dummy"] = seedJson(t.dummySeed);
        j["seed"] = seed;
    }
    return j;
}

json EpisodeToJson(const DummyEpisode& e) {
    json j;
    j["id"] = e.id;
    if (!e.ownerTask.empty()) j["ownerTask"] = e.ownerTask;
    j["kind"] = e.kind;
    if (!e.clip.empty()) j["clip"] = e.clip;
    if (!e.action.empty()) j["action"] = e.action;
    if (!e.expectedMoveIds.empty()) j["expectedMoveIds"] = e.expectedMoveIds;
    if (e.approach != "dash") j["approach"] = e.approach;
    if (!e.telegraph.empty()) j["telegraph"] = e.telegraph;
    if (!e.trigger.empty()) j["trigger"] = e.trigger;
    if (e.reactDelay != 30) j["reactDelay"] = e.reactDelay;
    if (e.predicate.empty()) {
        j["start"] = e.start;
    } else {
        j["start"] = { {"kind", e.start}, {"predicate", e.predicate} };
    }
    if (!e.cueText.empty()) j["cue"] = { {"text", e.cueText}, {"lead", e.cueLead} };
    if (!e.variants.empty()) j["variants"] = e.variants;
    if (!e.variantGroup.empty()) j["variantGroup"] = e.variantGroup;
    if (!e.script.empty()) {
        json arr = json::array();
        for (const auto& st : e.script) {
            json o = json::object();
            if (!st.action.empty()) o["action"] = st.action;
            if (st.waitTicks > 0) o["waitTicks"] = st.waitTicks;
            if (st.gapTicks > 0) o["gapTicks"] = st.gapTicks;
            if (st.cancel) o["cancel"] = true;
            arr.push_back(o);
        }
        j["script"] = arr;
    }
    j["repeat"] = { {"mode", e.repeatMode}, {"delay", e.repeatDelay} };
    if (e.pausePolicy != "missionFreeze") j["pausePolicy"] = e.pausePolicy;
    if (e.completeOn != "actionEnd") j["completeOn"] = e.completeOn;
    return j;
}

}

bool NormalizeFlexibleEntityFanoutEpisodes(Mission& mission,
                                            bool allowInference) {
    if (!allowInference) return false;
    return NormalizeFlexibleEntityFanoutEpisodesImpl(mission);
}

bool ValidateLesson(const Mission& m, std::string& errorOut) {
    if (m.tutorialSchema <= 0) return true;
    auto fail = [&](const std::string& msg) { errorOut = m.lessonId + ": " + msg; return false; };
    if (m.lessonId.empty()) { errorOut = m.sourcePath + ": tutorial lesson missing stable id"; return false; }
    if (!m.hasLesson) return fail("tutorialSchema set but no lesson block");
    if (m.player.rfLock && m.player.rf < 0) {
        return fail("player rfLock needs an authored rf value");
    }
    if (m.dummy.rfLock && m.dummy.rf < 0) {
        return fail("dummy rfLock needs an authored rf value");
    }
    const Lesson& L = m.lesson;
    if (L.completion != "pages" && L.completion != "allTasks" && L.completion != "anyTask") {
        return fail("completion mode must be pages/allTasks/anyTask");
    }
    if (L.requirementPlacement != "upperLeft" &&
        L.requirementPlacement != "belowStats") {
        return fail("requirementPlacement must be upperLeft/belowStats");
    }
    if (L.completion == "pages" && L.pages.empty()) return fail("pages completion with no pages");
    if (L.completion != "pages" && L.tasks.empty()) return fail("task completion with no tasks");
    if (L.wrongAction != "coach" && L.wrongAction != "fail" && L.wrongAction != "ignore") {
        return fail("flow.wrongAction must be coach/fail/ignore");
    }
    if (L.failureReset != "taskCheckpoint" && L.failureReset != "lessonStart") {
        return fail("flow.failureReset must be taskCheckpoint/lessonStart");
    }
    auto validReq = [](const std::string& req) {
        return req == "input" || req == "commit" || req == "move" ||
               req == "land" || req == "hits" || req == "connect" ||
               req == "block" || req == "rg" ||
               req == "projectileInterception";
    };
    auto validContinuity = [](const std::string& continuity) {
        return continuity == "independent" || continuity == "sameCombo" ||
               continuity == "setupGap" || continuity == "newCombo" ||
               continuity == "free" || continuity == "continue";
    };
    auto validateContact = [&](const ContactPredicate& c, const std::string& owner) {
        if (c.attacker != "learner" && c.attacker != "dummy") {
            return fail(owner + " contact attacker must be learner/dummy");
        }
        if (c.target != "learner" && c.target != "dummy") {
            return fail(owner + " contact target must be learner/dummy");
        }
        if (c.attacker == c.target) return fail(owner + " contact attacker and target must differ");
        if (c.source != "direct" && c.source != "entity") {
            return fail(owner + " contact source must be direct/entity");
        }
        if (c.result != "hit" && c.result != "block" &&
            c.result != "recoil_guard" && c.result != "throw" &&
            c.result != "special" && c.result != "guard_point" &&
            c.result != "whiff") {
            return fail(owner + " contact has unknown result '" + c.result + "'");
        }
        if (!c.targetStateBefore.empty() && c.targetStateBefore != "downed") {
            return fail(owner + " contact has unknown targetStateBefore '" +
                        c.targetStateBefore + "'");
        }
        if (c.count < 1) return fail(owner + " contact count must be at least 1");
        if (c.moveIds.empty()) return fail(owner + " contact needs committed moveIds");
        for (int id : c.moveIds) if (id <= 0) return fail(owner + " contact has non-positive move ID");
        return true;
    };
    std::vector<std::string> seen;
    auto unique = [&](const std::string& id, const char* what) {
        if (id.empty()) { errorOut = m.lessonId + ": empty " + std::string(what) + " id"; return false; }
        for (const auto& s : seen) if (s == id) { errorOut = m.lessonId + ": duplicate id '" + id + "'"; return false; }
        seen.push_back(id);
        return true;
    };
    for (const auto& p : L.pages)    if (!unique(p.id, "page")) return false;
    for (const auto& e : L.episodes) if (!unique(e.id, "episode")) return false;
    for (const auto& t : L.tasks) {
        if (!unique(t.id, "task")) return false;
        if (t.kind == "choice") {
            if (t.options.size() < 2 || t.options.size() > 3) return fail("choice task '" + t.id + "' needs 2..3 options");
            if (t.acceptedOptionIds.empty()) return fail("choice task '" + t.id + "' has no accepted options");
            if (t.prompt.empty()) return fail("choice task '" + t.id + "' has no prompt");
            std::vector<std::string> optSeen;
            for (const auto& o : t.options) {
                if (o.id.empty()) return fail("choice task '" + t.id + "' has an empty option id");
                if (o.label.empty()) return fail("choice task '" + t.id + "' option '" + o.id + "' has no label");
                for (const auto& s : optSeen) if (s == o.id) return fail("choice task '" + t.id + "' duplicate option id '" + o.id + "'");
                optSeen.push_back(o.id);
            }
            for (const auto& accepted : t.acceptedOptionIds) {
                if (std::find(optSeen.begin(), optSeen.end(), accepted) == optSeen.end()) {
                    return fail("choice task '" + t.id + "' accepts missing option '" + accepted + "'");
                }
            }
            for (const auto& fb : t.optionFeedback) {
                if (std::find(optSeen.begin(), optSeen.end(), fb.first) == optSeen.end()) {
                    return fail("choice task '" + t.id + "' has feedback for missing option '" + fb.first + "'");
                }
            }
        } else if (t.kind == "combat") {
            if (!validContinuity(t.continuity)) {
                return fail("combat task '" + t.id + "' has unknown continuity '" + t.continuity + "'");
            }
            if (!validReq(t.req)) {
                return fail("combat task '" + t.id + "' has unknown req '" + t.req + "'");
            }
            if (t.hitsRequired < 1) return fail("combat task '" + t.id + "' has hits below 1");
            if (t.inputMask < 0 || t.inputMask > 255) {
                return fail("combat task '" + t.id + "' inputMask is outside 0..255");
            }
            if (!t.sequence.empty() && (!t.moveIds.empty() || t.inputMask != 0)) {
                return fail("combat task '" + t.id + "' cannot mix sequence with task-level moveIds/inputMask");
            }
            if (!t.sequence.empty() && t.hasContact) {
                return fail("combat task '" + t.id + "' cannot mix task contact with sequence actions");
            }
            if (t.completionHoldTicks < -1) {
                return fail("combat task '" + t.id + "' completionHoldTicks must be -1 or greater");
            }
            if (t.afterWakeMaxTicks < 0) {
                return fail("combat task '" + t.id + "' afterWake.maxTicks must be positive");
            }
            auto validDummyState = [&](const std::string& st,
                                       const std::vector<int>& ids,
                                       const std::string& owner) {
                if (!st.empty() && st != "airborne" && st != "downed" &&
                    st != "airtech" && st != "launched" && st != "blockstun") {
                    return fail(owner + " has unknown dummyState '" + st + "'");
                }
                for (int id : ids) {
                    if (id <= 0) return fail(owner + " dummyState has non-positive move ID");
                }
                return true;
            };
            if (!validDummyState(t.dummyState, t.dummyStateMoveIds,
                                 "combat task '" + t.id + "'")) return false;
            if (t.whiffWindowMaxTicks < 0) {
                return fail("combat task '" + t.id + "' withinWhiffWindow.maxTicks must be positive");
            }
            if (t.hasCompare) {
                if (t.compareVs.empty()) {
                    return fail("combat task '" + t.id + "' compare needs a vs task id");
                }
                bool earlier = false;
                for (const auto& prior : L.tasks) {
                    if (&prior == &t) break;
                    if (prior.id == t.compareVs) { earlier = true; break; }
                }
                if (!earlier) {
                    return fail("combat task '" + t.id + "' compare.vs must reference an EARLIER task");
                }
                if (t.compareMetric != "comboHits" && t.compareMetric != "comboDamage" &&
                    t.compareMetric != "ticks" && t.compareMetric != "p2HpDelta" &&
                    t.compareMetric != "p1RfDelta" && t.compareMetric != "p2GuardDelta" &&
                    t.compareMetric != "maxUntech" &&
                    t.compareMetric != "untechTicks") {
                    return fail("combat task '" + t.id + "' compare has unknown metric '" + t.compareMetric + "'");
                }
                if (!t.compareOp.empty() && t.compareOp != "gt" && t.compareOp != "lt" &&
                    t.compareOp != "ge" && t.compareOp != "le") {
                    return fail("combat task '" + t.id + "' compare has unknown op '" + t.compareOp + "'");
                }
            }
            if (t.hasBranch) {
                if (!t.moveIds.empty() || t.inputMask != 0 || !t.sequence.empty() ||
                    t.hasContact || t.hasGoalState || t.hasAbsence ||
                    t.hasProjectileInterception) {
                    return fail("combat task '" + t.id + "' branch cannot mix with other contracts");
                }
                if (t.branchStarterIds.empty()) {
                    return fail("combat task '" + t.id + "' branch needs starterMoveIds");
                }
                for (int id : t.branchStarterIds) {
                    if (id <= 0) return fail("combat task '" + t.id + "' branch has non-positive starter ID");
                }
                if (t.branchOnHit.empty()) {
                    return fail("combat task '" + t.id + "' branch needs an onHit continuation");
                }
                if (t.branchOnBlockHoldTicks < 1) {
                    return fail("combat task '" + t.id + "' branch onBlockHoldTicks must be positive");
                }
                std::vector<std::string> branchIds;
                for (const auto& a : t.branchOnHit) {
                    if (a.id.empty()) return fail("combat task '" + t.id + "' branch action has empty id");
                    if (std::find(branchIds.begin(), branchIds.end(), a.id) != branchIds.end()) {
                        return fail("combat task '" + t.id + "' branch duplicates action '" + a.id + "'");
                    }
                    branchIds.push_back(a.id);
                    if (!validReq(a.req)) {
                        return fail("combat task '" + t.id + "' branch action '" + a.id + "' has unknown req");
                    }
                    if (a.req != "input" && a.moveIds.empty()) {
                        return fail("combat task '" + t.id + "' branch action '" + a.id + "' needs moveIds");
                    }
                    for (int id : a.moveIds) {
                        if (id <= 0) return fail("combat task '" + t.id + "' branch action '" + a.id + "' has non-positive move ID");
                    }
                }
            }
            if (t.endsCombo && (t.hasGoalState || t.hasAbsence || t.hasProjectileInterception)) {
                return fail("combat task '" + t.id + "' endsCombo needs a move/sequence/branch contract");
            }
            if (t.hasAbsence) {
                if (!t.moveIds.empty() || t.inputMask != 0 || !t.sequence.empty() ||
                    t.hasContact || t.hasGoalState || t.hasProjectileInterception) {
                    return fail("combat task '" + t.id + "' absence cannot mix with other contracts");
                }
                if (t.absenceForbid.empty() && t.absenceForbidIds.empty()) {
                    return fail("combat task '" + t.id + "' absence names nothing to forbid");
                }
                if (!t.absenceForbid.empty() && t.absenceForbid != "airtech" &&
                    t.absenceForbid != "attack") {
                    return fail("combat task '" + t.id + "' absence forbid must be airtech/attack or move IDs");
                }
                for (int id : t.absenceForbidIds) {
                    if (id <= 0) return fail("combat task '" + t.id + "' absence has non-positive move ID");
                }
                if (t.absenceStart != "launched" && t.absenceStart != "taskArmed" &&
                    t.absenceStart != "dummyUntech") {
                    return fail("combat task '" + t.id +
                                "' absence start must be launched/taskArmed/dummyUntech");
                }
                if (t.absenceEnd != "grounded" && t.absenceEnd != "ticks" &&
                    t.absenceEnd != "dummyUntechEmpty" &&
                    t.absenceEnd != "dummyAttackEnd" &&
                    t.absenceEnd != "episodeCycleEnd") {
                    return fail("combat task '" + t.id +
                                "' absence end must be grounded/ticks/dummyUntechEmpty/dummyAttackEnd/episodeCycleEnd");
                }
                if (t.absenceEnd == "ticks" && t.absenceTicks < 1) {
                    return fail("combat task '" + t.id + "' absence end.ticks must be positive");
                }
                if (t.absenceMinBlocks < 0) {
                    return fail("combat task '" + t.id + "' absence minBlocks must be nonnegative");
                }
                if (t.absenceEnd == "dummyAttackEnd" &&
                    (t.absenceStart != "taskArmed" || t.script.empty())) {
                    return fail("combat task '" + t.id +
                                "' dummyAttackEnd needs a taskArmed window and scripted dummy attack");
                }
                if (t.absenceEnd == "episodeCycleEnd" &&
                    (t.absenceStart != "taskArmed" || t.script.empty())) {
                    return fail("combat task '" + t.id +
                                "' episodeCycleEnd needs a taskArmed window and scripted dummy episode");
                }
            }
            {
                auto validSeed = [&](const TaskStateSeed& s, const char* side) {
                    if (!s.has) return true;
                    if (s.rf > 1000) {
                        return fail("combat task '" + t.id + "' " + side + " seed rf must be inside 0..1000");
                    }
                    if (s.blueIC > 1) {
                        return fail("combat task '" + t.id + "' " + side + " seed blueIC must be 0 or 1");
                    }
                    if (s.meter > 30000) {
                        return fail("combat task '" + t.id + "' " + side + " seed meter is out of range");
                    }
                    if (s.hp == 0 || s.hp > 99999) {
                        return fail("combat task '" + t.id + "' " + side + " seed hp is out of range");
                    }
                    if (s.guard > 360) {
                        return fail("combat task '" + t.id + "' " + side + " seed guard must be inside 0..360");
                    }
                    return true;
                };
                if (!validSeed(t.playerSeed, "player") ||
                    !validSeed(t.dummySeed, "dummy")) return false;
            }
            if (t.req == "projectileInterception" &&
                !t.hasProjectileInterception) {
                return fail("combat task '" + t.id + "' req=projectileInterception needs projectileInterception");
            }
            if (t.hasProjectileInterception &&
                t.req != "projectileInterception") {
                return fail("combat task '" + t.id + "' projectileInterception needs req=projectileInterception");
            }
            if (t.hasProjectileInterception &&
                (t.incomingProjectilePattern <= 0 ||
                 t.incomingProjectilePattern > 65535 ||
                 t.guardProjectilePattern <= 0 ||
                 t.guardProjectilePattern > 65535)) {
                return fail("combat task '" + t.id + "' projectileInterception patterns must be inside 1..65535");
            }
            if (t.hasContact && !validateContact(t.contact, "combat task '" + t.id + "'")) return false;
            if (t.hasGoalState) {
                if (!::Mission::TutorialStatePolicy::IsKnownField(t.goalState.field)) {
                    return fail("combat task '" + t.id + "' goalState has unknown field '" + t.goalState.field + "'");
                }
                if (!::Mission::TutorialStatePolicy::IsKnownOperator(t.goalState.op)) {
                    return fail("combat task '" + t.id + "' goalState has unknown op '" + t.goalState.op + "'");
                }
                if (t.goalState.player != 1 && t.goalState.player != 2) {
                    return fail("combat task '" + t.id + "' goalState player must be 1 or 2");
                }
                if (t.goalState.requiresBlock && t.goalState.player != 1) {
                    return fail("combat task '" + t.id +
                                "' goalState.requiresBlock only supports the learner");
                }
                if (!t.moveIds.empty() || t.inputMask != 0 || !t.sequence.empty() || t.hasContact) {
                    return fail("combat task '" + t.id + "' goalState cannot mix with a move/input/sequence contract");
                }
            }
            if (!t.hasGoalState && !t.hasContact && !t.hasAbsence && !t.hasBranch && t.sequence.empty() && t.moveIds.empty() && t.inputMask == 0 && m.steps.empty()) {
                return fail("combat task '" + t.id + "' names no committed action and the lesson has no steps");
            }
            if (t.sequence.empty() && t.req == "input" && t.inputMask == 0 && !t.hasGoalState) {
                return fail("combat task '" + t.id + "' req=input needs inputMask");
            }
            if (t.sequence.empty() && t.req == "commit" &&
                (t.inputMask == 0 || t.moveIds.empty()) && !t.hasGoalState) {
                return fail("combat task '" + t.id + "' req=commit needs inputMask and moveIds");
            }
            if (!t.hasGoalState && !t.hasContact && !t.hasAbsence && !t.hasBranch && t.sequence.empty() && t.req != "input" && t.req != "commit" && t.moveIds.empty() && m.steps.empty()) {
                return fail("combat task '" + t.id + "' req=" + t.req + " needs moveIds");
            }
            std::vector<std::string> actionIds;
            for (size_t actionIndex = 0; actionIndex < t.sequence.size(); ++actionIndex) {
                const auto& a = t.sequence[actionIndex];
                if (a.id.empty()) return fail("combat task '" + t.id + "' has sequence action with empty id");
                if (std::find(actionIds.begin(), actionIds.end(), a.id) != actionIds.end()) {
                    return fail("combat task '" + t.id + "' duplicates sequence action '" + a.id + "'");
                }
                actionIds.push_back(a.id);
                if (!validReq(a.req)) return fail("combat task '" + t.id + "' action '" + a.id + "' has unknown req '" + a.req + "'");
                if (a.hitsRequired < 1) return fail("combat task '" + t.id + "' action '" + a.id + "' has hits below 1");
                if (a.inputMask < 0 || a.inputMask > 255) return fail("combat task '" + t.id + "' action '" + a.id + "' inputMask is outside 0..255");
                if (a.minGap < 0 || a.maxGap < 0 || (a.maxGap > 0 && a.minGap > a.maxGap)) {
                    return fail("combat task '" + t.id + "' action '" + a.id + "' has an invalid gap window");
                }
                if (a.afterWakeMaxTicks < 0) {
                    return fail("combat task '" + t.id + "' action '" + a.id + "' afterWake.maxTicks must be positive");
                }
                if (a.whiffWindowMaxTicks < 0) {
                    return fail("combat task '" + t.id + "' action '" + a.id + "' withinWhiffWindow.maxTicks must be positive");
                }
                if (!validDummyState(a.dummyState, a.dummyStateMoveIds,
                                     "combat task '" + t.id + "' action '" + a.id + "'")) return false;
                if (a.req == "input" && a.inputMask == 0) return fail("combat task '" + t.id + "' action '" + a.id + "' req=input needs inputMask");
                if (a.req == "commit" && (a.inputMask == 0 || a.moveIds.empty())) return fail("combat task '" + t.id + "' action '" + a.id + "' req=commit needs inputMask and moveIds");
                if (a.req != "input" && a.moveIds.empty() && !a.hasContact) return fail("combat task '" + t.id + "' action '" + a.id + "' needs moveIds");
                for (int id : a.moveIds) if (id <= 0) return fail("combat task '" + t.id + "' action '" + a.id + "' has non-positive move ID");
                if (actionIndex == 0 && !a.fromMoveIds.empty()) {
                    return fail("combat task '" + t.id + "' first action cannot require a source move");
                }
                for (int id : a.fromMoveIds) {
                    if (id <= 0) return fail("combat task '" + t.id + "' action '" + a.id + "' has non-positive source move ID");
                }
                if (a.hasFlickerIC) {
                    if (a.req != "commit" || a.inputMask == 0 ||
                        a.fromMoveIds.empty()) {
                        return fail("combat task '" + t.id + "' action '" + a.id +
                                    "' flickerIC needs a committed input and source move");
                    }
                    if (a.flickerProjectilePattern <= 0 ||
                        a.flickerProjectilePattern > 65535) {
                        return fail("combat task '" + t.id + "' action '" + a.id +
                                    "' flickerIC projectilePattern must be inside 1..65535");
                    }
                    if (a.flickerMinDistance < 0) {
                        return fail("combat task '" + t.id + "' action '" + a.id +
                                    "' flickerIC minDistance must not be negative");
                    }
                }
                if (a.hasContact && !validateContact(a.contact,
                    "combat task '" + t.id + "' action '" + a.id + "'")) return false;
            }
        } else {
            return fail("task '" + t.id + "' has unknown kind '" + t.kind + "'");
        }
        if (!t.script.empty()) {
            const DummyEpisode* found = nullptr;
            for (const auto& e : L.episodes) if (e.id == t.script) { found = &e; break; }
            if (!found) return fail("task '" + t.id + "' references missing episode '" + t.script + "'");
            if (found->ownerTask != t.id) {
                return fail("task '" + t.id + "' references episode '" + t.script +
                            "' owned by task '" + found->ownerTask + "'");
            }
        }
    }
    for (const auto& e : L.episodes) {
        bool ownerFound = false;
        for (const auto& t : L.tasks) if (t.id == e.ownerTask) { ownerFound = true; break; }
        if (!ownerFound) return fail("episode '" + e.id + "' references missing owner task '" + e.ownerTask + "'");
        if (e.kind != "macro" && e.kind != "idle" &&
            e.kind != "block" && e.kind != "rg" &&
            e.kind != "held" && e.kind != "guard" && e.kind != "cue" &&
            e.kind != "block_answer" && e.kind != "rg_answer" &&
            e.kind != "airtech") {
            return fail("episode '" + e.id + "' has unknown kind '" + e.kind + "'");
        }
        if (e.kind == "held" || e.kind == "guard") {
            if (e.clip.empty()) return fail("episode '" + e.id + "' has no clip");
        }
        if (e.kind == "airtech" &&
            e.clip != "forward" && e.clip != "backward") {
            return fail("episode '" + e.id + "' airtech clip must be forward or backward");
        }
        if ((e.kind == "macro" || e.kind == "block_answer" || e.kind == "rg_answer") &&
            e.script.empty() &&
            !::Mission::TutorialEpisodePolicy::IsInjectableAction(e.action)) {
            return fail("episode '" + e.id + "' has unsupported action '" + e.action + "'");
        }
        if (!e.script.empty()) {
            if (e.kind != "macro") {
                return fail("episode '" + e.id + "' script requires kind macro");
            }
            if (!e.action.empty()) {
                return fail("episode '" + e.id + "' script replaces the single action - leave action empty");
            }
            if (e.start != "taskArmed") {
                return fail("episode '" + e.id + "' script requires taskArmed start");
            }
            if (e.script.front().action.empty()) {
                return fail("episode '" + e.id + "' script must begin with an action step");
            }
            for (size_t si = 0; si < e.script.size(); ++si) {
                const auto& st = e.script[si];
                if (!st.action.empty() && st.gapTicks > 0) {
                    return fail("episode '" + e.id + "' script step mixes action and gap");
                }
                if (st.action.empty() && st.gapTicks <= 0) {
                    return fail("episode '" + e.id + "' script step is neither action nor gap");
                }
                if (st.cancel && (si == 0 || st.action.empty())) {
                    return fail("episode '" + e.id + "' cancel step needs a prior action and its own action");
                }
                if (!st.action.empty() &&
                    !::Mission::TutorialEpisodePolicy::IsSupportedScriptAction(
                        st.action, si)) {
                    return fail("episode '" + e.id + "' script action '" + st.action +
                                "' cannot ride the ordinary injection lanes");
                }
                if (st.waitTicks < 0 || st.gapTicks < 0) {
                    return fail("episode '" + e.id + "' script step has a negative delay");
                }
            }
        }
        if (!e.variants.empty()) {
            bool kindInPool = false;
            for (const auto& v : e.variants) {
                if (v != "idle" && v != "block" && v != "rg") {
                    return fail("episode '" + e.id + "' variant '" + v +
                                "' is not a lease-only kind");
                }
                if (v == e.kind) kindInPool = true;
            }
            if (!kindInPool) {
                return fail("episode '" + e.id + "' kind must be one of its variants");
            }
        }
        if (!e.variantGroup.empty() && e.variants.empty()) {
            return fail("episode '" + e.id + "' variantGroup needs a variants pool");
        }
        if (e.kind == "macro" &&
            (::Mission::TutorialEpisodePolicy::IsDashNormalAction(e.action) ||
             ::Mission::TutorialEpisodePolicy::IsContactChainAction(e.action)) &&
            (e.start != "taskArmed" || e.approach != "none")) {
            return fail("episode '" + e.id + "' scripted action '" + e.action +
                        "' requires taskArmed start and no approach");
        }
        if (!::Mission::TutorialEpisodePolicy::IsSupportedApproach(e.approach)) {
            return fail("episode '" + e.id + "' has unsupported approach '" + e.approach + "'");
        }
        if (!::Mission::TutorialEpisodePolicy::IsSupportedTelegraph(e.telegraph)) {
            return fail("episode '" + e.id + "' has unsupported telegraph '" + e.telegraph + "'");
        }
        if (!e.telegraph.empty()) {
            const bool hasOpener =
                (!e.action.empty() && e.action != "jump") ||
                (!e.script.empty() && !e.script.front().action.empty());
            if (e.kind != "macro" || e.start != "taskArmed" || !hasOpener) {
                return fail("episode '" + e.id +
                            "' telegraph needs a taskArmed macro opener");
            }
        }
        if (e.start != "taskArmed" && e.start != "playerState" && e.start != "trigger") {
            return fail("episode '" + e.id + "' has unknown start '" + e.start + "'");
        }
        if (e.start == "playerState" && e.predicate.empty()) {
            return fail("episode '" + e.id + "' playerState start has no predicate");
        }
        if (e.start == "playerState") {
            int ignoredMove = 0;
            if (!::Mission::TutorialEpisodePolicy::ParseP1MovePredicate(
                    e.predicate, ignoredMove)) {
                return fail("episode '" + e.id + "' has unsupported playerState predicate '" +
                            e.predicate + "'");
            }
        }
        if (e.start == "trigger" && e.trigger.empty()) {
            return fail("episode '" + e.id + "' trigger start has no trigger");
        }
        if (e.start == "trigger" &&
            !::Mission::TutorialEpisodePolicy::IsSupportedTrigger(e.trigger)) {
            return fail("episode '" + e.id + "' has unsupported trigger '" + e.trigger + "'");
        }
        if (!e.expectedMoveIds.empty()) {
            if (e.kind != "macro" || e.action.empty()) {
                return fail("episode '" + e.id +
                            "' expectedMoveIds need a macro action");
            }
            const bool nativeWake =
                ::Mission::TutorialEpisodePolicy::UsesNativeWakeProducer(
                    e.kind, e.start, e.trigger);
            const bool supportedOrdinaryGround = e.start == "taskArmed" &&
                e.action != "jump" && e.action.rfind("j.", 0) != 0 &&
                !::Mission::TutorialEpisodePolicy::IsDashNormalAction(e.action) &&
                !::Mission::TutorialEpisodePolicy::IsContactChainAction(e.action);
            if (!nativeWake && !supportedOrdinaryGround) {
                return fail("episode '" + e.id +
                            "' expectedMoveIds are not supported by this driver");
            }
            std::set<int> uniqueExpected;
            for (int moveId : e.expectedMoveIds) {
                if (moveId < 200 || moveId > 32767 ||
                    !uniqueExpected.insert(moveId).second) {
                    return fail("episode '" + e.id +
                                "' has an invalid expected move ID");
                }
            }
        }
        if (!::Mission::TutorialEpisodePolicy::IsSupportedRepeatMode(e.repeatMode)) {
            return fail("episode '" + e.id + "' has unknown repeat mode '" + e.repeatMode + "'");
        }
        if (e.pausePolicy != "missionFreeze") {
            return fail("episode '" + e.id + "' has unsupported pause policy '" +
                        e.pausePolicy + "'");
        }
        if (e.completeOn != "actionEnd") {
            return fail("episode '" + e.id + "' has unsupported completion event '" +
                        e.completeOn + "'");
        }
        if (e.cueLead < 0 || e.repeatDelay < 0) return fail("episode '" + e.id + "' has a negative timing value");
    }
    std::map<std::string, std::vector<const DummyEpisode*>> variantGroups;
    for (const auto& e : L.episodes) {
        if (!e.variantGroup.empty()) variantGroups[e.variantGroup].push_back(&e);
    }
    for (const auto& group : variantGroups) {
        const auto& members = group.second;
        if (members.size() < 2) {
            return fail("variantGroup '" + group.first + "' needs at least two episodes");
        }
        const auto& expected = members.front()->variants;
        for (const DummyEpisode* member : members) {
            if (member->variants != expected) {
                return fail("variantGroup '" + group.first + "' has mismatched variants pools");
            }
        }
    }
    return true;
}

bool LoadMission(const std::string& path, Mission& out, std::string& errorOut) {
    const std::string text = ReadFile(path);
    if (text.empty()) { errorOut = "cannot open " + path; return false; }
    try {
        const json j = json::parse(text);
        Mission m;
        m.format = j.value("format", 1);
        if (m.format != 1) {
            errorOut = "unsupported mission format " + std::to_string(m.format) +
                " in " + path;
            return false;
        }
        m.id = j.value("id", std::string());
        m.type = j.value("type", std::string("combo"));
        m.category = j.value("category", std::string());
        m.difficulty = j.value("difficulty", 0);
        m.order = j.value("order", 0);
        m.name = j.value("name", std::string());
        m.description = j.value("description", std::string());
        if (j.contains("player")) m.player = ParsePlayer(j["player"]);
        if (j.contains("dummy"))  m.dummy  = ParseDummy(j["dummy"]);
        m.stage = j.value("stage", -1);
        m.bgm = j.value("bgm", -1);
        if (j.contains("bgmKind") && j["bgmKind"].is_string()) {
            const std::string kind = j["bgmKind"].get<std::string>();
            if (kind == "native") m.bgm = -1;
        } else if (m.bgm >= 0 && m.bgm != 150 &&
                   (IsGeneratedRecordingPath(path) || m.bgm > 32)) {
            // Old recorders wrote the DirectSound buffer index to `bgm`; an unmarked recorded_* value is a
            // buffer index (logical tracks 0..32; 150 = OFF).
            m.bgm = -1;
        }
        const bool migrateLegacyRecordedMultiHits =
            IsGeneratedRecordingPath(path) ||
            j.contains("recordingDiagnostics") ||
            (j.value("strictEntityContacts", false) &&
             j.contains("demo") && j.contains("savestate"));
        if (j.contains("steps") && j["steps"].is_array()) {
            for (std::size_t stepIndex = 0; stepIndex < j["steps"].size(); ++stepIndex) {
                const auto& s = j["steps"][stepIndex];
                if (!s.is_object()) {
                    errorOut = "step " + std::to_string(stepIndex) +
                        " is not an object in " + path;
                    return false;
                }
                if (s.contains("contactSource") &&
                    (!s["contactSource"].is_string() ||
                     s["contactSource"].get<std::string>() != "direct")) {
                    errorOut = "step " + std::to_string(stepIndex) +
                        " has unsupported contactSource in " + path;
                    return false;
                }
                if (s.contains("contactResult")) {
                    if (!s["contactResult"].is_string() ||
                        !s.contains("contactSource") ||
                        !::Mission::Contact::ValidResultRequirement(
                            s["contactResult"].get<std::string>()) ||
                        s["contactResult"].get<std::string>() == "whiff") {
                        errorOut = "step " + std::to_string(stepIndex) +
                            " has unsupported contactResult in " + path;
                        return false;
                    }
                }
                if (s.contains("expectedAttackMask") &&
                    (!s["expectedAttackMask"].is_number_integer() ||
                     !::Mission::SequencePolicy::ValidExpectedAttackMask(
                         s["expectedAttackMask"].get<int>()))) {
                    errorOut = "step " + std::to_string(stepIndex) +
                        " has invalid expectedAttackMask (A/B/C/D bits only) in " +
                        path;
                    return false;
                }
                if (s.contains("allowPartialHits") &&
                    !s["allowPartialHits"].is_boolean()) {
                    errorOut = "step " + std::to_string(stepIndex) +
                        " has invalid allowPartialHits in " + path;
                    return false;
                }
                if (s.contains("entityCommand")) {
                    std::string entityCommandError;
                    if (!ValidateEntityCommandOriginJson(
                            s["entityCommand"], entityCommandError)) {
                        errorOut = "step " + std::to_string(stepIndex) +
                            " entityCommand " + entityCommandError + " in " +
                            path;
                        return false;
                    }
                }
                Step parsed = ParseStep(s);
                if (!s.contains("allowPartialHits") &&
                    migrateLegacyRecordedMultiHits && parsed.directContact &&
                    parsed.req == StepReq::Hits && parsed.hitsRequired > 1) {
                    parsed.allowPartialHits = true;
                }
                m.steps.push_back(std::move(parsed));
            }
        }
        m.strictEntityContacts = j.value("strictEntityContacts", false);
        if (j.contains("entityContacts")) {
            if (!j["entityContacts"].is_array()) {
                errorOut = "entityContacts is not an array in " + path;
                return false;
            }
            for (std::size_t contactIndex = 0;
                 contactIndex < j["entityContacts"].size(); ++contactIndex) {
                const auto& contact = j["entityContacts"][contactIndex];
                if (!contact.is_object()) {
                    errorOut = "entity contact " + std::to_string(contactIndex) +
                        " is not an object in " + path;
                    return false;
                }
                std::string fanoutError;
                if (!ValidateEntityContactFanoutJson(contact, fanoutError)) {
                    errorOut = "entity contact " +
                        std::to_string(contactIndex) + " " + fanoutError +
                        " in " + path;
                    return false;
                }
                std::string semanticSourceError;
                if (!ValidateEntityContactSemanticSourceJson(
                        contact, semanticSourceError)) {
                    errorOut = "entity contact " +
                        std::to_string(contactIndex) + " " +
                        semanticSourceError + " in " + path;
                    return false;
                }
                EntityContactRequirement parsed =
                    ParseEntityContact(contact);
                if (::Mission::SemanticSourcePolicy::IsExact(
                        parsed.semanticSourceAction,
                        parsed.semanticSourceMove)) {
                    if (parsed.semanticSourceAction >=
                            static_cast<int>(m.steps.size())) {
                        errorOut = "entity contact " +
                            std::to_string(contactIndex) +
                            " has semantic source action outside the recipe in " +
                            path;
                        return false;
                    }
                    const Step& source = m.steps[static_cast<std::size_t>(
                        parsed.semanticSourceAction)];
                    if (std::find(source.moveIds.begin(), source.moveIds.end(),
                                  parsed.semanticSourceMove) ==
                        source.moveIds.end()) {
                        errorOut = "entity contact " +
                            std::to_string(contactIndex) +
                            " has semantic source move outside its action in " +
                            path;
                        return false;
                    }
                    if ((parsed.opensAfterAction >= 0 &&
                         parsed.semanticSourceAction >
                             parsed.opensAfterAction) ||
                        (parsed.contactAfterAction >= 0 &&
                         parsed.semanticSourceAction >
                             parsed.contactAfterAction)) {
                        errorOut = "entity contact " +
                            std::to_string(contactIndex) +
                            " has semantic source after its lifecycle/contact gate in " +
                            path;
                        return false;
                    }
                }
                m.entityContacts.push_back(std::move(parsed));
            }
        }
        if (j.contains("entityLifecycles")) {
            if (!j["entityLifecycles"].is_array()) {
                errorOut = "entityLifecycles is not an array in " + path;
                return false;
            }
            for (std::size_t lifecycleIndex = 0;
                 lifecycleIndex < j["entityLifecycles"].size();
                 ++lifecycleIndex) {
                const auto& lifecycle =
                    j["entityLifecycles"][lifecycleIndex];
                if (!lifecycle.is_object()) {
                    errorOut = "entity lifecycle " +
                        std::to_string(lifecycleIndex) +
                        " is not an object in " + path;
                    return false;
                }
                std::string lifecycleError;
                if (!ValidateEntityLifecycleJson(lifecycle, lifecycleError)) {
                    errorOut = "entity lifecycle " +
                        std::to_string(lifecycleIndex) + " " +
                        lifecycleError + " in " + path;
                    return false;
                }
                m.entityLifecycles.push_back(
                    ParseEntityLifecycle(lifecycle));
            }
        }
        NormalizeFlexibleEntityContactBarriers(m);
        PromoteLegacySemanticSources(m);
        m.demo = j.value("demo", std::string());
        m.savestate = j.value("savestate", std::string());
        m.failTimer = j.value("failTimer", j.value("fail_timer", 60));
        if (j.contains("score") && j["score"].is_array()) {
            for (const auto& s : j["score"]) m.scores.push_back(ParseScore(s));
        }
        if (j.contains("hint") && j["hint"].is_array()) {
            for (const auto& h : j["hint"]) if (h.is_string()) m.hints.push_back(h.get<std::string>());
        }
        if (j.contains("reviewRequired")) {
            if (!j["reviewRequired"].is_array()) {
                errorOut = "reviewRequired is not an array in " + path;
                return false;
            }
            for (const auto& item : j["reviewRequired"]) {
                if (!item.is_string() || item.get<std::string>().empty()) {
                    errorOut = "reviewRequired contains an invalid reason in " + path;
                    return false;
                }
                m.reviewRequired.push_back(item.get<std::string>());
            }
        }
        if (j.contains("recordingDiagnostics")) {
            if (!j["recordingDiagnostics"].is_array()) {
                errorOut = "recordingDiagnostics is not an array in " + path;
                return false;
            }
            for (const auto& item : j["recordingDiagnostics"]) {
                if (!item.is_string() || item.get<std::string>().empty()) {
                    errorOut = "recordingDiagnostics contains an invalid entry in " + path;
                    return false;
                }
                m.recordingDiagnostics.push_back(item.get<std::string>());
            }
        }
        if (!HasLegacyUngradedLifecycleReview(m)) {
            NormalizeFlexibleEntityFanoutEpisodes(
                m, IsGeneratedRecordingPath(path));
        }
        m.tutorialSchema = j.value("tutorialSchema", 0);
        m.lessonId = m.id;
        m.revision = j.value("revision", 1);
        m.summary = j.value("summary", std::string());
        if (j.contains("sourceRefs") && j["sourceRefs"].is_array()) {
            for (const auto& v : j["sourceRefs"]) if (v.is_string()) m.sourceRefs.push_back(v.get<std::string>());
        }
        if (j.contains("requires") && j["requires"].is_array()) {
            for (const auto& v : j["requires"]) if (v.is_string()) m.requires.push_back(v.get<std::string>());
        }
        m.requiresExactBaseline = j.value("requiresExactBaseline", false);
        if (j.contains("lesson") && j["lesson"].is_object()) {
            const auto& lj = j["lesson"];
            m.hasLesson = true;
            m.lesson.completion = lj.value("completion", std::string());
            m.lesson.requirementPlacement =
                lj.value("requirementPlacement", std::string("upperLeft"));
            if (lj.contains("flow") && lj["flow"].is_object()) {
                m.lesson.wrongAction = lj["flow"].value("wrongAction", std::string("coach"));
                m.lesson.failureReset = lj["flow"].value("failureReset", std::string("taskCheckpoint"));
                m.lesson.preserveCompletedTasks = lj["flow"].value("preserveCompletedTasks", true);
            }
            if (lj.contains("pages") && lj["pages"].is_array()) {
                for (const auto& p : lj["pages"]) m.lesson.pages.push_back(ParseLessonPage(p));
            }
            if (lj.contains("tasks") && lj["tasks"].is_array()) {
                for (const auto& tk : lj["tasks"]) m.lesson.tasks.push_back(ParseLessonTask(tk));
            }
            if (lj.contains("dummyScript") && lj["dummyScript"].is_object() &&
                lj["dummyScript"].contains("episodes") && lj["dummyScript"]["episodes"].is_array()) {
                for (const auto& e : lj["dummyScript"]["episodes"]) m.lesson.episodes.push_back(ParseEpisode(e));
            }
        }
        if (j.contains("next") && j["next"].is_object()) {
            m.nextLessonId = j["next"].value("lessonId", std::string());
        }
        if (j.contains("recommendedAfter") && j["recommendedAfter"].is_array()) {
            for (const auto& v : j["recommendedAfter"]) if (v.is_string()) m.recommendedAfter.push_back(v.get<std::string>());
        }
        if (m.tutorialSchema > 0) {
            std::string verr;
            if (!ValidateLesson(m, verr)) { errorOut = verr; return false; }
        }
        m.sourcePath = path;
        out = std::move(m);
        return true;
    } catch (const std::exception& e) {
        errorOut = std::string("JSON error in ") + path + ": " + e.what();
        return false;
    }
}

bool SaveMission(const std::string& path, const Mission& m, std::string& errorOut) {
    try {
        for (std::size_t stepIndex = 0; stepIndex < m.steps.size(); ++stepIndex) {
            if (!::Mission::SequencePolicy::ValidExpectedAttackMask(
                    m.steps[stepIndex].expectedAttackMask)) {
                errorOut = "step " + std::to_string(stepIndex) +
                    " has invalid expectedAttackMask (A/B/C/D bits only)";
                return false;
            }
            if (m.steps[stepIndex].allowPartialHits &&
                (!m.steps[stepIndex].directContact ||
                 m.steps[stepIndex].req != StepReq::Hits ||
                 m.steps[stepIndex].hitsRequired <= 1)) {
                errorOut = "step " + std::to_string(stepIndex) +
                    " has allowPartialHits without a direct multi-hit requirement";
                return false;
            }
            if (m.steps[stepIndex].entityCommand.present) {
                const EntityCommandOrigin& command =
                    m.steps[stepIndex].entityCommand;
                const json encoded = {
                    {"slot", command.slot},
                    {"generation", command.generation},
                    {"rootPattern", command.rootPattern},
                    {"activationPattern", command.activationPattern}
                };
                std::string entityCommandError;
                if (!ValidateEntityCommandOriginJson(encoded,
                                                     entityCommandError)) {
                    errorOut = "step " + std::to_string(stepIndex) +
                        " entityCommand " + entityCommandError;
                    return false;
                }
            }
        }
        json j;
        j["format"] = m.format;
        if (!m.id.empty()) j["id"] = m.id;
        j["type"] = m.type;
        if (!m.category.empty()) j["category"] = m.category;
        if (m.difficulty > 0) j["difficulty"] = m.difficulty;
        if (m.order > 0) j["order"] = m.order;
        j["name"] = m.name;
        if (!m.description.empty()) j["description"] = m.description;
        j["player"] = PlayerToJson(m.player);
        j["dummy"] = DummyToJson(m.dummy);
        if (m.stage >= 0) j["stage"] = m.stage;
        if (m.bgm >= 0) {
            j["bgm"] = m.bgm;
            j["bgmKind"] = "track";
        }
        j["steps"] = json::array();
        for (const Step& s : m.steps) j["steps"].push_back(StepToJson(s));
        if (!m.entityContacts.empty() || !m.entityLifecycles.empty()) {
            j["strictEntityContacts"] = m.strictEntityContacts;
        }
        if (!m.entityContacts.empty()) {
            j["entityContacts"] = json::array();
            for (const EntityContactRequirement& e : m.entityContacts) {
                if (!::Mission::SemanticSourcePolicy::ValidPersistedPair(
                        e.semanticSourceAction, e.semanticSourceMove)) {
                    errorOut = "entity contact " +
                        std::to_string(j["entityContacts"].size()) +
                        " has invalid semantic source provenance";
                    return false;
                }
                if (::Mission::SemanticSourcePolicy::IsExact(
                        e.semanticSourceAction, e.semanticSourceMove)) {
                    if (e.semanticSourceAction >=
                            static_cast<int>(m.steps.size()) ||
                        std::find(
                            m.steps[static_cast<std::size_t>(
                                e.semanticSourceAction)].moveIds.begin(),
                            m.steps[static_cast<std::size_t>(
                                e.semanticSourceAction)].moveIds.end(),
                            e.semanticSourceMove) ==
                            m.steps[static_cast<std::size_t>(
                                e.semanticSourceAction)].moveIds.end() ||
                        (e.opensAfterAction >= 0 &&
                         e.semanticSourceAction > e.opensAfterAction) ||
                        (e.contactAfterAction >= 0 &&
                         e.semanticSourceAction > e.contactAfterAction)) {
                        errorOut = "entity contact " +
                            std::to_string(j["entityContacts"].size()) +
                            " has semantic source outside its exact action/gates";
                        return false;
                    }
                }
                json encoded = EntityContactToJson(e);
                std::string fanoutError;
                if (!ValidateEntityContactFanoutJson(encoded,
                                                     fanoutError)) {
                    errorOut = "entity contact " +
                        std::to_string(j["entityContacts"].size()) + " " +
                        fanoutError;
                    return false;
                }
                std::string semanticSourceError;
                if (!ValidateEntityContactSemanticSourceJson(
                        encoded, semanticSourceError)) {
                    errorOut = "entity contact " +
                        std::to_string(j["entityContacts"].size()) + " " +
                        semanticSourceError;
                    return false;
                }
                j["entityContacts"].push_back(std::move(encoded));
            }
        }
        if (!m.entityLifecycles.empty()) {
            j["entityLifecycles"] = json::array();
            for (const EntityLifecycleRequirement& e : m.entityLifecycles) {
                j["entityLifecycles"].push_back(
                    EntityLifecycleToJson(e));
            }
        }
        if (m.tutorialSchema > 0) {
            j["tutorialSchema"] = m.tutorialSchema;
            j["id"] = m.lessonId.empty() ? m.id : m.lessonId;
            j["revision"] = m.revision;
            if (!m.summary.empty()) j["summary"] = m.summary;
            if (!m.sourceRefs.empty()) j["sourceRefs"] = m.sourceRefs;
            if (!m.requires.empty()) j["requires"] = m.requires;
            if (m.requiresExactBaseline) j["requiresExactBaseline"] = true;
            if (m.hasLesson) {
                json lj;
                lj["completion"] = m.lesson.completion;
                if (m.lesson.requirementPlacement != "upperLeft") {
                    lj["requirementPlacement"] = m.lesson.requirementPlacement;
                }
                lj["flow"] = { {"wrongAction", m.lesson.wrongAction},
                               {"failureReset", m.lesson.failureReset},
                               {"preserveCompletedTasks", m.lesson.preserveCompletedTasks} };
                json pages = json::array();
                for (const auto& p : m.lesson.pages) pages.push_back(LessonPageToJson(p));
                if (!pages.empty()) lj["pages"] = pages;
                json tasks = json::array();
                for (const auto& tk : m.lesson.tasks) tasks.push_back(LessonTaskToJson(tk));
                if (!tasks.empty()) lj["tasks"] = tasks;
                if (!m.lesson.episodes.empty()) {
                    json eps = json::array();
                    for (const auto& e : m.lesson.episodes) eps.push_back(EpisodeToJson(e));
                    lj["dummyScript"] = { {"episodes", eps} };
                }
                j["lesson"] = lj;
            }
            if (!m.nextLessonId.empty()) j["next"] = { {"lessonId", m.nextLessonId} };
            if (!m.recommendedAfter.empty()) j["recommendedAfter"] = m.recommendedAfter;
        }
        if (!m.demo.empty()) j["demo"] = m.demo;
        if (!m.savestate.empty()) j["savestate"] = m.savestate;
        j["failTimer"] = m.failTimer;
        j["score"] = json::array();
        for (const ScoreTier& t : m.scores) j["score"].push_back(ScoreToJson(t));
        if (!m.hints.empty()) j["hint"] = m.hints;
        if (!m.reviewRequired.empty()) j["reviewRequired"] = m.reviewRequired;
        if (!m.recordingDiagnostics.empty()) {
            j["recordingDiagnostics"] = m.recordingDiagnostics;
        }
        if (!WriteFile(path, j.dump(4))) { errorOut = "cannot write " + path; return false; }
        return true;
    } catch (const std::exception& e) {
        errorOut = std::string("JSON write error: ") + e.what();
        return false;
    }
}

bool LoadPack(const std::string& path, Pack& out, std::string& errorOut) {
    const std::string text = ReadFile(path);
    if (text.empty()) { errorOut = "cannot open " + path; return false; }
    try {
        const json j = json::parse(text);
        Pack p;
        p.format = j.value("format", 1);
        p.id = j.value("id", std::string());
        p.name = j.value("name", std::string());
        p.author = j.value("author", std::string());
        p.version = j.value("version", std::string());
        p.character = j.value("character", std::string());
        p.description = j.value("description", std::string());
        p.editable = j.value("editable", false);
        p.curriculumRevision = j.value("curriculumRevision", 0);
        if (j.contains("categories") && j["categories"].is_array()) {
            for (const auto& c : j["categories"]) {
                PackCategory cat;
                cat.id = c.value("id", std::string());
                cat.label = c.value("label", std::string());
                cat.description = c.value("description", std::string());
                cat.order = c.value("order", 0);
                p.categories.push_back(std::move(cat));
            }
        }
        if (j.contains("scenarios") && j["scenarios"].is_array()) {
            for (const auto& s : j["scenarios"]) {
                Scenario sc;
                sc.name = s.value("name", std::string());
                sc.file = s.value("file", std::string());
                sc.description = s.value("description", std::string());
                sc.category = s.value("category", std::string());
                sc.preview = s.value("preview", std::string());
                sc.locked = s.value("locked", s.value("may_be_locked", false));
                sc.id = s.value("id", std::string());
                sc.order = s.value("order", 0);
                sc.difficulty = s.value("difficulty", 0);
                sc.next = s.value("next", std::string());
                if (s.contains("recommendedAfter") && s["recommendedAfter"].is_array()) {
                    for (const auto& v : s["recommendedAfter"]) {
                        if (v.is_string()) sc.recommendedAfter.push_back(v.get<std::string>());
                    }
                }
                p.scenarios.push_back(sc);
            }
        }
        const size_t slash = path.find_last_of("\\/");
        p.folderPath = (slash == std::string::npos) ? std::string() : path.substr(0, slash);
        out = std::move(p);
        return true;
    } catch (const std::exception& e) {
        errorOut = std::string("JSON error in ") + path + ": " + e.what();
        return false;
    }
}

bool SavePack(const std::string& path, const Pack& p, std::string& errorOut) {
    try {
        json j;
        j["format"] = p.format;
        if (!p.id.empty()) j["id"] = p.id;
        j["name"] = p.name;
        if (!p.author.empty()) j["author"] = p.author;
        if (!p.version.empty()) j["version"] = p.version;
        if (!p.character.empty()) j["character"] = p.character;
        if (!p.description.empty()) j["description"] = p.description;
        if (p.editable) j["editable"] = true;
        if (p.curriculumRevision > 0) j["curriculumRevision"] = p.curriculumRevision;
        if (!p.categories.empty()) {
            json cats = json::array();
            for (const PackCategory& c : p.categories) {
                json cj;
                cj["id"] = c.id;
                cj["label"] = c.label;
                if (!c.description.empty()) cj["description"] = c.description;
                cj["order"] = c.order;
                cats.push_back(cj);
            }
            j["categories"] = cats;
        }
        j["scenarios"] = json::array();
        for (const Scenario& s : p.scenarios) {
            json sj;
            sj["name"] = s.name;
            sj["file"] = s.file;
            if (!s.description.empty()) sj["description"] = s.description;
            if (!s.category.empty()) sj["category"] = s.category;
            if (!s.preview.empty()) sj["preview"] = s.preview;
            if (s.locked) sj["locked"] = true;
            if (!s.id.empty()) sj["id"] = s.id;
            if (s.order > 0) sj["order"] = s.order;
            if (s.difficulty > 0) sj["difficulty"] = s.difficulty;
            if (!s.next.empty()) sj["next"] = s.next;
            if (!s.recommendedAfter.empty()) sj["recommendedAfter"] = s.recommendedAfter;
            j["scenarios"].push_back(sj);
        }
        if (!WriteFile(path, j.dump(4))) { errorOut = "cannot write " + path; return false; }
        return true;
    } catch (const std::exception& e) {
        errorOut = std::string("JSON write error: ") + e.what();
        return false;
    }
}

std::string ResolveMissionsRoot() {
    HMODULE mod = GetModuleHandleA("efz_training_mode.dll");
    char path[MAX_PATH] = {0};
    if (!mod || GetModuleFileNameA(mod, path, MAX_PATH) == 0) return std::string();
    std::string dir(path);
    const size_t slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) return std::string();
    dir.resize(slash + 1);
    return dir + "assets\\missions";
}

std::vector<std::string> DiscoverPackJsonPaths(const std::string& rootDir) {
    std::vector<std::string> result;
    if (rootDir.empty()) return result;
    const std::string search = rootDir + "\\*";
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return result;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        const std::string packJson = rootDir + "\\" + fd.cFileName + "\\pack.json";
        if (GetFileAttributesA(packJson.c_str()) != INVALID_FILE_ATTRIBUTES) {
            result.push_back(packJson);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(result.begin(), result.end(), [](const std::string& lhs,
                                                const std::string& rhs) {
        return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
    });
    return result;
}

}
