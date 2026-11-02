#pragma once

#include "game/mission/mission_data.h"

#include <string>

namespace Mission::LegacyEntityMigration {

enum class Result {
    NotApplicable = 0,
    Upgraded,
    EvidenceRejected,
};

struct Report {
    Result result = Result::NotApplicable;
    int objectivesAdded = 0;
    std::string diagnostic;
};

// Read-only adapter for format-1 recordings; the on-disk JSON/sidecar are never modified.
Report UpgradeLegacyLifecycleReviews(Mission& mission,
                                     const std::string& sourcePath);

}
