#include "../../../include/game/mission/mission_movedata.h"
#include "../../../include/game/character_settings.h"
#include "../../../include/core/logger.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace Mission::MoveData {

namespace {

struct CharEntry {
    bool attempted = false;
    bool ok = false;
    std::unordered_map<int, Cls> classById;
    std::unordered_map<int, bool> attackById;
    std::vector<int> specials;
    std::vector<int> entities;
    std::vector<int> attackEntities;
};

std::unordered_map<int, CharEntry> g_cache;
const std::vector<int> g_emptySpecials;
const std::vector<int> g_emptyEntities;

Cls ParseClass(const std::string& c) {
    if (c == "A")            return Cls::A;
    if (c == "B")            return Cls::B;
    if (c == "C")            return Cls::C;
    if (c == "command")      return Cls::Command;
    if (c == "special")      return Cls::Special;
    if (c == "super")        return Cls::Super;
    if (c == "projectile")   return Cls::Projectile;
    if (c == "super_entity") return Cls::SuperEntity;
    if (c == "entity")       return Cls::Entity;
    if (c == "system")       return Cls::System;
    return Cls::Unknown;
}

std::string ResolveFile(const std::string& resource) {
    if (resource.empty() || resource == "unknown") return std::string();
    HMODULE mod = GetModuleHandleA("efz_training_mode.dll");
    char path[MAX_PATH] = {0};
    if (!mod || GetModuleFileNameA(mod, path, MAX_PATH) == 0) return std::string();
    std::string dir(path);
    const size_t slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) return std::string();
    dir.resize(slash + 1);
    return dir + "assets\\movedata\\" + resource + ".json";
}

template <typename T>
void SortUnique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

bool ParseFile(const std::string& file, CharEntry& e) {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) return false;
    try {
        auto j = nlohmann::json::parse(text);
        auto it = j.find("moves");
        if (it == j.end() || !it->is_object()) return false;
        for (auto m = it->begin(); m != it->end(); ++m) {
            const int id = std::atoi(m.key().c_str());
            const auto& v = m.value();
            Cls cls = Cls::Unknown;
            if (v.contains("class") && v["class"].is_string())
                cls = ParseClass(v["class"].get<std::string>());
            bool atk = v.contains("attack") && v["attack"].is_boolean() && v["attack"].get<bool>();
            auto priorClass = e.classById.find(id);
            const Cls mergedClass = priorClass == e.classById.end()
                ? cls : Detail::MergeProfileClass(priorClass->second, cls, id);
            e.classById[id] = mergedClass;
            auto priorAttack = e.attackById.find(id);
            const bool mergedAttack = Detail::MergeProfileAttack(
                priorAttack != e.attackById.end() && priorAttack->second, atk);
            e.attackById[id] = mergedAttack;
            if (cls == Cls::Special || cls == Cls::Super)
                e.specials.push_back(id);
            if (id >= 400 && (cls == Cls::Entity || cls == Cls::Projectile ||
                              cls == Cls::SuperEntity)) {
                e.entities.push_back(id);
                if (atk) e.attackEntities.push_back(id);
            }
        }
    } catch (...) {
        return false;
    }
    SortUnique(e.specials);
    SortUnique(e.entities);
    SortUnique(e.attackEntities);
    return true;
}

}

bool EnsureLoaded(int charId) {
    auto it = g_cache.find(charId);
    if (it != g_cache.end()) return it->second.ok;

    CharEntry e;
    e.attempted = true;
    const std::string resource = CharacterSettings::GetCharacterInternalName(charId);
    std::vector<std::string> profiles;
    if (!resource.empty() && resource != "unknown") profiles.push_back(resource);
    // nanase/ikumi swap to alternate .pat tables without changing their
    // character ID.
    if (resource == "nanase") profiles.push_back("nanase2");
    if (resource == "ikumi") profiles.push_back("ikumi2");

    bool allLoaded = !profiles.empty();
    std::string loadedFiles;
    for (const std::string& profile : profiles) {
        const std::string file = ResolveFile(profile);
        const bool loaded = !file.empty() && ParseFile(file, e);
        allLoaded = allLoaded && loaded;
        if (!loadedFiles.empty()) loadedFiles += ",";
        loadedFiles += file.empty() ? profile : file;
    }
    e.ok = allLoaded;
    if (!profiles.empty()) {
        LogOut(std::string("[MISSION] movedata ") + (e.ok ? "loaded " : "FAILED ") + loadedFiles +
               (e.ok ? (" (" + std::to_string(e.classById.size()) + " ids, " +
                        std::to_string(e.specials.size()) + " specials, " +
                        std::to_string(e.entities.size()) + " entities, " +
                        std::to_string(e.attackEntities.size()) + " attack entities)") : ""),
               true);
    }
    bool ok = e.ok;
    g_cache.emplace(charId, std::move(e));
    return ok;
}

Cls GetClass(int charId, int moveId) {
    auto it = g_cache.find(charId);
    if (it == g_cache.end() || !it->second.ok) return Cls::Unknown;
    auto m = it->second.classById.find(moveId);
    return m != it->second.classById.end() ? m->second : Cls::Unknown;
}

const char* ClassName(Cls c) {
    switch (c) {
        case Cls::System:      return "system";
        case Cls::A:           return "A";
        case Cls::B:           return "B";
        case Cls::C:           return "C";
        case Cls::Command:     return "command";
        case Cls::Special:     return "special";
        case Cls::Super:       return "super";
        case Cls::Projectile:  return "projectile";
        case Cls::SuperEntity: return "super_ent";
        case Cls::Entity:      return "entity";
        default:               return "";
    }
}

bool IsAttack(int charId, int moveId) {
    auto it = g_cache.find(charId);
    if (it == g_cache.end() || !it->second.ok) return false;
    auto m = it->second.attackById.find(moveId);
    return m != it->second.attackById.end() && m->second;
}

bool IsSpecialOrSuper(int charId, int moveId) {
    const Cls c = GetClass(charId, moveId);
    return c == Cls::Special || c == Cls::Super;
}

bool IsKnownEntityPattern(int charId, int moveId) {
    if (moveId < 400) return false;
    const Cls c = GetClass(charId, moveId);
    return c == Cls::Entity || c == Cls::Projectile || c == Cls::SuperEntity;
}

bool IsCommonSystemHelperCandidate(int moveId) {
    return Detail::IsCommonSystemHelperCandidateId(moveId);
}

const std::vector<int>& Specials(int charId) {
    auto it = g_cache.find(charId);
    if (it == g_cache.end() || !it->second.ok) return g_emptySpecials;
    return it->second.specials;
}

const std::vector<int>& EntityPatterns(int charId) {
    auto it = g_cache.find(charId);
    if (it == g_cache.end() || !it->second.ok) return g_emptyEntities;
    return it->second.entities;
}

const std::vector<int>& AttackEntityPatterns(int charId) {
    auto it = g_cache.find(charId);
    if (it == g_cache.end() || !it->second.ok) return g_emptyEntities;
    return it->second.attackEntities;
}

}
