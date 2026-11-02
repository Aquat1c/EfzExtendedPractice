#include "game/character_action_catalog.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << "character_action_catalog_tests: " << message << '\n';
        std::exit(1);
    }
}

void TestMayuRecipes() {
    using namespace CharacterActionCatalog;
    Require(IsAvailable(CHAR_ID_MAYU, ACTION_6B, 1), "Mayu 6B missing");
    Require(IsAvailable(CHAR_ID_MAYU, ACTION_1X, 1), "Mayu 1B missing");
    Require(DashNormalMask(CHAR_ID_MAYU, ACTION_664X) == kABC,
            "Mayu 664A-C missing");
    Require(DashNormalMask(CHAR_ID_MAYU, ACTION_66X) == 0,
            "fabricated Mayu 66X advertised");
    Require(DashNormalMask(CHAR_ID_MAYU, ACTION_662X) == 0,
            "fabricated Mayu 662X advertised");
}

void TestKanoRecipes() {
    using namespace CharacterActionCatalog;
    Require(IsAvailable(CHAR_ID_KANO, ACTION_6B, 1), "Kano 6B missing");
    Require(IsAvailable(CHAR_ID_KANO, ACTION_6C, 2), "Kano 6C missing");
    Require(IsAvailable(CHAR_ID_KANO, ACTION_3X, 2), "Kano 3C missing");
    Require(StrengthMask(CHAR_ID_KANO, ACTION_J2X) == kABC,
            "Kano j.2A-C mask drifted");
    Require(StrengthMask(CHAR_ID_KANO, ACTION_QCF) == kABCD,
            "Kano 236S missing");
    Require(DashNormalMask(CHAR_ID_KANO, ACTION_66X) == kABC &&
            DashNormalMask(CHAR_ID_KANO, ACTION_662X) == kABC,
            "Kano standard dash normals missing");
}

void TestCharacterLocalExceptions() {
    using namespace CharacterActionCatalog;
    Require(DashNormalMask(CHAR_ID_KAORI, ACTION_66X) == kABC,
            "Kaori 66A-C missing");
    Require(DashNormalMask(CHAR_ID_KAORI, ACTION_662X) == kC,
            "Kaori must expose only 662C");
    Require(IsAvailable(CHAR_ID_KAORI, ACTION_KAORI_RECOIL_DUCK, 0),
            "Kaori 44~66 Recoil Ducking recipe missing");
    Require(!IsAvailable(CHAR_ID_AKIKO, ACTION_KAORI_RECOIL_DUCK, 0),
            "Kaori-only 44~66 recipe leaked to another character");
    Require(!IsAvailable(-1, ACTION_KAORI_RECOIL_DUCK, 0) &&
            !IsAvailable(CHAR_ID_KANO + 1, ACTION_KAORI_RECOIL_DUCK, 0),
            "Kaori-only 44~66 recipe leaked through the unknown-character fallback");
    Require(DashNormalMask(CHAR_ID_AKIKO, ACTION_66X) == 0,
            "Akiko was given fabricated dash normals");
    Require(StrengthMask(CHAR_ID_SAYURI, ACTION_DP) == kABC,
            "Sayuri 623 strength mask drifted");
    Require(StrengthMask(CHAR_ID_IKUMI, ACTION_41236) == kC,
            "Ikumi must expose only 41236C");
    Require(SpecialMask(CHAR_ID_AKIKO, ACTION_41236) == kABC &&
            SuperMask(CHAR_ID_AKIKO, ACTION_41236) == 0,
            "41236 drifted out of the ordinary-special inventory");
}

void TestCommandNormalsStayCharacterLocal() {
    using namespace CharacterActionCatalog;
    Require(IsAvailable(CHAR_ID_EXNANASE, ACTION_6C, 2),
            "Doppel 6C missing");
    Require(!IsAvailable(CHAR_ID_EXNANASE, ACTION_6B, 1),
            "Doppel was given a fabricated 6B");
    Require(IsAvailable(CHAR_ID_AKANE, ACTION_J6X, 2),
            "Akane j.6C missing");
    Require(!IsAvailable(CHAR_ID_AKANE, ACTION_6C, 2),
            "Akane was given a fabricated grounded 6C");
    Require(IsAvailable(CHAR_ID_MAKOTO, ACTION_J2X, 2),
            "Makoto j.2C missing");
    Require(!IsAvailable(CHAR_ID_MAKOTO, ACTION_J2X, 0),
            "Makoto was given a fabricated j.2A");
    Require(IsAvailable(CHAR_ID_MIO, ACTION_4B, 1) &&
            IsAvailable(CHAR_ID_MIO, ACTION_J2X, 1) &&
            IsAvailable(CHAR_ID_MIO, ACTION_J2X, 2),
            "Mio's stance-union command normals are incomplete");
    Require(!IsAvailable(CHAR_ID_MIO, ACTION_4C, 2),
            "Mio was given a fabricated 4C");
}

void TestSButtonNormalsAreNotUniversal() {
    using namespace CharacterActionCatalog;
    Require(!IsAvailable(CHAR_ID_AKIKO, ACTION_5D, 3),
            "Akiko was given a fabricated 5S");
    Require(IsAvailable(CHAR_ID_KANO, ACTION_5D, 3), "Kano 5S missing");
    Require(!IsAvailable(CHAR_ID_MAI, ACTION_2D, 3),
            "Mai was given a fabricated 2S");
    Require(IsAvailable(CHAR_ID_AYU, ACTION_JD, 3), "Ayu j.S missing");
    Require(!IsAvailable(CHAR_ID_KANO, ACTION_2D, 3),
            "Kano was given a fabricated 2S");
    Require(!IsAvailable(CHAR_ID_MIO, ACTION_2D, 3),
            "Mio was given a fabricated 2S");
    Require(!IsAvailable(CHAR_ID_MINAGI, ACTION_2D, 3),
            "Minagi was given a fabricated 2S");
    Require(IsAvailable(CHAR_ID_NAYUKI, ACTION_2D, 3),
            "sleepy Nayuki 2S missing");
    Require(StrengthMask(CHAR_ID_NAYUKI, ACTION_QCF) == kABCD,
            "sleepy Nayuki 236S missing from its four-button special");
    Require(IsAvailable(CHAR_ID_MAI, ACTION_4D, 3) &&
            IsAvailable(CHAR_ID_MAI, ACTION_6D, 3),
            "Mai 4S/6S summon placements missing");
    Require(IsAvailable(CHAR_ID_MINAGI, ACTION_4D, 3) &&
            IsAvailable(CHAR_ID_MINAGI, ACTION_6D, 3),
            "Minagi 4S/6S command normals missing");
    Require(IsAvailable(CHAR_ID_NANASE, ACTION_5D, 3), "Rumi 5S missing");
    Require(!IsAvailable(CHAR_ID_MIZUKA, ACTION_5D, 3) &&
            !IsAvailable(CHAR_ID_MIZUKA, ACTION_2D, 3) &&
            !IsAvailable(CHAR_ID_MIZUKA, ACTION_JD, 3),
            "Mizuka Nagamori was given a fabricated S normal");
}

void TestMizukaNagamoriRecipes() {
    using namespace CharacterActionCatalog;
    Require(CommandNormalMask(CHAR_ID_MIZUKA, ACTION_6B) == kB,
            "Mizuka Nagamori 6B command normal missing");
    constexpr int otherCommands[] = {
        ACTION_6A, ACTION_6C, ACTION_4A, ACTION_4B, ACTION_4C,
        ACTION_1X, ACTION_3X, ACTION_J2X, ACTION_J6X
    };
    for (int action : otherCommands) {
        Require(CommandNormalMask(CHAR_ID_MIZUKA, action) == 0,
                "Mizuka Nagamori was given a fabricated command normal");
    }
    Require(StrengthMask(CHAR_ID_MIZUKA, ACTION_QCF) == kABC &&
            StrengthMask(CHAR_ID_MIZUKA, ACTION_DP) == kABC &&
            StrengthMask(CHAR_ID_MIZUKA, ACTION_QCB) == kABC &&
            StrengthMask(CHAR_ID_MIZUKA, ACTION_412) == kABC,
            "Mizuka Nagamori special inventory gained or lost an S version");
}

void TestBossUnknownRecipes() {
    using namespace CharacterActionCatalog;
    Require(IsAvailable(CHAR_ID_UNKNOWN_BOSS, ACTION_5D, 3),
            "boss UNKNOWN 5S missing");
    Require(IsAvailable(CHAR_ID_UNKNOWN_BOSS, ACTION_JD, 3),
            "boss UNKNOWN j.S missing");
    Require(!IsAvailable(CHAR_ID_UNKNOWN_BOSS, ACTION_2D, 3),
            "boss UNKNOWN was given an unverified 2S");
    Require(!IsAvailable(CHAR_ID_UNKNOWN_BOSS, ACTION_6B, 1),
            "boss UNKNOWN was aliased to Mizuka Nagamori's 6B");
    Require(DashNormalMask(CHAR_ID_UNKNOWN_BOSS, ACTION_66X) == kABC,
            "boss UNKNOWN confirmed 66A-C missing");
    Require(DashNormalMask(CHAR_ID_UNKNOWN_BOSS, ACTION_662X) == 0,
            "boss UNKNOWN unverified 662 family advertised");
}

void TestUnknownRecipes() {
    using namespace CharacterActionCatalog;
    Require(IsAvailable(CHAR_ID_UNKNOWN, ACTION_J6X, 2),
            "UNKNOWN j.6C missing");
    Require(DashNormalMask(CHAR_ID_UNKNOWN, ACTION_66X) == kABC &&
            DashNormalMask(CHAR_ID_UNKNOWN, ACTION_662X) == kABC,
            "UNKNOWN dash-normal inventory incomplete");
    Require(StrengthMask(CHAR_ID_UNKNOWN, ACTION_421) == kABC &&
            StrengthMask(CHAR_ID_UNKNOWN, ACTION_22) == kD,
            "UNKNOWN 421/22S inventory was aliased to a Mizuka slot");
}

void TestDocumentedMotionExceptions() {
    using namespace CharacterActionCatalog;
    Require(StrengthMask(CHAR_ID_AKANE, ACTION_QCB) == kABC &&
            StrengthMask(CHAR_ID_AKANE, ACTION_412) == kABC,
            "Akane 214/412 families missing");
    Require(StrengthMask(CHAR_ID_KAORI, ACTION_DP) == kABC,
            "Kaori 623A-C mask drifted");
    Require(StrengthMask(CHAR_ID_MAI, ACTION_QCF) == kABCD &&
            StrengthMask(CHAR_ID_MAI, ACTION_22) == kD,
            "Mai S-special inventory drifted");
    Require(StrengthMask(CHAR_ID_MISHIO, ACTION_22) == (kA | kB),
            "Mishio attack 22 mask must exclude universal 22C IC");

    Require(StrengthMask(CHAR_ID_KANNA, ACTION_236236) == (kA | kB),
            "Kanna FM-only 236236C leaked into ordinary supers");
    Require(StrengthMask(CHAR_ID_KANO, ACTION_2141236) == kABC &&
            StrengthMask(CHAR_ID_KANO, ACTION_22) == kB,
            "Kano ordinary super inventory drifted");
    Require(!IsAvailable(CHAR_ID_KANO, ACTION_2141236, 3),
            "Kano FM-only 2141236S leaked into ordinary supers");
    Require(!IsAvailable(CHAR_ID_MAI, ACTION_236236, 3),
            "Mai FM-only 236236S leaked into ordinary supers");
    Require(StrengthMask(CHAR_ID_MIO, ACTION_641236) == 0,
            "Mio FM-only 641236C leaked into ordinary supers");
    Require(!IsAvailable(CHAR_ID_NANASE, ACTION_4123641236, 3),
            "Rumi FM-only 4123641236S leaked into ordinary supers");
    Require(StrengthMask(CHAR_ID_NANASE, ACTION_236236) == kABC,
            "Rumi no-shinai 236236 family missing");

    Require(StrengthMask(CHAR_ID_UNKNOWN_BOSS, ACTION_41236) == kABC &&
            StrengthMask(CHAR_ID_UNKNOWN_BOSS, ACTION_463214) == kC &&
            StrengthMask(CHAR_ID_UNKNOWN_BOSS, ACTION_412) == 0,
            "boss UNKNOWN inventory was aliased to Mizuka Nagamori");
    Require(StrengthMask(CHAR_ID_MIZUKA, ACTION_412) == kABC &&
            StrengthMask(CHAR_ID_MIZUKA, ACTION_41236) == 0,
            "Mizuka Nagamori was aliased to boss UNKNOWN");
    Require(StrengthMask(CHAR_ID_NAYUKI, ACTION_QCF) == kABCD &&
            StrengthMask(CHAR_ID_NAYUKIB, ACTION_QCF) == kABC,
            "sleepy/awake Nayuki 236 inventories were aliased");
}

void TestFinalMemoryVisibility() {
    using namespace CharacterActionCatalog;
    Require(HasFinalMemoryInput(CHAR_ID_AKANE), "Akane FM input hidden");
    Require(!HasFinalMemoryInput(CHAR_ID_EXNANASE),
            "Doppel automatic Enlightenment exposed as an input");
    Require(!HasFinalMemoryInput(CHAR_ID_UNKNOWN_BOSS),
            "boss UNKNOWN exposed an unsupported FM command");
}

void TestStablePoolAdapter() {
    using namespace CharacterActionCatalog;
    int action = -1;
    int strength = -1;
    Require(PoolIndexToAction(44, action, strength) &&
            action == ACTION_41236 && strength == 0,
            "legacy pool bit 44 no longer means 41236A");
    Require(PoolIndexToAction(76, action, strength) &&
            action == ACTION_FINAL_MEMORY,
            "legacy pool bit 76 no longer means Final Memory");
    Require(PoolIndexToAction(kPool3X + 2, action, strength) &&
            action == ACTION_3X && strength == 2,
            "appended 3C pool identity drifted");
    Require(PoolIndexToAction(kPool664X + 1, action, strength) &&
            action == ACTION_664X && strength == 1,
            "appended 664B pool identity drifted");
    Require(IsPoolIndexAvailable(CHAR_ID_MAYU, kPool664X + 2),
            "Mayu 664C pool entry hidden");
    Require(!IsPoolIndexAvailable(CHAR_ID_MAYU, kPool66X),
            "Mayu fabricated 66A pool entry visible");
    Require(PoolIndexToAction(kPoolKaoriRecoilDuck, action, strength) &&
            action == ACTION_KAORI_RECOIL_DUCK && strength == 0,
            "appended Kaori 44~66 pool identity drifted");
    Require(IsPoolIndexAvailable(CHAR_ID_KAORI, kPoolKaoriRecoilDuck) &&
            !IsPoolIndexAvailable(CHAR_ID_MAYU, kPoolKaoriRecoilDuck),
            "Kaori-only staged pool filtering drifted");
}

}

int main() {
    TestMayuRecipes();
    TestKanoRecipes();
    TestCharacterLocalExceptions();
    TestCommandNormalsStayCharacterLocal();
    TestSButtonNormalsAreNotUniversal();
    TestMizukaNagamoriRecipes();
    TestBossUnknownRecipes();
    TestUnknownRecipes();
    TestDocumentedMotionExceptions();
    TestFinalMemoryVisibility();
    TestStablePoolAdapter();
    std::cout << "character_action_catalog_tests: ok\n";
    return 0;
}
