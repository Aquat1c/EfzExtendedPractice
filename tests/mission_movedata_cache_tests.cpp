#include <windows.h>
#include "game/character_settings.h"
#include "core/logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

HMODULE g_hSelfModule = reinterpret_cast<HMODULE>(uintptr_t(1));

namespace {
constexpr int kCharacters = 96;
std::unordered_map<std::string, std::string> g_fixtures;
std::array<std::atomic<unsigned>, kCharacters + 3> g_nameReads{};
std::atomic<unsigned> g_logCount{0};

HRSRC WINAPI FixtureResource(HMODULE module, LPCSTR name, LPCSTR type) {
    if (module != g_hSelfModule || !name || !type || std::strcmp(type, "MOVEDATA")) return nullptr;
    const auto found = g_fixtures.find(name);
    return found == g_fixtures.end() ? nullptr
        : reinterpret_cast<HRSRC>(const_cast<std::string*>(&found->second));
}
HGLOBAL WINAPI FixtureLoadResource(HMODULE, HRSRC resource) {
    return reinterpret_cast<HGLOBAL>(resource);
}
LPVOID WINAPI FixtureLockResource(HGLOBAL resource) {
    return resource ? const_cast<char*>(reinterpret_cast<std::string*>(resource)->data()) : nullptr;
}
DWORD WINAPI FixtureResourceSize(HMODULE, HRSRC resource) {
    return resource ? DWORD(reinterpret_cast<std::string*>(resource)->size()) : 0;
}
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

namespace CharacterSettings {
std::string GetCharacterInternalName(int charId) {
    if (charId < 0 || charId >= int(g_nameReads.size())) return "unknown";
    ++g_nameReads[charId];
    if (charId == kCharacters) return "nanase";
    if (charId == kCharacters + 1) return "broken";
    if (charId == kCharacters + 2) return "missing";
    return "fixture" + std::to_string(charId);
}
}
void LogOut(const std::string&, bool) { ++g_logCount; }

#define FindResourceA FixtureResource
#define LoadResource FixtureLoadResource
#define LockResource FixtureLockResource
#define SizeofResource FixtureResourceSize
#include "../src/game/mission/mission_movedata.cpp"
#undef SizeofResource
#undef LockResource
#undef LoadResource
#undef FindResourceA

int main() {
    using namespace Mission::MoveData;
    try {
        for (int i = 0; i < kCharacters; ++i) {
            const auto json = nlohmann::json{
                {"moves", {
                    {"200", {{"class", "A"}, {"attack", true}}},
                    {"250", {{"class", "special"}, {"attack", true}}},
                    {"300", {{"class", "super"}, {"attack", true}}},
                    {"400", {{"class", "projectile"}, {"attack", true}}},
                    {"401", {{"class", "entity"}, {"attack", false}}}
                }}
            };
            g_fixtures["FIXTURE" + std::to_string(i)] = json.dump();
        }
        g_fixtures["NANASE"] = R"({"moves":{"250":{"class":"special","attack":true},"413":{"class":"entity","attack":false}}})";
        g_fixtures["NANASE2"] = R"({"moves":{"250":{"class":"system","attack":false},"413":{"class":"projectile","attack":true}}})";
        g_fixtures["BROKEN"] = "{invalid";

        Check(EnsureLoaded(0), "initial fixture load failed");
        const auto& borrowedSpecials = Specials(0);
        const auto& borrowedEntities = EntityPatterns(0);
        const auto& borrowedAttackEntities = AttackEntityPatterns(0);
        const auto* specialsAddress = &borrowedSpecials;
        Check(borrowedSpecials == std::vector<int>({250,300}), "initial specials");
        Check(borrowedEntities == std::vector<int>({400,401}), "initial entities");
        Check(borrowedAttackEntities == std::vector<int>({400}), "initial attack entities");

        std::atomic<unsigned> ready{0};
        std::atomic<bool> start{false}, failed{false};
        std::array<std::atomic<bool>, kCharacters> published{};
        published[0] = true;
        std::vector<std::thread> workers;
        constexpr unsigned loaders = 6, readers = 6;
        for (unsigned worker = 0; worker < loaders + readers; ++worker) {
            workers.emplace_back([&, worker] {
                ++ready;
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                if (worker < loaders) {
                    for (unsigned round = 0; round < 8; ++round) {
                        for (int step = 1; step < kCharacters; ++step) {
                            const int id = 1 + (step - 1 + int(worker) * 13) % (kCharacters - 1);
                            if (!EnsureLoaded(id)) failed = true;
                            published[id].store(true, std::memory_order_release);
                            if (GetClass(id,200) != Cls::A || !IsAttack(id,250) ||
                                !IsSpecialOrSuper(id,300) || !IsKnownEntityPattern(id,400))
                                failed = true;
                        }
                        if (!EnsureLoaded(kCharacters) || EnsureLoaded(kCharacters+1) ||
                            EnsureLoaded(kCharacters+2)) failed = true;
                    }
                } else {
                    for (unsigned round = 0; round < 3000; ++round) {
                        const int id = int((round * 17 + worker) % kCharacters);
                        const bool loaded = published[id].load(std::memory_order_acquire);
                        const auto cls = GetClass(id,250);
                        const bool attack = IsAttack(id,250);
                        const auto& specials = Specials(id);
                        const auto& entities = EntityPatterns(id);
                        const auto& attackEntities = AttackEntityPatterns(id);
                        if (loaded && (cls != Cls::Special || !attack || specials.size() != 2 ||
                            entities.size() != 2 || attackEntities.size() != 1)) failed = true;
                        if ((cls != Cls::Unknown && cls != Cls::Special) ||
                            (!specials.empty() && specials != borrowedSpecials) ||
                            (!entities.empty() && entities != borrowedEntities) ||
                            (!attackEntities.empty() && attackEntities != borrowedAttackEntities)) failed = true;
                        if (borrowedSpecials[0] != 250 || borrowedEntities[1] != 401 ||
                            borrowedAttackEntities[0] != 400) failed = true;
                    }
                }
            });
        }
        while (ready.load(std::memory_order_acquire) != loaders + readers) std::this_thread::yield();
        start.store(true,std::memory_order_release);
        for (auto& worker : workers) worker.join();
        Check(!failed.load(), "concurrent first-load/query returned inconsistent metadata");
        for (const auto& reads : g_nameReads)
            Check(reads.load() == 1, "cache entry was loaded more than once");
        Check(g_logCount.load() == g_nameReads.size(), "cache publication/logging repeated");
        Check(&Specials(0) == specialsAddress && borrowedSpecials == std::vector<int>({250,300}),
            "borrowed vector reference changed across cache insertions");
        Check(GetClass(kCharacters,250) == Cls::Special && IsAttack(kCharacters,250),
            "alternate nonattack erased primary special");
        Check(GetClass(kCharacters,413) == Cls::Projectile && IsAttack(kCharacters,413),
            "alternate projectile metadata was not merged");
        Check(GetClass(kCharacters+1,250) == Cls::Unknown && !IsAttack(kCharacters+1,250) &&
            Specials(kCharacters+1).empty() && EntityPatterns(kCharacters+1).empty() &&
            AttackEntityPatterns(kCharacters+1).empty(), "invalid fixture published partial data");

        std::cout << "Production move-data cache concurrent loading and queries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
