#include "game/mission/entity_notation_tables.h"
#include "game/mission/mission_entity_review_policy.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}

int main(int argc, char** argv) {
    Require(argc == 2, "expected entity_manifest.json path");
    std::ifstream input(argv[1], std::ios::binary);
    Require(input.good(), "could not open entity manifest");
    nlohmann::json manifest;
    input >> manifest;

    const std::set<int> common = {495, 496, 497, 498, 499};
    int resources = 0;
    int classifiedPatterns = 0;
    int attackPatterns = 0;
    for (const auto& character : manifest.at("characters")) {
        const std::string resource = character.at("resource").get<std::string>();
        ++resources;
        for (const auto& value : character.at("entityPatterns")) {
            const int pattern = value.get<int>();
            if (common.count(pattern)) continue;
            const auto* semantic = Mission::EntityNames::LookupSemantic(
                resource.c_str(), pattern);
            Require(semantic != nullptr,
                    resource + " #" + std::to_string(pattern) +
                        " has no semantic/lifecycle classification");
            Require(semantic->character && semantic->family && semantic->label &&
                        semantic->character[0] && semantic->family[0] &&
                        semantic->label[0],
                    resource + " has an invalid semantic row");
            ++classifiedPatterns;
        }
        for (const auto& value : character.at("attackEntityPatterns")) {
            const int pattern = value.get<int>();
            if (common.count(pattern)) continue;
            Require(Mission::EntityNames::LookupSemantic(
                        resource.c_str(), pattern) != nullptr,
                    resource + " attack #" + std::to_string(pattern) +
                        " fell back to a raw number");
            ++attackPatterns;
        }
    }
    Require(resources == 26, "runtime resource coverage drifted");
    Require(classifiedPatterns > attackPatterns && attackPatterns > 400,
            "catalog does not cover the complete PAT lifecycle inventory");

    std::set<std::tuple<std::string, int, int>> unique;
    int unresolvedRows = 0;
    int anonymousNonattackRows = 0;
    std::set<std::pair<std::string, int>> persistentWildcards;
    std::set<std::pair<std::string, int>> persistentContexts;
    for (int i = 0; i < Mission::EntityNames::kGeneratedSemanticNoteCount; ++i) {
        const auto& row = Mission::EntityNames::kGeneratedSemanticNotes[i];
        Require(unique.emplace(row.character, static_cast<int>(row.pattern),
                               row.producerMove).second,
                "duplicate generated character/pattern/producer key");
        if (row.disposition ==
            Mission::EntityNames::LifecycleDisposition::ContactEffect) {
            const std::string label(row.label);
            Require(label.find("UNRESOLVED") == std::string::npos &&
                        label.find("CONTROLLER") == std::string::npos &&
                        label.find("HELPER") == std::string::npos &&
                        label.find("VFX") == std::string::npos &&
                        label.find("DESTRUCTION") == std::string::npos &&
                        label.find("BOW") == std::string::npos &&
                        label.find("NEEDLE") == std::string::npos &&
                        label.find("SEQUENCE") == std::string::npos &&
                        label.find("LONG") == std::string::npos &&
                        label.find("SHORT") == std::string::npos &&
                        label != "FM",
                    std::string(row.character) + " #" +
                        std::to_string(row.pattern) +
                        " exposes an internal lifecycle name as a hit");
        } else if (row.disposition ==
                       Mission::EntityNames::LifecycleDisposition::Unresolved) {
            ++unresolvedRows;
        } else if (row.disposition ==
                       Mission::EntityNames::LifecycleDisposition::Controller ||
                   row.disposition == Mission::EntityNames::
                       LifecycleDisposition::PostContactRecovery ||
                   row.disposition ==
                       Mission::EntityNames::LifecycleDisposition::VisualEffect ||
                   row.disposition == Mission::EntityNames::
                       LifecycleDisposition::DormantOrOrphan) {
            Require(!Mission::EntityNames::LifecyclePromisesContact(
                        row.disposition),
                    std::string(row.character) + " #" +
                        std::to_string(row.pattern) +
                        " non-attacking phase promises a visible contact");
        }
        const std::string anonymousPrefix =
            std::string(row.character) + ".entity_";
        if (row.producerMove < 0 &&
            std::strncmp(row.family, anonymousPrefix.c_str(),
                         anonymousPrefix.size()) == 0) {
            Require(row.disposition ==
                        Mission::EntityNames::LifecycleDisposition::Controller,
                    "anonymous fallback is not a non-attacking controller");
            ++anonymousNonattackRows;
        }
        if (row.role != Mission::EntityNames::PresentationRole::InlineProjectile &&
            row.disposition ==
                Mission::EntityNames::LifecycleDisposition::ContactEffect) {
            const auto key = std::make_pair(std::string(row.character),
                                            static_cast<int>(row.pattern));
            if (row.producerMove < 0) {
                persistentWildcards.insert(key);
            } else {
                persistentContexts.insert(key);
            }
        }
        if (row.disposition ==
            Mission::EntityNames::LifecycleDisposition::DormantOrOrphan) {
            Require(std::string(row.label).find("UNRESOLVED") ==
                        std::string::npos,
                    std::string(row.character) + " #" +
                        std::to_string(row.pattern) +
                        " dormant row still looks unresolved");
        }
        if (row.producerMove >= 0) {
            Require(Mission::EntityNames::LookupSemantic(
                        row.character, row.pattern) != nullptr,
                    "contextual semantic row has no wildcard fallback");
            Require(Mission::EntityNames::LookupSemantic(
                        row.character, row.pattern, row.producerMove) == &row,
                    "contextual lookup did not prefer its producer-qualified row");
        }
    }
    Require(unresolvedRows == 0,
            "reachable attack-capable entity rows remain unresolved");
    Require(anonymousNonattackRows == 126,
            "audited anonymous non-attacking PAT inventory drifted");
    const std::set<std::pair<std::string, int>> commandOriginExceptions = {
        {"mai", 414}, {"mai", 431}, {"mai", 451}, {"mai", 454}};
    for (const auto& key : persistentWildcards) {
        Require(persistentContexts.count(key) != 0 ||
                    commandOriginExceptions.count(key) != 0,
                key.first + " #" + std::to_string(key.second) +
                    " persistent contact has no exact setter mapping");
    }

    const auto* minagiHit = Mission::EntityNames::LookupSemantic(
        "minagi", 405, 254);
    const auto* minagiBounce = Mission::EntityNames::LookupSemantic(
        "minagi", 425, 254);
    Require(minagiHit && minagiBounce &&
                std::strcmp(minagiHit->family, minagiBounce->family) == 0,
            "Minagi hit/recovery lifecycle family drifted");
    Require(Mission::EntityNames::LifecyclePromisesContact(
                minagiHit->disposition) &&
                !Mission::EntityNames::LifecyclePromisesContact(
                    minagiBounce->disposition),
            "Minagi #425 is still treated as a second hit");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "minagi", 405, 266)->label,
                        "j.236B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "minagi", 408, 270)->label,
                            "j.421B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "minagi", 411, 273)->label,
                            "j.214B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "minagi", 419, 310)->label,
                            "j.214214B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "minagi", 455, 307)->label,
                            "j.236236B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "minagi", 458, 320)->label,
                            "j.236236B") == 0,
            "Minagi's airborne Michiru commands lost their j. notation");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "mai", 441, 321)->label,
                        "214214B") == 0,
            "Mai shared child is not producer-qualified");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "mai", 431)->label,
                        "(J.)236S") == 0,
            "Mai's command-driven bound summon lost its real input label");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "mio", 422, 268)->label,
                        "214B (L)") == 0,
            "Mio shared long-214 child is not producer-qualified");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "mio", 407)->family,
                        "mio.236a_followup_short") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "mio", 456, 273)->family,
                            "mio.236_followup_short") == 0,
            "Mio's two Short 236 follow-up branches were conflated");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "kanna", 406, 266)->label,
                        "41236A/B") == 0,
            "Kanna's shared A/B hawk was falsely narrowed to A");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "kano", 400, 256)->label,
                        "236C") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "kano", 403, 258)->label,
                            "214B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "kano", 439, 262)->label,
                            "421C") == 0 &&
                Mission::EntityNames::LookupSemantic(
                    "kano", 439, 262)->role ==
                    Mission::EntityNames::PresentationRole::Setplay,
            "Kano's delayed bolts lost their exact setter identity");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "sayuri", 401, 252)->label,
                        "236C") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "shiori", 401, 251)->label,
                            "236B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "shiori", 402, 257)->label,
                            "j.412B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "shiori", 408, 263)->label,
                            "j.236B") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "ayu", 400, 252)->label,
                            "623B") == 0,
            "shared ordinary projectiles lost their exact button variants");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "nagamori", 405)->label,
                        "NOTE") == 0,
            "Nagamori delayed note contact label drifted");
    Require(!Mission::EntityNames::
                HasProducerQualifiedPersistentContactVariant(
                    "akane", 400) &&
                !Mission::EntityNames::
                    HasProducerQualifiedPersistentContactVariant(
                        "shiori", 435) &&
                Mission::EntityNames::
                    HasProducerQualifiedPersistentContactVariant(
                        "nagamori", 405),
            "ordinary sampled projectiles and persistent contextual patterns "
            "were not separated");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "nagamori", 405, 275)->label,
                        "NOTE") == 0 &&
                Mission::EntityNames::LookupSemantic(
                    "nagamori", 405, 206)->producerMove < 0 &&
                Mission::EntityNames::LookupSemantic(
                    "nagamori", 405, 315)->producerMove < 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "nagamori", 454, 306)->label,
                            "TREBLE") == 0,
            "Nagamori note setter was replaced by its later trigger");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "nayukib", 404, 306)->label,
                        "641236A") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "nayukib", 405, 308)->label,
                            "641236C") == 0,
            "Awake Nayuki's Snowbunny lanes lost their cast origins");
    Require(Mission::EntityNames::LookupSemantic(
                "nanase", 413, 258)->disposition ==
                Mission::EntityNames::LifecycleDisposition::Controller &&
                Mission::EntityNames::LookupSemantic(
                    "nanase2", 413, 258)->disposition ==
                    Mission::EntityNames::LifecycleDisposition::ContactEffect,
            "Rumi's variant-specific #413 attack capability was flattened");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "makoto", 409, 253)->label,
                        "214A") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "makoto", 409, 262)->label,
                            "J.214A") == 0 &&
                Mission::EntityNames::LookupSemantic(
                    "makoto", 409, 253)->role ==
                    Mission::EntityNames::PresentationRole::Trap,
            "Makoto's delayed 214 smoke lost its setter identity");
    Require(Mission::EntityNames::LookupSemantic(
                "exnanase", 404)->disposition ==
                Mission::EntityNames::LifecycleDisposition::VisualEffect &&
                Mission::EntityNames::LookupSemantic(
                    "ikumi", 470)->disposition ==
                    Mission::EntityNames::LifecycleDisposition::Controller &&
                Mission::EntityNames::LookupSemantic(
                    "kano", 402)->disposition ==
                    Mission::EntityNames::LifecycleDisposition::DormantOrOrphan,
            "documented non-attacking lifecycle rows regressed to fallbacks");
    Require(std::strcmp(Mission::EntityNames::LookupSemantic(
                            "minagi", 413)->label,
                        "LEGACY/INTERNAL MICHIRU ATTACK") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "mai", 414)->label,
                            "(J.)22S") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "mai", 431)->label,
                            "(J.)236S") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "mai", 451)->label,
                            "(J.)214S") == 0 &&
                std::strcmp(Mission::EntityNames::LookupSemantic(
                                "mai", 454)->label,
                            "(J.)412S") == 0 &&
                Mission::EntityNames::LookupSemantic(
                    "mai", 431)->producerMove < 0,
            "PAT-only/internal or command-origin exception semantics drifted");
    const auto* shioriAirFan = Mission::EntityNames::LookupSemantic(
        "shiori", 435, 312);
    const auto* shioriGroundFan = Mission::EntityNames::LookupSemantic(
        "shiori", 435, 317);
    const auto* shioriController = Mission::EntityNames::LookupSemantic(
        "shiori", 436, 312);
    Require(shioriAirFan && shioriGroundFan &&
                std::strcmp(shioriAirFan->label, "j.2141236A") == 0 &&
                std::strcmp(shioriGroundFan->label, "2141236C") == 0,
            "Shiori fan contact is not bound to its exact cast notation");
    Require(Mission::EntityNames::FormatSemanticContact(
                "shiori", 435, "HIT", 312) == "j.2141236A (HIT)" &&
                Mission::EntityNames::
                    LookupSemanticForGeneratedContactNotation(
                        "shiori", 435, "HIT", 1,
                        "2141236 (HIT)") != nullptr,
            "Shiori fan cannot upgrade an existing generic recording");
    Require(shioriController && shioriController->producerMove < 0 &&
                shioriController->disposition == Mission::EntityNames::
                    LifecycleDisposition::Controller &&
                !Mission::EntityNames::CanOwnRecordedContact(
                    shioriController->disposition),
            "Shiori #436 controller became a visible contact alias");
    const auto* bossPlush = Mission::EntityNames::LookupSemantic(
        "mizuka", 406, 253);
    const auto* bossOrphan = Mission::EntityNames::LookupSemantic(
        "mizuka", 407);
    const auto* playablePlush = Mission::EntityNames::LookupSemantic(
        "mizukab", 406);
    Require(bossPlush && playablePlush &&
                std::strcmp(bossPlush->label, "214A") == 0 &&
                std::strcmp(bossPlush->character,
                            playablePlush->character) != 0,
            "boss Mizuka was aliased to playable UNKNOWN");
    Require(bossOrphan &&
                bossOrphan->disposition == Mission::EntityNames::
                    LifecycleDisposition::DormantOrOrphan &&
                !Mission::EntityNames::CanOwnRecordedContact(
                    bossOrphan->disposition),
            "boss Mizuka's unreachable #407 can still become a hit");

    Mission::Mission legacyMinagi;
    legacyMinagi.player.character = "minagi";
    legacyMinagi.steps.resize(3);
    legacyMinagi.steps[2].moveIds = {254};
    Mission::EntityContactRequirement realHit;
    realHit.patterns = {405};
    realHit.slot = 0;
    realHit.generation = 1;
    realHit.opensAfterAction = 2;
    legacyMinagi.entityContacts.push_back(realHit);
    const std::string staleReview =
        "attack entity setup #425 (slot 0, generation 1) had no gradeable "
        "contact; author a lifecycle objective or retake it";
    Require(Mission::EntityReviewPolicy::IsObsoletePostContactReview(
                legacyMinagi, staleReview),
            "the saved #405 HIT -> #425 recovery recording stays unavailable");
    legacyMinagi.entityContacts[0].generation = 2;
    Require(!Mission::EntityReviewPolicy::IsObsoletePostContactReview(
                legacyMinagi, staleReview),
            "legacy review migration crossed entity generations");
    legacyMinagi.entityContacts[0].generation = 1;
    legacyMinagi.entityContacts[0].patterns = {443};
    Require(!Mission::EntityReviewPolicy::IsObsoletePostContactReview(
                legacyMinagi, staleReview),
            "legacy review migration ignored the semantic family identity");

    std::cout << "entity_semantic_catalog_tests: " << resources
              << " resources, " << classifiedPatterns << " lifecycle patterns, "
              << attackPatterns << " attack-capable patterns\n";
    return 0;
}
