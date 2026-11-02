#include "game/mission/mission_authoring.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// Stub: tutorial_support.cpp is linked for its policy code, not the injected DLL logger.
void LogOut(const std::string&, bool) {}

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

std::string ReadAll(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

bool WriteAll(const fs::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    stream.flush();
    return stream.good();
}

std::set<std::string> DirectoryFiles(const fs::path& path) {
    std::set<std::string> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (entry.is_regular_file()) files.insert(entry.path().filename().string());
    }
    return files;
}

struct TempTree {
    fs::path root;

    TempTree() {
        root = fs::temp_directory_path() /
               ("efz-mission-authoring-" + std::to_string(GetCurrentProcessId()) +
                "-" + std::to_string(GetTickCount()));
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
    }

    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void TestSlugsAndComponents() {
    using namespace Mission::Authoring;
    Check(MakeSlug("  My Great Pack!  ") == "my-great-pack",
          "slug normalizes punctuation and whitespace");
    Check(MakeSlug("A___B") == "a-b", "slug collapses separators");
    Check(MakeSlug("CON") == "con-item", "slug disambiguates reserved device name");
    Check(MakeSlug("\xE6\x97\xA5\xE6\x9C\xAC") == "untitled",
          "non-ASCII-only text has a deterministic fallback");

    std::string error;
    Check(ValidateSafeComponent("corner-route_2.json", error),
          "ordinary generated filename is safe");
    Check(!ValidateSafeComponent("../route.json", error), "traversal is rejected");
    Check(!ValidateSafeComponent("sub/route.json", error), "path separators are rejected");
    Check(!ValidateSafeComponent("CON.json", error), "reserved filename stem is rejected");
    Check(!ValidateSafeComponent("route. ", error), "trailing space is rejected");
    Check(!ValidateSafeComponent("\xC3\xA9.json", error), "non-ASCII filename is rejected");
}

void TestPackAndMissionLifecycle() {
    using namespace Mission::Authoring;

    TempTree tree;
    const fs::path missionsRoot = tree.root / "missions";
    std::string error;

    CreatePackResult first;
    Check(CreatePack(missionsRoot.string(), "My Pack", "Aquatic",
                     "Locally authored trials", "1.2.3", first, error),
          "create first pack: " + error);
    Check(first.folderName == "my-pack", "first pack receives base folder slug");
    Check(first.packId == "local.my-pack", "first pack receives stable namespaced id");
    Check(first.defaultCategoryId == "general", "created pack exposes default category id");
    Check(fs::path(first.packJsonPath).parent_path().parent_path() == missionsRoot,
          "created pack is an immediate child of the missions root");

    ::Mission::Pack loadedPack;
    Check(::Mission::LoadPack(first.packJsonPath, loadedPack, error), "load created pack: " + error);
    Check(loadedPack.name == "My Pack" && loadedPack.author == "Aquatic" &&
          loadedPack.version == "1.2.3" && loadedPack.editable,
          "created pack metadata round-trips");
    Check(loadedPack.categories.size() == 1 &&
          loadedPack.categories[0].id == "general" &&
          loadedPack.categories[0].order == 1,
          "created pack starts with ordered General category");

    CreatePackResult second;
    Check(CreatePack(missionsRoot.string(), "My Pack", "", "", "", second, error),
          "create same-named pack with unique destination: " + error);
    Check(second.folderName == "my-pack-2" && second.packId == "local.my-pack-2",
          "same title receives deterministic unique folder and id");

    const fs::path outsideRoot = tree.root / "outside-missions";
    CreatePackResult outside;
    Check(CreatePack(outsideRoot.string(), "Outside Pack", "", "", "1.0",
                     outside, error),
          "create editable pack outside the selected library root: " + error);
    ::Mission::PackCategory containmentCategory;
    Check(!AddCategory(missionsRoot.string(), outside.packJsonPath,
                       "Must Not Mutate", "", containmentCategory, error) &&
          error.find("immediate child") != std::string::npos,
          "category mutation rejects an editable pack outside missions root");

    std::vector<PackSummary> summaries;
    Check(EnumeratePackSummaries(missionsRoot.string(), summaries, error),
          "enumerate pack summaries: " + error);
    Check(summaries.size() == 2, "enumeration finds both immediate-child packs");
    auto firstSummary = std::find_if(summaries.begin(), summaries.end(),
        [&](const PackSummary& summary) { return summary.id == first.packId; });
    Check(firstSummary != summaries.end() && firstSummary->version == "1.2.3" &&
          firstSummary->editable && firstSummary->missionCount == 0,
          "summary carries authoring metadata and mission count");

    const fs::path brokenFolder = missionsRoot / "broken-third-party";
    fs::create_directories(brokenFolder);
    {
        std::ofstream malformed(brokenFolder / "pack.json", std::ios::binary);
        malformed << "{ this is not valid json";
    }
    summaries.clear();
    error.clear();
    Check(EnumeratePackSummaries(missionsRoot.string(), summaries, error),
          "an unreadable third-party pack does not disable destination discovery");
    Check(summaries.size() == 2 && error.find("skipped unreadable pack") != std::string::npos,
          "pack enumeration returns valid packs and a useful warning");
    CreatePackResult resilient;
    Check(CreatePack(missionsRoot.string(), "Still Works", "", "Line one\nLine two",
                     "1.0", resilient, error),
          "local pack creation skips an unrelated malformed manifest: " + error);
    CreatePackResult invalidTextPack;
    Check(!CreatePack(missionsRoot.string(), "Bad\nPack", "", "", "1.0",
                      invalidTextPack, error),
          "pack names reject embedded newlines");
    Check(!CreatePack(missionsRoot.string(), "Bad Version", "", "", "1.0\n2.0",
                      invalidTextPack, error),
          "pack versions reject embedded newlines");

    ::Mission::PackCategory offense;
    Check(AddCategory(missionsRoot.string(), first.packJsonPath, "Offense", "Pressure and combo routes",
                      offense, error),
          "add category: " + error);
    Check(offense.id == "offense" && offense.order == 2,
          "category receives stable id and next order");
    ::Mission::PackCategory ignoredCategory;
    Check(!AddCategory(missionsRoot.string(), first.packJsonPath, "Bad\tCategory", "",
                       ignoredCategory, error),
          "category names reject control whitespace");

    const std::string beforeDuplicateCategory = ReadAll(first.packJsonPath);
    Check(!AddCategory(missionsRoot.string(), first.packJsonPath, "OFFENSE", "duplicate", ignoredCategory, error),
          "case-insensitive duplicate category label is rejected");
    Check(ReadAll(first.packJsonPath) == beforeDuplicateCategory,
          "duplicate category failure does not mutate pack");

    ::Mission::PackCategory slugCollision;
    Check(AddCategory(missionsRoot.string(), first.packJsonPath, "General!", "Different display label",
                      slugCollision, error),
          "different category label with same base slug is accepted: " + error);
    Check(slugCollision.id == "general-2",
          "category slug collision receives deterministic suffix");

    ::Mission::Mission recording;
    recording.name = "Recorder placeholder";
    recording.description = "placeholder";
    recording.type = "combo";
    recording.player.character = "nanase";
    recording.dummy.character = "akane";
    ::Mission::Step step;
    step.notation = "5A";
    step.moveIds = {200};
    recording.steps.push_back(step);

    MissionMetadata metadata;
    metadata.name = "Corner Route";
    metadata.description = "Carry into the corner.";
    metadata.type = "trial";
    metadata.difficulty = 3;

    const fs::path nestedFolder = fs::path(first.packJsonPath).parent_path() / "routes";
    fs::create_directories(nestedFolder);
    const fs::path nestedMission = nestedFolder / "nested-route.json";
    Check(::Mission::SaveMission(nestedMission.string(), recording, error),
          "write nested third-party scenario fixture: " + error);
    std::string resolvedScenario;
    Check(ResolveScenarioMissionPath(fs::path(first.packJsonPath).parent_path().string(),
                                     "routes\\nested-route.json",
                                     resolvedScenario, error) &&
          fs::equivalent(fs::path(resolvedScenario), nestedMission),
          "safe nested scenario paths resolve inside the pack");
    Check(!ResolveScenarioMissionPath(fs::path(first.packJsonPath).parent_path().string(),
                                      "..\\outside.json", resolvedScenario, error),
          "scenario traversal is rejected");
    Check(!ResolveScenarioMissionPath(fs::path(first.packJsonPath).parent_path().string(),
                                      nestedMission.string(), resolvedScenario, error),
          "absolute scenario paths are rejected");
    Check(!ResolveScenarioMissionPath(fs::path(first.packJsonPath).parent_path().string(),
                                      "routes\\missing.json", resolvedScenario, error),
          "missing scenario files are rejected during browser resolution");

    MissionMetadata invalidSingleLine = metadata;
    invalidSingleLine.name = "Bad\nMission";
    PublishResult invalidSingleLineResult;
    Check(!PublishMission(missionsRoot.string(), first.packJsonPath, offense.id,
                          recording, invalidSingleLine, invalidSingleLineResult, error),
          "mission names reject embedded newlines");

    PublishResult containmentPublish;
    Check(!PublishMission(missionsRoot.string(), outside.packJsonPath,
                          outside.defaultCategoryId, recording, metadata,
                          containmentPublish, error) &&
          error.find("immediate child") != std::string::npos,
          "mission publication rejects an editable pack outside missions root");

    PublishResult published;
    const fs::path firstFolder = fs::path(first.packJsonPath).parent_path();
    const fs::path stagedTrace = firstFolder / ".recording-trace.tmp";
    const std::string traceEvidence =
        "{\"record\":\"metadata\",\"eventCount\":1}\n";
    Check(WriteAll(stagedTrace, traceEvidence),
          "stage raw entity trace for transactional publication");
    Check(PublishMission(missionsRoot.string(), first.packJsonPath, offense.id, recording, metadata,
                         published, error, stagedTrace.string()),
          "publish mission: " + error);
    Check(published.fileName == "corner-route.json" &&
          published.missionId == first.packId + ".corner-route" &&
          published.order == 1 && fs::is_regular_file(published.missionPath),
          "published mission receives stable path, id and order");
    Check(!published.entityTracePath.empty() &&
          fs::is_regular_file(published.entityTracePath) &&
          ReadAll(published.entityTracePath) == traceEvidence &&
          !fs::exists(stagedTrace),
          "publication atomically promotes the staged entity trace beside the mission");

    ::Mission::Mission loadedMission;
    Check(::Mission::LoadMission(published.missionPath, loadedMission, error),
          "load published mission: " + error);
    Check(loadedMission.id == published.missionId &&
          loadedMission.name == metadata.name &&
          loadedMission.description == metadata.description &&
          loadedMission.type == "trial" && loadedMission.difficulty == 3 &&
          loadedMission.category == offense.id && loadedMission.order == 1,
          "published mission metadata replaces recorder placeholders");
    Check(loadedMission.steps.size() == 1 && loadedMission.steps[0].moveIds ==
          std::vector<int>{200}, "published mission preserves recorded content");

    ::Mission::Pack packAfterPublish;
    Check(::Mission::LoadPack(first.packJsonPath, packAfterPublish, error),
          "load manifest after publish: " + error);
    Check(packAfterPublish.scenarios.size() == 1 &&
          packAfterPublish.scenarios[0].id == published.missionId &&
          packAfterPublish.scenarios[0].file == published.fileName &&
          packAfterPublish.scenarios[0].category == offense.id &&
          packAfterPublish.scenarios[0].difficulty == 3,
          "publish appends matching Scenario metadata");

    PackMetadata packEdit;
    packEdit.name = "My Renamed Pack";
    packEdit.author = "Aquatic + Team";
    packEdit.description = "Updated local collection description.";
    packEdit.version = "2.0";
    Check(UpdatePackMetadata(missionsRoot.string(), first.packJsonPath,
                             packEdit, error),
          "edit pack display metadata: " + error);
    Check(::Mission::LoadPack(first.packJsonPath, packAfterPublish, error) &&
          packAfterPublish.id == first.packId &&
          packAfterPublish.name == packEdit.name &&
          packAfterPublish.version == packEdit.version,
          "pack edit retains stable id while updating display metadata");

    CategoryMetadata categoryEdit;
    categoryEdit.label = "Pressure";
    categoryEdit.description = "Pressure, confirms, and corner routes.";
    Check(UpdateCategoryMetadata(missionsRoot.string(), first.packJsonPath,
                                 offense.id, categoryEdit, error),
          "edit category display metadata: " + error);
    Check(::Mission::LoadPack(first.packJsonPath, packAfterPublish, error) &&
          packAfterPublish.categories[1].id == offense.id &&
          packAfterPublish.categories[1].label == categoryEdit.label &&
          packAfterPublish.categories[1].order == offense.order,
          "category edit retains stable id and order");

    ExistingMissionMetadata missionEdit;
    missionEdit.name = "Corner Route Revised";
    missionEdit.description = "Revised player-facing objective.";
    missionEdit.type = "combo";
    missionEdit.difficulty = 4;
    missionEdit.categoryId = slugCollision.id;
    Check(UpdateMissionMetadata(missionsRoot.string(), first.packJsonPath,
                                published.missionId, missionEdit, error),
          "edit published mission metadata: " + error);
    Check(::Mission::LoadMission(published.missionPath, loadedMission, error) &&
          loadedMission.id == published.missionId &&
          loadedMission.name == missionEdit.name &&
          loadedMission.category == slugCollision.id &&
          loadedMission.difficulty == 4 && loadedMission.order == published.order &&
          loadedMission.steps.size() == 1,
          "mission edit preserves identity, order, and recorded content");
    Check(::Mission::LoadPack(first.packJsonPath, packAfterPublish, error) &&
          packAfterPublish.scenarios.front().id == published.missionId &&
          packAfterPublish.scenarios.front().file == published.fileName &&
          packAfterPublish.scenarios.front().name == missionEdit.name &&
          packAfterPublish.scenarios.front().category == slugCollision.id,
          "mission edit commits matching manifest metadata without changing path");

    const std::string missionBeforeRejectedEdit = ReadAll(published.missionPath);
    const std::string packBeforeRejectedEdit = ReadAll(first.packJsonPath);
    ExistingMissionMetadata rejectedMissionEdit = missionEdit;
    rejectedMissionEdit.categoryId = "missing-category";
    Check(!UpdateMissionMetadata(missionsRoot.string(), first.packJsonPath,
                                 published.missionId, rejectedMissionEdit, error),
          "mission edit rejects a missing destination category");
    Check(ReadAll(published.missionPath) == missionBeforeRejectedEdit &&
          ReadAll(first.packJsonPath) == packBeforeRejectedEdit,
          "rejected mission edit leaves both mirrors unchanged");

    HANDLE editManifestLease = CreateFileA(first.packJsonPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(editManifestLease != INVALID_HANDLE_VALUE,
          "open pack replacement denial lease for metadata edit");
    ExistingMissionMetadata rollbackEdit = missionEdit;
    rollbackEdit.name = "Must Not Partially Commit";
    Check(!UpdateMissionMetadata(missionsRoot.string(), first.packJsonPath,
                                 published.missionId, rollbackEdit, error),
          "manifest replacement failure rejects mission metadata edit");
    if (editManifestLease != INVALID_HANDLE_VALUE) CloseHandle(editManifestLease);
    Check(ReadAll(published.missionPath) == missionBeforeRejectedEdit &&
          ReadAll(first.packJsonPath) == packBeforeRejectedEdit,
          "failed mission metadata transaction rolls back mission and manifest");

    PublishResult repeated;
    Check(PublishMission(missionsRoot.string(), first.packJsonPath, offense.id, recording, metadata,
                         repeated, error),
          "publish duplicate display name with unique identity: " + error);
    Check(repeated.fileName == "corner-route-2.json" &&
          repeated.missionId == first.packId + ".corner-route-2" &&
          repeated.order == 2,
          "duplicate display name receives stable filename/id suffix");

    const std::string packBeforeInvalid = ReadAll(first.packJsonPath);
    const std::set<std::string> filesBeforeInvalid =
        DirectoryFiles(fs::path(first.packJsonPath).parent_path());
    MissionMetadata invalid = metadata;
    invalid.difficulty = 0;
    PublishResult ignoredPublish;
    Check(!PublishMission(missionsRoot.string(), first.packJsonPath, offense.id, recording, invalid,
                          ignoredPublish, error),
          "difficulty below 1 is rejected");
    invalid.difficulty = 6;
    Check(!PublishMission(missionsRoot.string(), first.packJsonPath, offense.id, recording, invalid,
                          ignoredPublish, error),
          "difficulty above 5 is rejected");
    Check(!PublishMission(missionsRoot.string(), first.packJsonPath, "missing-category", recording, metadata,
                          ignoredPublish, error),
          "missing destination category is rejected");
    Check(ReadAll(first.packJsonPath) == packBeforeInvalid &&
          DirectoryFiles(fs::path(first.packJsonPath).parent_path()) == filesBeforeInvalid,
          "validation failures leave manifest and directory untouched");

    // Deny delete/replace sharing while permitting LoadPack to read: the commit fails after the
    // staged mission is promoted, so the rollback path runs rather than an early validation error.
    const fs::path secondFolder = fs::path(second.packJsonPath).parent_path();
    const std::string secondBefore = ReadAll(second.packJsonPath);
    const std::set<std::string> secondFilesBefore = DirectoryFiles(secondFolder);
    HANDLE manifestLease = CreateFileA(second.packJsonPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(manifestLease != INVALID_HANDLE_VALUE, "open pack replacement denial lease");
    MissionMetadata rollbackMetadata = metadata;
    rollbackMetadata.name = "Must Roll Back";
    const fs::path rollbackStagedTrace = secondFolder / ".rollback-trace.tmp";
    Check(WriteAll(rollbackStagedTrace, traceEvidence),
          "stage raw entity trace for manifest rollback test");
    Check(!PublishMission(missionsRoot.string(), second.packJsonPath, second.defaultCategoryId, recording,
                          rollbackMetadata, ignoredPublish, error,
                          rollbackStagedTrace.string()),
          "pack replacement failure is reported");
    if (manifestLease != INVALID_HANDLE_VALUE) CloseHandle(manifestLease);
    Check(ReadAll(second.packJsonPath) == secondBefore &&
          DirectoryFiles(secondFolder) == secondFilesBefore,
          "failed pack replacement removes mission and trace and preserves manifest");

    ::Mission::Pack readOnlyPack;
    Check(::Mission::LoadPack(second.packJsonPath, readOnlyPack, error),
          "load second pack for read-only guard test");
    readOnlyPack.editable = false;
    Check(::Mission::SavePack(second.packJsonPath, readOnlyPack, error),
          "mark second pack as bundled/read-only for service guard test");
    const std::string readOnlyBefore = ReadAll(second.packJsonPath);
    const std::set<std::string> readOnlyFilesBefore = DirectoryFiles(secondFolder);
    Check(!AddCategory(missionsRoot.string(), second.packJsonPath, "Should Fail", "", ignoredCategory, error),
          "category creation rejects a read-only bundled pack");
    PackMetadata rejectedPackEdit;
    rejectedPackEdit.name = "Read Only Rename";
    rejectedPackEdit.version = "1.0";
    Check(!UpdatePackMetadata(missionsRoot.string(), second.packJsonPath,
                              rejectedPackEdit, error),
          "pack metadata edit rejects a read-only bundled pack");
    Check(!PublishMission(missionsRoot.string(), second.packJsonPath, second.defaultCategoryId, recording,
                          metadata, ignoredPublish, error),
          "mission publication rejects a read-only bundled pack");
    Check(ReadAll(second.packJsonPath) == readOnlyBefore &&
          DirectoryFiles(secondFolder) == readOnlyFilesBefore,
          "read-only guard failures do not mutate the pack directory");

    Check(::Mission::LoadPack(first.packJsonPath, packAfterPublish, error),
          "reload pack for duplicate-integrity test");
    ::Mission::Scenario duplicate = packAfterPublish.scenarios.front();
    duplicate.id += ".different-id";
    packAfterPublish.scenarios.push_back(duplicate);
    Check(::Mission::SavePack(first.packJsonPath, packAfterPublish, error),
          "seed malformed duplicate file manifest for validation test");
    const std::string malformedBefore = ReadAll(first.packJsonPath);
    const std::set<std::string> malformedFilesBefore =
        DirectoryFiles(fs::path(first.packJsonPath).parent_path());
    Check(!PublishMission(missionsRoot.string(), first.packJsonPath, offense.id, recording, metadata,
                          ignoredPublish, error),
          "existing duplicate scenario files block publication");
    Check(ReadAll(first.packJsonPath) == malformedBefore &&
          DirectoryFiles(fs::path(first.packJsonPath).parent_path()) ==
              malformedFilesBefore,
          "malformed-pack rejection makes no further mutation");
}

}

int main() {
    TestSlugsAndComponents();
    TestPackAndMissionLifecycle();
    if (g_failures != 0) {
        std::cerr << g_failures << " mission authoring check(s) failed\n";
        return 1;
    }
    std::cout << "mission authoring checks passed\n";
    return 0;
}
