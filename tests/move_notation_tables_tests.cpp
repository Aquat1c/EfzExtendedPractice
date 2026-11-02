#include "game/mission/move_notation_tables.h"
#include "game/mission/entity_notation_tables.h"
#include "game/mission/mission_sequence_policy.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "move_notation_tables_tests: " << message << '\n';
        std::exit(1);
    }
}

void RequireNotation(const char* character, short moveId, const char* expected,
                     const char* message) {
    const char* actual = Mission::MoveNames::Lookup(character, moveId);
    Require(actual != nullptr && std::strcmp(actual, expected) == 0, message);
}

void RequireInputFollowups(const char* character,
                           std::initializer_list<short> moveIds) {
    for (short moveId : moveIds) {
        Require(Mission::MoveNames::IsInputFollowup(character, moveId) &&
                    !Mission::MoveNames::IsAutomaticPhase(character, moveId),
                "audited player follow-up role drifted");
    }
}

void RequireAutomaticAliases(const char* character,
                             std::initializer_list<short> moveIds) {
    for (short moveId : moveIds) {
        Require(Mission::MoveNames::IsAutomaticPhase(character, moveId) &&
                    !Mission::MoveNames::IsInputFollowup(character, moveId),
                "audited automatic/internal phase role drifted");
    }
}

template <std::size_t N>
void ValidateTable(int index, const char* expectedName,
                   const Mission::MoveNames::MoveNote (&expectedEntries)[N]) {
    using namespace Mission::MoveNames;
    Require(index >= 0 && index < kCharTableCount, "table index out of range");

    const CharTable& table = kCharTables[index];
    Require(std::strcmp(table.name, expectedName) == 0,
            "character table order/name drifted");
    Require(table.entries == expectedEntries, "character table points at wrong rows");
    Require(table.count == static_cast<int>(N), "declared row count drifted");
    Require(N > 0, "empty character table");

    for (std::size_t i = 0; i < N; ++i) {
        const MoveNote& row = expectedEntries[i];
        Require(row.note != nullptr && row.note[0] != '\0',
                "empty notation string");
        const std::uint8_t roleBits = static_cast<std::uint8_t>(row.role);
        Require(roleBits != 0 && (roleBits & ~std::uint8_t{0x0F}) == 0,
                "invalid move-role flags");
        Require((row.stage == 0) == (row.route == nullptr),
                "route and stage must either both be present or both be absent");
        if (row.route != nullptr) {
            Require(row.route[0] != '\0', "empty route-family name");
        }
        Require(!(HasRole(row.role, MoveRole::InputFollowup) &&
                  (HasRole(row.role, MoveRole::AutomaticPhase) ||
                   HasRole(row.role, MoveRole::InternalPhase))),
                "player-input and automatic/internal roles overlap");
        if (i > 0) {
            Require(expectedEntries[i - 1].id < row.id,
                    "move IDs must be strictly increasing and unique");
        }
        const char* resolved = Lookup(expectedName, row.id);
        Require(resolved != nullptr && std::strcmp(resolved, row.note) == 0,
                "lookup disagrees with the authoritative row");
    }
}

}

int main() {
    using namespace Mission::MoveNames;

    Require(kCharTableCount == 23, "playable character table count drifted");
    ValidateTable(0, "akane", kAkane);
    ValidateTable(1, "akiko", kAkiko);
    ValidateTable(2, "ayu", kAyu);
    ValidateTable(3, "exnanase", kExnanase);
    ValidateTable(4, "ikumi", kIkumi);
    ValidateTable(5, "kanna", kKanna);
    ValidateTable(6, "kano", kKano);
    ValidateTable(7, "kaori", kKaori);
    ValidateTable(8, "mai", kMai);
    ValidateTable(9, "makoto", kMakoto);
    ValidateTable(10, "mayu", kMayu);
    ValidateTable(11, "minagi", kMinagi);
    ValidateTable(12, "mio", kMio);
    ValidateTable(13, "misaki", kMisaki);
    ValidateTable(14, "mishio", kMishio);
    ValidateTable(15, "misuzu", kMisuzu);
    ValidateTable(16, "mizukab", kMizukab);
    ValidateTable(17, "nagamori", kNagamori);
    ValidateTable(18, "nanase", kNanase);
    ValidateTable(19, "nayuki", kNayuki);
    ValidateTable(20, "nayukib", kNayukib);
    ValidateTable(21, "sayuri", kSayuri);
    ValidateTable(22, "shiori", kShiori);

    RequireInputFollowups("akane", {262, 263, 264, 265, 266, 267});
    RequireInputFollowups("akiko", {254, 255, 258, 259});
    RequireInputFollowups("exnanase",
                          {251, 255, 300, 302, 306, 307, 318, 319});
    RequireInputFollowups("kano", {251});
    RequireInputFollowups("kaori", {259, 260, 261, 263});
    RequireInputFollowups("mayu",
                          {256, 262, 263, 269, 270, 271,
                           285, 286, 287, 290});
    RequireInputFollowups("mio",
                          {256, 272, 273, 316, 317, 318,
                           319, 320, 321, 322, 323, 324});
    RequireInputFollowups("mishio",
                          {262, 263, 264, 265, 266,
                           306, 307, 308, 309, 310});
    RequireInputFollowups("nanase", {252, 262, 306});
    RequireInputFollowups("nayuki",
                          {250, 251, 260, 269, 271, 274,
                           275, 276, 277, 278, 280, 281});

    RequireAutomaticAliases("akane", {221, 241, 268, 269, 270});
    RequireAutomaticAliases("akiko",
                            {221, 241, 264, 266, 268, 304, 306, 308, 319});
    RequireAutomaticAliases("ayu", {221, 241, 259, 263});
    RequireAutomaticAliases("exnanase",
                            {221, 249, 254, 256, 257, 258, 259,
                             301, 310, 312, 314});
    RequireAutomaticAliases("ikumi",
                            {221, 249, 259, 264, 274, 278,
                             300, 304, 306, 308, 322});
    RequireAutomaticAliases("kanna", {221, 259, 310});
    RequireAutomaticAliases("kano", {221, 249, 316});
    RequireAutomaticAliases("kaori",
                            {221, 249, 250, 251, 301, 303,
                             305, 307, 309, 311, 316});
    RequireAutomaticAliases("mai",
                            {221, 241, 253, 261, 262, 306, 307, 308, 325});
    RequireAutomaticAliases("makoto", {221, 249, 310});
    RequireAutomaticAliases("mayu",
                            {221, 233, 249, 253, 254, 255, 257, 258,
                             264, 265, 275, 284, 288, 291,
                             304, 305, 306, 307, 311, 312, 313,
                             318, 320, 322});
    RequireAutomaticAliases("minagi", {221, 249});
    RequireAutomaticAliases("mio", {221, 249, 331});
    RequireAutomaticAliases("misaki",
                            {221, 249, 253, 259, 262, 309, 310, 311, 312});
    RequireAutomaticAliases("mishio", {221, 249, 267, 282, 283, 311});
    RequireAutomaticAliases("misuzu", {221, 249, 303});
    RequireAutomaticAliases("mizukab", {221, 249, 306, 307, 308, 315});
    RequireAutomaticAliases("nagamori", {221, 249, 263, 303, 304, 305});
    RequireAutomaticAliases("nanase", {221, 249, 251});
    RequireAutomaticAliases("nayuki",
                            {212, 221, 249, 253, 254, 256, 257, 258, 272, 273});
    RequireAutomaticAliases("nayukib", {221, 249});
    RequireAutomaticAliases("sayuri",
                            {221, 249, 258, 268, 269, 299, 310, 312, 314, 316});
    RequireAutomaticAliases("shiori", {221, 249});

    RequireNotation("akiko", 263, "41236A", "Akiko vacuum starter drifted");
    RequireNotation("akiko", 264, "41236A hit (automatic second phase)",
                    "Akiko vacuum hit phase drifted");
    Require(IsAutomaticPhase("akiko", 264) &&
                !IsInputFollowup("akiko", 264),
            "Akiko vacuum hit lost its automatic-phase classification");
    RequireNotation("kano", 210, "6C", "Kano 6C mapping drifted");
    RequireNotation("kano", 212, "3C", "Kano 3C mapping drifted");
    RequireNotation("kano", 213, "j.2A", "Kano j.2A mapping drifted");
    RequireNotation("kano", 214, "j.2B", "Kano j.2B mapping drifted");
    RequireNotation("kano", 215, "j.2C", "Kano j.2C mapping drifted");
    Require(Lookup("kano", 216) == nullptr,
            "unused Kano action 216 acquired invented notation");
    RequireNotation("kano", 217, "6B", "Kano 6B mapping drifted");
    Require(Mission::SequencePolicy::ExpectedAttackMaskForAction(
                Lookup("kano", 210), 210) ==
                Mission::SequencePolicy::kAttackButtonC &&
            Mission::SequencePolicy::ExpectedAttackMaskForAction(
                Lookup("kano", 212), 212) ==
                Mission::SequencePolicy::kAttackButtonC &&
            Mission::SequencePolicy::ExpectedAttackMaskForAction(
                Lookup("kano", 213), 213) ==
                Mission::SequencePolicy::kAttackButtonA &&
            Mission::SequencePolicy::ExpectedAttackMaskForAction(
                Lookup("kano", 214), 214) ==
                Mission::SequencePolicy::kAttackButtonB &&
            Mission::SequencePolicy::ExpectedAttackMaskForAction(
                Lookup("kano", 215), 215) ==
                Mission::SequencePolicy::kAttackButtonC &&
            Mission::SequencePolicy::ExpectedAttackMaskForAction(
                Lookup("kano", 217), 217) ==
                Mission::SequencePolicy::kAttackButtonB,
            "Kano unique normals lost their causal attack strengths");
    Require(!IsAutomaticPhase("kano", 210) &&
                !IsAutomaticPhase("kano", 212) &&
                !IsAutomaticPhase("kano", 213) &&
                !IsAutomaticPhase("kano", 214) &&
                !IsAutomaticPhase("kano", 215) &&
                !IsAutomaticPhase("kano", 217) &&
                !IsInputFollowup("kano", 210) &&
                !IsInputFollowup("kano", 212) &&
                !IsInputFollowup("kano", 213) &&
                !IsInputFollowup("kano", 214) &&
                !IsInputFollowup("kano", 215) &&
                !IsInputFollowup("kano", 217),
            "Kano unique normals became automatic follow-ups");
    RequireNotation("nanase", 250, "41236A", "Rumi command throw drifted");
    Require(IsInputFollowup("nanase", 252) &&
                !IsAutomaticPhase("nanase", 252),
            "Rumi player-entered throw follow-up lost its input role");
    RequireNotation("exnanase", 257, "41236* automatic/default ender",
                    "Doppel default command-throw ender drifted");
    Require(IsAutomaticPhase("exnanase", 254) &&
                IsAutomaticPhase("exnanase", 257) &&
                !IsInputFollowup("exnanase", 257),
            "Doppel success/default phases are no longer automatic");
    for (short inputFollowup : {short{251}, short{255}, short{300},
                                short{302}, short{306}, short{307},
                                short{318}, short{319}}) {
        Require(IsInputFollowup("exnanase", inputFollowup) &&
                    !IsAutomaticPhase("exnanase", inputFollowup),
                "Doppel selectable route branch lost its input role");
    }
    RequireNotation("shiori", 257, "j.412B",
                    "Shiori action 257 mapping drifted");
    Require(!IsInputFollowup("shiori", 257) &&
                !IsAutomaticPhase("shiori", 257),
            "Shiori j.412B was confused with Doppel's action 257");
    Require(IsInputFollowup("mio", 316) &&
                IsInputFollowup("mio", 324) &&
                !IsAutomaticPhase("mio", 316),
            "Mio short-FM input sequence became automatic");
    Require(IsInputFollowup("mishio", 262) &&
                IsInputFollowup("mishio", 310) &&
                IsAutomaticPhase("mishio", 267) &&
                IsAutomaticPhase("mishio", 311),
            "Mishio input continuations and recovery phases were conflated");
    RequireNotation("nayuki", 264, "41236A (requires jam)",
                    "sleepy Neyuki jam special leaked to Awake Nayuki");
    RequireNotation("nayuki", 268, "214A/B/C (sleep stance)",
                    "sleepy Neyuki jam stance mapping drifted");
    RequireNotation("nayukib", 256, "412A / j.412A",
                    "Awake Nayuki 412 mapping drifted");
    RequireNotation("nayukib", 262, "j.214A",
                    "Awake Nayuki air 214 mapping drifted");
    RequireNotation("nayukib", 300, "j.214214A",
                    "Awake Nayuki air super mapping drifted");
    Require(Lookup("nayuki", 307) == nullptr,
            "Awake Nayuki super leaked into sleepy Neyuki");
    Require(Lookup("nayukib", 268) == nullptr,
            "sleepy Neyuki jam action leaked into Awake Nayuki");

    Require(Lookup(nullptr, 200) == nullptr, "null character name resolved");
    Require(!IsInputFollowup(nullptr, 200) &&
                !IsAutomaticPhase(nullptr, 200),
            "null character name resolved a continuation role");
    Require(Lookup("mizuka", 200) == nullptr,
            "unsupported boss table unexpectedly resolved");
    Require(Lookup("akane", -1) == nullptr, "unknown move ID resolved");

    Require(Mission::EntityNames::kCharTableCount == 23,
            "entity notation character table count drifted");
    int mappedEntityRows = 0;
    for (int tableIndex = 0;
         tableIndex < Mission::EntityNames::kCharTableCount; ++tableIndex) {
        const Mission::EntityNames::CharTable& table =
            Mission::EntityNames::kCharTables[tableIndex];
        Require(table.name != nullptr && table.entries != nullptr && table.count > 0,
                "invalid entity notation table");
        mappedEntityRows += table.count;
        for (int entryIndex = 0; entryIndex < table.count; ++entryIndex) {
            const Mission::EntityNames::EntityNote& entry =
                table.entries[entryIndex];
            Require(entry.note != nullptr && entry.note[0] != '\0',
                    "empty mapped entity description");
            if (entryIndex > 0) {
                Require(table.entries[entryIndex - 1].pattern < entry.pattern,
                        "entity patterns must be strictly increasing and unique");
            }
            const char* resolved = Mission::EntityNames::Lookup(
                table.name, entry.pattern);
            Require(resolved != nullptr && std::strcmp(resolved, entry.note) == 0,
                    "entity lookup disagrees with authoritative row");
        }
    }
    Require(mappedEntityRows == 749,
            "mapped entity rows no longer match MOVE_ID_MAP");
    RequireNotation("nagamori", 250, "623A",
                    "Nagamori DP source notation drifted");
    Require(std::strcmp(Mission::EntityNames::Lookup("nagamori", 405),
                        "shared ordinary note-explosion hit") == 0,
            "Nagamori #405 note-explosion mapping drifted");
    Require(std::strcmp(Mission::EntityNames::Lookup("nanase", 405),
                        "214C shockwave bullet") == 0,
            "character-local #405 entity mapping crossed characters");
    Require(Mission::EntityNames::Lookup("nagamori", 410) == nullptr,
            "unresolved Nagamori #410 lost its numeric fallback");
    Require(Mission::EntityNames::FormatContact("nagamori", 405, "HIT") ==
                "shared ordinary note-explosion hit",
            "mapped hit description duplicated its outcome");
    Require(Mission::EntityNames::FormatContact("nagamori", 411, "HIT") ==
                "236A bow projectile HIT",
            "mapped projectile omitted its contact outcome");
    Require(Mission::EntityNames::FormatContact("nagamori", 410, "HIT") ==
                "#410 HIT",
            "unmapped entity did not retain the raw numeric fallback");
    Require(!Mission::EntityNames::ContainsOutcomeCaseInsensitive(
                "large explosion", "RG"),
            "outcome matching accepted a substring inside a word");
    Require(Mission::EntityNames::IsRawPatternNotation("#405 HIT", 405),
            "legacy recorder notation was not recognized");
    Require(!Mission::EntityNames::IsRawPatternNotation(
                "NOTE #405 HIT", 405),
            "author wording was mistaken for generated raw notation");

    using Mission::EntityNames::LookupSemantic;
    const auto* rumiShockwaveA = LookupSemantic("nanase", 405);
    const auto* rumiShockwaveD = LookupSemantic("nanase", 408);
    const auto* noteExplosion = LookupSemantic("nagamori", 405);
    const auto* trebleExplosion = LookupSemantic("nagamori", 454);
    Require(rumiShockwaveA && rumiShockwaveD &&
                std::strcmp(rumiShockwaveA->family,
                            rumiShockwaveD->family) == 0,
            "Rumi shockwave bullets lost their logical family");
    Require(noteExplosion && trebleExplosion &&
                std::strcmp(noteExplosion->family,
                            trebleExplosion->family) != 0,
            "Nagamori note and Treble explosions were collapsed together");
    Require(std::strcmp(rumiShockwaveA->family,
                        noteExplosion->family) != 0,
            "character-local #405 families crossed characters");
    const auto* meteorStart = LookupSemantic("kano", 450);
    const auto* meteorEnd = LookupSemantic("kano", 453);
    Require(meteorStart && meteorEnd &&
                std::strcmp(meteorStart->family, meteorEnd->family) == 0,
            "Kano meteor phases lost their family");
    const auto* snowbunnyHit = LookupSemantic("nayukib", 404);
    const auto* snowbunnyController = LookupSemantic("nayukib", 409);
    Require(snowbunnyHit && snowbunnyController &&
                std::strcmp(snowbunnyHit->family,
                            snowbunnyController->family) == 0,
            "Awake Nayuki Snowbunny lanes no longer group");
    const auto* nonstandardNote = LookupSemantic("nagamori", 410);
    Require(nonstandardNote &&
                std::strcmp(nonstandardNote->family,
                            "nagamori.nonstandard_note") == 0,
            "Nagamori #410 lost its decomp-proven nonstandard-note family");
    Require(Mission::EntityNames::FormatSemanticContact(
                "nagamori", 405, "HIT") == "NOTE (HIT)",
            "generated note contact did not use its canonical family label");
    Require(Mission::EntityNames::IsGeneratedContactNotation(
                "nagamori", 405, "HIT", 2,
                "NOTE (HIT) x2"),
            "canonical generated family notation was mistaken for author copy");
    Require(Mission::EntityNames::IsGeneratedContactNotation(
                "nagamori", 405, "HIT", 1,
                "shared ordinary note-explosion hit"),
            "legacy generated mapped notation was mistaken for author copy");
    Require(Mission::EntityNames::IsGeneratedContactNotation(
                "nagamori", 405, "HIT", 1,
                "NOTE EXPLOSION HIT"),
            "prior sparse semantic notation was mistaken for author copy");
    Require(Mission::EntityNames::IsGeneratedContactNotation(
                "minagi", 405, "HIT", 1,
                "MICHIRU ATTACK HIT", 254),
            "prior generic Michiru notation cannot migrate to 236B (HIT)");
    Require(!Mission::EntityNames::IsGeneratedContactNotation(
                "nagamori", 405, "HIT", 1,
                "Echo this note after 2C"),
            "custom authored entity wording was overwritten by canonicalization");
    Require(Mission::EntityNames::kGeneratedSemanticNoteCount >
                Mission::EntityNames::kLegacySemanticNoteCount,
            "exhaustive semantic catalog was not generated");
    const auto* minagi236B = LookupSemantic("minagi", 405, 254);
    const auto* minagiRecovery = LookupSemantic("minagi", 425, 254);
    const auto* mai214214BHit = LookupSemantic("mai", 441, 321);
    const auto* mai214214BController = LookupSemantic("mai", 443, 321);
    const auto* mioLong214B = LookupSemantic("mio", 422, 268);
    Require(minagi236B && std::strcmp(minagi236B->label, "236B") == 0,
            "Minagi producer-qualified 236B label is missing");
    Require(minagiRecovery &&
                Mission::EntityNames::IsPostContactRecovery(
                    minagiRecovery->disposition) &&
                !Mission::EntityNames::LifecyclePromisesContact(
                    minagiRecovery->disposition),
            "Minagi #425 became a second contact objective");
    Require(mai214214BHit &&
                std::strcmp(mai214214BHit->label, "214214B") == 0,
            "Mai shared #441 did not resolve through its producer");
    Require(Mission::EntityNames::FormatSemanticContact(
                "mai", 441, "HIT", 321) == "214214B (HIT)",
            "Mai's contact-owning child exposed an internal summon name");
    const auto* persistedMai214214B =
        Mission::EntityNames::LookupSemanticForGeneratedContactNotation(
            "mai", 441, "HIT", 1, "214214B (HIT)");
    Require(persistedMai214214B &&
                persistedMai214214B->producerMove == 321 &&
                std::strcmp(persistedMai214214B->family,
                            "mai.destruction.b") == 0,
            "delayed Mai child lost its persisted source-button identity");
    Require(mai214214BController &&
                !Mission::EntityNames::LifecyclePromisesContact(
                    mai214214BController->disposition),
            "Mai's non-attacking 214214B controller became a trial hit");
    Require(mai214214BController &&
                !Mission::EntityNames::CanOwnRecordedContact(
                    mai214214BController->disposition) &&
                Mission::EntityNames::FormatSemanticContact(
                    "mai", 443, "HIT", 321).empty(),
            "a persisted Mai controller can still render as a hit");
    Require(Mission::EntityNames::CanOwnRecordedContact(
                Mission::EntityNames::LifecycleDisposition::Unresolved) &&
                !Mission::EntityNames::HasResolvedContactPresentation(
                    Mission::EntityNames::LifecycleDisposition::Unresolved) &&
                Mission::EntityNames::FormatSemanticContact(
                "missing", 777, "HIT") == "#777 (HIT)",
            "unresolved contact evidence did not remain visibly raw");
    const auto* dormantMizukaB = LookupSemantic("mizukab", 452);
    Require(dormantMizukaB &&
                !Mission::EntityNames::CanOwnRecordedContact(
                    dormantMizukaB->disposition) &&
                Mission::EntityNames::FormatSemanticContact(
                    "mizukab", 452, "HIT").empty(),
            "a proven dormant/orphan PAT row can still become a trial hit");
    Require(Mission::EntityNames::FormatSemanticContact(
                "minagi", 410, "HIT", 259) == "214A (HIT)",
            "Michiru's 214A contact did not use source-action notation");
    Require(Mission::EntityNames::FormatSemanticContact(
                "mizuka", 406, "HIT", 253) == "214A (HIT)" &&
                Mission::EntityNames::FormatSemanticContact(
                    "mizuka", 444, "HIT", 257) == "41236B (HIT)" &&
                Mission::EntityNames::FormatSemanticContact(
                    "mizuka", 459, "HIT", 265) == "J.214 (HIT)",
            "boss UNKNOWN contact children lost their actual source inputs");
    Require(mioLong214B &&
                std::strcmp(mioLong214B->label, "214B (L)") == 0,
            "Mio long-214 child did not resolve through its producer");
    const auto* kanoLightningController = LookupSemantic("kano", 443);
    const auto* kanoDormantSoulStrike = LookupSemantic("kano", 417);
    Require(kanoLightningController && kanoDormantSoulStrike &&
                !Mission::EntityNames::CanOwnRecordedContact(
                    kanoLightningController->disposition) &&
                kanoDormantSoulStrike->disposition == Mission::EntityNames::
                    LifecycleDisposition::DormantOrOrphan,
            "Kano controller/orphan phases can still become contact goals");
    Require(Mission::EntityNames::FormatSemanticContact(
                "nagamori", 411, "HIT") == "236A (HIT)" &&
                Mission::EntityNames::FormatSemanticContact(
                    "nayuki", 411, "HIT", 306) == "C236236 (HIT)" &&
                Mission::EntityNames::FormatSemanticContact(
                    "mishio", 429, "HIT", 318) == "B2B5CA (HIT)" &&
                Mission::EntityNames::FormatSemanticContact(
                    "mio", 456, "HIT", 273) ==
                    "236A/B~236B (S) (HIT)",
            "known source input regressed to an internal projectile label");
    const auto* unknownFm = LookupSemantic("mizukab", 421, 315);
    const auto* unknownOrb = LookupSemantic("mizukab", 420);
    Require(unknownFm && unknownOrb &&
                std::strcmp(unknownFm->family, "mizukab.fm") == 0 &&
                std::strcmp(unknownFm->label, "CB6AA") == 0 &&
                std::strcmp(unknownFm->family, unknownOrb->family) != 0,
            "playable UNKNOWN's FM contact is still grouped as a 236 orb");
    for (int semanticIndex = 0;
         semanticIndex < Mission::EntityNames::kLegacySemanticNoteCount;
         ++semanticIndex) {
        const auto& semantic =
            Mission::EntityNames::kLegacySemanticNotes[semanticIndex];
        Require(semantic.character && semantic.family && semantic.label &&
                    semantic.character[0] && semantic.family[0] &&
                    semantic.label[0],
                "invalid semantic entity row");
        Require(Mission::EntityNames::Lookup(
                    semantic.character, semantic.pattern) != nullptr,
                "semantic entity row has no raw mapped pattern");
        for (int previous = 0; previous < semanticIndex; ++previous) {
            const auto& earlier =
                Mission::EntityNames::kLegacySemanticNotes[previous];
            Require(semantic.pattern != earlier.pattern ||
                        std::strcmp(semantic.character,
                                    earlier.character) != 0,
                    "duplicate character/pattern semantic row");
        }
    }

    std::cout << "move_notation_tables_tests: ok\n";
    return 0;
}
