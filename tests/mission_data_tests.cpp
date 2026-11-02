#include "game/mission/mission_data.h"
#include "game/mission/tutorial_support.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <process.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Stub: tutorial_support.cpp is linked for its policy code, not the injected DLL logger.
void LogOut(const std::string&, bool) {}

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    throw std::runtime_error(message);
}

std::string ReadAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

class TempArtifactCleanup {
public:
    TempArtifactCleanup(std::initializer_list<std::filesystem::path> paths)
        : paths_(paths) {}

    ~TempArtifactCleanup() {
        for (const auto& path : paths_) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }

    TempArtifactCleanup(const TempArtifactCleanup&) = delete;
    TempArtifactCleanup& operator=(const TempArtifactCleanup&) = delete;

private:
    std::vector<std::filesystem::path> paths_;
};

}

int main(int argc, char** argv) {
    // Release and Debug may run concurrently under CTest; a per-process name keeps one binary from
    // truncating the other's destructive JSON fixture.
    const std::string fixtureSuffix = "_" + std::to_string(_getpid()) + ".json";
    const std::string path = "mission_data_boundary_test" + fixtureSuffix;
    const std::string recordedPath = "recorded_legacy_in_range" + fixtureSuffix;
    const std::string lessonPath = "tutorial_roundtrip" + fixtureSuffix;
    TempArtifactCleanup cleanup({path, recordedPath, lessonPath});

    try {
        Mission::Mission mission;
    mission.name = "boundary serde";
    Mission::Step first;
    first.notation = "2C";
    first.moveIds = {206};
    first.expectedAttackMask = 64;
    first.comboEndAfter = true;
    first.directContact = true;
    first.contactResult = "recoil_guard";
    Mission::Step second;
    second.notation = "j.B";
    second.moveIds = {208};
    second.maxGap = 460;
    Mission::Step flexible;
    flexible.notation = "2C";
    flexible.moveIds = {206};
    flexible.req = Mission::StepReq::Hits;
    flexible.hitsRequired = 5;
    flexible.directContact = true;
    flexible.contactResult = "hit";
    flexible.allowPartialHits = true;
    mission.steps = {first, second, flexible};
    mission.reviewRequired = {"entity attribution"};
    mission.recordingDiagnostics = {
        "A at frame 996 was sampled but produced no move"
    };
    mission.demo =
        "EFZMACRO 1 5 {3: 5 5 5}x2 5A {3: 5A 5 5} 5 {3: 5 5 5}";

    std::string error;
    Check(Mission::SaveMission(path, mission, error), "save boundary mission");
    const std::string encoded = ReadAll(path);
    Check(encoded.find("\"comboEndAfter\": true") != std::string::npos,
          "true boundary is serialized explicitly");
    Check(encoded.find("\"contactSource\": \"direct\"") != std::string::npos,
          "direct resolver ownership is serialized explicitly");
    Check(encoded.find("\"contactResult\": \"recoil_guard\"") != std::string::npos,
          "typed direct-contact result is serialized explicitly");
    Check(encoded.find("\"expectedAttackMask\": 64") != std::string::npos,
          "recorded causal attack mask is serialized explicitly");
    Check(encoded.find("\"allowPartialHits\": true") != std::string::npos,
          "recorder-flexible multi-hit policy is serialized explicitly");
    Check(encoded.find("\"reviewRequired\"") != std::string::npos,
          "author-review guardrail is serialized explicitly");
    Check(encoded.find("\"recordingDiagnostics\"") != std::string::npos,
          "non-blocking recording diagnostics are serialized explicitly");
    Check(encoded.find("EFZMACRO 1") != std::string::npos,
          "the sealed demonstration is serialized with the mission");

    Mission::Mission loaded;
    Check(Mission::LoadMission(path, loaded, error), "load boundary mission");
    Check(loaded.steps.size() == 3, "step count roundtrips");
    Check(loaded.steps[0].comboEndAfter, "boundary bool roundtrips");
    Check(loaded.steps[0].directContact, "direct contact ownership roundtrips");
    Check(loaded.steps[0].contactResult == "recoil_guard",
          "typed direct-contact result roundtrips");
    Check(loaded.steps[0].expectedAttackMask == 64,
          "recorded causal attack mask roundtrips");
    Check(loaded.steps[1].maxGap == 460, "delayed-button gap roundtrips");
    Check(loaded.steps[1].expectedAttackMask == 0,
          "legacy step causal attack mask defaults to unknown");
    Check(loaded.steps[2].allowPartialHits &&
              loaded.steps[2].hitsRequired == 5,
          "flexible multi-hit policy and observed count roundtrip");
    Check(loaded.reviewRequired == std::vector<std::string>{"entity attribution"},
          "author-review guardrail roundtrips");
    Check(loaded.recordingDiagnostics == std::vector<std::string>{
              "A at frame 996 was sampled but produced no move"},
          "non-blocking recording diagnostics roundtrip");
    Check(loaded.demo == mission.demo,
          "the exact sealed demonstration text roundtrips without normalization");

    loaded.steps[0].expectedAttackMask = 1;
    Check(!Mission::SaveMission(path, loaded, error) &&
              error.find("invalid expectedAttackMask") != std::string::npos,
          "programmatic saves also reject non-attack causal mask bits");

    loaded.steps[0].comboEndAfter = false;
    loaded.steps[0].directContact = false;
    loaded.steps[0].contactResult.clear();
    loaded.steps[0].expectedAttackMask = 0;
    loaded.steps.pop_back();
    loaded.reviewRequired.clear();
    loaded.recordingDiagnostics.clear();
    Check(Mission::SaveMission(path, loaded, error), "save legacy-compatible mission");
    Check(ReadAll(path).find("comboEndAfter") == std::string::npos,
          "false boundary is omitted for legacy files");
    Check(ReadAll(path).find("contactSource") == std::string::npos,
          "legacy aggregate contact omits resolver ownership");
    Check(ReadAll(path).find("expectedAttackMask") == std::string::npos,
          "legacy/unknown causal attack mask is omitted");
    Check(ReadAll(path).find("recordingDiagnostics") == std::string::npos,
          "empty recording diagnostics are omitted for old-file compatibility");

    {
        Mission::Mission commandMission;
        commandMission.name = "input-only entity command";
        Mission::Step command;
        command.notation = "(J.)236S";
        command.expectedAttackMask = 128;
        command.req = Mission::StepReq::Move;
        command.entityCommand.present = true;
        command.entityCommand.slot = 7;
        command.entityCommand.generation = 3;
        command.entityCommand.rootPattern = 401;
        command.entityCommand.activationPattern = 431;
        commandMission.steps.push_back(command);

        Check(Mission::SaveMission(path, commandMission, error),
              "save input-only entity command step");
        const std::string commandJson = ReadAll(path);
        Check(commandJson.find("\"entityCommand\"") != std::string::npos &&
                  commandJson.find("\"rootPattern\": 401") !=
                      std::string::npos &&
                  commandJson.find("\"activationPattern\": 431") !=
                      std::string::npos,
              "entity command identity is serialized explicitly");
        Check(commandJson.find("\"ids\"") == std::string::npos,
              "input-only entity command omits an empty ids array");

        Check(Mission::LoadMission(path, loaded, error) &&
                  loaded.steps.size() == 1 &&
                  loaded.steps[0].moveIds.empty() &&
                  loaded.steps[0].entityCommand.present &&
                  loaded.steps[0].entityCommand.slot == 7 &&
                  loaded.steps[0].entityCommand.generation == 3 &&
                  loaded.steps[0].entityCommand.rootPattern == 401 &&
                  loaded.steps[0].entityCommand.activationPattern == 431,
              "input-only entity command identity roundtrips");

        loaded.steps[0].entityCommand.slot = 64;
        Check(!Mission::SaveMission(path, loaded, error) &&
                  error.find("entityCommand") != std::string::npos,
              "programmatic saves reject command slots outside the ring");
        loaded.steps[0].entityCommand.slot = 7;
        loaded.steps[0].entityCommand.generation = 0;
        Check(!Mission::SaveMission(path, loaded, error) &&
                  error.find("entityCommand") != std::string::npos,
              "programmatic saves reject incomplete entity command identity");
    }

    {
        std::ofstream out(recordedPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad entity command type","steps":[{"notation":"S","entityCommand":7}]})json";
    }
    Check(!Mission::LoadMission(recordedPath, loaded, error) &&
              error.find("entityCommand is not an object") !=
                  std::string::npos,
          "non-object entity command is rejected");

    {
        std::ofstream out(recordedPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"incomplete entity command","steps":[{"notation":"S","entityCommand":{"slot":7,"generation":3,"rootPattern":401}}]})json";
    }
    Check(!Mission::LoadMission(recordedPath, loaded, error) &&
              error.find("missing or non-integer activationPattern") !=
                  std::string::npos,
          "incomplete entity command identity is rejected");

    {
        std::ofstream out(recordedPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"invalid entity command","steps":[{"notation":"S","entityCommand":{"slot":7,"generation":0,"rootPattern":401,"activationPattern":431}}]})json";
    }
    Check(!Mission::LoadMission(recordedPath, loaded, error) &&
              error.find("out-of-range identity field") != std::string::npos,
          "out-of-range entity command identity is rejected");

    {
        std::ofstream out(recordedPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"out-of-ring entity command","steps":[{"notation":"S","entityCommand":{"slot":64,"generation":1,"rootPattern":401,"activationPattern":431}}]})json";
    }
    Check(!Mission::LoadMission(recordedPath, loaded, error) &&
              error.find("out-of-range identity field") != std::string::npos,
          "entity command slot must fit the restored 64-slot ring");

    {
        std::ofstream out(recordedPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"old exact recording","steps":[{"notation":"2C","ids":[206],"req":"hits","hits":5,"contactSource":"direct","contactResult":"hit"}]})json";
    }
    Check(Mission::LoadMission(recordedPath, loaded, error) &&
              loaded.steps.size() == 1 &&
              loaded.steps[0].allowPartialHits,
          "pre-key exact direct recordings migrate to flexible multi-hit playback");

    {
        std::ofstream out(recordedPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"old flexible entity barriers","steps":[{"notation":"2C","ids":[206],"req":"hits","hits":5,"contactSource":"direct","contactResult":"hit"}],"entityContacts":[{"patterns":[405],"afterStep":0,"afterStepContact":4,"dueBeforeStep":0,"dueBeforeStepContact":5},{"patterns":[405],"afterStep":0,"afterStepContact":5,"dueBeforeStep":0,"dueBeforeStepContact":1},{"patterns":[405],"dueBeforeStep":0,"dueBeforeStepContact":0}]})json";
    }
    Check(Mission::LoadMission(recordedPath, loaded, error) &&
              loaded.entityContacts.size() == 3 &&
              loaded.entityContacts[0].afterStepContact == 1 &&
              loaded.entityContacts[0].dueBeforeStepContact == -1 &&
              loaded.entityContacts[1].afterStepContact == -1 &&
              loaded.entityContacts[1].dueBeforeStepContact == 1 &&
              loaded.entityContacts[2].dueBeforeStepContact == 0,
          "legacy flexible multi-hit barriers retain first-contact lower bounds, whole-action completion, and exact action-start gates");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"old authored exact count","steps":[{"notation":"2C","ids":[206],"req":"hits","hits":5,"contactSource":"direct","contactResult":"hit"}]})json";
    }
    Check(Mission::LoadMission(path, loaded, error) &&
              !loaded.steps[0].allowPartialHits,
          "an unmarked old hand-authored multi-hit remains strict");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"strict exact recording","steps":[{"notation":"2C","ids":[206],"req":"hits","hits":5,"contactSource":"direct","contactResult":"hit","allowPartialHits":false}]})json";
    }
    Check(Mission::LoadMission(path, loaded, error) &&
              !loaded.steps[0].allowPartialHits,
          "an explicitly authored strict direct hit count stays strict");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad partial type","steps":[{"notation":"2C","ids":[206],"req":"hits","hits":5,"contactSource":"direct","allowPartialHits":"yes"}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("invalid allowPartialHits") != std::string::npos,
          "non-boolean flexible hit policy fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":2,"name":"future","steps":[{"notation":"5A","ids":[200]}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("unsupported mission format 2") != std::string::npos,
          "future mission formats fail closed until their runtime exists");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad source","steps":[{"notation":"5A","ids":[200],"contactSource":"entity"}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("unsupported contactSource") != std::string::npos,
          "entity contact cannot silently downgrade into a legacy linear step");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad causal mask","steps":[{"notation":"5A","ids":[200],"expectedAttackMask":256}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("invalid expectedAttackMask") !=
                  std::string::npos,
          "out-of-range recorded causal attack mask fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"direction causal mask","steps":[{"notation":"5A","ids":[200],"expectedAttackMask":1}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("invalid expectedAttackMask") !=
                  std::string::npos,
          "non-attack bits cannot masquerade as a recorded causal attack mask");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad source type","steps":[{"notation":"5A","ids":[200],"contactSource":7}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("unsupported contactSource") != std::string::npos,
          "non-string contactSource fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad result","steps":[{"notation":"5A","ids":[200],"contactSource":"direct","contactResult":"anything"}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("unsupported contactResult") != std::string::npos,
          "unknown typed contact result fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad review","reviewRequired":"entity","steps":[{"notation":"5A","ids":[200]}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("reviewRequired is not an array") != std::string::npos,
          "malformed author-review guardrail cannot disappear during parsing");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad diagnostics","recordingDiagnostics":"input","steps":[{"notation":"5A","ids":[200]}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("recordingDiagnostics is not an array") != std::string::npos,
          "malformed recording diagnostics cannot disappear during parsing");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad diagnostic entry","recordingDiagnostics":[""],"steps":[{"notation":"5A","ids":[200]}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("recordingDiagnostics contains an invalid entry") !=
                  std::string::npos,
          "empty recording diagnostic entries fail closed");

    {
        Mission::Mission entityMission;
        entityMission.name = "entity contact serde";
        entityMission.strictEntityContacts = true;
        entityMission.reviewRequired = {
            "runtime:entity-contact-schedule-v2"
        };
        Mission::EntityContactRequirement contact;
        contact.notation = "#443 HIT x4";
        contact.owner = 1;
        contact.target = 2;
        contact.slot = 6;
        contact.generation = 3;
        contact.patterns = {443, 454};
        contact.result = "hit";
        contact.contactsRequired = 4;
        contact.comboHitsRequired = 4;
        contact.opensAfterAction = 2;
        contact.contactAfterAction = 2;
        contact.afterStep = 1;
        contact.afterStepContact = 2;
        contact.dueBeforeStep = 3;
        contact.dueBeforeStepContact = 1;
        contact.segment = 1;
        contact.maxDelay = 432;
        contact.damage = 95;
        contact.comboEndAfter = true;
        entityMission.entityContacts.push_back(contact);

        Check(Mission::SaveMission(path, entityMission, error),
              "save strict entity-contact schedule");
        const std::string entityEncoded = ReadAll(path);
        Check(entityEncoded.find("\"strictEntityContacts\": true") !=
                  std::string::npos &&
              entityEncoded.find("\"entityContacts\"") != std::string::npos &&
              entityEncoded.find("\"patterns\": [") != std::string::npos &&
              entityEncoded.find("\"contactAfterAction\": 2") != std::string::npos &&
              entityEncoded.find("\"afterStepContact\": 2") != std::string::npos &&
              entityEncoded.find("\"dueBeforeStepContact\": 1") != std::string::npos &&
              entityEncoded.find("\"comboEndAfter\": true") != std::string::npos,
              "entity-contact schedule uses canonical JSON fields");

        Mission::Mission entityLoaded;
        Check(Mission::LoadMission(path, entityLoaded, error),
              "reload strict entity-contact schedule");
        Check(entityLoaded.strictEntityContacts &&
                  entityLoaded.entityContacts.size() == 1 &&
                  entityLoaded.reviewRequired == std::vector<std::string>{
                      "runtime:entity-contact-schedule-v2"},
              "strict entity-contact schedule round-trips");
        const auto& roundTrip = entityLoaded.entityContacts[0];
        Check(roundTrip.notation == contact.notation &&
                  roundTrip.owner == contact.owner &&
                  roundTrip.target == contact.target &&
                  roundTrip.slot == contact.slot &&
                  roundTrip.generation == contact.generation &&
                  roundTrip.patterns == contact.patterns &&
                  roundTrip.result == contact.result &&
                  roundTrip.contactsRequired == contact.contactsRequired &&
                  roundTrip.comboHitsRequired == contact.comboHitsRequired &&
                  roundTrip.opensAfterAction == contact.opensAfterAction &&
                  roundTrip.contactAfterAction ==
                      contact.contactAfterAction &&
                  roundTrip.afterStep == contact.afterStep &&
                  roundTrip.afterStepContact == contact.afterStepContact &&
                  roundTrip.dueBeforeStep == contact.dueBeforeStep &&
                  roundTrip.dueBeforeStepContact ==
                      contact.dueBeforeStepContact &&
                  roundTrip.segment == contact.segment &&
                  roundTrip.maxDelay == contact.maxDelay &&
                  roundTrip.damage == contact.damage &&
                  roundTrip.comboEndAfter == contact.comboEndAfter,
              "every entity-contact field round-trips");
    }

    {
        Mission::Mission lineageMission;
        lineageMission.name = "entity lineage serde";
        lineageMission.strictEntityContacts = true;
        lineageMission.reviewRequired = {
            "runtime:entity-contact-schedule-v3"
        };
        Mission::EntityContactRequirement contact;
        contact.slot = 11;
        contact.generation = 2;
        contact.patterns = {403};
        contact.producerLifecycle = "morph";
        contact.producerPattern = 403;
        contact.producerPriorPattern = 400;
        contact.opensAfterAction = 4;
        contact.maxDelay = 90;
        lineageMission.entityContacts.push_back(contact);
        Check(Mission::SaveMission(path, lineageMission, error),
              "save v3 entity lineage");
        const std::string encoded = ReadAll(path);
        Check(encoded.find("\"producerLifecycle\": \"morph\"") !=
                  std::string::npos &&
              encoded.find("\"producerPattern\": 403") !=
                  std::string::npos &&
              encoded.find("\"producerPriorPattern\": 400") !=
                  std::string::npos,
              "v3 producer descriptor uses canonical JSON fields");
        Mission::Mission roundTrip;
        Check(Mission::LoadMission(path, roundTrip, error) &&
                  roundTrip.entityContacts.size() == 1 &&
                  roundTrip.entityContacts[0].producerLifecycle == "morph" &&
                  roundTrip.entityContacts[0].producerPattern == 403 &&
                  roundTrip.entityContacts[0].producerPriorPattern == 400,
              "v3 producer descriptor round-trips");
    }

    {
        Mission::Mission fanoutMission;
        fanoutMission.name = "entity fanout serde";
        fanoutMission.strictEntityContacts = true;
        fanoutMission.reviewRequired = {
            "runtime:entity-fanout-schedule-v5"
        };
        Mission::EntityContactRequirement contact;
        contact.notation = "2141236 (HIT)";
        contact.slot = -1;
        contact.patterns = {435};
        contact.result = "hit";
        contact.contactsRequired = 1;
        contact.comboHitsRequired = 1;
        contact.minimumContactsRequired = 1;
        contact.minimumComboHitsRequired = 1;
        contact.producerLifecycle = "spawn";
        contact.producerPattern = 435;
        contact.opensAfterAction = 12;
        contact.semanticSourceAction =
            Mission::SemanticSourcePolicy::kLegacyAbsent;
        contact.semanticSourceMove =
            Mission::SemanticSourcePolicy::kLegacyAbsent;
        contact.contactAfterAction = 12;
        contact.afterStep = 11;
        contact.dueBeforeStep = 13;
        contact.dueBeforeStepContact = 0;
        Mission::EntityContactFanoutMember hitChild;
        hitChild.slot = 4;
        hitChild.generation = 1;
        hitChild.patterns = {435};
        hitChild.producerLifecycle = "spawn";
        hitChild.producerPattern = 435;
        hitChild.contactsObserved = 1;
        hitChild.comboHitsObserved = 1;
        hitChild.damageObserved = 72;
        Mission::EntityContactFanoutMember whiffedChild = hitChild;
        whiffedChild.slot = 6;
        whiffedChild.contactsObserved = 0;
        whiffedChild.comboHitsObserved = 0;
        whiffedChild.damageObserved = 0;
        contact.fanoutMembers = {hitChild, whiffedChild};
        fanoutMission.entityContacts.push_back(contact);

        Check(Mission::SaveMission(path, fanoutMission, error),
              "save v5 entity fanout");
        const std::string encoded = ReadAll(path);
        Check(encoded.find("\"minimumContactsRequired\": 1") !=
                  std::string::npos &&
              encoded.find("\"fanoutMembers\"") != std::string::npos &&
              encoded.find("\"contactsObserved\": 0") !=
                  std::string::npos,
              "fanout JSON explicitly preserves minima and whiffed siblings");
        Mission::Mission roundTrip;
        Check(Mission::LoadMission(path, roundTrip, error) &&
                  roundTrip.entityContacts.size() == 1 &&
                  roundTrip.entityContacts[0].fanoutMembers.size() == 2 &&
                  roundTrip.entityContacts[0].minimumContactsRequired == 1 &&
                  roundTrip.entityContacts[0].minimumComboHitsRequired == 1 &&
                  roundTrip.entityContacts[0]
                          .fanoutMembers[1]
                          .contactsObserved == 0,
              "explicit fanout round-trips outside a generated path");
    }

    {
        Mission::Mission legacy;
        legacy.name = "legacy Shiori fanout";
        legacy.player.character = "shiori";
        legacy.strictEntityContacts = true;
        legacy.steps.resize(14);
        legacy.steps[12].moveIds = {312};
        legacy.reviewRequired = {
            "author note", "runtime:entity-lifecycle-schedule-v4"
        };
        const auto makeContact = [](int slot, int delay, int damage) {
            Mission::EntityContactRequirement contact;
            contact.notation = "2141236 (HIT)";
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
            contact.maxDelay = delay;
            contact.damage = damage;
            return contact;
        };
        legacy.entityContacts = {
            makeContact(4, 96, 72), makeContact(6, 150, 71)
        };
        Mission::EntityLifecycleRequirement whiffed;
        whiffed.owner = 1;
        whiffed.slot = 2;
        whiffed.generation = 1;
        whiffed.lifecycle = "spawn";
        whiffed.pattern = 435;
        whiffed.opensAfterAction = 12;
        whiffed.maxDelay = 132;
        Mission::EntityLifecycleRequirement controller = whiffed;
        controller.slot = 3;
        controller.pattern = 436;
        legacy.entityLifecycles = {whiffed, controller};

        Mission::Mission authoredExact = legacy;
        Check(!Mission::NormalizeFlexibleEntityFanoutEpisodes(authoredExact) &&
                  authoredExact.entityContacts.size() == 2,
              "fanout inference is opt-in for generated recorder output");
        Check(Mission::SaveMission(recordedPath, authoredExact, error),
              "save generated-path legacy fanout fixture");
        Mission::Mission generatedLoad;
        Check(Mission::LoadMission(recordedPath, generatedLoad, error) &&
                  generatedLoad.entityContacts.size() == 1 &&
                  generatedLoad.entityContacts[0].fanoutMembers.size() == 3 &&
                  generatedLoad.reviewRequired ==
                      std::vector<std::string>{
                          "author note",
                          "runtime:entity-fanout-schedule-v5"},
              "generated v4 take upgrades only after review markers are parsed");
        Check(Mission::NormalizeFlexibleEntityFanoutEpisodes(legacy, true) &&
                  legacy.entityContacts.size() == 1 &&
                  legacy.entityContacts[0].fanoutMembers.size() == 3 &&
                  legacy.entityContacts[0].contactsRequired == 2 &&
                  legacy.entityContacts[0].comboHitsRequired == 2 &&
                  legacy.entityContacts[0].minimumContactsRequired == 1 &&
                  legacy.entityContacts[0].minimumComboHitsRequired == 1 &&
                  legacy.entityContacts[0].maxDelay == 150 &&
                  legacy.entityContacts[0].damage == 143 &&
                  legacy.entityLifecycles.size() == 1 &&
                  legacy.entityLifecycles[0].pattern == 436 &&
                  legacy.reviewRequired == std::vector<std::string>{
                      "author note", "runtime:entity-fanout-schedule-v5"},
              "generated Shiori siblings coalesce without absorbing controller entities");
    }

    {
        Mission::Mission interleaved;
        interleaved.name = "interleaved Shiori fanout";
        interleaved.player.character = "shiori";
        interleaved.strictEntityContacts = true;
        interleaved.steps.resize(13);
        interleaved.steps[12].moveIds = {312};
        interleaved.reviewRequired = {
            "runtime:entity-contact-schedule-v3"
        };
        const auto makeFanout = [](int slot) {
            Mission::EntityContactRequirement contact;
            contact.notation = "fanout-" + std::to_string(slot);
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
            contact.dueBeforeStep = 12;
            contact.dueBeforeStepContact = -1;
            return contact;
        };
        const auto makeUnrelated = [](const char* notation, int slot) {
            Mission::EntityContactRequirement contact;
            contact.notation = notation;
            contact.slot = slot;
            contact.generation = 1;
            contact.patterns = {433};
            contact.result = "hit";
            contact.contactsRequired = 1;
            contact.comboHitsRequired = 1;
            return contact;
        };
        interleaved.entityContacts = {
            makeUnrelated("before", 20),
            makeFanout(4),
            makeUnrelated("middle", 22),
            makeFanout(6),
            makeUnrelated("after", 24),
        };

        Check(Mission::NormalizeFlexibleEntityFanoutEpisodes(
                  interleaved, true) &&
                  interleaved.entityContacts.size() == 4 &&
                  interleaved.entityContacts[0].notation == "before" &&
                  interleaved.entityContacts[1].fanoutMembers.size() == 2 &&
                  interleaved.entityContacts[1].fanoutMembers[0].slot == 4 &&
                  interleaved.entityContacts[1].fanoutMembers[1].slot == 6 &&
                  interleaved.entityContacts[2].notation == "middle" &&
                  interleaved.entityContacts[3].notation == "after",
              "interleaved Shiori children merge at their first occurrence without reordering unrelated requirements");
    }

    {
        Mission::Mission take;
        take.name = "Shiori shuffled 2141236 take";
        take.player.character = "shiori";
        take.strictEntityContacts = true;
        take.steps.resize(14);
        take.steps[12].moveIds = {312};
        take.steps[13].moveIds = {259};
        take.reviewRequired = {
            "runtime:entity-lifecycle-schedule-v4"
        };
        const int hitSlots[] = {
            2, 6, 4, 10, 8, 16, 20, 18, 24, 26, 30
        };
        for (int index = 0; index < 11; ++index) {
            Mission::EntityContactRequirement contact;
            contact.notation = "2141236 (HIT)";
            contact.slot = hitSlots[index];
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
            contact.maxDelay = index == 10 ? 222 : 96 + index * 6;
            contact.damage = 72 - index;
            contact.comboEndAfter = index == 10;
            take.entityContacts.push_back(contact);
        }
        const int whiffSlots[] = {12, 14, 22, 28};
        for (int index = 0; index < 4; ++index) {
            Mission::EntityLifecycleRequirement whiffed;
            whiffed.notation = "2141236 (SETUP)";
            whiffed.owner = 1;
            whiffed.slot = whiffSlots[index];
            whiffed.generation = 1;
            whiffed.lifecycle = "spawn";
            whiffed.pattern = 435;
            whiffed.opensAfterAction = 12;
            whiffed.maxDelay = 126 + index * 24;
            take.entityLifecycles.push_back(whiffed);
        }
        Mission::EntityLifecycleRequirement laterSetup;
        laterSetup.notation = "214A (SETUP)";
        laterSetup.owner = 1;
        laterSetup.slot = 33;
        laterSetup.generation = 1;
        laterSetup.lifecycle = "spawn";
        laterSetup.pattern = 433;
        laterSetup.opensAfterAction = 13;
        laterSetup.maxDelay = 72;
        take.entityLifecycles.push_back(laterSetup);

        Check(Mission::SaveMission(path, take, error) &&
                  Mission::SaveMission(recordedPath, take, error),
              "save shuffled Shiori migration fixtures");
        Mission::Mission authoredLoad;
        Check(Mission::LoadMission(path, authoredLoad, error) &&
                  authoredLoad.entityContacts.size() == 11 &&
                  authoredLoad.entityContacts[0].fanoutMembers.empty() &&
                  authoredLoad.entityLifecycles.size() == 5 &&
                  authoredLoad.reviewRequired ==
                      std::vector<std::string>{
                          "runtime:entity-lifecycle-schedule-v4"},
              "non-recorded shuffled requirements remain exact");
        Mission::Mission generatedLoad;
        Check(Mission::LoadMission(recordedPath, generatedLoad, error) &&
                  generatedLoad.entityContacts.size() == 1 &&
                  generatedLoad.entityContacts[0].fanoutMembers.size() == 15 &&
                  generatedLoad.entityContacts[0].contactsRequired == 11 &&
                  generatedLoad.entityContacts[0].comboHitsRequired == 11 &&
                  generatedLoad.entityContacts[0].minimumContactsRequired == 1 &&
                  generatedLoad.entityContacts[0]
                          .minimumComboHitsRequired == 1 &&
                  generatedLoad.entityContacts[0].comboEndAfter &&
                  generatedLoad.entityContacts[0]
                          .fanoutMembers[0]
                          .slot == 2 &&
                  generatedLoad.entityContacts[0]
                          .fanoutMembers[2]
                          .slot == 4 &&
                  generatedLoad.entityLifecycles.size() == 1 &&
                  generatedLoad.entityLifecycles[0].pattern == 433 &&
                  generatedLoad.reviewRequired ==
                      std::vector<std::string>{
                          "runtime:entity-fanout-schedule-v5"},
              "generated shuffled children migrate to one minimum-one fanout");
        const auto& migratedMembers =
            generatedLoad.entityContacts[0].fanoutMembers;
        Check(std::count_if(
                  migratedMembers.begin(), migratedMembers.end(),
                  [](const Mission::EntityContactFanoutMember& member) {
                      return member.contactsObserved == 0 &&
                             member.comboHitsObserved == 0;
                  }) == 4 &&
                  std::any_of(
                      migratedMembers.begin(), migratedMembers.end(),
                      [](const Mission::EntityContactFanoutMember& member) {
                          return member.slot == 30 && member.generation == 1 &&
                                 member.contactsObserved == 1 &&
                                 member.comboHitsObserved == 1;
                      }),
              "fanout preserves four whiffs and the terminal hit as exact members");
        Check(Mission::SaveMission(path, generatedLoad, error),
              "save explicit migrated v5 fanout");
        Mission::Mission explicitReload;
        Check(Mission::LoadMission(path, explicitReload, error) &&
                  explicitReload.entityContacts.size() == 1 &&
                  explicitReload.entityContacts[0].fanoutMembers.size() == 15 &&
                  explicitReload.entityContacts[0].comboEndAfter &&
                  explicitReload.reviewRequired ==
                      std::vector<std::string>{
                          "runtime:entity-fanout-schedule-v5"},
              "explicit v5 fanout round-trips without path inference");

        Mission::Mission splitV5 = generatedLoad;
        Mission::EntityContactRequirement terminal =
            take.entityContacts.back();
        auto& splitEpisode = splitV5.entityContacts.front();
        splitEpisode.fanoutMembers.erase(std::remove_if(
            splitEpisode.fanoutMembers.begin(),
            splitEpisode.fanoutMembers.end(),
            [](const Mission::EntityContactFanoutMember& member) {
                return member.slot == 30 && member.generation == 1;
            }), splitEpisode.fanoutMembers.end());
        splitEpisode.contactsRequired = 10;
        splitEpisode.comboHitsRequired = 10;
        splitEpisode.damage -= terminal.damage;
        splitEpisode.maxDelay = 198;
        splitEpisode.comboEndAfter = false;
        splitV5.entityContacts.push_back(terminal);

        Check(Mission::SaveMission(path, splitV5, error),
              "save authored split-v5 control");
        Mission::Mission authoredSplitReload;
        Check(Mission::LoadMission(path, authoredSplitReload, error) &&
                  authoredSplitReload.entityContacts.size() == 2 &&
                  authoredSplitReload.entityContacts[0]
                          .fanoutMembers.size() == 14 &&
                  authoredSplitReload.entityContacts[1].slot == 30,
              "authored split-v5 mission is not inferred or rewritten");

        Check(Mission::SaveMission(recordedPath, splitV5, error),
              "save generated split-v5 regression fixture");
        Mission::Mission repairedV5;
        Check(Mission::LoadMission(recordedPath, repairedV5, error) &&
                  repairedV5.entityContacts.size() == 1 &&
                  repairedV5.entityContacts[0].fanoutMembers.size() == 15 &&
                  repairedV5.entityContacts[0].contactsRequired == 11 &&
                  repairedV5.entityContacts[0].comboHitsRequired == 11 &&
                  repairedV5.entityContacts[0].comboEndAfter &&
                  repairedV5.entityContacts[0].maxDelay == 222,
              "generated split-v5 terminal child repairs into its fanout");
    }

    {
        Mission::Mission actionDueMission;
        actionDueMission.name = "entity action-start due serde";
        Mission::EntityContactRequirement contact;
        contact.patterns = {401};
        contact.dueBeforeStep = 2;
        contact.dueBeforeStepContact = 0;
        actionDueMission.entityContacts.push_back(contact);
        Check(Mission::SaveMission(path, actionDueMission, error),
              "save entity action-start due barrier");
        Check(ReadAll(path).find("\"dueBeforeStepContact\": 0") !=
                  std::string::npos,
              "action-start due barrier serializes zero explicitly");
        Mission::Mission actionDueLoaded;
        Check(Mission::LoadMission(path, actionDueLoaded, error) &&
                  actionDueLoaded.entityContacts.size() == 1 &&
                  actionDueLoaded.entityContacts[0].dueBeforeStepContact == 0,
              "action-start due barrier round-trips without becoming legacy");
    }

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"entity defaults","steps":[],"entityContacts":[{}]})json";
    }
    Check(Mission::LoadMission(path, loaded, error),
          "load minimal entity-contact requirement");
    Check(!loaded.strictEntityContacts && loaded.entityContacts.size() == 1,
          "entity-contact strictness defaults off");
    {
        const auto& defaults = loaded.entityContacts[0];
        Check(defaults.notation.empty() && defaults.owner == 1 &&
                  defaults.target == 2 && defaults.slot == -1 &&
                  defaults.generation == 0 && defaults.patterns.empty() &&
                  defaults.result == "hit" && defaults.contactsRequired == 1 &&
                  defaults.comboHitsRequired == 0 &&
                  defaults.minimumContactsRequired == 0 &&
                  defaults.minimumComboHitsRequired == 0 &&
                  defaults.fanoutMembers.empty() &&
                  defaults.producerLifecycle.empty() &&
                  defaults.producerPattern == -1 &&
                  defaults.producerPriorPattern == -1 &&
                  defaults.opensAfterAction == -1 &&
                  defaults.semanticSourceAction ==
                      Mission::SemanticSourcePolicy::kLegacyAbsent &&
                  defaults.semanticSourceMove ==
                      Mission::SemanticSourcePolicy::kLegacyAbsent &&
                  defaults.contactAfterAction == -1 &&
                  defaults.afterStep == -1 &&
                  defaults.afterStepContact == -1 &&
                  defaults.dueBeforeStep == -1 &&
                  defaults.dueBeforeStepContact == -1 &&
                  defaults.segment == 0 &&
                  defaults.maxDelay == 0 && defaults.damage == 0 &&
                  !defaults.comboEndAfter,
              "minimal entity contact receives stable parser defaults");
    }

    {
        Mission::EntityContactRequirement freshContact;
        Check(Mission::SemanticSourcePolicy::IsExplicitUnresolved(
                  freshContact.semanticSourceAction,
                  freshContact.semanticSourceMove),
              "a newly constructed contact cannot masquerade as legacy JSON");
        Mission::Mission semanticMission;
        semanticMission.name = "entity semantic source serde";
        Mission::Step setter;
        setter.notation = "214A";
        setter.moveIds = {253};
        semanticMission.steps.push_back(setter);
        Mission::EntityContactRequirement contact;
        contact.patterns = {405};
        contact.opensAfterAction = 0;
        contact.contactAfterAction = 0;
        contact.semanticSourceAction = 0;
        contact.semanticSourceMove = 253;
        semanticMission.entityContacts.push_back(contact);

        Check(Mission::SaveMission(path, semanticMission, error),
              "save exact semantic source provenance");
        const std::string exactEncoded = ReadAll(path);
        Check(exactEncoded.find("\"semanticSourceAction\": 0") !=
                  std::string::npos &&
              exactEncoded.find("\"semanticSourceMove\": 253") !=
                  std::string::npos,
              "exact semantic source pair serializes explicitly");
        Mission::Mission exactLoaded;
        Check(Mission::LoadMission(path, exactLoaded, error) &&
                  exactLoaded.entityContacts.size() == 1 &&
                  exactLoaded.entityContacts[0].semanticSourceAction == 0 &&
                  exactLoaded.entityContacts[0].semanticSourceMove == 253,
              "exact semantic source pair round-trips");

        semanticMission.entityContacts[0].semanticSourceAction =
            Mission::SemanticSourcePolicy::kExplicitUnresolved;
        semanticMission.entityContacts[0].semanticSourceMove =
            Mission::SemanticSourcePolicy::kExplicitUnresolved;
        Check(Mission::SaveMission(path, semanticMission, error),
              "save explicitly unresolved semantic source");
        const std::string unresolvedEncoded = ReadAll(path);
        Check(unresolvedEncoded.find("\"semanticSourceAction\": -1") !=
                  std::string::npos &&
              unresolvedEncoded.find("\"semanticSourceMove\": -1") !=
                  std::string::npos,
              "new ambiguity cannot disappear into legacy field absence");
        Mission::Mission unresolvedLoaded;
        Check(Mission::LoadMission(path, unresolvedLoaded, error) &&
                  Mission::SemanticSourcePolicy::IsExplicitUnresolved(
                      unresolvedLoaded.entityContacts[0]
                          .semanticSourceAction,
                      unresolvedLoaded.entityContacts[0]
                          .semanticSourceMove),
              "explicitly unresolved provenance round-trips");

        semanticMission.entityContacts[0].semanticSourceAction = 0;
        semanticMission.entityContacts[0].semanticSourceMove = -1;
        Check(!Mission::SaveMission(path, semanticMission, error) &&
                  error.find("invalid semantic source provenance") !=
                      std::string::npos,
              "mixed semantic source states fail closed on save");
    }
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"partial semantic source","steps":[{"notation":"214A","ids":[253]}],"entityContacts":[{"semanticSourceAction":0}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("only one semantic source field") !=
                  std::string::npos,
          "partial semantic source provenance fails closed on load");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"wrong semantic source","steps":[{"notation":"214A","ids":[253]}],"entityContacts":[{"opensAfterAction":0,"contactAfterAction":0,"semanticSourceAction":0,"semanticSourceMove":254}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("semantic source move outside its action") !=
                  std::string::npos,
          "semantic source move must belong to its persisted action");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy exact producer","player":{"character":"shiori"},"steps":[{"notation":"2141236A","ids":[315]}],"entityContacts":[{"slot":2,"generation":1,"patterns":[435],"producerLifecycle":"spawn","producerPattern":435,"opensAfterAction":0,"contactAfterAction":0,"afterStep":-1}]})json";
    }
    Mission::Mission legacyExact;
    Check(Mission::LoadMission(path, legacyExact, error) &&
              legacyExact.entityContacts.size() == 1 &&
              legacyExact.entityContacts[0].semanticSourceAction == 0 &&
              legacyExact.entityContacts[0].semanticSourceMove == 315,
          "legacy exact producer-qualified contact gains provenance");
    Check(Mission::SaveMission(path, legacyExact, error) &&
              ReadAll(path).find("\"semanticSourceMove\": 315") !=
                  std::string::npos,
          "legacy semantic-source promotion persists on the next save");

    {
        Mission::Mission fanout;
        fanout.name = "legacy exact Shiori fanout source";
        fanout.player.character = "shiori";
        fanout.steps.resize(14);
        fanout.steps[12].notation = "j.2141236A";
        fanout.steps[12].moveIds = {312};
        fanout.steps[13].notation = "j.214A";
        fanout.steps[13].moveIds = {259};
        Mission::EntityContactRequirement contact;
        contact.notation = "j.2141236A (HIT)";
        contact.slot = -1;
        contact.generation = 0;
        contact.patterns = {435};
        contact.contactsRequired = 10;
        contact.comboHitsRequired = 10;
        contact.minimumContactsRequired = 1;
        contact.minimumComboHitsRequired = 1;
        contact.producerLifecycle = "spawn";
        contact.producerPattern = 435;
        contact.opensAfterAction = 12;
        contact.semanticSourceAction =
            Mission::SemanticSourcePolicy::kLegacyAbsent;
        contact.semanticSourceMove =
            Mission::SemanticSourcePolicy::kLegacyAbsent;
        contact.contactAfterAction = 12;
        contact.afterStep = 11;
        contact.dueBeforeStep = 13;
        contact.dueBeforeStepContact = 0;
        for (int index = 0; index < 14; ++index) {
            Mission::EntityContactFanoutMember member;
            member.slot = 2 + index * 2;
            member.generation = 1;
            member.patterns = {435};
            member.producerLifecycle = "spawn";
            member.producerPattern = 435;
            member.contactsObserved = index < 10 ? 1 : 0;
            member.comboHitsObserved = index < 10 ? 1 : 0;
            contact.fanoutMembers.push_back(member);
        }
        fanout.entityContacts.push_back(contact);
        Check(Mission::SaveMission(path, fanout, error),
              "save legacy Shiori fanout source fixture");
        Mission::Mission fanoutLoaded;
        Check(Mission::LoadMission(path, fanoutLoaded, error) &&
                  fanoutLoaded.entityContacts.size() == 1 &&
                  fanoutLoaded.entityContacts[0].semanticSourceAction == 12 &&
                  fanoutLoaded.entityContacts[0].semanticSourceMove == 312,
              "curated exact-member fanout gains one cast provenance");

        fanout.entityContacts[0].fanoutMembers[3].producerPattern = 434;
        Check(Mission::SaveMission(path, fanout, error),
              "save mismatched legacy fanout control");
        Mission::Mission mismatchedFanout;
        Check(Mission::LoadMission(path, mismatchedFanout, error) &&
                  Mission::SemanticSourcePolicy::IsLegacyAbsent(
                      mismatchedFanout.entityContacts[0]
                          .semanticSourceAction,
                      mismatchedFanout.entityContacts[0]
                          .semanticSourceMove),
              "fanout with one mismatched child lineage stays standalone");
    }

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy wildcard projectile","player":{"character":"akane"},"steps":[{"notation":"214236A","ids":[250]}],"entityContacts":[{"slot":3,"generation":1,"patterns":[400],"producerLifecycle":"spawn","producerPattern":400,"opensAfterAction":0,"contactAfterAction":0}]})json";
    }
    Mission::Mission legacyWildcard;
    Check(Mission::LoadMission(path, legacyWildcard, error) &&
              legacyWildcard.entityContacts.size() == 1 &&
              legacyWildcard.entityContacts[0].semanticSourceAction == 0 &&
              legacyWildcard.entityContacts[0].semanticSourceMove == 250,
          "one-move ordinary wildcard projectile gains safe provenance");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy ambiguous persistent","player":{"character":"nagamori"},"steps":[{"notation":"j.214A/B","ids":[255,256]}],"entityContacts":[{"slot":4,"generation":2,"patterns":[405],"producerLifecycle":"spawn","producerPattern":405,"opensAfterAction":0,"contactAfterAction":0}]})json";
    }
    Mission::Mission legacyAmbiguous;
    Check(Mission::LoadMission(path, legacyAmbiguous, error) &&
              Mission::SemanticSourcePolicy::IsLegacyAbsent(
                  legacyAmbiguous.entityContacts[0].semanticSourceAction,
                  legacyAmbiguous.entityContacts[0].semanticSourceMove),
          "two producer-qualified persistent moves stay standalone");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy ambiguous wildcard","player":{"character":"akane"},"steps":[{"notation":"needle","ids":[250,251]}],"entityContacts":[{"slot":3,"generation":1,"patterns":[400],"producerLifecycle":"spawn","producerPattern":400,"opensAfterAction":0,"contactAfterAction":0}]})json";
    }
    Mission::Mission legacyWildcardAmbiguous;
    Check(Mission::LoadMission(path, legacyWildcardAmbiguous, error) &&
              Mission::SemanticSourcePolicy::IsLegacyAbsent(
                  legacyWildcardAmbiguous.entityContacts[0]
                      .semanticSourceAction,
                  legacyWildcardAmbiguous.entityContacts[0]
                      .semanticSourceMove),
          "ordinary wildcard with two possible moves stays standalone");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy delayed contact","player":{"character":"nagamori"},"steps":[{"notation":"j.214A","ids":[255]},{"notation":"5A","ids":[150]}],"entityContacts":[{"slot":4,"generation":2,"patterns":[405],"producerLifecycle":"spawn","producerPattern":405,"opensAfterAction":0,"contactAfterAction":1}]})json";
    }
    Mission::Mission legacyDelayed;
    Check(Mission::LoadMission(path, legacyDelayed, error) &&
              Mission::SemanticSourcePolicy::IsLegacyAbsent(
                  legacyDelayed.entityContacts[0].semanticSourceAction,
                  legacyDelayed.entityContacts[0].semanticSourceMove),
          "persistent activation after a later action stays standalone");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy intervening deadline","player":{"character":"shiori"},"steps":[{"notation":"2141236A","ids":[315]},{"notation":"5A","ids":[150]},{"notation":"5B","ids":[160]}],"entityContacts":[{"slot":2,"generation":1,"patterns":[435],"producerLifecycle":"spawn","producerPattern":435,"opensAfterAction":0,"contactAfterAction":0,"dueBeforeStep":2}]})json";
    }
    Mission::Mission legacyIntervening;
    Check(Mission::LoadMission(path, legacyIntervening, error) &&
              Mission::SemanticSourcePolicy::IsLegacyAbsent(
                  legacyIntervening.entityContacts[0].semanticSourceAction,
                  legacyIntervening.entityContacts[0].semanticSourceMove),
          "an intervening action deadline prevents legacy compaction");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy custom label","player":{"character":"shiori"},"steps":[{"notation":"2141236A","ids":[315]}],"entityContacts":[{"notation":"AUTHOR OBJECTIVE","slot":2,"generation":1,"patterns":[435],"producerLifecycle":"spawn","producerPattern":435,"opensAfterAction":0,"contactAfterAction":0}]})json";
    }
    Mission::Mission legacyCustom;
    Check(Mission::LoadMission(path, legacyCustom, error) &&
              Mission::SemanticSourcePolicy::IsLegacyAbsent(
                  legacyCustom.entityContacts[0].semanticSourceAction,
                  legacyCustom.entityContacts[0].semanticSourceMove),
          "authored entity wording is never migrated into a hidden row");

    {
        Mission::Mission lifecycleMission;
        lifecycleMission.name = "entity lifecycle serde";
        lifecycleMission.strictEntityContacts = true;
        Mission::EntityLifecycleRequirement lifecycle;
        lifecycle.notation = "236C (SETUP)";
        lifecycle.owner = 1;
        lifecycle.slot = 6;
        lifecycle.generation = 3;
        lifecycle.lifecycle = "morph";
        lifecycle.pattern = 406;
        lifecycle.priorPattern = 400;
        lifecycle.opensAfterAction = 2;
        lifecycle.segment = 1;
        lifecycle.maxDelay = 96;
        lifecycleMission.entityLifecycles.push_back(lifecycle);

        Check(Mission::SaveMission(path, lifecycleMission, error),
              "save strict entity-lifecycle objective");
        const std::string lifecycleEncoded = ReadAll(path);
        Check(lifecycleEncoded.find("\"entityLifecycles\"") !=
                  std::string::npos &&
              lifecycleEncoded.find("\"strictEntityContacts\": true") !=
                  std::string::npos &&
              lifecycleEncoded.find("\"lifecycle\": \"morph\"") !=
                  std::string::npos &&
              lifecycleEncoded.find("\"pattern\": 406") !=
                  std::string::npos &&
              lifecycleEncoded.find("\"priorPattern\": 400") !=
                  std::string::npos &&
              lifecycleEncoded.find("\"opensAfterAction\": 2") !=
                  std::string::npos,
              "entity-lifecycle objective uses canonical JSON fields");

        Mission::Mission lifecycleLoaded;
        Check(Mission::LoadMission(path, lifecycleLoaded, error) &&
                  lifecycleLoaded.strictEntityContacts &&
                  lifecycleLoaded.entityLifecycles.size() == 1,
              "reload strict entity-lifecycle objective");
        const auto& roundTrip = lifecycleLoaded.entityLifecycles[0];
        Check(roundTrip.notation == lifecycle.notation &&
                  roundTrip.owner == lifecycle.owner &&
                  roundTrip.slot == lifecycle.slot &&
                  roundTrip.generation == lifecycle.generation &&
                  roundTrip.lifecycle == lifecycle.lifecycle &&
                  roundTrip.pattern == lifecycle.pattern &&
                  roundTrip.priorPattern == lifecycle.priorPattern &&
                  roundTrip.opensAfterAction ==
                      lifecycle.opensAfterAction &&
                  roundTrip.segment == lifecycle.segment &&
                  roundTrip.maxDelay == lifecycle.maxDelay,
              "every entity-lifecycle field round-trips");
    }

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"entity lifecycle defaults","entityLifecycles":[{}]})json";
    }
    Check(Mission::LoadMission(path, loaded, error) &&
              loaded.entityLifecycles.size() == 1,
          "load minimal entity-lifecycle requirement");
    {
        const auto& defaults = loaded.entityLifecycles[0];
        Check(defaults.notation.empty() && defaults.owner == 1 &&
                  defaults.slot == -1 && defaults.generation == 0 &&
                  defaults.lifecycle.empty() && defaults.pattern == -1 &&
                  defaults.priorPattern == -1 &&
                  defaults.opensAfterAction == -1 &&
                  defaults.segment == 0 && defaults.maxDelay == 0,
              "minimal entity lifecycle receives stable parser defaults");
    }

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad lifecycle list","entityLifecycles":{}})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("entityLifecycles is not an array") !=
                  std::string::npos,
          "malformed entity-lifecycle list fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad lifecycle entry","entityLifecycles":[7]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("entity lifecycle 0 is not an object") !=
                  std::string::npos,
          "non-object entity-lifecycle entry fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad lifecycle type","entityLifecycles":[{"lifecycle":7}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("non-string lifecycle") != std::string::npos,
          "non-string entity lifecycle kind fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"bad lifecycle range","entityLifecycles":[{"lifecycle":"morph","pattern":406,"maxDelay":-1}]})json";
    }
    Check(!Mission::LoadMission(path, loaded, error) &&
              error.find("out-of-range numeric field") !=
                  std::string::npos,
          "out-of-range entity-lifecycle field fails closed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"alias","steps":[{"notation":"5A","ids":[200],"comboEnd":"must"},{"notation":"5B","ids":[201]}]})json";
    }
    Check(Mission::LoadMission(path, loaded, error), "load comboEnd must alias");
    Check(loaded.steps[0].comboEndAfter, "comboEnd must alias is accepted");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy buffer","bgm":59,"steps":[{"notation":"5A","ids":[200]}]})json";
    }
    Check(Mission::LoadMission(path, loaded, error), "load legacy buffer-valued BGM");
    Check(loaded.bgm == -1, "unmarked legacy buffer value falls back to native BGM");

    loaded.bgm = 59;
    Check(Mission::SaveMission(path, loaded, error), "save explicit logical BGM");
    const std::string bgmEncoded = ReadAll(path);
    Check(bgmEncoded.find("\"bgmKind\": \"track\"") != std::string::npos,
          "saved logical BGM carries provenance");
    Check(Mission::LoadMission(path, loaded, error), "reload explicit logical BGM");
    Check(loaded.bgm == 59, "explicit logical BGM is not legacy-sanitized");

    {
        std::ofstream out(recordedPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"name":"legacy in-range buffer","bgm":12,"steps":[{"notation":"5A","ids":[200]}]})json";
    }
    Check(Mission::LoadMission(recordedPath, loaded, error),
          "load generated legacy recording with in-range buffer value");
    Check(loaded.bgm == -1,
          "unmarked generated recording never treats a buffer index as a logical track");

    {
        Mission::Mission m;
        m.format = 1;
        m.tutorialSchema = 1;
        m.type = "tutorial";
        m.lessonId = "efz.test.lesson";
        m.revision = 3;
        m.category = "fundamentals";
        m.order = 10;
        m.difficulty = 1;
        m.name = "Test Lesson";
        m.summary = "Round-trip everything.";
        m.requires = { "pages", "tutorial_tasks" };
        m.requiresExactBaseline = true;
        m.player.character = "nayukib";
        m.player.hasPos = true;
        m.player.posX = 0.0;
        m.player.posY = 0.0;
        m.hasLesson = true;
        m.lesson.completion = "allTasks";
        m.lesson.requirementPlacement = "belowStats";
        m.lesson.pages.push_back({ "p0", "Title", "Hold {dir:4} to guard." });
        Mission::LessonTask combat;
        combat.id = "t0"; combat.kind = "combat"; combat.label = "Jump then attack";
        Mission::LessonAction jump;
        jump.id = "jump"; jump.notation = "9"; jump.moveIds = { 5 }; jump.req = "move";
        Mission::LessonAction attack;
        attack.id = "attack"; attack.notation = "j.A"; attack.moveIds = { 207 };
        attack.fromMoveIds = { 5 };
        attack.req = "land"; attack.minGap = 2; attack.maxGap = 120;
        combat.sequence = { jump, attack };
        m.lesson.tasks.push_back(combat);
        Mission::LessonTask choice;
        choice.id = "t1"; choice.kind = "choice"; choice.prompt = "Pick one";
        choice.options = { { "a", "First" }, { "b", "Second" } };
        choice.acceptedOptionIds = { "a" };
        choice.optionFeedback["b"] = "Nope.";
        m.lesson.tasks.push_back(choice);
        m.nextLessonId = "efz.test.next";
        Check(Mission::SaveMission(lessonPath, m, error), "save tutorial lesson");
    }
    Check(Mission::LoadMission(lessonPath, loaded, error), "reload tutorial lesson");
    Check(loaded.tutorialSchema == 1 && loaded.lessonId == "efz.test.lesson",
          "tutorial identity round-trips");
    Check(loaded.requiresExactBaseline, "requiresExactBaseline round-trips");
    Check(loaded.player.hasPos && loaded.player.posX == 0.0,
          "explicit zero position survives (presence-aware)");
    Check(loaded.hasLesson && loaded.lesson.pages.size() == 1 &&
          loaded.lesson.tasks.size() == 2, "lesson block round-trips");
    Check(loaded.lesson.requirementPlacement == "belowStats",
          "authored requirement placement round-trips");
    Check(loaded.lesson.tasks[1].kind == "choice" &&
          loaded.lesson.tasks[1].acceptedOptionIds.size() == 1 &&
          loaded.lesson.tasks[1].optionFeedback.count("b") == 1,
          "choice contract round-trips");
    Check(loaded.lesson.tasks[0].sequence.size() == 2 &&
          loaded.lesson.tasks[0].sequence[1].id == "attack" &&
          loaded.lesson.tasks[0].sequence[1].req == "land" &&
          loaded.lesson.tasks[0].sequence[1].fromMoveIds.size() == 1 &&
          loaded.lesson.tasks[0].sequence[1].fromMoveIds[0] == 5 &&
          loaded.lesson.tasks[0].sequence[1].minGap == 2 &&
          loaded.lesson.tasks[0].sequence[1].maxGap == 120,
          "ordered tutorial action sequence round-trips");
    Check(loaded.nextLessonId == "efz.test.next", "next link round-trips");

    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.dup",
            "lesson":{"completion":"allTasks","tasks":[
              {"id":"t0","kind":"combat","moveIds":[4]},
              {"id":"t0","kind":"combat","moveIds":[5]}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("duplicate") != std::string::npos,
          "duplicate task ids are rejected");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badmode",
            "lesson":{"completion":"whenever","tasks":[{"id":"t0","kind":"combat","moveIds":[4]}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("completion") != std::string::npos,
          "undeclared/unknown completion mode is rejected");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.defaultplacement",
            "lesson":{"completion":"allTasks","tasks":[{"id":"t0","kind":"combat","moveIds":[4]}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.requirementPlacement == "upperLeft",
          "omitted requirement placement uses the stable upper-left default");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badplacement",
            "lesson":{"completion":"allTasks","requirementPlacement":"movingTarget",
            "tasks":[{"id":"t0","kind":"combat","moveIds":[4]}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("requirementPlacement") != std::string::npos,
          "unknown requirement placement is rejected with the field named");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badchoice",
            "lesson":{"completion":"allTasks","tasks":[{"id":"q","kind":"choice",
            "prompt":"Pick","options":[{"id":"a","label":"A"},{"id":"b","label":"B"}],
            "acceptedOptionIds":["missing"]}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("missing option") != std::string::npos,
          "choice accepted IDs must name real options");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badsequence",
            "lesson":{"completion":"allTasks","tasks":[{"id":"route","kind":"combat",
            "sequence":[{"id":"a","moveIds":[200]},{"id":"a","moveIds":[201]}]}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("duplicates sequence action") != std::string::npos,
          "ordered action IDs are stable and unique");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badsource",
            "lesson":{"completion":"allTasks","tasks":[{"id":"route","kind":"combat",
            "sequence":[{"id":"a","moveIds":[163],"fromMoveIds":[0]},
            {"id":"b","moveIds":[207]}]}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("first action") != std::string::npos,
          "the first ordered action cannot claim a cancel source");

    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.goal",
            "lesson":{"completion":"allTasks","tasks":[{"id":"build","kind":"combat",
            "goalState":{"field":"sp","player":1,"op":"ge","value":1000,
            "requiresBlock":true}}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error),
          "goalState task with no move contract loads");
    Check(loaded.lesson.tasks[0].hasGoalState &&
          loaded.lesson.tasks[0].goalState.field == "sp" &&
          loaded.lesson.tasks[0].goalState.op == "ge" &&
          loaded.lesson.tasks[0].goalState.value == 1000 &&
          loaded.lesson.tasks[0].goalState.requiresBlock,
          "goalState round-trips through load");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"requiresBlock\": true") != std::string::npos,
          "goalState is serialized");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.juggle_goal",
            "lesson":{"completion":"allTasks","tasks":[{"id":"wait","kind":"combat",
            "goalState":{"field":"untech","player":2,"op":"le","value":0}}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].goalState.field == "untech" &&
          loaded.lesson.tasks[0].goalState.player == 2,
          "juggle-state untech goal parses without a fake move contract");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.airtech_goal",
            "lesson":{"completion":"allTasks","tasks":[{"id":"tech","kind":"combat",
            "goalState":{"field":"airtech","player":2,"op":"eq","value":1}}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error),
          "juggle-state airtech classifier goal loads");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_interception",
            "lesson":{"completion":"allTasks","tasks":[{"id":"probe","kind":"combat",
            "moveIds":[309],"req":"projectileInterception"}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("needs projectileInterception") != std::string::npos,
          "projectile interception cannot omit its exact pattern pair");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.interception",
            "lesson":{"completion":"allTasks","tasks":[{"id":"probe","kind":"combat",
            "moveIds":[309],"req":"projectileInterception",
            "projectileInterception":{"incomingPattern":401,"guardPattern":423}}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].hasProjectileInterception &&
          loaded.lesson.tasks[0].incomingProjectilePattern == 401 &&
          loaded.lesson.tasks[0].guardProjectilePattern == 423,
          "curated projectile interception pattern pair parses exactly");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"projectileInterception\"") != std::string::npos,
          "projectile interception pattern pair serializes");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.fic",
            "lesson":{"completion":"allTasks","tasks":[{"id":"fic","kind":"combat",
            "sequence":[{"id":"cast","moveIds":[250],"req":"move"},
            {"id":"ic","moveIds":[167],"fromMoveIds":[250],"inputMask":64,
            "req":"commit","flickerIC":{"projectilePattern":401,"minDistance":300}}]}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].sequence[1].hasFlickerIC &&
          loaded.lesson.tasks[0].sequence[1].flickerProjectilePattern == 401 &&
          loaded.lesson.tasks[0].sequence[1].flickerMinDistance == 300,
          "curated FIC evidence parses on the committed IC destination");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"flickerIC\"") != std::string::npos,
          "curated FIC evidence serializes");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.gatelessfic",
            "lesson":{"completion":"allTasks","tasks":[{"id":"fic","kind":"combat",
            "sequence":[{"id":"cast","moveIds":[250],"req":"move"},
            {"id":"ic","moveIds":[167],"fromMoveIds":[250],"inputMask":64,
            "req":"commit","flickerIC":{"projectilePattern":401}}]}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].sequence[1].hasFlickerIC &&
          loaded.lesson.tasks[0].sequence[1].flickerMinDistance == 0,
          "FIC evidence may omit the spacing gate (proof carried by contact invariants)");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badgoal",
            "lesson":{"completion":"allTasks","tasks":[{"id":"g","kind":"combat",
            "goalState":{"field":"nonsense","op":"ge","value":1}}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("unknown field") != std::string::npos,
          "goalState with an unknown field is rejected");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.mixgoal",
            "lesson":{"completion":"allTasks","tasks":[{"id":"g","kind":"combat","moveIds":[200],
            "goalState":{"field":"sp","op":"ge","value":1000}}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("cannot mix") != std::string::npos,
          "goalState cannot mix with a move contract");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.untech_absence",
            "lesson":{"completion":"allTasks","tasks":[{"id":"watch","kind":"combat",
            "absence":{"forbid":"attack","start":"dummyUntech","end":"dummyUntechEmpty"}}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].absenceStart == "dummyUntech" &&
          loaded.lesson.tasks[0].absenceEnd == "dummyUntechEmpty",
          "untech-driven absence window parses exactly");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("dummyUntechEmpty") != std::string::npos,
          "untech-driven absence window serializes");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.dummy_attack_absence",
            "lesson":{"completion":"allTasks","tasks":[{"id":"bait","kind":"combat","script":"ep",
            "absence":{"forbid":"attack","start":"taskArmed","end":"dummyAttackEnd"}}],
            "dummyScript":{"episodes":[{"id":"ep","ownerTask":"bait","kind":"macro",
            "action":"623C","start":"trigger","trigger":"onWakeup"}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].absenceEnd == "dummyAttackEnd",
          "dummy-attack-driven absence window parses exactly");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("dummyAttackEnd") != std::string::npos,
          "dummy-attack-driven absence window serializes");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.episode_cycle_absence",
            "lesson":{"completion":"allTasks","tasks":[{"id":"defend","kind":"combat","script":"ep",
            "absence":{"forbid":"attack","start":"taskArmed","end":"episodeCycleEnd",
            "failOnHit":true,"minBlocks":2}}],
            "dummyScript":{"episodes":[{"id":"ep","ownerTask":"defend","kind":"macro",
            "action":"5A","approach":"none","start":"taskArmed"}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].absenceEnd == "episodeCycleEnd" &&
          loaded.lesson.tasks[0].absenceFailOnHit &&
          loaded.lesson.tasks[0].absenceMinBlocks == 2,
          "episode-cycle absence evidence parses exactly");
    Check(Mission::SaveMission(lessonPath, loaded, error),
          "episode-cycle absence evidence serializes");
    Check(ReadAll(lessonPath).find("\"episodeCycleEnd\"") != std::string::npos &&
          ReadAll(lessonPath).find("\"failOnHit\": true") != std::string::npos &&
          ReadAll(lessonPath).find("\"minBlocks\": 2") != std::string::npos,
          "episode-cycle absence fields survive serialization");
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].absenceFailOnHit &&
          loaded.lesson.tasks[0].absenceMinBlocks == 2,
          "episode-cycle absence fields round-trip through disk");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_absence_blocks",
            "lesson":{"completion":"allTasks","tasks":[{"id":"watch","kind":"combat",
            "absence":{"forbid":"attack","start":"taskArmed","end":{"ticks":30},
            "minBlocks":-1}}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("minBlocks must be nonnegative") != std::string::npos,
          "absence minBlocks rejects negative counts");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_episode_cycle_absence",
            "lesson":{"completion":"allTasks","tasks":[{"id":"watch","kind":"combat",
            "absence":{"forbid":"attack","start":"taskArmed","end":"episodeCycleEnd"}}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("episodeCycleEnd needs") != std::string::npos,
          "episode-cycle absence requires a task-owned script");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_episode_cycle_start",
            "lesson":{"completion":"allTasks","tasks":[{"id":"watch","kind":"combat","script":"ep",
            "absence":{"forbid":"attack","start":"launched","end":"episodeCycleEnd"}}],
            "dummyScript":{"episodes":[{"id":"ep","ownerTask":"watch","kind":"macro",
            "action":"5A","approach":"none","start":"taskArmed"}]}}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("episodeCycleEnd needs") != std::string::npos,
          "episode-cycle absence requires a taskArmed window");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_absence_gate",
            "lesson":{"completion":"allTasks","tasks":[{"id":"watch","kind":"combat",
            "absence":{"forbid":"attack","start":"dummyStanding","end":"dummyUntechEmpty"}}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("absence start") != std::string::npos,
          "unknown absence start gate is rejected");

    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.contact",
            "lesson":{"completion":"allTasks","tasks":[{"id":"throw","kind":"combat",
            "moveIds":[249],"req":"move","completionHoldTicks":0,"restoreOnSuccess":true,
            "contact":{"attacker":"learner","target":"dummy","source":"direct",
            "result":"throw","targetStateBefore":"downed",
            "moveIds":[249],"count":1}}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error),
          "structured direct-contact task loads");
    Check(loaded.lesson.tasks[0].hasContact &&
          loaded.lesson.tasks[0].contact.result == "throw" &&
          loaded.lesson.tasks[0].contact.targetStateBefore == "downed" &&
          loaded.lesson.tasks[0].contact.moveIds.size() == 1 &&
          loaded.lesson.tasks[0].completionHoldTicks == 0 &&
          loaded.lesson.tasks[0].restoreOnSuccess,
          "contact and immediate-restore policy parse exactly");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"contact\"") != std::string::npos &&
          ReadAll(lessonPath).find("\"targetStateBefore\": \"downed\"") != std::string::npos &&
          ReadAll(lessonPath).find("\"restoreOnSuccess\": true") != std::string::npos,
          "contact and restore policy serialize");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badcontact",
            "lesson":{"completion":"allTasks","tasks":[{"id":"bad","kind":"combat",
            "moveIds":[200],"contact":{"result":"maybe","moveIds":[200]}}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("unknown result") != std::string::npos,
          "unknown contact outcomes are rejected");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.commit",
            "lesson":{"completion":"allTasks","tasks":[{"id":"versions","kind":"combat",
            "sequence":[{"id":"a","moveIds":[256],"inputMask":16,"req":"commit"},
            {"id":"b","moveIds":[256],"inputMask":32,"req":"commit"}]}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].sequence[1].req == "commit",
          "input-to-move commit sequence loads");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.block",
            "lesson":{"completion":"allTasks","tasks":[{"id":"tick","kind":"combat",
            "sequence":[{"id":"jab","moveIds":[200],"req":"block"},
            {"id":"throw","moveIds":[221],"req":"move"}]}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].sequence[0].req == "block",
          "defender-block-state sequence action loads");

    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.episode",
            "lesson":{"completion":"allTasks","tasks":[{"id":"guard","kind":"combat",
            "moveIds":[168],"script":"ep"}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"guard","kind":"macro","action":"5B","approach":"none",
            "start":"taskArmed","repeat":{"mode":"once","delay":30}}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error),
          "supported once-per-attempt dummy episode loads");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.episode_scripts",
            "lesson":{"completion":"allTasks","tasks":[
            {"id":"dash_cancel","kind":"combat","moveIds":[168],"script":"ep_dash_cancel"},
            {"id":"dash_approach","kind":"combat","moveIds":[168],"script":"ep_dash_approach"}],
            "dummyScript":{"episodes":[
            {"id":"ep_dash_cancel","ownerTask":"dash_cancel","kind":"macro","approach":"none",
             "start":"taskArmed","script":[{"action":"66B"},{"action":"623C","cancel":true,"waitTicks":8}]},
            {"id":"ep_dash_approach","ownerTask":"dash_approach","kind":"macro","approach":"dash",
             "start":"taskArmed","script":[{"action":"5A"},{"action":"5B","cancel":true,"waitTicks":10}]}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.episodes.size() == 2 &&
          loaded.lesson.episodes[0].script.size() == 2 &&
          loaded.lesson.episodes[0].script[1].waitTicks == 8 &&
          loaded.lesson.episodes[1].approach == "dash" &&
          loaded.lesson.episodes[1].script[1].waitTicks == 10,
          "multi-action episodes load a first-step dash normal and a scripted dash approach");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"waitTicks\": 8") != std::string::npos,
          "script destination delay serializes");
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.episodes.size() == 2 &&
          loaded.lesson.episodes[1].approach == "dash" &&
          loaded.lesson.episodes[1].script[1].waitTicks == 10,
          "script destination delay and default dash approach round-trip");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_late_dash_normal",
            "lesson":{"completion":"allTasks","tasks":[{"id":"pattern","kind":"combat",
            "moveIds":[168],"script":"ep"}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"pattern","kind":"macro","start":"taskArmed",
            "script":[{"action":"5A"},{"action":"66B","cancel":true,"waitTicks":8}]}]}}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("cannot ride the ordinary injection lanes") != std::string::npos,
          "only the first scripted action may use the dedicated dash-normal driver");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.telegraph",
            "lesson":{"completion":"allTasks","tasks":[{"id":"rg","kind":"combat",
            "moveIds":[168],"script":"ep"}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"rg","kind":"macro","action":"66B","approach":"none","telegraph":"jump",
            "start":"taskArmed","repeat":{"mode":"loop","delay":150}}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.episodes[0].telegraph == "jump",
          "a dash-normal episode loads its neutral-hop telegraph");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"telegraph\": \"jump\"") != std::string::npos,
          "episode telegraph serializes");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.telegraph_special",
            "lesson":{"completion":"allTasks","tasks":[{"id":"armor","kind":"combat",
            "moveIds":[221],"script":"ep"}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"armor","kind":"macro","action":"41236C","approach":"none","telegraph":"jump",
            "start":"taskArmed","repeat":{"mode":"loop","delay":150}}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.episodes[0].telegraph == "jump" &&
          loaded.lesson.episodes[0].action == "41236C",
          "a buffered-special opener accepts the neutral-hop telegraph");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_telegraph",
            "lesson":{"completion":"allTasks","tasks":[{"id":"rg","kind":"combat",
            "moveIds":[168],"script":"ep"}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"rg","kind":"macro","action":"236A","approach":"none","telegraph":"jump",
            "start":"playerState","predicate":"p1move:200",
            "repeat":{"mode":"loop","delay":150}}]}}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("telegraph needs a taskArmed macro opener") != std::string::npos,
          "telegraph is rejected on a reactive (non-taskArmed) episode");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.rflock",
            "player":{"rf":1000,"blueIC":0,"rfLock":true},
            "dummy":{"rf":1000,"blueIC":0,"rfLock":true},
            "lesson":{"completion":"allTasks","tasks":[{"id":"t","kind":"combat",
            "moveIds":[200]}]}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.player.rfLock && loaded.dummy.rfLock &&
          loaded.player.rf == 1000 && loaded.dummy.rf == 1000,
          "session rf locks parse on both sides");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"rfLock\": true") != std::string::npos,
          "session rf lock serializes");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.badrflock",
            "player":{"rfLock":true},
            "lesson":{"completion":"allTasks","tasks":[{"id":"t","kind":"combat",
            "moveIds":[200]}]}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("rfLock needs an authored rf value") != std::string::npos,
          "an rf lock without an authored rf value is rejected");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.rg_chain",
            "lesson":{"completion":"allTasks","tasks":[{"id":"pattern","kind":"combat",
            "moveIds":[168],"script":"ep","pos":{"x":20.1234567890123,"y":-3.125},
            "dummyPos":{"x":80.9876543210987,"y":0}}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"pattern","kind":"macro","action":"5A>5B>2C","approach":"none",
            "start":"taskArmed","repeat":{"mode":"loop","delay":120}}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.tasks[0].hasPos &&
          std::abs(loaded.lesson.tasks[0].posX - 20.1234567890123) < 1e-12 &&
          std::abs(loaded.lesson.tasks[0].posY - (-3.125)) < 1e-12 &&
          loaded.lesson.tasks[0].hasDummyPos &&
          std::abs(loaded.lesson.tasks[0].dummyPosX - 80.9876543210987) < 1e-12 &&
          loaded.lesson.episodes[0].action == "5A>5B>2C",
          "contact-chain episode and double-precision task positions load");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"dummyPos\"") != std::string::npos,
          "task-scoped dummy position serializes");
    Mission::Mission precisionReload;
    Check(Mission::LoadMission(lessonPath, precisionReload, error) &&
          std::abs(precisionReload.lesson.tasks[0].posX -
                   20.1234567890123) < 1e-12 &&
          std::abs(precisionReload.lesson.tasks[0].dummyPosX -
                   80.9876543210987) < 1e-12,
          "task-scoped fighter positions retain double precision after save/reload");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_rg_chain",
            "lesson":{"completion":"allTasks","tasks":[{"id":"pattern","kind":"combat",
            "moveIds":[168],"script":"ep"}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"pattern","kind":"macro","action":"5A>5B>2C","approach":"dash",
            "start":"taskArmed"}]}}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("requires taskArmed start and no approach") != std::string::npos,
          "contact-chain script rejects an extra automatic approach");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_episode_action",
            "lesson":{"completion":"allTasks","tasks":[{"id":"guard","kind":"combat",
            "moveIds":[168],"script":"ep"}],"dummyScript":{"episodes":[{"id":"ep",
            "ownerTask":"guard","kind":"macro","action":"63214C","start":"taskArmed"}]}}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("unsupported action") != std::string::npos,
          "dummy episode cannot claim an injection action the runtime cannot drive");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.variant_group",
            "lesson":{"completion":"allTasks","tasks":[
            {"id":"one","kind":"combat","moveIds":[200],"script":"ep1"},
            {"id":"two","kind":"combat","moveIds":[200],"script":"ep2"}],
            "dummyScript":{"episodes":[
            {"id":"ep1","ownerTask":"one","kind":"idle","variants":["idle","block"],"variantGroup":"pair"},
            {"id":"ep2","ownerTask":"two","kind":"idle","variants":["idle","block"],"variantGroup":"pair"}]}}})json";
    }
    Check(Mission::LoadMission(lessonPath, loaded, error) &&
          loaded.lesson.episodes.size() == 2 &&
          loaded.lesson.episodes[0].variantGroup == "pair" &&
          loaded.lesson.episodes[1].variantGroup == "pair",
          "a balanced dummy-variant group parses");
    Check(Mission::SaveMission(lessonPath, loaded, error) &&
          ReadAll(lessonPath).find("\"variantGroup\": \"pair\"") != std::string::npos,
          "a balanced dummy-variant group serializes");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_variant_group",
            "lesson":{"completion":"allTasks","tasks":[
            {"id":"one","kind":"combat","moveIds":[200],"script":"ep1"},
            {"id":"two","kind":"combat","moveIds":[200],"script":"ep2"}],
            "dummyScript":{"episodes":[
            {"id":"ep1","ownerTask":"one","kind":"idle","variants":["idle","block"],"variantGroup":"pair"},
            {"id":"ep2","ownerTask":"two","kind":"idle","variants":["idle","rg"],"variantGroup":"pair"}]}}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("mismatched variants pools") != std::string::npos,
          "a grouped variant bag rejects mismatched member pools");
    {
        std::ofstream out(lessonPath, std::ios::binary | std::ios::trunc);
        out << R"json({"format":1,"tutorialSchema":1,"type":"tutorial","id":"efz.test.bad_episode_owner",
            "lesson":{"completion":"allTasks","tasks":[{"id":"one","kind":"combat",
            "moveIds":[200],"script":"ep"},{"id":"two","kind":"combat","moveIds":[201]}],
            "dummyScript":{"episodes":[{"id":"ep","ownerTask":"two","kind":"idle"}]}}})json";
    }
    Check(!Mission::LoadMission(lessonPath, loaded, error) &&
          error.find("owned by task") != std::string::npos,
          "a task cannot borrow another task's dummy episode");

    if (argc > 1) {
        int lessonCount = 0;
        bool sawInputEdgeLesson = false;
        bool sawOrderedLesson = false;
        bool sawCorrectNayukiCancelIds = false;
        bool sawJumpingMisakiAirString = false;
        bool sawCorrectAirRecoveryDrills = false;
        bool sawCorrectAirRecoveryDecisionDrills = false;
        bool sawCorrectAirRecoveryPunish = false;
        bool sawCorrectNayukiRfIds = false;
        bool sawCorrectBicSafetyContract = false;
        bool sawAllTakeSpaceGuardReactions = false;
        bool sawCorrectNayukiAntiAirs = false;
        bool sawOrderedWakeupThrow = false;
        bool sawWakeupBaitCarry = false;
        bool sawWakeupKnockdownRoutes = false;
        bool sawMisaki214CReversal = false;
        bool sawMisaki214CResources = false;
        bool sawLiveJuggleContinuation = false;
        bool sawStrictWakeupJb = false;
        bool sawCharacterCounterFiveB = false;
        bool sawCorrectRedIcContract = false;
        bool sawCorrectBicRetryContract = false;
        bool sawSpendRfBlueParity = false;
        bool sawCorrectRgDrills = false;
        bool sawStrictRgResponse = false;
        bool sawAuthoredMaiOtgRoute = false;
        bool sawStrictFlickerIc = false;
        bool sawCenteredFlickerIc = false;
        bool sawPowerStatsPlacement = false;
        bool sawPowerSingleComboRoute = false;
        bool sawDirectDashMomentumJump = false;
        bool sawDirectArmorCancelEscape = false;
        bool sawBufferedArmorCommandThrow = false;
        bool sawRfBlockCompletion = false;
        bool sawGuidedHitConfirm = false;
        bool sawStableGuardOpeners = false;
        bool sawTrueGuardAttackDashNormal = false;
        bool sawGuardGaugeRecoveryLauncher = false;
        bool sawDashWhiffPunishRoute = false;
        bool sawPressureInteractionProofs = false;
        bool sawChooseRgBlockProof = false;
        bool sawAttackIntoRgCancelRace = false;
        bool sawEndTurnInteractionProof = false;
        bool sawStrategicRfProof = false;
        int launchableCount = 0;
        int placeholderTaskCount = 0;
        int missingEpisodeCount = 0;
        int sourcedLessonCount = 0;
        int shippedTaskCount = 0;
        int shippedEpisodeCount = 0;
        const std::filesystem::path root(argv[1]);
        const std::filesystem::path repositoryRoot =
            root.parent_path().parent_path().parent_path();
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
                entry.path().filename() == "pack.json") {
                continue;
            }
            Mission::Mission curriculumLesson;
            error.clear();
            if (!Mission::LoadMission(entry.path().string(), curriculumLesson, error)) {
                std::cerr << "FAILED: shipped lesson " << entry.path().string()
                          << " did not load: " << error << '\n';
                return 1;
            }
            Check(curriculumLesson.tutorialSchema == 1 && curriculumLesson.hasLesson,
                  "shipped curriculum entry uses the tutorial schema");
            shippedTaskCount += static_cast<int>(curriculumLesson.lesson.tasks.size());
            shippedEpisodeCount += static_cast<int>(curriculumLesson.lesson.episodes.size());
            if (!curriculumLesson.sourceRefs.empty()) ++sourcedLessonCount;
            for (const auto& sourceRef : curriculumLesson.sourceRefs) {
                static const std::string localPrefix = "local:";
                if (sourceRef.rfind(localPrefix, 0) != 0) continue;
                std::string relative = sourceRef.substr(localPrefix.size());
                const std::size_t locator = relative.find("::");
                if (locator != std::string::npos) relative.resize(locator);
                Check(!relative.empty() &&
                      std::filesystem::exists(repositoryRoot / relative),
                      "tutorial local source reference resolves inside the repository");
            }
            for (const auto& task : curriculumLesson.lesson.tasks) {
                if (task.inputMask != 0 && task.req == "input") sawInputEdgeLesson = true;
                if (!task.sequence.empty()) sawOrderedLesson = true;
                for (int id : task.moveIds) if (id <= 0) ++placeholderTaskCount;
            }
            if (curriculumLesson.lessonId == "efz.offense.power_scaling") {
                sawPowerStatsPlacement =
                    curriculumLesson.lesson.requirementPlacement == "belowStats";
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "watch_power") {
                        sawPowerSingleComboRoute = task.continuity == "sameCombo" &&
                            task.sequence.size() == 3 && task.endsCombo;
                    }
                }
            }
            if (curriculumLesson.lessonId == "efz.fundamentals.ground_dash") {
                const bool declaresCancelTransition =
                    std::find(curriculumLesson.requires.begin(),
                              curriculumLesson.requires.end(),
                              "cancel_transition") != curriculumLesson.requires.end();
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "dash_jump" || task.sequence.size() != 2) continue;
                    const auto& dash = task.sequence[0];
                    const auto& jump = task.sequence[1];
                    sawDirectDashMomentumJump = declaresCancelTransition &&
                        dash.moveIds == std::vector<int>{163} &&
                        jump.moveIds == std::vector<int>{4} &&
                        jump.fromMoveIds == std::vector<int>{163};
                }
            }
            if (curriculumLesson.lessonId == "efz.systems.recoil_armor") {
                const bool declaresCancelTransition =
                    std::find(curriculumLesson.requires.begin(),
                              curriculumLesson.requires.end(),
                              "cancel_transition") != curriculumLesson.requires.end();
                bool superBreaksArmor = false;
                bool breakUsesCStartup = false;
                bool noDummyRfGate = curriculumLesson.dummy.rf < 0;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    noDummyRfGate = noDummyRfGate && task.dummySeed.rf < 0;
                    if (task.id == "super_armor" && task.sequence.size() == 2) {
    // The 236236 connect deals 0 damage (throw-like), so only the resolver contact during 300-302
    // with defenderMoveBefore 264 can prove the armored state.
                        const auto& connect = task.sequence[0];
                        const auto& seal = task.sequence[1];
                        superBreaksArmor =
                            connect.moveIds == std::vector<int>{300, 301, 302} &&
                            connect.req == "land" &&
                            connect.dummyStateMoveIds == std::vector<int>{264} &&
                            connect.hasContact &&
                            connect.contact.source == "direct" &&
                            connect.contact.result == "hit" &&
                            connect.contact.moveIds ==
                                std::vector<int>{300, 301, 302} &&
                            seal.moveIds == std::vector<int>{303, 304, 305} &&
                            seal.fromMoveIds == std::vector<int>{300, 301, 302} &&
                            seal.req == "move" &&
                            task.playerSeed.meter == 3000;
                    } else if (task.id == "break_armor") {
                        breakUsesCStartup = task.dummyStateMoveIds ==
                                std::vector<int>{264} &&
                            task.hasContact &&
                            task.contact.source == "direct" &&
                            task.contact.result == "hit" &&
                            task.contact.moveIds == std::vector<int>{206};
                    } else if (task.id == "cancel_escape" &&
                               task.sequence.size() == 2) {
                        const auto& poke = task.sequence[0];
                        const auto& jump = task.sequence[1];
                        sawDirectArmorCancelEscape = declaresCancelTransition &&
                            poke.moveIds == std::vector<int>{200} &&
                            poke.dummyStateMoveIds == std::vector<int>{264} &&
                            jump.moveIds == std::vector<int>{4} &&
                            jump.fromMoveIds == std::vector<int>{200};
                    }
                }
                int bufferedCCommandEpisodes = 0;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    if (episode.kind != "macro" ||
                        episode.action != "41236C" ||
                        episode.approach != "none") {
                        continue;
                    }
                    if ((episode.id == "ep_armor_super" &&
                         episode.ownerTask == "super_armor" &&
                         episode.start == "taskArmed" &&
                         episode.telegraph == "jump") ||
                        (episode.id == "ep_armor_break" &&
                         episode.ownerTask == "break_armor" &&
                         episode.start == "taskArmed" &&
                         episode.telegraph == "jump") ||
                        (episode.id == "ep_cmd_throw" &&
                         episode.ownerTask == "cancel_escape" &&
                         episode.start == "taskArmed" &&
                         episode.telegraph == "jump")) {
                        ++bufferedCCommandEpisodes;
                    }
                }
                sawBufferedArmorCommandThrow = noDummyRfGate &&
                    superBreaksArmor &&
                    breakUsesCStartup && bufferedCCommandEpisodes == 3;
            }
            if (curriculumLesson.lessonId == "efz.offense.cancel_routes") {
                bool saw236A = false;
                bool saw623A = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    for (const auto& action : task.sequence) {
                        if (task.id == "showcase" && action.id == "236a" &&
                            action.moveIds == std::vector<int>{253}) {
                            saw236A = true;
                        }
                        if (task.id == "dash_special" && action.id == "623a" &&
                            action.moveIds == std::vector<int>{250}) {
                            saw623A = true;
                        }
                    }
                }
                sawCorrectNayukiCancelIds = saw236A && saw623A;
            }
            if (curriculumLesson.lessonId == "efz.offense.rf_gauge") {
                bool blockGoal = false;
                bool pressure = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "block_faster") continue;
                    blockGoal = task.hasGoalState &&
                        task.goalState.field == "rf" &&
                        task.goalState.player == 1 &&
                        task.goalState.op == "ge" && task.goalState.value == 500 &&
                        task.goalState.requiresBlock &&
                        task.hasCompare && task.compareVs == "reach_red" &&
                        task.compareMetric == "ticks" && task.compareOp.empty() &&
                        task.script == "ep_rf_pressure";
                }
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    pressure |= episode.id == "ep_rf_pressure" &&
                        episode.ownerTask == "block_faster" &&
                        episode.kind == "macro" &&
                        episode.action == "5A>5B>2C" &&
                        episode.approach == "none" &&
                        episode.repeatMode == "loop";
                }
                sawRfBlockCompletion = blockGoal && pressure;
            }
            if (curriculumLesson.lessonId == "efz.offense.jump_cancels") {
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    if (episode.id == "ep_air_string_jump" &&
                        episode.ownerTask == "air_string" &&
                        episode.kind == "macro" && episode.action == "jump" &&
                        episode.approach == "none" && episode.start == "taskArmed" &&
                        episode.repeatMode == "loop") {
                        sawJumpingMisakiAirString = true;
                    }
                }
            }
            if (curriculumLesson.lessonId == "efz.fundamentals.air_recovery") {
                bool away = false, toward = false, noTech = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "tech_away") {
                        away = task.moveIds == std::vector<int>{158} &&
                               task.req == "move" && task.script == "ep_tech_away";
                    } else if (task.id == "tech_toward") {
                        toward = task.moveIds == std::vector<int>{157} &&
                                 task.req == "move" && task.script == "ep_tech_toward";
                    } else if (task.id == "no_tech") {
                        noTech = task.hasAbsence && task.absenceForbid == "airtech" &&
                                 task.absenceStart == "launched" &&
                                 task.absenceEnd == "grounded" &&
                                 task.script == "ep_no_tech";
                    }
                }
                bool awayLauncher = false, towardLauncher = false, noTechLauncher = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    const bool isLauncher = episode.kind == "macro" &&
                        episode.action == "236A" && episode.approach == "none" &&
                        episode.start == "taskArmed";
                    awayLauncher |= isLauncher && episode.id == "ep_tech_away" &&
                                    episode.ownerTask == "tech_away";
                    towardLauncher |= isLauncher && episode.id == "ep_tech_toward" &&
                                      episode.ownerTask == "tech_toward";
                    noTechLauncher |= isLauncher && episode.id == "ep_no_tech" &&
                                      episode.ownerTask == "no_tech";
                }
                sawCorrectAirRecoveryDrills = away && toward && noTech &&
                    awayLauncher && towardLauncher && noTechLauncher;
            }
            if (curriculumLesson.lessonId == "efz.defense.air_recovery_decisions") {
                bool observe = false, noRecover = false, recoverAway = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "observe_launch") {
                        observe = task.hasGoalState && task.goalState.field == "launched" &&
                                  task.goalState.player == 1 &&
                                  task.script == "ep_observe_launch";
                    } else if (task.id == "no_recover") {
                        noRecover = task.hasAbsence && task.absenceForbid == "airtech" &&
                                    task.absenceStart == "launched" &&
                                    task.absenceEnd == "grounded" &&
                                    task.script == "ep_no_recover";
                    } else if (task.id == "recover_away") {
                        recoverAway = task.moveIds == std::vector<int>{158} &&
                                      task.req == "move" &&
                                      task.script == "ep_recover_away";
                    }
                }
                bool observeLauncher = false, nearLauncher = false, farLauncher = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    const bool isProjectileLauncher = episode.kind == "macro" &&
                        episode.action == "236A" && episode.approach == "none" &&
                        episode.start == "taskArmed";
                    observeLauncher |= isProjectileLauncher && episode.id == "ep_observe_launch" &&
                                       episode.ownerTask == "observe_launch";
                    nearLauncher |= isProjectileLauncher && episode.id == "ep_no_recover" &&
                                    episode.ownerTask == "no_recover";
                    farLauncher |= episode.kind == "macro" && episode.action == "66C" &&
                                   episode.approach == "none" &&
                                   episode.start == "taskArmed" &&
                                   episode.repeatMode == "loop" &&
                                   episode.repeatDelay == 240 &&
                                   episode.id == "ep_recover_away" &&
                                   episode.ownerTask == "recover_away";
                }
                sawCorrectAirRecoveryDecisionDrills = observe && noRecover && recoverAway &&
                    observeLauncher && nearLauncher && farLauncher;
            }
            if (curriculumLesson.lessonId == "efz.defense.air_recovery_punish") {
                bool route = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "recovery_punish" || task.sequence.size() != 2) continue;
                    const auto& launch = task.sequence[0];
                    const auto& punish = task.sequence[1];
                    route = launch.moveIds == std::vector<int>{254} &&
                        launch.req == "land" &&
                        punish.moveIds == std::vector<int>{249} &&
                        punish.req == "move" && task.script == "ep_airtech_fwd";
                }
                bool ownsForwardTech = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    ownsForwardTech |= episode.id == "ep_airtech_fwd" &&
                        episode.ownerTask == "recovery_punish" &&
                        episode.kind == "airtech" && episode.clip == "forward";
                }
                sawCorrectAirRecoveryPunish = route && ownsForwardTech &&
                    curriculumLesson.player.character == "misaki" &&
                    curriculumLesson.dummy.character == "nayukib";
            }
            if (curriculumLesson.lessonId == "efz.offense.rf_attacks") {
                bool saw623C = false;
                bool saw623BFallback = false;
                bool sawBicSafety = false;
                bool sawBlockingAnswer = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "rf_version" && task.moveIds == std::vector<int>{252}) {
                        saw623C = true;
                    }
                    if (task.id == "rf_fallback" && task.moveIds == std::vector<int>{251}) {
                        saw623BFallback = true;
                    }
                    if (task.id == "bic_safety" && task.sequence.size() == 3) {
                        const auto& blocked = task.sequence[0];
                        const auto& bic = task.sequence[1];
                        const auto& answer = task.sequence[2];
                        sawBicSafety = task.playerSeed.rf == 1000 &&
                            task.playerSeed.blueIC == 1 &&
                            blocked.moveIds == std::vector<int>{252} &&
                            blocked.inputMask == 64 && blocked.req == "block" &&
                            bic.moveIds == std::vector<int>{167, 171} &&
                            bic.fromMoveIds == std::vector<int>{252} &&
                            bic.inputMask == 64 && bic.req == "commit" &&
                            answer.moveIds ==
                                std::vector<int>{150, 151, 152, 153, 154, 155,
                                                 156, 16};
                    }
                }
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    sawBlockingAnswer |= episode.id == "ep_bic_answer" &&
                        episode.ownerTask == "bic_safety" &&
                        episode.kind == "block_answer" && episode.action == "5A";
                }
                sawCorrectNayukiRfIds = saw623C && saw623BFallback;
                sawCorrectBicSafetyContract = sawBicSafety && sawBlockingAnswer;
            }
            if (curriculumLesson.lessonId == "efz.offense.red_ic") {
                const bool declaresInputEdges =
                    std::find(curriculumLesson.requires.begin(),
                              curriculumLesson.requires.end(), "input_edges") !=
                    curriculumLesson.requires.end();
                const bool declaresCommitMapping =
                    std::find(curriculumLesson.requires.begin(),
                              curriculumLesson.requires.end(), "input_commit_mapping") !=
                    curriculumLesson.requires.end();
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "ic_drill" || task.sequence.size() != 3) continue;
                    const auto& ic = task.sequence[1];
                    sawCorrectRedIcContract = declaresInputEdges && declaresCommitMapping &&
                        ic.moveIds == std::vector<int>{167} &&
                        ic.fromMoveIds == std::vector<int>{203} &&
                        ic.inputMask == 64 && ic.req == "commit";
                }
            }
            if (curriculumLesson.lessonId == "efz.offense.blue_ic") {
                bool bicUsesC = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "bic_route" || task.sequence.size() != 5) continue;
                    const auto& bic = task.sequence[1];
                    bicUsesC = bic.moveIds == std::vector<int>{167} &&
                        bic.fromMoveIds == std::vector<int>{232} &&
                        bic.inputMask == 64 && bic.req == "commit";
                }
                sawCorrectBicRetryContract = bicUsesC &&
                    curriculumLesson.player.rf == 1000 &&
                    curriculumLesson.player.blueIC == 1 &&
                    curriculumLesson.player.meter >= 2000 &&
                    curriculumLesson.player.posX == 20.0 &&
                    curriculumLesson.dummy.posX == 160.0 &&
                    curriculumLesson.lesson.wrongAction == "fail" &&
                    curriculumLesson.lesson.failureReset == "taskCheckpoint";
            }
            if (curriculumLesson.lessonId == "efz.offense.flicker_ic") {
                bool route = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "fic_projectile" || task.sequence.size() != 2) continue;
                    const auto& cast = task.sequence[0];
                    const auto& fic = task.sequence[1];
                    route = cast.moveIds == std::vector<int>{250} &&
                        cast.req == "move" &&
                        fic.moveIds == std::vector<int>{167} &&
                        fic.fromMoveIds == std::vector<int>{250} &&
                        fic.inputMask == 64 && fic.req == "commit" &&
                        fic.hasFlickerIC && fic.flickerProjectilePattern == 401 &&
                        fic.flickerMinDistance == 0 &&
                        task.script == "ep_fic_idle";
                }
                sawStrictFlickerIc = route &&
                    curriculumLesson.player.character == "sayuri" &&
                    curriculumLesson.player.rf == 500 &&
                    curriculumLesson.player.blueIC == 0 &&
                    curriculumLesson.dummy.character == "misaki";
                sawCenteredFlickerIc = curriculumLesson.player.hasPos &&
                    curriculumLesson.dummy.hasPos &&
                    curriculumLesson.player.posX == 190.0 &&
                    curriculumLesson.dummy.posX == 450.0 &&
                    (curriculumLesson.player.posX +
                        curriculumLesson.dummy.posX) / 2.0 == 320.0;
            }
            if (curriculumLesson.lessonId == "efz.defense.recoil_guard") {
                bool pattern = false;
                if (curriculumLesson.lesson.tasks.size() == 3) {
                    const auto& task = curriculumLesson.lesson.tasks[2];
                    pattern = task.id == "rg_pattern" && task.sequence.size() == 3 &&
                        task.sequence[0].moveIds == std::vector<int>{168} &&
                        task.sequence[1].moveIds == std::vector<int>{151} &&
                        task.sequence[1].maxGap == 90 &&
                        task.sequence[2].moveIds == std::vector<int>{169} &&
                        task.sequence[2].maxGap == 90 &&
                        task.hasPos && task.posX == 20.0f &&
                        task.hasDummyPos && task.dummyPosX == 80.0f;
                }
                bool dashA = false, dashLowA = false, chain = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    dashA |= episode.ownerTask == "rg_stand" &&
                        episode.action == "66A" && episode.telegraph == "jump";
                    dashLowA |= episode.ownerTask == "rg_low" &&
                        episode.action == "662A" && episode.telegraph == "jump";
                    chain |= episode.ownerTask == "rg_pattern" &&
                        episode.action == "5A>5B>2C" && episode.approach == "none";
                }
                sawCorrectRgDrills = pattern && dashA && dashLowA && chain &&
                    curriculumLesson.player.posX == 290.0 &&
                    curriculumLesson.dummy.posX == 350.0;
            }
            if (curriculumLesson.lessonId == "efz.defense.rg_response") {
                bool challenge = false, blockThenPunish = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "rg_challenge" && task.sequence.size() == 2) {
                        const auto& punish = task.sequence[1];
                        challenge = punish.moveIds == std::vector<int>{200} &&
                            punish.req == "land" && punish.maxGap == 150 &&
                            punish.dummyStateMoveIds == std::vector<int>{231} &&
                            punish.hasContact && punish.contact.source == "direct" &&
                            punish.contact.result == "hit" &&
                            punish.contact.moveIds == std::vector<int>{200};
                    } else if (task.id == "rg_block" && task.sequence.size() == 3) {
                        const auto& guard = task.sequence[1];
                        const auto& punish = task.sequence[2];
                        blockThenPunish =
                            guard.moveIds == std::vector<int>{151, 153} &&
                            guard.req == "move" && guard.maxGap == 150 &&
                            guard.dummyStateMoveIds == std::vector<int>{258} &&
                            punish.moveIds == std::vector<int>{200} &&
                            punish.req == "land" && punish.maxGap == 150 &&
                            punish.dummyStateMoveIds == std::vector<int>{258} &&
                            punish.hasContact && punish.contact.source == "direct" &&
                            punish.contact.result == "hit" &&
                            punish.contact.moveIds == std::vector<int>{200};
                    }
                }
                sawStrictRgResponse = challenge && blockThenPunish;
            }
            if (curriculumLesson.lessonId == "efz.strategy.take_space") {
                bool walkThenGuard = false;
                bool jumpAfterLow = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "walk_guard" && task.sequence.size() == 2) {
                        const auto& walk = task.sequence[0];
                        const auto& guard = task.sequence[1];
                        walkThenGuard = walk.moveIds == std::vector<int>{1} &&
                            walk.req == "move" &&
                            guard.moveIds ==
                                std::vector<int>{150, 151, 152, 153, 154, 155} &&
                            guard.req == "move" && guard.maxGap == 240 &&
                            guard.dummyStateMoveIds == std::vector<int>{203};
                    }
                    if (task.id == "dash_whiff" && task.sequence.size() == 2) {
                        const auto& dash = task.sequence[0];
                        const auto& punish = task.sequence[1];
                        sawDashWhiffPunishRoute =
                            dash.moveIds == std::vector<int>{163} &&
                            dash.req == "move" && dash.whiffWindowMaxTicks == 120 &&
                            punish.req == "land" && punish.maxGap == 120 &&
                            punish.whiffWindowMaxTicks == 120;
                    } else if (task.id == "jump_low") {
                        jumpAfterLow = task.moveIds == std::vector<int>{5} &&
                            task.req == "move" && task.whiffWindowMaxTicks == 100;
                    }
                }
                sawAllTakeSpaceGuardReactions = walkThenGuard && jumpAfterLow;
            }
            if (curriculumLesson.lessonId == "efz.defense.guard_gauge") {
                bool build = false, empty = false, stored = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "build_gauge") {
                        build = task.playerSeed.has && task.playerSeed.guard == 0 &&
                            task.hasGoalState && task.goalState.field == "guard" &&
                            task.goalState.requiresBlock;
                    } else if (task.id == "juggle_empty") {
                        empty = task.moveIds == std::vector<int>{254} &&
                            task.req == "land" && task.endsCombo &&
                            task.dummySeed.guard == 0;
                    } else if (task.id == "compare_juggle") {
                        stored = task.moveIds == std::vector<int>{254} &&
                            task.req == "land" && task.endsCombo &&
                            task.dummySeed.guard == 320 && task.hasCompare &&
                            task.compareMetric == "untechTicks" && task.compareOp == "gt";
                    }
                }
                sawGuardGaugeRecoveryLauncher = build && empty && stored &&
                    curriculumLesson.player.character == "misaki" &&
                    curriculumLesson.dummy.character == "nayukib";
            }
            if (curriculumLesson.lessonId == "efz.strategy.survive_pressure") {
                bool blockAll = false, escape = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "block_all") {
                        blockAll = task.hasAbsence && task.absenceForbid == "attack" &&
                            task.absenceEnd == "episodeCycleEnd" &&
                            task.absenceFailOnHit && task.absenceMinBlocks == 3;
                    } else if (task.id == "escape_gap" && task.sequence.size() == 2) {
                        const auto& guarded = task.sequence[0];
                        const auto& jump = task.sequence[1];
                        escape = guarded.moveIds ==
                                     std::vector<int>{150, 151, 152, 153, 154, 155} &&
                            guarded.req == "move" &&
                            guarded.dummyStateMoveIds == std::vector<int>{200, 201} &&
                            jump.moveIds == std::vector<int>{4, 5, 6} &&
                            jump.req == "move" && jump.duringGap && jump.maxGap == 360;
                    }
                }
                sawPressureInteractionProofs = blockAll && escape;
            }
            if (curriculumLesson.lessonId == "efz.strategy.choose_rg") {
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "no_challenge") {
                        sawChooseRgBlockProof = task.hasAbsence &&
                            task.absenceEnd == "episodeCycleEnd" &&
                            task.absenceFailOnHit && task.absenceMinBlocks == 4;
                    }
                }
            }
            if (curriculumLesson.lessonId == "efz.strategy.attack_into_rg") {
                bool cancelRace = false, counterEpisode = false;
                bool poke = false, answer = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "safe_cancel" && task.sequence.size() == 2) {
                        const auto& c5b = task.sequence[0];
                        const auto& f5b = task.sequence[1];
                        cancelRace = task.script == "ep_rg_dummy" &&
                            c5b.moveIds == std::vector<int>{201} &&
                            c5b.req == "rg" &&
                            f5b.moveIds == std::vector<int>{202} &&
                            f5b.fromMoveIds == std::vector<int>{201} &&
                            f5b.req == "land";
                    }
                    poke |= task.id == "end_turn_poke" &&
                        task.moveIds == std::vector<int>{200} && task.req == "block" &&
                        task.script == "ep_block_50";
                    answer |= task.id == "end_turn" && task.continuity == "continue" &&
                        task.hasAbsence && task.absenceEnd == "episodeCycleEnd" &&
                        task.absenceFailOnHit && task.absenceMinBlocks == 1 &&
                        task.script == "ep_answer_50";
                }
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    counterEpisode |= episode.id == "ep_rg_dummy" &&
                        episode.ownerTask == "safe_cancel" &&
                        episode.kind == "rg_answer" && episode.action == "5A" &&
                        episode.start == "taskArmed";
                }
                sawAttackIntoRgCancelRace = cancelRace && counterEpisode;
                sawEndTurnInteractionProof = poke && answer;
            }
            if (curriculumLesson.lessonId == "efz.strategy.spend_rf") {
                bool redCommit = false, blueCommit = false, defense = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "spend_red" && task.sequence.size() == 3) {
                        const auto& ic = task.sequence[1];
                        redCommit = ic.moveIds == std::vector<int>{167} &&
                            ic.inputMask == 64 && ic.req == "commit";
                    } else if (task.id == "spend_blue" && task.sequence.size() == 5) {
                        const auto& opener = task.sequence[0];
                        const auto& ic = task.sequence[1];
                        blueCommit = ic.moveIds == std::vector<int>{167} &&
                            ic.inputMask == 64 && ic.req == "commit";
                        const auto& first66B = task.sequence[2];
                        const auto& delayed66B = task.sequence[3];
                        const auto& super = task.sequence[4];
                        sawSpendRfBlueParity = task.hasPos && task.posX == 20.0f &&
                            task.hasDummyPos && task.dummyPosX == 160.0f &&
                            task.playerSeed.has && task.playerSeed.rf == 1000 &&
                            task.playerSeed.blueIC == 1 &&
                            task.playerSeed.meter == 2000 &&
                            task.continuity == "sameCombo" &&
                            opener.moveIds == std::vector<int>{232} &&
                            opener.req == "land" &&
                            ic.fromMoveIds == std::vector<int>{232} &&
                            ic.maxGap == 120 &&
                            first66B.moveIds == std::vector<int>{231} &&
                            first66B.maxGap == 240 &&
                            delayed66B.moveIds == std::vector<int>{231} &&
                            delayed66B.minGap == 6 && delayed66B.maxGap == 300 &&
                            super.moveIds == std::vector<int>{304} &&
                            super.fromMoveIds == std::vector<int>{231} &&
                            super.req == "land" &&
                            super.maxGap == 180;
                    } else if (task.id == "defensive" && task.sequence.size() == 3) {
                        const auto& rf = task.sequence[0];
                        const auto& bic = task.sequence[1];
                        const auto& guard = task.sequence[2];
                        defense = rf.moveIds == std::vector<int>{252} &&
                            rf.inputMask == 64 && rf.req == "block" &&
                            bic.moveIds == std::vector<int>{167, 171} &&
                            bic.inputMask == 64 && bic.req == "commit" &&
                            bic.fromMoveIds == std::vector<int>{252} &&
                            guard.moveIds ==
                                std::vector<int>{150, 151, 152, 153, 154, 155,
                                                 156, 16};
                    }
                }
                sawStrategicRfProof = redCommit && blueCommit && defense;
            }
            if (curriculumLesson.lessonId == "efz.strategy.anti_air") {
                sawCorrectNayukiAntiAirs = !curriculumLesson.lesson.tasks.empty();
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.moveIds != std::vector<int>{251, 252} ||
                        task.req != "land" || task.dummyState != "airborne") {
                        sawCorrectNayukiAntiAirs = false;
                    }
                }
            }
            if (curriculumLesson.lessonId == "efz.strategy.hit_confirm") {
                auto validBranch = [](const Mission::LessonTask& task) {
                    if (!task.hasBranch ||
                        task.branchStarterIds != std::vector<int>{201} ||
                        task.branchOnHit.size() != 3 ||
                        task.branchOnBlockHoldTicks != 90) return false;
                    const auto& f5b = task.branchOnHit[0];
                    const auto& c = task.branchOnHit[1];
                    const auto& special = task.branchOnHit[2];
                    return f5b.moveIds == std::vector<int>{202} &&
                        f5b.fromMoveIds == std::vector<int>{201} &&
                        f5b.req == "land" &&
                        c.moveIds == std::vector<int>{203} &&
                        c.fromMoveIds == std::vector<int>{202} &&
                        c.req == "land" &&
                        special.moveIds == std::vector<int>{253} &&
                        special.fromMoveIds == std::vector<int>{203} &&
                        special.req == "land";
                };
                bool hitTask = false, blockTask = false;
                int mixedTasks = 0;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (!validBranch(task)) continue;
                    if (task.id == "learn_hit" && task.script == "ep_learn_hit") {
                        hitTask = true;
                    } else if (task.id == "learn_block" &&
                               task.script == "ep_learn_block") {
                        blockTask = true;
                    } else if ((task.id == "mixed_1" || task.id == "mixed_2") &&
                               task.script == "ep_" + task.id) {
                        ++mixedTasks;
                    }
                }
                bool hitEpisode = false, blockEpisode = false;
                int mixedEpisodes = 0;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    hitEpisode |= episode.id == "ep_learn_hit" &&
                        episode.ownerTask == "learn_hit" && episode.kind == "idle" &&
                        episode.variants.empty();
                    blockEpisode |= episode.id == "ep_learn_block" &&
                        episode.ownerTask == "learn_block" && episode.kind == "block" &&
                        episode.variants.empty();
                    if ((episode.id == "ep_mixed_1" || episode.id == "ep_mixed_2") &&
                        episode.variants == std::vector<std::string>{"idle", "block"} &&
                        episode.variantGroup == "mixed_confirm") {
                        ++mixedEpisodes;
                    }
                }
                sawGuidedHitConfirm = curriculumLesson.lesson.tasks.size() == 4 &&
                    hitTask && blockTask && mixedTasks == 2 && hitEpisode &&
                    blockEpisode && mixedEpisodes == 2;
            }
            if (curriculumLesson.lessonId == "efz.systems.guard_attacks") {
                bool taskGrades66B = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "use_gp" && task.sequence.size() == 3) {
                        const auto& start = task.sequence[0];
                        const auto& absorb = task.sequence[1];
                        const auto& hit = task.sequence[2];
                        taskGrades66B =
                            start.moveIds == std::vector<int>{211} &&
                            start.req == "move" &&
                            absorb.moveIds.empty() && absorb.hasContact &&
                            absorb.contact.attacker == "dummy" &&
                            absorb.contact.target == "learner" &&
                            absorb.contact.source == "direct" &&
                            absorb.contact.result == "guard_point" &&
                            absorb.contact.moveIds == std::vector<int>{231} &&
                            hit.moveIds.empty() && hit.hasContact &&
                            hit.dummyStateMoveIds == std::vector<int>{231} &&
                            hit.contact.attacker == "learner" &&
                            hit.contact.target == "dummy" &&
                            hit.contact.source == "direct" &&
                            hit.contact.result == "hit" &&
                            hit.contact.moveIds == std::vector<int>{211};
                    }
                }
                bool trueDashNormal = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    if (episode.id == "ep_cued_strike") {
                        trueDashNormal = episode.kind == "macro" &&
                            episode.action == "66B" &&
                            episode.approach == "none" &&
                            episode.telegraph == "jump";
                    }
                }
                const bool declaresDirectContactState =
                    std::find(curriculumLesson.requires.begin(),
                              curriculumLesson.requires.end(),
                              "direct_contact_state") !=
                        curriculumLesson.requires.end();
                const bool declaresGuardPointContact =
                    std::find(curriculumLesson.requires.begin(),
                              curriculumLesson.requires.end(),
                              "guard_point_contact") !=
                        curriculumLesson.requires.end();
                sawTrueGuardAttackDashNormal =
                    taskGrades66B && trueDashNormal &&
                    declaresDirectContactState && declaresGuardPointContact;
            }
            if (curriculumLesson.lessonId == "efz.strategy.open_guard") {
                bool low = false, high = false, delayedThrow = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "open_low" && task.sequence.size() == 3) {
                        low = task.sequence[0].moveIds == std::vector<int>{201} &&
                            task.sequence[0].req == "block" &&
                            task.sequence[1].moveIds == std::vector<int>{202} &&
                            task.sequence[1].fromMoveIds == std::vector<int>{201} &&
                            task.sequence[1].req == "block" &&
                            task.sequence[2].moveIds == std::vector<int>{206} &&
                            task.sequence[2].fromMoveIds == std::vector<int>{202} &&
                            task.sequence[2].req == "land" &&
                            task.script == "ep_guard_stand";
                    } else if (task.id == "open_high" && task.sequence.size() == 3) {
                        high = task.sequence[0].moveIds == std::vector<int>{201} &&
                            task.sequence[0].req == "block" &&
                            task.sequence[1].moveIds == std::vector<int>{202} &&
                            task.sequence[1].fromMoveIds == std::vector<int>{201} &&
                            task.sequence[1].req == "block" &&
                            task.sequence[2].moveIds == std::vector<int>{255} &&
                            task.sequence[2].fromMoveIds == std::vector<int>{202} &&
                            task.sequence[2].req == "land" &&
                            task.script == "ep_guard_crouch";
                    } else if (task.id == "open_throw" && task.sequence.size() == 2) {
                        const auto& throwAction = task.sequence[1];
                        delayedThrow = task.sequence[0].moveIds ==
                                std::vector<int>{200, 230} &&
                            task.sequence[0].req == "block" &&
                            throwAction.moveIds == std::vector<int>{221} &&
                            throwAction.req == "move" && throwAction.minGap == 30 &&
                            throwAction.maxGap == 720 && task.script == "ep_guard_throw";
                    }
                }
                bool stand = false, crouch = false, adaptive = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    stand |= episode.id == "ep_guard_stand" &&
                        episode.ownerTask == "open_low" &&
                        episode.kind == "guard" && episode.clip == "stand" &&
                        episode.start == "taskArmed";
                    crouch |= episode.id == "ep_guard_crouch" &&
                        episode.ownerTask == "open_high" &&
                        episode.kind == "guard" && episode.clip == "crouch" &&
                        episode.start == "taskArmed";
                    adaptive |= episode.id == "ep_guard_throw" &&
                        episode.ownerTask == "open_throw" &&
                        episode.kind == "block" &&
                        episode.start == "taskArmed";
                }
                sawStableGuardOpeners = low && high && delayedThrow &&
                    stand && crouch && adaptive;
            }
            if (curriculumLesson.lessonId == "efz.strategy.wakeup_offense") {
                sawMisaki214CResources = curriculumLesson.player.rf == 1000 &&
                    curriculumLesson.dummy.rf == 1000;
                const auto isKnockdownRoute = [](const Mission::LessonTask& task) {
                    if (task.sequence.size() < 4) return false;
                    const auto& a = task.sequence[0];
                    const auto& b = task.sequence[1];
                    const auto& c = task.sequence[2];
                    const auto& d = task.sequence[3];
                    return a.moveIds == std::vector<int>{200} && a.req == "land" &&
                        b.moveIds == std::vector<int>{201} &&
                        b.fromMoveIds == std::vector<int>{200} && b.req == "land" &&
                        c.moveIds == std::vector<int>{203} &&
                        c.fromMoveIds == std::vector<int>{201} && c.req == "land" &&
                        d.moveIds == std::vector<int>{253} &&
                        d.fromMoveIds == std::vector<int>{203} && d.req == "land";
                };
                bool meatySetup = false;
                bool throwSetup = false;
                bool baitSetup = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "meaty" && task.sequence.size() == 5) {
                        const auto& meaty = task.sequence[4];
                        meatySetup = isKnockdownRoute(task) &&
                            meaty.moveIds == std::vector<int>{200, 204} &&
                            meaty.req == "hits" && meaty.afterWakeMaxTicks == 5;
                    } else if (task.id == "throw_setup") {
                        throwSetup = task.sequence.size() == 4 && isKnockdownRoute(task);
                    } else if (task.id == "wake_throw") {
                        sawOrderedWakeupThrow = task.sequence.empty() &&
                            task.moveIds == std::vector<int>{221} &&
                            task.req == "move" && task.afterWakeMaxTicks == 192 &&
                            task.continuity == "continue" && task.script == "ep_block";
                    } else if (task.id == "bait_setup") {
                        baitSetup = task.sequence.size() == 4 && isKnockdownRoute(task);
                    } else if (task.id == "bait" && task.continuity == "continue" &&
                        task.hasAbsence && task.absenceStart == "taskArmed" &&
                        task.absenceEnd == "dummyAttackEnd") {
                        sawWakeupBaitCarry = true;
                    }
                }
                sawWakeupKnockdownRoutes = meatySetup && throwSetup && baitSetup;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    if (episode.id == "ep_reversal" && episode.ownerTask == "bait" &&
                        episode.kind == "macro" && episode.action == "214C" &&
                        episode.expectedMoveIds == std::vector<int>{252} &&
                        episode.start == "trigger" && episode.trigger == "onWakeup" &&
                        episode.repeatMode == "once") {
                        sawMisaki214CReversal = true;
                    }
                }
            }
            if (curriculumLesson.lessonId == "efz.offense.juggle_gauge") {
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id == "time_out" && task.continuity == "continue" &&
                        task.hasAbsence && task.absenceForbid == "attack" &&
                        task.absenceStart == "dummyUntech" &&
                        task.absenceEnd == "dummyUntechEmpty") {
                        sawLiveJuggleContinuation = true;
                    }
                }
            }
            if (curriculumLesson.lessonId == "efz.systems.wakeup_vulnerability") {
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "wakeup_hit" || task.sequence.size() != 2) continue;
                    const auto& meaty = task.sequence[1];
                    sawStrictWakeupJb = meaty.moveIds == std::vector<int>{208} &&
                        meaty.req == "hits" && meaty.afterWakeMaxTicks == 5;
                }
            }
            if (curriculumLesson.lessonId == "efz.systems.character_counters") {
                bool route = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "counter_escape" || task.sequence.size() != 3) continue;
                    const auto& trigger = task.sequence[0];
                    const auto& redIc = task.sequence[1];
                    const auto& guard = task.sequence[2];
                    route = trigger.moveIds == std::vector<int>{201} &&
                        trigger.req == "move" &&
                        trigger.dummyStateMoveIds == std::vector<int>{261} &&
                        redIc.moveIds == std::vector<int>{167} &&
                        redIc.fromMoveIds == std::vector<int>{201} &&
                        redIc.inputMask == 64 && redIc.req == "commit" &&
                        guard.moveIds == std::vector<int>{151, 153} &&
                        guard.req == "move" &&
                        task.hasPos && task.posX == 330.0f &&
                        task.playerSeed.rf == 500 && task.playerSeed.blueIC == 0;
                }
                bool counterEpisode = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    counterEpisode |= episode.id == "ep_counter_stance" &&
                        episode.ownerTask == "counter_escape" &&
                        episode.kind == "macro" && episode.action == "22A" &&
                        episode.approach == "none" && episode.start == "taskArmed";
                }
                const auto declares = [&](const char* capability) {
                    return std::find(curriculumLesson.requires.begin(),
                                     curriculumLesson.requires.end(), capability) !=
                           curriculumLesson.requires.end();
                };
                sawCharacterCounterFiveB = route && counterEpisode &&
                    declares("input_edges") && declares("input_commit_mapping") &&
                    curriculumLesson.player.character == "shiori" &&
                    curriculumLesson.dummy.character == "misaki";
            }
            if (curriculumLesson.lessonId == "efz.systems.special_knockdown") {
                const auto hasCapability = [&](const char* capability) {
                    return std::find(curriculumLesson.requires.begin(),
                                     curriculumLesson.requires.end(), capability) !=
                           curriculumLesson.requires.end();
                };
                bool routeIsExact = false;
                for (const auto& task : curriculumLesson.lesson.tasks) {
                    if (task.id != "otg_route" || task.sequence.size() != 4) continue;
                    const auto& throwStartup = task.sequence[0];
                    const auto& throwSuccess = task.sequence[1];
                    const auto& redIc = task.sequence[2];
                    const auto& pickup = task.sequence[3];
                    routeIsExact = task.continuity == "sameCombo" &&
                        throwStartup.moveIds == std::vector<int>{220} &&
                        throwStartup.req == "move" &&
                        throwSuccess.moveIds == std::vector<int>{221} &&
                        throwSuccess.fromMoveIds == std::vector<int>{220} &&
                        throwSuccess.req == "move" &&
                        redIc.moveIds == std::vector<int>{167} &&
                        redIc.fromMoveIds == std::vector<int>{221} &&
                        redIc.inputMask == 64 && redIc.req == "commit" &&
                        pickup.moveIds == std::vector<int>{206} &&
    // IC 167 is a completed system action: Mai passes through neutral (167 -> 8 -> 206), so a
    // direct fromMoveIds proof on the pickup can never match.
                        pickup.fromMoveIds.empty() &&
                        pickup.req == "land" && pickup.hasContact &&
                        pickup.contact.source == "direct" &&
                        pickup.contact.result == "hit" &&
                        pickup.contact.targetStateBefore == "downed" &&
                        pickup.contact.moveIds == std::vector<int>{206} &&
                        pickup.dummyState.empty() && pickup.dummyStateMoveIds.empty();
                }
                bool ownsIdle = false;
                for (const auto& episode : curriculumLesson.lesson.episodes) {
                    ownsIdle |= episode.id == "ep_otg_idle" &&
                        episode.ownerTask == "otg_route" && episode.kind == "idle";
                }
                sawAuthoredMaiOtgRoute = routeIsExact && ownsIdle &&
                    curriculumLesson.player.character == "mai" &&
                    curriculumLesson.player.rf == 500 &&
                    curriculumLesson.player.blueIC == 0 &&
                    curriculumLesson.player.posX == 310.0 &&
                    curriculumLesson.dummy.character == "misaki" &&
                    curriculumLesson.dummy.posX == 330.0 &&
                    !hasCapability("combo_lifecycle") &&
                    hasCapability("otg_contact");
            }
            const auto runtimeIssues =
                Mission::Tutorial::RuntimeSupportIssues(curriculumLesson);
            bool launchable = runtimeIssues.empty();
            if (std::find(curriculumLesson.requires.begin(), curriculumLesson.requires.end(),
                          "dummy_script") != curriculumLesson.requires.end() &&
                curriculumLesson.lesson.episodes.empty()) {
                ++missingEpisodeCount;
            }
            for (const auto& task : curriculumLesson.lesson.tasks) {
                for (int id : task.moveIds) if (id <= 0) launchable = false;
                if (task.hitsRequired > 1 && task.req != "hits") launchable = false;
            }
            if (!launchable) {
                std::cerr << "PREFLIGHT UNAVAILABLE " << curriculumLesson.lessonId;
                for (const auto& issue : runtimeIssues) {
                    std::cerr << "\n  - " << issue;
                }
                std::cerr << '\n';
            }
            if (launchable) ++launchableCount;
            ++lessonCount;
        }
        Check(lessonCount == 53, "all 53 shipped Core tutorial lessons load");
        Check(shippedTaskCount == 147,
              "the audited Core tutorial contains exactly 147 tasks");
        Check(shippedEpisodeCount == 85,
              "the audited Core tutorial contains exactly 85 dummy episodes");
        Check(sourcedLessonCount == 53,
              "all 53 shipped Core tutorial lessons cite an auditable source");
        Check(sawInputEdgeLesson, "Core tutorial teaches an animation-less button through input edges");
        Check(sawOrderedLesson, "Core tutorial contains a real ordered action task");
        Check(sawCorrectNayukiCancelIds,
              "Nayuki cancel routes use the verified 236A and 623A move ids");
        Check(sawJumpingMisakiAirString,
              "the air jump-cancel task owns a looping Misaki jump episode");
        Check(sawCorrectAirRecoveryDrills,
              "Air Recovery uses Misaki 236A and grades 157 toward / 158 away separately");
        Check(sawCorrectAirRecoveryDecisionDrills,
              "Recovery Decisions uses 236A nearby, far-reaching 66C at range, and grades away as 158");
        Check(sawCorrectAirRecoveryPunish,
              "Recovery Punish uses Misaki 236A to launch Nayuki, then grades air throw 249");
        Check(sawCorrectNayukiRfIds,
              "Nayuki RF lesson uses the verified 623C and fallback 623B move ids");
        Check(sawCorrectBicSafetyContract,
              "BIC safety makes the learner BIC blocked 623C and guard the answer");
        Check(sawCorrectRedIcContract,
              "Red IC uses the universal 22C input and an adjacent 5C source");
        Check(sawCorrectBicRetryContract,
              "Blue IC uses the universal 22C input and restores a cornered RF/SP baseline on failure");
        Check(sawStrictFlickerIc,
              "FIC uses Sayuri 236A, the C-edge 22C cancel, live projectile, and safe spacing");
        Check(sawCenteredFlickerIc,
              "FIC keeps its exact 300-unit proof centered inside the visible arena");
        Check(sawCorrectRgDrills,
              "Recoil Guard uses 66A, 662A, then a cornered 5A-5B-2C pattern");
        Check(sawStrictRgResponse,
              "RG Response requires ordinary block and binds each punish to the current recovery");
        Check(sawAllTakeSpaceGuardReactions,
              "Take Space proves walk-then-block and a forward jump after the low whiffs");
        Check(sawCorrectNayukiAntiAirs,
              "Nayuki anti-air tasks accept only 623B/C, never 623A or normals");
        Check(sawOrderedWakeupThrow,
              "wakeup offense carries its authored knockdown into the delayed wakeup throw");
        Check(sawWakeupKnockdownRoutes,
              "every wakeup-offense drill grades 5A-c.5B-5C-236A as its knockdown setup");
        Check(sawWakeupBaitCarry,
              "wakeup reversal bait carries the knockdown and waits for a real dummy attack end");
        Check(sawMisaki214CReversal,
              "wakeup reversal bait injects Misaki 214C on wakeup, not 623C");
        Check(sawMisaki214CResources,
              "wakeup reversal bait gives both fighters full RF so Misaki 214C is driveable");
        Check(sawLiveJuggleContinuation,
              "Juggle Gauge observation continues from the live launched untech state");
        Check(sawStrictWakeupJb,
              "Wakeup Vulnerability requires j.B inside the sub-two-frame window");
        Check(sawCharacterCounterFiveB,
              "Character Counters requires Shiori 5B into C-committed Red IC, then guard");
        Check(sawPowerStatsPlacement,
              "Power Scaling keeps its requirements below the native combo/Power readout");
        Check(sawPowerSingleComboRoute,
              "Power Scaling keeps its three-hit observation inside one combo");
        Check(sawDirectDashMomentumJump,
              "Ground Dash momentum drill must jump directly from the dash state");
        Check(sawDirectArmorCancelEscape,
              "Recoil Armor cancel escape requires a direct 5A-to-neutral-jump transition");
        Check(sawBufferedArmorCommandThrow,
              "Recoil Armor drives Rumi's buffered 41236C in all three tasks, grades startup 264, and does not gate it on RF");
        Check(sawGuardGaugeRecoveryLauncher,
              "Guard Gauge starts empty and compares Misaki 236A recovery launches");
        Check(sawDashWhiffPunishRoute,
              "Take Space requires both the whiff-window dash and its landed punish");
        Check(sawPressureInteractionProofs,
              "Survive Pressure proves the guarded string and a guarded opener before gap escape");
        Check(sawChooseRgBlockProof,
              "Choose RG ends its no-challenge drill on a real guarded episode");
        Check(sawAttackIntoRgCancelRace,
              "Attack into RG makes Misaki answer c.5B with 5A and grades the direct f.5B cancel hit");
        Check(sawEndTurnInteractionProof,
              "Attack into RG separates the blocked poke from the no-attack guarded answer");
        Check(sawStrategicRfProof,
              "Spend RF proves C-committed IC and blocked defensive BIC safety");
        Check(sawSpendRfBlueParity,
              "Spend RF task 2 reproduces the standalone Blue IC setup and route contract");
        Check(sawAuthoredMaiOtgRoute,
              "Special Knockdown authors Mai throw-Red-IC-2C with direct OTG proof and no unused combo-end contract");
        Check(sawRfBlockCompletion,
              "RF blocking requires a real blocked hit plus Red, with timing comparison informational only");
        Check(sawGuidedHitConfirm,
              "Hit Confirm teaches guaranteed hit, guaranteed block, then two hidden decisions with one graded route");
        Check(sawStableGuardOpeners,
              "Open Guard uses stable c.5B-f.5B low/high strings and an auto-block walk-up throw");
        Check(sawTrueGuardAttackDashNormal,
              "Guard Attacks proves Misaki 66B was guard-pointed before Ayu 6C lands");
        Check(placeholderTaskCount == 4,
              "curriculum backlog has the audited 4 scenario placeholders");
        Check(missingEpisodeCount == 1,
              "curriculum backlog has the audited 1 missing dummy episode set");
        Check(launchableCount == 51,
              "capability/content preflight exposes exactly 51 honest lessons");
    }
    std::cout << "mission_data_tests passed\n";
    return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
