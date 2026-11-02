#include "../../../include/game/mission/mission_setup.h"
#include "../../../include/game/character_settings.h"
#include "../../../include/game/character_hotswap.h"
#include "../../../include/core/constants.h"
#include "../../../include/core/logger.h"
#include "../../../include/core/memory.h"
#include "../../../include/utils/utilities.h"
#include "../../../include/utils/bgm_control.h"
#include "../../../include/game/game_state.h"

#include <windows.h>

#include <algorithm>

#include <cstring>

namespace Mission::Setup {

namespace {

bool ResourceNamesEqual(const std::string& lhs, const std::string& rhs) {
    return _stricmp(lhs.c_str(), rhs.c_str()) == 0;
}

ResourcePolicy::Family ResourceFamilyForCharacter(int charId) {
    switch (charId) {
        case CHAR_ID_MISHIO: return ResourcePolicy::Family::Mishio;
        case CHAR_ID_MIO: return ResourcePolicy::Family::Mio;
        case CHAR_ID_KANO: return ResourcePolicy::Family::Kano;
        case CHAR_ID_MAI: return ResourcePolicy::Family::Mai;
        case CHAR_ID_IKUMI: return ResourcePolicy::Family::Ikumi;
        case CHAR_ID_MISUZU: return ResourcePolicy::Family::Misuzu;
        case CHAR_ID_NANASE: return ResourcePolicy::Family::Rumi;
        case CHAR_ID_AKIKO: return ResourcePolicy::Family::Akiko;
        case CHAR_ID_NAYUKI: return ResourcePolicy::Family::Neyuki;
        default: return ResourcePolicy::Family::None;
    }
}

void CaptureResources(const DisplayData& d, int charId, bool p1,
                      std::map<std::string, int>& out) {
    switch (charId) {
        case CHAR_ID_MISHIO:
            out["element"] = p1 ? d.p1MishioElement : d.p2MishioElement; break;
        case CHAR_ID_MIO:
            out["stance"] = p1 ? d.p1MioStance : d.p2MioStance; break;
        case CHAR_ID_KANO:
            out["magic"] = p1 ? d.p1KanoMagic : d.p2KanoMagic; break;
        case CHAR_ID_MAI:
            out["status"]     = p1 ? d.p1MaiStatus : d.p2MaiStatus;
            out["ghostTime"]  = p1 ? d.p1MaiGhostTime : d.p2MaiGhostTime;
            out["ghostCharge"]= p1 ? d.p1MaiGhostCharge : d.p2MaiGhostCharge;
            break;
        case CHAR_ID_IKUMI:
            out["blood"]      = p1 ? d.p1IkumiBlood : d.p2IkumiBlood;
            out["genocide"]   = p1 ? d.p1IkumiGenocide : d.p2IkumiGenocide;
            out["levelGauge"] = p1 ? d.p1IkumiLevelGauge : d.p2IkumiLevelGauge;
            break;
        case CHAR_ID_MISUZU:
            out["feathers"]    = p1 ? d.p1MisuzuFeathers : d.p2MisuzuFeathers;
            out["poisonTimer"] = p1 ? d.p1MisuzuPoisonTimer : d.p2MisuzuPoisonTimer;
            out["poisonLevel"] = p1 ? d.p1MisuzuPoisonLevel : d.p2MisuzuPoisonLevel;
            break;
        case CHAR_ID_NANASE:
            out["barehanded"]  = (p1 ? d.p1RumiBarehanded : d.p2RumiBarehanded) ? 1 : 0;
            out["kimchiActive"] = (p1 ? d.p1RumiKimchiActive : d.p2RumiKimchiActive) ? 1 : 0;
            out["kimchiTimer"] = p1 ? d.p1RumiKimchiTimer : d.p2RumiKimchiTimer;
            break;
        case CHAR_ID_AKIKO:
            out["bulletCycle"] = p1 ? d.p1AkikoBulletCycle : d.p2AkikoBulletCycle;
            break;
        case CHAR_ID_NAYUKI:
            out["jam"] = p1 ? d.p1NeyukiJamCount : d.p2NeyukiJamCount;
            break;
        default: break;
    }
}

void ApplyResources(DisplayData& d, int charId, bool p1,
                    const std::map<std::string, int>& res) {
    if (res.empty()) return;
    auto get = [&](const char* k, int cur) {
        auto it = res.find(k);
        return it != res.end() ? it->second : cur;
    };
    switch (charId) {
        case CHAR_ID_MISHIO:
            (p1 ? d.p1MishioElement : d.p2MishioElement) = get("element", p1 ? d.p1MishioElement : d.p2MishioElement);
            break;
        case CHAR_ID_MIO:
            (p1 ? d.p1MioStance : d.p2MioStance) = get("stance", p1 ? d.p1MioStance : d.p2MioStance);
            break;
        case CHAR_ID_KANO:
            (p1 ? d.p1KanoMagic : d.p2KanoMagic) = get("magic", p1 ? d.p1KanoMagic : d.p2KanoMagic);
            break;
        case CHAR_ID_MAI:
            (p1 ? d.p1MaiStatus : d.p2MaiStatus)           = get("status", p1 ? d.p1MaiStatus : d.p2MaiStatus);
            (p1 ? d.p1MaiGhostTime : d.p2MaiGhostTime)     = get("ghostTime", p1 ? d.p1MaiGhostTime : d.p2MaiGhostTime);
            (p1 ? d.p1MaiGhostCharge : d.p2MaiGhostCharge) = get("ghostCharge", p1 ? d.p1MaiGhostCharge : d.p2MaiGhostCharge);
            break;
        case CHAR_ID_IKUMI:
            (p1 ? d.p1IkumiBlood : d.p2IkumiBlood)           = get("blood", p1 ? d.p1IkumiBlood : d.p2IkumiBlood);
            (p1 ? d.p1IkumiGenocide : d.p2IkumiGenocide)     = get("genocide", p1 ? d.p1IkumiGenocide : d.p2IkumiGenocide);
            (p1 ? d.p1IkumiLevelGauge : d.p2IkumiLevelGauge) = get("levelGauge", p1 ? d.p1IkumiLevelGauge : d.p2IkumiLevelGauge);
            break;
        case CHAR_ID_MISUZU:
            (p1 ? d.p1MisuzuFeathers : d.p2MisuzuFeathers)       = get("feathers", p1 ? d.p1MisuzuFeathers : d.p2MisuzuFeathers);
            (p1 ? d.p1MisuzuPoisonTimer : d.p2MisuzuPoisonTimer) = get("poisonTimer", p1 ? d.p1MisuzuPoisonTimer : d.p2MisuzuPoisonTimer);
            (p1 ? d.p1MisuzuPoisonLevel : d.p2MisuzuPoisonLevel) = get("poisonLevel", p1 ? d.p1MisuzuPoisonLevel : d.p2MisuzuPoisonLevel);
            break;
        case CHAR_ID_NANASE:
            (p1 ? d.p1RumiBarehanded : d.p2RumiBarehanded) = get("barehanded", (p1 ? d.p1RumiBarehanded : d.p2RumiBarehanded) ? 1 : 0) != 0;
            (p1 ? d.p1RumiKimchiActive : d.p2RumiKimchiActive) = get("kimchiActive", (p1 ? d.p1RumiKimchiActive : d.p2RumiKimchiActive) ? 1 : 0) != 0;
            (p1 ? d.p1RumiKimchiTimer : d.p2RumiKimchiTimer) = get("kimchiTimer", p1 ? d.p1RumiKimchiTimer : d.p2RumiKimchiTimer);
            break;
        case CHAR_ID_AKIKO:
            (p1 ? d.p1AkikoBulletCycle : d.p2AkikoBulletCycle) = get("bulletCycle", p1 ? d.p1AkikoBulletCycle : d.p2AkikoBulletCycle);
            break;
        case CHAR_ID_NAYUKI:
            (p1 ? d.p1NeyukiJamCount : d.p2NeyukiJamCount) = get("jam", p1 ? d.p1NeyukiJamCount : d.p2NeyukiJamCount);
            break;
        default: break;
    }
}

bool ReadResourceValue(uintptr_t base, int charId, bool p1,
                       const std::string& key, int& valueOut) {
    const uintptr_t playerOffset = p1 ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    auto readInt = [&](uintptr_t field) {
        const uintptr_t address = ResolvePointer(base, playerOffset, field);
        return address && SafeReadMemory(address, &valueOut, sizeof(valueOut));
    };
    auto readByte = [&](uintptr_t field, uint8_t& value) {
        const uintptr_t address = ResolvePointer(base, playerOffset, field);
        return address && SafeReadMemory(address, &value, sizeof(value));
    };

    switch (charId) {
        case CHAR_ID_MISHIO:
            return key == "element" && readInt(MISHIO_ELEMENT_OFFSET);
        case CHAR_ID_MIO:
            return key == "stance" && readInt(MIO_STANCE_OFFSET);
        case CHAR_ID_KANO:
            return key == "magic" && readInt(KANO_MAGIC_OFFSET);
        case CHAR_ID_MAI: {
            uint8_t status = 0;
            if (!readByte(MAI_STATUS_OFFSET, status)) return false;
            if (key == "status") {
                valueOut = static_cast<int>(status);
                return true;
            }
            int timer = 0;
            const uintptr_t timerAddress = ResolvePointer(base, playerOffset, MAI_MULTI_TIMER_OFFSET);
            if (!timerAddress || !SafeReadMemory(timerAddress, &timer, sizeof(timer))) return false;
            if (key == "ghostTime") {
                valueOut = status == 1 ? timer : 0;
                return true;
            }
            if (key == "ghostCharge") {
                valueOut = status == 3 ? timer : 0;
                return true;
            }
            return false;
        }
        case CHAR_ID_IKUMI:
            if (key == "blood") return readInt(IKUMI_BLOOD_OFFSET);
            if (key == "genocide") return readInt(IKUMI_GENOCIDE_OFFSET);
            return key == "levelGauge" && readInt(IKUMI_LEVEL_GAUGE_OFFSET);
        case CHAR_ID_MISUZU:
            if (key == "feathers") return readInt(MISUZU_FEATHER_OFFSET);
            if (key == "poisonTimer") return readInt(MISUZU_POISON_TIMER_OFFSET);
            return key == "poisonLevel" && readInt(MISUZU_POISON_LEVEL_OFFSET);
        case CHAR_ID_NANASE: {
            if (key == "barehanded") {
                uint8_t mode = 0;
                uint8_t gate = 0;
                if (!readByte(RUMI_MODE_BYTE_OFFSET, mode) ||
                    !readByte(RUMI_WEAPON_GATE_OFFSET, gate)) return false;
                valueOut = mode != 0 || gate != 0 ? 1 : 0;
                return true;
            }
            if (key == "kimchiActive") {
                if (!readInt(RUMI_KIMCHI_ACTIVE_OFFSET)) return false;
                valueOut = valueOut != 0 ? 1 : 0;
                return true;
            }
            return key == "kimchiTimer" && readInt(RUMI_KIMCHI_TIMER_OFFSET);
        }
        case CHAR_ID_AKIKO:
            return key == "bulletCycle" && readInt(AKIKO_BULLET_CYCLE_OFFSET);
        case CHAR_ID_NAYUKI:
            return key == "jam" && readInt(NEYUKI_JAM_COUNT_OFFSET);
        default:
            return false;
    }
}

bool VerifyResources(uintptr_t base, int charId, bool p1,
                     const std::map<std::string, int>& resources,
                     std::string& errorOut) {
    const ResourcePolicy::Family family = ResourceFamilyForCharacter(charId);
    const char* side = p1 ? "learner" : "dummy";
    for (const auto& resource : resources) {
        const ResourcePolicy::Verification verification =
            ResourcePolicy::Classify(family, resource.first);
        if (verification == ResourcePolicy::Verification::Unsupported) {
            errorOut = std::string("The lesson uses an unsupported ") + side +
                " resource: " + resource.first + ".";
            return false;
        }
        int observed = 0;
        if (!ReadResourceValue(base, charId, p1, resource.first, observed)) {
            errorOut = std::string("The lesson could not verify the ") + side +
                " resource " + resource.first + ".";
            return false;
        }
        const int expected = resource.first == "barehanded" ||
                             resource.first == "kimchiActive"
            ? (resource.second != 0 ? 1 : 0) : resource.second;
        if (observed != expected) {
            if (verification == ResourcePolicy::Verification::RequiresActionable) {
                errorOut = "Rumi's weapon mode could not be prepared while she was busy. Restart the lesson.";
            } else {
                errorOut = std::string("The lesson could not set the ") + side +
                    " resource " + resource.first + ".";
            }
            return false;
        }
    }
    return true;
}

bool ValidateResourceKeys(int charId, bool p1,
                          const std::map<std::string, int>& resources,
                          std::string& errorOut) {
    const ResourcePolicy::Family family = ResourceFamilyForCharacter(charId);
    for (const auto& resource : resources) {
        if (ResourcePolicy::Classify(family, resource.first) ==
            ResourcePolicy::Verification::Unsupported) {
            errorOut = std::string("The lesson uses an unsupported ") +
                (p1 ? "learner" : "dummy") + " resource: " +
                resource.first + ".";
            return false;
        }
    }
    return true;
}

bool  g_pending = false;
int   g_pendingTimeout = 0;
::Mission::Mission g_pendingMission;
bool g_pendingApplyValues = false;
int g_pendingP1SelectId = -1;
int g_pendingP2SelectId = -1;
int g_pendingStage = -1;
int g_pendingBgm = -1;
CharacterHotswap::PaletteSelection g_pendingPalette{};
std::string g_failure;

bool ApplyValues(const ::Mission::Mission& m, std::string& errorOut) {
    uintptr_t base = GetEFZBase();
    if (!base) {
        errorOut = "the game memory base is unavailable";
        return false;
    }
    bool ok = true;

    if (m.player.hasPos || m.player.posX != 0.0 || m.player.posY != 0.0) {
        ok = TrySetPlayerPosition(base, EFZ_BASE_OFFSET_P1,
                                  m.player.posX, m.player.posY, true) && ok;
    }
    if (m.dummy.hasPos || m.dummy.posX != 0.0 || m.dummy.posY != 0.0) {
        ok = TrySetPlayerPosition(base, EFZ_BASE_OFFSET_P2,
                                  m.dummy.posX, m.dummy.posY, true) && ok;
    }

    // HP is 32-bit; the SP meter is 16-bit at METER_OFFSET, so a dword write clobbers the adjacent
    // field. SP clamps 0..3000.
    auto writeExact = [&](uintptr_t address, const void* value, size_t size) {
        unsigned char observed[sizeof(double)] = {};
        return address && size <= sizeof(observed) &&
            SafeWriteMemory(address, value, size) &&
            SafeReadMemory(address, observed, size) &&
            std::memcmp(observed, value, size) == 0;
    };
    auto writeInt = [&](uintptr_t off, uintptr_t field, int v) {
        uintptr_t addr = ResolvePointer(base, off, field);
        const bool wrote = writeExact(addr, &v, sizeof(v));
        ok = ok && wrote;
        return wrote;
    };
    auto writeMeter = [&](uintptr_t off, int v) {
        if (v < 0) return true;
        const uint16_t clamped = static_cast<uint16_t>((std::min)(3000, (std::max)(0, v)));
        uintptr_t addr = ResolvePointer(base, off, METER_OFFSET);
        const bool wrote = writeExact(addr, &clamped, sizeof(clamped));
        ok = ok && wrote;
        return wrote;
    };
    if (m.player.hp >= 0)    writeInt(EFZ_BASE_OFFSET_P1, HP_OFFSET, m.player.hp);
    if (m.dummy.hp >= 0)     writeInt(EFZ_BASE_OFFSET_P2, HP_OFFSET, m.dummy.hp);
    writeMeter(EFZ_BASE_OFFSET_P1, m.player.meter);
    writeMeter(EFZ_BASE_OFFSET_P2, m.dummy.meter);

    if (m.player.rf >= 0 || m.dummy.rf >= 0) {
        double rf1 = static_cast<double>(m.player.rf);
        double rf2 = static_cast<double>(m.dummy.rf);
        if (m.player.rf < 0) {
            const uintptr_t addr = ResolvePointer(base, EFZ_BASE_OFFSET_P1, RF_OFFSET);
            ok = addr && SafeReadMemory(addr, &rf1, sizeof(rf1)) && ok;
        }
        if (m.dummy.rf < 0) {
            const uintptr_t addr = ResolvePointer(base, EFZ_BASE_OFFSET_P2, RF_OFFSET);
            ok = addr && SafeReadMemory(addr, &rf2, sizeof(rf2)) && ok;
        }
        if (ok) ok = SetRFValuesDirect(rf1, rf2) && ok;
    }

    if (m.player.blueIC >= 0) ok = SetICColorPlayer(1, m.player.blueIC != 0) && ok;
    if (m.dummy.blueIC  >= 0) ok = SetICColorPlayer(2, m.dummy.blueIC != 0) && ok;

    auto writeGuard = [&](uintptr_t off, int v) {
        if (v < 0) return true;
        uintptr_t addr = ResolvePointer(base, off, PLAYER_GUARD_GAUGE_OFFSET);
        float f = static_cast<float>(v);
        const bool wrote = writeExact(addr, &f, sizeof(f));
        ok = ok && wrote;
        return wrote;
    };
    writeGuard(EFZ_BASE_OFFSET_P1, m.player.guard);
    writeGuard(EFZ_BASE_OFFSET_P2, m.dummy.guard);

    DisplayData setupData = displayData;
    const int authoredP1Id = m.player.character.empty()
        ? setupData.p1CharID
        : CharacterSettings::GetCharacterID(m.player.character);
    const int authoredP2Id = m.dummy.character.empty()
        ? setupData.p2CharID
        : CharacterSettings::GetCharacterID(m.dummy.character);
    if (authoredP1Id >= 0) setupData.p1CharID = authoredP1Id;
    if (authoredP2Id >= 0) setupData.p2CharID = authoredP2Id;
    if (!ValidateResourceKeys(setupData.p1CharID, true, m.player.resources, errorOut) ||
        !ValidateResourceKeys(setupData.p2CharID, false, m.dummy.resources, errorOut)) {
        LogOut("[MISSION][SETUP] " + errorOut, true);
        return false;
    }
    CharacterSettings::ReadCharacterValues(base, setupData, true);
    ApplyResources(setupData, setupData.p1CharID, true, m.player.resources);
    ApplyResources(setupData, setupData.p2CharID, false, m.dummy.resources);
    if (!m.player.resources.empty() || !m.dummy.resources.empty()) {
        CharacterSettings::ApplyCharacterValues(base, setupData, true);
        if (!VerifyResources(base, setupData.p1CharID, true,
                             m.player.resources, errorOut) ||
            !VerifyResources(base, setupData.p2CharID, false,
                             m.dummy.resources, errorOut)) {
            LogOut("[MISSION][SETUP] " + errorOut, true);
            return false;
        }
    }

    if (!ok) {
        errorOut = "the lesson could not verify its starting position, health, meter, or gauge values";
        LogOut("[MISSION][SETUP] value application failed (" + m.name + ")", true);
        return false;
    }
    LogOut("[MISSION][SETUP] values applied (" + m.name + ")", true);
    return true;
}

}

void Capture(::Mission::Mission& m) {
    DisplayData d = displayData;
    const uintptr_t base = GetEFZBase();
    if (base) {
        CharacterSettings::ReadCharacterValues(base, d, true);
    }
    m.player.character = CharacterSettings::GetCharacterInternalName(d.p1CharID);
    m.dummy.character  = CharacterSettings::GetCharacterInternalName(d.p2CharID);
    CharacterHotswap::PaletteSelection palette{};
    if (CharacterHotswap::ReadCurrentPaletteSelection(palette)) {
        m.player.palette = palette.p1Color;
        m.dummy.palette = palette.p2Color;
    }
    m.player.hasPos = true;
    m.dummy.hasPos  = true;
    m.player.posX = d.x1; m.player.posY = d.y1;
    m.dummy.posX  = d.x2; m.dummy.posY  = d.y2;
    m.player.hp = d.hp1;       m.dummy.hp = d.hp2;
    m.player.meter = d.meter1; m.dummy.meter = d.meter2;
    m.player.rf = static_cast<int>(d.rf1);
    m.dummy.rf  = static_cast<int>(d.rf2);
    m.player.blueIC = d.p1BlueIC ? 1 : 0;
    m.dummy.blueIC  = d.p2BlueIC ? 1 : 0;
    m.player.resources.clear();
    m.dummy.resources.clear();
    CaptureResources(d, d.p1CharID, true, m.player.resources);
    CaptureResources(d, d.p2CharID, false, m.dummy.resources);

    // Guard gauge (player+0x134, float 0..360).
    if (base) {
        auto readGuard = [&](uintptr_t off) -> int {
            uintptr_t addr = ResolvePointer(base, off, PLAYER_GUARD_GAUGE_OFFSET);
            float v = -1.0f;
            if (addr && SafeReadMemory(addr, &v, sizeof(v)) && v >= 0.0f) return static_cast<int>(v);
            return -1;
        };
        m.player.guard = readGuard(EFZ_BASE_OFFSET_P1);
        m.dummy.guard  = readGuard(EFZ_BASE_OFFSET_P2);
    }

    // GameSystem+0xF26 is a DirectSound buffer index, not a PlayBGM track;
    // the audio entry-point tracker is authoritative, -1 = native stage music.
    const uintptr_t gameStatePtr = GetGameStatePtr();
    if (gameStatePtr) {
        uint8_t stage = 0;
        if (SafeReadMemory(gameStatePtr + 3890, &stage, sizeof(stage))) {
            m.stage = static_cast<int>(stage);
        }
        const unsigned short bgmTrack = GetLastBgmTrack();
        const int nativeStageTrack = m.stage >= 0 ? 10 + m.stage : -1;
        m.bgm = bgmTrack == 0xFFFFu || static_cast<int>(bgmTrack) == nativeStageTrack
            ? -1 : static_cast<int>(bgmTrack);
    }
    LogOut("[MISSION][SETUP] captured " + m.player.character + " vs " + m.dummy.character +
           " stage=" + std::to_string(m.stage) + " bgm=" + std::to_string(m.bgm), true);
}

bool Apply(const ::Mission::Mission& m, bool applyValues, bool forceFreshMatch,
           bool allowReload, bool acceptPreparedSession) {
    if (g_pending) {
        g_failure = "another lesson start is still being prepared";
        LogOut("[MISSION][SETUP] rejected overlapping setup transaction", true);
        return false;
    }
    g_failure.clear();
    const std::string currentP1Resource =
        CharacterSettings::GetCharacterInternalName(displayData.p1CharID);
    const std::string currentP2Resource =
        CharacterSettings::GetCharacterInternalName(displayData.p2CharID);
    const bool wantsP1 = !m.player.character.empty();
    const bool wantsP2 = !m.dummy.character.empty();
    const int wantP1SelectId = wantsP1
        ? CharacterHotswap::GetSelectIdForResourceName(m.player.character.c_str()) : -1;
    const int wantP2SelectId = wantsP2
        ? CharacterHotswap::GetSelectIdForResourceName(m.dummy.character.c_str()) : -1;
    bool characterMismatch =
        (wantsP1 && !ResourceNamesEqual(m.player.character, currentP1Resource)) ||
        (wantsP2 && !ResourceNamesEqual(m.dummy.character, currentP2Resource));

    int liveStage = -1;
    const uintptr_t gameStatePtr = GetGameStatePtr();
    if (gameStatePtr) {
        uint8_t stage = 0;
        if (SafeReadMemory(gameStatePtr + 3890, &stage, sizeof(stage))) {
            liveStage = static_cast<int>(stage);
        }
    }
    bool stageMismatch = m.stage >= 0 && m.stage != liveStage;
    const unsigned short observedBgmTrack = GetLastBgmTrack();
    const int liveBgm = observedBgmTrack == 0xFFFFu
        ? -1 : static_cast<int>(observedBgmTrack);
    bool bgmNeedsApply = m.bgm >= 0 && m.bgm != liveBgm;

    CharacterHotswap::PaletteSelection requestedPalette{};
    requestedPalette.p1Color = m.player.palette;
    requestedPalette.p2Color = m.dummy.palette;
    const int resolvedP1SelectId = wantsP1 ? wantP1SelectId
        : CharacterHotswap::GetSelectIdForResourceName(currentP1Resource.c_str());
    const int resolvedP2SelectId = wantsP2 ? wantP2SelectId
        : CharacterHotswap::GetSelectIdForResourceName(currentP2Resource.c_str());
    if (resolvedP1SelectId >= 0 && resolvedP2SelectId >= 0) {
        CharacterHotswap::SanitizePaletteSelection(
            resolvedP1SelectId, resolvedP2SelectId, requestedPalette);
    }
    CharacterHotswap::PaletteSelection livePalette{};
    const bool haveLivePalette = CharacterHotswap::ReadCurrentPaletteSelection(livePalette);
    bool paletteMismatch = haveLivePalette &&
        (livePalette.p1Color != requestedPalette.p1Color ||
         livePalette.p2Color != requestedPalette.p2Color ||
         livePalette.p1UseCustomPalette != requestedPalette.p1UseCustomPalette ||
         livePalette.p2UseCustomPalette != requestedPalette.p2UseCustomPalette);

    const int requestedStage = m.stage >= 0 ? m.stage : (liveStage >= 0 ? liveStage : 0);
    const bool acceptedPreparedSession = acceptPreparedSession && !forceFreshMatch &&
        resolvedP1SelectId >= 0 && resolvedP2SelectId >= 0 &&
        CharacterHotswap::ConsumeCompletedPracticeLoad(
            resolvedP1SelectId, resolvedP2SelectId, requestedStage,
            requestedPalette, m.bgm);
    if (acceptedPreparedSession) {
        characterMismatch = false;
        stageMismatch = false;
        paletteMismatch = false;
        bgmNeedsApply = false;
        LogOut("[MISSION][SETUP] accepted completed Practice launch receipt", true);
    }

    const bool needFreshMatch = forceFreshMatch || characterMismatch || stageMismatch ||
                                paletteMismatch;

    if (needFreshMatch) {
        if (!allowReload) {
            LogOut("[MISSION][SETUP] in-place restore rejected because the prepared session no longer matches",
                   true);
            return false;
        }
        const int p1SelectId = resolvedP1SelectId;
        const int p2SelectId = resolvedP2SelectId;
        if (p1SelectId < 0 || p2SelectId < 0) {
            LogOut("[MISSION][SETUP] reload rejected: unresolved resource-to-select mapping p1='" +
                   (wantsP1 ? m.player.character : currentP1Resource) + "' p2='" +
                   (wantsP2 ? m.dummy.character : currentP2Resource) + "'", true);
            return false;
        }

        const int stage = requestedStage;
        const int bgm = m.bgm;
        LogOut("[MISSION][SETUP] fresh match required current=" + currentP1Resource + "/" +
               currentP2Resource + " requested=" +
               (wantsP1 ? m.player.character : currentP1Resource) + "/" +
               (wantsP2 ? m.dummy.character : currentP2Resource) + " select=" +
               std::to_string(p1SelectId) + "/" + std::to_string(p2SelectId) +
               " reasons=force:" + std::to_string(forceFreshMatch ? 1 : 0) +
               " chars:" + std::to_string(characterMismatch ? 1 : 0) +
               " stage:" + std::to_string(stageMismatch ? 1 : 0) +
               " palette:" + std::to_string(paletteMismatch ? 1 : 0) +
                " bgmInPlace:" + std::to_string(bgmNeedsApply ? 1 : 0), true);

        bool queued = CharacterHotswap::QueueDirectPracticeLoad(
            p1SelectId, p2SelectId, stage, requestedPalette, bgm);
        if (!queued && CharacterHotswap::CanQueueReload()) {
            queued = CharacterHotswap::QueueReload(
                p1SelectId, p2SelectId, stage, requestedPalette, bgm);
        }
        if (queued) {
            g_pending = true;
            g_pendingTimeout = 0;
            g_pendingMission = m;
            g_pendingApplyValues = applyValues;
            g_pendingP1SelectId = p1SelectId;
            g_pendingP2SelectId = p2SelectId;
            g_pendingStage = stage;
            g_pendingBgm = bgm;
            g_pendingPalette = requestedPalette;
            if (applyValues) {
                LogOut("[MISSION][SETUP] native reload queued -> values deferred", true);
            } else {
                LogOut("[MISSION][SETUP] native reload queued -> state restore held for matching receipt",
                       true);
            }
            return true;
        }
        LogOut("[MISSION][SETUP] reload queue failed; refusing to apply setup to the wrong session", true);
        return false;
    }
    if (bgmNeedsApply) {
        if (gameStatePtr && PlayBGM(gameStatePtr, static_cast<unsigned short>(m.bgm))) {
            LogOut("[MISSION][SETUP] applied BGM track in place=" +
                   std::to_string(m.bgm), true);
        } else {
            LogOut("[MISSION][SETUP] could not apply BGM track in place=" +
                   std::to_string(m.bgm), true);
        }
    }
    if (applyValues) {
        std::string error;
        if (!ApplyValues(m, error)) {
            g_failure = error.empty()
                ? std::string("the authored start values could not be applied")
                : error;
            return false;
        }
    }
    return true;
}

void Tick() {
    if (!g_pending) return;
    const bool timedOut = ++g_pendingTimeout > 192 * 30;
    const bool busy = CharacterHotswap::IsBusy();
    const bool inMatch = GetCurrentGamePhase() == GamePhase::Match;
    if (timedOut) {
        g_pending = false;
        g_failure = "the character/stage reload did not settle before setup timed out";
        LogOut("[MISSION][SETUP] deferred apply timed out", true);
        return;
    }
    if (busy || !inMatch) return;

    const bool receiptMatched = CharacterHotswap::ConsumeCompletedPracticeLoad(
        g_pendingP1SelectId, g_pendingP2SelectId, g_pendingStage,
        g_pendingPalette, g_pendingBgm);
    const auto effect = DeferredPolicy::DecideSettle(
        false, false, true, receiptMatched, g_pendingApplyValues);
    g_pending = false;
    if (effect == DeferredPolicy::SettleEffect::Fail) {
        g_failure = "the character/stage reload did not produce the requested match";
        LogOut("[MISSION][SETUP] deferred apply rejected: matching completion receipt missing",
               true);
        return;
    }
    if (effect == DeferredPolicy::SettleEffect::ReleaseForStateRestore) {
        LogOut("[MISSION][SETUP] reload receipt accepted -> embedded state restore released",
               true);
        return;
    }

    std::string error;
    if (!ApplyValues(g_pendingMission, error)) g_failure = error;
}

bool IsPending() { return g_pending; }

void Cancel(const char* reason) {
    if (g_pending) {
        LogOut(std::string("[MISSION][SETUP] pending transaction canceled") +
               (reason && *reason ? std::string(": ") + reason : std::string()), true);
    }
    g_pending = false;
    g_pendingTimeout = 0;
    g_pendingMission = ::Mission::Mission{};
    g_pendingApplyValues = false;
    g_pendingP1SelectId = -1;
    g_pendingP2SelectId = -1;
    g_pendingStage = -1;
    g_pendingBgm = -1;
    g_pendingPalette = CharacterHotswap::PaletteSelection{};
    g_failure.clear();
}

bool TakeFailure(std::string& errorOut) {
    if (g_failure.empty()) return false;
    errorOut.swap(g_failure);
    g_failure.clear();
    return true;
}

}
