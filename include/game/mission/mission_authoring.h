#pragma once

#include "mission_data.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Mission::Authoring {

struct ScenarioSummary {
    std::string id;
    std::string file;
    std::string name;
    std::string description;
    std::string categoryId;
    std::string type;
    int difficulty = 0;
    int order = 0;
};

struct PackSummary {
    std::string packJsonPath;
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string version;
    bool editable = false;
    std::vector<PackCategory> categories;
    std::vector<ScenarioSummary> scenarios;
    std::size_t missionCount = 0;
};

struct CreatePackResult {
    std::string packJsonPath;
    std::string packId;
    std::string folderName;
    std::string defaultCategoryId;
};

struct MissionMetadata {
    std::string name;
    std::string description;
    std::string type = "combo";
    int difficulty = 1;
};

struct PackMetadata {
    std::string name;
    std::string author;
    std::string description;
    std::string version;
};

struct CategoryMetadata {
    std::string label;
    std::string description;
};

struct ExistingMissionMetadata : MissionMetadata {
    std::string categoryId;
};

struct PublishResult {
    std::string missionPath;
    std::string entityTracePath;
    std::string missionId;
    std::string fileName;
    int order = 0;
};

std::string MakeSlug(const std::string& text);

// Generated authoring filenames only: rejects paths, traversal, control/non-ASCII bytes, trailing
// dots/spaces and device names. Read-only pack scenarios use ResolveScenarioMissionPath.
bool ValidateSafeComponent(const std::string& component, std::string& errorOut);

// Nested folders are allowed, but the canonical target must stay below packFolder, including
// through junctions/symlinks. Only an existing .json regular file succeeds.
bool ResolveScenarioMissionPath(const std::string& packFolder,
                                const std::string& relativeJsonPath,
                                std::string& absolutePathOut,
                                std::string& errorOut);

bool LoadPackSummary(const std::string& packJsonPath,
                     PackSummary& out,
                     std::string& errorOut);
bool EnumeratePackSummaries(const std::string& missionsRoot,
                            std::vector<PackSummary>& out,
                            std::string& errorOut);

bool CreatePack(const std::string& missionsRoot,
                const std::string& title,
                const std::string& author,
                const std::string& description,
                const std::string& version,
                CreatePackResult& out,
                std::string& errorOut);

bool AddCategory(const std::string& missionsRoot,
                 const std::string& packJsonPath,
                 const std::string& label,
                 const std::string& description,
                 PackCategory& out,
                 std::string& errorOut);

bool UpdatePackMetadata(const std::string& missionsRoot,
                        const std::string& packJsonPath,
                        const PackMetadata& metadata,
                        std::string& errorOut);
bool UpdateCategoryMetadata(const std::string& missionsRoot,
                            const std::string& packJsonPath,
                            const std::string& categoryId,
                            const CategoryMetadata& metadata,
                            std::string& errorOut);
bool UpdateMissionMetadata(const std::string& missionsRoot,
                           const std::string& packJsonPath,
                           const std::string& missionId,
                           const ExistingMissionMetadata& metadata,
                           std::string& errorOut);

bool PublishMission(const std::string& missionsRoot,
                    const std::string& packJsonPath,
                    const std::string& categoryId,
                    const Mission& recording,
                    const MissionMetadata& metadata,
                    PublishResult& out,
                    std::string& errorOut,
                    const std::string& stagedEntityTracePath = {});

}
