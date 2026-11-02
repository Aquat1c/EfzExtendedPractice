#include "game/mission/mission_legacy_entity_migration.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

void LogOut(const std::string&, bool) {}

namespace {

constexpr const char* kLegacyReview =
    "attack entity setup #404 (slot 0, generation 1) had no gradeable "
    "contact; author a lifecycle objective or retake it";
constexpr const char* kMarkerV3 = "runtime:entity-contact-schedule-v3";
constexpr const char* kMarkerV4 = "runtime:entity-lifecycle-schedule-v4";
constexpr const char* kMarkerV5 = "runtime:entity-fanout-schedule-v5";
constexpr const char* kSnowbunnyMismatchReview =
    "entity contacts need review: entity contact pattern has no "
    "contact-linked lifecycle transition";

void Check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string ReadAll(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

void WriteAll(const fs::path& path, const std::string& value) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    stream.flush();
    Check(stream.good(), "could not write " + path.string());
}

struct TempTree {
    fs::path root;

    TempTree() {
        root = fs::temp_directory_path() /
            ("efz-legacy-entity-migration-" +
             std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(GetTickCount()));
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "_recorded", ignored);
        Check(!ignored, "could not create migration test directory");
    }

    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

Mission::Mission LegacyMission(bool include404Contact = false,
                               int firstContactAfterAction = 2) {
    Mission::Mission mission;
    mission.format = 1;
    mission.player.character = "minagi";
    mission.dummy.character = "akane";
    mission.savestate = "exact-state";
    mission.strictEntityContacts = true;
    mission.reviewRequired = {kLegacyReview, kMarkerV3};
    mission.steps.resize(12);
    for (std::size_t i = 0; i < mission.steps.size(); ++i) {
        mission.steps[i].moveIds = {100 + static_cast<int>(i)};
        mission.steps[i].expectedAttackMask = 1;
    }
    mission.steps[2].moveIds = {254};  // Minagi 236B
    mission.steps[11].moveIds = {253}; // Minagi 236A

    Mission::EntityContactRequirement firstPart;
    firstPart.notation = "236B (HIT)";
    firstPart.owner = 1;
    firstPart.target = 2;
    firstPart.slot = 0;
    firstPart.generation = 1;
    firstPart.patterns = {405};
    firstPart.result = "hit";
    firstPart.contactsRequired = 1;
    firstPart.producerLifecycle = "morph";
    firstPart.producerPattern = 405;
    firstPart.producerPriorPattern = 400;
    firstPart.opensAfterAction = 2;
    firstPart.contactAfterAction = firstContactAfterAction;
    firstPart.comboEndAfter = true;
    mission.entityContacts.push_back(firstPart);

    if (include404Contact) {
        Mission::EntityContactRequirement finalContact;
        finalContact.notation = "236A (HIT)";
        finalContact.owner = 1;
        finalContact.target = 2;
        finalContact.slot = 0;
        finalContact.generation = 1;
        finalContact.patterns = {404};
        finalContact.result = "hit";
        finalContact.contactsRequired = 1;
        finalContact.producerLifecycle = "morph";
        finalContact.producerPattern = 404;
        finalContact.producerPriorPattern = 400;
        finalContact.opensAfterAction = 11;
        finalContact.contactAfterAction = 11;
        mission.entityContacts.push_back(finalContact);
    }
    return mission;
}

json Metadata(int eventCount, bool reliable) {
    return {
        {"record", "metadata"},
        {"schema", "efz_recorder_entity_trace"},
        {"version", 5},
        {"authoring_only", true},
        {"sampled", true},
        {"unordered", true},
        {"non_strict", true},
        {"slotCapacity", 64},
        {"playersSampled", 2},
        {"effectiveFrameUnit", "freeze-excluded EFZ internal ticks"},
        {"p1CharacterId", 12},
        {"p1Resource", "minagi"},
        {"p2CharacterId", 4},
        {"p2Resource", "akane"},
        {"eventCount", eventCount},
        {"droppedEvents", reliable ? 0 : 1},
        {"entityContactHookObserved", true},
        {"entityContactHookComplete", true},
        {"contactEpochDiscontinuity", false},
        {"directContactHookObserved", true},
        {"contactJournalOverflow", false},
        {"p1EntityProbeIncomplete", false},
        {"p2EntityProbeIncomplete", false},
        {"p1AllocationCursorAmbiguous", false},
        {"p2AllocationCursorAmbiguous", false},
        // The compiled #405 entity hit sets this routing flag; not an integrity error.
        {"requiresEntityAttributionReview", true},
        {"containsUnclassifiedContact", false},
        {"usedLegacyContactAttribution", false},
    };
}

json ActionRow(int step, int move, int effectiveFrame) {
    return {
        {"record", "action_order"},
        {"step", step},
        {"move", move},
        {"expectedAttackMask", 1},
        {"effectiveFrame", effectiveFrame},
        {"battleBatch", 1000 + step},
        {"actionOrder", step + 1},
    };
}

json LifecycleRow(const char* event, int pattern, int priorPattern,
                  int effectiveFrame, int afterStep, bool attack) {
    return {
        {"record", "entity_lifecycle"},
        {"event", event},
        {"evidence", "ring_sample"},
        {"authoring_only", true},
        {"sampled", true},
        {"unordered", true},
        {"non_strict", true},
        {"effectiveFrame", effectiveFrame},
        {"characterId", 12},
        {"resource", "minagi"},
        {"player", 1},
        {"slot", 0},
        {"generation", 1},
        {"pattern", pattern},
        {"priorPattern", priorPattern < 0 ? json(nullptr) : json(priorPattern)},
        {"class", attack ? "projectile" : "state"},
        {"attack", attack},
        {"observedAfterStep", afterStep},
        {"observedAfterActionOrder", afterStep + 1},
    };
}

json ContactRow(int pattern, int effectiveFrame, int afterStep,
                bool comboEndAfter = false) {
    return {
        {"record", "contact"},
        {"event", "contact"},
        {"evidence", "resolver_hook"},
        {"authoring_only", true},
        {"sampled", false},
        {"unordered", false},
        {"non_strict", true},
        {"effectiveFrame", effectiveFrame},
        {"resource", "minagi"},
        {"player", 1},
        {"slot", 0},
        {"generation", 1},
        {"pattern", pattern},
        {"observedAfterStep", afterStep},
        {"observedAfterActionOrder", afterStep + 1},
        {"source", "entity"},
        {"defender", 2},
        {"result", "hit"},
        {"comboEndAfter", comboEndAfter},
    };
}

struct FixtureFiles {
    fs::path mission;
    fs::path sidecar;
    std::string missionBytes;
    std::string sidecarBytes;
};

FixtureFiles WriteFixture(const TempTree& tree, const std::string& stem,
                          const Mission::Mission& mission,
                          bool linked404Contact, bool reliableMetadata,
                          int firstContactAfterAction = 2) {
    FixtureFiles files;
    files.mission = tree.root / "_recorded" / ("recorded_" + stem + ".json");
    files.sidecar = tree.root / "_recorded" /
        ("recorded_" + stem + ".entities.jsonl");
    files.missionBytes =
        "{\r\n  \"source_must_remain\": \"byte-for-byte\"\r\n}\r\n";

    std::vector<json> trace;
    trace.push_back(LifecycleRow("baseline", 400, -1, 0, -1, false));
    trace.push_back(LifecycleRow("morph", 405, 400, 34, 2, true));
    const int firstContactFrame = firstContactAfterAction == 2
        ? 36 : firstContactAfterAction * 16 + 4;
    trace.push_back(ContactRow(405, firstContactFrame,
                               firstContactAfterAction, true));
    trace.push_back(LifecycleRow("morph", 400, 405,
                                 firstContactFrame + 2,
                                 firstContactAfterAction, false));
    trace.push_back(LifecycleRow("morph", 404, 400, 202, 11, true));
    if (linked404Contact) trace.push_back(ContactRow(404, 203, 11));

    std::ostringstream sidecar;
    sidecar << Metadata(static_cast<int>(trace.size()), reliableMetadata).dump()
            << '\n';
    for (std::size_t i = 0; i < mission.steps.size(); ++i) {
        const int frame = i == 11 ? 193 : static_cast<int>(i) * 16;
        sidecar << ActionRow(
            static_cast<int>(i), mission.steps[i].moveIds.front(), frame).dump()
                << '\n';
    }
    for (const json& row : trace) sidecar << row.dump() << '\n';
    files.sidecarBytes = sidecar.str();
    WriteAll(files.mission, files.missionBytes);
    WriteAll(files.sidecar, files.sidecarBytes);
    return files;
}

void CheckSourceUnchanged(const FixtureFiles& files) {
    Check(ReadAll(files.mission) == files.missionBytes,
          "migration rewrote the source mission JSON");
    Check(ReadAll(files.sidecar) == files.sidecarBytes,
          "migration rewrote the recorder entity sidecar");
}

void CheckRejectedMissionUnchanged(const Mission::Mission& mission,
                                   const Mission::Mission& before) {
    Check(mission.reviewRequired == before.reviewRequired,
          "rejected evidence changed the review obligations");
    Check(mission.entityLifecycles.empty(),
          "rejected evidence created a lifecycle objective");
    Check(mission.strictEntityContacts == before.strictEntityContacts &&
              mission.entityContacts.size() == before.entityContacts.size() &&
              mission.steps.size() == before.steps.size() &&
              mission.savestate == before.savestate,
          "rejected evidence partially mutated the mission");
}

void TestExact404Migration(const TempTree& tree) {
    Mission::Mission mission = LegacyMission();
    const FixtureFiles files =
        WriteFixture(tree, "exact_404", mission, false, true);
    const auto report =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            mission, files.mission.string());

    Check(report.result == Mission::LegacyEntityMigration::Result::Upgraded,
          "exact #404 sidecar was not upgraded: " + report.diagnostic);
    Check(report.objectivesAdded == 1,
          "exact #404 migration did not add one objective");
    Check(mission.entityLifecycles.size() == 1,
          "exact #404 migration has the wrong objective count");
    const auto& requirement = mission.entityLifecycles.front();
    Check(requirement.notation == "236A (SETUP)",
          "#404 migration produced the wrong presentation notation: " +
              requirement.notation);
    Check(requirement.owner == 1 && requirement.slot == 0 &&
              requirement.generation == 1 &&
              requirement.lifecycle == "morph" &&
              requirement.pattern == 404 && requirement.priorPattern == 400,
          "#404 migration lost its exact lifecycle identity");
    Check(requirement.opensAfterAction == 11,
          "#404 migration lost action gate 11");
    Check(requirement.segment == 1,
          "#404 migration did not retain the earlier entity combo boundary");
    Check(requirement.maxDelay == 48,
          "#404 migration did not derive the recorded 9-tick delay allowance");
    Check(mission.strictEntityContacts,
          "upgraded lifecycle schedule is not strict");
    Check(mission.reviewRequired.size() == 1 &&
              mission.reviewRequired.front() == kMarkerV4,
          "legacy review/v3 marker were not replaced by the v4 marker");
    CheckSourceUnchanged(files);
}

void TestLinkedContactRejectsWithoutMutation(const TempTree& tree) {
    Mission::Mission mission = LegacyMission(true);
    const Mission::Mission before = mission;
    const FixtureFiles files =
        WriteFixture(tree, "linked_contact", mission, true, true);
    const auto report =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            mission, files.mission.string());
    Check(report.result ==
              Mission::LegacyEntityMigration::Result::EvidenceRejected,
          "a #404 episode with a real contact was weakened to lifecycle-only");
    CheckRejectedMissionUnchanged(mission, before);
    CheckSourceUnchanged(files);
}

void TestDelayedContactReconcilesProducerAndHitGates(const TempTree& tree) {
    constexpr int kDelayedContactAction = 5;
    Mission::Mission mission = LegacyMission(false, kDelayedContactAction);
    const FixtureFiles files = WriteFixture(
        tree, "delayed_contact", mission, false, true,
        kDelayedContactAction);
    const auto report =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            mission, files.mission.string());
    Check(report.result == Mission::LegacyEntityMigration::Result::Upgraded,
          "a delayed strict contact was confused with its producer gate: " +
              report.diagnostic);
    Check(mission.entityLifecycles.size() == 1 &&
              mission.entityLifecycles.front().opensAfterAction == 11 &&
              mission.entityLifecycles.front().segment == 1,
          "delayed-contact migration derived the wrong lifecycle contract");
    CheckSourceUnchanged(files);
}

void TestUnreliableMetadataRejectsWithoutMutation(const TempTree& tree) {
    Mission::Mission mission = LegacyMission();
    const Mission::Mission before = mission;
    const FixtureFiles files =
        WriteFixture(tree, "dropped_event", mission, false, false);
    const auto report =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            mission, files.mission.string());
    Check(report.result ==
              Mission::LegacyEntityMigration::Result::EvidenceRejected,
          "a sidecar reporting dropped events was accepted");
    CheckRejectedMissionUnchanged(mission, before);
    CheckSourceUnchanged(files);
}

void TestShioriWhiffMigratesBeforeFanout(const TempTree& tree) {
    constexpr const char* kShioriWhiffReview =
        "attack entity setup #435 (slot 2, generation 1) had no gradeable "
        "contact; author a lifecycle objective or retake it";

    Mission::Mission legacy;
    legacy.format = 1;
    legacy.name = "legacy Shiori fanout plus whiff";
    legacy.player.character = "shiori";
    legacy.dummy.character = "minagi";
    legacy.savestate = "exact-state";
    legacy.strictEntityContacts = true;
    legacy.reviewRequired = {kShioriWhiffReview, kMarkerV3};
    legacy.steps.resize(14);
    for (std::size_t i = 0; i < legacy.steps.size(); ++i) {
        legacy.steps[i].moveIds = {200 + static_cast<int>(i)};
        legacy.steps[i].expectedAttackMask = 16;
    }
    legacy.steps[12].moveIds = {312};
    legacy.steps[13].moveIds = {259};

    const auto makeContact = [](int slot, int damage) {
        Mission::EntityContactRequirement contact;
        contact.notation = "j.2141236A (HIT)";
        contact.owner = 1;
        contact.target = 2;
        contact.slot = slot;
        contact.generation = 1;
        contact.patterns = {435};
        contact.result = "hit";
        contact.contactsRequired = 1;
        contact.comboHitsRequired = 1;
        contact.producerLifecycle = "spawn";
        contact.producerPattern = 435;
        contact.opensAfterAction = 12;
        contact.contactAfterAction = 12;
        contact.afterStep = 11;
        contact.dueBeforeStep = 13;
        contact.dueBeforeStepContact = 0;
        contact.maxDelay = 90;
        contact.damage = damage;
        return contact;
    };
    legacy.entityContacts = {makeContact(4, 72), makeContact(6, 71)};

    const fs::path missionPath = tree.root / "_recorded" /
        "recorded_shiori_fanout_legacy.json";
    const fs::path sidecarPath = tree.root / "_recorded" /
        "recorded_shiori_fanout_legacy.entities.jsonl";
    const fs::path authoredPath = tree.root / "authored_shiori_v3.json";
    std::string saveError;
    Check(Mission::SaveMission(missionPath.string(), legacy, saveError),
          "could not save generated Shiori legacy fixture: " + saveError);
    Check(Mission::SaveMission(authoredPath.string(), legacy, saveError),
          "could not save authored Shiori control fixture: " + saveError);

    json metadata = Metadata(5, true);
    metadata["p1Resource"] = "shiori";
    metadata["p2Resource"] = "minagi";

    const auto shioriLifecycle = [](int slot, int effectiveFrame) {
        json row = LifecycleRow(
            "spawn", 435, -1, effectiveFrame, 12, true);
        row["resource"] = "shiori";
        row["slot"] = slot;
        return row;
    };
    const auto shioriContact = [](int slot, int effectiveFrame) {
        json row = ContactRow(435, effectiveFrame, 12);
        row["resource"] = "shiori";
        row["slot"] = slot;
        return row;
    };

    std::ostringstream sidecar;
    sidecar << metadata.dump() << '\n';
    for (std::size_t i = 0; i < legacy.steps.size(); ++i) {
        json action = ActionRow(
            static_cast<int>(i), legacy.steps[i].moveIds.front(),
            static_cast<int>(i) * 10);
        action["expectedAttackMask"] = 16;
        sidecar << action.dump() << '\n';
    }
    sidecar << shioriLifecycle(4, 122).dump() << '\n'
            << shioriLifecycle(6, 123).dump() << '\n'
            << shioriLifecycle(2, 124).dump() << '\n'
            << shioriContact(4, 126).dump() << '\n'
            << shioriContact(6, 127).dump() << '\n';
    WriteAll(sidecarPath, sidecar.str());

    Mission::Mission loaded;
    std::string loadError;
    Check(Mission::LoadMission(missionPath.string(), loaded, loadError),
          "could not load generated Shiori legacy fixture: " + loadError);
    Check(loaded.entityContacts.size() == 2 &&
              loaded.entityLifecycles.empty() &&
              loaded.reviewRequired ==
                  std::vector<std::string>{kShioriWhiffReview, kMarkerV3},
          "LoadMission normalized Shiori fanout before proving its legacy whiff");

    const auto migration =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            loaded, missionPath.string());
    Check(migration.result ==
              Mission::LegacyEntityMigration::Result::Upgraded &&
              loaded.entityLifecycles.size() == 1 &&
              loaded.entityLifecycles.front().slot == 2 &&
              loaded.entityLifecycles.front().pattern == 435,
          "Shiori legacy whiff sidecar did not promote before fanout: " +
              migration.diagnostic);
    Check(Mission::NormalizeFlexibleEntityFanoutEpisodes(loaded, true) &&
              loaded.entityContacts.size() == 1 &&
              loaded.entityContacts.front().fanoutMembers.size() == 3 &&
              loaded.entityContacts.front().minimumContactsRequired == 1 &&
              loaded.entityContacts.front().minimumComboHitsRequired == 1 &&
              loaded.entityLifecycles.empty() &&
              loaded.reviewRequired == std::vector<std::string>{kMarkerV5},
          "promoted Shiori whiff did not join the v5 fanout without a blocker");

    Mission::Mission authored;
    Check(Mission::LoadMission(authoredPath.string(), authored, loadError) &&
              authored.entityContacts.size() == 2 &&
              authored.entityContacts.front().fanoutMembers.empty() &&
              authored.reviewRequired ==
                  std::vector<std::string>{kShioriWhiffReview, kMarkerV3},
          "an authored v3 Shiori schedule outside recorded_* lost strict semantics");
    const auto authoredMigration =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            authored, authoredPath.string());
    Check(authoredMigration.result ==
              Mission::LegacyEntityMigration::Result::NotApplicable &&
              authored.entityContacts.size() == 2 &&
              authored.entityContacts.front().fanoutMembers.empty(),
          "an authored Shiori schedule entered recorder-only migration");
}

enum class SnowbunnyFault {
    None = 0,
    Overflow,
    Incomplete,
    CrossLane,
    UnrelatedContact,
    MissingSidecar,
};

Mission::Mission BrokenSnowbunnyMission() {
    Mission::Mission mission;
    mission.format = 1;
    mission.name = "recorded Snowbunny producer-order regression";
    mission.player.character = "nayukib";
    mission.dummy.character = "ayu";
    mission.savestate = "exact-state";
    mission.strictEntityContacts = true;
    mission.reviewRequired = {kSnowbunnyMismatchReview, kMarkerV4};
    mission.steps.resize(5);
    const int moves[] = {200, 306, 201, 208, 202};
    const int masks[] = {16, 16, 16, 32, 16};
    for (std::size_t index = 0; index < mission.steps.size(); ++index) {
        mission.steps[index].moveIds = {moves[index]};
        mission.steps[index].expectedAttackMask = masks[index];
    }
    mission.steps[2].directContact = true;
    mission.steps[3].directContact = true;

    Mission::EntityLifecycleRequirement falseSetup;
    falseSetup.notation = "641236 (SETUP)";
    falseSetup.owner = 1;
    falseSetup.slot = 0;
    falseSetup.generation = 1;
    falseSetup.lifecycle = "morph";
    falseSetup.pattern = 404;
    falseSetup.priorPattern = 408;
    falseSetup.opensAfterAction = 1;
    falseSetup.segment = 0;
    falseSetup.maxDelay = 90;
    mission.entityLifecycles.push_back(falseSetup);
    return mission;
}

json SnowbunnyMetadata(int eventCount, SnowbunnyFault fault) {
    json metadata = Metadata(eventCount, true);
    metadata["p1CharacterId"] = 17;
    metadata["p1Resource"] = "nayukib";
    metadata["p2CharacterId"] = 19;
    metadata["p2Resource"] = "ayu";
    metadata["requiresEntityAttributionReview"] = true;
    if (fault == SnowbunnyFault::Overflow) {
        metadata["contactJournalOverflow"] = true;
    } else if (fault == SnowbunnyFault::Incomplete) {
        metadata["eventCount"] = eventCount + 1;
    }
    return metadata;
}

json SnowbunnyAction(const Mission::Mission& mission, int step,
                     int effectiveFrame, int battleBatch) {
    return {
        {"record", "action_order"},
        {"step", step},
        {"move", mission.steps[static_cast<std::size_t>(step)].moveIds.front()},
        {"expectedAttackMask",
         mission.steps[static_cast<std::size_t>(step)].expectedAttackMask},
        {"effectiveFrame", effectiveFrame},
        {"battleBatch", battleBatch},
        {"actionOrder", step + 1},
    };
}

json SnowbunnyLifecycle(const char* event, int slot, int pattern,
                        int priorPattern, int effectiveFrame, int afterStep,
                        bool attack) {
    return {
        {"record", "entity_lifecycle"},
        {"event", event},
        {"evidence", "ring_sample"},
        {"authoring_only", true},
        {"sampled", true},
        {"unordered", true},
        {"non_strict", true},
        {"effectiveFrame", effectiveFrame},
        {"characterId", 17},
        {"resource", "nayukib"},
        {"player", 1},
        {"slot", slot},
        {"generation", 1},
        {"pattern", pattern},
        {"priorPattern", priorPattern < 0 ? json(nullptr) : json(priorPattern)},
        {"class", attack ? "projectile" : "entity"},
        {"attack", attack},
        {"observedAfterStep", afterStep},
        {"observedAfterActionOrder", afterStep + 1},
    };
}

json SnowbunnyContact(int sequence, int batch, int pattern,
                      int effectiveFrame, int afterStep,
                      int comboBefore, int comboAfter,
                      int hpBefore, int hpAfter) {
    const bool entity = pattern >= 0;
    return {
        {"record", "contact"},
        {"event", "contact"},
        {"evidence", "resolver_hook"},
        {"authoring_only", true},
        {"sampled", false},
        {"unordered", false},
        {"non_strict", true},
        {"effectiveFrame", effectiveFrame},
        {"resource", "nayukib"},
        {"player", 1},
        {"slot", entity ? json(pattern == 404 ? 0 : 1)
                         : json(nullptr)},
        {"generation", entity ? 1 : 0},
        {"pattern", entity ? json(pattern) : json(nullptr)},
        {"observedAfterStep", afterStep},
        {"observedAfterActionOrder", afterStep + 1},
        {"sequence", sequence},
        {"batch", batch},
        {"source", entity ? "entity" : "direct"},
        {"attributedStep", entity ? -1 : afterStep},
        {"attackerMove", entity ? pattern : (afterStep == 2 ? 201 : 208)},
        {"defender", 2},
        {"result", "hit"},
        {"comboBefore", comboBefore},
        {"comboAfter", comboAfter},
        {"hpBefore", hpBefore},
        {"hpAfter", hpAfter},
        {"comboEndAfter", false},
    };
}

FixtureFiles WriteSnowbunnyFixture(const TempTree& tree,
                                   const std::string& stem,
                                   const Mission::Mission& mission,
                                   SnowbunnyFault fault) {
    FixtureFiles files;
    files.mission = tree.root / "_recorded" /
        ("recorded_" + stem + ".json");
    files.sidecar = tree.root / "_recorded" /
        ("recorded_" + stem + ".entities.jsonl");
    files.missionBytes = "{\r\n  \"generated\": true\r\n}\r\n";
    WriteAll(files.mission, files.missionBytes);

    std::vector<json> trace;
    trace.push_back(SnowbunnyLifecycle("spawn", 0, 408, -1, 20, 1, false));
    trace.push_back(SnowbunnyLifecycle("spawn", 1, 409, -1, 20, 1, false));
    trace.push_back(SnowbunnyLifecycle("morph", 0, 404, 408, 25, 1, true));
    trace.push_back(SnowbunnyLifecycle("morph", 1, 405, 409, 25, 1, true));
    trace.push_back(SnowbunnyContact(1, 120, 405, 30, 1,
                                     0, 1, 1000, 900));
    trace.push_back(SnowbunnyLifecycle("morph", 1, 407, 405, 32, 1, false));
    trace.push_back(SnowbunnyContact(2, 130, -1, 40, 2,
                                     1, 2, 900, 850));
    trace.push_back(SnowbunnyLifecycle("morph", 0, 406, 404, 40, 2, false));
    trace.push_back(SnowbunnyContact(3, 131, 404, 40, 2,
                                     2, 3, 850, 750));
    trace.push_back(SnowbunnyLifecycle("morph", 1, 409, 407, 50, 2, false));
    trace.push_back(SnowbunnyContact(4, 140, -1, 60, 3,
                                     3, 4, 750, 700));
    trace.push_back(SnowbunnyLifecycle(
        "morph", 1, 407,
        fault == SnowbunnyFault::CrossLane ? 408 : 409,
        60, 3, false));
    trace.push_back(SnowbunnyContact(
        5, 141,
        fault == SnowbunnyFault::UnrelatedContact ? 403 : 405,
        60, 3, 4, 5, 700, 600));

    std::ostringstream sidecar;
    sidecar << SnowbunnyMetadata(
        static_cast<int>(trace.size()), fault).dump() << '\n';
    const int frames[] = {0, 10, 40, 60, 80};
    const int batches[] = {100, 110, 125, 135, 150};
    for (std::size_t step = 0; step < mission.steps.size(); ++step) {
        sidecar << SnowbunnyAction(
            mission, static_cast<int>(step), frames[step], batches[step]).dump()
                << '\n';
    }
    for (const json& row : trace) sidecar << row.dump() << '\n';
    files.sidecarBytes = sidecar.str();
    if (fault != SnowbunnyFault::MissingSidecar) {
        WriteAll(files.sidecar, files.sidecarBytes);
    }
    return files;
}

void CheckSnowbunnyRejectedUnchanged(const Mission::Mission& mission,
                                     const Mission::Mission& before) {
    Check(mission.reviewRequired == before.reviewRequired &&
              mission.entityContacts.empty() &&
              mission.entityLifecycles.size() ==
                  before.entityLifecycles.size() &&
              mission.entityLifecycles.front().pattern ==
                  before.entityLifecycles.front().pattern &&
              mission.strictEntityContacts == before.strictEntityContacts,
          "rejected Snowbunny evidence partially mutated the mission");
}

void TestSnowbunnyProducerOrderingRepair(const TempTree& tree) {
    Mission::Mission mission = BrokenSnowbunnyMission();
    const FixtureFiles files = WriteSnowbunnyFixture(
        tree, "snowbunny_repair", mission, SnowbunnyFault::None);
    const auto report =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            mission, files.mission.string());
    Check(report.result == Mission::LegacyEntityMigration::Result::Upgraded,
          "Snowbunny producer-order repair failed: " + report.diagnostic);
    Check(report.objectivesAdded == 3 &&
              mission.entityContacts.size() == 3 &&
              mission.entityLifecycles.empty(),
          "Snowbunny repair did not replace the false setup with three contacts");
    const auto& first = mission.entityContacts[0];
    const auto& retirement = mission.entityContacts[1];
    const auto& collapsed = mission.entityContacts[2];
    Check(first.patterns == std::vector<int>{405} && first.slot == 1 &&
              first.generation == 1 && first.producerLifecycle == "morph" &&
              first.producerPattern == 405 &&
              first.producerPriorPattern == 409 &&
              first.opensAfterAction == 1 && first.contactAfterAction == 1,
          "ordinary Snowbunny #405 lost its exact producer descriptor");
    Check(retirement.patterns == std::vector<int>{404} &&
              retirement.slot == 0 && retirement.generation == 1 &&
              retirement.producerPattern == 404 &&
              retirement.producerPriorPattern == 408 &&
              retirement.opensAfterAction == 1 &&
              retirement.contactAfterAction == 2 &&
              retirement.afterStep == 2,
          "same-sample #404 retirement did not recover its earlier producer");
    Check(collapsed.patterns == std::vector<int>{405} &&
              collapsed.slot == 1 && collapsed.generation == 1 &&
              collapsed.producerPattern == 405 &&
              collapsed.producerPriorPattern == 409 &&
              collapsed.opensAfterAction == 3 &&
              collapsed.contactAfterAction == 3 &&
              collapsed.afterStep == 3,
          "collapsed controller-to-recovery sample was not synthesized exactly");
    for (const auto& contact : mission.entityContacts) {
        Check(contact.notation == "641236A (HIT)" &&
                  contact.result == "hit" &&
                  contact.contactsRequired == 1 &&
                  contact.comboHitsRequired == 1 && contact.damage == 100,
              "repaired Snowbunny contact lost result/count/damage presentation");
    }
    Check(mission.reviewRequired == std::vector<std::string>{kMarkerV3},
          "Snowbunny mismatch/v4 marker did not become the strict v3 marker");
    CheckSourceUnchanged(files);

    const auto secondPass =
        Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
            mission, files.mission.string());
    Check(secondPass.result ==
              Mission::LegacyEntityMigration::Result::NotApplicable,
          "the in-memory Snowbunny repair was not idempotent");
}

void TestSnowbunnyRepairRejectsIncompleteEvidence(const TempTree& tree) {
    for (const SnowbunnyFault fault : {
             SnowbunnyFault::Overflow,
             SnowbunnyFault::Incomplete,
             SnowbunnyFault::CrossLane,
             SnowbunnyFault::UnrelatedContact,
             SnowbunnyFault::MissingSidecar}) {
        Mission::Mission mission = BrokenSnowbunnyMission();
        const Mission::Mission before = mission;
        const FixtureFiles files = WriteSnowbunnyFixture(
            tree, "snowbunny_reject_" + std::to_string(static_cast<int>(fault)),
            mission, fault);
        const auto report =
            Mission::LegacyEntityMigration::UpgradeLegacyLifecycleReviews(
                mission, files.mission.string());
        Check(report.result ==
                  Mission::LegacyEntityMigration::Result::EvidenceRejected,
              "unsafe Snowbunny evidence was accepted");
        CheckSnowbunnyRejectedUnchanged(mission, before);
        Check(ReadAll(files.mission) == files.missionBytes,
              "rejected Snowbunny repair rewrote mission JSON");
        if (fault != SnowbunnyFault::MissingSidecar) {
            Check(ReadAll(files.sidecar) == files.sidecarBytes,
                  "rejected Snowbunny repair rewrote its sidecar");
        }
    }
}

}

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            Mission::Mission mission;
            std::string loadError;
            Check(Mission::LoadMission(argv[1], mission, loadError),
                  "could not load migration probe mission: " + loadError);
            const auto report =
                Mission::LegacyEntityMigration::
                    UpgradeLegacyLifecycleReviews(mission, argv[1]);
            Check(report.result ==
                      Mission::LegacyEntityMigration::Result::Upgraded,
                  "migration probe was not upgraded: " +
                      report.diagnostic);
            if (mission.player.character == "nayukib") {
                Check(mission.entityLifecycles.empty() &&
                          !mission.entityContacts.empty(),
                      "Snowbunny migration probe retained its false lifecycle");
                std::cout << report.diagnostic << " contacts="
                          << mission.entityContacts.size() << '\n';
                return 0;
            }
            Check(mission.entityLifecycles.size() == 1,
                  "migration probe has the wrong objective count");
            const auto& requirement = mission.entityLifecycles.front();
            std::cout << report.diagnostic << " notation="
                      << requirement.notation << " action="
                      << requirement.opensAfterAction << " segment="
                      << requirement.segment << " maxDelay="
                      << requirement.maxDelay << '\n';
            return 0;
        }
        TempTree tree;
        TestExact404Migration(tree);
        TestDelayedContactReconcilesProducerAndHitGates(tree);
        TestLinkedContactRejectsWithoutMutation(tree);
        TestUnreliableMetadataRejectsWithoutMutation(tree);
        TestShioriWhiffMigratesBeforeFanout(tree);
        TestSnowbunnyProducerOrderingRepair(tree);
        TestSnowbunnyRepairRejectsIncompleteEvidence(tree);
        std::cout << "mission_legacy_entity_migration_tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mission_legacy_entity_migration_tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
