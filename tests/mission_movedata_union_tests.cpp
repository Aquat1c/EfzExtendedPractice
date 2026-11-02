#include "game/mission/mission_movedata.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

using Mission::MoveData::Cls;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

Cls ParseClass(const std::string& value) {
    if (value == "system") return Cls::System;
    if (value == "A") return Cls::A;
    if (value == "B") return Cls::B;
    if (value == "C") return Cls::C;
    if (value == "command") return Cls::Command;
    if (value == "special") return Cls::Special;
    if (value == "super") return Cls::Super;
    if (value == "projectile") return Cls::Projectile;
    if (value == "super_entity") return Cls::SuperEntity;
    if (value == "entity") return Cls::Entity;
    return Cls::Unknown;
}

struct UnionMetadata {
    std::unordered_map<int, Cls> classes;
    std::unordered_map<int, bool> attacks;

    void MergeFile(const char* path) {
        std::ifstream in(path, std::ios::binary);
        Check(in.is_open(), "alternate movedata fixture opens");
        nlohmann::json doc;
        in >> doc;
        for (auto it = doc.at("moves").begin(); it != doc.at("moves").end(); ++it) {
            const int id = std::stoi(it.key());
            const Cls incoming = ParseClass(it.value().value("class", ""));
            const bool incomingAttack = it.value().value("attack", false);
            auto c = classes.find(id);
            const Cls mergedClass = c == classes.end() ? incoming
                : Mission::MoveData::Detail::MergeProfileClass(c->second, incoming, id);
            classes[id] = mergedClass;
            auto a = attacks.find(id);
            const bool mergedAttack = Mission::MoveData::Detail::MergeProfileAttack(
                a != attacks.end() && a->second, incomingAttack);
            attacks[id] = mergedAttack;
        }
    }
};

}

int main(int argc, char** argv) {
    Check(argc == 3, "nanase and nanase2 fixture paths are supplied");
    UnionMetadata metadata;
    metadata.MergeFile(argv[1]);
    metadata.MergeFile(argv[2]);

    for (int id : {259, 260, 263, 264}) {
        Check(metadata.classes[id] == Cls::Special,
              "base special class survives a non-attacking alternate entry");
        Check(metadata.attacks[id],
              "base attack capability survives a non-attacking alternate entry");
    }
    for (int id : {301, 302}) {
        Check(metadata.classes[id] == Cls::Super,
              "base super class survives an alternate system entry");
        Check(metadata.attacks[id],
              "base super attack capability survives the alternate entry");
    }
    Check(metadata.classes[413] == Cls::Projectile && metadata.attacks[413],
          "alternate thrown-Shinai projectile augments the base inventory");
    Check(metadata.classes[414] == Cls::Projectile && metadata.attacks[414],
          "alternate projectile augments the base inventory");
    Check(metadata.classes[417] == Cls::Projectile && metadata.attacks[417],
          "base-only projectile remains in the union");

    std::cout << "mission_movedata_union_tests passed\n";
    return 0;
}
