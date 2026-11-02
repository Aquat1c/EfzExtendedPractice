#include "../../../include/game/mission/mission_authoring.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <set>
#include <system_error>

namespace Mission::Authoring {

namespace {

namespace fs = std::filesystem;

std::atomic<unsigned long> g_tempSequence{0};

std::string Trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string LowerAscii(std::string value) {
    for (char& c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'A' && uc <= 'Z') c = static_cast<char>(uc - 'A' + 'a');
    }
    return value;
}

bool EqualInsensitive(const std::string& left, const std::string& right) {
    return LowerAscii(left) == LowerAscii(right);
}

bool IsReservedDeviceName(const std::string& component) {
    std::string stem = component;
    const std::size_t dot = stem.find('.');
    if (dot != std::string::npos) stem.resize(dot);
    stem = LowerAscii(stem);
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        stem == "clock$") {
        return true;
    }
    if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9') {
        return stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0;
    }
    return false;
}

bool ValidateDisplayText(const std::string& field,
                         const std::string& value,
                         bool required,
                         bool multiline,
                         std::size_t maxLength,
                         std::string& errorOut) {
    const std::string trimmed = Trim(value);
    if (required && trimmed.empty()) {
        errorOut = field + " cannot be empty";
        return false;
    }
    if (trimmed.size() > maxLength) {
        errorOut = field + " is too long (maximum " +
                   std::to_string(maxLength) + " bytes)";
        return false;
    }
    for (unsigned char c : value) {
        const bool allowedMultilineWhitespace =
            multiline && (c == '\t' || c == '\n' || c == '\r');
        if (c < 0x20 && !allowedMultilineWhitespace) {
            errorOut = field + " contains a control character";
            return false;
        }
    }
    return true;
}

std::string WinError(DWORD code) {
    return std::system_category().message(static_cast<int>(code)) +
           " (error " + std::to_string(code) + ")";
}

fs::path TemporarySibling(const fs::path& destination) {
    const unsigned long sequence = ++g_tempSequence;
    const std::string suffix = ".tmp-" + std::to_string(GetCurrentProcessId()) +
                               "-" + std::to_string(sequence);
    return destination.parent_path() / (destination.filename().string() + suffix);
}

void RemoveQuietly(const fs::path& path) {
    std::error_code ignored;
    fs::remove(path, ignored);
}

bool MoveNewFile(const fs::path& staged,
                 const fs::path& destination,
                 std::string& errorOut) {
    if (MoveFileExA(staged.string().c_str(), destination.string().c_str(),
                    MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    const DWORD code = GetLastError();
    errorOut = "cannot publish " + destination.string() + ": " + WinError(code);
    return false;
}

bool ReplaceExistingFile(const fs::path& staged,
                         const fs::path& destination,
                         std::string& errorOut) {
    // Replace-by-rename: the known-good manifest is never opened with truncation.
    if (MoveFileExA(staged.string().c_str(), destination.string().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    const DWORD code = GetLastError();
    errorOut = "cannot update " + destination.string() + ": " + WinError(code);
    return false;
}

bool ValidatePackPath(const std::string& value,
                      fs::path& out,
                      std::string& errorOut) {
    if (value.empty()) {
        errorOut = "pack path cannot be empty";
        return false;
    }
    const fs::path path(value);
    if (!EqualInsensitive(path.filename().string(), "pack.json")) {
        errorOut = "pack path must name pack.json";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        errorOut = "pack file does not exist: " + value;
        return false;
    }
    out = path;
    return true;
}

bool ValidatePackMutationPath(const std::string& missionsRoot,
                              const std::string& value,
                              fs::path& out,
                              std::string& errorOut) {
    if (!ValidatePackPath(value, out, errorOut)) return false;
    if (missionsRoot.empty()) {
        errorOut = "missions root cannot be empty";
        return false;
    }
    std::error_code ec;
    const fs::path canonicalRoot = fs::weakly_canonical(fs::path(missionsRoot), ec);
    if (ec) {
        errorOut = "cannot resolve missions root: " + ec.message();
        return false;
    }
    ec.clear();
    const fs::path canonicalPack = fs::weakly_canonical(out, ec);
    if (ec) {
        errorOut = "cannot resolve pack path: " + ec.message();
        return false;
    }
    // Mutations must stay within one directory level of the missions root (forged paths/junctions).
    const fs::path packRoot = canonicalPack.parent_path().parent_path();
    if (!EqualInsensitive(packRoot.string(), canonicalRoot.string())) {
        errorOut = "pack must be an immediate child of the missions root";
        return false;
    }
    out = canonicalPack;
    return true;
}

bool ValidatePackIntegrity(const Pack& pack, std::string& errorOut) {
    if (pack.format != 1) {
        errorOut = "unsupported pack format " + std::to_string(pack.format);
        return false;
    }

    std::set<std::string> categoryIds;
    for (const PackCategory& category : pack.categories) {
        if (category.id.empty()) {
            errorOut = "pack contains a category without an id";
            return false;
        }
        const std::string key = LowerAscii(category.id);
        if (!categoryIds.insert(key).second) {
            errorOut = "pack contains duplicate category id '" + category.id + "'";
            return false;
        }
    }

    std::set<std::string> scenarioIds;
    std::set<std::string> scenarioFiles;
    for (const Scenario& scenario : pack.scenarios) {
        if (!scenario.id.empty() &&
            !scenarioIds.insert(LowerAscii(scenario.id)).second) {
            errorOut = "pack contains duplicate mission id '" + scenario.id + "'";
            return false;
        }
        std::string resolvedPath;
        std::string pathError;
        if (!ResolveScenarioMissionPath(pack.folderPath, scenario.file,
                                        resolvedPath, pathError)) {
            errorOut = "pack scenario has unsafe file '" + scenario.file + "': " +
                       pathError;
            return false;
        }
        const fs::path filePath(scenario.file);
        const std::string normalizedFile =
            LowerAscii(filePath.lexically_normal().generic_string());
        if (!scenarioFiles.insert(normalizedFile).second) {
            errorOut = "pack contains duplicate mission file '" + scenario.file + "'";
            return false;
        }
    }
    return true;
}

bool LoadValidatedPack(const fs::path& packPath,
                       Pack& out,
                       std::string& errorOut) {
    Pack loaded;
    if (!LoadPack(packPath.string(), loaded, errorOut)) return false;
    if (!ValidatePackIntegrity(loaded, errorOut)) {
        errorOut = packPath.string() + ": " + errorOut;
        return false;
    }
    out = std::move(loaded);
    return true;
}

bool SavePackStaged(const fs::path& packPath,
                    const Pack& pack,
                    fs::path& stagedOut,
                    std::string& errorOut) {
    const fs::path staged = TemporarySibling(packPath);
    RemoveQuietly(staged);
    if (!SavePack(staged.string(), pack, errorOut)) {
        RemoveQuietly(staged);
        return false;
    }
    stagedOut = staged;
    return true;
}

int NextCategoryOrder(const Pack& pack) {
    int highest = 0;
    for (const PackCategory& category : pack.categories) {
        highest = (std::max)(highest, category.order);
    }
    return highest + 1;
}

int NextMissionOrder(const Pack& pack) {
    int highest = 0;
    for (const Scenario& scenario : pack.scenarios) {
        highest = (std::max)(highest, scenario.order);
    }
    return highest + 1;
}

}

std::string MakeSlug(const std::string& text) {
    std::string slug;
    slug.reserve((std::min<std::size_t>)(text.size(), 64));
    bool pendingSeparator = false;
    for (unsigned char c : text) {
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool digit = c >= '0' && c <= '9';
        if (letter || digit) {
            if (pendingSeparator && !slug.empty() && slug.size() < 64) {
                slug.push_back('-');
            }
            pendingSeparator = false;
            if (slug.size() >= 64) break;
            slug.push_back(static_cast<char>(
                c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        } else {
            pendingSeparator = !slug.empty();
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "untitled";
    if (IsReservedDeviceName(slug)) slug += "-item";
    return slug;
}

bool ValidateSafeComponent(const std::string& component, std::string& errorOut) {
    errorOut.clear();
    if (component.empty()) {
        errorOut = "component cannot be empty";
        return false;
    }
    if (component.size() > 120) {
        errorOut = "component is too long (maximum 120 bytes)";
        return false;
    }
    if (component == "." || component == ".." || component.front() == '.') {
        errorOut = "dot and hidden path components are not allowed";
        return false;
    }
    if (component.back() == ' ' || component.back() == '.') {
        errorOut = "component cannot end in a space or dot";
        return false;
    }
    for (unsigned char c : component) {
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool digit = c >= '0' && c <= '9';
        if (!(letter || digit || c == '-' || c == '_' || c == '.')) {
            errorOut = c >= 0x80
                ? "component must use ASCII characters"
                : "component contains an unsafe character";
            return false;
        }
    }
    if (IsReservedDeviceName(component)) {
        errorOut = "component is a reserved Windows device name";
        return false;
    }
    return true;
}

bool ResolveScenarioMissionPath(const std::string& packFolder,
                                const std::string& relativeJsonPath,
                                std::string& absolutePathOut,
                                std::string& errorOut) {
    absolutePathOut.clear();
    errorOut.clear();
    if (packFolder.empty()) {
        errorOut = "pack folder cannot be empty";
        return false;
    }
    if (relativeJsonPath.empty() || relativeJsonPath.size() > 1024) {
        errorOut = relativeJsonPath.empty()
            ? "scenario path cannot be empty"
            : "scenario path is too long";
        return false;
    }
    if (relativeJsonPath.find(':') != std::string::npos) {
        errorOut = "scenario path cannot contain a drive or stream prefix";
        return false;
    }

    const fs::path relative(relativeJsonPath);
    if (relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory()) {
        errorOut = "scenario path must be relative to its pack";
        return false;
    }
    if (relative.empty() || relative.filename().empty()) {
        errorOut = "scenario path must name a JSON file";
        return false;
    }
    for (const fs::path& part : relative) {
        const std::string component = part.string();
        std::string componentError;
        if (!ValidateSafeComponent(component, componentError)) {
            errorOut = "unsafe scenario path component '" + component + "': " +
                       componentError;
            return false;
        }
    }
    if (!EqualInsensitive(relative.extension().string(), ".json")) {
        errorOut = "scenario path must end in .json";
        return false;
    }

    std::error_code ec;
    const fs::path folder = fs::weakly_canonical(fs::path(packFolder), ec);
    if (ec || !fs::is_directory(folder, ec) || ec) {
        errorOut = "pack folder does not exist or cannot be resolved";
        return false;
    }
    ec.clear();
    const fs::path target = fs::weakly_canonical(folder / relative, ec);
    if (ec) {
        errorOut = "scenario path cannot be resolved: " + ec.message();
        return false;
    }

    auto folderIt = folder.begin();
    auto targetIt = target.begin();
    for (; folderIt != folder.end(); ++folderIt, ++targetIt) {
        if (targetIt == target.end() ||
            !EqualInsensitive(folderIt->string(), targetIt->string())) {
            errorOut = "scenario path escapes its pack folder";
            return false;
        }
    }
    if (targetIt == target.end()) {
        errorOut = "scenario path must name a file below its pack folder";
        return false;
    }
    ec.clear();
    if (!fs::is_regular_file(target, ec) || ec) {
        errorOut = "scenario file does not exist: " + target.string();
        return false;
    }
    absolutePathOut = target.string();
    return true;
}

bool LoadPackSummary(const std::string& packJsonPath,
                     PackSummary& out,
                     std::string& errorOut) {
    errorOut.clear();
    fs::path path;
    if (!ValidatePackPath(packJsonPath, path, errorOut)) return false;
    Pack pack;
    if (!LoadValidatedPack(path, pack, errorOut)) return false;

    PackSummary summary;
    summary.packJsonPath = path.string();
    summary.id = pack.id;
    summary.name = pack.name;
    summary.author = pack.author;
    summary.description = pack.description;
    summary.version = pack.version;
    summary.editable = pack.editable;
    summary.categories = pack.categories;
    summary.scenarios.reserve(pack.scenarios.size());
    for (const Scenario& scenario : pack.scenarios) {
        ScenarioSummary item;
        item.id = scenario.id;
        item.file = scenario.file;
        item.name = scenario.name;
        item.description = scenario.description;
        item.categoryId = scenario.category;
        item.difficulty = scenario.difficulty;
        item.order = scenario.order;
        std::string missionPath;
        std::string missionError;
        Mission mission;
        if (ResolveScenarioMissionPath(pack.folderPath, scenario.file,
                                       missionPath, missionError) &&
            LoadMission(missionPath, mission, missionError)) {
            item.type = mission.type;
            if (item.name.empty()) item.name = mission.name;
            if (item.description.empty()) item.description = mission.description;
            if (item.categoryId.empty()) item.categoryId = mission.category;
            if (item.difficulty <= 0) item.difficulty = mission.difficulty;
        }
        summary.scenarios.push_back(std::move(item));
    }
    summary.missionCount = pack.scenarios.size();
    out = std::move(summary);
    return true;
}

bool EnumeratePackSummaries(const std::string& missionsRoot,
                            std::vector<PackSummary>& out,
                            std::string& errorOut) {
    errorOut.clear();
    std::vector<PackSummary> summaries;
    std::vector<std::string> paths = DiscoverPackJsonPaths(missionsRoot);
    std::sort(paths.begin(), paths.end(), [](const std::string& left,
                                             const std::string& right) {
        return LowerAscii(left) < LowerAscii(right);
    });
    for (const std::string& path : paths) {
        PackSummary summary;
        std::string packError;
        if (!LoadPackSummary(path, summary, packError)) {
            if (errorOut.empty()) {
                errorOut = "skipped unreadable pack " + path + ": " + packError;
            }
            continue;
        }
        summaries.push_back(std::move(summary));
    }
    out = std::move(summaries);
    return true;
}

bool CreatePack(const std::string& missionsRoot,
                const std::string& title,
                const std::string& author,
                const std::string& description,
                const std::string& version,
                CreatePackResult& out,
                std::string& errorOut) {
    errorOut.clear();
    if (missionsRoot.empty()) {
        errorOut = "missions root cannot be empty";
        return false;
    }
    if (!ValidateDisplayText("pack name", title, true, false, 128, errorOut) ||
        !ValidateDisplayText("author", author, false, false, 128, errorOut) ||
        !ValidateDisplayText("description", description, false, true, 4096, errorOut) ||
        !ValidateDisplayText("version", version, false, false, 32, errorOut)) {
        return false;
    }

    const fs::path root(missionsRoot);
    std::error_code ec;
    if (fs::exists(root, ec) && !fs::is_directory(root, ec)) {
        errorOut = "missions root is not a directory: " + missionsRoot;
        return false;
    }
    if (!fs::exists(root, ec) && !fs::create_directories(root, ec)) {
        errorOut = "cannot create missions root: " + ec.message();
        return false;
    }

    std::set<std::string> packIds;
    for (const std::string& existingPath : DiscoverPackJsonPaths(missionsRoot)) {
        Pack existing;
        std::string loadError;
        if (!LoadPack(existingPath, existing, loadError)) {
            continue;
        }
        if (!existing.id.empty()) packIds.insert(LowerAscii(existing.id));
    }

    const std::string base = MakeSlug(Trim(title));
    fs::path folder;
    std::string folderName;
    std::string packId;
    bool created = false;
    for (int suffix = 1; suffix < 10000; ++suffix) {
        folderName = suffix == 1 ? base : base + "-" + std::to_string(suffix);
        packId = "local." + folderName;
        if (packIds.find(LowerAscii(packId)) != packIds.end()) continue;
        folder = root / folderName;
        ec.clear();
        if (fs::create_directory(folder, ec)) {
            created = true;
            break;
        }
        if (ec) {
            errorOut = "cannot create pack folder " + folder.string() + ": " + ec.message();
            return false;
        }
    }
    if (!created) {
        errorOut = "could not allocate a unique pack folder";
        return false;
    }

    Pack pack;
    pack.format = 1;
    pack.id = packId;
    pack.name = Trim(title);
    pack.author = Trim(author);
    pack.description = Trim(description);
    pack.version = Trim(version).empty() ? "1.0" : Trim(version);
    pack.editable = true;
    PackCategory general;
    general.id = "general";
    general.label = "General";
    general.description = "Trials that do not need a more specific category.";
    general.order = 1;
    pack.categories.push_back(general);

    const fs::path packPath = folder / "pack.json";
    fs::path staged;
    if (!SavePackStaged(packPath, pack, staged, errorOut) ||
        !MoveNewFile(staged, packPath, errorOut)) {
        RemoveQuietly(staged);
        std::error_code cleanupError;
        fs::remove_all(folder, cleanupError);
        return false;
    }

    CreatePackResult result;
    result.packJsonPath = packPath.string();
    result.packId = pack.id;
    result.folderName = folderName;
    result.defaultCategoryId = general.id;
    out = std::move(result);
    return true;
}

bool AddCategory(const std::string& missionsRoot,
                 const std::string& packJsonPath,
                 const std::string& label,
                 const std::string& description,
                 PackCategory& out,
                 std::string& errorOut) {
    errorOut.clear();
    if (!ValidateDisplayText("category name", label, true, false, 96, errorOut) ||
        !ValidateDisplayText("category description", description, false, true, 1024,
                             errorOut)) {
        return false;
    }
    fs::path packPath;
    if (!ValidatePackMutationPath(missionsRoot, packJsonPath, packPath, errorOut)) return false;
    Pack pack;
    if (!LoadValidatedPack(packPath, pack, errorOut)) return false;
    if (!pack.editable) {
        errorOut = "pack is read-only; only packs created by the local authoring flow can be changed";
        return false;
    }

    const std::string cleanLabel = Trim(label);
    for (const PackCategory& category : pack.categories) {
        if (EqualInsensitive(Trim(category.label), cleanLabel)) {
            errorOut = "category '" + cleanLabel + "' already exists";
            return false;
        }
    }

    const std::string base = MakeSlug(cleanLabel);
    std::set<std::string> usedIds;
    for (const PackCategory& category : pack.categories) {
        usedIds.insert(LowerAscii(category.id));
    }
    std::string id;
    for (int suffix = 1; suffix < 10000; ++suffix) {
        id = suffix == 1 ? base : base + "-" + std::to_string(suffix);
        if (usedIds.find(LowerAscii(id)) == usedIds.end()) break;
        id.clear();
    }
    if (id.empty()) {
        errorOut = "could not allocate a unique category id";
        return false;
    }

    PackCategory category;
    category.id = id;
    category.label = cleanLabel;
    category.description = Trim(description);
    category.order = NextCategoryOrder(pack);
    pack.categories.push_back(category);

    fs::path staged;
    if (!SavePackStaged(packPath, pack, staged, errorOut)) return false;
    if (!ReplaceExistingFile(staged, packPath, errorOut)) {
        RemoveQuietly(staged);
        return false;
    }
    out = std::move(category);
    return true;
}

bool UpdatePackMetadata(const std::string& missionsRoot,
                        const std::string& packJsonPath,
                        const PackMetadata& metadata,
                        std::string& errorOut) {
    errorOut.clear();
    if (!ValidateDisplayText("pack name", metadata.name, true, false, 128,
                             errorOut) ||
        !ValidateDisplayText("author", metadata.author, false, false, 128,
                             errorOut) ||
        !ValidateDisplayText("description", metadata.description, false, true,
                             4096, errorOut) ||
        !ValidateDisplayText("version", metadata.version, false, false, 32,
                             errorOut)) {
        return false;
    }
    fs::path packPath;
    if (!ValidatePackMutationPath(missionsRoot, packJsonPath, packPath,
                                  errorOut)) {
        return false;
    }
    Pack pack;
    if (!LoadValidatedPack(packPath, pack, errorOut)) return false;
    if (!pack.editable) {
        errorOut = "pack is read-only; only packs created by the local authoring flow can be changed";
        return false;
    }
    pack.name = Trim(metadata.name);
    pack.author = Trim(metadata.author);
    pack.description = Trim(metadata.description);
    pack.version = Trim(metadata.version).empty() ? "1.0" : Trim(metadata.version);

    fs::path staged;
    if (!SavePackStaged(packPath, pack, staged, errorOut)) return false;
    if (!ReplaceExistingFile(staged, packPath, errorOut)) {
        RemoveQuietly(staged);
        return false;
    }
    return true;
}

bool UpdateCategoryMetadata(const std::string& missionsRoot,
                            const std::string& packJsonPath,
                            const std::string& categoryId,
                            const CategoryMetadata& metadata,
                            std::string& errorOut) {
    errorOut.clear();
    if (categoryId.empty()) {
        errorOut = "category id cannot be empty";
        return false;
    }
    if (!ValidateDisplayText("category name", metadata.label, true, false, 96,
                             errorOut) ||
        !ValidateDisplayText("category description", metadata.description,
                             false, true, 1024, errorOut)) {
        return false;
    }
    fs::path packPath;
    if (!ValidatePackMutationPath(missionsRoot, packJsonPath, packPath,
                                  errorOut)) {
        return false;
    }
    Pack pack;
    if (!LoadValidatedPack(packPath, pack, errorOut)) return false;
    if (!pack.editable) {
        errorOut = "pack is read-only; only packs created by the local authoring flow can be changed";
        return false;
    }
    PackCategory* target = nullptr;
    for (PackCategory& category : pack.categories) {
        if (category.id == categoryId) target = &category;
        else if (EqualInsensitive(Trim(category.label), Trim(metadata.label))) {
            errorOut = "category '" + Trim(metadata.label) + "' already exists";
            return false;
        }
    }
    if (!target) {
        errorOut = "category '" + categoryId + "' does not exist in this pack";
        return false;
    }
    target->label = Trim(metadata.label);
    target->description = Trim(metadata.description);

    fs::path staged;
    if (!SavePackStaged(packPath, pack, staged, errorOut)) return false;
    if (!ReplaceExistingFile(staged, packPath, errorOut)) {
        RemoveQuietly(staged);
        return false;
    }
    return true;
}

bool UpdateMissionMetadata(const std::string& missionsRoot,
                           const std::string& packJsonPath,
                           const std::string& missionId,
                           const ExistingMissionMetadata& metadata,
                           std::string& errorOut) {
    errorOut.clear();
    if (missionId.empty()) {
        errorOut = "mission id cannot be empty";
        return false;
    }
    if (!ValidateDisplayText("mission name", metadata.name, true, false, 128,
                             errorOut) ||
        !ValidateDisplayText("mission description", metadata.description,
                             false, true, 4096, errorOut)) {
        return false;
    }
    if (metadata.difficulty < 1 || metadata.difficulty > 5) {
        errorOut = "difficulty must be between 1 and 5";
        return false;
    }
    const std::string type = LowerAscii(Trim(metadata.type));
    if (type != "combo" && type != "trial" && type != "mission") {
        errorOut = "mission type must be combo, trial, or mission";
        return false;
    }

    fs::path packPath;
    if (!ValidatePackMutationPath(missionsRoot, packJsonPath, packPath,
                                  errorOut)) {
        return false;
    }
    Pack pack;
    if (!LoadValidatedPack(packPath, pack, errorOut)) return false;
    if (!pack.editable) {
        errorOut = "pack is read-only; only packs created by the local authoring flow can be changed";
        return false;
    }
    const PackCategory* destination = nullptr;
    for (const PackCategory& category : pack.categories) {
        if (category.id == metadata.categoryId) {
            destination = &category;
            break;
        }
    }
    if (!destination) {
        errorOut = "category '" + metadata.categoryId +
                   "' does not exist in this pack";
        return false;
    }

    Scenario* scenario = nullptr;
    for (Scenario& candidate : pack.scenarios) {
        if (candidate.id == missionId) {
            scenario = &candidate;
            break;
        }
    }
    if (!scenario) {
        errorOut = "mission '" + missionId + "' does not exist in this pack";
        return false;
    }
    std::string missionPathString;
    if (!ResolveScenarioMissionPath(pack.folderPath, scenario->file,
                                    missionPathString, errorOut)) {
        return false;
    }
    const fs::path missionPath(missionPathString);
    Mission mission;
    if (!LoadMission(missionPath.string(), mission, errorOut)) return false;
    if (mission.tutorialSchema > 0) {
        errorOut = "tutorial lessons cannot be edited through the mission workshop";
        return false;
    }

    const std::string cleanName = Trim(metadata.name);
    const std::string cleanDescription = Trim(metadata.description);
    scenario->name = cleanName;
    scenario->description = cleanDescription;
    scenario->category = destination->id;
    scenario->difficulty = metadata.difficulty;
    mission.name = cleanName;
    mission.description = cleanDescription;
    mission.category = destination->id;
    mission.type = type;
    mission.difficulty = metadata.difficulty;
    mission.id = scenario->id;
    mission.order = scenario->order;
    mission.sourcePath.clear();

    const fs::path stagedMission = TemporarySibling(missionPath);
    const fs::path stagedPack = TemporarySibling(packPath);
    const fs::path backupMission = TemporarySibling(missionPath);
    RemoveQuietly(stagedMission);
    RemoveQuietly(stagedPack);
    RemoveQuietly(backupMission);
    if (!SaveMission(stagedMission.string(), mission, errorOut)) {
        RemoveQuietly(stagedMission);
        return false;
    }
    if (!SavePack(stagedPack.string(), pack, errorOut)) {
        RemoveQuietly(stagedMission);
        RemoveQuietly(stagedPack);
        return false;
    }
    if (!CopyFileA(missionPath.string().c_str(), backupMission.string().c_str(),
                   TRUE)) {
        errorOut = "cannot prepare mission rollback copy: " +
                   WinError(GetLastError());
        RemoveQuietly(stagedMission);
        RemoveQuietly(stagedPack);
        RemoveQuietly(backupMission);
        return false;
    }
    if (!ReplaceExistingFile(stagedMission, missionPath, errorOut)) {
        RemoveQuietly(stagedMission);
        RemoveQuietly(stagedPack);
        RemoveQuietly(backupMission);
        return false;
    }
    if (!ReplaceExistingFile(stagedPack, packPath, errorOut)) {
        RemoveQuietly(stagedPack);
        if (!MoveFileExA(backupMission.string().c_str(),
                         missionPath.string().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            errorOut += "; mission rollback failed: " + WinError(GetLastError()) +
                        "; original retained at " + backupMission.string();
        } else {
            RemoveQuietly(backupMission);
        }
        return false;
    }
    RemoveQuietly(backupMission);
    return true;
}

bool PublishMission(const std::string& missionsRoot,
                    const std::string& packJsonPath,
                    const std::string& categoryId,
                    const Mission& recording,
                    const MissionMetadata& metadata,
                    PublishResult& out,
                    std::string& errorOut,
                    const std::string& stagedEntityTracePath) {
    errorOut.clear();
    if (!ValidateDisplayText("mission name", metadata.name, true, false, 128, errorOut) ||
        !ValidateDisplayText("mission description", metadata.description, false, true, 4096,
                             errorOut)) {
        return false;
    }
    if (metadata.difficulty < 1 || metadata.difficulty > 5) {
        errorOut = "difficulty must be between 1 and 5";
        return false;
    }
    const std::string type = LowerAscii(Trim(metadata.type));
    if (type != "combo" && type != "trial" && type != "mission") {
        errorOut = "mission type must be combo, trial, or mission";
        return false;
    }
    if (recording.tutorialSchema > 0) {
        errorOut = "tutorial lessons cannot be published through the trial recorder";
        return false;
    }

    fs::path packPath;
    if (!ValidatePackMutationPath(missionsRoot, packJsonPath, packPath, errorOut)) return false;
    Pack pack;
    if (!LoadValidatedPack(packPath, pack, errorOut)) return false;
    if (!pack.editable) {
        errorOut = "pack is read-only; only packs created by the local authoring flow can be changed";
        return false;
    }

    fs::path stagedEntityTrace;
    const bool hasEntityTrace = !stagedEntityTracePath.empty();
    if (hasEntityTrace) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(stagedEntityTracePath), ec) || ec) {
            errorOut = "staged entity trace does not exist: " +
                       stagedEntityTracePath;
            return false;
        }
        ec.clear();
        stagedEntityTrace = fs::weakly_canonical(
            fs::path(stagedEntityTracePath), ec);
        if (ec) {
            errorOut = "cannot resolve staged entity trace: " + ec.message();
            return false;
        }
        if (!EqualInsensitive(stagedEntityTrace.parent_path().string(),
                              packPath.parent_path().string())) {
            errorOut = "staged entity trace must be beside pack.json";
            return false;
        }
        if (EqualInsensitive(stagedEntityTrace.string(), packPath.string())) {
            errorOut = "pack.json cannot be used as an entity trace staging file";
            return false;
        }
    }

    const PackCategory* destinationCategory = nullptr;
    for (const PackCategory& category : pack.categories) {
        if (category.id == categoryId) {
            destinationCategory = &category;
            break;
        }
    }
    if (!destinationCategory) {
        errorOut = "category '" + categoryId + "' does not exist in this pack";
        return false;
    }

    std::set<std::string> usedIds;
    std::set<std::string> usedFiles;
    for (const Scenario& scenario : pack.scenarios) {
        if (!scenario.id.empty()) usedIds.insert(LowerAscii(scenario.id));
        usedFiles.insert(LowerAscii(scenario.file));
    }

    const std::string base = MakeSlug(Trim(metadata.name));
    const std::string packScope = !pack.id.empty()
        ? pack.id
        : "local." + MakeSlug(pack.name);
    std::string slug;
    std::string missionId;
    std::string fileName;
    fs::path missionPath;
    fs::path entityTracePath;
    std::error_code ec;
    for (int suffix = 1; suffix < 10000; ++suffix) {
        slug = suffix == 1 ? base : base + "-" + std::to_string(suffix);
        missionId = packScope + "." + slug;
        fileName = slug + ".json";
        missionPath = packPath.parent_path() / fileName;
        entityTracePath = packPath.parent_path() /
                          (slug + ".entities.jsonl");
        ec.clear();
        const bool missionExists = fs::exists(missionPath, ec);
        if (ec) {
            errorOut = "cannot inspect mission destination: " + ec.message();
            return false;
        }
        ec.clear();
        const bool traceExists = hasEntityTrace && fs::exists(entityTracePath, ec);
        if (ec) {
            errorOut = "cannot inspect entity trace destination: " + ec.message();
            return false;
        }
        if (!missionExists && !traceExists &&
            usedIds.find(LowerAscii(missionId)) == usedIds.end() &&
            usedFiles.find(LowerAscii(fileName)) == usedFiles.end()) {
            break;
        }
        slug.clear();
    }
    if (slug.empty()) {
        errorOut = "could not allocate a unique mission id and filename";
        return false;
    }

    const int order = NextMissionOrder(pack);
    Mission mission = recording;
    mission.id = missionId;
    mission.lessonId.clear();
    mission.type = type;
    mission.category = destinationCategory->id;
    mission.difficulty = metadata.difficulty;
    mission.order = order;
    mission.name = Trim(metadata.name);
    mission.description = Trim(metadata.description);
    mission.sourcePath.clear();

    Scenario scenario;
    scenario.id = missionId;
    scenario.name = mission.name;
    scenario.file = fileName;
    scenario.description = mission.description;
    scenario.category = destinationCategory->id;
    scenario.order = order;
    scenario.difficulty = mission.difficulty;
    scenario.locked = false;
    pack.scenarios.push_back(scenario);

    const fs::path stagedMission = TemporarySibling(missionPath);
    const fs::path stagedPack = TemporarySibling(packPath);
    RemoveQuietly(stagedMission);
    RemoveQuietly(stagedPack);
    if (!SaveMission(stagedMission.string(), mission, errorOut)) {
        RemoveQuietly(stagedMission);
        return false;
    }
    if (!SavePack(stagedPack.string(), pack, errorOut)) {
        RemoveQuietly(stagedMission);
        RemoveQuietly(stagedPack);
        return false;
    }

    if (!MoveNewFile(stagedMission, missionPath, errorOut)) {
        RemoveQuietly(stagedMission);
        RemoveQuietly(stagedPack);
        return false;
    }
    if (hasEntityTrace &&
        !MoveNewFile(stagedEntityTrace, entityTracePath, errorOut)) {
        RemoveQuietly(stagedPack);
        std::error_code rollbackError;
        if (!fs::remove(missionPath, rollbackError) && rollbackError) {
            errorOut += "; rollback could not remove " + missionPath.string() +
                        ": " + rollbackError.message();
        }
        return false;
    }
    if (!ReplaceExistingFile(stagedPack, packPath, errorOut)) {
        RemoveQuietly(stagedPack);
        std::error_code rollbackError;
        if (!fs::remove(missionPath, rollbackError) && rollbackError) {
            errorOut += "; rollback could not remove " + missionPath.string() +
                        ": " + rollbackError.message();
        }
        if (hasEntityTrace) {
            rollbackError.clear();
            if (!fs::remove(entityTracePath, rollbackError) && rollbackError) {
                errorOut += "; rollback could not remove " +
                            entityTracePath.string() + ": " +
                            rollbackError.message();
            }
        }
        return false;
    }

    PublishResult result;
    result.missionPath = missionPath.string();
    if (hasEntityTrace) result.entityTracePath = entityTracePath.string();
    result.missionId = missionId;
    result.fileName = fileName;
    result.order = order;
    out = std::move(result);
    return true;
}

}
