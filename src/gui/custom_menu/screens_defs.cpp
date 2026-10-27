#include "../include/gui/custom_menu/screens.h"
#include "../include/gui/custom_menu/renderer.h"
#include "../include/gui/custom_menu/layout.h"
#include "../include/gui/custom_menu/theme.h"
#include "../include/gui/custom_menu/scale.h"
#include "../include/gui/custom_menu/input.h"
#include "../include/gui/imgui_gui.h"
#include "../include/gui/imgui_impl.h"
#include "../include/utils/utilities.h"
#include "../include/utils/config.h"
#include "../include/utils/i18n.h"
#include "../include/utils/debug_log.h"
#include "../include/core/constants.h"
#include "../include/core/version.h"
#include "../include/utils/update_check.h"
#include "../include/game/practice_patch.h"
#include "../include/game/hud_disable.h"
#include "../include/game/practice_offsets.h"
#include "../include/game/game_state.h"
#include "../include/game/always_rg.h"
#include "../include/game/random_rg.h"
#include "../include/game/random_block.h"
#include "../include/game/final_memory_patch.h"
#include "../include/game/macro_controller.h"
#include "../include/game/mission/mission_engine.h"
#include "../include/game/mission/mission_data.h"
#include "../include/game/mission/mission_authoring.h"
#include "../include/game/custom_savestate.h"
#include "../include/game/savestate_hook.h"
#include "../include/game/fm_commands.h"
#include "../include/game/character_settings.h"
#include "../include/game/character_action_catalog.h"
#include "../include/game/character_hotswap.h"
#include "../include/game/sayuri_counter.h"
#include "../include/game/timer_freeze_patch.h"
#include "../include/gui/overlay.h"
#include "../include/gui/framebar.h"
#include "../include/utils/controller_names.h"
#include "../include/utils/xinput_shim.h"
#include "../include/utils/network.h"
#include "../include/utils/pause_integration.h"
#include "../include/utils/switch_players.h"
#include "../include/utils/bgm_control.h"
#include "../include/utils/audio_control.h"
#include "../include/utils/extended_config_bridge.h"
#include "../include/input/framestep.h"
#include "../include/input/input_motion.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/gui/gif_player.h"
#include "../3rdparty/imgui/imgui.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace CustomMenu::Screens {

namespace {

Config::Settings& MutableSettings() {
    return const_cast<Config::Settings&>(Config::GetSettings());
}

void PersistBool(const char* section, const char* key, bool v) {
    Config::SetSetting(section, key, v ? "1" : "0");
}

void PersistInt(const char* section, const char* key, int v) {
    char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", v);
    Config::SetSetting(section, key, buf);
}

void PersistFloat(const char* section, const char* key, float v) {
    char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.4f", v);
    Config::SetSetting(section, key, buf);
}

bool P1Or(int charId) {
    const auto& d = ImGuiGui::guiState.localData;
    return d.p1CharID == charId || d.p2CharID == charId;
}

bool HasIkumi()    { return P1Or(CHAR_ID_IKUMI); }
bool HasShiori()   { return P1Or(CHAR_ID_SHIORI); }
bool HasMisuzu()   { return P1Or(CHAR_ID_MISUZU); }
bool HasMishio()   { return P1Or(CHAR_ID_MISHIO); }
bool HasAkiko()    { return P1Or(CHAR_ID_AKIKO); }
bool HasNayuki()   { return P1Or(CHAR_ID_NAYUKIB); }
bool HasKano()     { return P1Or(CHAR_ID_KANO); }
bool HasRumi()     { return P1Or(CHAR_ID_NANASE); }
bool HasDoppel()   { return P1Or(CHAR_ID_EXNANASE); }
bool HasMio()      { return P1Or(CHAR_ID_MIO); }
bool HasNeyuki()   { return P1Or(CHAR_ID_NAYUKI); }
bool HasMai()      { return P1Or(CHAR_ID_MAI); }
bool HasMinagi()   { return P1Or(CHAR_ID_MINAGI); }
bool HasMizuka()   { return P1Or(CHAR_ID_MIZUKA); }

bool NotIkumi()  { return !HasIkumi(); }
bool NotMisuzu() { return !HasMisuzu(); }
bool NotMishio() { return !HasMishio(); }
bool NotAkiko()  { return !HasAkiko(); }
bool NotNayuki() { return !HasNayuki(); }
bool NotKano()   { return !HasKano(); }
bool NotRumi()   { return !HasRumi(); }
bool NotDoppel() { return !HasDoppel(); }
bool NotMio()    { return !HasMio(); }
bool NotNeyuki() { return !HasNeyuki(); }
bool NotMai()    { return !HasMai(); }
bool NotMinagi() { return !HasMinagi(); }

bool NoneOfTheAbove() {
    return !HasIkumi() && !HasMisuzu() && !HasMishio() && !HasAkiko() &&
           !HasNayuki() && !HasKano()  && !HasRumi()   && !HasDoppel() &&
           !HasMio()   && !HasNeyuki() && !HasMai()    && !HasMinagi() &&
           !HasMizuka();
}

bool CharsDetected() {
    const auto& d = ImGuiGui::guiState.localData;
    return d.p1CharID != 0 || d.p2CharID != 0 ||
           d.p1CharName[0] != '\0' || d.p2CharName[0] != '\0';
}

bool CharsNotDetected() { return !CharsDetected(); }

void OnAutoApply()       { ImGuiGui::ApplyImGuiSettings(); }

void OnUseCustomMenu()   { PersistBool ("General", "useCustomMenu",          MutableSettings().useCustomMenu); }
void OnUiScale()         { PersistFloat("General", "uiScale",                MutableSettings().uiScale); }
void OnFADuration()      { PersistFloat("General", "frameAdvantageDisplayDuration", MutableSettings().frameAdvantageDisplayDuration); }
void OnShowCombo()       { PersistBool ("General", "showComboStatisticsOverlay",    MutableSettings().showComboStatisticsOverlay); }
void OnComboDetailRow()  { PersistBool ("General", "comboOverlayShowDetailRow",     MutableSettings().comboOverlayShowDetailRow); }
void OnComboDetailSrc()  { PersistInt  ("General", "comboOverlayDetailRowSource",   MutableSettings().comboOverlayDetailRowSource); }
void OnComboFinal()      { PersistBool ("General", "comboOverlayShowFinalSummary",  MutableSettings().comboOverlayShowFinalSummary); }
void OnComboDuration()   { PersistFloat("General", "comboOverlayDisplayDuration",   MutableSettings().comboOverlayDisplayDuration); }
void OnComboHideMenu()   { PersistBool ("General", "comboOverlayHideWhenImGuiVisible", MutableSettings().comboOverlayHideWhenImGuiVisible); }
void OnComboResume()     { PersistBool ("General", "comboOverlayResumeAfterImGui",  MutableSettings().comboOverlayResumeAfterImGui); }
void OnComboRfMult()     { PersistBool ("General", "comboOverlayShowRfMultiplier",  MutableSettings().comboOverlayShowRfMultiplier); }
void OnComboRawScale()   { PersistBool ("General", "comboOverlayShowRawScale",      MutableSettings().comboOverlayShowRawScale); }
void OnCollisionDisplayHitboxes()    { PersistBool("General", "collisionDisplayHitboxes", MutableSettings().collisionDisplayHitboxes); }
void OnCollisionDisplayHurtboxes()   { PersistBool("General", "collisionDisplayHurtboxes", MutableSettings().collisionDisplayHurtboxes); }
void OnCollisionDisplayPushboxes()   { PersistBool("General", "collisionDisplayCollisionBoxes", MutableSettings().collisionDisplayCollisionBoxes); }
void OnCollisionDisplayProjectiles() { PersistBool("General", "collisionDisplayProjectileInteractions", MutableSettings().collisionDisplayProjectileInteractions); }
void OnCollisionDisplayP1Hitboxes()  { PersistBool("General", "collisionDisplayP1Hitboxes", MutableSettings().collisionDisplayP1Hitboxes); }
void OnCollisionDisplayP2Hitboxes()  { PersistBool("General", "collisionDisplayP2Hitboxes", MutableSettings().collisionDisplayP2Hitboxes); }
void OnCollisionDisplayP1Hurtboxes() { PersistBool("General", "collisionDisplayP1Hurtboxes", MutableSettings().collisionDisplayP1Hurtboxes); }
void OnCollisionDisplayP2Hurtboxes() { PersistBool("General", "collisionDisplayP2Hurtboxes", MutableSettings().collisionDisplayP2Hurtboxes); }
void OnCollisionDisplayP1Pushboxes() { PersistBool("General", "collisionDisplayP1CollisionBoxes", MutableSettings().collisionDisplayP1CollisionBoxes); }
void OnCollisionDisplayP2Pushboxes() { PersistBool("General", "collisionDisplayP2CollisionBoxes", MutableSettings().collisionDisplayP2CollisionBoxes); }
void OnCollisionDisplayAlpha()       { PersistInt ("General", "collisionDisplayFillAlphaPercent", MutableSettings().collisionDisplayFillAlphaPercent); }
void OnCollisionProjectileBoxes()    { PersistBool("General", "collisionDisplayProjectileBoxes", MutableSettings().collisionDisplayProjectileBoxes); }
void OnCollisionProjectileOrigins()  { PersistBool("General", "collisionDisplayProjectileOrigins", MutableSettings().collisionDisplayProjectileOrigins); }
void OnCollisionProjectileIntersections() { PersistBool("General", "collisionDisplayProjectileIntersections", MutableSettings().collisionDisplayProjectileIntersections); }
void OnCollisionNagamoriRanges()     { PersistBool("General", "collisionDisplayNagamoriRanges", MutableSettings().collisionDisplayNagamoriRanges); }
void OnCollisionNagamoriAffected()   { PersistBool("General", "collisionDisplayNagamoriAffected", MutableSettings().collisionDisplayNagamoriAffected); }
void OnCrRequire()       { PersistBool ("General", "crRequireBothNeutral",   MutableSettings().crRequireBothNeutral); }
void OnCrDelay()         { PersistInt  ("General", "crBothNeutralDelayMs",   MutableSettings().crBothNeutralDelayMs); }
void OnAutoFixHp()       { PersistBool ("General", "autoFixHPOnNeutral",     MutableSettings().autoFixHPOnNeutral); }
void OnFreezeRfAfterCr() { PersistBool ("General", "freezeRFAfterContRec",   MutableSettings().freezeRFAfterContRec); }
void OnFreezeRfNeutral() { PersistBool ("General", "freezeRFOnlyWhenNeutral",MutableSettings().freezeRFOnlyWhenNeutral); }
void OnDetailedLogging() { PersistBool ("General", "detailedLogging",        MutableSettings().detailedLogging);
                           detailedLogging.store(MutableSettings().detailedLogging); }
void OnShowConsole()     { PersistBool ("General", "enableConsole",          MutableSettings().enableConsole); }
void OnFpsDiag()         { PersistBool ("General", "enableFpsDiagnostics",   MutableSettings().enableFpsDiagnostics); }
void OnAutoBlockTimeout(){ PersistInt  ("General", "autoBlockNeutralTimeoutMs", MutableSettings().autoBlockNeutralTimeoutMs); }
void OnMissionRecordCountIn(){ PersistInt("General", "missionRecorderCountInMs", MutableSettings().missionRecorderCountInMs); }

void SaveSettingsToDisk(){ Config::SaveSettings(); }

void ReloadSettingsFromDisk() {
    if (Config::LoadSettings()) {
        detailedLogging.store(Config::GetSettings().detailedLogging);
        if (!DebugLog::SetEnabled(Config::GetSettings().enableDebugFileLog)) {
            MutableSettings().enableDebugFileLog = false;
            Config::SetSetting("General", "enableDebugFileLog", "0");
            LogOut("[CONFIG/UI][SETUP-FAILURE] Could not enable efz_training_debug.log", false);
        }
        AudioControl::ApplyConfiguredVolumesNow();
        LogOut("[CONFIG/UI] Settings reloaded from ini", false);
        DirectDrawHook::AddMessage("Settings reloaded from disk", "SYSTEM", RGB(180, 255, 220), 1200, 0, 120);
    } else {
        LogOut("[CONFIG/UI] Settings reload failed", true);
        DirectDrawHook::AddMessage("Settings reload failed", "SYSTEM", RGB(255, 120, 120), 1400, 0, 120);
    }
}

void OnDebugFileLogging() {
    auto& enabled = MutableSettings().enableDebugFileLog;
    if (!DebugLog::SetEnabled(enabled)) enabled = false;
    PersistBool("General", "enableDebugFileLog", enabled);
}

const char* CurrentConfigPathInfo() {
    static char buf[320];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, I18n::Tr("Config file: %s"), Config::GetConfigFilePath().c_str());
    return buf;
}

bool g_mirrorRandomize     = false;
bool g_mirrorWakeBuffer    = false;
bool g_mirrorCounterRG     = false;
bool g_mirrorFaOverlay     = false;
bool g_mirrorInfiniteBlood = false;
bool g_mirrorInfiniteShioriShield = false;
bool g_mirrorInfiniteFeather = false;
bool g_mirrorInfiniteElement = false;
bool g_mirrorInfiniteAwakened = false;

uint64_t g_poolMaskABLo, g_poolMaskABHi;
uint64_t g_poolMaskWULo, g_poolMaskWUHi;
uint64_t g_poolMaskAHLo, g_poolMaskAHHi;
uint64_t g_poolMaskAALo, g_poolMaskAAHi;
uint64_t g_poolMaskRGLo, g_poolMaskRGHi;
bool g_useMaskAB, g_useMaskWU, g_useMaskAH, g_useMaskAA, g_useMaskRG;

int GetMotionIndexForAction(int action);
int EffectiveAutoActionCharId();
void RefreshTriggerActionChoices();
int GetTriggerActionChoiceIndex(int action, int strength);
bool NormalizeTriggerActionForCatalog(int& action, int& strength);
const char* FormatTriggerActionChoiceRow(const Row& row);
void ExpandLegacyActionPoolMask(uint32_t legacyMask, int strength, uint64_t& lo, uint64_t& hi);
void InitializeAddedPoolDelaysForTrigger(int triggerIdx,
                                         uint64_t oldLo, uint64_t oldHi,
                                         uint64_t newLo, uint64_t newHi);
bool NormalizeSelectedPoolDelaysForTrigger(int triggerIdx,
                                           uint64_t maskLo, uint64_t maskHi,
                                           bool initializeMissing);

int g_motionIdxAB, g_motionIdxWU, g_motionIdxAH, g_motionIdxAA, g_motionIdxRG;
int g_actionPickIdxAB, g_actionPickIdxWU, g_actionPickIdxAH, g_actionPickIdxAA, g_actionPickIdxRG;
int g_selectedAutoTrigger = 0;
int g_mirrorFwdDashFollowup = 0;

void RefreshAutoMirrors() {
    auto& d = ImGuiGui::guiState.localData;
    g_mirrorRandomize  = d.randomizeTriggers;
    g_mirrorWakeBuffer = g_wakeBufferingEnabled.load();
    g_mirrorCounterRG  = g_counterRGEnabled.load();
    g_mirrorFaOverlay  = g_showFrameAdvantageOverlay.load();

    g_poolMaskABLo = triggerAfterBlockActionPoolMaskLo.load();
    g_poolMaskABHi = triggerAfterBlockActionPoolMaskHi.load();
    g_poolMaskWULo = triggerOnWakeupActionPoolMaskLo.load();
    g_poolMaskWUHi = triggerOnWakeupActionPoolMaskHi.load();
    g_poolMaskAHLo = triggerAfterHitstunActionPoolMaskLo.load();
    g_poolMaskAHHi = triggerAfterHitstunActionPoolMaskHi.load();
    g_poolMaskAALo = triggerAfterAirtechActionPoolMaskLo.load();
    g_poolMaskAAHi = triggerAfterAirtechActionPoolMaskHi.load();
    g_poolMaskRGLo = triggerOnRGActionPoolMaskLo.load();
    g_poolMaskRGHi = triggerOnRGActionPoolMaskHi.load();
    if ((g_poolMaskABLo | g_poolMaskABHi) == 0) ExpandLegacyActionPoolMask(triggerAfterBlockActionPoolMask.load(), d.strengthAfterBlock, g_poolMaskABLo, g_poolMaskABHi);
    if ((g_poolMaskWULo | g_poolMaskWUHi) == 0) ExpandLegacyActionPoolMask(triggerOnWakeupActionPoolMask.load(), d.strengthOnWakeup, g_poolMaskWULo, g_poolMaskWUHi);
    if ((g_poolMaskAHLo | g_poolMaskAHHi) == 0) ExpandLegacyActionPoolMask(triggerAfterHitstunActionPoolMask.load(), d.strengthAfterHitstun, g_poolMaskAHLo, g_poolMaskAHHi);
    if ((g_poolMaskAALo | g_poolMaskAAHi) == 0) ExpandLegacyActionPoolMask(triggerAfterAirtechActionPoolMask.load(), d.strengthAfterAirtech, g_poolMaskAALo, g_poolMaskAAHi);
    if ((g_poolMaskRGLo | g_poolMaskRGHi) == 0) ExpandLegacyActionPoolMask(triggerOnRGActionPoolMask.load(), d.strengthOnRG, g_poolMaskRGLo, g_poolMaskRGHi);
    g_useMaskAB  = triggerAfterBlockUsePool.load();
    g_useMaskWU  = triggerOnWakeupUsePool.load();
    g_useMaskAH  = triggerAfterHitstunUsePool.load();
    g_useMaskAA  = triggerAfterAirtechUsePool.load();
    g_useMaskRG  = triggerOnRGUsePool.load();

    g_mirrorFwdDashFollowup = forwardDashFollowup.load();
    RefreshTriggerActionChoices();
    bool normalized = false;
    normalized = NormalizeTriggerActionForCatalog(
                     d.actionAfterBlock, d.strengthAfterBlock) || normalized;
    normalized = NormalizeTriggerActionForCatalog(
                     d.actionOnWakeup, d.strengthOnWakeup) || normalized;
    normalized = NormalizeTriggerActionForCatalog(
                     d.actionAfterHitstun, d.strengthAfterHitstun) || normalized;
    normalized = NormalizeTriggerActionForCatalog(
                     d.actionAfterAirtech, d.strengthAfterAirtech) || normalized;
    normalized = NormalizeTriggerActionForCatalog(
                     d.actionOnRG, d.strengthOnRG) || normalized;
    if (normalized) OnAutoApply();
    g_motionIdxAB = GetMotionIndexForAction(d.actionAfterBlock);
    g_motionIdxWU = GetMotionIndexForAction(d.actionOnWakeup);
    g_motionIdxAH = GetMotionIndexForAction(d.actionAfterHitstun);
    g_motionIdxAA = GetMotionIndexForAction(d.actionAfterAirtech);
    g_motionIdxRG = GetMotionIndexForAction(d.actionOnRG);
    g_actionPickIdxAB = GetTriggerActionChoiceIndex(d.actionAfterBlock, d.strengthAfterBlock);
    g_actionPickIdxWU = GetTriggerActionChoiceIndex(d.actionOnWakeup, d.strengthOnWakeup);
    g_actionPickIdxAH = GetTriggerActionChoiceIndex(d.actionAfterHitstun, d.strengthAfterHitstun);
    g_actionPickIdxAA = GetTriggerActionChoiceIndex(d.actionAfterAirtech, d.strengthAfterAirtech);
    g_actionPickIdxRG = GetTriggerActionChoiceIndex(d.actionOnRG, d.strengthOnRG);
}

void OnPoolMaskAB() {
    auto& d = ImGuiGui::guiState.localData;
    const uint64_t oldLo = d.afterBlockActionPoolMaskLo;
    const uint64_t oldHi = d.afterBlockActionPoolMaskHi;
    d.afterBlockActionPoolMaskLo = g_poolMaskABLo; d.afterBlockActionPoolMaskHi = g_poolMaskABHi;
    d.afterBlockActionPoolMask = 0;
    InitializeAddedPoolDelaysForTrigger(0, oldLo, oldHi, g_poolMaskABLo, g_poolMaskABHi);
    triggerAfterBlockActionPoolMaskLo.store(g_poolMaskABLo); triggerAfterBlockActionPoolMaskHi.store(g_poolMaskABHi);
    triggerAfterBlockActionPoolMask.store(0);
    OnAutoApply();
}
void OnPoolMaskWU() {
    auto& d = ImGuiGui::guiState.localData;
    const uint64_t oldLo = d.onWakeupActionPoolMaskLo;
    const uint64_t oldHi = d.onWakeupActionPoolMaskHi;
    d.onWakeupActionPoolMaskLo = g_poolMaskWULo; d.onWakeupActionPoolMaskHi = g_poolMaskWUHi;
    d.onWakeupActionPoolMask = 0;
    InitializeAddedPoolDelaysForTrigger(1, oldLo, oldHi, g_poolMaskWULo, g_poolMaskWUHi);
    triggerOnWakeupActionPoolMaskLo.store(g_poolMaskWULo); triggerOnWakeupActionPoolMaskHi.store(g_poolMaskWUHi);
    triggerOnWakeupActionPoolMask.store(0);
    OnAutoApply();
}
void OnPoolMaskAH() {
    auto& d = ImGuiGui::guiState.localData;
    const uint64_t oldLo = d.afterHitstunActionPoolMaskLo;
    const uint64_t oldHi = d.afterHitstunActionPoolMaskHi;
    d.afterHitstunActionPoolMaskLo = g_poolMaskAHLo; d.afterHitstunActionPoolMaskHi = g_poolMaskAHHi;
    d.afterHitstunActionPoolMask = 0;
    InitializeAddedPoolDelaysForTrigger(2, oldLo, oldHi, g_poolMaskAHLo, g_poolMaskAHHi);
    triggerAfterHitstunActionPoolMaskLo.store(g_poolMaskAHLo); triggerAfterHitstunActionPoolMaskHi.store(g_poolMaskAHHi);
    triggerAfterHitstunActionPoolMask.store(0);
    OnAutoApply();
}
void OnPoolMaskAA() {
    auto& d = ImGuiGui::guiState.localData;
    const uint64_t oldLo = d.afterAirtechActionPoolMaskLo;
    const uint64_t oldHi = d.afterAirtechActionPoolMaskHi;
    d.afterAirtechActionPoolMaskLo = g_poolMaskAALo; d.afterAirtechActionPoolMaskHi = g_poolMaskAAHi;
    d.afterAirtechActionPoolMask = 0;
    InitializeAddedPoolDelaysForTrigger(3, oldLo, oldHi, g_poolMaskAALo, g_poolMaskAAHi);
    triggerAfterAirtechActionPoolMaskLo.store(g_poolMaskAALo); triggerAfterAirtechActionPoolMaskHi.store(g_poolMaskAAHi);
    triggerAfterAirtechActionPoolMask.store(0);
    OnAutoApply();
}
void OnPoolMaskRG() {
    auto& d = ImGuiGui::guiState.localData;
    const uint64_t oldLo = d.onRGActionPoolMaskLo;
    const uint64_t oldHi = d.onRGActionPoolMaskHi;
    d.onRGActionPoolMaskLo = g_poolMaskRGLo; d.onRGActionPoolMaskHi = g_poolMaskRGHi;
    d.onRGActionPoolMask = 0;
    InitializeAddedPoolDelaysForTrigger(4, oldLo, oldHi, g_poolMaskRGLo, g_poolMaskRGHi);
    triggerOnRGActionPoolMaskLo.store(g_poolMaskRGLo); triggerOnRGActionPoolMaskHi.store(g_poolMaskRGHi);
    triggerOnRGActionPoolMask.store(0);
    OnAutoApply();
}
void OnUseMaskAB()  {
    auto& d = ImGuiGui::guiState.localData;
    d.afterBlockUseActionPool = g_useMaskAB;
    if (g_useMaskAB) NormalizeSelectedPoolDelaysForTrigger(0, g_poolMaskABLo, g_poolMaskABHi, true);
    triggerAfterBlockUsePool.store(g_useMaskAB);
    OnAutoApply();
}
void OnUseMaskWU()  {
    auto& d = ImGuiGui::guiState.localData;
    d.onWakeupUseActionPool = g_useMaskWU;
    if (g_useMaskWU) NormalizeSelectedPoolDelaysForTrigger(1, g_poolMaskWULo, g_poolMaskWUHi, true);
    triggerOnWakeupUsePool.store(g_useMaskWU);
    OnAutoApply();
}
void OnUseMaskAH()  {
    auto& d = ImGuiGui::guiState.localData;
    d.afterHitstunUseActionPool = g_useMaskAH;
    if (g_useMaskAH) NormalizeSelectedPoolDelaysForTrigger(2, g_poolMaskAHLo, g_poolMaskAHHi, true);
    triggerAfterHitstunUsePool.store(g_useMaskAH);
    OnAutoApply();
}
void OnUseMaskAA()  {
    auto& d = ImGuiGui::guiState.localData;
    d.afterAirtechUseActionPool = g_useMaskAA;
    if (g_useMaskAA) NormalizeSelectedPoolDelaysForTrigger(3, g_poolMaskAALo, g_poolMaskAAHi, true);
    triggerAfterAirtechUsePool.store(g_useMaskAA);
    OnAutoApply();
}
void OnUseMaskRG()  {
    auto& d = ImGuiGui::guiState.localData;
    d.onRGUseActionPool = g_useMaskRG;
    if (g_useMaskRG) NormalizeSelectedPoolDelaysForTrigger(4, g_poolMaskRGLo, g_poolMaskRGHi, true);
    triggerOnRGUsePool.store(g_useMaskRG);
    OnAutoApply();
}

void OnRandomizeToggle()    { ImGuiGui::guiState.localData.randomizeTriggers = g_mirrorRandomize; OnAutoApply(); }
void OnWakeBufferToggle()   { g_wakeBufferingEnabled.store(g_mirrorWakeBuffer); }
void OnCounterRGToggle()    { g_counterRGEnabled.store(g_mirrorCounterRG); OnAutoApply(); }
void OnFaOverlayToggle()    { g_showFrameAdvantageOverlay.store(g_mirrorFaOverlay); OnAutoApply(); }

void RefreshCharMirrors() {
    const auto& d = ImGuiGui::guiState.localData;
    g_mirrorInfiniteBlood    = d.infiniteBloodMode;
    g_mirrorInfiniteShioriShield = d.infiniteShioriShield;
    g_mirrorInfiniteFeather  = d.infiniteFeatherMode;
    g_mirrorInfiniteElement  = d.infiniteMishioElement;
    g_mirrorInfiniteAwakened = d.infiniteMishioAwakened;
}

void OnInfBlood()    { ImGuiGui::guiState.localData.infiniteBloodMode = g_mirrorInfiniteBlood; OnAutoApply(); }
void OnInfShioriShield() { ImGuiGui::guiState.localData.infiniteShioriShield = g_mirrorInfiniteShioriShield; OnAutoApply(); }
void OnInfFeather()  { ImGuiGui::guiState.localData.infiniteFeatherMode = g_mirrorInfiniteFeather; OnAutoApply(); }
void OnInfElement()  { ImGuiGui::guiState.localData.infiniteMishioElement = g_mirrorInfiniteElement; OnAutoApply(); }
void OnInfAwakened() { ImGuiGui::guiState.localData.infiniteMishioAwakened = g_mirrorInfiniteAwakened; OnAutoApply(); }

bool g_mirrorRandomBlock  = false;
bool g_mirrorAlwaysRG     = false;
bool g_mirrorRandomRG     = false;
bool g_mirrorAdaptiveStance = false;
int  g_mirrorDummyBlockMode = 0;   // 0..3 via SetDummyAutoBlockMode
int  g_mirrorPracticeStance = 0;   // 0=Standing,1=Jumping,2=Crouching
bool g_mirrorFmBypass     = false;
int  g_mirrorAirtechMode  = 0;     // 0=disabled/neutral,1=forward,2=back
int  g_mirrorAutoJumpTargetIdx = 2; // 0=P1, 1=P2, 2=Both; runtime stores 1/2/3

void RefreshOpponentMirrors() {
    const auto& d = ImGuiGui::guiState.localData;
    g_mirrorRandomBlock     = RandomBlock::IsEnabled();
    g_mirrorAlwaysRG        = AlwaysRG::IsEnabled();
    g_mirrorRandomRG        = RandomRG::IsEnabled();
    g_mirrorAdaptiveStance  = GetAdaptiveStanceEnabled();
    g_mirrorDummyBlockMode  = GetDummyAutoBlockMode();
    g_mirrorAutoJumpTargetIdx = (d.jumpTarget == 2) ? 1 : (d.jumpTarget == 3 ? 2 : 0);
    int stance = 0;
    if (GetPracticeBlockMode(stance)) g_mirrorPracticeStance = stance;
}

void RefreshOptionsMirrors() {
    g_mirrorFaOverlay = g_showFrameAdvantageOverlay.load();
    g_mirrorFmBypass  = IsFinalMemoryBypassEnabled();
}

void ForceDefenseMutex(int kept /*0=RandomBlock, 1=AlwaysRG, 2=RandomRG*/) {
    if (kept != 0 && g_mirrorRandomBlock) { g_mirrorRandomBlock = false; RandomBlock::SetEnabled(false); }
    if (kept != 1 && g_mirrorAlwaysRG)    { g_mirrorAlwaysRG    = false; AlwaysRG::SetEnabled(false); }
    if (kept != 2 && g_mirrorRandomRG)    { g_mirrorRandomRG    = false; RandomRG::SetEnabled(false); }
}

void OnRandomBlock() {
    if (g_mirrorRandomBlock) ForceDefenseMutex(0);
    RandomBlock::SetEnabled(g_mirrorRandomBlock);
}
void OnAlwaysRG() {
    if (g_mirrorAlwaysRG) ForceDefenseMutex(1);
    AlwaysRG::SetEnabled(g_mirrorAlwaysRG);
}
void OnRandomRG() {
    if (g_mirrorRandomRG) ForceDefenseMutex(2);
    RandomRG::SetEnabled(g_mirrorRandomRG);
}
void OnAdaptiveStance() { SetAdaptiveStanceEnabled(g_mirrorAdaptiveStance); }
void OnDummyBlockMode() {
    SetDummyAutoBlockMode(g_mirrorDummyBlockMode);
    if (g_mirrorDummyBlockMode == 0 && g_mirrorRandomBlock) {
        g_mirrorRandomBlock = false;
        RandomBlock::SetEnabled(false);
    }
}
void OnPracticeStance() { SetPracticeBlockMode(g_mirrorPracticeStance); }
void OnFmBypass()       { SetFinalMemoryBypass(g_mirrorFmBypass); }

const char* const kElementChoices[4] = { "NONE", "FIRE", "LIGHT", "AWAKE" };
const char* const kStanceChoices[2]  = { "SHORT", "LONG" };
const char* const kRumiModeChoices[2] = { "SHINAI", "BARE" };

// Dense action-index list (matches ACTION_xxx constant values in constants.h).
const char* const kActionNames[ACTION_COUNT] = {
    "5A","5B","5C","5S",
    "2A","2B","2C","2S",
    "jA","jB","jC","jS",
    "6A","6B","6C","6S",
    "4A","4B","4C","4S",
    "QCF (236)", "DP (623)", "QCB (214)", "421",
    "41236", "2141236", "236236", "214214",
    "JUMP", "BACKDASH", "FORWARD DASH", "BLOCK", "FINAL MEMORY",
    "641236", "463214", "412", "22", "4123641236", "6321463214",
    "1X", "3X", "j.2X", "j.6X", "66X", "662X", "664X",
    "KAORI 44~66"
};
constexpr int kActionCount = ACTION_COUNT;

const char* const kStrengthChoices[4] = { "A", "B", "C", "S" };
const char* const kChargeFollowupChoices[3] = { "OFF", "IC ON CONTACT", "FIC WINDOW" };
const char* const kJumpDirChoicesAuto[3] = { "NEUTRAL", "FORWARD", "BACK" };
const char* const kFdFollowupChoices[7] = {
    "NO FOLLOW-UP", "A", "B", "C", "2A", "2B", "2C"
};
const char* const kAkikoSlowChoices[4] = { "INACTIVE", "A", "B", "C" };
const char* const kAkikoBulletChoices[3] = { "EGG / TUNA", "CARROT / RADISH", "SARDINE / DURIAH" };
const char* const kDoppelTechChoices[6] = { "OFF", "NEVER", "TECH B", "TECH C", "ALWAYS", "RANDOM" };
const char* const kDoppelTechStageChoices[4] = { "ALL", "STAGE 1", "STAGE 2", "STAGE 3" };
const char* const kSayuriCutterChoices[2] = { "NORMAL", "ALWAYS READY" };
const char* const kSayuriAutoCutterChoices[2] = { "OFF", "ON" };
bool HideP1SayuriCutterDelay() { return ImGuiGui::guiState.localData.p1SayuriAutoCutter == 0; }
bool HideP2SayuriCutterDelay() { return ImGuiGui::guiState.localData.p2SayuriAutoCutter == 0; }
const char* const kMaiStatusChoices[5] = { "INACTIVE", "ACTIVE GHOST", "UNSUMMON", "CHARGING", "AWAKENING" };

const char* const kTriggerMotionChoices[] = {
    "5X (STANDING)",
    "2X (CROUCHING)",
    "jX (AIR)",
    "QCF (236)",
    "DP (623)",
    "QCB (214)",
    "421",
    "41236",
    "2141236",
    "236236",
    "214214",
    "641236",
    "463214",
    "412",
    "22",
    "4123641236",
    "6321463214",
    "JUMP",
    "BACKDASH",
    "FORWARD DASH",
    "BLOCK",
    "FINAL MEMORY",
    "6X (FORWARD)",
    "4X (BACK)",
    "1X (DOWN-BACK)",
    "3X (DOWN-FORWARD)",
    "j.2X (AIR DOWN)",
    "j.6X (AIR FORWARD)",
    "66X (DASH NORMAL)",
    "662X (DASH LOW)",
    "664X (DASH BACK NORMAL)",
    "44~66 (KAORI RECOIL DUCKING)"
};
constexpr int kTriggerMotionCount = sizeof(kTriggerMotionChoices) / sizeof(kTriggerMotionChoices[0]);

// Random action pools are stored as concrete action+variant bits.
const char* const kActionPoolNames[] = {
    "5A", "5B", "5C", "5S",
    "2A", "2B", "2C", "2S",
    "jA", "jB", "jC", "jS",
    "6A", "6B", "6C", "6S",
    "4A", "4B", "4C", "4S",

    "236A", "236B", "236C", "236S",
    "623A", "623B", "623C", "623S",
    "214A", "214B", "214C", "214S",
    "421A", "421B", "421C", "421S",
    "412A", "412B", "412C", "412S",
    "22A", "22B", "22C", "22S",

    "41236A", "41236B", "41236C", "41236S",
    "2141236A", "2141236B", "2141236C", "2141236S",
    "236236A", "236236B", "236236C", "236236S",
    "214214A", "214214B", "214214C", "214214S",
    "641236A", "641236B", "641236C", "641236S",
    "463214A", "463214B", "463214C", "463214S",
    "4123641236A", "4123641236B", "4123641236C", "4123641236S",
    "6321463214A", "6321463214B", "6321463214C", "6321463214S",
    "FINAL MEMORY",

    "JUMP NEUTRAL", "JUMP FORWARD", "JUMP BACK",
    "BACKDASH", "FORWARD DASH",
    "BLOCK",

    // Appended catalog recipes.  These indices are stable and must never be
    // inserted into the 0..82 legacy pool range.
    "1A", "1B", "1C", "1S",
    "3A", "3B", "3C", "3S",
    "j.2A", "j.2B", "j.2C", "j.2S",
    "j.6A", "j.6B", "j.6C", "j.6S",
    "66A", "66B", "66C", "66S",
    "662A", "662B", "662C", "662S",
    "664A", "664B", "664C", "664S",
    "KAORI 44~66"
};
constexpr int kActionPoolCount = sizeof(kActionPoolNames) / sizeof(kActionPoolNames[0]);
static_assert(kActionPoolCount <= 128, "Action pool mask uses two 64-bit words");
static_assert(kActionPoolCount == CharacterActionCatalog::kPoolCount,
              "catalog and UI pool indices drifted");

int ClampIndex(int v, int maxExclusive) {
    if (v < 0) return 0;
    if (v >= maxExclusive) return maxExclusive - 1;
    return v;
}

int NormalActionBase(int action) {
    if (action >= ACTION_5A && action <= ACTION_2D) return (action / 4) * 4;
    if (action >= ACTION_JA && action <= ACTION_JD) return ACTION_JA;
    if (action >= ACTION_6A && action <= ACTION_4D) return ACTION_6A + ((action - ACTION_6A) / 4) * 4;
    return -1;
}

bool IsNormalAction(int action) {
    return NormalActionBase(action) >= 0;
}

bool ActionUsesButtonStrength(int action) {
    switch (action) {
        case ACTION_QCF:
        case ACTION_DP:
        case ACTION_QCB:
        case ACTION_421:
        case ACTION_SUPER1:
        case ACTION_SUPER2:
        case ACTION_236236:
        case ACTION_214214:
        case ACTION_641236:
        case ACTION_463214:
        case ACTION_412:
        case ACTION_22:
        case ACTION_4123641236:
        case ACTION_6321463214:
        case ACTION_1X:
        case ACTION_3X:
        case ACTION_J2X:
        case ACTION_J6X:
        case ACTION_66X:
        case ACTION_662X:
        case ACTION_664X:
            return true;
        default:
            return false;
    }
}

bool IsSpecialMoveAction(int action) {
    return ActionUsesButtonStrength(action);
}

bool ActionSupportsChargeFollowup(int action) {
    if (action == ACTION_JUMP || action == ACTION_BACKDASH ||
        action == ACTION_FORWARD_DASH || action == ACTION_BLOCK ||
        action == ACTION_FINAL_MEMORY ||
        action == ACTION_KAORI_RECOIL_DUCK || action == ACTION_66X ||
        action == ACTION_662X || action == ACTION_664X) {
        return false;
    }
    if (action >= ACTION_5A && action <= ACTION_4D) return true;
    if (action == ACTION_1X || action == ACTION_3X ||
        action == ACTION_J2X || action == ACTION_J6X) {
        return true;
    }
    return ActionUsesButtonStrength(action);
}

int GetPostureIndexForAction(int action) {
    switch (action) {
        case ACTION_5A: case ACTION_5B: case ACTION_5C: case ACTION_5D: return 0;
        case ACTION_2A: case ACTION_2B: case ACTION_2C: case ACTION_2D: return 1;
        case ACTION_JA: case ACTION_JB: case ACTION_JC: case ACTION_JD: return 2;
        default: return -1;
    }
}

int ExtractButtonIndex(int action, int strength) {
    const int posture = GetPostureIndexForAction(action);
    if (posture >= 0) {
        const int base = NormalActionBase(action);
        return ClampIndex(action - base, 4);
    }
    if (action >= ACTION_6A && action <= ACTION_6D) {
        return ClampIndex(action - ACTION_6A, 4);
    }
    if (action >= ACTION_4A && action <= ACTION_4D) {
        return ClampIndex(action - ACTION_4A, 4);
    }
    if (action == ACTION_JUMP) {
        return ClampIndex(strength, 3);
    }
    if (ActionUsesButtonStrength(action)) {
        return ClampIndex(strength, 4);
    }
    return 0;
}

int MapPostureAndButtonToAction(int postureIdx, int buttonIdx) {
    buttonIdx = ClampIndex(buttonIdx, 4);
    switch (postureIdx) {
        case 0: return buttonIdx == 0 ? ACTION_5A : (buttonIdx == 1 ? ACTION_5B : (buttonIdx == 2 ? ACTION_5C : ACTION_5D));
        case 1: return buttonIdx == 0 ? ACTION_2A : (buttonIdx == 1 ? ACTION_2B : (buttonIdx == 2 ? ACTION_2C : ACTION_2D));
        case 2: return buttonIdx == 0 ? ACTION_JA : (buttonIdx == 1 ? ACTION_JB : (buttonIdx == 2 ? ACTION_JC : ACTION_JD));
        default: return ACTION_5A;
    }
}

int MapMotionIndexToAction(int motionIdx, int buttonIdx) {
    motionIdx = ClampIndex(motionIdx, kTriggerMotionCount);
    buttonIdx = ClampIndex(buttonIdx, 4);
    switch (motionIdx) {
        case 17: return ACTION_JUMP;
        case 18: return ACTION_BACKDASH;
        case 19: return ACTION_FORWARD_DASH;
        case 20: return ACTION_BLOCK;
        case 21: return ACTION_FINAL_MEMORY;
        case 3: return ACTION_QCF;
        case 4: return ACTION_DP;
        case 5: return ACTION_QCB;
        case 6: return ACTION_421;
        case 7: return ACTION_SUPER1;
        case 8: return ACTION_SUPER2;
        case 9: return ACTION_236236;
        case 10: return ACTION_214214;
        case 11: return ACTION_641236;
        case 12: return ACTION_463214;
        case 13: return ACTION_412;
        case 14: return ACTION_22;
        case 15: return ACTION_4123641236;
        case 16: return ACTION_6321463214;
        case 22: return buttonIdx == 0 ? ACTION_6A : (buttonIdx == 1 ? ACTION_6B : (buttonIdx == 2 ? ACTION_6C : ACTION_6D));
        case 23: return buttonIdx == 0 ? ACTION_4A : (buttonIdx == 1 ? ACTION_4B : (buttonIdx == 2 ? ACTION_4C : ACTION_4D));
        case 24: return ACTION_1X;
        case 25: return ACTION_3X;
        case 26: return ACTION_J2X;
        case 27: return ACTION_J6X;
        case 28: return ACTION_66X;
        case 29: return ACTION_662X;
        case 30: return ACTION_664X;
        case 31: return ACTION_KAORI_RECOIL_DUCK;
        default: return MapPostureAndButtonToAction(motionIdx, buttonIdx);
    }
}

int GetMotionIndexForAction(int action) {
    switch (action) {
        case ACTION_5A: case ACTION_5B: case ACTION_5C: case ACTION_5D: return 0;
        case ACTION_2A: case ACTION_2B: case ACTION_2C: case ACTION_2D: return 1;
        case ACTION_JA: case ACTION_JB: case ACTION_JC: case ACTION_JD: return 2;
        case ACTION_6A: case ACTION_6B: case ACTION_6C: case ACTION_6D: return 22;
        case ACTION_4A: case ACTION_4B: case ACTION_4C: case ACTION_4D: return 23;
        case ACTION_QCF: return 3;
        case ACTION_DP: return 4;
        case ACTION_QCB: return 5;
        case ACTION_421: return 6;
        case ACTION_SUPER1: return 7;
        case ACTION_SUPER2: return 8;
        case ACTION_236236: return 9;
        case ACTION_214214: return 10;
        case ACTION_641236: return 11;
        case ACTION_463214: return 12;
        case ACTION_412: return 13;
        case ACTION_22: return 14;
        case ACTION_4123641236: return 15;
        case ACTION_6321463214: return 16;
        case ACTION_JUMP: return 17;
        case ACTION_BACKDASH: return 18;
        case ACTION_FORWARD_DASH: return 19;
        case ACTION_BLOCK: return 20;
        case ACTION_FINAL_MEMORY: return 21;
        case ACTION_1X: return 24;
        case ACTION_3X: return 25;
        case ACTION_J2X: return 26;
        case ACTION_J6X: return 27;
        case ACTION_66X: return 28;
        case ACTION_662X: return 29;
        case ACTION_664X: return 30;
        case ACTION_KAORI_RECOIL_DUCK: return 31;
        default: return 0;
    }
}

void ApplyMotionToTrigger(int motionIdx, int& action, int& strength) {
    motionIdx = ClampIndex(motionIdx, kTriggerMotionCount);
    const int buttonIdx = ExtractButtonIndex(action, strength);
    action = MapMotionIndexToAction(motionIdx, buttonIdx);
    if (GetPostureIndexForAction(action) >= 0 || motionIdx == 22 || motionIdx == 23) {
        strength = ExtractButtonIndex(action, strength);
    } else if (action == ACTION_JUMP) {
        strength = ClampIndex(strength, 3);
    } else if (ActionUsesButtonStrength(action)) {
        strength = ClampIndex(strength, 4);
    } else {
        strength = 0;
    }
}

enum class TriggerButtonMode {
    Hidden = 0,
    NoneLabel,
    Abcd,
    JumpDir,
    FdFollowup
};

TriggerButtonMode GetTriggerButtonMode(int action) {
    if (action == ACTION_BLOCK || action == ACTION_BACKDASH ||
        action == ACTION_FINAL_MEMORY ||
        action == ACTION_KAORI_RECOIL_DUCK) {
        return TriggerButtonMode::NoneLabel;
    }
    if (action == ACTION_JUMP) return TriggerButtonMode::JumpDir;
    if (action == ACTION_FORWARD_DASH) return TriggerButtonMode::FdFollowup;
    if (GetPostureIndexForAction(action) >= 0) return TriggerButtonMode::Abcd;
    if (action >= ACTION_6A && action <= ACTION_4D) return TriggerButtonMode::Abcd;
    if (ActionUsesButtonStrength(action)) return TriggerButtonMode::Abcd;
    return TriggerButtonMode::NoneLabel;
}

void ApplyTriggerButtonIndex(int& action, int& strength, int* dashFollowup, TriggerButtonMode mode, int idx) {
    switch (mode) {
        case TriggerButtonMode::Abcd: {
            idx = ClampIndex(idx, 4);
            const int posture = GetPostureIndexForAction(action);
            if (posture >= 0) {
                action = MapPostureAndButtonToAction(posture, idx);
                strength = idx;
            } else if (action >= ACTION_6A && action <= ACTION_6D) {
                action = idx == 0 ? ACTION_6A : (idx == 1 ? ACTION_6B : (idx == 2 ? ACTION_6C : ACTION_6D));
                strength = idx;
            } else if (action >= ACTION_4A && action <= ACTION_4D) {
                action = idx == 0 ? ACTION_4A : (idx == 1 ? ACTION_4B : (idx == 2 ? ACTION_4C : ACTION_4D));
                strength = idx;
            } else {
                strength = idx;
            }
            break;
        }
        case TriggerButtonMode::JumpDir:
            strength = ClampIndex(idx, 3);
            break;
        case TriggerButtonMode::FdFollowup:
            if (dashFollowup) {
                *dashFollowup = ClampIndex(idx, 7);
                forwardDashFollowup.store(*dashFollowup);
            }
            break;
        default:
            break;
    }
}

int GetTriggerButtonIndex(int action, int strength, int dashFollowup, TriggerButtonMode mode) {
    switch (mode) {
        case TriggerButtonMode::Abcd:
            return ExtractButtonIndex(action, strength);
        case TriggerButtonMode::JumpDir:
            return ClampIndex(strength, 3);
        case TriggerButtonMode::FdFollowup:
            return ClampIndex(dashFollowup, 7);
        default:
            return 0;
    }
}

int TriggerButtonChoiceCount(TriggerButtonMode mode) {
    switch (mode) {
        case TriggerButtonMode::Abcd: return 4;
        case TriggerButtonMode::JumpDir: return 3;
        case TriggerButtonMode::FdFollowup: return 7;
        default: return 0;
    }
}

const char* TriggerButtonChoiceLabel(TriggerButtonMode mode, int idx) {
    switch (mode) {
        case TriggerButtonMode::Abcd:
            return kStrengthChoices[ClampIndex(idx, 4)];
        case TriggerButtonMode::JumpDir:
            return kJumpDirChoicesAuto[ClampIndex(idx, 3)];
        case TriggerButtonMode::FdFollowup:
            return kFdFollowupChoices[ClampIndex(idx, 7)];
        default:
            return "(NONE)";
    }
}

struct TriggerActionChoice {
    int motionIdx;
    int action;
    int strength;
    int dashFollowup; // -1 when the action does not use forward-dash follow-up.
    int category;
};

enum TriggerActionCategory {
    kTriggerActionCategoryNormals = 0,
    kTriggerActionCategoryCommandNormals,
    kTriggerActionCategorySpecials,
    kTriggerActionCategorySupers,
    kTriggerActionCategoryMovement,
    kTriggerActionCategoryDefense,
    kTriggerActionCategoryCount
};

const char* const kTriggerActionCategoryChoices[kTriggerActionCategoryCount] = {
    "NORMALS",
    "COMMAND NORMALS",
    "SPECIALS",
    "SUPERS",
    "MOVEMENT",
    "DEFENSE"
};

int ActionPoolCategoryForIndex(int index) {
    if (index >= 0 && index <= 11) return kTriggerActionCategoryNormals;
    if (index >= 12 && index <= 19) return kTriggerActionCategoryCommandNormals;
    // Kano's selectable 22 entry is 22B and is a super; other characters use this family as a special.
    if (index >= 40 && index <= 43 &&
        EffectiveAutoActionCharId() == CHAR_ID_KANO) {
        return kTriggerActionCategorySupers;
    }
    if ((index >= 20 && index <= 47)) return kTriggerActionCategorySpecials;
    if (index >= 48 && index <= 76) return kTriggerActionCategorySupers;
    if (index >= 77 && index <= 81) return kTriggerActionCategoryMovement;
    if (index == CharacterActionCatalog::kPoolKaoriRecoilDuck) {
        return kTriggerActionCategoryMovement;
    }
    if (index >= CharacterActionCatalog::kPool1X) return kTriggerActionCategoryCommandNormals;
    return kTriggerActionCategoryDefense;
}

int kActionPoolCategoryMap[kActionPoolCount] = {};
bool g_actionPoolCategoryMapReady = false;
int g_actionPoolCategoryChar = -999;

void EnsureActionPoolCategoryMap() {
    const int charId = EffectiveAutoActionCharId();
    if (g_actionPoolCategoryMapReady && g_actionPoolCategoryChar == charId) return;
    for (int i = 0; i < kActionPoolCount; ++i) {
        kActionPoolCategoryMap[i] = ActionPoolCategoryForIndex(i);
    }
    g_actionPoolCategoryChar = charId;
    g_actionPoolCategoryMapReady = true;
}

void SetConcretePoolBit(uint64_t& lo, uint64_t& hi, int index) {
    if (index < 0 || index >= kActionPoolCount) return;
    if (index < 64) {
        lo |= (1ull << index);
    } else {
        hi |= (1ull << (index - 64));
    }
}

bool ConcretePoolBitSet(uint64_t lo, uint64_t hi, int index) {
    if (index < 0 || index >= kActionPoolCount) return false;
    if (index < 64) return ((lo >> index) & 1ull) != 0;
    return ((hi >> (index - 64)) & 1ull) != 0;
}

int ClampPoolDelay(int value) {
    if (value < 0) return 0;
    if (value > 60) return 60;
    return value;
}

int* PoolDelayArrayForTriggerIndex(int triggerIdx) {
    auto& d = ImGuiGui::guiState.localData;
    switch (triggerIdx) {
        case 0: return d.afterBlockActionPoolDelays;
        case 1: return d.onWakeupActionPoolDelays;
        case 2: return d.afterHitstunActionPoolDelays;
        case 3: return d.afterAirtechActionPoolDelays;
        case 4: return d.onRGActionPoolDelays;
        default: return nullptr;
    }
}

int DefaultDelayForTriggerIndex(int triggerIdx) {
    const auto& d = ImGuiGui::guiState.localData;
    switch (triggerIdx) {
        case 0: return ClampPoolDelay(d.delayAfterBlock);
        case 1: return ClampPoolDelay(d.delayOnWakeup);
        case 2: return ClampPoolDelay(d.delayAfterHitstun);
        case 3: return ClampPoolDelay(d.delayAfterAirtech);
        case 4: return ClampPoolDelay(d.delayOnRG);
        default: return 0;
    }
}

void CurrentPoolMaskForTriggerIndex(int triggerIdx, uint64_t& lo, uint64_t& hi) {
    switch (triggerIdx) {
        case 0: lo = g_poolMaskABLo; hi = g_poolMaskABHi; return;
        case 1: lo = g_poolMaskWULo; hi = g_poolMaskWUHi; return;
        case 2: lo = g_poolMaskAHLo; hi = g_poolMaskAHHi; return;
        case 3: lo = g_poolMaskAALo; hi = g_poolMaskAAHi; return;
        case 4: lo = g_poolMaskRGLo; hi = g_poolMaskRGHi; return;
        default: lo = 0; hi = 0; return;
    }
}

bool PoolEnabledForTriggerIndex(int triggerIdx) {
    switch (triggerIdx) {
        case 0: return g_useMaskAB;
        case 1: return g_useMaskWU;
        case 2: return g_useMaskAH;
        case 3: return g_useMaskAA;
        case 4: return g_useMaskRG;
        default: return false;
    }
}

int EffectivePoolDelayForIndex(const int* delays, int index, int defaultDelay) {
    if (!delays || index < 0 || index >= MAX_ACTION_POOL_OPTIONS) return defaultDelay;
    return delays[index] < 0 ? defaultDelay : ClampPoolDelay(delays[index]);
}

bool NormalizeSelectedPoolDelaysForTrigger(int triggerIdx,
                                           uint64_t maskLo, uint64_t maskHi,
                                           bool initializeMissing) {
    int* delays = PoolDelayArrayForTriggerIndex(triggerIdx);
    if (!delays) return false;

    bool changed = false;
    const int defaultDelay = DefaultDelayForTriggerIndex(triggerIdx);
    for (int i = 0; i < kActionPoolCount && i < MAX_ACTION_POOL_OPTIONS; ++i) {
        if (!ConcretePoolBitSet(maskLo, maskHi, i)) continue;
        if (delays[i] < 0) {
            if (initializeMissing) {
                delays[i] = defaultDelay;
                changed = true;
            }
            continue;
        }
        const int clamped = ClampPoolDelay(delays[i]);
        if (clamped != delays[i]) {
            delays[i] = clamped;
            changed = true;
        }
    }
    return changed;
}

void InitializeAddedPoolDelaysForTrigger(int triggerIdx,
                                         uint64_t oldLo, uint64_t oldHi,
                                         uint64_t newLo, uint64_t newHi) {
    int* delays = PoolDelayArrayForTriggerIndex(triggerIdx);
    if (!delays) return;

    const int defaultDelay = DefaultDelayForTriggerIndex(triggerIdx);
    for (int i = 0; i < kActionPoolCount && i < MAX_ACTION_POOL_OPTIONS; ++i) {
        if (!ConcretePoolBitSet(newLo, newHi, i) ||
            ConcretePoolBitSet(oldLo, oldHi, i)) {
            continue;
        }
        if (delays[i] < 0) {
            delays[i] = defaultDelay;
        }
    }
    NormalizeSelectedPoolDelaysForTrigger(triggerIdx, newLo, newHi, true);
}

int ConcretePoolIndexForLegacyMotion(int motionIdx, int strength) {
    strength = ClampIndex(strength, 4);
    switch (motionIdx) {
        case 0:  return 0 + strength;   // 5A-D
        case 1:  return 4 + strength;   // 2A-D
        case 2:  return 8 + strength;   // jA-D
        case 3:  return 20 + strength;  // 236A-D
        case 4:  return 24 + strength;  // 623A-D
        case 5:  return 28 + strength;  // 214A-D
        case 6:  return 32 + strength;  // 421A-D
        case 7:  return 44 + strength;  // 41236A-D
        case 8:  return 48 + strength;  // 2141236A-D
        case 9:  return 52 + strength;  // 236236A-D
        case 10: return 56 + strength;  // 214214A-D
        case 11: return 60 + strength;  // 641236A-D
        case 12: return 64 + strength;  // 463214A-D
        case 13: return 36 + strength;  // 412A-D
        case 14: return 40 + strength;  // 22A-D
        case 15: return 68 + strength;  // 4123641236A-D
        case 16: return 72 + strength;  // 6321463214A-D
        case 17: return 77 + ClampIndex(strength, 3); // jump direction
        case 18: return 80;             // backdash
        case 19: return 81;             // forward dash
        case 20: return 82;             // block
        case 21: return 76;             // Final Memory
        case 22: return 12 + strength;  // 6A-D
        case 23: return 16 + strength;  // 4A-D
        default: return -1;
    }
}

void ExpandLegacyActionPoolMask(uint32_t legacyMask, int strength, uint64_t& lo, uint64_t& hi) {
    lo = 0;
    hi = 0;
    for (int bit = 0; bit < 24; ++bit) {
        if ((legacyMask & (1u << bit)) == 0) continue;
        SetConcretePoolBit(lo, hi, ConcretePoolIndexForLegacyMotion(bit, strength));
    }
}

const char* FormatActionPoolChoice(const Row&, int choiceValue) {
    choiceValue = ClampIndex(choiceValue, kActionPoolCount);
    return kActionPoolNames[choiceValue];
}

const char* FormatActionPoolChoiceHelp(const Row&, int choiceValue) {
    static char s_buf[256];
    choiceValue = ClampIndex(choiceValue, kActionPoolCount);
    const char* name = kActionPoolNames[choiceValue];
    const int category = ActionPoolCategoryForIndex(choiceValue);
    const char* kind = "response";
    switch (category) {
        case kTriggerActionCategoryNormals:
        case kTriggerActionCategoryCommandNormals:
            kind = "normal";
            break;
        case kTriggerActionCategorySpecials:
            kind = "special";
            break;
        case kTriggerActionCategorySupers:
            kind = "super";
            break;
        case kTriggerActionCategoryMovement:
            kind = "movement option";
            break;
        case kTriggerActionCategoryDefense:
            kind = "defensive response";
            break;
        default:
            break;
    }
    _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE,
                I18n::Tr("Adds %s as an exact %s; only checked entries can be rolled by Random Pool."),
                I18n::Tr(name), I18n::Tr(kind));
    return s_buf;
}

const char* FormatActionPoolSummary(const Row& row) {
    return FormatMaskSelectionSummary(row, 3);
}

constexpr int kMaxTriggerActionChoices = 96;
TriggerActionChoice g_triggerActionChoices[kMaxTriggerActionChoices];
char g_triggerActionLabels[kMaxTriggerActionChoices][48];
char g_triggerActionShortLabels[kMaxTriggerActionChoices][32];
const char* g_triggerActionChoiceArr[kMaxTriggerActionChoices];
int g_triggerActionCategoryMap[kMaxTriggerActionChoices];
int g_triggerActionChoiceCount = 0;
int g_triggerActionCatalogChar = -999;

int EffectiveAutoActionCharId() {
    const auto& d = ImGuiGui::guiState.localData;
    const bool p1 = ResolveAutoActionTargetPlayer() == 1;
    const char* name = p1 ? d.p1CharName : d.p2CharName;
    if (!name || name[0] == '\0') return -1;
    return p1 ? d.p1CharID : d.p2CharID;
}

int* PoolChargeArrayForTriggerIndex(int triggerIdx) {
    auto& d = ImGuiGui::guiState.localData;
    switch (triggerIdx) {
        case 0: return d.afterBlockActionPoolCharges;
        case 1: return d.onWakeupActionPoolCharges;
        case 2: return d.afterHitstunActionPoolCharges;
        case 3: return d.afterAirtechActionPoolCharges;
        case 4: return d.onRGActionPoolCharges;
        default: return nullptr;
    }
}

bool FilterActionPoolChoice(const Row&, int choiceValue) {
    return CharacterActionCatalog::IsPoolIndexAvailable(
        EffectiveAutoActionCharId(), choiceValue);
}

bool MotionChoiceAvailable(int charId, int motionIdx) {
    if (motionIdx >= 0 && motionIdx <= 2) return true;
    if (motionIdx == 22 || motionIdx == 23) {
        const int first = motionIdx == 22 ? ACTION_6A : ACTION_4A;
        for (int strength = 0; strength < 4; ++strength) {
            if (CharacterActionCatalog::IsAvailable(
                    charId, first + strength, strength)) return true;
        }
        return false;
    }
    const int action = MapMotionIndexToAction(motionIdx, 0);
    return CharacterActionCatalog::AnyAvailable(charId, action);
}

int AvailableStrengthForMotion(int charId, int motionIdx, int preferred) {
    preferred = ClampIndex(preferred, 4);
    if (motionIdx == 22 || motionIdx == 23) {
        const int first = motionIdx == 22 ? ACTION_6A : ACTION_4A;
        if (CharacterActionCatalog::IsAvailable(
                charId, first + preferred, preferred)) return preferred;
        for (int strength = 0; strength < 4; ++strength) {
            if (CharacterActionCatalog::IsAvailable(
                    charId, first + strength, strength)) return strength;
        }
        return 0;
    }
    return CharacterActionCatalog::FirstAvailableStrength(
        charId, MapMotionIndexToAction(motionIdx, preferred), preferred);
}

int NextAvailableStrengthForMotion(int charId, int motionIdx,
                                   int current, int direction) {
    current = ClampIndex(current, 4);
    for (int step = 0; step < 4; ++step) {
        current = (current + (direction > 0 ? 1 : 3)) & 3;
        if (motionIdx == 22 || motionIdx == 23) {
            const int first = motionIdx == 22 ? ACTION_6A : ACTION_4A;
            if (CharacterActionCatalog::IsAvailable(
                    charId, first + current, current)) return current;
        } else if (CharacterActionCatalog::IsAvailable(
                       charId, MapMotionIndexToAction(motionIdx, current),
                       current)) {
            return current;
        }
    }
    return AvailableStrengthForMotion(charId, motionIdx, current);
}

int TriggerActionCategoryForMotion(int motionIdx) {
    switch (motionIdx) {
        case 0:
        case 1:
        case 2:
            return kTriggerActionCategoryNormals;
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
            return kTriggerActionCategoryCommandNormals;
        case 31:
            return kTriggerActionCategoryMovement;
        case 3:
        case 4:
        case 5:
        case 6:
        case 13:
        case 14:
        case 7: // 41236 is an ordinary special input family.
            return kTriggerActionCategorySpecials;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 15:
        case 16:
        case 21:
            return kTriggerActionCategorySupers;
        default:
            return kTriggerActionCategoryMovement;
    }
}

const char* TriggerActionLabelForMotion(int motionIdx) {
    switch (motionIdx) {
        case 0:  return "STANDING";
        case 1:  return "CROUCHING";
        case 2:  return "AIR";
        case 3:  return "QCF (236)";
        case 4:  return "DP (623)";
        case 5:  return "QCB (214)";
        case 6:  return "421";
        case 7:  return "41236";
        case 8:  return "2141236";
        case 9:  return "236236";
        case 10: return "214214";
        case 11: return "641236";
        case 12: return "463214";
        case 13: return "412";
        case 14: return "22";
        case 15: return "4123641236";
        case 16: return "6321463214";
        case 17: return "JUMP";
        case 18: return "BACKDASH";
        case 19: return "FORWARD DASH";
        case 20: return "BLOCK";
        case 21: return "FINAL MEMORY";
        case 22: return "FORWARD NORMAL";
        case 23: return "BACK NORMAL";
        case 24: return "DOWN-BACK NORMAL";
        case 25: return "DOWN-FORWARD NORMAL";
        case 26: return "AIR DOWN NORMAL";
        case 27: return "AIR FORWARD NORMAL";
        case 28: return "DASH NORMAL";
        case 29: return "DASH LOW";
        case 30: return "DASH-BACK NORMAL";
        case 31: return "RECOIL DUCKING (44~66)";
        default: return "";
    }
}

void AddTriggerActionChoice(int category,
                            int motionIdx,
                            const char* shortLabel,
                            int action,
                            int strength,
                            int dashFollowup) {
    if (g_triggerActionChoiceCount >= kMaxTriggerActionChoices) return;
    const int i = g_triggerActionChoiceCount++;
    category = ClampIndex(category, kTriggerActionCategoryCount);
    motionIdx = ClampIndex(motionIdx, kTriggerMotionCount);
    g_triggerActionChoices[i] = { motionIdx, action, strength, dashFollowup, category };
    g_triggerActionCategoryMap[i] = category;
    strncpy_s(g_triggerActionShortLabels[i], sizeof(g_triggerActionShortLabels[i]), shortLabel, _TRUNCATE);
    strncpy_s(g_triggerActionLabels[i], sizeof(g_triggerActionLabels[i]), shortLabel, _TRUNCATE);
    g_triggerActionChoiceArr[i] = g_triggerActionLabels[i];
}

void AddMotionActionChoice(int motionIdx) {
    AddTriggerActionChoice(TriggerActionCategoryForMotion(motionIdx),
                           motionIdx,
                           TriggerActionLabelForMotion(motionIdx),
                           MapMotionIndexToAction(motionIdx, 0),
                           0,
                           -1);
}

void RefreshTriggerActionChoices() {
    const int charId = EffectiveAutoActionCharId();
    if (g_triggerActionChoiceCount > 0 && g_triggerActionCatalogChar == charId) return;
    g_triggerActionChoiceCount = 0;
    g_triggerActionCatalogChar = charId;

    const int normalMotions[] = { 0, 1, 2 };
    for (int motionIdx : normalMotions) {
        AddMotionActionChoice(motionIdx);
    }

    const int commandNormalMotions[] = { 22, 23 };
    for (int motionIdx : commandNormalMotions) {
        if (MotionChoiceAvailable(charId, motionIdx)) AddMotionActionChoice(motionIdx);
    }

    const int extendedCommandNormalMotions[] = { 24, 25, 26, 27, 28, 29, 30 };
    for (int motionIdx : extendedCommandNormalMotions) {
        if (MotionChoiceAvailable(charId, motionIdx)) AddMotionActionChoice(motionIdx);
    }

    const int specialMotions[] = { 3, 4, 5, 6, 7, 13, 14 };
    for (int motionIdx : specialMotions) {
        if (!MotionChoiceAvailable(charId, motionIdx)) continue;
        if (motionIdx == 14 && charId == CHAR_ID_KANO) {
            AddTriggerActionChoice(kTriggerActionCategorySupers, motionIdx,
                                   TriggerActionLabelForMotion(motionIdx),
                                   MapMotionIndexToAction(motionIdx, 0),
                                   0, -1);
        } else {
            AddMotionActionChoice(motionIdx);
        }
    }

    const int superMotions[] = { 8, 9, 10, 11, 12, 15, 16 };
    for (int motionIdx : superMotions) {
        if (MotionChoiceAvailable(charId, motionIdx)) AddMotionActionChoice(motionIdx);
    }

    if (MotionChoiceAvailable(charId, 21)) {
        AddTriggerActionChoice(kTriggerActionCategorySupers, 21,
                               "FINAL MEMORY", ACTION_FINAL_MEMORY, 0, -1);
    }

    AddTriggerActionChoice(kTriggerActionCategoryMovement, 17, "JUMP", ACTION_JUMP, 0, -1);
    AddTriggerActionChoice(kTriggerActionCategoryMovement, 18, "BACKDASH", ACTION_BACKDASH, 0, -1);
    AddTriggerActionChoice(kTriggerActionCategoryMovement, 19, "FORWARD DASH", ACTION_FORWARD_DASH, 0, 0);
    if (MotionChoiceAvailable(charId, 31)) {
        AddTriggerActionChoice(kTriggerActionCategoryMovement, 31,
                               "RECOIL DUCKING (44~66)",
                               ACTION_KAORI_RECOIL_DUCK, 0, -1);
    }
    AddTriggerActionChoice(kTriggerActionCategoryDefense, 20, "BLOCK", ACTION_BLOCK, 0, -1);
}

bool NormalizeTriggerActionForCatalog(int& action, int& strength) {
    const int charId = EffectiveAutoActionCharId();
    if (!CharacterActionCatalog::IsKnownCharacter(charId)) return false;

    const int catalogStrength =
        (action >= ACTION_5A && action <= ACTION_4D)
            ? (action & 3)
            : ClampIndex(strength, 4);
    if (CharacterActionCatalog::IsAvailable(
            charId, action, catalogStrength)) {
        return false;
    }

    const int oldAction = action;
    const int oldStrength = strength;
    const int motionIdx = GetMotionIndexForAction(action);
    if (motionIdx >= 0 && MotionChoiceAvailable(charId, motionIdx) &&
        GetTriggerButtonMode(action) == TriggerButtonMode::Abcd) {
        strength = AvailableStrengthForMotion(charId, motionIdx, strength);
        action = MapMotionIndexToAction(motionIdx, strength);
    } else {
        action = ACTION_5A;
        strength = 0;
    }
    return action != oldAction || strength != oldStrength;
}

int GetTriggerActionChoiceIndex(int action, int strength) {
    RefreshTriggerActionChoices();
    if (g_triggerActionChoiceCount <= 0) return 0;

    const int motionIdx = GetMotionIndexForAction(action);

    for (int i = 0; i < g_triggerActionChoiceCount; ++i) {
        const TriggerActionChoice& choice = g_triggerActionChoices[i];
        if (choice.motionIdx != motionIdx) continue;
        return i;
    }
    return 0;
}

void ApplyTriggerActionChoiceIndex(int selectedIdx, int& action, int& strength) {
    RefreshTriggerActionChoices();
    if (g_triggerActionChoiceCount <= 0) return;
    selectedIdx = ClampIndex(selectedIdx, g_triggerActionChoiceCount);
    const TriggerActionChoice& choice = g_triggerActionChoices[selectedIdx];
    const TriggerButtonMode mode = GetTriggerButtonMode(choice.action);
    switch (mode) {
        case TriggerButtonMode::Abcd: {
            const int buttonIdx = AvailableStrengthForMotion(
                EffectiveAutoActionCharId(), choice.motionIdx, strength);
            action = MapMotionIndexToAction(choice.motionIdx, buttonIdx);
            strength = buttonIdx;
            break;
        }
        case TriggerButtonMode::JumpDir:
            action = choice.action;
            strength = ClampIndex(strength, 3);
            break;
        case TriggerButtonMode::FdFollowup:
            action = choice.action;
            strength = 0;
            g_mirrorFwdDashFollowup = ClampIndex(g_mirrorFwdDashFollowup, 7);
            forwardDashFollowup.store(g_mirrorFwdDashFollowup);
            break;
        default:
            action = choice.action;
            strength = choice.strength;
            break;
    }
}

const char* FormatTriggerActionChoiceLabel(int choiceIdx,
                                           int strength,
                                           int dashFollowup,
                                           bool hideNoDashFollowup) {
    if (g_triggerActionChoiceCount <= 0) return "?";
    choiceIdx = ClampIndex(choiceIdx, g_triggerActionChoiceCount);
    const TriggerActionChoice& choice = g_triggerActionChoices[choiceIdx];
    const char* baseLabel = g_triggerActionShortLabels[choiceIdx];
    const TriggerButtonMode mode = GetTriggerButtonMode(choice.action);
    static char s_buf[64];
    switch (mode) {
        case TriggerButtonMode::Abcd:
            _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE, "%s %s",
                        baseLabel,
                        TriggerButtonChoiceLabel(mode, ClampIndex(strength, 4)));
            return s_buf;
        case TriggerButtonMode::JumpDir:
            _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE, "%s %s",
                        baseLabel,
                        TriggerButtonChoiceLabel(mode, ClampIndex(strength, 3)));
            return s_buf;
        case TriggerButtonMode::FdFollowup:
            dashFollowup = ClampIndex(dashFollowup, 7);
            if (hideNoDashFollowup && dashFollowup == 0) return baseLabel;
            _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE, "%s > %s",
                        baseLabel,
                        TriggerButtonChoiceLabel(mode, dashFollowup));
            return s_buf;
        default:
            return baseLabel;
    }
}

const char* FormatTriggerActionChoiceRow(const Row& row) {
    RefreshTriggerActionChoices();
    if (!row.choiceIdxPtr || g_triggerActionChoiceCount <= 0) return "?";
    int idx = ClampIndex(*row.choiceIdxPtr, g_triggerActionChoiceCount);
    const int action = row.choice2IdxPtr ? *row.choice2IdxPtr : g_triggerActionChoices[idx].action;
    const int strength = row.intPtr ? *row.intPtr : g_triggerActionChoices[idx].strength;
    if (row.choice2IdxPtr) {
        idx = GetTriggerActionChoiceIndex(action, strength);
    }

    return FormatTriggerActionChoiceLabel(idx, strength, g_mirrorFwdDashFollowup, true);
}

const char* FormatTriggerActionPopupChoice(const Row& row, int choiceValue) {
    RefreshTriggerActionChoices();
    if (g_triggerActionChoiceCount <= 0) return "?";
    choiceValue = ClampIndex(choiceValue, g_triggerActionChoiceCount);
    const TriggerActionChoice& choice = g_triggerActionChoices[choiceValue];
    int strength = row.intPtr ? *row.intPtr : choice.strength;
    if (GetTriggerButtonMode(choice.action) == TriggerButtonMode::Abcd) {
        strength = AvailableStrengthForMotion(
            EffectiveAutoActionCharId(), choice.motionIdx, strength);
    }
    const int dashFollowup = row.choiceVariantPtr ? *row.choiceVariantPtr : g_mirrorFwdDashFollowup;
    return FormatTriggerActionChoiceLabel(choiceValue, strength, dashFollowup, false);
}

bool AdjustTriggerActionPopupChoice(const Row& row, int choiceValue, int direction) {
    if (!row.intPtr || direction == 0) return false;
    RefreshTriggerActionChoices();
    if (g_triggerActionChoiceCount <= 0) return false;
    choiceValue = ClampIndex(choiceValue, g_triggerActionChoiceCount);
    const TriggerActionChoice& choice = g_triggerActionChoices[choiceValue];
    const TriggerButtonMode mode = GetTriggerButtonMode(choice.action);
    const int count = TriggerButtonChoiceCount(mode);
    if (count <= 0) return false;

    switch (mode) {
        case TriggerButtonMode::Abcd:
            *row.intPtr = NextAvailableStrengthForMotion(
                EffectiveAutoActionCharId(), choice.motionIdx,
                *row.intPtr, direction);
            return true;
        case TriggerButtonMode::JumpDir:
            *row.intPtr = (*row.intPtr + direction + count) % count;
            return true;
        case TriggerButtonMode::FdFollowup:
            if (!row.choiceVariantPtr) return false;
            *row.choiceVariantPtr = (*row.choiceVariantPtr + direction + count) % count;
            return true;
        default:
            return false;
    }
}

bool AdjustTriggerActionChoiceRow(const Row& row, int direction) {
    if (!row.choice2IdxPtr || !row.intPtr || direction == 0) return false;
    int action = *row.choice2IdxPtr;
    int strength = *row.intPtr;
    const TriggerButtonMode mode = GetTriggerButtonMode(action);
    const int count = TriggerButtonChoiceCount(mode);
    if (count <= 0) return false;

    int dashFollowup = g_mirrorFwdDashFollowup;
    int idx = GetTriggerButtonIndex(action, strength, dashFollowup, mode);
    if (mode == TriggerButtonMode::Abcd) {
        idx = NextAvailableStrengthForMotion(
            EffectiveAutoActionCharId(), GetMotionIndexForAction(action),
            idx, direction);
    } else {
        idx = (idx + direction + count) % count;
    }

    ApplyTriggerButtonIndex(action, strength, &g_mirrorFwdDashFollowup, mode, idx);
    *row.choice2IdxPtr = action;
    *row.intPtr = strength;
    if (row.choiceIdxPtr) {
        *row.choiceIdxPtr = GetTriggerActionChoiceIndex(action, strength);
    }
    return true;
}

void OnTriggerActionAfterBlock() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyTriggerActionChoiceIndex(g_actionPickIdxAB, d.actionAfterBlock, d.strengthAfterBlock);
    OnAutoApply();
}
void OnTriggerActionOnWakeup() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyTriggerActionChoiceIndex(g_actionPickIdxWU, d.actionOnWakeup, d.strengthOnWakeup);
    OnAutoApply();
}
void OnTriggerActionAfterHitstun() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyTriggerActionChoiceIndex(g_actionPickIdxAH, d.actionAfterHitstun, d.strengthAfterHitstun);
    OnAutoApply();
}
void OnTriggerActionAfterAirtech() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyTriggerActionChoiceIndex(g_actionPickIdxAA, d.actionAfterAirtech, d.strengthAfterAirtech);
    OnAutoApply();
}
void OnTriggerActionOnRG() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyTriggerActionChoiceIndex(g_actionPickIdxRG, d.actionOnRG, d.strengthOnRG);
    OnAutoApply();
}

void OnTriggerMotionAfterBlock() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyMotionToTrigger(g_motionIdxAB, d.actionAfterBlock, d.strengthAfterBlock);
    OnAutoApply();
}
void OnTriggerMotionOnWakeup() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyMotionToTrigger(g_motionIdxWU, d.actionOnWakeup, d.strengthOnWakeup);
    OnAutoApply();
}
void OnTriggerMotionAfterHitstun() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyMotionToTrigger(g_motionIdxAH, d.actionAfterHitstun, d.strengthAfterHitstun);
    OnAutoApply();
}
void OnTriggerMotionAfterAirtech() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyMotionToTrigger(g_motionIdxAA, d.actionAfterAirtech, d.strengthAfterAirtech);
    OnAutoApply();
}
void OnTriggerMotionOnRG() {
    auto& d = ImGuiGui::guiState.localData;
    ApplyMotionToTrigger(g_motionIdxRG, d.actionOnRG, d.strengthOnRG);
    OnAutoApply();
}

bool HideABSingleAction() {
    const auto& d = ImGuiGui::guiState.localData;
    return g_useMaskAB || d.macroSlotAfterBlock > 0;
}
bool HideWUSingleAction() {
    const auto& d = ImGuiGui::guiState.localData;
    return g_useMaskWU || d.macroSlotOnWakeup > 0;
}
bool HideAHSingleAction() {
    const auto& d = ImGuiGui::guiState.localData;
    return g_useMaskAH || d.macroSlotAfterHitstun > 0;
}
bool HideAASingleAction() {
    const auto& d = ImGuiGui::guiState.localData;
    return g_useMaskAA || d.macroSlotAfterAirtech > 0;
}
bool HideRGSingleAction() {
    const auto& d = ImGuiGui::guiState.localData;
    return g_useMaskRG || d.macroSlotOnRG > 0;
}

bool HideABButton() { return HideABSingleAction(); }
bool HideWUButton() { return HideWUSingleAction(); }
bool HideAHButton() { return HideAHSingleAction(); }
bool HideAAButton() { return HideAASingleAction(); }
bool HideRGButton() { return HideRGSingleAction(); }

const char* FormatTriggerButtonRowImpl(const Row& row) {
    if (!row.choiceIdxPtr) return "(NONE)";
    const int action = *row.choiceIdxPtr;
    const int strength = row.choice2IdxPtr ? *row.choice2IdxPtr : 0;
    const int dashFollowup = row.intPtr ? *row.intPtr : forwardDashFollowup.load();
    const TriggerButtonMode mode = GetTriggerButtonMode(action);
    if (mode == TriggerButtonMode::NoneLabel) return "(NONE)";
    const int idx = GetTriggerButtonIndex(action, strength, dashFollowup, mode);
    return TriggerButtonChoiceLabel(mode, idx);
}

bool AdjustTriggerButtonRowImpl(const Row& row, int direction) {
    if (!row.choiceIdxPtr || direction == 0) return false;
    const TriggerButtonMode mode = GetTriggerButtonMode(*row.choiceIdxPtr);
    const int count = TriggerButtonChoiceCount(mode);
    if (count <= 0) return false;

    int strength = row.choice2IdxPtr ? *row.choice2IdxPtr : 0;
    int dashFollowup = row.intPtr ? *row.intPtr : forwardDashFollowup.load();
    int idx = GetTriggerButtonIndex(*row.choiceIdxPtr, strength, dashFollowup, mode);
    idx = (idx + direction + count) % count;

    int action = *row.choiceIdxPtr;
    ApplyTriggerButtonIndex(action, strength, row.intPtr, mode, idx);
    *row.choiceIdxPtr = action;
    if (row.choice2IdxPtr) *row.choice2IdxPtr = strength;
    if (row.intPtr && mode == TriggerButtonMode::FdFollowup) {
        forwardDashFollowup.store(*row.intPtr);
    }
    if (row.onChange) row.onChange();
    return true;
}

bool HideABPool() { return !g_useMaskAB; }
bool HideWUPool() { return !g_useMaskWU; }
bool HideAHPool() { return !g_useMaskAH; }
bool HideAAPool() { return !g_useMaskAA; }
bool HideRGPool() { return !g_useMaskRG; }

bool HideABRegularDelay() { return g_useMaskAB; }
bool HideWURegularDelay() { return g_useMaskWU; }
bool HideAHRegularDelay() { return g_useMaskAH; }
bool HideAARegularDelay() { return g_useMaskAA; }
bool HideRGRegularDelay() { return g_useMaskRG; }

bool HideABCharge() {
    const auto& d = ImGuiGui::guiState.localData;
    return HideABSingleAction() || !ActionSupportsChargeFollowup(d.actionAfterBlock);
}
bool HideWUCharge() {
    const auto& d = ImGuiGui::guiState.localData;
    return HideWUSingleAction() || !ActionSupportsChargeFollowup(d.actionOnWakeup);
}
bool HideAHCharge() {
    const auto& d = ImGuiGui::guiState.localData;
    return HideAHSingleAction() || !ActionSupportsChargeFollowup(d.actionAfterHitstun);
}
bool HideAACharge() {
    const auto& d = ImGuiGui::guiState.localData;
    return HideAASingleAction() || !ActionSupportsChargeFollowup(d.actionAfterAirtech);
}
bool HideRGCharge() {
    const auto& d = ImGuiGui::guiState.localData;
    return HideRGSingleAction() || !ActionSupportsChargeFollowup(d.actionOnRG);
}

constexpr int kMaxMacroSlotEntries = 17;   // 1 "none" + up to 16 slots
static const char* g_macroSlotChoiceArr[kMaxMacroSlotEntries];
static char s_macroSlotLabels[kMaxMacroSlotEntries][32];
static int g_macroSlotChoiceCount = 1;

void RefreshMacroSlotChoices() {
    int slots = MacroController::GetSlotCount();
    if (slots < 0) slots = 0;
    if (slots > kMaxMacroSlotEntries - 1) slots = kMaxMacroSlotEntries - 1;
    g_macroSlotChoiceCount = slots + 1;
    // Index 0 = "NONE" (macroSlot==0 in DisplayData means no macro)
    strncpy_s(s_macroSlotLabels[0], sizeof(s_macroSlotLabels[0]), "NONE", _TRUNCATE);
    g_macroSlotChoiceArr[0] = s_macroSlotLabels[0];
    for (int i = 1; i <= slots; ++i) {
        _snprintf_s(s_macroSlotLabels[i], sizeof(s_macroSlotLabels[i]), _TRUNCATE, I18n::Tr("SLOT %d"), i);
        g_macroSlotChoiceArr[i] = s_macroSlotLabels[i];
    }
}

const char* const kUiFontChoices[2] = { "DEFAULT", "SEGOE UI" };
void OnUiFont() {
    PersistInt("General", "uiFont", MutableSettings().uiFontMode);
}
// Index order matches I18n::Language; each language is named in its own script once active.
const char* const kLanguageChoices[2] = { "ENGLISH", "JAPANESE" };
void OnLanguage() {
    Config::SetSetting("General", "language",
                       MutableSettings().language == I18n::Japanese ? "ja" : "en");
}
void OnPracticeHint() {
    PersistBool("General", "showPracticeEntryHint", MutableSettings().showPracticeEntryHint);
}
void OnSavestateBackendMode() {
    PersistInt("General", "savestateBackendMode", MutableSettings().savestateBackendMode);
}
void OnSavestateLoadCustomPalettes() {
    PersistBool("General", "savestateLoadCustomPalettes", MutableSettings().savestateLoadCustomPalettes);
}
void OnCheckForUpdates() {
    PersistBool("General", "checkForUpdates", MutableSettings().checkForUpdates);
}
void OnBgmVolume() {
    ExtendedConfigBridge::PublishAudioLaneSetting(true, MutableSettings().bgmVolumePercent);
}
void OnSeVolume() {
    ExtendedConfigBridge::PublishAudioLaneSetting(false, MutableSettings().seVolumePercent);
}

const char* FormatPercentRowValue(const Row& row) {
    static char buf[32];
    const int value = row.intPtr ? *row.intPtr : 0;
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d%%", value);
    return buf;
}

const char* ValInterfaceSettings() { return MutableSettings().useCustomMenu ? "CUSTOM" : "IMGUI"; }
const char* ValAudioSettings() {
    static char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "BGM %d / SE %d",
                MutableSettings().bgmVolumePercent,
                MutableSettings().seVolumePercent);
    return buf;
}
const char* ValRecoverySettings()  { return MutableSettings().crRequireBothNeutral ? "NEUTRAL" : "ANY"; }
const char* ValPracticeSettings()  { return "PRACTICE"; }

bool CollisionProjectileOptionsHidden() {
    return !MutableSettings().collisionDisplayProjectileInteractions;
}

bool CollisionHitboxPlayerOptionsHidden() {
    return !MutableSettings().collisionDisplayHitboxes;
}

bool CollisionHurtboxPlayerOptionsHidden() {
    return !MutableSettings().collisionDisplayHurtboxes;
}

bool CollisionPushboxPlayerOptionsHidden() {
    return !MutableSettings().collisionDisplayCollisionBoxes;
}

const char* ValDisplaySettings() {
    static char buf[48];
    const auto& s = MutableSettings();
    const int enabled =
        (s.collisionDisplayHitboxes && s.collisionDisplayP1Hitboxes ? 1 : 0)
        + (s.collisionDisplayHitboxes && s.collisionDisplayP2Hitboxes ? 1 : 0)
        + (s.collisionDisplayHurtboxes && s.collisionDisplayP1Hurtboxes ? 1 : 0)
        + (s.collisionDisplayHurtboxes && s.collisionDisplayP2Hurtboxes ? 1 : 0)
        + (s.collisionDisplayCollisionBoxes && s.collisionDisplayP1CollisionBoxes ? 1 : 0)
        + (s.collisionDisplayCollisionBoxes && s.collisionDisplayP2CollisionBoxes ? 1 : 0)
        + (s.collisionDisplayProjectileInteractions ? 1 : 0);
    if (enabled == 0) {
        return "OFF";
    }
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, I18n::Tr("%d ON / %d%%"), enabled, s.collisionDisplayFillAlphaPercent);
    return buf;
}

Row* BuildDisplayOverlayRows(int& count) {
    static Row s_rows[24];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("HITBOX / COLLISION DISPLAY");
    s_rows[n++] = Toggle("HITBOXES", &s.collisionDisplayHitboxes, OnCollisionDisplayHitboxes);
    s_rows[n++] = Toggle("  P1 HITBOXES", &s.collisionDisplayP1Hitboxes,
                         OnCollisionDisplayP1Hitboxes, nullptr,
                         CollisionHitboxPlayerOptionsHidden);
    s_rows[n++] = Toggle("  P2 HITBOXES", &s.collisionDisplayP2Hitboxes,
                         OnCollisionDisplayP2Hitboxes, nullptr,
                         CollisionHitboxPlayerOptionsHidden);
    s_rows[n++] = Toggle("HURTBOXES", &s.collisionDisplayHurtboxes, OnCollisionDisplayHurtboxes);
    s_rows[n++] = Toggle("  P1 HURTBOXES", &s.collisionDisplayP1Hurtboxes,
                         OnCollisionDisplayP1Hurtboxes, nullptr,
                         CollisionHurtboxPlayerOptionsHidden);
    s_rows[n++] = Toggle("  P2 HURTBOXES", &s.collisionDisplayP2Hurtboxes,
                         OnCollisionDisplayP2Hurtboxes, nullptr,
                         CollisionHurtboxPlayerOptionsHidden);
    s_rows[n++] = Toggle("COLLISION BOXES", &s.collisionDisplayCollisionBoxes, OnCollisionDisplayPushboxes);
    s_rows[n++] = Toggle("  P1 COLLISION BOXES", &s.collisionDisplayP1CollisionBoxes,
                         OnCollisionDisplayP1Pushboxes, nullptr,
                         CollisionPushboxPlayerOptionsHidden);
    s_rows[n++] = Toggle("  P2 COLLISION BOXES", &s.collisionDisplayP2CollisionBoxes,
                         OnCollisionDisplayP2Pushboxes, nullptr,
                         CollisionPushboxPlayerOptionsHidden);
    s_rows[n++] = Toggle("PROJECTILE INTERACTIONS", &s.collisionDisplayProjectileInteractions, OnCollisionDisplayProjectiles);
    Row alpha = IntSlider("BOX FILL ALPHA", &s.collisionDisplayFillAlphaPercent, 0, 100, 1, 10, OnCollisionDisplayAlpha);
    alpha.valueFormatter = FormatPercentRowValue;
    s_rows[n++] = alpha;

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("PROJECTILE SUB-LAYERS");
    s_rows[n++] = Toggle("  PROJECTILE BOXES", &s.collisionDisplayProjectileBoxes,
                         OnCollisionProjectileBoxes, nullptr, CollisionProjectileOptionsHidden);
    s_rows[n++] = Toggle("  ORIGIN / RANGE DOTS", &s.collisionDisplayProjectileOrigins,
                         OnCollisionProjectileOrigins, nullptr, CollisionProjectileOptionsHidden);
    s_rows[n++] = Toggle("  INTERSECTION BOXES", &s.collisionDisplayProjectileIntersections,
                         OnCollisionProjectileIntersections, nullptr, CollisionProjectileOptionsHidden);
    s_rows[n++] = Info("P1 / P2 filters cover that fighter and its projectile boxes. Origin / Range Dots adds the dots that mark projectile anchors and trigger-range centers.");
    s_rows[n++] = Info("Mizuka note display settings are under Character Settings when Mizuka is in the match.");
    s_rows[n++] = Info("Origin dots mark a projectile's anchor / activation point, not the center of its boxes.");

    count = n;
    return s_rows;
}

Row* BuildSettingsInterfaceRows(int& count) {
    static Row s_rows[16];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("INTERFACE");
    s_rows[n++] = Toggle    ("USE CUSTOM MENU",        &s.useCustomMenu,       OnUseCustomMenu);
    s_rows[n++] = FloatNum  ("UI SCALE",               &s.uiScale,      0.70f, 1.50f, 0.05f, 0.10f, "%.2f", OnUiScale);
    s_rows[n++] = ChoicesRow("UI FONT (ADVANCED MENU)", &s.uiFontMode,   kUiFontChoices, 2, OnUiFont);
    s_rows[n] = ChoicesRow("LANGUAGE", &s.language, kLanguageChoices, 2, OnLanguage);
    s_rows[n++].helpText = "Chooses the language of the custom menu and in-game overlays.";
    s_rows[n++] = Toggle    ("PRACTICE OVERLAY HINT",  &s.showPracticeEntryHint, OnPracticeHint);
    s_rows[n++] = Toggle    ("CHECK FOR UPDATES",       &s.checkForUpdates,     OnCheckForUpdates);
    count = n;
    return s_rows;
}

Row* BuildSettingsRecoveryRows(int& count) {
    static Row s_rows[16];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("RECOVERY");
    s_rows[n++] = Toggle ("CR: BOTH NEUTRAL REQD",     &s.crRequireBothNeutral, OnCrRequire);
    s_rows[n++] = IntNum ("CR NEUTRAL DELAY (MS)",     &s.crBothNeutralDelayMs, 0, 5000, 50, 500, OnCrDelay);
    s_rows[n++] = Toggle ("AUTO-FIX HP<=0",            &s.autoFixHPOnNeutral,   OnAutoFixHp);
    s_rows[n++] = Toggle ("FREEZE RF AFTER CR",        &s.freezeRFAfterContRec, OnFreezeRfAfterCr);
    s_rows[n++] = Toggle ("FREEZE RF ONLY NEUTRAL",    &s.freezeRFOnlyWhenNeutral, OnFreezeRfNeutral);
    count = n;
    return s_rows;
}

Row* BuildSettingsAudioRows(int& count) {
    static Row s_rows[8];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("AUDIO");
    Row bgmVolume = IntSlider("BGM VOLUME", &s.bgmVolumePercent, 0, 100, 1, 10, OnBgmVolume);
    bgmVolume.valueFormatter = FormatPercentRowValue;
    s_rows[n++] = bgmVolume;
    Row seVolume = IntSlider("SE VOLUME", &s.seVolumePercent, 0, 100, 1, 10, OnSeVolume);
    seVolume.valueFormatter = FormatPercentRowValue;
    s_rows[n++] = seVolume;
    s_rows[n++] = Info("100% leaves the game's own volume untouched.");
    count = n;
    return s_rows;
}

Row* BuildSettingsPracticeRows(int& count) {
    static Row s_rows[8];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("PRACTICE");
    s_rows[n++] = IntNum ("AUTO-BLOCK TIMEOUT (MS)",   &s.autoBlockNeutralTimeoutMs, 0, 60000, 500, 5000, OnAutoBlockTimeout);
    count = n;
    return s_rows;
}

Row* BuildSettingsGeneralRows(int& count) {
    static Row s_rows[20];
    int n = 0;

    s_rows[n++] = Header("GENERAL MENUS");
    s_rows[n++] = Submenu("INTERFACE", "INTERFACE", BuildSettingsInterfaceRows, ValInterfaceSettings);
    s_rows[n++] = Submenu("DISPLAY",   "DISPLAY OVERLAYS", BuildDisplayOverlayRows, ValDisplaySettings);
    s_rows[n++] = Submenu("AUDIO",     "AUDIO",     BuildSettingsAudioRows,     ValAudioSettings);
    s_rows[n++] = Submenu("RECOVERY",  "RECOVERY",  BuildSettingsRecoveryRows,  ValRecoverySettings);
    s_rows[n++] = Submenu("PRACTICE",  "PRACTICE",  BuildSettingsPracticeRows,  ValPracticeSettings);
    s_rows[n++] = Spacer();
    s_rows[n++] = Info(CurrentConfigPathInfo());
    s_rows[n++] = Action ("SAVE ALL TO DISK",          SaveSettingsToDisk);
    s_rows[n++] = Action ("RELOAD FROM DISK",          ReloadSettingsFromDisk);

    count = n;
    return s_rows;
}

// SETTINGS / HOTKEYS
const char* HotkeyNameValue(int vk) {
    static char buffers[8][64];
    static int next = 0;
    char* buf = buffers[next++ & 7];
    if (vk < 0) {
        _snprintf_s(buf, sizeof(buffers[0]), _TRUNCATE, "DISABLED");
    } else {
        _snprintf_s(buf, sizeof(buffers[0]), _TRUNCATE, "%s",
                    Config::GetKeyName(vk).c_str());
    }
    return buf;
}

const char* HotkeyCodeValue(int vk) {
    static char buffers[8][96];
    static int next = 0;
    char* buf = buffers[next++ & 7];
    if (vk < 0) {
        _snprintf_s(buf, sizeof(buffers[0]), _TRUNCATE, "DISABLED");
    } else {
        _snprintf_s(buf, sizeof(buffers[0]), _TRUNCATE, "0x%X  %s", vk, Config::GetKeyName(vk).c_str());
    }
    return buf;
}

void BindHotkey(const char* title, int* field, const char* key) {
    OpenKeybind(title, field, "Hotkeys", key);
}

void BindFooterHotkey(const char* title, int* field, const char* key) {
    OpenKeybind(title, field, "Hotkeys", key, true);
}

void BindTeleport()       { auto& s = MutableSettings(); BindHotkey("TELEPORT",        &s.teleportKey,           "TeleportKey"); }
void BindSavePosition()   { auto& s = MutableSettings(); BindHotkey("SAVE POSITION",   &s.recordKey,             "RecordKey"); }
void BindToggleStats()    { auto& s = MutableSettings(); BindHotkey("TOGGLE STATS",    &s.toggleTitleKey,        "ToggleTitleKey"); }
void BindResetCounter()   { auto& s = MutableSettings(); BindHotkey("RESET COUNTER",   &s.resetFrameCounterKey,  "ResetFrameCounterKey"); }
void BindSavestateSave()  { auto& s = MutableSettings(); BindHotkey("SAVESTATE SAVE",  &s.savestateSaveKey,      "SavestateSaveKey"); }
void BindSavestateLoad()  { auto& s = MutableSettings(); BindHotkey("SAVESTATE LOAD",  &s.savestateLoadKey,      "SavestateLoadKey"); }
void BindSwitchPlayers()  { auto& s = MutableSettings(); BindHotkey("SWITCH PLAYERS",  &s.switchPlayersKey,      "SwitchPlayersKey"); }
void BindMacroRecord()    { auto& s = MutableSettings(); BindHotkey("MACRO RECORD",    &s.macroRecordKey,        "MacroRecordKey"); }
void BindMacroPlay()      { auto& s = MutableSettings(); BindHotkey("MACRO PLAY",      &s.macroPlayKey,          "MacroPlayKey"); }
void BindMacroSlot()      { auto& s = MutableSettings(); BindHotkey("MACRO NEXT SLOT", &s.macroSlotKey,          "MacroSlotKey"); }
void BindUiAccept()       { auto& s = MutableSettings(); BindFooterHotkey("UI ACCEPT",  &s.uiAcceptKey,          "UIAcceptKey"); }
void BindUiRefresh()      { auto& s = MutableSettings(); BindFooterHotkey("UI REFRESH", &s.uiRefreshKey,         "UIRefreshKey"); }
void BindUiExit()         { auto& s = MutableSettings(); BindFooterHotkey("UI EXIT",    &s.uiExitKey,            "UIExitKey"); }
void BindFramestepPause() { auto& s = MutableSettings(); BindHotkey("FRAMESTEP PAUSE", &s.framestepPauseKey,     "FramestepPauseKey"); }
void BindFramestepStep()  { auto& s = MutableSettings(); BindHotkey("FRAMESTEP STEP",  &s.framestepStepKey,      "FramestepStepKey"); }
void BindSwapCustom()     { auto& s = MutableSettings(); BindHotkey("SWAP CUSTOM KEY", &s.swapCustomKey,         "SwapCustomKey"); }
void BindMenuKey()        { auto& s = MutableSettings(); BindHotkey("OPEN MENU",       &s.configMenuKey,         "MenuKey"); }
// Esc cancels a capture, so it needs its own reset row.
void ResetMenuKeyToEsc()  { auto& s = MutableSettings(); s.configMenuKey = VK_ESCAPE; Config::SetSetting("Hotkeys", "MenuKey", "0x1B"); }
bool MenuKeyIsEsc()       { return Config::GetSettings().configMenuKey == VK_ESCAPE; }

struct ManualKeybindEditorState {
    bool active = false;
    bool disallowMenuReserved = false;
    int* settingsField = nullptr;
    char title[48] = "";
    char iniSection[16] = "";
    char iniKey[32] = "";
    char valueBuf[32] = "";
    char errorBuf[128] = "";
};
ManualKeybindEditorState g_manualKeybindEditor;

bool IsManualKeybindEditorActive() {
    return g_manualKeybindEditor.active;
}

bool ManualKeybindAllowed(int vk, bool disallowMenuReserved) {
    if (vk < 0) return true;
    if (vk == 0) return false;
    if (vk >= 0x01 && vk <= 0x06) return false;
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return false;
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) return false;
    if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) return false;
    if (vk == VK_LWIN || vk == VK_RWIN) return false;
    if (vk == VK_CLEAR) return false;
    if (vk == VK_ESCAPE) return false;
    if (vk == Config::GetSettings().configMenuKey) return false;
    if (disallowMenuReserved && (vk == VK_RETURN || vk == VK_SPACE)) return false;
    return true;
}

std::string TrimAscii(const char* text) {
    if (!text) return std::string();
    std::string value(text);
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    size_t start = 0;
    while (start < value.size() && isSpace(value[start])) ++start;
    size_t end = value.size();
    while (end > start && isSpace(value[end - 1])) --end;
    return value.substr(start, end - start);
}

void CloseManualKeybindEditor() {
    g_manualKeybindEditor.active = false;
    g_manualKeybindEditor.settingsField = nullptr;
    g_manualKeybindEditor.title[0] = '\0';
    g_manualKeybindEditor.iniSection[0] = '\0';
    g_manualKeybindEditor.iniKey[0] = '\0';
    g_manualKeybindEditor.valueBuf[0] = '\0';
    g_manualKeybindEditor.errorBuf[0] = '\0';
    g_manualKeybindEditor.disallowMenuReserved = false;
}

void OpenManualKeybindEditor(const char* title, int* field,
                             const char* section, const char* key,
                             bool disallowMenuReserved = false) {
    if (!title || !field || !section || !key) return;
    g_manualKeybindEditor.active = true;
    g_manualKeybindEditor.settingsField = field;
    g_manualKeybindEditor.disallowMenuReserved = disallowMenuReserved;
    strncpy_s(g_manualKeybindEditor.title, sizeof(g_manualKeybindEditor.title), title, _TRUNCATE);
    strncpy_s(g_manualKeybindEditor.iniSection, sizeof(g_manualKeybindEditor.iniSection), section, _TRUNCATE);
    strncpy_s(g_manualKeybindEditor.iniKey, sizeof(g_manualKeybindEditor.iniKey), key, _TRUNCATE);
    if (*field < 0) {
        strncpy_s(g_manualKeybindEditor.valueBuf, sizeof(g_manualKeybindEditor.valueBuf), "-1", _TRUNCATE);
    } else {
        _snprintf_s(g_manualKeybindEditor.valueBuf, sizeof(g_manualKeybindEditor.valueBuf), _TRUNCATE, "0x%X", *field);
    }
    g_manualKeybindEditor.errorBuf[0] = '\0';
    Input::ResetEdges();
}

bool ApplyManualKeybindEditor() {
    if (!g_manualKeybindEditor.active || !g_manualKeybindEditor.settingsField) {
        return false;
    }

    const std::string raw = TrimAscii(g_manualKeybindEditor.valueBuf);
    int parsed = 0;
    bool disable = false;
    if (raw.empty()) {
        strncpy_s(g_manualKeybindEditor.errorBuf, sizeof(g_manualKeybindEditor.errorBuf),
                  "Enter a virtual-key code in hex (0x48) or decimal (72).", _TRUNCATE);
        return false;
    }
    if (raw == "-1" || raw == "off" || raw == "OFF" || raw == "disabled" || raw == "DISABLED") {
        disable = true;
        parsed = -1;
    } else {
        parsed = Config::ParseKeyValue(raw);
    }

    if (!disable && !ManualKeybindAllowed(parsed, g_manualKeybindEditor.disallowMenuReserved)) {
        strncpy_s(g_manualKeybindEditor.errorBuf, sizeof(g_manualKeybindEditor.errorBuf),
                  g_manualKeybindEditor.disallowMenuReserved
                      ? "That key is reserved here. Footer bindings disallow Enter, Escape, and Space."
                      : "That key is not allowed for menu binding.",
                  _TRUNCATE);
        return false;
    }

    *g_manualKeybindEditor.settingsField = parsed;
    if (disable) {
        Config::SetSetting(g_manualKeybindEditor.iniSection, g_manualKeybindEditor.iniKey, "-1");
    } else {
        char buf[16];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "0x%X", parsed);
        Config::SetSetting(g_manualKeybindEditor.iniSection, g_manualKeybindEditor.iniKey, buf);
    }

    DirectDrawHook::AddMessage(disable ? "Keybind disabled" : "Keybind updated", "HOTKEY", RGB(180, 255, 220), 900, 0, 120);
    CloseManualKeybindEditor();
    Input::ResetEdges();
    return true;
}

std::string ManualKeybindPreviewText() {
    const std::string raw = TrimAscii(g_manualKeybindEditor.valueBuf);
    if (raw.empty()) return "Preview: enter a value";
    if (raw == "-1" || raw == "off" || raw == "OFF" || raw == "disabled" || raw == "DISABLED") {
        return "Preview: DISABLED";
    }
    const int parsed = Config::ParseKeyValue(raw);
    if (!ManualKeybindAllowed(parsed, g_manualKeybindEditor.disallowMenuReserved)) {
        return "Preview: blocked by key restrictions";
    }
    char buf[96];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, I18n::Tr("Preview: 0x%X  %s"), parsed, Config::GetKeyName(parsed).c_str());
    return std::string(buf);
}

bool TickManualKeybindEditorIfActive(ImDrawList*, const ScreenLayout& layout) {
    if (!g_manualKeybindEditor.active) return false;

    const CustomMenu::Scale::Metrics& metrics = CustomMenu::Scale::Get();
    const float marginX = CustomMenu::Scale::Snap(52.0f * metrics.layoutScale);
    const float x = CustomMenu::Scale::Snap(layout.panelX + marginX);
    const float y = CustomMenu::Scale::Snap(layout.contentTopY + 26.0f * metrics.layoutScale);
    const float w = CustomMenu::Scale::Snap(Theme::kPanelW - marginX * 2.0f);
    const float h = CustomMenu::Scale::Snap(184.0f * metrics.layoutScale);

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.86f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.04f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin(I18n::Tr("MANUAL KEYBIND EDITOR"), nullptr, flags)) {
        ImGui::TextUnformatted(I18n::Tr(g_manualKeybindEditor.title));
        ImGui::TextDisabled("%s", I18n::Tr("Enter a raw virtual-key code as hex or decimal. Example: 0x48 or 72."));
        if (g_manualKeybindEditor.disallowMenuReserved) {
            ImGui::TextDisabled("%s", I18n::Tr("Footer bindings reserve Enter, Escape, and Space."));
        }

        if (g_manualKeybindEditor.errorBuf[0]) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", g_manualKeybindEditor.errorBuf);
        } else {
            const std::string preview = ManualKeybindPreviewText();
            ImGui::TextDisabled("%s", I18n::Tr(preview.c_str()));
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##manual_vk_value", g_manualKeybindEditor.valueBuf, sizeof(g_manualKeybindEditor.valueBuf));

        if (ImGui::Button(I18n::Tr("Apply"))) {
            ApplyManualKeybindEditor();
        }
        ImGui::SameLine();
        if (ImGui::Button(I18n::Tr("Disable"))) {
            strncpy_s(g_manualKeybindEditor.valueBuf, sizeof(g_manualKeybindEditor.valueBuf), "-1", _TRUNCATE);
            ApplyManualKeybindEditor();
        }
        ImGui::SameLine();
        if (ImGui::Button(I18n::Tr("Cancel"))) {
            CloseManualKeybindEditor();
            Input::ResetEdges();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            CloseManualKeybindEditor();
            Input::ResetEdges();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(2);
    return true;
}

void EditKeyboardHotkey(const char* title, int* field, const char* key, bool disallowMenuReserved = false) {
    OpenManualKeybindEditor(title, field, "Hotkeys", key, disallowMenuReserved);
}

void EditTeleportVk()       { auto& s = MutableSettings(); EditKeyboardHotkey("TELEPORT",        &s.teleportKey,          "TeleportKey"); }
void EditSavePositionVk()   { auto& s = MutableSettings(); EditKeyboardHotkey("SAVE POSITION",   &s.recordKey,            "RecordKey"); }
void EditToggleStatsVk()    { auto& s = MutableSettings(); EditKeyboardHotkey("TOGGLE STATS",    &s.toggleTitleKey,       "ToggleTitleKey"); }
void EditResetCounterVk()   { auto& s = MutableSettings(); EditKeyboardHotkey("RESET COUNTER",   &s.resetFrameCounterKey, "ResetFrameCounterKey"); }
void EditSavestateSaveVk()  { auto& s = MutableSettings(); EditKeyboardHotkey("SAVESTATE SAVE",  &s.savestateSaveKey,     "SavestateSaveKey"); }
void EditSavestateLoadVk()  { auto& s = MutableSettings(); EditKeyboardHotkey("SAVESTATE LOAD",  &s.savestateLoadKey,     "SavestateLoadKey"); }
void EditSwitchPlayersVk()  { auto& s = MutableSettings(); EditKeyboardHotkey("SWITCH PLAYERS",  &s.switchPlayersKey,     "SwitchPlayersKey"); }
void EditMacroRecordVk()    { auto& s = MutableSettings(); EditKeyboardHotkey("MACRO RECORD",    &s.macroRecordKey,       "MacroRecordKey"); }
void EditMacroPlayVk()      { auto& s = MutableSettings(); EditKeyboardHotkey("MACRO PLAY",      &s.macroPlayKey,         "MacroPlayKey"); }
void EditMacroSlotVk()      { auto& s = MutableSettings(); EditKeyboardHotkey("MACRO NEXT SLOT", &s.macroSlotKey,         "MacroSlotKey"); }
void EditUiAcceptVk()       { auto& s = MutableSettings(); EditKeyboardHotkey("UI ACCEPT",       &s.uiAcceptKey,          "UIAcceptKey", true); }
void EditUiRefreshVk()      { auto& s = MutableSettings(); EditKeyboardHotkey("UI REFRESH",      &s.uiRefreshKey,         "UIRefreshKey", true); }
void EditUiExitVk()         { auto& s = MutableSettings(); EditKeyboardHotkey("UI EXIT",         &s.uiExitKey,            "UIExitKey", true); }
void EditFramestepPauseVk() { auto& s = MutableSettings(); EditKeyboardHotkey("FRAMESTEP PAUSE", &s.framestepPauseKey,    "FramestepPauseKey"); }
void EditFramestepStepVk()  { auto& s = MutableSettings(); EditKeyboardHotkey("FRAMESTEP STEP",  &s.framestepStepKey,     "FramestepStepKey"); }
void EditSwapCustomVk()     { auto& s = MutableSettings(); EditKeyboardHotkey("SWAP CUSTOM KEY", &s.swapCustomKey,        "SwapCustomKey"); }

const char* ValTeleport()       { return HotkeyNameValue(Config::GetSettings().teleportKey); }
const char* ValSavePosition()   { return HotkeyNameValue(Config::GetSettings().recordKey); }
const char* ValToggleStats()    { return HotkeyNameValue(Config::GetSettings().toggleTitleKey); }
const char* ValResetCounter()   { return HotkeyNameValue(Config::GetSettings().resetFrameCounterKey); }
const char* ValSavestateSave()  { return HotkeyNameValue(Config::GetSettings().savestateSaveKey); }
const char* ValSavestateLoad()  { return HotkeyNameValue(Config::GetSettings().savestateLoadKey); }
const char* ValSwitchPlayers()  { return HotkeyNameValue(Config::GetSettings().switchPlayersKey); }
const char* ValMacroRecord()    { return HotkeyNameValue(Config::GetSettings().macroRecordKey); }
const char* ValMacroPlay()      { return HotkeyNameValue(Config::GetSettings().macroPlayKey); }
const char* ValMacroSlot()      { return HotkeyNameValue(Config::GetSettings().macroSlotKey); }
const char* ValUiAccept()       { return HotkeyNameValue(Config::GetSettings().uiAcceptKey); }
const char* ValUiRefresh()      { return HotkeyNameValue(Config::GetSettings().uiRefreshKey); }
const char* ValUiExit()         { return HotkeyNameValue(Config::GetSettings().uiExitKey); }
const char* ValFramestepPause() { return HotkeyNameValue(Config::GetSettings().framestepPauseKey); }
const char* ValFramestepStep()  { return HotkeyNameValue(Config::GetSettings().framestepStepKey); }
const char* ValSwapCustom()     { return HotkeyNameValue(Config::GetSettings().swapCustomKey); }
const char* ValMenuKey()        { return HotkeyNameValue(Config::GetSettings().configMenuKey); }
const char* ValTeleportCode()       { return HotkeyCodeValue(Config::GetSettings().teleportKey); }
const char* ValSavePositionCode()   { return HotkeyCodeValue(Config::GetSettings().recordKey); }
const char* ValToggleStatsCode()    { return HotkeyCodeValue(Config::GetSettings().toggleTitleKey); }
const char* ValResetCounterCode()   { return HotkeyCodeValue(Config::GetSettings().resetFrameCounterKey); }
const char* ValSavestateSaveCode()  { return HotkeyCodeValue(Config::GetSettings().savestateSaveKey); }
const char* ValSavestateLoadCode()  { return HotkeyCodeValue(Config::GetSettings().savestateLoadKey); }
const char* ValSwitchPlayersCode()  { return HotkeyCodeValue(Config::GetSettings().switchPlayersKey); }
const char* ValMacroRecordCode()    { return HotkeyCodeValue(Config::GetSettings().macroRecordKey); }
const char* ValMacroPlayCode()      { return HotkeyCodeValue(Config::GetSettings().macroPlayKey); }
const char* ValMacroSlotCode()      { return HotkeyCodeValue(Config::GetSettings().macroSlotKey); }
const char* ValUiAcceptCode()       { return HotkeyCodeValue(Config::GetSettings().uiAcceptKey); }
const char* ValUiRefreshCode()      { return HotkeyCodeValue(Config::GetSettings().uiRefreshKey); }
const char* ValUiExitCode()         { return HotkeyCodeValue(Config::GetSettings().uiExitKey); }
const char* ValFramestepPauseCode() { return HotkeyCodeValue(Config::GetSettings().framestepPauseKey); }
const char* ValFramestepStepCode()  { return HotkeyCodeValue(Config::GetSettings().framestepStepKey); }
const char* ValSwapCustomCode()     { return HotkeyCodeValue(Config::GetSettings().swapCustomKey); }
const char* ValSwapEnabled()    { return Config::GetSettings().swapCustomEnabled ? "ON" : "OFF"; }
constexpr int kControllerChoiceCount = 5;

char g_controllerChoiceLabels[kControllerChoiceCount][96] = {};
const char* const g_controllerChoicePtrs[kControllerChoiceCount] = {
    g_controllerChoiceLabels[0],
    g_controllerChoiceLabels[1],
    g_controllerChoiceLabels[2],
    g_controllerChoiceLabels[3],
    g_controllerChoiceLabels[4],
};

int g_controllerChoiceIndex = 0;
int g_gpTeleportChoice = 0;

void OnControllerIndexChoice() {
    const int newValue = (g_controllerChoiceIndex <= 0) ? -1 : (g_controllerChoiceIndex - 1);
    PersistInt("General", "controllerIndex", newValue);
}

void BindGamepadHotkey(const char* title, int* field, const char* key) {
    OpenGamepadKeybind(title, field, "Hotkeys", key);
}

void BindGpTeleport()      { auto& s = MutableSettings(); BindGamepadHotkey("LOAD / TELEPORT",  &s.gpTeleportButton,      "gpTeleportButton"); }
void BindGpSavePosition()  { auto& s = MutableSettings(); BindGamepadHotkey("SAVE POSITION",    &s.gpSavePositionButton,  "gpSavePositionButton"); }
void BindGpSwitchPlayers() { auto& s = MutableSettings(); BindGamepadHotkey("SWITCH PLAYERS",   &s.gpSwitchPlayersButton, "gpSwitchPlayersButton"); }
void BindGpSwapPositions() { auto& s = MutableSettings(); BindGamepadHotkey("SWAP POSITIONS",   &s.gpSwapPositionsButton, "gpSwapPositionsButton"); }
void BindGpMacroRecord()   { auto& s = MutableSettings(); BindGamepadHotkey("MACRO RECORD",     &s.gpMacroRecordButton,   "gpMacroRecordButton"); }
void BindGpMacroPlay()     { auto& s = MutableSettings(); BindGamepadHotkey("MACRO PLAY",       &s.gpMacroPlayButton,     "gpMacroPlayButton"); }
void BindGpMacroSlot()     { auto& s = MutableSettings(); BindGamepadHotkey("MACRO NEXT SLOT",  &s.gpMacroSlotButton,     "gpMacroSlotButton"); }
void BindGpToggleMenu()    { auto& s = MutableSettings(); BindGamepadHotkey("TOGGLE MENU",      &s.gpToggleMenuButton,    "gpToggleMenuButton"); }
void BindGpUiTopTabPrev()  { auto& s = MutableSettings(); BindGamepadHotkey("TOP TAB PREVIOUS", &s.gpUiTopTabPrev,        "gpUiTopTabPrev"); }
void BindGpUiTopTabNext()  { auto& s = MutableSettings(); BindGamepadHotkey("TOP TAB NEXT",     &s.gpUiTopTabNext,        "gpUiTopTabNext"); }
void BindGpUiSubTabPrev()  { auto& s = MutableSettings(); BindGamepadHotkey("SUBTAB PREVIOUS",  &s.gpUiSubTabPrev,        "gpUiSubTabPrev"); }
void BindGpUiSubTabNext()  { auto& s = MutableSettings(); BindGamepadHotkey("SUBTAB NEXT",      &s.gpUiSubTabNext,        "gpUiSubTabNext"); }

const char* GamepadBindNameValue(int mask) {
    static char buffers[8][64];
    static int next = 0;
    char* buf = buffers[next++ & 7];
    _snprintf_s(buf, sizeof(buffers[0]), _TRUNCATE, "%s",
                Config::GetGamepadButtonName(mask).c_str());
    return buf;
}

const char* ValGpTeleport()      { return GamepadBindNameValue(Config::GetSettings().gpTeleportButton); }
const char* ValGpSavePosition()  { return GamepadBindNameValue(Config::GetSettings().gpSavePositionButton); }
const char* ValGpSwitchPlayers() { return GamepadBindNameValue(Config::GetSettings().gpSwitchPlayersButton); }
const char* ValGpSwapPositions() { return GamepadBindNameValue(Config::GetSettings().gpSwapPositionsButton); }
const char* ValGpMacroRecord()   { return GamepadBindNameValue(Config::GetSettings().gpMacroRecordButton); }
const char* ValGpMacroPlay()     { return GamepadBindNameValue(Config::GetSettings().gpMacroPlayButton); }
const char* ValGpMacroSlot()     { return GamepadBindNameValue(Config::GetSettings().gpMacroSlotButton); }
const char* ValGpToggleMenu()    { return GamepadBindNameValue(Config::GetSettings().gpToggleMenuButton); }
const char* ValGpUiTopTabPrev()  { return GamepadBindNameValue(Config::GetSettings().gpUiTopTabPrev); }
const char* ValGpUiTopTabNext()  { return GamepadBindNameValue(Config::GetSettings().gpUiTopTabNext); }
const char* ValGpUiSubTabPrev()  { return GamepadBindNameValue(Config::GetSettings().gpUiSubTabPrev); }
const char* ValGpUiSubTabNext()  { return GamepadBindNameValue(Config::GetSettings().gpUiSubTabNext); }

void OnSwapCustomEnabled() {
    PersistBool("Hotkeys", "SwapCustomEnabled", MutableSettings().swapCustomEnabled);
}

bool SwapCustomKeyDisabled() {
    return !Config::GetSettings().swapCustomEnabled;
}

void RefreshHotkeyStrings() {
    const auto& s = Config::GetSettings();

    static DWORD s_lastControllerLabelRefresh = 0;
    static unsigned s_lastControllerMask = 0xFFFFFFFFu;
    static unsigned s_lastNativeMask = 0xFFFFFFFFu;
    static unsigned s_lastGenericMask = 0xFFFFFFFFu;

    const DWORD now = GetTickCount();
    XInputShim::Snapshot controllerSnapshot{};
    XInputShim::CopySnapshot(controllerSnapshot);
    const unsigned controllerMask = controllerSnapshot.connectedMask;
    const unsigned nativeMask = controllerSnapshot.nativeMask;
    const unsigned genericMask = controllerSnapshot.genericMask;
    // Building these labels can hit the XInput shim, which is expensive when slots are empty.
    const bool labelsDue = s_lastControllerLabelRefresh == 0
        || (now - s_lastControllerLabelRefresh) >= 10000
        || controllerMask != s_lastControllerMask
        || nativeMask != s_lastNativeMask
        || genericMask != s_lastGenericMask;

    if (labelsDue) {
        _snprintf_s(g_controllerChoiceLabels[0], sizeof(g_controllerChoiceLabels[0]), _TRUNCATE, "All (Any)");
        for (int i = 0; i < 4; ++i) {
// Never call Windows controller-enumeration APIs on the render thread; read the watcher's published copy.
            (void)XInputShim::GetPublishedControllerName(
                i, g_controllerChoiceLabels[i + 1], sizeof(g_controllerChoiceLabels[i + 1]));
        }
        s_lastControllerLabelRefresh = now;
        s_lastControllerMask = controllerMask;
        s_lastNativeMask = nativeMask;
        s_lastGenericMask = genericMask;
    }

    g_controllerChoiceIndex = (s.controllerIndex >= 0 && s.controllerIndex <= 3)
        ? (s.controllerIndex + 1)
        : 0;
}

const char* ValHotkeyGameplay() { return "5 KEYS"; }
const char* ValHotkeySavestate() { return "4 KEYS"; }
const char* ValHotkeyMacros()   { return "3 KEYS"; }
const char* ValHotkeyMenu()     { return "6 KEYS"; }
const char* ValHotkeyController() { return "12 BINDS"; }
const char* ValHotkeyManual()   { return "RAW VK"; }

Row* BuildHotkeysGameplayRows(int& count) {
    static Row s_rows[16];
    int n = 0;

    s_rows[n++] = Header("GAMEPLAY HOTKEYS");
    s_rows[n++] = Action("TELEPORT",        BindTeleport,       ValTeleport);
    s_rows[n++] = Action("SAVE POSITION",   BindSavePosition,   ValSavePosition);
    s_rows[n++] = Action("TOGGLE STATS",    BindToggleStats,    ValToggleStats);
    s_rows[n++] = Action("RESET COUNTER",   BindResetCounter,   ValResetCounter);
    s_rows[n++] = Action("SWITCH PLAYERS",  BindSwitchPlayers,  ValSwitchPlayers);
    count = n;
    return s_rows;
}

Row* BuildHotkeysMacroRows(int& count) {
    static Row s_rows[8];
    int n = 0;

    s_rows[n++] = Header("MACROS");
    s_rows[n++] = Action("MACRO RECORD",    BindMacroRecord,    ValMacroRecord);
    s_rows[n++] = Action("MACRO PLAY",      BindMacroPlay,      ValMacroPlay);
    s_rows[n++] = Action("MACRO NEXT SLOT", BindMacroSlot,      ValMacroSlot);
    count = n;
    return s_rows;
}

Row* BuildHotkeysSavestateRows(int& count) {
    static Row s_rows[10];
    int n = 0;

    s_rows[n++] = Header("SAVESTATE HOTKEYS");
    s_rows[n++] = Action("SAVE PRACTICE SNAPSHOT", BindSavestateSave, ValSavestateSave);
    s_rows[n++] = Action("LOAD PRACTICE SNAPSHOT", BindSavestateLoad, ValSavestateLoad);
    s_rows[n++] = Info("Save Practice Snapshot and Load Practice Snapshot capture and restore the whole Practice match right away. There is only one snapshot.");
    count = n;
    return s_rows;
}

Row* BuildHotkeysMenuRows(int& count) {
    static Row s_rows[10];
    int n = 0;

    s_rows[n++] = Header("MENU CONTROL");
    s_rows[n++] = Action("OPEN MENU",       BindMenuKey,        ValMenuKey);
    s_rows[n++] = Action("USE ESC TO OPEN MENU", ResetMenuKeyToEsc, nullptr, MenuKeyIsEsc);
    s_rows[n++] = Info("With any key other than Esc, Esc does its normal EFZ job again (it leaves the match). Inside this menu Esc still goes back. Avoid keys EfzRevival uses for Practice.");
    s_rows[n++] = Action("UI ACCEPT",       BindUiAccept,       ValUiAccept);
    s_rows[n++] = Action("UI REFRESH",      BindUiRefresh,      ValUiRefresh);
    s_rows[n++] = Action("UI EXIT",         BindUiExit,         ValUiExit);
    s_rows[n++] = Action("FRAMESTEP PAUSE", BindFramestepPause, ValFramestepPause);
    s_rows[n++] = Action("FRAMESTEP STEP",  BindFramestepStep,  ValFramestepStep);
    count = n;
    return s_rows;
}

Row* BuildHotkeysSwapRows(int& count) {
    static Row s_rows[8];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("SWAP POSITIONS");
    s_rows[n++] = Toggle("CUSTOM SWAP KEY", &s.swapCustomEnabled, OnSwapCustomEnabled);
    s_rows[n++] = Action("SWAP CUSTOM KEY", BindSwapCustom, ValSwapCustom, SwapCustomKeyDisabled);
    count = n;
    return s_rows;
}

Row* BuildHotkeysManualRows(int& count) {
    static Row s_rows[40];
    int n = 0;

    s_rows[n++] = Header("RAW VK CODES");
    s_rows[n++] = Info("Use this page for direct virtual-key entry when press-to-bind is not enough.");
    s_rows[n++] = Info("Examples: 0x48 or 72. Enter -1 in the editor to disable a binding.");
    s_rows[n++] = Spacer();

    s_rows[n++] = Header("GAMEPLAY");
    s_rows[n++] = Action("TELEPORT",        EditTeleportVk,      ValTeleportCode);
    s_rows[n++] = Action("SAVE POSITION",   EditSavePositionVk,  ValSavePositionCode);
    s_rows[n++] = Action("TOGGLE STATS",    EditToggleStatsVk,   ValToggleStatsCode);
    s_rows[n++] = Action("RESET COUNTER",   EditResetCounterVk,  ValResetCounterCode);
    s_rows[n++] = Action("SWITCH PLAYERS",  EditSwitchPlayersVk, ValSwitchPlayersCode);
    s_rows[n++] = Spacer();

    s_rows[n++] = Header("SAVESTATES / MACROS");
    s_rows[n++] = Action("SAVESTATE SAVE",  EditSavestateSaveVk, ValSavestateSaveCode);
    s_rows[n++] = Action("SAVESTATE LOAD",  EditSavestateLoadVk, ValSavestateLoadCode);
    s_rows[n++] = Action("MACRO RECORD",    EditMacroRecordVk,   ValMacroRecordCode);
    s_rows[n++] = Action("MACRO PLAY",      EditMacroPlayVk,     ValMacroPlayCode);
    s_rows[n++] = Action("MACRO NEXT SLOT", EditMacroSlotVk,     ValMacroSlotCode);
    s_rows[n++] = Spacer();

    s_rows[n++] = Header("MENU / FRAMESTEP");
    s_rows[n++] = Action("UI ACCEPT",       EditUiAcceptVk,       ValUiAcceptCode);
    s_rows[n++] = Action("UI REFRESH",      EditUiRefreshVk,      ValUiRefreshCode);
    s_rows[n++] = Action("UI EXIT",         EditUiExitVk,         ValUiExitCode);
    s_rows[n++] = Info("Footer hotkeys disallow Enter, Escape, and Space so they do not fight menu controls.");
    s_rows[n++] = Action("FRAMESTEP PAUSE", EditFramestepPauseVk, ValFramestepPauseCode);
    s_rows[n++] = Action("FRAMESTEP STEP",  EditFramestepStepVk,  ValFramestepStepCode);
    s_rows[n++] = Action("SWAP CUSTOM KEY", EditSwapCustomVk,     ValSwapCustomCode, SwapCustomKeyDisabled);
    count = n;
    return s_rows;
}

Row* BuildHotkeysControllerRows(int& count) {
    static Row s_rows[40];
    int n = 0;

    s_rows[n++] = Header("CONTROLLER BINDINGS");
    s_rows[n++] = DropdownRow("CONTROLLER FOR MOD INPUTS", &g_controllerChoiceIndex,
                              g_controllerChoicePtrs, kControllerChoiceCount,
                              OnControllerIndexChoice);
    s_rows[n++] = Info("All (Any) lets any connected controller open the menu and trigger mod actions.");
    s_rows[n++] = Info("Press Enter or the confirm button (A / Cross) on any bind row, release your inputs, then press the new controller button.");
    s_rows[n++] = Info("During controller capture, press your Toggle Menu button on pad to cancel. Delete or Backspace still disables the bind from keyboard.");
    s_rows[n++] = Info("Button names are shown with Xbox labels. On many PlayStation-style pads, read A / B / X / Y as Cross / Circle / Square / Triangle.");
    s_rows[n++] = Spacer();

    s_rows[n++] = Header("GAMEPLAY");
    s_rows[n++] = Action("LOAD / TELEPORT", BindGpTeleport, ValGpTeleport);
    s_rows[n++] = Action("SAVE POSITION", BindGpSavePosition, ValGpSavePosition);
    s_rows[n++] = Action("SWITCH PLAYERS", BindGpSwitchPlayers, ValGpSwitchPlayers);
    s_rows[n++] = Action("SWAP POSITIONS", BindGpSwapPositions, ValGpSwapPositions);
    s_rows[n++] = Spacer();

    s_rows[n++] = Header("MACROS");
    s_rows[n++] = Action("MACRO RECORD", BindGpMacroRecord, ValGpMacroRecord);
    s_rows[n++] = Action("MACRO PLAY", BindGpMacroPlay, ValGpMacroPlay);
    s_rows[n++] = Action("MACRO NEXT SLOT", BindGpMacroSlot, ValGpMacroSlot);
    s_rows[n++] = Spacer();

    s_rows[n++] = Header("MENU");
    s_rows[n++] = Action("TOGGLE MENU", BindGpToggleMenu, ValGpToggleMenu);
    s_rows[n++] = Info("Toggle Menu uses the selected controller for both opening and closing the custom menu.");
    s_rows[n++] = Spacer();

    s_rows[n++] = Header("NAVIGATION");
    s_rows[n++] = Action("TOP TAB PREVIOUS", BindGpUiTopTabPrev, ValGpUiTopTabPrev);
    s_rows[n++] = Action("TOP TAB NEXT", BindGpUiTopTabNext, ValGpUiTopTabNext);
    s_rows[n++] = Action("SUBTAB PREVIOUS", BindGpUiSubTabPrev, ValGpUiSubTabPrev);
    s_rows[n++] = Action("SUBTAB NEXT", BindGpUiSubTabNext, ValGpUiSubTabNext);
    s_rows[n++] = Info("These tab and subtab binds also work while you are inside submenus.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Action("SAVE CONTROLLER BINDS", SaveSettingsToDisk);
    s_rows[n++] = Action("RELOAD FROM DISK", ReloadSettingsFromDisk);
    s_rows[n++] = Info(CurrentConfigPathInfo());
    count = n;
    return s_rows;
}

Row* BuildSettingsHotkeysRows(int& count) {
    static Row s_rows[20];
    int n = 0;

    s_rows[n++] = Header("HOTKEY MENUS");
    s_rows[n++] = Submenu("GAMEPLAY",      "GAMEPLAY HOTKEYS", BuildHotkeysGameplayRows, ValHotkeyGameplay);
    s_rows[n++] = Submenu("SAVESTATE",     "SAVESTATE HOTKEYS", BuildHotkeysSavestateRows, ValHotkeySavestate);
    s_rows[n++] = Submenu("MACROS",        "MACRO HOTKEYS",    BuildHotkeysMacroRows,    ValHotkeyMacros);
    s_rows[n++] = Submenu("MENU CONTROL",  "MENU CONTROL",     BuildHotkeysMenuRows,     ValHotkeyMenu);
    s_rows[n++] = Submenu("RAW VK CODES",  "RAW VK CODES",     BuildHotkeysManualRows,   ValHotkeyManual);
    s_rows[n++] = Submenu("CONTROLLER",    "CONTROLLER BINDINGS", BuildHotkeysControllerRows, ValHotkeyController);
    s_rows[n++] = Submenu("SWAP POSITIONS","SWAP POSITIONS",   BuildHotkeysSwapRows,     ValSwapEnabled);
    s_rows[n++] = Spacer();
    s_rows[n++] = Info(CurrentConfigPathInfo());
    s_rows[n++] = Action("SAVE ALL TO DISK", SaveSettingsToDisk);
    s_rows[n++] = Action("RELOAD FROM DISK", ReloadSettingsFromDisk);
    count = n;
    return s_rows;
}

bool g_mirrorOverlayBorders = false;
bool g_mirrorRGToasts       = false;
bool g_mirrorPadInputLog    = false;
bool g_mirrorDeepFA         = false;
bool g_mirrorDisableHud     = false;
bool g_mirrorHudTopBar      = false;
bool g_mirrorHudTimer       = false;
bool g_mirrorHudPortraits   = false;
bool g_mirrorHudHpBars      = false;
bool g_mirrorHudRoundDots   = false;
bool g_mirrorHudNameplates  = false;
bool g_mirrorHudBottomBar   = false;
bool g_mirrorHudSpMeter     = false;
bool g_mirrorHudRfGauge     = false;
bool g_mirrorHudCombo       = false;
int g_customSavestateDiskSlot = 0;
CustomSavestate::EditableFields g_customSavestateFields;
char g_customSavestateModeInfo[256] = "Mode: custom savestates active";
char g_customSavestateWorkingInfo[256] = "Current State: empty";
char g_customSavestateMetaInfo[256] = "Meta: n/a";
char g_customSavestateStatusInfo[256] = "Status: idle";
char g_customSavestateDiskInfo[256] = "Slot 0: initial snapshot | memory only";
bool g_customSavestateHotswapPrompt = false;
bool g_customSavestateHotswapDismissed = false;
unsigned int g_customSavestateHotswapWorkingStamp = 0;
char g_customSavestateHotswapInfo[256] = "Loaded slot differs from the current match.";
int g_debugPracticeLocalSide = -1;
bool g_debugSwitchPlayersAvailable = false;
bool g_debugRfFreezeP1Active = false;
bool g_debugRfFreezeP2Active = false;
char g_debugLocalSideInfo[96] = "Current Local: unknown";
char g_debugAiControlInfo[96] = "AI Control Flags: unknown";
char g_debugPracticeCpuInfo[96] = "Practice P2 CPU flag: unknown";
char g_debugGamespeedInfo[96] = "Gamespeed: unknown";
char g_debugRfFreezeP1Info[160] = "P1 RF Freeze: inactive";
char g_debugRfFreezeP2Info[160] = "P2 RF Freeze: inactive";

struct HotswapCurrentState;
int CharacterSelectIdFromInternalCharacterId(int internalCharId);
bool ReadCurrentHotswapState(HotswapCurrentState& state);
bool RevivalBgmMuted();
const char* GetNamedStageLabel(int stageId);
void UpdateCustomSavestateHotswapPromptFromWorking();
void UpdateCustomSavestateHotswapPromptFromSummary(const CustomSavestate::Summary& summary);

const char* const kSavestateBackendChoices[3] = {
    "CUSTOM",
    "REVIVAL",
    "CUSTOM+FALLBACK",
};

int SavedSavestateSelectId(uint8_t rawCharId) {
    if (rawCharId == 0xFF) {
        return -1;
    }

    // Custom savestate headers capture characterObject+141, the select ID consumed by CharacterHotswap::QueueReload.
    const int selectId = static_cast<int>(rawCharId);
    if (selectId < 0 || selectId >= CharacterHotswap::kCharacterSelectCount) {
        return -1;
    }
    return selectId;
}

const char* SavedSavestateDisplayName(uint8_t rawCharId) {
    const int selectId = SavedSavestateSelectId(rawCharId);
    return selectId >= 0 ? CharacterHotswap::GetDisplayNameForSelectId(selectId) : "UNKNOWN";
}

void LogSavestateMirrorStateIfChanged(const char* source,
                                      const CustomSavestate::Summary& summary,
                                      CustomSavestate::BackendMode backendMode,
                                      int activeDiskSlot,
                                      bool fieldsOk) {
    static std::string s_lastLogLine;

    std::ostringstream oss;
    oss << "[SAVESTATE][MENU][" << (source ? source : "MIRROR") << "]"
        << " backend=" << CustomSavestate::BackendModeName(backendMode)
        << " slot=" << activeDiskSlot
        << " stamp=" << summary.workingSnapshotStamp
        << " working=" << (summary.hasWorkingSnapshot ? 1 : 0)
        << " dirty=" << (summary.workingSnapshotDirty ? 1 : 0)
        << " rawChars=" << static_cast<unsigned int>(summary.savedP1CharId)
        << "/" << static_cast<unsigned int>(summary.savedP2CharId)
        << " selectChars=" << SavedSavestateSelectId(summary.savedP1CharId)
        << "/" << SavedSavestateSelectId(summary.savedP2CharId)
        << " stage=" << static_cast<unsigned int>(summary.savedStageId)
        << " compat=" << (summary.currentPairCompatible ? 1 : 0)
        << "/" << (summary.currentStageCompatible ? 1 : 0)
        << "/" << (summary.currentVersionCompatible ? 1 : 0)
        << " restoreAllowed=" << (summary.currentRestoreAllowed ? 1 : 0)
        << " fieldsOk=" << (fieldsOk ? 1 : 0)
        << " saveLoad=" << summary.saveCount << "/" << summary.loadCount
        << " revival=" << summary.savedRevivalVersion;

    const std::string line = oss.str();
    if (line != s_lastLogLine) {
        LogOut(line, true);
        s_lastLogLine = line;
    }
}

bool CustomSavestateDiskSlotExistsCached(int slot, DWORD maxAgeMs = 1000) {
    struct CacheEntry {
        bool valid = false;
        int slot = -999;
        bool exists = false;
        DWORD tick = 0;
    };

    constexpr int kCacheSlots = 16;
    static CacheEntry s_cache[kCacheSlots];
    const int index = (slot >= 0 && slot < kCacheSlots) ? slot : (kCacheSlots - 1);
    CacheEntry& entry = s_cache[index];

    const DWORD now = GetTickCount();
    if (maxAgeMs > 0 &&
        entry.valid &&
        entry.slot == slot &&
        (now - entry.tick) < maxAgeMs) {
        return entry.exists;
    }

    const DWORD start = now;
    const bool exists = CustomSavestate::DoesDiskSlotExist(slot);
    const DWORD elapsed = GetTickCount() - start;

    entry.valid = true;
    entry.slot = slot;
    entry.exists = exists;
    entry.tick = GetTickCount();

    static DWORD s_lastSlowLog = 0;
    if (elapsed >= 25 && (s_lastSlowLog == 0 || (now - s_lastSlowLog) >= 1000)) {
        s_lastSlowLog = now;
        char buf[192];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "[CUSTOM_MENU][TIMING] Savestate slot existence probe took %lums slot=%d exists=%d",
            static_cast<unsigned long>(elapsed),
            slot,
            exists ? 1 : 0);
        LogOut(buf, true);
    }

    return exists;
}

void RefreshCustomSavestateMirrors() {
    CustomSavestate::Summary summary{};
    CustomSavestate::GetSummary(summary);
    const CustomSavestate::BackendMode backendMode = CustomSavestate::GetConfiguredBackendMode();
    g_customSavestateDiskSlot = CustomSavestate::GetActiveDiskSlot();

    const char* currentVersionName = EfzRevivalVersionName(GetEfzRevivalVersion());
    const char* savedVersionName = summary.savedRevivalVersion != 0
        ? EfzRevivalVersionName(static_cast<EfzRevivalVersion>(summary.savedRevivalVersion))
        : "UNKNOWN/LEGACY";
    const unsigned int revivalSaves = SavestateHook::GetSaveCount();
    const unsigned int revivalLoads = SavestateHook::GetLoadCount();
    if (summary.workingSnapshotStamp != g_customSavestateHotswapWorkingStamp) {
        g_customSavestateHotswapWorkingStamp = summary.workingSnapshotStamp;
        g_customSavestateHotswapDismissed = false;
    }
    UpdateCustomSavestateHotswapPromptFromSummary(summary);

    const bool fieldsOk = summary.hasWorkingSnapshot && CustomSavestate::GetWorkingEditableFields(g_customSavestateFields);
    if (fieldsOk) {
        const std::string p1Name = SavedSavestateDisplayName(summary.savedP1CharId);
        const std::string p2Name = SavedSavestateDisplayName(summary.savedP2CharId);
        const char* savedStageName = GetNamedStageLabel(summary.savedStageId);
        _snprintf_s(g_customSavestateWorkingInfo, sizeof(g_customSavestateWorkingInfo), _TRUNCATE,
                "Current State: %s / %s | %s | stage %s | restore %s/%s/%s | save/load %u/%u | revival %u/%u",
                    p1Name.c_str(),
                    p2Name.c_str(),
                summary.workingSnapshotDirty ? "EDITED" : "READY",
                    savedStageName,
                    summary.currentPairCompatible ? "PAIR OK" : "PAIR BLOCK",
                    summary.currentStageCompatible ? "STAGE OK" : "STAGE BLOCK",
                    summary.currentVersionCompatible ? "VER OK" : "VER BLOCK",
                    summary.saveCount,
                    summary.loadCount,
                    revivalSaves,
                    revivalLoads);
        _snprintf_s(g_customSavestateMetaInfo, sizeof(g_customSavestateMetaInfo), _TRUNCATE,
                    "Meta: saved %s | current %s | BGM %u | P1 0x%X CPU %u | P2 0x%X CPU %u | side %d | BC 0x%X GS 0x%X RB 0x%X",
                    savedVersionName,
                    currentVersionName,
                    summary.savedBgmTrack,
                    summary.savedP1StateSize,
                    summary.savedP1CpuFlag,
                    summary.savedP2StateSize,
                    summary.savedP2CpuFlag,
                    summary.savedLocalSide,
                    summary.savedBattleContextSize,
                    summary.savedGameStateSize,
                    summary.savedRenderBitmapSize);
    } else {
        g_customSavestateFields = CustomSavestate::EditableFields{};
        _snprintf_s(g_customSavestateWorkingInfo, sizeof(g_customSavestateWorkingInfo), _TRUNCATE,
                    "Current State: empty | save/load %u/%u | revival %u/%u",
                    summary.saveCount,
                    summary.loadCount,
                    revivalSaves,
                    revivalLoads);
        _snprintf_s(g_customSavestateMetaInfo, sizeof(g_customSavestateMetaInfo), _TRUNCATE,
                    "Meta: current %s | backend %s",
                    currentVersionName,
                    CustomSavestate::BackendModeName(backendMode));
    }

    switch (backendMode) {
    case CustomSavestate::BackendMode::Revival:
        _snprintf_s(g_customSavestateModeInfo, sizeof(g_customSavestateModeInfo), _TRUNCATE,
                    "Mode: Revival save/load is active. Custom current-state capture, restore, edits, and slot files are locked.");
        break;
    case CustomSavestate::BackendMode::CustomWithRevivalFallback:
        _snprintf_s(g_customSavestateModeInfo, sizeof(g_customSavestateModeInfo), _TRUNCATE,
                    "Mode: Custom savestates are active. Revival stays installed for tracking and supported fallback cases.");
        break;
    case CustomSavestate::BackendMode::Custom:
    default:
        _snprintf_s(g_customSavestateModeInfo, sizeof(g_customSavestateModeInfo), _TRUNCATE,
                    "Mode: Custom savestates are active. Current State editing and slot files are available.");
        break;
    }

    _snprintf_s(g_customSavestateStatusInfo, sizeof(g_customSavestateStatusInfo), _TRUNCATE,
                "Status: %s",
                CustomSavestate::GetLastStatus().c_str());

    constexpr DWORD kDiskInfoRefreshMs = 1000;
    static int s_lastDiskInfoSlot = -999;
    static unsigned int s_lastDiskInfoSaveCount = 0xFFFFFFFFu;
    static unsigned int s_lastDiskInfoLoadCount = 0xFFFFFFFFu;
    static DWORD s_lastDiskInfoRefresh = 0;

    const DWORD diskNow = GetTickCount();
    const bool diskInfoDue = s_lastDiskInfoRefresh == 0
        || (diskNow - s_lastDiskInfoRefresh) >= kDiskInfoRefreshMs
        || s_lastDiskInfoSlot != g_customSavestateDiskSlot
        || s_lastDiskInfoSaveCount != summary.saveCount
        || s_lastDiskInfoLoadCount != summary.loadCount;

    if (diskInfoDue) {
        const DWORD diskStart = diskNow;
        if (g_customSavestateDiskSlot == 0) {
            _snprintf_s(g_customSavestateDiskInfo, sizeof(g_customSavestateDiskInfo), _TRUNCATE,
                        "Slot 0: %s | memory snapshot | next save uses slot 1",
                        CustomSavestateDiskSlotExistsCached(0, 0) ? "READY" : "EMPTY");
        } else {
            const std::string slotPath = CustomSavestate::GetDiskSlotPath(g_customSavestateDiskSlot);
            _snprintf_s(g_customSavestateDiskInfo, sizeof(g_customSavestateDiskInfo), _TRUNCATE,
                        "Slot %d: %s | %s",
                        g_customSavestateDiskSlot,
                        CustomSavestateDiskSlotExistsCached(g_customSavestateDiskSlot, 0) ? "HAS FILE" : "EMPTY",
                        slotPath.c_str());
        }

        s_lastDiskInfoSlot = g_customSavestateDiskSlot;
        s_lastDiskInfoSaveCount = summary.saveCount;
        s_lastDiskInfoLoadCount = summary.loadCount;
        s_lastDiskInfoRefresh = GetTickCount();

        const DWORD diskElapsed = s_lastDiskInfoRefresh - diskStart;
        static DWORD s_lastSlowDiskLog = 0;
        if (diskElapsed >= 25 && (s_lastSlowDiskLog == 0 || (diskNow - s_lastSlowDiskLog) >= 1000)) {
            s_lastSlowDiskLog = diskNow;
            char buf[192];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[CUSTOM_MENU][TIMING] Savestate disk mirror took %lums slot=%d",
                static_cast<unsigned long>(diskElapsed),
                g_customSavestateDiskSlot);
            LogOut(buf, true);
        }
    }

    LogSavestateMirrorStateIfChanged("SUMMARY", summary, backendMode, g_customSavestateDiskSlot, fieldsOk);
}

void RefreshDebugRuntimeMirrors() {
    g_debugSwitchPlayersAvailable = GetCurrentGameMode() == GameMode::Practice;
    g_debugPracticeLocalSide = -1;
    if (g_debugSwitchPlayersAvailable) {
        g_debugPracticeLocalSide = SwitchPlayers::GetLocalSide();
    }

    if (g_debugPracticeLocalSide == 0) {
        strncpy_s(g_debugLocalSideInfo, sizeof(g_debugLocalSideInfo), "Current Local: P1", _TRUNCATE);
    } else if (g_debugPracticeLocalSide == 1) {
        strncpy_s(g_debugLocalSideInfo, sizeof(g_debugLocalSideInfo), "Current Local: P2", _TRUNCATE);
    } else if (g_debugSwitchPlayersAvailable) {
        strncpy_s(g_debugLocalSideInfo, sizeof(g_debugLocalSideInfo), "Current Local: (unknown)", _TRUNCATE);
    } else {
        strncpy_s(g_debugLocalSideInfo, sizeof(g_debugLocalSideInfo), "Current Local: Practice mode only", _TRUNCATE);
    }

    _snprintf_s(g_debugAiControlInfo, sizeof(g_debugAiControlInfo), _TRUNCATE,
                I18n::Tr("AI Control Flags: P1=%s  P2=%s"),
                I18n::Tr(IsAIControlFlagHuman(1) ? "Human" : "AI"),
                I18n::Tr(IsAIControlFlagHuman(2) ? "Human" : "AI"));

    uint8_t p2CpuFlag = 0xFF;
    uintptr_t efzBase = GetEFZBase();
    if (efzBase) {
        uintptr_t gameStatePtr = 0;
        if (SafeReadMemory(efzBase + EFZ_BASE_OFFSET_GAME_STATE, &gameStatePtr, sizeof(gameStatePtr)) && gameStatePtr) {
            SafeReadMemory(gameStatePtr + 4931, &p2CpuFlag, sizeof(p2CpuFlag));
        }
    }
    if (p2CpuFlag != 0xFF) {
        _snprintf_s(g_debugPracticeCpuInfo, sizeof(g_debugPracticeCpuInfo), _TRUNCATE,
                    I18n::Tr("Practice P2 CPU flag: %s (byte=%u)"), I18n::Tr(p2CpuFlag ? "CPU" : "Human"), (unsigned)p2CpuFlag);
    } else {
        strncpy_s(g_debugPracticeCpuInfo, sizeof(g_debugPracticeCpuInfo), "Practice P2 CPU flag: unknown", _TRUNCATE);
    }

    uint8_t curSpeed = 0xFF;
    if (HMODULE hEfz = GetModuleHandleA("efz.exe")) {
        uint32_t rootPtr = 0;
        if (SafeReadMemory(reinterpret_cast<uintptr_t>(hEfz) + 0x39010C, &rootPtr, sizeof(rootPtr)) && rootPtr) {
            SafeReadMemory(static_cast<uintptr_t>(rootPtr) + 0xF7FF8, &curSpeed, sizeof(curSpeed));
        }
    }
    if (curSpeed != 0xFF) {
        _snprintf_s(g_debugGamespeedInfo, sizeof(g_debugGamespeedInfo), _TRUNCATE,
                    I18n::Tr("Gamespeed: %u (0=freeze, 3=normal)"), (unsigned)curSpeed);
    } else {
        strncpy_s(g_debugGamespeedInfo, sizeof(g_debugGamespeedInfo), "Gamespeed: unknown", _TRUNCATE);
    }

    auto fillRfInfo = [](int player, char* buf, size_t bufSz, bool& activeOut) {
        bool active = false;
        bool colorManaged = false;
        bool colorBlue = false;
        double value = 0.0;
        activeOut = false;
        if (!GetRFFreezeStatus(player, active, value, colorManaged, colorBlue)) {
            _snprintf_s(buf, bufSz, _TRUNCATE, I18n::Tr("P%d RF Freeze: unknown"), player);
            return;
        }
        activeOut = active;
        if (!active) {
            _snprintf_s(buf, bufSz, _TRUNCATE, I18n::Tr("P%d RF Freeze: inactive"), player);
            return;
        }

        const RFFreezeOrigin origin = GetRFFreezeOrigin(player);
        const char* originLabel = "Unknown";
        if (origin == RFFreezeOrigin::ManualUI) originLabel = "Manual UI";
        else if (origin == RFFreezeOrigin::ContinuousRecovery) originLabel = "Continuous Recovery";
        else if (origin == RFFreezeOrigin::Other) originLabel = "Other";

        _snprintf_s(buf, bufSz, _TRUNCATE,
                    I18n::Tr("P%d RF Freeze: ACTIVE  RF=%.1f  Color=%s%s  Source=%s"),
                    player,
                    value,
                    I18n::Tr(colorManaged ? "Locked" : "Off"),
                    I18n::Tr(colorManaged ? (colorBlue ? " (Blue)" : " (Red)") : ""),
                    I18n::Tr(originLabel));
    };
    fillRfInfo(1, g_debugRfFreezeP1Info, sizeof(g_debugRfFreezeP1Info), g_debugRfFreezeP1Active);
    fillRfInfo(2, g_debugRfFreezeP2Info, sizeof(g_debugRfFreezeP2Info), g_debugRfFreezeP2Active);
}

void RefreshDebugMirrors() {
    g_mirrorOverlayBorders = g_ShowOverlayDebugBorders.load();
    g_mirrorRGToasts       = g_ShowRGDebugToasts.load();
    g_mirrorPadInputLog    = XInputShim::g_LogGenericPadInputDebug.load();
    g_mirrorDeepFA         = g_deepFrameAdvDebug.load();
    g_mirrorDisableHud     = HudDisable::IsHidden();
    g_mirrorHudTopBar      = HudDisable::IsElementDisabled(HudDisable::ElemTopBar);
    g_mirrorHudTimer       = HudDisable::IsElementDisabled(HudDisable::ElemTimer);
    g_mirrorHudPortraits   = HudDisable::IsElementDisabled(HudDisable::ElemPortraits);
    g_mirrorHudHpBars      = HudDisable::IsElementDisabled(HudDisable::ElemHpBars);
    g_mirrorHudRoundDots   = HudDisable::IsElementDisabled(HudDisable::ElemRoundDots);
    g_mirrorHudNameplates  = HudDisable::IsElementDisabled(HudDisable::ElemNameplates);
    g_mirrorHudBottomBar   = HudDisable::IsElementDisabled(HudDisable::ElemBottomBar);
    g_mirrorHudSpMeter     = HudDisable::IsElementDisabled(HudDisable::ElemSpMeter);
    g_mirrorHudRfGauge     = HudDisable::IsElementDisabled(HudDisable::ElemRfGauge);
    g_mirrorHudCombo       = HudDisable::IsElementDisabled(HudDisable::ElemComboPanel);
}

void OnOverlayBorders() { g_ShowOverlayDebugBorders.store(g_mirrorOverlayBorders); }
void OnRGToasts()       { g_ShowRGDebugToasts.store(g_mirrorRGToasts); }
void OnDisableHud()     { HudDisable::SetHidden(g_mirrorDisableHud); }
void OnHudElementsChanged() {
    HudDisable::SetElementDisabled(HudDisable::ElemTopBar,     g_mirrorHudTopBar);
    HudDisable::SetElementDisabled(HudDisable::ElemTimer,      g_mirrorHudTimer);
    HudDisable::SetElementDisabled(HudDisable::ElemPortraits,  g_mirrorHudPortraits);
    HudDisable::SetElementDisabled(HudDisable::ElemHpBars,     g_mirrorHudHpBars);
    HudDisable::SetElementDisabled(HudDisable::ElemRoundDots,  g_mirrorHudRoundDots);
    HudDisable::SetElementDisabled(HudDisable::ElemNameplates, g_mirrorHudNameplates);
    HudDisable::SetElementDisabled(HudDisable::ElemBottomBar,  g_mirrorHudBottomBar);
    HudDisable::SetElementDisabled(HudDisable::ElemSpMeter,    g_mirrorHudSpMeter);
    HudDisable::SetElementDisabled(HudDisable::ElemRfGauge,    g_mirrorHudRfGauge);
    HudDisable::SetElementDisabled(HudDisable::ElemComboPanel, g_mirrorHudCombo);
}
const char* ValHudDisable() {
    if (HudDisable::IsHidden()) return "All hidden";
    const unsigned bits[] = {
        HudDisable::ElemTopBar, HudDisable::ElemTimer, HudDisable::ElemPortraits,
        HudDisable::ElemHpBars, HudDisable::ElemRoundDots, HudDisable::ElemNameplates,
        HudDisable::ElemBottomBar, HudDisable::ElemSpMeter, HudDisable::ElemRfGauge,
        HudDisable::ElemComboPanel };
    for (unsigned b : bits) if (HudDisable::IsElementDisabled(b)) return "Custom";
    return "Shown";
}
Row* BuildHudDisableRows(int& count) {
    static Row s_rows[16];
    int n = 0;
    s_rows[n++] = Header("GAME HUD");
    s_rows[n++] = Toggle("HIDE ENTIRE HUD", &g_mirrorDisableHud, OnDisableHud);
    s_rows[n++] = Header("PER ELEMENT");
    s_rows[n++] = Toggle("Top Bar",     &g_mirrorHudTopBar,     OnHudElementsChanged);
    s_rows[n++] = Toggle("Timer",       &g_mirrorHudTimer,      OnHudElementsChanged);
    s_rows[n++] = Toggle("Portraits",   &g_mirrorHudPortraits,  OnHudElementsChanged);
    s_rows[n++] = Toggle("HP Bars",     &g_mirrorHudHpBars,     OnHudElementsChanged);
    s_rows[n++] = Toggle("Round Dots",  &g_mirrorHudRoundDots,  OnHudElementsChanged);
    s_rows[n++] = Toggle("Name Plates", &g_mirrorHudNameplates, OnHudElementsChanged);
    s_rows[n++] = Toggle("Bottom Bar",  &g_mirrorHudBottomBar,  OnHudElementsChanged);
    s_rows[n++] = Toggle("SP Meter",    &g_mirrorHudSpMeter,    OnHudElementsChanged);
    s_rows[n++] = Toggle("RF Gauge",    &g_mirrorHudRfGauge,    OnHudElementsChanged);
    s_rows[n++] = Toggle("Combo Panel", &g_mirrorHudCombo,      OnHudElementsChanged);
    count = n;
    return s_rows;
}
static bool g_mirrorMissionInspector = false;
void OnMissionInspector() { Mission::Engine::SetInspectorEnabled(g_mirrorMissionInspector); }
void ActLoadLatestMission() {
    std::string msg;
    const bool ok = Mission::Engine::Runner::LoadLatestRecorded(msg);
    DirectDrawHook::AddMessage(ok ? ("Mission loaded: " + msg) : ("Load failed: " + msg),
        "SYSTEM", ok ? RGB(180, 255, 220) : RGB(255, 120, 120), 3000, 0, 120);
}
void ActResetMission() {
    if (Mission::Engine::Runner::IsActive()) {
        std::string restoreMessage;
        const bool restored =
            Mission::Engine::Runner::RequestBaselineRestore(restoreMessage);
        DirectDrawHook::AddMessage(
            restored ? "Mission reset" : "Mission reset unavailable: " + restoreMessage,
            "SYSTEM", restored ? RGB(180, 255, 220) : RGB(255, 180, 120),
            restored ? 1500 : 2400, 0, 120);
    }
}
const char* ValMissionRecordSteps() {
    static char b[48];
    _snprintf_s(b, sizeof(b), _TRUNCATE, I18n::Tr("%d steps / %d setup breaks"),
                Mission::Engine::Recorder::GetStepCount(),
                Mission::Engine::Recorder::GetComboEndCount());
    return b;
}
void OnPadInputLog()    { XInputShim::g_LogGenericPadInputDebug.store(g_mirrorPadInputLog); }
void OnDeepFA()         { g_deepFrameAdvDebug.store(g_mirrorDeepFA); }
int g_bgmSlot = 1;

char g_hotswapOstTrack08Label[64] = "08 - Character Selection (BME)";

const char* const kNamedStageChoices[] = {
    "00 - Courtyard of the Full Moon",
    "01 - Snowy Park (Night)",
    "02 - Lunch Break Courtyard",
    "03 - School Road Park (Day)",
    "04 - School Road Park (Night)",
    "05 - Sunset Rooftop",
    "06 - Shopping Street",
    "07 - Tree of Beginnings",
    "08 - Minase House (Day)",
    "09 - Gymnasium",
    "10 - Rainy Field",
    "11 - Behind the School",
    "12 - World of Eternity",
    "13 - Abandoned Station (Day)",
    "14 - Shrine Near the Sky (Day)",
    "15 - Kamio House",
    "16 - The Infinite Sky",
    "17 - Minase House (Night)",
    "18 - Abandoned Station (Dusk)",
    "19 - FARGO Research Facility",
    "20 - Monomi Hill",
    "21 - Shrine Near the Sky (Night)",
    "22 - Snowy Park (Day)",
};

const unsigned short kNamedOstTracks[] = {
    150,
    0,
    1,
    5,
    6,
    7,
    8,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25,
    26,
    27,
    28,
    29,
    30,
    31,
    32,
};

const char* const kNamedOstChoices[] = {
    "OFF (150)",
    "00 - Character Selection",
    "01 - Character Selection (Practice)",
    "05 - Staff Roll",
    "06 - Replay Menu OST",
    "07 - Config Menu OST",
    g_hotswapOstTrack08Label,
    "10 - Courtyard of the Full Moon",
    "11 - Snowy Park (Night)",
    "12 - Lunch Break Courtyard",
    "13 - School Road Park (Day)",
    "14 - School Road Park (Night)",
    "15 - Sunset Rooftop",
    "16 - Shopping Street",
    "17 - Tree of Beginnings",
    "18 - Minase House (Day)",
    "19 - Gymnasium",
    "20 - Rainy Field",
    "21 - Behind the School",
    "22 - World of Eternity",
    "23 - Abandoned Station (Day)",
    "24 - Shrine Near the Sky (Day)",
    "25 - Kamio House",
    "26 - The Infinite Sky",
    "27 - Minase House (Night)",
    "28 - Abandoned Station (Dusk)",
    "29 - FARGO Research Facility",
    "30 - Monomi Hill",
    "31 - Shrine Near the Sky (Night)",
    "32 - Snowy Park (Day)",
};

constexpr int kNamedStageChoiceCount = static_cast<int>(sizeof(kNamedStageChoices) / sizeof(kNamedStageChoices[0]));
constexpr int kNamedOstChoiceCount = static_cast<int>(sizeof(kNamedOstChoices) / sizeof(kNamedOstChoices[0]));

const char* GetNamedStageLabel(int stageId) {
    return (stageId >= 0 && stageId < kNamedStageChoiceCount)
        ? kNamedStageChoices[stageId]
        : "UNKNOWN STAGE";
}

bool NetplayModLoaded() {
    return GetModuleHandleA("efz_netplay_mod.dll") != nullptr
        || GetModuleHandleA("efz_netplay_mod") != nullptr;
}

void RefreshNamedOstChoices() {
    _snprintf_s(g_hotswapOstTrack08Label,
                sizeof(g_hotswapOstTrack08Label),
                _TRUNCATE,
                "%s",
                NetplayModLoaded() ? "08 - Netplay Menu OST" : "08 - Character Selection (BME)");
}

int FindNamedOstChoiceIndexByTrack(int trackNumber) {
    for (int i = 0; i < kNamedOstChoiceCount; ++i) {
        if (static_cast<int>(kNamedOstTracks[i]) == trackNumber) {
            return i;
        }
    }
    return -1;
}

unsigned short TrackForNamedOstChoice(int choiceIdx) {
    if (choiceIdx < 0 || choiceIdx >= kNamedOstChoiceCount) {
        return 150;
    }
    return kNamedOstTracks[choiceIdx];
}

int CharacterSelectIdFromInternalCharacterId(int internalCharId) {
    const std::string resourceName = CharacterSettings::GetCharacterInternalName(internalCharId);
    return CharacterHotswap::GetSelectIdForResourceName(resourceName.c_str());
}
void InvalidateMissionReviewReadiness();
void RequestInvalidateMissionReviewReadiness();
void ActArmMissionRecording() {
    PrepareNewMissionAuthoringSession();
    Mission::Engine::Recorder::Arm();
    if (ImGuiImpl::IsVisible()) ImGuiImpl::ToggleVisibility();
}
void ActAdvanceMissionRecording() {
    const auto phase = Mission::Engine::Recorder::GetPhase();
    Mission::Engine::Recorder::Advance();
    if (phase == Mission::Engine::Recorder::Phase::PreRecord && ImGuiImpl::IsVisible()) {
        ImGuiImpl::ToggleVisibility();
    }
}
void ActRetakeMissionRecording() {
    RequestInvalidateMissionReviewReadiness();
    Mission::Engine::Recorder::Retake();
    if (ImGuiImpl::IsVisible()) ImGuiImpl::ToggleVisibility();
}
void ActDiscardMissionRecording() {
    Mission::Engine::Recorder::Cancel();
    DirectDrawHook::AddMessage("Mission recording discarded", "SYSTEM",
                               RGB(255, 200, 120), 1200, 0, 120);
}
const char* ValMissionAuthoringState() {
    switch (Mission::Engine::Recorder::GetPhase()) {
        case Mission::Engine::Recorder::Phase::PreRecord: return "PRE-RECORD";
        case Mission::Engine::Recorder::Phase::CountIn:   return "COUNT-IN";
        case Mission::Engine::Recorder::Phase::Recording: return "RECORDING";
        case Mission::Engine::Recorder::Phase::Review:    return "REVIEW";
        default: return "IDLE";
    }
}

bool g_bgmChoiceSeeded = false;

void SeedDebugBgmChoiceIfNeeded() {
    RefreshNamedOstChoices();
    if (g_bgmChoiceSeeded) {
        return;
    }

    uintptr_t gameStatePtr = GetGameStatePtr();
    if (gameStatePtr) {
        const unsigned short observedTrack = GetLastBgmTrack();
        const int currentTrack = observedTrack == 0xFFFFu
            ? -1 : static_cast<int>(observedTrack);
        const int ostChoice = FindNamedOstChoiceIndexByTrack(currentTrack);
        if (ostChoice >= 0) {
            g_bgmSlot = ostChoice;
        }
    }

    if (g_bgmSlot < 0 || g_bgmSlot >= kNamedOstChoiceCount) {
        g_bgmSlot = 0;
    }
    g_bgmChoiceSeeded = true;
}

void RunStopBGM() {
    uintptr_t gameStatePtr = GetGameStatePtr();
    if (!gameStatePtr) return;
    StopBGM(gameStatePtr);
}
void RunPlayBGM() {
    SeedDebugBgmChoiceIfNeeded();
    uintptr_t gameStatePtr = GetGameStatePtr();
    if (!gameStatePtr) return;
    if (RevivalBgmMuted()) {
        LogOut("[BGM] PLAY BGM ignored because Revival MuteBGM is enabled", true);
        return;
    }
    PlayBGM(gameStatePtr, TrackForNamedOstChoice(g_bgmSlot));
}
void RunP1FinalMemory() {
    const auto& d = ImGuiGui::guiState.localData;
    ExecuteFinalMemory(1, d.p1CharID);
}
void RunP2FinalMemory() {
    const auto& d = ImGuiGui::guiState.localData;
    ExecuteFinalMemory(2, d.p2CharID);
}

void OnFrameBarPersist();
void OnFrameBarTiming();
void OnFrameBarDetail();
bool FrameBarOptionsHidden();
const char* const kFrameBarTimingChoices[2] = { "SUBFRAMES", "VISUAL FRAMES" };
const char* const kFrameBarDetailChoices[3] = { "FULL", "COMPACT", "BARS ONLY" };

const char* ValDebugLogging() { return MutableSettings().detailedLogging ? "DETAILED" : "NORMAL"; }
const char* ValDebugOverlays() {
    return (g_ShowOverlayDebugBorders.load() || g_ShowRGDebugToasts.load()) ? "ON" : "TOOLS";
}
const char* ValDebugSavestate() {
    return CustomSavestate::BackendModeName(CustomSavestate::GetConfiguredBackendMode());
}
const char* ValDebugBgm() {
    SeedDebugBgmChoiceIfNeeded();
    if (g_bgmSlot >= 0 && g_bgmSlot < kNamedOstChoiceCount) {
        return kNamedOstChoices[g_bgmSlot];
    }
    return "OFF (150)";
}
const char* ValFinalMemoryTools() { return "RUN"; }
const char* ValDebugRuntime() {
    if (g_debugPracticeLocalSide == 0) return "P1 LOCAL";
    if (g_debugPracticeLocalSide == 1) return "P2 LOCAL";
    if (g_debugRfFreezeP1Active || g_debugRfFreezeP2Active) return "RF ACTIVE";
    return g_debugSwitchPlayersAvailable ? "LIVE" : "PRACTICE";
}

Row* BuildDebugLoggingRows(int& count) {
    static Row s_rows[16];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("DEBUG LOGGING");
    s_rows[n++] = Toggle ("DETAILED LOGGING",     &s.detailedLogging,      OnDetailedLogging);
    s_rows[n++] = Toggle ("DEBUG FILE LOG",       &s.enableDebugFileLog, OnDebugFileLogging);
    s_rows[n++] = Toggle ("FPS DIAGNOSTICS",      &s.enableFpsDiagnostics, OnFpsDiag);
    s_rows[n++] = Toggle ("SHOW DEBUG CONSOLE",   &s.enableConsole,        OnShowConsole);
    s_rows[n++] = Toggle ("CHAR SELECT LOGGER",   &s.enableCharacterSelectLogger,
        [](){ PersistBool("General", "enableCharacterSelectLogger", MutableSettings().enableCharacterSelectLogger); });
    s_rows[n++] = Toggle ("LOG CONTROLLER INPUT", &g_mirrorPadInputLog,    OnPadInputLog);
    s_rows[n++] = Toggle ("LOG DETAILED FA",      &g_mirrorDeepFA,         OnDeepFA);
    count = n;
    return s_rows;
}

// The modal target must belong to the persistent authoring form, never to a
// per-frame Row.
struct MissionTextEditorState {
    bool active = false;
    bool wantFocus = false;
    bool multiline = false;
    const char* title = "MISSION TEXT";
    const char* help = "Enter the text shown in the mission browser.";
    std::size_t maxBytes = 0;
    std::string* target = nullptr;
    std::string original;
    std::vector<char> buffer;
};
MissionTextEditorState g_missionTextEditor;

void OpenMissionTextEditor(const char* title, const char* help,
                           std::string* target, bool multiline,
                           std::size_t maxBytes) {
    if (!target) return;
    g_missionTextEditor.active = true;
    g_missionTextEditor.wantFocus = true;
    g_missionTextEditor.multiline = multiline;
    g_missionTextEditor.title = (title && *title) ? title : "MISSION TEXT";
    g_missionTextEditor.help = (help && *help)
        ? help : "Enter the text shown in the mission browser.";
    g_missionTextEditor.maxBytes = maxBytes;
    g_missionTextEditor.target = target;
    g_missionTextEditor.original = *target;
    const std::size_t capacity = (std::max)(maxBytes + 1u, target->size() + 1u);
    g_missionTextEditor.buffer.assign(capacity, '\0');
    if (!target->empty()) {
        memcpy(g_missionTextEditor.buffer.data(), target->data(),
               (std::min)(target->size(), capacity - 1));
    }
    Input::ResetEdges();
}

void CloseMissionTextEditor(bool commit) {
    if (!g_missionTextEditor.active) return;
    if (g_missionTextEditor.target) {
        *g_missionTextEditor.target = commit
            ? std::string(g_missionTextEditor.buffer.data())
            : g_missionTextEditor.original;
    }
    g_missionTextEditor = MissionTextEditorState{};
    Input::ResetEdges();
}

constexpr const char* kAuthorDifficultyChoices[] = {
    "CHOOSE DIFFICULTY", "NOVICE", "BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"
};
constexpr int kAuthorDifficultyChoiceCount =
    static_cast<int>(sizeof(kAuthorDifficultyChoices) /
                     sizeof(kAuthorDifficultyChoices[0]));
constexpr const char* kAuthorTypeChoices[] = { "TRIAL", "MISSION" };
constexpr int kAuthorTypeChoiceCount =
    static_cast<int>(sizeof(kAuthorTypeChoices) / sizeof(kAuthorTypeChoices[0]));
constexpr const char* kMissingAuthoringPackWarning =
    "The selected pack is no longer available. Choose a destination again.";

struct MissionAuthoringForm {
    Mission::Engine::Recorder::Phase previousPhase =
        Mission::Engine::Recorder::Phase::Idle;
    bool sessionSeeded = false;
    bool packsLoaded = false;

    std::string name;
    std::string description;
    int difficultyIndex = 0;
    int typeIndex = 0;
    int packIndex = 0;
    bool packSelectionValid = true;
    bool missingPackWarning = false;
    int categoryIndex = 0;
    std::string rememberedPackPath;

    std::vector<Mission::Authoring::PackSummary> packs;
    std::vector<std::string> packLabels;
    std::vector<const char*> packChoices;
    std::vector<std::string> categoryLabels;
    std::vector<const char*> categoryChoices;
    std::vector<std::string> missionLabels;
    std::vector<const char*> missionChoices;

    // Seed keys are stable IDs/paths, not display labels.
    std::string editSeedPackPath;
    std::string editSeedCategoryId;
    std::string editSeedMissionId;
    std::string editPackName;
    std::string editPackAuthor;
    std::string editPackDescription;
    std::string editPackVersion;
    std::string editCategoryName;
    std::string editCategoryDescription;
    int missionIndex = 0;
    std::string editMissionName;
    std::string editMissionDescription;
    int editMissionTypeIndex = 0;
    int editMissionDifficultyIndex = 1;
    int editMissionCategoryIndex = 0;

    std::string newPackName;
    std::string newPackAuthor;
    std::string newPackDescription;
    std::string newPackVersion = "1.0.0";
    std::string newCategoryName;
    std::string newCategoryDescription;
    std::string status;
    bool reviewReadinessCaptured = false;
    bool takePublishReady = false;
    std::string takePublishBlocker;
    bool categoryChoicesDirty = false;
    bool rescanPending = false;
    bool createPackPending = false;
    bool createCategoryPending = false;
    bool updatePackPending = false;
    bool updateCategoryPending = false;
    bool updateMissionPending = false;
    bool invalidateReadinessPending = false;
};
MissionAuthoringForm g_missionAuthoring;
std::atomic<bool> g_prepareNewMissionAuthoringRequested{false};
std::atomic<bool> g_authoringRescanRequested{false};

void RequestInvalidateMissionReviewReadiness() {
    g_missionAuthoring.invalidateReadinessPending = true;
}

void InvalidateMissionReviewReadiness() {
    g_missionAuthoring.reviewReadinessCaptured = false;
    g_missionAuthoring.takePublishReady = false;
    g_missionAuthoring.takePublishBlocker.clear();
}

void ShowMissionAuthoringResult(bool ok, const std::string& text) {
    g_missionAuthoring.status = text;
    DirectDrawHook::AddMessage(
        text.c_str(), "MISSION AUTHORING",
        ok ? RGB(180, 255, 220) : RGB(255, 130, 120),
        ok ? 2600 : 4200, 0, 120);
}

void RebuildAuthoringCategoryChoices() {
    auto& form = g_missionAuthoring;
    form.categoryLabels.clear();
    form.categoryChoices.clear();
    if (form.packSelectionValid && form.packIndex >= 0 &&
        form.packIndex < static_cast<int>(form.packs.size())) {
        const auto& categories = form.packs[form.packIndex].categories;
        form.categoryLabels.reserve(categories.size());
        for (const auto& category : categories) {
            form.categoryLabels.push_back(
                category.label.empty() ? category.id : category.label);
        }
    }
    form.categoryChoices.reserve(form.categoryLabels.size());
    for (const std::string& label : form.categoryLabels) {
        form.categoryChoices.push_back(label.c_str());
    }
    if (form.categoryChoices.empty()) {
        form.categoryIndex = 0;
    } else if (form.categoryIndex < 0 ||
               form.categoryIndex >= static_cast<int>(form.categoryChoices.size())) {
        form.categoryIndex = 0;
    }
}

const Mission::Authoring::PackSummary* SelectedAuthoringPack();
const Mission::PackCategory* SelectedAuthoringCategory();

void RebuildAuthoringMissionChoices() {
    auto& form = g_missionAuthoring;
    form.missionLabels.clear();
    form.missionChoices.clear();
    if (form.packSelectionValid && form.packIndex >= 0 &&
        form.packIndex < static_cast<int>(form.packs.size())) {
        const auto& missions = form.packs[form.packIndex].scenarios;
        form.missionLabels.reserve(missions.size());
        for (const auto& mission : missions) {
            form.missionLabels.push_back(
                mission.name.empty() ? mission.id : mission.name);
        }
    }
    form.missionChoices.reserve(form.missionLabels.size());
    for (const std::string& label : form.missionLabels) {
        form.missionChoices.push_back(label.c_str());
    }
    if (form.missionChoices.empty()) {
        form.missionIndex = 0;
    } else if (form.missionIndex < 0 ||
               form.missionIndex >= static_cast<int>(form.missionChoices.size())) {
        form.missionIndex = 0;
    }
}

void SeedSelectedAuthoringEditors(bool force = false) {
    auto& form = g_missionAuthoring;
    const auto* pack = SelectedAuthoringPack();
    if (!pack) {
        form.editSeedPackPath.clear();
        form.editSeedCategoryId.clear();
        form.editSeedMissionId.clear();
        return;
    }
    if (force || form.editSeedPackPath != pack->packJsonPath) {
        form.editSeedPackPath = pack->packJsonPath;
        form.editPackName = pack->name;
        form.editPackAuthor = pack->author;
        form.editPackDescription = pack->description;
        form.editPackVersion = pack->version;
        form.editSeedCategoryId.clear();
        form.editSeedMissionId.clear();
    }

    const auto* category = SelectedAuthoringCategory();
    const std::string categoryId = category ? category->id : std::string();
    if (force || form.editSeedCategoryId != categoryId) {
        form.editSeedCategoryId = categoryId;
        form.editCategoryName = category ? category->label : std::string();
        form.editCategoryDescription = category ? category->description : std::string();
    }

    const Mission::Authoring::ScenarioSummary* mission = nullptr;
    if (form.missionIndex >= 0 &&
        form.missionIndex < static_cast<int>(pack->scenarios.size())) {
        mission = &pack->scenarios[form.missionIndex];
    }
    const std::string missionId = mission ? mission->id : std::string();
    if (force || form.editSeedMissionId != missionId) {
        form.editSeedMissionId = missionId;
        form.editMissionName = mission ? mission->name : std::string();
        form.editMissionDescription = mission ? mission->description : std::string();
        form.editMissionDifficultyIndex = mission && mission->difficulty >= 1 &&
                                                   mission->difficulty <= 5
            ? mission->difficulty : 1;
        const std::string type = mission ? mission->type : std::string();
        form.editMissionTypeIndex = _stricmp(type.c_str(), "mission") == 0 ? 1 : 0;
        form.editMissionCategoryIndex = 0;
        if (mission) {
            for (int i = 0; i < static_cast<int>(pack->categories.size()); ++i) {
                if (pack->categories[i].id == mission->categoryId) {
                    form.editMissionCategoryIndex = i;
                    break;
                }
            }
        }
    }
}

bool RefreshAuthoringPacks(bool keepSelection = true) {
    auto& form = g_missionAuthoring;
    form.status.clear();
    std::string wanted = keepSelection ? form.rememberedPackPath : std::string();
    std::string wantedCategoryId;
    std::string wantedMissionId;
    if (keepSelection && form.packSelectionValid && form.packIndex >= 0 &&
        form.packIndex < static_cast<int>(form.packs.size())) {
        const auto& previousCategories = form.packs[form.packIndex].categories;
        if (form.categoryIndex >= 0 &&
            form.categoryIndex < static_cast<int>(previousCategories.size())) {
            wantedCategoryId = previousCategories[form.categoryIndex].id;
        }
        const auto& previousMissions = form.packs[form.packIndex].scenarios;
        if (form.missionIndex >= 0 &&
            form.missionIndex < static_cast<int>(previousMissions.size())) {
            wantedMissionId = previousMissions[form.missionIndex].id;
        }
    }
    if (wanted.empty() && form.packIndex >= 0 &&
        form.packIndex < static_cast<int>(form.packs.size())) {
        wanted = form.packs[form.packIndex].packJsonPath;
    }

    std::vector<Mission::Authoring::PackSummary> discovered;
    std::string error;
    if (!Mission::Authoring::EnumeratePackSummaries(
            Mission::ResolveMissionsRoot(), discovered, error)) {
        form.status = "Pack scan failed: " + error;
        discovered.clear();
        form.packs.clear();
        form.packLabels.clear();
        form.packChoices.clear();
        form.categoryLabels.clear();
        form.categoryChoices.clear();
        form.missionLabels.clear();
        form.missionChoices.clear();
        form.packSelectionValid = false;
        form.missingPackWarning = false;
        form.packsLoaded = true;
        return false;
    }
    if (!error.empty()) form.status = "Pack scan warning: " + error;

    // Only packs explicitly created for local authoring are mutation targets here.
    form.packs.clear();
    for (auto& pack : discovered) {
        if (pack.editable) form.packs.push_back(std::move(pack));
    }
    form.packLabels.clear();
    form.packChoices.clear();
    int selected = 0;
    bool foundWanted = wanted.empty();
    std::vector<std::string> baseLabels;
    baseLabels.reserve(form.packs.size());
    for (const auto& pack : form.packs) {
        std::string label = pack.name.empty() ? pack.id : pack.name;
        if (!pack.version.empty()) label += "  v" + pack.version;
        baseLabels.push_back(std::move(label));
    }
    for (int i = 0; i < static_cast<int>(form.packs.size()); ++i) {
        const auto& pack = form.packs[i];
        std::string label = baseLabels[i];
        bool duplicateDisplay = false;
        for (int j = 0; j < static_cast<int>(baseLabels.size()); ++j) {
            if (i != j && _stricmp(baseLabels[i].c_str(), baseLabels[j].c_str()) == 0) {
                duplicateDisplay = true;
                break;
            }
        }
        if (duplicateDisplay) {
            // Folder is the final discriminator: copied manifests can retain the
            // same pack id, author, name, and version.
            const std::size_t packSlash = pack.packJsonPath.find_last_of("\\/");
            const std::string folder = packSlash == std::string::npos
                ? pack.packJsonPath : pack.packJsonPath.substr(0, packSlash);
            const std::size_t folderSlash = folder.find_last_of("\\/");
            const std::string discriminator = folderSlash == std::string::npos
                ? folder : folder.substr(folderSlash + 1);
            if (!discriminator.empty()) label += "  [" + discriminator + "]";
        }
        form.packLabels.push_back(std::move(label));
        if (!wanted.empty() && pack.packJsonPath == wanted) {
            selected = i;
            foundWanted = true;
        }
    }
    form.packChoices.reserve(form.packLabels.size());
    for (const std::string& label : form.packLabels) {
        form.packChoices.push_back(label.c_str());
    }
    form.packIndex = form.packs.empty() ? 0 : selected;
    form.packSelectionValid = !form.packs.empty() && foundWanted;
    form.missingPackWarning = !form.packs.empty() && !wanted.empty() && !foundWanted;
    if (form.packSelectionValid) {
        form.rememberedPackPath = form.packs[form.packIndex].packJsonPath;
        form.categoryIndex = 0;
        form.missionIndex = 0;
        if (!wantedCategoryId.empty()) {
            const auto& categories = form.packs[form.packIndex].categories;
            for (int i = 0; i < static_cast<int>(categories.size()); ++i) {
                if (categories[i].id == wantedCategoryId) {
                    form.categoryIndex = i;
                    break;
                }
            }
        }
        if (!wantedMissionId.empty()) {
            const auto& missions = form.packs[form.packIndex].scenarios;
            for (int i = 0; i < static_cast<int>(missions.size()); ++i) {
                if (missions[i].id == wantedMissionId) {
                    form.missionIndex = i;
                    break;
                }
            }
        }
    } else if (form.missingPackWarning) {
        if (form.status.empty()) form.status = kMissingAuthoringPackWarning;
        else form.status += std::string(" ") + kMissingAuthoringPackWarning;
    }
    form.packsLoaded = true;
    RebuildAuthoringCategoryChoices();
    RebuildAuthoringMissionChoices();
    SeedSelectedAuthoringEditors(true);
    return true;
}

void SeedMissionAuthoringSessionIfNeeded(
    Mission::Engine::Recorder::Phase phase) {
    auto& form = g_missionAuthoring;
    const bool enteringSession =
        phase != Mission::Engine::Recorder::Phase::Idle &&
        form.previousPhase == Mission::Engine::Recorder::Phase::Idle;
    if (enteringSession) {
        form.name.clear();
        form.description.clear();
        form.difficultyIndex = 0;
        form.typeIndex = 0;
        form.status.clear();
        form.reviewReadinessCaptured = false;
        form.sessionSeeded = true;
        RefreshAuthoringPacks(true);
    } else if (!form.packsLoaded) {
        RefreshAuthoringPacks(true);
    }
    if (phase == Mission::Engine::Recorder::Phase::Idle) {
        form.sessionSeeded = false;
    }
    form.previousPhase = phase;
}

void RefreshRecordedTakeReadiness() {
    auto& form = g_missionAuthoring;
    form.takePublishBlocker.clear();
    form.takePublishReady = Mission::Engine::Recorder::CanPublishRecorded(
        form.takePublishBlocker);
    form.reviewReadinessCaptured = true;
}

const Mission::Authoring::PackSummary* SelectedAuthoringPack() {
    const auto& form = g_missionAuthoring;
    return form.packSelectionValid && form.packIndex >= 0 &&
           form.packIndex < static_cast<int>(form.packs.size())
        ? &form.packs[form.packIndex] : nullptr;
}

const Mission::PackCategory* SelectedAuthoringCategory() {
    const auto* pack = SelectedAuthoringPack();
    const int index = g_missionAuthoring.categoryIndex;
    return pack && index >= 0 && index < static_cast<int>(pack->categories.size())
        ? &pack->categories[index] : nullptr;
}

void OnAuthoringPackChanged() {
    auto& form = g_missionAuthoring;
    const bool wasMissing = form.missingPackWarning;
    form.packSelectionValid = form.packIndex >= 0 &&
        form.packIndex < static_cast<int>(form.packs.size());
    form.missingPackWarning = false;
    if (form.packSelectionValid && wasMissing) {
        const std::size_t warningPos = form.status.find(kMissingAuthoringPackWarning);
        if (warningPos != std::string::npos) {
            std::size_t eraseFrom = warningPos;
            if (eraseFrom > 0 && form.status[eraseFrom - 1] == ' ') --eraseFrom;
            form.status.erase(eraseFrom, std::strlen(kMissingAuthoringPackWarning) +
                                         (warningPos - eraseFrom));
        }
    }
    form.categoryIndex = 0;
    form.missionIndex = 0;
    if (const auto* pack = SelectedAuthoringPack()) {
        form.rememberedPackPath = pack->packJsonPath;
    }
    // Rebuilding here would invalidate the sibling CATEGORY Row's raw choices
    // pointer; the next builder pass applies it.
    form.categoryChoicesDirty = true;
    form.editSeedPackPath.clear();
}

void OnAuthoringCategoryChanged() {
    g_missionAuthoring.editSeedCategoryId.clear();
}

void OnAuthoringMissionChanged() {
    g_missionAuthoring.editSeedMissionId.clear();
}

void ActEditTrialName() {
    OpenMissionTextEditor("SESSION NAME", "Required. This is the title players see in the browser.",
                          &g_missionAuthoring.name, false, 128);
}
void ActEditTrialDescription() {
    OpenMissionTextEditor("WHAT THIS TEACHES",
        "Describe the route, setup, or skill the player should learn.",
        &g_missionAuthoring.description, true, 4096);
}
void ActEditNewPackName() {
    OpenMissionTextEditor("PACK NAME", "Required. Folder and stable ID are generated safely.",
                          &g_missionAuthoring.newPackName, false, 128);
}
void ActEditNewPackAuthor() {
    OpenMissionTextEditor("PACK AUTHOR", "Optional author or team credit.",
                          &g_missionAuthoring.newPackAuthor, false, 128);
}
void ActEditNewPackDescription() {
    OpenMissionTextEditor("PACK DESCRIPTION", "Explain the pack's scope and intended audience.",
                          &g_missionAuthoring.newPackDescription, true, 4096);
}
void ActEditNewPackVersion() {
    OpenMissionTextEditor("PACK VERSION", "Display version, for example 1.0.0.",
                          &g_missionAuthoring.newPackVersion, false, 32);
}
void ActEditNewCategoryName() {
    OpenMissionTextEditor("CATEGORY NAME", "Required. Examples: Fundamentals or Corner Routes.",
                          &g_missionAuthoring.newCategoryName, false, 96);
}
void ActEditNewCategoryDescription() {
    OpenMissionTextEditor("CATEGORY DESCRIPTION", "Optional one-line purpose for this section.",
                          &g_missionAuthoring.newCategoryDescription, true, 1024);
}
void ActEditExistingPackName() {
    OpenMissionTextEditor("PACK NAME", "Rename the selected pack without changing its stable ID or folder.",
                          &g_missionAuthoring.editPackName, false, 128);
}
void ActEditExistingPackAuthor() {
    OpenMissionTextEditor("PACK AUTHOR", "Author or team credit shown in the browser.",
                          &g_missionAuthoring.editPackAuthor, false, 128);
}
void ActEditExistingPackDescription() {
    OpenMissionTextEditor("PACK DESCRIPTION", "Describe this collection for players browsing it.",
                          &g_missionAuthoring.editPackDescription, true, 4096);
}
void ActEditExistingPackVersion() {
    OpenMissionTextEditor("PACK VERSION", "Display version only; stable pack identity is unchanged.",
                          &g_missionAuthoring.editPackVersion, false, 32);
}
void ActEditExistingCategoryName() {
    OpenMissionTextEditor("CATEGORY NAME", "Rename this category without changing its stable ID.",
                          &g_missionAuthoring.editCategoryName, false, 96);
}
void ActEditExistingCategoryDescription() {
    OpenMissionTextEditor("CATEGORY DESCRIPTION", "Describe what players will find in this section.",
                          &g_missionAuthoring.editCategoryDescription, true, 1024);
}
void ActEditExistingMissionName() {
    OpenMissionTextEditor("MISSION NAME", "Rename this published session without changing its file or stable ID.",
                          &g_missionAuthoring.editMissionName, false, 128);
}
void ActEditExistingMissionDescription() {
    OpenMissionTextEditor("WHAT THIS TEACHES", "Revise the player-facing objective; recorded gameplay stays unchanged.",
                          &g_missionAuthoring.editMissionDescription, true, 4096);
}

bool HasNonWhitespace(const std::string& value) {
    for (unsigned char c : value) {
        if (!std::isspace(c)) return true;
    }
    return false;
}

const char* ValTrialName() {
    return HasNonWhitespace(g_missionAuthoring.name)
        ? g_missionAuthoring.name.c_str() : "REQUIRED";
}
const char* ValTrialDescription() {
    return HasNonWhitespace(g_missionAuthoring.description) ? "SET" : "ADD SUMMARY";
}
const char* ValNewPackName() {
    return HasNonWhitespace(g_missionAuthoring.newPackName)
        ? g_missionAuthoring.newPackName.c_str() : "REQUIRED";
}
const char* ValNewPackAuthor() {
    return HasNonWhitespace(g_missionAuthoring.newPackAuthor)
        ? g_missionAuthoring.newPackAuthor.c_str() : "OPTIONAL";
}
const char* ValNewPackDescription() {
    return HasNonWhitespace(g_missionAuthoring.newPackDescription) ? "SET" : "OPTIONAL";
}
const char* ValNewPackVersion() {
    return HasNonWhitespace(g_missionAuthoring.newPackVersion)
        ? g_missionAuthoring.newPackVersion.c_str() : "1.0.0";
}
const char* ValNewCategoryName() {
    return HasNonWhitespace(g_missionAuthoring.newCategoryName)
        ? g_missionAuthoring.newCategoryName.c_str() : "REQUIRED";
}
const char* ValNewCategoryDescription() {
    return HasNonWhitespace(g_missionAuthoring.newCategoryDescription) ? "SET" : "OPTIONAL";
}
const char* ValExistingPackName() {
    return HasNonWhitespace(g_missionAuthoring.editPackName)
        ? g_missionAuthoring.editPackName.c_str() : "REQUIRED";
}
const char* ValExistingPackAuthor() {
    return HasNonWhitespace(g_missionAuthoring.editPackAuthor)
        ? g_missionAuthoring.editPackAuthor.c_str() : "OPTIONAL";
}
const char* ValExistingPackDescription() {
    return HasNonWhitespace(g_missionAuthoring.editPackDescription) ? "SET" : "OPTIONAL";
}
const char* ValExistingPackVersion() {
    return HasNonWhitespace(g_missionAuthoring.editPackVersion)
        ? g_missionAuthoring.editPackVersion.c_str() : "1.0";
}
const char* ValExistingCategoryName() {
    return HasNonWhitespace(g_missionAuthoring.editCategoryName)
        ? g_missionAuthoring.editCategoryName.c_str() : "REQUIRED";
}
const char* ValExistingCategoryDescription() {
    return HasNonWhitespace(g_missionAuthoring.editCategoryDescription) ? "SET" : "OPTIONAL";
}
const char* ValExistingMissionName() {
    return HasNonWhitespace(g_missionAuthoring.editMissionName)
        ? g_missionAuthoring.editMissionName.c_str() : "REQUIRED";
}
const char* ValExistingMissionDescription() {
    return HasNonWhitespace(g_missionAuthoring.editMissionDescription) ? "SET" : "OPTIONAL";
}
const char* ValSelectedExistingMission() {
    const auto* pack = SelectedAuthoringPack();
    const int index = g_missionAuthoring.missionIndex;
    if (!pack || index < 0 || index >= static_cast<int>(pack->scenarios.size())) {
        return "NO PUBLISHED MISSIONS";
    }
    const auto& mission = pack->scenarios[index];
    return mission.name.empty() ? mission.id.c_str() : mission.name.c_str();
}
const char* ValSelectedPack() {
    const auto* pack = SelectedAuthoringPack();
    return pack ? pack->name.c_str() : "CREATE A PACK";
}
const char* ValSelectedCategory() {
    const auto* category = SelectedAuthoringCategory();
    return category ? category->label.c_str() : "ADD A CATEGORY";
}
const char* FormatAuthoringPackDropdown(const Row& row) {
    if (!g_missionAuthoring.packSelectionValid) return "CHOOSE PACK";
    const int index = row.choiceIdxPtr ? *row.choiceIdxPtr : -1;
    return row.choices && index >= 0 && index < row.choiceCount
        ? row.choices[index] : "CHOOSE PACK";
}
const char* ValAuthoringStatus() {
    const auto phase = Mission::Engine::Recorder::GetPhase();
    if (phase != Mission::Engine::Recorder::Phase::Review) return "DRAFT SETUP";
    if (!HasNonWhitespace(g_missionAuthoring.name) ||
        g_missionAuthoring.difficultyIndex <= 0 ||
        !SelectedAuthoringPack() ||
        !SelectedAuthoringCategory()) {
        return "NEEDS DETAILS";
    }
    return g_missionAuthoring.takePublishReady ? "READY TO PUBLISH" : "NEEDS REVIEW";
}

bool CreatePackDisabled() {
    return !HasNonWhitespace(g_missionAuthoring.newPackName);
}
bool AddCategoryDisabled() {
    return !SelectedAuthoringPack() ||
           !HasNonWhitespace(g_missionAuthoring.newCategoryName);
}
bool UpdatePackDisabled() {
    return !SelectedAuthoringPack() ||
           !HasNonWhitespace(g_missionAuthoring.editPackName);
}
bool UpdateCategoryDisabled() {
    return !SelectedAuthoringCategory() ||
           !HasNonWhitespace(g_missionAuthoring.editCategoryName);
}
bool UpdateMissionDisabled() {
    const auto* pack = SelectedAuthoringPack();
    return !pack || g_missionAuthoring.missionIndex < 0 ||
           g_missionAuthoring.missionIndex >= static_cast<int>(pack->scenarios.size()) ||
           !HasNonWhitespace(g_missionAuthoring.editMissionName) ||
           g_missionAuthoring.editMissionDifficultyIndex < 1 ||
           g_missionAuthoring.editMissionDifficultyIndex > 5 ||
           g_missionAuthoring.editMissionCategoryIndex < 0 ||
           g_missionAuthoring.editMissionCategoryIndex >=
               static_cast<int>(pack->categories.size());
}
bool PackSelectionDisabled() { return !SelectedAuthoringPack(); }
bool PublishRecordedDisabled() {
    return Mission::Engine::Recorder::GetPhase() != Mission::Engine::Recorder::Phase::Review ||
           !HasNonWhitespace(g_missionAuthoring.name) ||
           g_missionAuthoring.difficultyIndex <= 0 ||
           !SelectedAuthoringPack() ||
           !SelectedAuthoringCategory() || !g_missionAuthoring.takePublishReady;
}

void ActRefreshAuthoringPacks() {
    g_missionAuthoring.rescanPending = true;
}

void ExecuteCreateAuthoringPack() {
    auto& form = g_missionAuthoring;
    Mission::Authoring::CreatePackResult created;
    std::string error;
    const std::string version = form.newPackVersion.empty()
        ? std::string("1.0.0") : form.newPackVersion;
    if (!Mission::Authoring::CreatePack(Mission::ResolveMissionsRoot(),
            form.newPackName, form.newPackAuthor, form.newPackDescription,
            version, created, error)) {
        ShowMissionAuthoringResult(false, "Pack creation failed: " + error);
        return;
    }
    form.rememberedPackPath = created.packJsonPath;
    form.newPackName.clear();
    form.newPackDescription.clear();
    RefreshAuthoringPacks(true);
    ShowMissionAuthoringResult(true, "Created pack " + created.packId);
}

void ExecuteCreateAuthoringCategory() {
    auto& form = g_missionAuthoring;
    const auto* selected = SelectedAuthoringPack();
    if (!selected) {
        ShowMissionAuthoringResult(false, "Create or select a pack first");
        return;
    }
    const std::string packPath = selected->packJsonPath;
    Mission::PackCategory created;
    std::string error;
    if (!Mission::Authoring::AddCategory(Mission::ResolveMissionsRoot(),
                                         packPath, form.newCategoryName,
                                         form.newCategoryDescription,
                                         created, error)) {
        ShowMissionAuthoringResult(false, "Category creation failed: " + error);
        return;
    }
    form.rememberedPackPath = packPath;
    form.newCategoryName.clear();
    form.newCategoryDescription.clear();
    RefreshAuthoringPacks(true);
    if (const auto* pack = SelectedAuthoringPack()) {
        for (int i = 0; i < static_cast<int>(pack->categories.size()); ++i) {
            if (pack->categories[i].id == created.id) {
                form.categoryIndex = i;
                break;
            }
        }
    }
    RebuildAuthoringCategoryChoices();
    ShowMissionAuthoringResult(true, "Added category " + created.label);
}

void ExecuteUpdateAuthoringPack() {
    auto& form = g_missionAuthoring;
    const auto* selected = SelectedAuthoringPack();
    if (!selected) {
        ShowMissionAuthoringResult(false, "Select an editable pack first");
        return;
    }
    const std::string packPath = selected->packJsonPath;
    Mission::Authoring::PackMetadata metadata;
    metadata.name = form.editPackName;
    metadata.author = form.editPackAuthor;
    metadata.description = form.editPackDescription;
    metadata.version = form.editPackVersion;
    std::string error;
    const bool ok = Mission::Authoring::UpdatePackMetadata(
        Mission::ResolveMissionsRoot(), packPath, metadata, error);
    if (ok) {
        form.rememberedPackPath = packPath;
        RefreshAuthoringPacks(true);
    }
    ShowMissionAuthoringResult(ok,
        ok ? "Pack details updated" : "Pack update failed: " + error);
}

void ExecuteUpdateAuthoringCategory() {
    auto& form = g_missionAuthoring;
    const auto* pack = SelectedAuthoringPack();
    const auto* category = SelectedAuthoringCategory();
    if (!pack || !category) {
        ShowMissionAuthoringResult(false, "Select an editable category first");
        return;
    }
    const std::string packPath = pack->packJsonPath;
    const std::string categoryId = category->id;
    Mission::Authoring::CategoryMetadata metadata;
    metadata.label = form.editCategoryName;
    metadata.description = form.editCategoryDescription;
    std::string error;
    const bool ok = Mission::Authoring::UpdateCategoryMetadata(
        Mission::ResolveMissionsRoot(), packPath, categoryId, metadata, error);
    if (ok) {
        form.rememberedPackPath = packPath;
        RefreshAuthoringPacks(true);
        if (const auto* refreshed = SelectedAuthoringPack()) {
            for (int i = 0; i < static_cast<int>(refreshed->categories.size()); ++i) {
                if (refreshed->categories[i].id == categoryId) {
                    form.categoryIndex = i;
                    break;
                }
            }
        }
        SeedSelectedAuthoringEditors(true);
    }
    ShowMissionAuthoringResult(ok,
        ok ? "Category details updated" : "Category update failed: " + error);
}

void ExecuteUpdateAuthoringMission() {
    auto& form = g_missionAuthoring;
    const auto* pack = SelectedAuthoringPack();
    if (!pack || form.missionIndex < 0 ||
        form.missionIndex >= static_cast<int>(pack->scenarios.size()) ||
        form.editMissionCategoryIndex < 0 ||
        form.editMissionCategoryIndex >= static_cast<int>(pack->categories.size())) {
        ShowMissionAuthoringResult(false, "Select an editable mission and category first");
        return;
    }
    const std::string packPath = pack->packJsonPath;
    const std::string missionId = pack->scenarios[form.missionIndex].id;
    Mission::Authoring::ExistingMissionMetadata metadata;
    metadata.name = form.editMissionName;
    metadata.description = form.editMissionDescription;
    metadata.type = form.editMissionTypeIndex == 1 ? "mission" : "combo";
    metadata.difficulty = form.editMissionDifficultyIndex;
    metadata.categoryId = pack->categories[form.editMissionCategoryIndex].id;
    std::string error;
    const bool ok = Mission::Authoring::UpdateMissionMetadata(
        Mission::ResolveMissionsRoot(), packPath, missionId, metadata, error);
    if (ok) {
        form.rememberedPackPath = packPath;
        RefreshAuthoringPacks(true);
        if (const auto* refreshed = SelectedAuthoringPack()) {
            for (int i = 0; i < static_cast<int>(refreshed->scenarios.size()); ++i) {
                if (refreshed->scenarios[i].id == missionId) {
                    form.missionIndex = i;
                    break;
                }
            }
        }
        SeedSelectedAuthoringEditors(true);
    }
    ShowMissionAuthoringResult(ok,
        ok ? "Mission details updated" : "Mission update failed: " + error);
}

void ActCreateAuthoringPack() {
    g_missionAuthoring.createPackPending = true;
}

void ActCreateAuthoringCategory() {
    g_missionAuthoring.createCategoryPending = true;
}

void ActUpdateAuthoringPack() { g_missionAuthoring.updatePackPending = true; }
void ActUpdateAuthoringCategory() { g_missionAuthoring.updateCategoryPending = true; }
void ActUpdateAuthoringMission() { g_missionAuthoring.updateMissionPending = true; }

void ProcessPendingMissionAuthoringActions() {
    auto& form = g_missionAuthoring;
    if (g_authoringRescanRequested.exchange(false,
            std::memory_order_acq_rel)) {
        form.rescanPending = true;
    }
    if (g_prepareNewMissionAuthoringRequested.exchange(false,
            std::memory_order_acq_rel)) {
        form.name.clear();
        form.description.clear();
        form.difficultyIndex = 0;
        form.typeIndex = 0;
        form.status.clear();
        form.reviewReadinessCaptured = false;
        form.takePublishReady = false;
        form.takePublishBlocker.clear();
        form.sessionSeeded = true;
        form.previousPhase = Mission::Engine::Recorder::Phase::Idle;
        RefreshAuthoringPacks(true);
    }
    if (form.invalidateReadinessPending) {
        form.invalidateReadinessPending = false;
        InvalidateMissionReviewReadiness();
    }
    if (form.createPackPending) {
        form.createPackPending = false;
        ExecuteCreateAuthoringPack();
    }
    if (form.createCategoryPending) {
        form.createCategoryPending = false;
        ExecuteCreateAuthoringCategory();
    }
    if (form.updatePackPending) {
        form.updatePackPending = false;
        ExecuteUpdateAuthoringPack();
    }
    if (form.updateCategoryPending) {
        form.updateCategoryPending = false;
        ExecuteUpdateAuthoringCategory();
    }
    if (form.updateMissionPending) {
        form.updateMissionPending = false;
        ExecuteUpdateAuthoringMission();
    }
    if (form.rescanPending) {
        form.rescanPending = false;
        const bool ok = RefreshAuthoringPacks(true);
        if (!ok) {
            ShowMissionAuthoringResult(false, form.status);
        } else if (!form.status.empty()) {
            DirectDrawHook::AddMessage(form.status.c_str(), "MISSION AUTHORING",
                                       RGB(255, 205, 120), 4200, 0, 120);
        } else {
            ShowMissionAuthoringResult(
                true, std::to_string(form.packs.size()) + " editable pack(s) found");
        }
    }
    if (form.categoryChoicesDirty) {
        form.categoryChoicesDirty = false;
        RebuildAuthoringCategoryChoices();
        RebuildAuthoringMissionChoices();
        SeedSelectedAuthoringEditors(true);
    }
    SeedSelectedAuthoringEditors(false);
}

Mission::Authoring::MissionMetadata CurrentMissionMetadata() {
    Mission::Authoring::MissionMetadata metadata;
    metadata.name = g_missionAuthoring.name;
    metadata.description = g_missionAuthoring.description;
    metadata.type = g_missionAuthoring.typeIndex == 1 ? "mission" : "combo";
    // Index zero is the intentional UNRATED draft state; published sessions
    // require one of the five user-selected ratings (1..5).
    metadata.difficulty = g_missionAuthoring.difficultyIndex;
    return metadata;
}

void ActSaveRecordedDraft() {
    std::string message;
    const bool ok = Mission::Engine::Recorder::SaveRecordedDraft(
        CurrentMissionMetadata(), message);
    ShowMissionAuthoringResult(ok,
        ok ? "Draft saved to Recorded" : "Draft save failed: " + message);
}

void ActPublishRecordedMission() {
    const auto* pack = SelectedAuthoringPack();
    const auto* category = SelectedAuthoringCategory();
    if (!pack || !category) {
        ShowMissionAuthoringResult(false, "Select a pack and category first");
        return;
    }
    const std::string packPath = pack->packJsonPath;
    const std::string categoryId = category->id;
    std::string message;
    const bool ok = Mission::Engine::Recorder::PublishRecorded(
        packPath, categoryId, CurrentMissionMetadata(), message);
    ShowMissionAuthoringResult(ok,
        ok ? "Published to " + pack->name : "Publish failed: " + message);
    if (ok) {
        // The current frame can still hold Row choice pointers into the pack
        // vectors; rebuild them at the start of the next menu tick.
        g_missionAuthoring.rescanPending = true;
    }
}

Row* BuildCreatePackRows(int& count) {
    static Row rows[16];
    int n = 0;
    rows[n++] = Header("NEW MISSION PACK");
    rows[n++] = Info("A pack is the shareable collection of missions shown in the browser.");
    rows[n++] = Action("PACK NAME", ActEditNewPackName, ValNewPackName);
    rows[n++] = Action("AUTHOR / CREDITS", ActEditNewPackAuthor, ValNewPackAuthor);
    rows[n++] = Action("DESCRIPTION", ActEditNewPackDescription, ValNewPackDescription);
    rows[n++] = Action("VERSION", ActEditNewPackVersion, ValNewPackVersion);
    rows[n++] = Action("CREATE PACK", ActCreateAuthoringPack, nullptr, CreatePackDisabled);
    count = n;
    return rows;
}

Row* BuildCreateCategoryRows(int& count) {
    static Row rows[14];
    int n = 0;
    rows[n++] = Header("NEW CATEGORY");
    rows[n++] = Info("Categories group a pack's missions into a suggested order without locking any of them.");
    rows[n++] = Action("CATEGORY NAME", ActEditNewCategoryName, ValNewCategoryName);
    rows[n++] = Action("DESCRIPTION", ActEditNewCategoryDescription, ValNewCategoryDescription);
    rows[n++] = Action("ADD TO SELECTED PACK", ActCreateAuthoringCategory,
                       ValSelectedPack, AddCategoryDisabled);
    count = n;
    return rows;
}

Row* BuildMissionDetailsRows(int& count) {
    static Row rows[24];
    int n = 0;
    auto& form = g_missionAuthoring;
    rows[n++] = Header("MISSION DETAILS");
    rows[n++] = Action("NAME", ActEditTrialName, ValTrialName);
    rows[n++] = Action("WHAT THIS TEACHES", ActEditTrialDescription, ValTrialDescription);
    rows[n++] = DropdownRow("LIBRARY TYPE", &form.typeIndex,
                            kAuthorTypeChoices, kAuthorTypeChoiceCount);
    rows[n++] = DropdownRow("DIFFICULTY", &form.difficultyIndex,
                            kAuthorDifficultyChoices, kAuthorDifficultyChoiceCount);
    if (form.difficultyIndex <= 0) {
        rows[n++] = Info("Publishing needs a difficulty. A recorded draft can stay unrated.");
    }
    rows[n++] = Spacer();
    rows[n++] = Header("DESTINATION");
    if (form.packChoices.empty()) {
        rows[n++] = Info("No editable packs yet. Create one before you can publish this recording.");
    } else {
        Row packRow = DropdownRow("PACK", &form.packIndex,
                                  form.packChoices.data(),
                                  static_cast<int>(form.packChoices.size()),
                                  OnAuthoringPackChanged);
        packRow.valueFormatter = FormatAuthoringPackDropdown;
        rows[n++] = packRow;
        if (form.categoryChoices.empty()) {
            rows[n++] = Info("The selected pack has no categories. Add one before publishing.");
        } else {
            rows[n++] = DropdownRow("CATEGORY", &form.categoryIndex,
                                    form.categoryChoices.data(),
                                    static_cast<int>(form.categoryChoices.size()));
        }
    }
    rows[n++] = Submenu("CREATE NEW PACK", "CREATE PACK", BuildCreatePackRows);
    rows[n++] = Submenu("ADD CATEGORY", "CREATE CATEGORY", BuildCreateCategoryRows,
                        ValSelectedPack, PackSelectionDisabled);
    rows[n++] = Action("RESCAN PACKS", ActRefreshAuthoringPacks);
    if (!form.status.empty()) {
        rows[n++] = Spacer();
        rows[n++] = Header("STATUS");
        rows[n++] = Info(form.status.c_str());
    }
    count = n;
    return rows;
}

Row* BuildEditPackRows(int& count) {
    static Row rows[16];
    int n = 0;
    SeedSelectedAuthoringEditors(false);
    rows[n++] = Header("EDIT PACK DETAILS");
    rows[n++] = Info("Edits the displayed text only. The pack ID and folder stay unchanged.");
    rows[n++] = Action("PACK NAME", ActEditExistingPackName, ValExistingPackName);
    rows[n++] = Action("AUTHOR / CREDITS", ActEditExistingPackAuthor,
                       ValExistingPackAuthor);
    rows[n++] = Action("DESCRIPTION", ActEditExistingPackDescription,
                       ValExistingPackDescription);
    rows[n++] = Action("VERSION", ActEditExistingPackVersion,
                       ValExistingPackVersion);
    rows[n++] = Action("SAVE PACK DETAILS", ActUpdateAuthoringPack, nullptr,
                       UpdatePackDisabled);
    count = n;
    return rows;
}

Row* BuildEditCategoryRows(int& count) {
    static Row rows[12];
    int n = 0;
    SeedSelectedAuthoringEditors(false);
    rows[n++] = Header("EDIT CATEGORY DETAILS");
    rows[n++] = Info("The category ID and order stay unchanged.");
    rows[n++] = Action("CATEGORY NAME", ActEditExistingCategoryName,
                       ValExistingCategoryName);
    rows[n++] = Action("DESCRIPTION", ActEditExistingCategoryDescription,
                       ValExistingCategoryDescription);
    rows[n++] = Action("SAVE CATEGORY DETAILS", ActUpdateAuthoringCategory,
                       nullptr, UpdateCategoryDisabled);
    count = n;
    return rows;
}

Row* BuildEditMissionRows(int& count) {
    static Row rows[18];
    int n = 0;
    auto& form = g_missionAuthoring;
    SeedSelectedAuthoringEditors(false);
    rows[n++] = Header("EDIT PUBLISHED MISSION");
    rows[n++] = Info("Recorded setup, inputs, steps, contacts, file, ID, and order stay unchanged.");
    rows[n++] = Action("NAME", ActEditExistingMissionName,
                       ValExistingMissionName);
    rows[n++] = Action("WHAT THIS TEACHES", ActEditExistingMissionDescription,
                       ValExistingMissionDescription);
    rows[n++] = DropdownRow("LIBRARY TYPE", &form.editMissionTypeIndex,
                            kAuthorTypeChoices, kAuthorTypeChoiceCount);
    rows[n++] = DropdownRow("DIFFICULTY", &form.editMissionDifficultyIndex,
                            kAuthorDifficultyChoices, kAuthorDifficultyChoiceCount);
    if (form.categoryChoices.empty()) {
        rows[n++] = Info("This pack has no category destination.");
    } else {
        rows[n++] = DropdownRow("CATEGORY", &form.editMissionCategoryIndex,
                                form.categoryChoices.data(),
                                static_cast<int>(form.categoryChoices.size()));
    }
    rows[n++] = Action("SAVE MISSION DETAILS", ActUpdateAuthoringMission,
                       nullptr, UpdateMissionDisabled);
    count = n;
    return rows;
}

Row* BuildPackWorkshopRows(int& count) {
    static Row rows[28];
    int n = 0;
    auto& form = g_missionAuthoring;
    rows[n++] = Header("PACK WORKSHOP");
    rows[n++] = Info("Edit packs you made locally. IDs, files, order, and recorded gameplay stay untouched.");
    if (!form.packChoices.empty()) {
        Row packRow = DropdownRow("ACTIVE PACK", &form.packIndex,
                                  form.packChoices.data(),
                                  static_cast<int>(form.packChoices.size()),
                                  OnAuthoringPackChanged);
        packRow.valueFormatter = FormatAuthoringPackDropdown;
        rows[n++] = packRow;
        rows[n++] = Submenu("EDIT PACK DETAILS", "EDIT PACK", BuildEditPackRows,
                            ValExistingPackName, UpdatePackDisabled);
        rows[n++] = Spacer();
        rows[n++] = Header("CATEGORIES");
        if (!form.categoryChoices.empty()) {
            rows[n++] = DropdownRow("ACTIVE CATEGORY", &form.categoryIndex,
                                    form.categoryChoices.data(),
                                    static_cast<int>(form.categoryChoices.size()),
                                    OnAuthoringCategoryChanged);
            rows[n++] = Submenu("EDIT CATEGORY DETAILS", "EDIT CATEGORY",
                                BuildEditCategoryRows, ValExistingCategoryName,
                                UpdateCategoryDisabled);
        } else {
            rows[n++] = Info("This pack has no categories.");
        }
        rows[n++] = Spacer();
        rows[n++] = Header("PUBLISHED MISSIONS");
        if (!form.missionChoices.empty()) {
            rows[n++] = DropdownRow("ACTIVE MISSION", &form.missionIndex,
                                    form.missionChoices.data(),
                                    static_cast<int>(form.missionChoices.size()),
                                    OnAuthoringMissionChanged);
            rows[n++] = Submenu("EDIT MISSION DETAILS", "EDIT MISSION",
                                BuildEditMissionRows, ValSelectedExistingMission,
                                UpdateMissionDisabled);
        } else {
            rows[n++] = Info("No published missions are in this pack yet.");
        }
    } else {
        rows[n++] = Info("No editable mission packs found.");
    }
    rows[n++] = Submenu("CREATE NEW PACK", "CREATE PACK", BuildCreatePackRows);
    rows[n++] = Submenu("ADD CATEGORY", "CREATE CATEGORY", BuildCreateCategoryRows,
                        ValSelectedPack, PackSelectionDisabled);
    rows[n++] = Action("RESCAN PACKS", ActRefreshAuthoringPacks);
    count = n;
    return rows;
}

static void ActMissionPreviewRecording() {
    std::string msg;
    const bool ok = Mission::Engine::Demo::PlayRecording(msg);
    DirectDrawHook::AddMessage(ok ? "Preparing recorded preview..." : ("Preview: " + msg),
        "SYSTEM", ok ? RGB(180, 255, 220) : RGB(255, 180, 120), 1800, 0, 120);
    if (ok && ImGuiImpl::IsVisible()) ImGuiImpl::ToggleVisibility();
}

static Row* BuildMissionBrowserRows(int& count) {
    static Row s_rows[32];
    static char bindingLine[160];
    ProcessPendingMissionAuthoringActions();
    int n = 0;
    s_rows[n++] = Header("RECORD & AUTHOR");
    _snprintf_s(bindingLine, sizeof(bindingLine), _TRUNCATE,
                I18n::Tr("Macro Record: %s"), Mission::Engine::Recorder::GetMacroRecordBindingLabel().c_str());
    s_rows[n++] = Info(bindingLine);
    const auto recPhase = Mission::Engine::Recorder::GetPhase();
    SeedMissionAuthoringSessionIfNeeded(recPhase);
    if (recPhase == Mission::Engine::Recorder::Phase::Review &&
        !g_missionAuthoring.reviewReadinessCaptured) {
        RefreshRecordedTakeReadiness();
    }
    if (recPhase == Mission::Engine::Recorder::Phase::Idle ||
        recPhase == Mission::Engine::Recorder::Phase::PreRecord) {
        auto& settings = MutableSettings();
        Row missionCountIn = IntNum("COUNT-IN",
                                    &settings.missionRecorderCountInMs,
                                    0, 3000, 100, 500,
                                    OnMissionRecordCountIn);
        missionCountIn.valueFormatter = [](const Row& row) -> const char* {
            static char value[32];
            const int milliseconds = row.intPtr ? *row.intPtr : 0;
            if (milliseconds <= 0) return "OFF";
            _snprintf_s(value, sizeof(value), _TRUNCATE, I18n::Tr("%.1f SEC"),
                        static_cast<double>(milliseconds) / 1000.0);
            return value;
        };
        s_rows[n++] = missionCountIn;
    }
    if (recPhase == Mission::Engine::Recorder::Phase::Idle) {
        s_rows[n++] = Action("NEW RECORDING (PRE-RECORD)", ActArmMissionRecording,
                             ValMissionAuthoringState);
        s_rows[n++] = Info("Arrange the setup first. When the count-in ends the setup is captured and recording starts.");
        s_rows[n++] = Submenu("PACK WORKSHOP", "PACK WORKSHOP", BuildPackWorkshopRows,
                              ValSelectedPack);
    } else if (recPhase == Mission::Engine::Recorder::Phase::PreRecord) {
        s_rows[n++] = Info("PRE-RECORD: arrange positions/resources. Nothing is being captured yet.");
        s_rows[n++] = Submenu("MISSION DETAILS & DESTINATION", "MISSION DETAILS",
                              BuildMissionDetailsRows, ValSelectedPack);
        s_rows[n++] = Action("START RECORDING", ActAdvanceMissionRecording, ValMissionRecordSteps);
        s_rows[n++] = Action("DISCARD SESSION", ActDiscardMissionRecording);
    } else if (recPhase == Mission::Engine::Recorder::Phase::CountIn) {
        s_rows[n++] = Info("COUNT-IN: close the menu and release all P1 controls.");
        s_rows[n++] = Action("DISCARD SESSION", ActDiscardMissionRecording);
    } else if (recPhase == Mission::Engine::Recorder::Phase::Recording) {
        s_rows[n++] = Info("RECORDING: your P1 inputs and the mission steps are captured together.");
        s_rows[n++] = Info("If the combo drops and you keep going, the clip marks a setup break there.");
        s_rows[n++] = Action("STOP & REVIEW", ActAdvanceMissionRecording, ValMissionRecordSteps);
        s_rows[n++] = Action("DISCARD SESSION", ActDiscardMissionRecording);
    } else {
        s_rows[n++] = Header(ValAuthoringStatus());
        s_rows[n++] = Info("REVIEW: nothing is written until you Publish To Pack or Save As Recorded Draft.");
        s_rows[n++] = Info("Preview the clip and edit the details players see. Retake keeps your pack, category, and text.");
        if (!g_missionAuthoring.takePublishReady &&
            !g_missionAuthoring.takePublishBlocker.empty()) {
            s_rows[n++] = Info(g_missionAuthoring.takePublishBlocker.c_str());
        }
        s_rows[n++] = Action("PREVIEW DEMONSTRATION", ActMissionPreviewRecording);
        s_rows[n++] = Submenu("EDIT MISSION DETAILS", "MISSION DETAILS",
                              BuildMissionDetailsRows, ValSelectedPack);
        s_rows[n++] = Action("PUBLISH TO PACK", ActPublishRecordedMission,
                             ValSelectedCategory, PublishRecordedDisabled);
        s_rows[n++] = Action("SAVE AS RECORDED DRAFT", ActSaveRecordedDraft,
                             ValMissionRecordSteps);
        s_rows[n++] = Action("RETAKE FROM BASELINE", ActRetakeMissionRecording);
        s_rows[n++] = Action("DISCARD SESSION", ActDiscardMissionRecording);
    }

    count = n;
    return s_rows;
}
Row* BuildDebugOverlayRows(int& count) {
    static Row s_rows[16];
    int n = 0;

    s_rows[n++] = Header("DEBUG OVERLAYS");
    s_rows[n++] = Info("Gameplay overlays moved to Options > Display Overlays.");
    s_rows[n++] = Toggle ("OVERLAY DEBUG BORDERS",     &g_mirrorOverlayBorders,       OnOverlayBorders);
    s_rows[n++] = Toggle ("RG DEBUG TOASTS",           &g_mirrorRGToasts,             OnRGToasts);
    s_rows[n++] = Submenu("GAME HUD",                  "GAME HUD", BuildHudDisableRows, ValHudDisable);
    count = n;
    return s_rows;
}

Row* BuildDebugBgmRows(int& count) {
    static Row s_rows[8];
    int n = 0;

    SeedDebugBgmChoiceIfNeeded();

    s_rows[n++] = Header("BGM");
    s_rows[n++] = DropdownRow("BGM TRACK",            &g_bgmSlot, kNamedOstChoices, kNamedOstChoiceCount);
    s_rows[n++] = Action ("PLAY BGM",                  RunPlayBGM);
    s_rows[n++] = Action ("STOP BGM",                  RunStopBGM);
    count = n;
    return s_rows;
}

bool DebugSwitchPlayersDisabled() {
    return !g_debugSwitchPlayersAvailable;
}

bool DebugCancelRFP1Disabled() {
    return !g_debugRfFreezeP1Active;
}

bool DebugCancelRFP2Disabled() {
    return !g_debugRfFreezeP2Active;
}

void RunDebugToggleSwitchPlayers() {
    // Same guard as the hotkeys: switching mid-replay leaves the macro's poll
    // override on the human's new slot and restores stale CPU flags at the end.
    if (MacroController::GetState() != MacroController::State::Idle) {
        DirectDrawHook::AddMessage("Switch Players disabled while a macro is active", "SYSTEM", RGB(255, 200, 120), 1200, 0, 100);
        return;
    }
    const bool ok = SwitchPlayers::ToggleLocalSide();
    if (!ok) {
        LogOut("[DEBUG/UI] SwitchPlayers toggle failed (Practice controller not ready?)", true);
        DirectDrawHook::AddMessage("Switch Players: FAILED", "SYSTEM", RGB(255, 100, 100), 1500, 0, 100);
        return;
    }
    RefreshDebugMirrors();
    if (g_debugPracticeLocalSide == 0) {
        DirectDrawHook::AddMessage("Local: P1", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
    } else if (g_debugPracticeLocalSide == 1) {
        DirectDrawHook::AddMessage("Local: P2", "SYSTEM", RGB(100, 255, 100), 1500, 0, 100);
    } else {
        DirectDrawHook::AddMessage("Local side changed", "SYSTEM", RGB(100, 255, 100), 1200, 0, 100);
    }
}

void RunCancelRFP1() {
    StopRFFreezePlayer(1);
    RefreshDebugMirrors();
}

void RunCancelRFP2() {
    StopRFFreezePlayer(2);
    RefreshDebugMirrors();
}

Row* BuildDebugRuntimeRows(int& count) {
    static Row s_rows[24];
    int n = 0;

    s_rows[n++] = Header("PRACTICE ROUTING");
    s_rows[n++] = Info("Live readouts for troubleshooting Switch Players and pause behavior.");
    s_rows[n++] = Action("TOGGLE SWITCH PLAYERS", RunDebugToggleSwitchPlayers, nullptr, DebugSwitchPlayersDisabled);
    s_rows[n++] = Info(g_debugLocalSideInfo);
    s_rows[n++] = Info(g_debugAiControlInfo);
    s_rows[n++] = Info(g_debugPracticeCpuInfo);
    s_rows[n++] = Info(g_debugGamespeedInfo);
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("RF FREEZE STATUS");
    s_rows[n++] = Info(g_debugRfFreezeP1Info);
    s_rows[n++] = Action("CANCEL P1 RF FREEZE", RunCancelRFP1, nullptr, DebugCancelRFP1Disabled);
    s_rows[n++] = Info(g_debugRfFreezeP2Info);
    s_rows[n++] = Action("CANCEL P2 RF FREEZE", RunCancelRFP2, nullptr, DebugCancelRFP2Disabled);
    count = n;
    return s_rows;
}


Row* BuildDebugFinalMemoryRows(int& count) {
    static Row s_rows[8];
    int n = 0;

    s_rows[n++] = Header("FINAL MEMORY");
    s_rows[n++] = Action ("RUN P1 FINAL MEMORY",       RunP1FinalMemory);
    s_rows[n++] = Action ("RUN P2 FINAL MEMORY",       RunP2FinalMemory);
    count = n;
    return s_rows;
}

Row* BuildSettingsDebugRows(int& count) {
    static Row s_rows[20];
    int n = 0;

    s_rows[n++] = Header("DEBUG MENUS");
    s_rows[n++] = Submenu("LOGGING",      "DEBUG LOGGING", BuildDebugLoggingRows,     ValDebugLogging);
    s_rows[n++] = Submenu("OVERLAYS",     "DEBUG OVERLAYS", BuildDebugOverlayRows,     ValDebugOverlays);
    s_rows[n++] = Submenu("INPUT / RUNTIME", "INPUT / RUNTIME", BuildDebugRuntimeRows, ValDebugRuntime);
    s_rows[n++] = Submenu("BGM",          "BGM",            BuildDebugBgmRows,         ValDebugBgm);
    s_rows[n++] = Submenu("FINAL MEMORY", "FINAL MEMORY",   BuildDebugFinalMemoryRows, ValFinalMemoryTools);
    s_rows[n++] = Spacer();
    s_rows[n++] = Info(CurrentConfigPathInfo());
    s_rows[n++] = Action ("SAVE ALL TO DISK",          SaveSettingsToDisk);
    s_rows[n++] = Action ("RELOAD FROM DISK",          ReloadSettingsFromDisk);
    count = n;
    return s_rows;
}

// HELP screen
char g_helpVersionStr[64];
char g_helpBuildStr[96];
char g_helpUpdateStr[256];
char g_helpUpdateHeadline[96];
char g_helpOpenHelp[96];
char g_helpToggleOverlay[256];
char g_helpSavePos[256];
char g_helpLoadPos[256];
char g_helpSwapPos[256];
char g_helpToggleStats[192];
char g_helpSwitchPlayers[256];
char g_helpUiFooter[256];
char g_helpTopTabs[256];
char g_helpSubTabs[320];
char g_helpControllerSupport[192];
char g_helpMacroRecord[256];
char g_helpMacroPlay[256];
char g_helpMacroSlot[192];
char g_helpSavestateSave[256];
char g_helpSavestateLoad[256];
char g_helpFaDuration[192];
char g_helpDetectedVersion[160];
char g_helpP1WikiLabel[96];
char g_helpP2WikiLabel[96];
char g_helpP1WikiUrl[192];
char g_helpP2WikiUrl[192];

void RefreshHelpStrings() {
    const auto& s = Config::GetSettings();
    _snprintf_s(g_helpVersionStr, sizeof(g_helpVersionStr), _TRUNCATE,
                "EfzExtendedPractice v%s", EFZ_TRAINING_MODE_VERSION);
    _snprintf_s(g_helpBuildStr, sizeof(g_helpBuildStr), _TRUNCATE,
                I18n::Tr("Build %s %s"), EFZ_TRAINING_MODE_BUILD_DATE, EFZ_TRAINING_MODE_BUILD_TIME);
    // Sole place worker-published update state is copied into the display buffer;
    // RefreshHelpStrings is a throttled, SEH-wrapped mirror step, keeping the render path
    // single-threaded. Two surfaces: a SHORT header-font headline (DrawUpdateNotice; one
    // line, only when announcing) and the ordinary wrapped detail line.
    switch (UpdateCheck::GetStatus()) {
        case UpdateCheck::Status::UpdateAvailable:
            _snprintf_s(g_helpUpdateHeadline, sizeof(g_helpUpdateHeadline), _TRUNCATE,
                        I18n::Tr("UPDATE AVAILABLE: %s"), UpdateCheck::LatestVersion().c_str());
            _snprintf_s(g_helpUpdateStr, sizeof(g_helpUpdateStr), _TRUNCATE,
                        "Open GitHub Releases below to download it.");
            break;
        case UpdateCheck::Status::UpToDate:
            g_helpUpdateHeadline[0] = 0;
            _snprintf_s(g_helpUpdateStr, sizeof(g_helpUpdateStr), _TRUNCATE,
                        I18n::Tr("You are running the latest release (%s)."),
                        EFZ_TRAINING_MODE_VERSION);
            break;
        case UpdateCheck::Status::Disabled:
            g_helpUpdateHeadline[0] = 0;
            _snprintf_s(g_helpUpdateStr, sizeof(g_helpUpdateStr), _TRUNCATE,
                        "Update checking is off. Enable it under Settings > General.");
            break;
        case UpdateCheck::Status::Checking:
            g_helpUpdateHeadline[0] = 0;
            _snprintf_s(g_helpUpdateStr, sizeof(g_helpUpdateStr), _TRUNCATE,
                        "Checking GitHub for a newer release...");
            break;
        default:
            g_helpUpdateHeadline[0] = 0;
            _snprintf_s(g_helpUpdateStr, sizeof(g_helpUpdateStr), _TRUNCATE,
                        "Find the newest version and release notes on GitHub.");
            break;
    }
    _snprintf_s(g_helpOpenHelp, sizeof(g_helpOpenHelp), _TRUNCATE,
                "Open Help from the Help tab in the menu.");
    _snprintf_s(g_helpToggleOverlay, sizeof(g_helpToggleOverlay), _TRUNCATE,
                I18n::Tr("Open the menu: %s (Controller: %s)."),
                Config::GetKeyName(s.configMenuKey).c_str(),
                Config::GetGamepadButtonName(s.gpToggleMenuButton).c_str());
    _snprintf_s(g_helpSavePos, sizeof(g_helpSavePos), _TRUNCATE,
                I18n::Tr("Save the current position: %s (Controller: %s)."),
                Config::GetKeyName(s.recordKey).c_str(),
                Config::GetGamepadButtonName(s.gpSavePositionButton).c_str());
    _snprintf_s(g_helpLoadPos, sizeof(g_helpLoadPos), _TRUNCATE,
                I18n::Tr("Load a saved or directional position: %s (Controller: %s)."),
                Config::GetKeyName(s.teleportKey).c_str(),
                Config::GetGamepadButtonName(s.gpTeleportButton).c_str());
    _snprintf_s(g_helpSwapPos, sizeof(g_helpSwapPos), _TRUNCATE,
                I18n::Tr("Swap positions: hold Load + D, or press %s on controller."),
                Config::GetGamepadButtonName(s.gpSwapPositionsButton).c_str());
    _snprintf_s(g_helpToggleStats, sizeof(g_helpToggleStats), _TRUNCATE,
                I18n::Tr("Toggle the stats display: %s."), Config::GetKeyName(s.toggleTitleKey).c_str());
    _snprintf_s(g_helpSwitchPlayers, sizeof(g_helpSwitchPlayers), _TRUNCATE,
                I18n::Tr("Switch player control: %s (Controller: %s)."),
                Config::GetKeyName(s.switchPlayersKey).c_str(),
                Config::GetGamepadButtonName(s.gpSwitchPlayersButton).c_str());
    _snprintf_s(g_helpUiFooter, sizeof(g_helpUiFooter), _TRUNCATE,
                I18n::Tr("Footer shortcuts: Apply=%s, Refresh=%s, Exit=%s."),
                Config::GetKeyName(s.uiAcceptKey).c_str(),
                Config::GetKeyName(s.uiRefreshKey).c_str(),
                Config::GetKeyName(s.uiExitKey).c_str());
    _snprintf_s(g_helpTopTabs, sizeof(g_helpTopTabs), _TRUNCATE,
                I18n::Tr("Top tabs: %s / %s on controller, or PgUp / PgDn on keyboard."),
                Config::GetGamepadButtonName(s.gpUiTopTabPrev).c_str(),
                Config::GetGamepadButtonName(s.gpUiTopTabNext).c_str());
    _snprintf_s(g_helpSubTabs, sizeof(g_helpSubTabs), _TRUNCATE,
                I18n::Tr("Subtabs: %s / %s on controller, or [ / ] on keyboard. These also work while you are inside submenus."),
                Config::GetGamepadButtonName(s.gpUiSubTabPrev).c_str(),
                Config::GetGamepadButtonName(s.gpUiSubTabNext).c_str());
    _snprintf_s(g_helpControllerSupport, sizeof(g_helpControllerSupport), _TRUNCATE,
                "Controller support: native XInput pads and DirectInput fallback pads use the same binds. Button names are shown with Xbox labels; on many PlayStation-style pads A/B/X/Y map to Cross/Circle/Square/Triangle.");
    _snprintf_s(g_helpMacroRecord, sizeof(g_helpMacroRecord), _TRUNCATE,
                I18n::Tr("Record macro: %s (Controller: %s)."),
                Config::GetKeyName(s.macroRecordKey).c_str(),
                Config::GetGamepadButtonName(s.gpMacroRecordButton).c_str());
    _snprintf_s(g_helpMacroPlay, sizeof(g_helpMacroPlay), _TRUNCATE,
                I18n::Tr("Play macro: %s (Controller: %s)."),
                Config::GetKeyName(s.macroPlayKey).c_str(),
                Config::GetGamepadButtonName(s.gpMacroPlayButton).c_str());
    _snprintf_s(g_helpMacroSlot, sizeof(g_helpMacroSlot), _TRUNCATE,
                I18n::Tr("Cycle macro slots: %s."), Config::GetKeyName(s.macroSlotKey).c_str());
    _snprintf_s(g_helpSavestateSave, sizeof(g_helpSavestateSave), _TRUNCATE,
                I18n::Tr("Save Practice snapshot: %s."), Config::GetKeyName(s.savestateSaveKey).c_str());
    _snprintf_s(g_helpSavestateLoad, sizeof(g_helpSavestateLoad), _TRUNCATE,
                I18n::Tr("Load Practice snapshot: %s."), Config::GetKeyName(s.savestateLoadKey).c_str());
    _snprintf_s(g_helpFaDuration, sizeof(g_helpFaDuration), _TRUNCATE,
                I18n::Tr("Frame advantage labels last about %.1f sec."), s.frameAdvantageDisplayDuration);

    const EfzRevivalVersion rv = GetEfzRevivalVersion();
    _snprintf_s(g_helpDetectedVersion, sizeof(g_helpDetectedVersion), _TRUNCATE,
                I18n::Tr("Detected: %s %s"),
                EfzRevivalVersionName(rv),
                I18n::Tr(IsEfzRevivalVersionSupported(rv) ? "(supported)" : "(unsupported)"));

    g_helpP1WikiLabel[0] = '\0';
    g_helpP2WikiLabel[0] = '\0';
    g_helpP1WikiUrl[0] = '\0';
    g_helpP2WikiUrl[0] = '\0';
    const auto& d = ImGuiGui::guiState.localData;
    auto fillWiki = [](int charId, const char* rawName, int player, char* label, size_t labelSz, char* url, size_t urlSz) {
        const char* path = nullptr;
        switch (charId) {
            case CHAR_ID_AKANE:    path = "Akane_Satomura"; break;
            case CHAR_ID_AKIKO:    path = "Akiko_Minase"; break;
            case CHAR_ID_AYU:      path = "Ayu_Tsukimiya"; break;
            case CHAR_ID_EXNANASE: path = "Doppel_Nanase"; break;
            case CHAR_ID_IKUMI:    path = "Ikumi_Amasawa"; break;
            case CHAR_ID_KANNA:    path = "Kanna"; break;
            case CHAR_ID_KANO:     path = "Kano_Kirishima"; break;
            case CHAR_ID_KAORI:    path = "Kaori_Misaka"; break;
            case CHAR_ID_MAI:      path = "Mai_Kawasumi"; break;
            case CHAR_ID_MAKOTO:   path = "Makoto_Sawatari"; break;
            case CHAR_ID_MAYU:     path = "Mayu_Shiina"; break;
            case CHAR_ID_MINAGI:   path = "Minagi_Tohno"; break;
            case CHAR_ID_MIO:      path = "Mio_Kouzuki"; break;
            case CHAR_ID_MISAKI:   path = "Misaki_Kawana"; break;
            case CHAR_ID_MISHIO:   path = "Mishio_Amano"; break;
            case CHAR_ID_MISUZU:   path = "Misuzu_Kamio"; break;
            case CHAR_ID_MIZUKA:        path = "Mizuka_Nagamori"; break;
            case CHAR_ID_UNKNOWN_BOSS:
            case CHAR_ID_UNKNOWN:       path = "UNKNOWN"; break;
            case CHAR_ID_NANASE:   path = "Rumi_Nanase"; break;
            case CHAR_ID_SAYURI:   path = "Sayuri_Kurata"; break;
            case CHAR_ID_SHIORI:   path = "Shiori_Misaka"; break;
            case CHAR_ID_NAYUKI:   path = "Nayuki_Minase_(asleep)"; break;
            case CHAR_ID_NAYUKIB:  path = "Nayuki_Minase_(awake)"; break;
            default: break;
        }
        if (!path) return;
        std::string name = CharacterSettings::GetCharacterName(charId);
        if (name.empty() || name == "Undefined") {
            name = (rawName && rawName[0]) ? rawName : "CHARACTER";
        }
        _snprintf_s(label, labelSz, _TRUNCATE, I18n::Tr("OPEN P%d WIKI: %s"), player, name.c_str());
        _snprintf_s(url, urlSz, _TRUNCATE, "https://wiki.gbl.gg/w/Eternal_Fighter_Zero/%s", path);
    };
    fillWiki(d.p1CharID, d.p1CharName, 1, g_helpP1WikiLabel, sizeof(g_helpP1WikiLabel), g_helpP1WikiUrl, sizeof(g_helpP1WikiUrl));
    fillWiki(d.p2CharID, d.p2CharName, 2, g_helpP2WikiLabel, sizeof(g_helpP2WikiLabel), g_helpP2WikiUrl, sizeof(g_helpP2WikiUrl));
}

void OpenUrl(const char* url) {
    if (!url || !url[0]) return;
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenGithubReleases() { OpenUrl("https://github.com/Aquat1c/EfzExtendedPractice/releases"); }
void OpenEternalFighterZeroWiki() { OpenUrl("https://wiki.gbl.gg/w/Eternal_Fighter_Zero"); }
void OpenTrainingModeWiki() { OpenUrl("https://wiki.gbl.gg/w/Eternal_Fighter_Zero/Training_Mode"); }
void OpenEfzDiscord() { OpenUrl("https://discord.gg/aUgqXAt"); }
void OpenP1Wiki() { OpenUrl(g_helpP1WikiUrl); }
void OpenP2Wiki() { OpenUrl(g_helpP2WikiUrl); }
bool P1WikiDisabled() { return g_helpP1WikiUrl[0] == '\0'; }
bool P2WikiDisabled() { return g_helpP2WikiUrl[0] == '\0'; }
const char* ValWiki() { return "OPEN"; }
// Right-aligned value text on the MENU > ABOUT row. Empty string suppresses
// the value entirely, so this costs nothing when no update is pending.
const char* ValUpdateBadge() { return UpdateCheck::BadgeText(); }

    // The update notice is the one line a user must not miss: drawn in the larger header
    // font with the cyan accent; a custom row is the only way to escape the body font.
bool UpdateNoticeHidden() { return !UpdateCheck::HasNewerRelease(); }

void DrawUpdateNotice(ImDrawList* dl, float x, float y, float w, float h) {
    if (!dl || !g_helpUpdateHeadline[0]) return;
    ImFont* font = Layout::HeaderFont();
    const float px = font ? font->FontSize : 18.0f;
    const float ty = Scale::Snap(y + (h - px) * 0.5f);
    const float tx = Scale::Snap(x + 6.0f);
    dl->AddRectFilled(ImVec2(Scale::Snap(x), Scale::Snap(y + 2.0f)),
                      ImVec2(Scale::Snap(x + 3.0f), Scale::Snap(y + h - 2.0f)),
                      Theme::kSelectedLine);
    Layout::DrawString(dl, font, px, tx + 1.0f, ty + 1.0f,
                       IM_COL32(0, 0, 0, 190), g_helpUpdateHeadline);
    Layout::DrawString(dl, font, px, tx, ty, Theme::kSelectedLine,
                       g_helpUpdateHeadline);
}

void DrawMichiruInline(ImDrawList* dl, float x, float y, float w, float h) {
    if (!dl) return;

    const float left = x;
    const float top = y + 2.0f;
    const float right = x + w;
    const float bottom = y + h;

    unsigned gw = 0, gh = 0;
    if (IDirect3DTexture9* tex = GifPlayer::GetTexture(gw, gh)) {
        float drawW = static_cast<float>(gw);
        float drawH = static_cast<float>(gh);
        const float maxW = (std::min)(right - left, 300.0f);
        const float maxH = bottom - top;
        if (drawW > maxW) {
            const float scale = maxW / drawW;
            drawW *= scale;
            drawH *= scale;
        }
        if (drawH > maxH) {
            const float scale = maxH / drawH;
            drawW *= scale;
            drawH *= scale;
        }

        const float imgX = left + (right - left - drawW) * 0.5f;
        const float imgY = top;
        dl->AddImage((ImTextureID)tex, ImVec2(imgX, imgY), ImVec2(imgX + drawW, imgY + drawH));
    }
}

Row* BuildHelpQuickStartRows(int& count) {
    static Row s_rows[32];
    int n = 0;
    s_rows[n++] = Header("QUICK START");
    s_rows[n++] = Info("Open the menu during Practice, set up the drill, then close it to keep playing. The game pauses while the menu is open.");
    s_rows[n++] = Info("Use Main > Opponent for dummy behavior, Main > Values for HP, meter, RF, and recovery, Main > Options for overlays and Frame Bar, Auto for triggers and macros, and Chars for matchup-specific tools.");
    s_rows[n++] = Info("Main > Menu exits to Character Select or the Title Screen.");
    s_rows[n++] = Info("Most toggles apply as soon as you change them. HP, meter, RF, position, and some character values are applied when you adjust them or confirm an edit.");
    s_rows[n++] = Info("Practice hotkeys are ignored while the menu is open and for 0.5s after it closes, so a menu press does not leak into the match.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("FAST SETUP");
    s_rows[n++] = Info("1. Save a position once for quick spacing resets, or use the Practice snapshot hotkeys under Settings > Hotkeys > Savestate to keep the whole match state.");
    s_rows[n++] = Info("2. Pick dummy defense, recovery, or movement in Main > Opponent.");
    s_rows[n++] = Info("3. Turn on Frame Advantage, Combo Statistics, Frame Bar, or Framestep in Main > Options when you need extra feedback.");
    s_rows[n++] = Info("4. Use Auto Actions or Macros for repeatable wakeup, block, hitstun, airtech, and RG tests.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("COMMON HOTKEYS");
    s_rows[n++] = Info(g_helpOpenHelp);
    s_rows[n++] = Info(g_helpToggleOverlay);
    s_rows[n++] = Info(g_helpSavePos);
    s_rows[n++] = Info(g_helpLoadPos);
    s_rows[n++] = Info(g_helpToggleStats);
    count = n;
    return s_rows;
}

Row* BuildHelpPositionRows(int& count) {
    static Row s_rows[18];
    int n = 0;
    s_rows[n++] = Header("POSITION TOOLS");
    s_rows[n++] = Info("Tap Teleport by itself to return to your saved spot.");
    s_rows[n++] = Info("Hold Teleport with a direction to place both players without needing to save first.");
    s_rows[n++] = Info("Teleport + Down centers both players. Teleport + Left or Right puts both of them in the corner you pressed.");
    s_rows[n++] = Info("Teleport + Down + A returns to round-start spacing. On controller, use D-Pad Down + A + Teleport.");
    s_rows[n++] = Info(g_helpSwapPos);
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("BINDINGS");
    s_rows[n++] = Info(g_helpLoadPos);
    s_rows[n++] = Info(g_helpSavePos);
    count = n;
    return s_rows;
}

Row* BuildHelpMenuTipsRows(int& count) {
    static Row s_rows[32];
    int n = 0;
    s_rows[n++] = Header("NAVIGATION");
    s_rows[n++] = Info  ("Use Up/Down or the D-Pad to move focus. Press Enter, Space, or the controller confirm button (A / Cross) to pick the highlighted row.");
    s_rows[n++] = Info  ("Left/Right adjusts the selected value. Hold Shift while pressing Left/Right to use the larger adjustment step.");
    s_rows[n++] = Info  ("Esc or the controller back button (B / Circle) returns from a submenu, closes a picker, or closes the menu.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("TABS");
    s_rows[n++] = Info  (g_helpTopTabs);
    s_rows[n++] = Info  (g_helpSubTabs);
    s_rows[n++] = Info  ("Number keys 1..5 jump directly to a top tab. From the first row in a list, press Up to move focus into subtab and tab selection.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("MOUSE");
    s_rows[n++] = Info  ("Mouse hover only takes focus after the pointer moves. Keyboard and gamepad edges take priority over a resting cursor.");
    s_rows[n++] = Info  ("Click activates a row; the mouse wheel scrolls long lists.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("TIPS");
    s_rows[n++] = Info  (g_helpUiFooter);
    s_rows[n++] = Info  (g_helpControllerSupport);
    s_rows[n++] = Info  ("Controller bindings can be changed in Settings > Hotkeys > Controller.");
    s_rows[n++] = Info  ("If text feels too small or large, adjust UI Scale in Settings > General > Interface.");
    s_rows[n++] = Info  (g_helpOpenHelp);
    count = n;
    return s_rows;
}

Row* BuildHelpStartRows(int& count) {
    static Row s_rows[16];
    int n = 0;
    s_rows[n++] = Header("GETTING STARTED");
    s_rows[n++] = Info("Use this page when you need a reminder without leaving Practice.");
    s_rows[n++] = Info("Quick Start covers the normal training flow. Position Tools lists save/load shortcuts. Menu Tips covers navigation.");
    s_rows[n++] = Info("Long pages scroll with Up/Down, D-Pad, mouse wheel, or the list controls shown in the footer.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("START MENUS");
    s_rows[n++] = Submenu("QUICK START",    "QUICK START",    BuildHelpQuickStartRows, nullptr);
    s_rows[n++] = Submenu("POSITION TOOLS", "POSITION TOOLS", BuildHelpPositionRows,   nullptr);
    s_rows[n++] = Submenu("MENU TIPS",      "MENU TIPS",      BuildHelpMenuTipsRows,   nullptr);
    count = n;
    return s_rows;
}

Row* BuildHelpBasicsRows(int& count) {
    static Row s_rows[40];
    int n = 0;
    s_rows[n++] = Header("BASICS");
    s_rows[n++] = Info("Dummy behavior lives in Main > Opponent: stance, blocking, airtech, and jumps.");
    s_rows[n++] = Info(g_helpSwitchPlayers);
    s_rows[n++] = Info("P2 Control lets you play from Player 2's side. The game's own F6 stance and F7 auto-block keys stop working while it is on.");
    s_rows[n++] = Info("Dummy Auto-Block: Off, All, First Hit (blocks once, then drops guard), After Hit (guards only once a hit lands).");
    s_rows[n++] = Info("Adaptive Stance stands against air attacks and overheads and crouches against grounded attacks. Dummy Stance is greyed out while both it and Dummy Auto-Block are on.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("BLOCK AND RG");
    s_rows[n++] = Info("Random Block flips a coin when the dummy is allowed to block; it is useful for hit-confirm practice.");
    s_rows[n++] = Info("Always Recoil Guard keeps the dummy's RG armed whenever the game allows it. Random Recoil Guard re-arms it at random, so only some blocks come out as RG.");
    s_rows[n++] = Info("Counter RG tries to RG back after you Recoil Guard, where the game allows it.");
    s_rows[n++] = Info("Random Block, Always Recoil Guard, and Random Recoil Guard are mutually exclusive: turning one on turns the other two off.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("TRAINING TOOLS");
    s_rows[n++] = Info("Auto-Airtech: Neutral is off, Forward and Back tech that way. Airtech Delay waits that many frames before teching, for late-tech setups.");
    s_rows[n++] = Info("Auto-Jump makes P1, P2, or both sides jump neutral, forward, or backward when able.");
    s_rows[n++] = Info("Final Memory At Any HP removes the normal 3332 HP requirement. Turn it off to play by the game's rule.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("FRAME ADVANTAGE");
    s_rows[n++] = Info(g_helpFaDuration);
    s_rows[n++] = Info("The number appears once both sides can act again. A yellow Gap readout flashes when your string left the defender free between hits.");
    s_rows[n++] = Info("A Recoil Guard shows two numbers: advantage at the end of the freeze, then advantage once the RG stun is over.");
    count = n;
    return s_rows;
}

Row* BuildHelpRecoveryRows(int& count) {
    static Row s_rows[36];
    int n = 0;
    s_rows[n++] = Header("CONTINUOUS RECOVERY");
    s_rows[n++] = Info("Continuous Recovery restores HP, meter, and RF when a side returns to neutral. Configure it per player under Main > Values > Continuous Recovery.");
    s_rows[n++] = Info("It stops applying while the game's own F4 or F5 recovery is running, so the two never fight over the same values.");
    s_rows[n++] = Info("HP Mode, Meter Mode, and RF Mode each take Off, a preset, or Custom. RF Force Blue IC appears once RF Mode is Custom.");
    s_rows[n++] = Info("Freeze RF After CR is on by default and pins RF where Continuous Recovery put it until you set RF Mode back to Off.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("ENGINE RECOVERY");
    s_rows[n++] = Info("Configure vanilla regeneration directly under Main > Values.");
    s_rows[n++] = Info("F4, F5, or an active Continuous Recovery locks HP, Meter, RF, and IC. X and Y stay editable in Values.");
    s_rows[n++] = Info("Tip: if numbers look wrong, press F4/F5 until the game returns to Normal mode, then re-apply your training values.");
    count = n;
    return s_rows;
}

Row* BuildHelpCharacterRows(int& count) {
    static Row s_rows[48];
    int n = 0;
    s_rows[n++] = Header("CHARACTER SETTINGS");
    s_rows[n++] = Info("Chars only shows controls for characters currently in the match. Match-wide locks appear above the player menus when the matchup supports them.");
    s_rows[n++] = Info("Match-wide locks: Infinite Ikumi Blood, Infinite Shiori Shield, Infinite Misuzu Feather, Lock Mishio Element, Infinite Mishio Awaken, and Minagi Projectiles -> Michiru.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("CHARACTER ROWS");
    s_rows[n++] = Info("Ikumi: set Blood Stock, Genocide Timer, and Level Gauge.");
    s_rows[n++] = Info("Misuzu: Feathers sets her stock. Poison Level 0 is off, Poison Timer counts the poison down, and Infinite Poison pins that timer at max.");
    s_rows[n++] = Info("Mishio: choose Element and set Awaken Timer; the match-wide locks keep element and awakened state from decaying.");
    s_rows[n++] = Info("Akiko: set Bullet Cycle, Freeze Cycle, Show Clean Hit, and read the Time-Slow Trigger. Infinite Timeslow stops the 641236 time-slow counter so the slow never winds down, and Freeze Curse Timer holds the 214214 curse timer once a curse has landed so the opponent stays at that curse level (A: no dashes, B: no specials or supers, C: no jumps). Both are patches on her own tick rather than memory writes: the counters stop where they are, turning a row off resumes them, and the round reset still clears everything. They apply to whichever side plays Akiko.");
    s_rows[n++] = Info("Nayuki (Awake): Snowbunny Timer sets how long the bunnies last. Infinite Snow pins it at max.");
    s_rows[n++] = Info("Nayuki (Asleep): Jam Count sets her stored jams. Lock Jam Count restores that count whenever she is actionable or waking up.");
    s_rows[n++] = Info("Kano: Magic sets the stored magic value, and Lock Magic keeps it from being spent.");
    s_rows[n++] = Info("Nanase (Rumi): Barehanded Mode drops the shinai; Infinite Shinai restores it and overrides Barehanded Mode. Kimchi Active, Kimchi Timer, and Infinite Kimchi drive her Final Memory state.");
    s_rows[n++] = Info("Doppel: Enlightened puts Doppel in the Final Memory state. Follow-Up Tech and Tech Stage decide whether, and out of which follow-up, the opponent escapes after her command throw connects. The grab itself and the Automatic Follow-up can never be escaped.");
    s_rows[n++] = Info("Doppel Follow-Up Tech: Off leaves the escape to the opponent's own input, whether that is a player, a recording, or the CPU. Never locks the opponent out at the selected stages, so those follow-ups always connect. Tech B escapes the B follow-ups (Maiden Fuji Yama, Exploding Inner-Soul Fist, Falling Maiden, Maiden Volcannon). Tech C escapes the A follow-ups (Maiden Crash, Relentless Granite-Breaking Barrage, Maiden Finger, Human Floor-Burning Polisher). Always escapes whichever follow-up Doppel actually goes for, and Random picks between the B and C escape once per follow-up. If the opponent gets their own escape in first, their choice wins.");
    s_rows[n++] = Info("Doppel Tech Stage picks where that escape is allowed. Stage 1 is the capture itself, and escaping there stops Maiden Crash and Maiden Fuji Yama. Stage 2 is Maiden Crash, and escaping there stops Relentless Granite-Breaking Barrage and Exploding Inner-Soul Fist. Stage 3 is those two supers, and escaping there stops Maiden Finger, Falling Maiden, Human Floor-Burning Polisher and Maiden Volcannon. Outside the chosen stage the opponent escapes only if they input it themselves.");
    s_rows[n++] = Info("Sayuri: Remembered Move sets the attack she has countered and is waiting for. Whenever she blocks that same attack standing or crouching she flashes white, and A, B or C cuts straight out of the blockstun into Magical Cutter. Off leaves it to her own counter, Nothing empties it so you can practise baiting, Last Blocked keeps loading whatever she just blocked so it is ready the next time, and picking a move from the list stands in for landing the counter and holds it there so a stray counter cannot replace it. The list is the current opponent's moves and is rebuilt when they change. The cut-out needs a fresh press: a button already held through the block freeze produces nothing, so let go and press again. A dummy Sayuri on auto-block will keep loading and arming on her own, which is usually what you want.");
    s_rows[n++] = Info("Sayuri: Magical Cutter set to Always Ready opens that same window on every standing or crouching block, no matter what she remembers. Air blocking never opens the window, and it stops working once Akiko's debuff has stacked up on her.");
    s_rows[n++] = Info("Sayuri: Auto Cutter presses A for her once the window opens - her block freeze over, no superflash running - so a dummy Sayuri actually cuts out and you can test which strings leave the window open. It is the only way a CPU dummy ever performs the Cutter; Off leaves the press to whoever controls her.");
    s_rows[n++] = Info("Sayuri: Cutter Delay is how many frames she waits after the window opens before pressing. 0 is the earliest press the game accepts, the first frame after the block freeze, which no player can do on reaction; raise it to stand in for a real reaction. If the delay outlasts the blockstun she simply does not cut out.");
    s_rows[n++] = Info("Mizuka: Freeze FM Timer holds her Final Memory timer once the FM is running, so the opponent stays frozen and the FM never ends until you turn it off or the round ends. Her meter stays pinned at 0 and no further poems spawn while it is held; turning it off resumes the countdown from where it stopped. Patch on the FM tick, no memory writes; applies to whichever side plays Mizuka.");
    s_rows[n++] = Info("Mio: Stance picks Short or Long. Lock Stance holds her in the stance you picked.");
    s_rows[n++] = Info("Mai: Status sets Inactive, Active Ghost, Unsummon, Charging, or Awakening. Ghost Time, Charge Timer, and Awaken Timer set that duration, Infinite Ghost/Charge/Awaken hold it, and No Charge Cooldown finishes a charge instantly.");
    s_rows[n++] = Info("Mai also has Force Summon and Force Despawn; Aggressive Summon lets Force Summon work during Unsummon. Ghost Target X/Y with Apply Ghost Position places the ghost exactly.");
    s_rows[n++] = Info("Minagi: Always Readied keeps Michiru ready, and Michiru Target X/Y with Apply Michiru Position places her for setup testing.");
    s_rows[n++] = Info("Mizuka: Note Trigger Ranges and Affected Notes draw her note interactions. Both need Main > Options > Display Overlays > Projectile Interactions on.");
    count = n;
    return s_rows;
}

Row* BuildHelpAutoActionRows(int& count) {
    static Row s_rows[44];
    int n = 0;
    s_rows[n++] = Header("AUTO ACTIONS");
    s_rows[n++] = Info("Auto Actions make the dummy act on key moments: On Wakeup, After Block, After Hitstun, After Airtech, or on Recoil Guard.");
    s_rows[n++] = Info("Pick a trigger at the top of the Triggers page and edit its controls right below. Auto Actions run on the side you are not controlling, P2 by default.");
    s_rows[n++] = Info("Randomize Triggers gives every trigger a 50/50 chance to skip, so your setup is not always answered. Pre-buffer Wakeup starts a wakeup macro early so its first attack is already buffered.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("PER TRIGGER");
    s_rows[n++] = Info("Action covers normals, command normals, specials, supers, jumps, dash and backdash, block, and Final Memory. Left and right on the row change the button or variant.");
    s_rows[n++] = Info("Delay applies after the trigger condition is detected. After Move can add an IC once the move connects, or use the move's own FIC window.");
    s_rows[n++] = Info("Macro Slot runs a recorded macro instead of the single action. Random Pool rolls from the moves ticked in Action Pool, with per-move delays under Pool Options.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("NOTES");
    s_rows[n++] = Info("Specials, supers, dashes, and macros are fed in as inputs, so the dummy drops off AI control for a moment. Normals and jumps are forced directly and leave AI control alone.");
    s_rows[n++] = Info("By default a wakeup action aims for the last frame of wakeup, using each character's own wakeup length.");
    s_rows[n++] = Info("Actions are rate-limited to avoid spam; toggling a trigger clears it. After Airtech is separate from Auto-Airtech, so Auto-Airtech must be enabled first.");
    s_rows[n++] = Info("Turn on Pre-buffer Wakeup when the wakeup action is a macro. Wakeup specials buffer on their own, and a buffered motion re-aims itself if a crossup swaps sides.");
    count = n;
    return s_rows;
}

Row* BuildHelpMacroRows(int& count) {
    static Row s_rows[44];
    int n = 0;
    s_rows[n++] = Header("MACROS");
    s_rows[n++] = Info("Macros record, play, and edit input sequences across eight slots. Playback flips left/right automatically for Player 2.");
    s_rows[n++] = Info(g_helpMacroRecord);
    s_rows[n++] = Info(g_helpMacroPlay);
    s_rows[n++] = Info(g_helpMacroSlot);
    s_rows[n++] = Info("Record enters Pre-recording, where your usual P1 controls drive P2 for recording. Press Record again to start recording, then press it a third time to save.");
    s_rows[n++] = Info("Play runs the current slot. During Pre-recording it cancels instead, and an empty slot does nothing.");
    s_rows[n++] = Info("Playback also follows side swaps, so the recording stays on the character you made it for. Framestep Pause and Step work during playback.");
    s_rows[n++] = Info("Mission authoring reuses Macro Record for PRE-RECORD, a count-in you can set (0.5 sec by default), then Stop to review. Its P1 demo clip does not use any of the eight macro slots.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("CUSTOM MENU TOOLS");
    s_rows[n++] = Info("Serialized Macro holds Edit Text, Apply To Slot, Reload From Slot, Clear Slot, Copy, Paste, Undo, Redo, and Insert Sample.");
    s_rows[n++] = Info("Slot Stats shows slot state, total ticks, effective ticks, first button tick, and buffer capture details.");
    s_rows[n++] = Info("The editor's Apply button saves the text into the current slot. Done closes the editor and keeps the draft text; Cancel reloads from the slot.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("NOTATION");
    s_rows[n++] = Info("Write macros as plain text: one token per tick. The EFZMACRO 1 header is optional when you type text in, and Reload From Slot always writes it. Use numpad directions 1..9, with 5 or N as neutral, and A/B/C/D for buttons.");
    s_rows[n++] = Info("Examples include 5A, 6B, and 2C. A token is one direction plus its buttons, so a motion like 236C is written as separate ticks: 2 3 6C. xN repeats the token before it, so 5x3 is three neutral ticks. A group like {3: 6 6 6} writes three inputs inside one tick, and three is the maximum.");
    s_rows[n++] = Info("Whitespace is flexible; Apply To Slot normalizes the text after it parses successfully.");
    s_rows[n++] = Info("Write notation as if Player 1 is facing right. Player 2 playback flips 4 and 6.");
    s_rows[n++] = Info("Example: EFZMACRO 1 5A 5x3 5B 5x3 5C 6 {3: 6 6 6} 2 {3: 2 2 2} 3 {3: 3 3 3} 5B");
    count = n;
    return s_rows;
}

Row* BuildHelpComboStatisticsRows(int& count) {
    static Row s_rows[40];
    int n = 0;
    s_rows[n++] = Header("COMBO STATISTICS");
    s_rows[n++] = Info("Combo Statistics is a compact damage and resource readout near the top of the screen, just left of center, shown during Practice.");
    s_rows[n++] = Info("It appears only in supported local match states. It clears outside Practice, online sessions, character select, or when the overlay is disabled.");
    s_rows[n++] = Info("Numbers come from the game's own combo counter and damage values, falling back to HP lost while the defender is in hitstun or untech.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("WHAT IT SHOWS");
    s_rows[n++] = Info("Move is last hit damage. Combo is total combo damage. Max is the best combo damage seen this match session.");
    s_rows[n++] = Info("HP shows defender HP at combo start, current HP, and total HP lost.");
    s_rows[n++] = Info("You and Opp rows show meter and RF changes during the combo, for the side you are playing. Resource spends update immediately; passive gains are kept stable so the numbers stay useful.");
    s_rows[n++] = Info("Proration shows the current damage scaling. Detail Row can add defender untech and, when using Last Hit details, the attacker's current move ID.");
    s_rows[n++] = Info("RFx shows the RF damage multiplier and Raw shows the scale value behind Proration. Both are off by default.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("OPTIONS");
    s_rows[n++] = Info("Turn it on with Enable Overlay under Main > Options. It is on by default.");
    s_rows[n++] = Info("Proration is always shown; Show Detail Row adds defender untech next to it. Detail Source chooses between current combo state and last-hit data.");
    s_rows[n++] = Info("Keep Final Summary holds the finished combo on screen. Summary Time sets for how long, 0.5 to 30 seconds, default 1.9.");
    s_rows[n++] = Info("Hide With Menu clears the readout while this menu is open. Resume After Menu brings the final summary back when you close it.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("NOTES");
    s_rows[n++] = Info("Max resets when the match/session resets. Combo Statistics is for quick damage and resource checks; use Framebar when you need timing and state breakdowns.");
    count = n;
    return s_rows;
}

Row* BuildHelpFramebarRows(int& count) {
    static Row s_rows[56];
    int n = 0;
    s_rows[n++] = Header("FRAMEBAR");
    s_rows[n++] = Info("Framebar is a per-player timeline near the bottom-center of the screen. Each cell is one subframe, 1/3 of a visual frame.");
    s_rows[n++] = Info("Set Cell Step to Visual Frames for one cell per displayed frame instead.");
    s_rows[n++] = Info("The right edge is the current frame, oldest on the left, so read it left to right.");
    s_rows[n++] = Info("It starts advancing when something important happens: attacks, stun, projectiles, lockout, Recoil Guard, or an overlapping block/RG check.");
    s_rows[n++] = Info("After roughly one second of calm, it freezes in place until the next action. This keeps the last useful sequence visible instead of scrolling through neutral forever.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("COLORS");
    s_rows[n++] = Info("Green means neutral movement, walking, crouching, landing, or falling.");
    s_rows[n++] = Info("Yellow means prejump, jump, double jump, airtech, or ground tech.");
    s_rows[n++] = Info("Blue and cyan mean dashes, air dashes, and Recoil Guard windows.");
    s_rows[n++] = Info("Red is attacking: dark red startup, bright red active, muted red recovery. Grey is blockstun, hitstun, or launch.");
    s_rows[n++] = Info("Purple is being thrown, pink is superflash, and blue-grey is shared hitstop.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("MARKERS");
    s_rows[n++] = Info("A white vertical line marks the first active frame detected in the current attack sequence.");
    s_rows[n++] = Info("Orange top ticks mark live projectile slots. A second orange tick means the projectile's current frame has attack boxes.");
    s_rows[n++] = Info("A bright red bottom strip means a live hitbox. Dark red means the move has attack data out but no live box yet.");
    s_rows[n++] = Info("Muted brown strips are the attacker's post-hit countdown, not active frames.");
    s_rows[n++] = Info("Blue and cyan small strips mean that side can block or Recoil Guard the overlapping character or projectile attack.");
    s_rows[n++] = Info("A dark blue band means shared hitstop. Yellow/magenta flashes call out untech, hit, block/RG, throw, and counter-hit moments.");
    s_rows[n++] = Info("Purple middle marks track air-mobility counters, useful when checking double jumps and air dashes.");
    s_rows[n++] = Info("Detail controls how much of this appears: Full shows every marker and all four status lines, Compact drops to two status lines and keeps only the hitbox strip, the projectile ticks, and the first-active line, and Bars Only shows the colored bars alone with no text and no markers.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("STATUS LINES");
    s_rows[n++] = Info("ID is the move ID, F is the current animation frame. BX is active/total character boxes, PB is projectile attack boxes, P is how many projectiles are out.");
    s_rows[n++] = Info("ST is hitstop while attacking, or remaining blockstun/hitstun while defending. UT is untech or stun time from the last hit. SF is superflash freeze on whoever triggered it. AM is air-mobility counters.");
    s_rows[n++] = Info("FA is the first active frame of the sequence. ACT counts consecutive active frames, character or projectile. TOT is how many frames that side has been busy in this sequence.");
    s_rows[n++] = Info("ATK is the attacker's countdown after a hit resolves. FL and CL are the frame and collision lockouts. GG is guard gauge, 0 to 360.");
    s_rows[n++] = Info("B and RG show whether that side can block or Recoil Guard the attack currently overlapping it. G is its guard state.");
    s_rows[n++] = Info("HS is a raw hit-state readout, not a confirmed hit result. CH is counter-hit. HST means both sides are in shared hitstop.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("FRAMESTEP");
    s_rows[n++] = Info("While paused or framestepping, Framebar advances only when the game steps, so you can review one frame at a time.");
    s_rows[n++] = Info("Turn it on with Show Frame Bar under Main > Options.");
    count = n;
    return s_rows;
}

Row* BuildHelpBoxDisplayRows(int& count) {
    static Row s_rows[40];
    int n = 0;
    s_rows[n++] = Header("BOX DISPLAY");
    s_rows[n++] = Info("Box Display draws the game's real hit, hurt, collision, and projectile boxes over the match.");
    s_rows[n++] = Info("Enable it from Main > Options > Display Overlays. Boxes only draw during a Practice match, never in netplay.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("CHARACTER BOXES");
    s_rows[n++] = Info("Red boxes are hitboxes.");
    s_rows[n++] = Info("Green boxes are hurtboxes.");
    s_rows[n++] = Info("Yellow boxes are collision boxes, also called pushboxes.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("PROJECTILE BOXES");
    s_rows[n++] = Info("Blue boxes on projectiles are the area the game checks for projectile interactions.");
    s_rows[n++] = Info("Bright blue means the projectile is active for that check. Faint blue means it is out but not eligible right now.");
    s_rows[n++] = Info("The blue box does not always match the sprite - projectiles can check bigger or smaller than they look.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("PROJECTILE HELPERS");
    s_rows[n++] = Info("White dots are projectile origin points: the anchor, not the center of the blue box.");
    s_rows[n++] = Info("Magenta boxes show where two active projectile boxes overlap. Use this to check clashes and projectile interactions.");
    s_rows[n++] = Info("If a projectile returns or changes state, its visible sprite may keep moving even when its blue interaction box is gone.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("MIZUKA NOTES");
    s_rows[n++] = Info("When Mizuka is in the match, Chars > Mizuka Notes Display adds the two overlays below.");
    s_rows[n++] = Info("Note Trigger Ranges draws the orange areas where notes can be set off. Strong fill is triggering now, light fill is the range preview.");
    s_rows[n++] = Info("Affected Notes tints a note pale yellow once it has been set off. Sitting inside a trigger range does not tint a note on its own.");
    s_rows[n++] = Info("Both are on by default, but nothing draws unless Display Overlays > Projectile Interactions is on.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("READING IT");
    s_rows[n++] = Info("When a box and the sprite disagree, the box is what the game uses.");
    s_rows[n++] = Info("Box Fill Alpha changes only the fill. Outlines stay full strength so edges stay readable.");
    s_rows[n++] = Info("Turn layers on one at a time if the screen gets noisy: Hitboxes, Hurtboxes, Collision Boxes, then Projectile Interactions.");
    count = n;
    return s_rows;
}

Row* BuildHelpSavestatesRows(int& count) {
    static Row s_rows[28];
    int n = 0;
    s_rows[n++] = Header("PRACTICE SNAPSHOTS");
    s_rows[n++] = Info("Snapshots save and restore the whole Practice match through EfzRevival. They are hotkey-only.");
    s_rows[n++] = Info("Use them for retry loops and for keeping a whole setup between attempts. Save and load only work during a Practice match.");
    s_rows[n++] = Info("Position save/load hotkeys only move players. Snapshots capture the whole match state through Revival.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("HOTKEYS");
    s_rows[n++] = Info(g_helpSavestateSave);
    s_rows[n++] = Info(g_helpSavestateLoad);
    s_rows[n++] = Info("There is one snapshot. Saving again replaces it, and loading always restores the latest save.");
    s_rows[n++] = Info("Rebind both under Settings > Hotkeys > Savestate.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("NOTES");
    s_rows[n++] = Info("EfzRevival must be present for snapshots to work. Unsupported Revival builds may disable or break save/load.");
    s_rows[n++] = Info("For spacing-only drills, position save/load is usually faster than a full snapshot.");
    count = n;
    return s_rows;
}

Row* BuildHelpIssuesRows(int& count) {
    static Row s_rows[36];
    int n = 0;
    s_rows[n++] = Header("CONFLICTS");
    s_rows[n++] = Info("Turning on Random Block, Always Recoil Guard, or Random Recoil Guard switches the other two off.");
    s_rows[n++] = Info("Counter RG is greyed out while Always Recoil Guard is on.");
    s_rows[n++] = Info("The menu pauses the game and blocks Practice hotkeys, and hotkeys stay off for 0.5s after it closes.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("TROUBLESHOOTING");
    s_rows[n++] = Info("If values look wrong, press F4/F5 until the game returns to Normal mode, then re-apply your values.");
    s_rows[n++] = Info("Settings > Recovery can hold Continuous Recovery until both sides are neutral. If it still looks wrong, leave Practice and re-enter.");
    s_rows[n++] = Info(g_helpOpenHelp);
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("UNSUPPORTED REVIVAL");
    s_rows[n++] = Info("On an unsupported EfzRevival build some tools stop working and others can misbehave.");
    s_rows[n++] = Info("Do not use an unsupported build for netplay. If the game misbehaves, launch efz.exe directly instead.");
    s_rows[n++] = Info("On those builds a Practice hotkey can still reach the game while this menu is open, though the pause should still hold.");
    count = n;
    return s_rows;
}

Row* BuildHelpGuideRows(int& count) {
    static Row s_rows[20];
    int n = 0;
    s_rows[n++] = Header("GUIDE MENUS");
    s_rows[n++] = Info("Open these sections for feature behavior, caveats, setup notes, and troubleshooting.");
    s_rows[n++] = Submenu("BASICS",               "BASICS",               BuildHelpBasicsRows,     nullptr);
    s_rows[n++] = Submenu("PRACTICE SNAPSHOTS",   "PRACTICE SNAPSHOTS",   BuildHelpSavestatesRows, nullptr);
    s_rows[n++] = Submenu("COMBO STATISTICS",   "COMBO STATISTICS",   BuildHelpComboStatisticsRows, nullptr);
    s_rows[n++] = Submenu("FRAMEBAR",           "FRAMEBAR",           BuildHelpFramebarRows,   nullptr);
    s_rows[n++] = Submenu("BOX DISPLAY",        "BOX DISPLAY",        BuildHelpBoxDisplayRows, nullptr);
    s_rows[n++] = Submenu("RECOVERY",           "RECOVERY",           BuildHelpRecoveryRows,   nullptr);
    s_rows[n++] = Submenu("CHARACTER SETTINGS", "CHARACTER SETTINGS", BuildHelpCharacterRows,  nullptr);
    s_rows[n++] = Submenu("AUTO ACTIONS",       "AUTO ACTIONS",       BuildHelpAutoActionRows, nullptr);
    s_rows[n++] = Submenu("MACROS",             "MACROS",             BuildHelpMacroRows,      nullptr);
    s_rows[n++] = Submenu("ISSUES",             "ISSUES",             BuildHelpIssuesRows,     nullptr);
    count = n;
    return s_rows;
}

Row* BuildHelpResourcesRows(int& count) {
    static Row s_rows[18];
    int n = 0;
    s_rows[n++] = Header("GAME RESOURCES");
    s_rows[n++] = Info("Each link below opens in your default browser, outside the game.");
    s_rows[n++] = Action("EFZ WIKI",           OpenEternalFighterZeroWiki, ValWiki);
    s_rows[n++] = Action("TRAINING MODE WIKI", OpenTrainingModeWiki,       ValWiki);
    s_rows[n++] = Action("EFZ GLOBAL DISCORD", OpenEfzDiscord,             ValWiki);
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("CURRENT CHARACTERS");
    s_rows[n++] = Info("The two rows below stay greyed out until each side's character is identified.");
    // Translated labels no longer match the label-keyed default help.
    const char* kCharacterWikiHelp = "Opens the current character's wiki page in your browser.";
    s_rows[n] = Action(g_helpP1WikiLabel[0] ? g_helpP1WikiLabel : "OPEN P1 WIKI", OpenP1Wiki, ValWiki, P1WikiDisabled);
    s_rows[n++].helpText = kCharacterWikiHelp;
    s_rows[n] = Action(g_helpP2WikiLabel[0] ? g_helpP2WikiLabel : "OPEN P2 WIKI", OpenP2Wiki, ValWiki, P2WikiDisabled);
    s_rows[n++].helpText = kCharacterWikiHelp;
    count = n;
    return s_rows;
}

Row* BuildHelpAboutRows(int& count) {
    static Row s_rows[48];
    int n = 0;
    s_rows[n++] = Header("EFZ TRAINING MODE");
    s_rows[n++] = Custom(26.0f, DrawUpdateNotice, UpdateNoticeHidden);
    s_rows[n++] = Info(g_helpVersionStr);
    s_rows[n++] = Info(g_helpBuildStr);
    s_rows[n++] = Info("A training toolkit for Eternal Fighter Zero - frame data, drills, macros, and matchup tools in one place.");
    s_rows[n++] = Info(g_helpUpdateStr);
    s_rows[n++] = Action("OPEN GITHUB RELEASES", OpenGithubReleases, ValWiki);
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("YOUR GAME");
    s_rows[n++] = Info(g_helpDetectedVersion);
    s_rows[n++] = Info("Works with vanilla EFZ and supported EfzRevival builds (1.02e through the verified 1.02j MinGW build).");
    s_rows[n++] = Info("Some features may be limited on unsupported or very new Revival versions.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("WHAT YOU GET");
    s_rows[n++] = Info("Frame advantage display, Combo Statistics, Framebar, practice snapshots, and continuous recovery.");
    s_rows[n++] = Info("Dummy auto-actions, input macros, character-specific settings, and match hotswap without leaving Practice.");
    s_rows[n++] = Info("Everything is configurable from this menu - no editing files by hand.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("CONTROLS");
    s_rows[n++] = Info("Play with keyboard and mouse, or use a gamepad - extensive controller support is built in.");
    s_rows[n++] = Info("Xbox, PlayStation, and most common gamepads use the same button bindings.");
    s_rows[n++] = Info("Open, navigate, and close the menu from your pad. Rebind anything under Settings > Hotkeys.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("OBLIGATORY MICHIRU");
    s_rows[n++] = Custom(216.0f, DrawMichiruInline);
    count = n;
    return s_rows;
}

// AUTO / TRIGGERS
Row WithHelp(Row row, const char* helpText) {
    row.helpText = helpText;
    return row;
}

int SelectedPoolActionCount(int triggerIdx) {
    uint64_t lo = 0;
    uint64_t hi = 0;
    CurrentPoolMaskForTriggerIndex(triggerIdx, lo, hi);
    int count = 0;
    for (int i = 0; i < kActionPoolCount; ++i) {
        if (ConcretePoolBitSet(lo, hi, i)) ++count;
    }
    return count;
}

const char* ValPoolDelaySummary() {
    static char s_buf[64];
    const int triggerIdx = ClampIndex(g_selectedAutoTrigger, 5);
    if (!PoolEnabledForTriggerIndex(triggerIdx)) {
        return "";
    }

    uint64_t lo = 0;
    uint64_t hi = 0;
    CurrentPoolMaskForTriggerIndex(triggerIdx, lo, hi);
    const int* delays = PoolDelayArrayForTriggerIndex(triggerIdx);
    const int defaultDelay = DefaultDelayForTriggerIndex(triggerIdx);

    int selected = 0;
    int firstDelay = -1;
    bool mixed = false;
    for (int i = 0; i < kActionPoolCount; ++i) {
        if (!ConcretePoolBitSet(lo, hi, i)) continue;
        if (!CharacterActionCatalog::IsPoolIndexAvailable(
                EffectiveAutoActionCharId(), i)) continue;
        const int effectiveDelay = EffectivePoolDelayForIndex(delays, i, defaultDelay);
        if (selected == 0) {
            firstDelay = effectiveDelay;
        } else if (effectiveDelay != firstDelay) {
            mixed = true;
        }
        ++selected;
    }

    if (selected <= 0) {
        return "EMPTY";
    }
    if (mixed) {
        _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE,
                    I18n::Tr("%d MOVES / MIXED"), selected);
    } else {
        _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE,
                    I18n::Tr("%d %s / %dF"),
                    selected,
                    I18n::Tr(selected == 1 ? "MOVE" : "MOVES"),
                    firstDelay);
    }
    return s_buf;
}

Row* BuildPoolDelayRows(int& count) {
    static Row s_rows[224];
    static char s_labels[MAX_ACTION_POOL_OPTIONS][48];
    static char s_help[MAX_ACTION_POOL_OPTIONS][160];
    static char s_chargeLabels[MAX_ACTION_POOL_OPTIONS][48];
    static char s_chargeHelp[MAX_ACTION_POOL_OPTIONS][160];

    int n = 0;
    const int triggerIdx = ClampIndex(g_selectedAutoTrigger, 5);
    uint64_t lo = 0;
    uint64_t hi = 0;
    CurrentPoolMaskForTriggerIndex(triggerIdx, lo, hi);
    int* delays = PoolDelayArrayForTriggerIndex(triggerIdx);
    int* charges = PoolChargeArrayForTriggerIndex(triggerIdx);

    s_rows[n++] = Header("POOL OPTIONS");
    if (!PoolEnabledForTriggerIndex(triggerIdx)) {
        s_rows[n++] = Info("Turn on Random Pool to edit per-move delays.");
        count = n;
        return s_rows;
    }
    if (!delays || !charges || (lo | hi) == 0) {
        s_rows[n++] = Info("Select moves in Action Pool first.");
        count = n;
        return s_rows;
    }

    const bool normalized = NormalizeSelectedPoolDelaysForTrigger(triggerIdx, lo, hi, true);
    if (normalized) {
        OnAutoApply();
    }

    s_rows[n++] = Info("Each selected move keeps its own delay and optional IC/FIC follow-up.");
    for (int i = 0; i < kActionPoolCount && n < 222; ++i) {
        if (!ConcretePoolBitSet(lo, hi, i)) continue;
        if (!CharacterActionCatalog::IsPoolIndexAvailable(
                EffectiveAutoActionCharId(), i)) continue;
        _snprintf_s(s_labels[i], sizeof(s_labels[i]), _TRUNCATE,
                    I18n::Tr("%s DELAY"), kActionPoolNames[i]);
        _snprintf_s(s_help[i], sizeof(s_help[i]), _TRUNCATE,
                    I18n::Tr("Waits this many frames when Random Pool rolls %s."),
                    kActionPoolNames[i]);
        s_rows[n++] = WithHelp(IntNum(s_labels[i],
                                      &delays[i],
                                      0,
                                      60,
                                      1,
                                      5,
                                      OnAutoApply),
                               s_help[i]);
        int action = ACTION_5A;
        int strength = 0;
        if (CharacterActionCatalog::PoolIndexToAction(i, action, strength) &&
            ActionSupportsChargeFollowup(action)) {
            charges[i] = ClampIndex(charges[i], 3);
            _snprintf_s(s_chargeLabels[i], sizeof(s_chargeLabels[i]), _TRUNCATE,
                        I18n::Tr("%s AFTER MOVE"), kActionPoolNames[i]);
            _snprintf_s(s_chargeHelp[i], sizeof(s_chargeHelp[i]), _TRUNCATE,
                        I18n::Tr("IC adds 22C once %s connects; FIC uses that move's own fixed 22C window."),
                        kActionPoolNames[i]);
            s_rows[n++] = WithHelp(ChoicesRow(s_chargeLabels[i],
                                              &charges[i],
                                              kChargeFollowupChoices,
                                              3,
                                              OnAutoApply),
                                   s_chargeHelp[i]);
        } else {
            charges[i] = 0;
        }
    }

    count = n;
    return s_rows;
}

void AddTriggerRows(Row* rows, int& n,
                    const char* title,
                    bool* enabled,
                    int* actionPickIdx,
                    void (*onActionPick)(),
                    int* action,
                    int* strength,
                    int* macroSlot,
                    int* delay,
                    int* chargeFollowup,
                    bool* usePool,
                    uint64_t* poolMaskLo,
                    uint64_t* poolMaskHi,
                    void (*onUsePool)(),
                    void (*onPoolMask)(),
                    bool (*hideRegularDelay)(),
                    bool (*hideSingleAction)(),
                    bool (*hidePool)(),
                    bool (*hideCharge)()) {
    rows[n++] = Header(title);
    rows[n++] = WithHelp(Toggle("ENABLE", enabled, OnAutoApply),
                         "Turns this trigger on so the dummy performs the action set below.");
    Row actionRow = DropdownRow("ACTION",        actionPickIdx, g_triggerActionChoiceArr, g_triggerActionChoiceCount,
                                onActionPick, nullptr, hideSingleAction);
    actionRow.choice2IdxPtr = action;
    actionRow.intPtr = strength;
    actionRow.valueFormatter = FormatTriggerActionChoiceRow;
    actionRow.inlineAdjuster = AdjustTriggerActionChoiceRow;
    actionRow.choiceValueFormatter = FormatTriggerActionPopupChoice;
    actionRow.choiceValueAdjuster = AdjustTriggerActionPopupChoice;
    actionRow.choiceVariantPtr = &g_mirrorFwdDashFollowup;
    actionRow.choiceCategoryMap = g_triggerActionCategoryMap;
    actionRow.categoryChoices = kTriggerActionCategoryChoices;
    actionRow.categoryCount = kTriggerActionCategoryCount;
    actionRow.helpText = "Chooses what the dummy performs for this trigger; left and right change the button or variant.";
    rows[n++] = actionRow;
    rows[n++] = WithHelp(IntNum("DELAY", delay, 0, 60, 1, 5, OnAutoApply, nullptr, hideRegularDelay),
                         "Waits this many frames before the dummy starts the selected response.");
    rows[n++] = WithHelp(ChoicesRow("AFTER MOVE", chargeFollowup,
                                    kChargeFollowupChoices, 3,
                                    OnAutoApply, nullptr, hideCharge),
                         "IC adds 22C once the move connects; FIC uses the move's own fixed 22C window.");
    rows[n++] = WithHelp(DropdownRow("MACRO SLOT", macroSlot, g_macroSlotChoiceArr, g_macroSlotChoiceCount,
                                     OnAutoApply),
                         "Runs the chosen recorded macro instead of Action. None keeps the single action.");
    rows[n++] = WithHelp(Toggle("RANDOM POOL", usePool, onUsePool),
                         "Rolls a random move from Action Pool instead of always using Action.");
    EnsureActionPoolCategoryMap();
    Row poolRow = MaskPickerRow64("ACTION POOL", poolMaskLo, poolMaskHi,
                                  kActionPoolNames, kActionPoolCount,
                                  onPoolMask, nullptr, hidePool);
    poolRow.choiceCategoryMap = kActionPoolCategoryMap;
    poolRow.categoryChoices = kTriggerActionCategoryChoices;
    poolRow.categoryCount = kTriggerActionCategoryCount;
    poolRow.valueFormatter = FormatActionPoolSummary;
    poolRow.choiceValueFormatter = FormatActionPoolChoice;
    poolRow.choiceHelpFormatter = FormatActionPoolChoiceHelp;
    poolRow.choiceFilter = FilterActionPoolChoice;
    poolRow.helpText = "Selects the exact move versions Random Pool is allowed to roll.";
    rows[n++] = poolRow;
    rows[n++] = WithHelp(Submenu("POOL OPTIONS",
                                 "POOL OPTIONS",
                                 BuildPoolDelayRows,
                                 ValPoolDelaySummary,
                                 nullptr,
                                 hidePool),
                         "Sets a separate delay and IC/FIC follow-up for each selected Random Pool move.");
}

const char* AutoActionTargetInfo() {
    static char s_buf[64];
    const int target = ResolveAutoActionTargetPlayer();
    _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE, I18n::Tr("TARGET: P%d (OPPONENT SIDE)"), target);
    return s_buf;
}

Row* BuildTriggersRows(int& count) {
    static Row s_rows[40];
    int n = 0;
    auto& d = ImGuiGui::guiState.localData;
    RefreshTriggerActionChoices();
    static const char* const kAutoTriggerChoices[5] = {
        "AFTER BLOCK",
        "ON WAKEUP",
        "AFTER HITSTUN",
        "AFTER AIRTECH",
        "ON RECOIL GUARD"
    };

    s_rows[n++] = Header("AUTO ACTIONS");
    s_rows[n++] = WithHelp(Info(AutoActionTargetInfo()),
                           "Auto Actions run on the side you are not playing, so switching sides moves the target.");
    g_selectedAutoTrigger = ClampIndex(g_selectedAutoTrigger, 5);
    s_rows[n++] = WithHelp(ChoicesRow("TRIGGER", &g_selectedAutoTrigger, kAutoTriggerChoices, 5),
                           "Chooses which auto-action timing you are editing on this page.");

    s_rows[n++] = Spacer();
    switch (g_selectedAutoTrigger) {
        case 0:
            g_motionIdxAB = GetMotionIndexForAction(d.actionAfterBlock);
            g_actionPickIdxAB = GetTriggerActionChoiceIndex(d.actionAfterBlock, d.strengthAfterBlock);
            AddTriggerRows(s_rows, n, "AFTER BLOCK",
                           &d.triggerAfterBlock,
                           &g_actionPickIdxAB, OnTriggerActionAfterBlock,
                           &d.actionAfterBlock, &d.strengthAfterBlock,
                           &d.macroSlotAfterBlock, &d.delayAfterBlock, &d.chargeAfterBlock,
                           &g_useMaskAB, &g_poolMaskABLo, &g_poolMaskABHi, OnUseMaskAB, OnPoolMaskAB,
                           HideABRegularDelay,
                           HideABSingleAction, HideABPool, HideABCharge);
            break;
        case 1:
            g_motionIdxWU = GetMotionIndexForAction(d.actionOnWakeup);
            g_actionPickIdxWU = GetTriggerActionChoiceIndex(d.actionOnWakeup, d.strengthOnWakeup);
            AddTriggerRows(s_rows, n, "ON WAKEUP",
                           &d.triggerOnWakeup,
                           &g_actionPickIdxWU, OnTriggerActionOnWakeup,
                           &d.actionOnWakeup, &d.strengthOnWakeup,
                           &d.macroSlotOnWakeup, &d.delayOnWakeup, &d.chargeOnWakeup,
                           &g_useMaskWU, &g_poolMaskWULo, &g_poolMaskWUHi, OnUseMaskWU, OnPoolMaskWU,
                           HideWURegularDelay,
                           HideWUSingleAction, HideWUPool, HideWUCharge);
            break;
        case 2:
            g_motionIdxAH = GetMotionIndexForAction(d.actionAfterHitstun);
            g_actionPickIdxAH = GetTriggerActionChoiceIndex(d.actionAfterHitstun, d.strengthAfterHitstun);
            AddTriggerRows(s_rows, n, "AFTER HITSTUN",
                           &d.triggerAfterHitstun,
                           &g_actionPickIdxAH, OnTriggerActionAfterHitstun,
                           &d.actionAfterHitstun, &d.strengthAfterHitstun,
                           &d.macroSlotAfterHitstun, &d.delayAfterHitstun, &d.chargeAfterHitstun,
                           &g_useMaskAH, &g_poolMaskAHLo, &g_poolMaskAHHi, OnUseMaskAH, OnPoolMaskAH,
                           HideAHRegularDelay,
                           HideAHSingleAction, HideAHPool, HideAHCharge);
            break;
        case 3:
            g_motionIdxAA = GetMotionIndexForAction(d.actionAfterAirtech);
            g_actionPickIdxAA = GetTriggerActionChoiceIndex(d.actionAfterAirtech, d.strengthAfterAirtech);
            AddTriggerRows(s_rows, n, "AFTER AIRTECH",
                           &d.triggerAfterAirtech,
                           &g_actionPickIdxAA, OnTriggerActionAfterAirtech,
                           &d.actionAfterAirtech, &d.strengthAfterAirtech,
                           &d.macroSlotAfterAirtech, &d.delayAfterAirtech, &d.chargeAfterAirtech,
                           &g_useMaskAA, &g_poolMaskAALo, &g_poolMaskAAHi, OnUseMaskAA, OnPoolMaskAA,
                           HideAARegularDelay,
                           HideAASingleAction, HideAAPool, HideAACharge);
            break;
        default:
            g_motionIdxRG = GetMotionIndexForAction(d.actionOnRG);
            g_actionPickIdxRG = GetTriggerActionChoiceIndex(d.actionOnRG, d.strengthOnRG);
            AddTriggerRows(s_rows, n, "ON RECOIL GUARD",
                           &d.triggerOnRG,
                           &g_actionPickIdxRG, OnTriggerActionOnRG,
                           &d.actionOnRG, &d.strengthOnRG,
                           &d.macroSlotOnRG, &d.delayOnRG, &d.chargeOnRG,
                           &g_useMaskRG, &g_poolMaskRGLo, &g_poolMaskRGHi, OnUseMaskRG, OnPoolMaskRG,
                           HideRGRegularDelay,
                           HideRGSingleAction, HideRGPool, HideRGCharge);
            break;
    }

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("GLOBAL");
    s_rows[n++] = WithHelp(Toggle("RANDOMIZE TRIGGERS", &g_mirrorRandomize, OnRandomizeToggle),
                           "Gives every enabled trigger a 50/50 chance to skip, so your setup is not always answered.");
    s_rows[n++] = WithHelp(Toggle("PRE-BUFFER WAKEUP", &g_mirrorWakeBuffer, OnWakeBufferToggle),
                           "Starts a wakeup macro early so its first attack is buffered. Wakeup specials buffer without it.");

    count = n;
    return s_rows;
}

// CHARACTERS screen
bool CharHasCustomRows(int charId) {
    switch (charId) {
        case CHAR_ID_IKUMI:
        case CHAR_ID_MISUZU:
        case CHAR_ID_MISHIO:
        case CHAR_ID_AKIKO:
        case CHAR_ID_NAYUKIB:
        case CHAR_ID_NAYUKI:
        case CHAR_ID_KANO:
        case CHAR_ID_SAYURI:
        case CHAR_ID_MIZUKA:
        case CHAR_ID_NANASE:
        case CHAR_ID_EXNANASE:
        case CHAR_ID_MIO:
        case CHAR_ID_MAI:
        case CHAR_ID_MINAGI:
            return true;
        default:
            return false;
    }
}

void RefreshCharacterDataAction() {
    ImGuiGui::RefreshLocalData();
}

std::string CharacterDisplayName(int charId, const char* rawName) {
    std::string name = CharacterSettings::GetCharacterName(charId);
    if (name.empty() || name == "Undefined") {
        name = (rawName && rawName[0]) ? rawName : "(NONE)";
    }
    return name;
}

void ForceP1MaiSummon()   { ImGuiGui::guiState.localData.p1MaiForceSummon = true; OnAutoApply(); }
void ForceP2MaiSummon()   { ImGuiGui::guiState.localData.p2MaiForceSummon = true; OnAutoApply(); }
void ForceP1MaiDespawn()  { ImGuiGui::guiState.localData.p1MaiForceDespawn = true; OnAutoApply(); }
void ForceP2MaiDespawn()  { ImGuiGui::guiState.localData.p2MaiForceDespawn = true; OnAutoApply(); }
void ApplyP1MaiGhostPos() { ImGuiGui::guiState.localData.p1MaiApplyGhostPos = true; OnAutoApply(); }
void ApplyP2MaiGhostPos() { ImGuiGui::guiState.localData.p2MaiApplyGhostPos = true; OnAutoApply(); }
void ApplyP1MinagiPos()   { ImGuiGui::guiState.localData.p1MinagiApplyPos = true; OnAutoApply(); }
void ApplyP2MinagiPos()   { ImGuiGui::guiState.localData.p2MinagiApplyPos = true; OnAutoApply(); }

void AddCharacterLockRows(Row* rows, int& n) {
    bool addedHeader = false;
    auto header = [&]() {
        if (!addedHeader) {
            rows[n++] = Header("MATCH-WIDE LOCKS");
            addedHeader = true;
        }
    };

    if (HasIkumi()) {
        header();
        rows[n++] = Toggle("INFINITE IKUMI BLOOD", &g_mirrorInfiniteBlood, OnInfBlood);
    }
    if (HasShiori()) {
        header();
        rows[n++] = Toggle("INFINITE SHIORI SHIELD", &g_mirrorInfiniteShioriShield, OnInfShioriShield);
    }
    if (HasMisuzu()) {
        header();
        rows[n++] = Toggle("INFINITE MISUZU FEATHER", &g_mirrorInfiniteFeather, OnInfFeather);
    }
    if (HasMishio()) {
        header();
        rows[n++] = Toggle("LOCK MISHIO ELEMENT", &g_mirrorInfiniteElement, OnInfElement);
        rows[n++] = Toggle("INFINITE MISHIO AWAKEN", &g_mirrorInfiniteAwakened, OnInfAwakened);
    }
    if (HasMinagi()) {
        header();
        rows[n++] = Toggle("MINAGI PROJECTILES -> MICHIRU", &ImGuiGui::guiState.localData.minagiConvertNewProjectiles, OnAutoApply);
    }

    if (addedHeader) {
        rows[n++] = Spacer();
    }
}

void AddMizukaNoteDisplayRows(Row* rows, int& n) {
    if (!HasMizuka()) {
        return;
    }

    rows[n++] = Header("MIZUKA NOTES DISPLAY");
    auto& s = MutableSettings();
    rows[n++] = Toggle("NOTE TRIGGER RANGES", &s.collisionDisplayNagamoriRanges, OnCollisionNagamoriRanges);
    rows[n++] = Toggle("AFFECTED NOTES", &s.collisionDisplayNagamoriAffected, OnCollisionNagamoriAffected);
    rows[n++] = Info("Both need Display Overlays > Projectile Interactions turned on.");
    rows[n++] = Spacer();
}

void AddIkumiRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = IntNum("BLOOD STOCK",  &d.p1IkumiBlood, 0, IKUMI_BLOOD_MAX, 1, 1, OnAutoApply);
        rows[n++] = IntNum("GENOCIDE TIMER", &d.p1IkumiGenocide, 0, IKUMI_GENOCIDE_MAX, 30, 300, OnAutoApply);
        rows[n++] = IntNum("LEVEL GAUGE", &d.p1IkumiLevelGauge, 0, 99, 1, 10, OnAutoApply);
    } else {
        rows[n++] = IntNum("BLOOD STOCK",  &d.p2IkumiBlood, 0, IKUMI_BLOOD_MAX, 1, 1, OnAutoApply);
        rows[n++] = IntNum("GENOCIDE TIMER", &d.p2IkumiGenocide, 0, IKUMI_GENOCIDE_MAX, 30, 300, OnAutoApply);
        rows[n++] = IntNum("LEVEL GAUGE", &d.p2IkumiLevelGauge, 0, 99, 1, 10, OnAutoApply);
    }
}

void AddMisuzuRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = IntNum("FEATHERS", &d.p1MisuzuFeathers, 0, MISUZU_FEATHER_MAX, 1, 1, OnAutoApply);
        rows[n++] = Toggle("INFINITE POISON", &d.p1MisuzuInfinitePoison, OnAutoApply);
        rows[n++] = IntNum("POISON TIMER", &d.p1MisuzuPoisonTimer, 0, MISUZU_POISON_TIMER_MAX, 30, 300, OnAutoApply);
        rows[n++] = IntNum("POISON LEVEL", &d.p1MisuzuPoisonLevel, 0, 99, 1, 10, OnAutoApply);
    } else {
        rows[n++] = IntNum("FEATHERS", &d.p2MisuzuFeathers, 0, MISUZU_FEATHER_MAX, 1, 1, OnAutoApply);
        rows[n++] = Toggle("INFINITE POISON", &d.p2MisuzuInfinitePoison, OnAutoApply);
        rows[n++] = IntNum("POISON TIMER", &d.p2MisuzuPoisonTimer, 0, MISUZU_POISON_TIMER_MAX, 30, 300, OnAutoApply);
        rows[n++] = IntNum("POISON LEVEL", &d.p2MisuzuPoisonLevel, 0, 99, 1, 10, OnAutoApply);
    }
}

void AddMishioRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = ChoicesRow("ELEMENT", &d.p1MishioElement, kElementChoices, 4, OnAutoApply);
        rows[n++] = IntNum("AWAKEN TIMER", &d.p1MishioAwakenedTimer, 0, MISHIO_AWAKENED_TARGET, 60, 600, OnAutoApply);
    } else {
        rows[n++] = ChoicesRow("ELEMENT", &d.p2MishioElement, kElementChoices, 4, OnAutoApply);
        rows[n++] = IntNum("AWAKEN TIMER", &d.p2MishioAwakenedTimer, 0, MISHIO_AWAKENED_TARGET, 60, 600, OnAutoApply);
    }
}

void AddAkikoRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = WithHelp(ChoicesRow("BULLET CYCLE", &d.p1AkikoBulletCycle, kAkikoBulletChoices, 3, OnAutoApply), "Which bullets 236A and 236B throw next. Using either one advances the cycle; Freeze Cycle holds it in place.");
        rows[n++] = Toggle("FREEZE CYCLE", &d.p1AkikoFreezeCycle, OnAutoApply);
        rows[n++] = Toggle("SHOW CLEAN HIT", &d.p1AkikoShowCleanHit, OnAutoApply);
        rows[n++] = ChoicesRow("TIME-SLOW TRIGGER", &d.p1AkikoTimeslowTrigger, kAkikoSlowChoices, 4, OnAutoApply);
        rows[n++] = WithHelp(Toggle("INFINITE TIMESLOW", &d.p1AkikoInfiniteTimeslow, OnAutoApply), "Stops the time-slow counter, so a 641236 slow never winds down. Patch on her tick, no memory writes.");
        rows[n++] = WithHelp(Toggle("FREEZE CURSE TIMER", &d.p1AkikoFreezeCurse, OnAutoApply), "Holds the 214214 curse timer once a curse lands, so the opponent stays at that curse level.");
    } else {
        rows[n++] = WithHelp(ChoicesRow("BULLET CYCLE", &d.p2AkikoBulletCycle, kAkikoBulletChoices, 3, OnAutoApply), "Which bullets 236A and 236B throw next. Using either one advances the cycle; Freeze Cycle holds it in place.");
        rows[n++] = Toggle("FREEZE CYCLE", &d.p2AkikoFreezeCycle, OnAutoApply);
        rows[n++] = Toggle("SHOW CLEAN HIT", &d.p2AkikoShowCleanHit, OnAutoApply);
        rows[n++] = ChoicesRow("TIME-SLOW TRIGGER", &d.p2AkikoTimeslowTrigger, kAkikoSlowChoices, 4, OnAutoApply);
        rows[n++] = WithHelp(Toggle("INFINITE TIMESLOW", &d.p2AkikoInfiniteTimeslow, OnAutoApply), "Stops the time-slow counter, so a 641236 slow never winds down. Patch on her tick, no memory writes.");
        rows[n++] = WithHelp(Toggle("FREEZE CURSE TIMER", &d.p2AkikoFreezeCurse, OnAutoApply), "Holds the 214214 curse timer once a curse lands, so the opponent stays at that curse level.");
    }
}

void AddNayukiRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = Toggle("INFINITE SNOW", &d.p1NayukiInfiniteSnow, OnAutoApply);
        rows[n++] = IntNum("SNOWBUNNY TIMER", &d.p1NayukiSnowbunnies, 0, NAYUKIB_SNOWBUNNY_MAX, 30, 300, OnAutoApply);
    } else {
        rows[n++] = Toggle("INFINITE SNOW", &d.p2NayukiInfiniteSnow, OnAutoApply);
        rows[n++] = IntNum("SNOWBUNNY TIMER", &d.p2NayukiSnowbunnies, 0, NAYUKIB_SNOWBUNNY_MAX, 30, 300, OnAutoApply);
    }
}

void AddNeyukiRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = IntNum("JAM COUNT", &d.p1NeyukiJamCount, 0, NEYUKI_JAM_COUNT_MAX, 1, 1, OnAutoApply);
        rows[n++] = Toggle("LOCK JAM COUNT", &d.p1NeyukiLockJam, OnAutoApply);
    } else {
        rows[n++] = IntNum("JAM COUNT", &d.p2NeyukiJamCount, 0, NEYUKI_JAM_COUNT_MAX, 1, 1, OnAutoApply);
        rows[n++] = Toggle("LOCK JAM COUNT", &d.p2NeyukiLockJam, OnAutoApply);
    }
}

void AddKanoRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = Toggle("LOCK MAGIC", &d.p1KanoLockMagic, OnAutoApply);
        rows[n++] = IntNum("MAGIC", &d.p1KanoMagic, 0, KANO_MAGIC_MAX, 100, 1000, OnAutoApply);
    } else {
        rows[n++] = Toggle("LOCK MAGIC", &d.p2KanoLockMagic, OnAutoApply);
        rows[n++] = IntNum("MAGIC", &d.p2KanoMagic, 0, KANO_MAGIC_MAX, 100, 1000, OnAutoApply);
    }
}

void AddRumiRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = Toggle("INFINITE SHINAI", &d.p1RumiInfiniteShinai, OnAutoApply);
        rows[n++] = Toggle("BAREHANDED MODE", &d.p1RumiBarehanded, OnAutoApply);
        rows[n++] = Toggle("KIMCHI ACTIVE", &d.p1RumiKimchiActive, OnAutoApply);
        rows[n++] = Toggle("INFINITE KIMCHI", &d.p1RumiInfiniteKimchi, OnAutoApply);
        rows[n++] = IntNum("KIMCHI TIMER", &d.p1RumiKimchiTimer, 0, RUMI_KIMCHI_TARGET, 30, 300, OnAutoApply);
    } else {
        rows[n++] = Toggle("INFINITE SHINAI", &d.p2RumiInfiniteShinai, OnAutoApply);
        rows[n++] = Toggle("BAREHANDED MODE", &d.p2RumiBarehanded, OnAutoApply);
        rows[n++] = Toggle("KIMCHI ACTIVE", &d.p2RumiKimchiActive, OnAutoApply);
        rows[n++] = Toggle("INFINITE KIMCHI", &d.p2RumiInfiniteKimchi, OnAutoApply);
        rows[n++] = IntNum("KIMCHI TIMER", &d.p2RumiKimchiTimer, 0, RUMI_KIMCHI_TARGET, 30, 300, OnAutoApply);
    }
}

void AddDoppelRows(Row* rows, int& n, DisplayData& d, int player) {
    rows[n++] = Toggle("ENLIGHTENED", player == 1 ? &d.p1DoppelEnlightened : &d.p2DoppelEnlightened, OnAutoApply);
    // These two rows sit in Doppel's pane but describe the OPPONENT. Explicit
    // help text keeps them off the tooltip label-matching tables in screens.cpp.
    rows[n++] = WithHelp(
        ChoicesRow("FOLLOW-UP TECH",
                   player == 1 ? &d.p1DoppelTechMode : &d.p2DoppelTechMode,
                   kDoppelTechChoices, 6, OnAutoApply),
    // The footer renders ONE line and truncates past ~100 chars; the full breakdown lives
    // on the wrapped Help > Character Settings page instead.
        "How the opponent escapes Doppel's follow-ups. Off leaves it to them; Never locks them out.");
    rows[n++] = WithHelp(
        ChoicesRow("TECH STAGE",
                   player == 1 ? &d.p1DoppelTechStage : &d.p2DoppelTechStage,
                   kDoppelTechStageChoices, 4, OnAutoApply),
        "Picks which follow-up the opponent may escape. Elsewhere only their own input escapes.");
}

    // Sayuri's REMEMBERED MOVE list is per-opponent; rebuilt by the SayuriCounter module
    // and handed here as labels only - the module keeps the resolved move ID.
void AddSayuriRows(Row* rows, int& n, DisplayData& d, int player) {
    SayuriCounter::RefreshMoveChoices();

    int* choice = (player == 1) ? &d.p1SayuriMemoryChoice : &d.p2SayuriMemoryChoice;
    // A selection that no longer names a move on THIS opponent falls back to OFF; never
    // silently keep the index - the same position means a different attack per character.
    const int validated = SayuriCounter::ValidateChoiceIndex(player, *choice);
    if (validated != *choice) {
    // Publish the correction directly: the OPPONENT changed, not the player, and a full
    // settings apply from a row builder would re-push health/meter/RF mid-drill.
        *choice = validated;
        SayuriCounter::SetMemory(player, validated, 0);
    }

    rows[n++] = WithHelp(
        DropdownRow("REMEMBERED MOVE", choice,
                    SayuriCounter::MoveChoiceItems(player),
                    SayuriCounter::MoveChoiceCount(player),
                    OnAutoApply),
    // The footer renders ONE line and truncates past ~100 chars; the full explanation
    // lives on the wrapped Help > Character Settings page instead.
        "Off changes nothing; Nothing keeps her empty; anything else is what she waits to block.");

    if (!SayuriCounter::OpponentListAvailable(player)) {
        rows[n++] = Info("No move list for this opponent. Last Blocked still works.");
    }
    if (SayuriCounter::MoveListTruncated(player)) {
        rows[n++] = Info("Some of this opponent's moves are not listed.");
    }

    rows[n++] = WithHelp(
        ChoicesRow("MAGICAL CUTTER",
                   player == 1 ? &d.p1SayuriCutterMode : &d.p2SayuriCutterMode,
                   kSayuriCutterChoices, 2, OnAutoApply),
        "Always Ready opens her cut-out window on every standing or crouching block.");

    rows[n++] = WithHelp(
        ChoicesRow("AUTO CUTTER",
                   player == 1 ? &d.p1SayuriAutoCutter : &d.p2SayuriAutoCutter,
                   kSayuriAutoCutterChoices, 2, OnAutoApply),
        "On presses A for her once the cut-out window opens, so a dummy Sayuri actually cuts out.");

    rows[n++] = WithHelp(
        IntNum("CUTTER DELAY",
               player == 1 ? &d.p1SayuriAutoCutterDelay : &d.p2SayuriAutoCutterDelay,
               0, SayuriCounter::kAutoCutterDelayMax, 1, 5, OnAutoApply, nullptr,
               player == 1 ? HideP1SayuriCutterDelay : HideP2SayuriCutterDelay),
        "Frames she waits after the window opens before pressing; 0 is the earliest press the game accepts.");
}

void AddMioRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = ChoicesRow("STANCE", &d.p1MioStance, kStanceChoices, 2, OnAutoApply);
        rows[n++] = Toggle("LOCK STANCE", &d.p1MioLockStance, OnAutoApply);
    } else {
        rows[n++] = ChoicesRow("STANCE", &d.p2MioStance, kStanceChoices, 2, OnAutoApply);
        rows[n++] = Toggle("LOCK STANCE", &d.p2MioLockStance, OnAutoApply);
    }
}

void AddMaiRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = ChoicesRow("STATUS", &d.p1MaiStatus, kMaiStatusChoices, 5, OnAutoApply);
        rows[n++] = IntNum("GHOST TIME", &d.p1MaiGhostTime, 0, MAI_GHOST_TIME_MAX, 60, 600, OnAutoApply);
        rows[n++] = IntNum("CHARGE TIMER", &d.p1MaiGhostCharge, 0, MAI_GHOST_CHARGE_MAX, 60, 600, OnAutoApply);
        rows[n++] = IntNum("AWAKEN TIMER", &d.p1MaiAwakeningTime, 0, MAI_AWAKENING_MAX, 60, 600, OnAutoApply);
        rows[n++] = Toggle("INFINITE GHOST", &d.p1MaiInfiniteGhost, OnAutoApply);
        rows[n++] = Toggle("INFINITE CHARGE", &d.p1MaiInfiniteCharge, OnAutoApply);
        rows[n++] = Toggle("INFINITE AWAKEN", &d.p1MaiInfiniteAwakening, OnAutoApply);
        rows[n++] = Toggle("NO CHARGE COOLDOWN", &d.p1MaiNoChargeCD, OnAutoApply);
        rows[n++] = Toggle("AGGRESSIVE SUMMON", &d.p1MaiAggressiveOverride, OnAutoApply);
        rows[n++] = Action("FORCE SUMMON", ForceP1MaiSummon);
        rows[n++] = Action("FORCE DESPAWN", ForceP1MaiDespawn);
        rows[n++] = DoubleNum("GHOST TARGET X", &d.p1MaiGhostSetX, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = DoubleNum("GHOST TARGET Y", &d.p1MaiGhostSetY, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = Action("APPLY GHOST POSITION", ApplyP1MaiGhostPos);
    } else {
        rows[n++] = ChoicesRow("STATUS", &d.p2MaiStatus, kMaiStatusChoices, 5, OnAutoApply);
        rows[n++] = IntNum("GHOST TIME", &d.p2MaiGhostTime, 0, MAI_GHOST_TIME_MAX, 60, 600, OnAutoApply);
        rows[n++] = IntNum("CHARGE TIMER", &d.p2MaiGhostCharge, 0, MAI_GHOST_CHARGE_MAX, 60, 600, OnAutoApply);
        rows[n++] = IntNum("AWAKEN TIMER", &d.p2MaiAwakeningTime, 0, MAI_AWAKENING_MAX, 60, 600, OnAutoApply);
        rows[n++] = Toggle("INFINITE GHOST", &d.p2MaiInfiniteGhost, OnAutoApply);
        rows[n++] = Toggle("INFINITE CHARGE", &d.p2MaiInfiniteCharge, OnAutoApply);
        rows[n++] = Toggle("INFINITE AWAKEN", &d.p2MaiInfiniteAwakening, OnAutoApply);
        rows[n++] = Toggle("NO CHARGE COOLDOWN", &d.p2MaiNoChargeCD, OnAutoApply);
        rows[n++] = Toggle("AGGRESSIVE SUMMON", &d.p2MaiAggressiveOverride, OnAutoApply);
        rows[n++] = Action("FORCE SUMMON", ForceP2MaiSummon);
        rows[n++] = Action("FORCE DESPAWN", ForceP2MaiDespawn);
        rows[n++] = DoubleNum("GHOST TARGET X", &d.p2MaiGhostSetX, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = DoubleNum("GHOST TARGET Y", &d.p2MaiGhostSetY, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = Action("APPLY GHOST POSITION", ApplyP2MaiGhostPos);
    }
}

void AddMinagiRows(Row* rows, int& n, DisplayData& d, int player) {
    if (player == 1) {
        rows[n++] = Toggle("ALWAYS READIED", &d.p1MinagiAlwaysReadied, OnAutoApply);
        rows[n++] = DoubleNum("MICHIRU TARGET X", &d.p1MinagiPuppetSetX, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = DoubleNum("MICHIRU TARGET Y", &d.p1MinagiPuppetSetY, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = Action("APPLY MICHIRU POSITION", ApplyP1MinagiPos);
    } else {
        rows[n++] = Toggle("ALWAYS READIED", &d.p2MinagiAlwaysReadied, OnAutoApply);
        rows[n++] = DoubleNum("MICHIRU TARGET X", &d.p2MinagiPuppetSetX, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = DoubleNum("MICHIRU TARGET Y", &d.p2MinagiPuppetSetY, -2000.0, 2000.0, 1.0, 10.0, "%.1f");
        rows[n++] = Action("APPLY MICHIRU POSITION", ApplyP2MinagiPos);
    }
}

void AddNagamoriRows(Row* rows, int& n, DisplayData& d, int player) {
    rows[n++] = WithHelp(
        Toggle("FREEZE FM TIMER", player == 1 ? &d.p1MizukaFreezeFm : &d.p2MizukaFreezeFm, OnAutoApply),
        "Holds her Final Memory timer once the FM runs: the opponent stays frozen and the FM never ends until you turn it off.");
}

bool AddPlayerCharacterRows(Row* rows, int& n, DisplayData& d, int player, int charId, const char* rawName) {
    if (!CharHasCustomRows(charId)) return false;

    static char s_headers[2][64];
    char* header = s_headers[(player == 2) ? 1 : 0];
    const std::string name = CharacterDisplayName(charId, rawName);
    _snprintf_s(header, sizeof(s_headers[0]), _TRUNCATE, "P%d  %s", player, name.c_str());
    rows[n++] = Header(header);

    switch (charId) {
        case CHAR_ID_IKUMI:    AddIkumiRows(rows, n, d, player); break;
        case CHAR_ID_MISUZU:   AddMisuzuRows(rows, n, d, player); break;
        case CHAR_ID_MISHIO:   AddMishioRows(rows, n, d, player); break;
        case CHAR_ID_AKIKO:    AddAkikoRows(rows, n, d, player); break;
        case CHAR_ID_NAYUKIB:  AddNayukiRows(rows, n, d, player); break;
        case CHAR_ID_NAYUKI:   AddNeyukiRows(rows, n, d, player); break;
        case CHAR_ID_KANO:     AddKanoRows(rows, n, d, player); break;
        case CHAR_ID_SAYURI:   AddSayuriRows(rows, n, d, player); break;
        case CHAR_ID_MIZUKA:   AddNagamoriRows(rows, n, d, player); break;
        case CHAR_ID_NANASE:   AddRumiRows(rows, n, d, player); break;
        case CHAR_ID_EXNANASE: AddDoppelRows(rows, n, d, player); break;
        case CHAR_ID_MIO:      AddMioRows(rows, n, d, player); break;
        case CHAR_ID_MAI:      AddMaiRows(rows, n, d, player); break;
        case CHAR_ID_MINAGI:   AddMinagiRows(rows, n, d, player); break;
        default: break;
    }
    return true;
}

const char* ValP1Character() {
    static char s_buf[64];
    const auto& d = ImGuiGui::guiState.localData;
    const std::string name = CharacterDisplayName(d.p1CharID, d.p1CharName);
    _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE, "%s", name.c_str());
    return s_buf;
}

const char* ValP2Character() {
    static char s_buf[64];
    const auto& d = ImGuiGui::guiState.localData;
    const std::string name = CharacterDisplayName(d.p2CharID, d.p2CharName);
    _snprintf_s(s_buf, sizeof(s_buf), _TRUNCATE, "%s", name.c_str());
    return s_buf;
}

Row* BuildP1CharacterRows(int& count) {
    static Row s_rows[64];
    int n = 0;
    auto& d = ImGuiGui::guiState.localData;
    if (!AddPlayerCharacterRows(s_rows, n, d, 1, d.p1CharID, d.p1CharName)) {
        s_rows[n++] = Header("STATUS");
        s_rows[n++] = Info("No custom controls for Player 1.");
    }
    count = n;
    return s_rows;
}

Row* BuildP2CharacterRows(int& count) {
    static Row s_rows[64];
    int n = 0;
    auto& d = ImGuiGui::guiState.localData;
    if (!AddPlayerCharacterRows(s_rows, n, d, 2, d.p2CharID, d.p2CharName)) {
        s_rows[n++] = Header("STATUS");
        s_rows[n++] = Info("No custom controls for Player 2.");
    }
    count = n;
    return s_rows;
}

Row* BuildCharsRows(int& count) {
    static Row s_rows[32];
    int n = 0;
    auto& d = ImGuiGui::guiState.localData;

    s_rows[n++] = Header("CHARACTER CONTROLS");
    s_rows[n++] = Action("REFRESH CHARACTER DATA", RefreshCharacterDataAction);

    s_rows[n++] = Spacer();
    AddCharacterLockRows(s_rows, n);
    AddMizukaNoteDisplayRows(s_rows, n);

    const bool p1HasRows = CharHasCustomRows(d.p1CharID);
    const bool p2HasRows = CharHasCustomRows(d.p2CharID);
    if (p1HasRows || p2HasRows) {
        s_rows[n++] = Header("PLAYER MENUS");
        if (p1HasRows) {
            s_rows[n++] = Submenu("PLAYER 1", "PLAYER 1 CHARACTER", BuildP1CharacterRows, ValP1Character);
        }
        if (p2HasRows) {
            s_rows[n++] = Submenu("PLAYER 2", "PLAYER 2 CHARACTER", BuildP2CharacterRows, ValP2Character);
        }
    }

    if (!p1HasRows && !p2HasRows && !HasMizuka()) {
        s_rows[n++] = Header("STATUS");
        s_rows[n++] = Info("No supported character controls in this matchup.");
    }

    count = n;
    return s_rows;
}

// OPPONENT screen
const char* const kAirtechDirChoices[3] = { "OFF", "FORWARD", "BACK" };
const char* const kJumpDirChoices[3]    = { "NEUTRAL", "FORWARD", "BACK" };
const char* const kJumpTargetChoices[3] = { "P1", "P2", "BOTH" };
const char* const kDummyBlockChoices[4] = { "OFF", "ALL", "FIRST HIT", "AFTER HIT" };
const char* const kDummyStanceChoices[3] = { "STAND", "JUMP", "CROUCH" };

bool RandomBlockHidden() { return g_mirrorDummyBlockMode == 0 && !g_mirrorRandomBlock; }
bool RandomBlockDisablesBlockMode() { return g_mirrorRandomBlock; }
const char* RandomBlockModeHelp() {
    return g_mirrorRandomBlock
        ? "Random Block is using this configured blocking window. Turn Random Block off to change it."
        : "Choose when the dummy may auto-block: never, every hit, only the first hit, or only after the first hit.";
}
bool AdaptiveStanceHidden() { return g_mirrorDummyBlockMode == 0 && !g_mirrorAdaptiveStance; }
bool AdaptiveDisablesStance() { return g_mirrorDummyBlockMode != 0 && g_mirrorAdaptiveStance; }
bool CounterRGDisabled() { return g_mirrorAlwaysRG; }
bool MovementJumpDirHidden();
bool MovementJumpTargetHidden();
bool AirtechDelayHidden();

// Most recovery/movement settings bind directly to DisplayData. Auto-Airtech uses
// a small mirror because the custom menu exposes disabled/forward/back as one row.

const char* ValDefenseSummary() {
    if (g_mirrorAlwaysRG) return "ALWAYS RG";
    if (g_mirrorRandomRG) return "RANDOM RG";
    if (g_mirrorDummyBlockMode != 0) {
        if (g_mirrorRandomBlock) return "RANDOM BLOCK";
        if (g_mirrorAdaptiveStance) return "ADAPTIVE BLOCK";
        return kDummyBlockChoices[g_mirrorDummyBlockMode];
    }
    return "OFF";
}

const char* ValRecoverySummary() {
    const auto& d = ImGuiGui::guiState.localData;
    if (!d.autoAirtech) return "OFF";
    return ClampIndex(d.airtechDirection, 2) == 0 ? "FORWARD" : "BACK";
}

void OnAirtechMode() {
    auto& d = ImGuiGui::guiState.localData;
    const int mode = ClampIndex(g_mirrorAirtechMode, 3);
    d.autoAirtech = mode != 0;
    d.airtechDirection = (mode > 0) ? (mode - 1) : 0;
    OnAutoApply();
}

void OnAutoJumpTarget() {
    static constexpr int kTargetMap[3] = {1, 2, 3};
    const int idx = ClampIndex(g_mirrorAutoJumpTargetIdx, 3);
    g_mirrorAutoJumpTargetIdx = idx;
    ImGuiGui::guiState.localData.jumpTarget = kTargetMap[idx];
    OnAutoApply();
}

const char* ValMovementSummary() {
    const auto& d = ImGuiGui::guiState.localData;
    return d.autoJump ? "AUTO-JUMP" : "OFF";
}

Row* BuildOpponentDefenseRows(int& count) {
    static Row s_rows[16];
    int n = 0;

    s_rows[n++] = WithHelp(ChoicesRow("DUMMY AUTO-BLOCK", &g_mirrorDummyBlockMode,
                                      kDummyBlockChoices, 4, OnDummyBlockMode,
                                      RandomBlockDisablesBlockMode),
                           RandomBlockModeHelp());
    s_rows[n++] = WithHelp(Toggle("RANDOM BLOCK", &g_mirrorRandomBlock, OnRandomBlock,
                                  nullptr, RandomBlockHidden),
                           "Coin-flips the dummy's guard each frame inside the Dummy Auto-Block window, so some hits land.");
    s_rows[n++] = WithHelp(Toggle("ADAPTIVE STANCE", &g_mirrorAdaptiveStance, OnAdaptiveStance,
                                  nullptr, AdaptiveStanceHidden),
                           "Automatically switches the dummy between standing and crouching guard for incoming attacks.");
    s_rows[n++] = WithHelp(Toggle("ALWAYS RECOIL GUARD", &g_mirrorAlwaysRG, OnAlwaysRG),
                           "Keeps the dummy's Recoil Guard armed. Normal RG rules apply: no grounded RG on two quick hits in a row.");
    s_rows[n++] = WithHelp(Toggle("RANDOM RECOIL GUARD", &g_mirrorRandomRG, OnRandomRG),
                           "Arms the dummy's Recoil Guard on a coin flip each frame, so some blocks come out as RG.");
    s_rows[n++] = WithHelp(Toggle("COUNTER RG", &g_mirrorCounterRG, OnCounterRGToggle,
                                  CounterRGDisabled),
                           "Tries to Recoil Guard back after your Recoil Guard; unavailable while Always RG is on.");
    count = n;
    return s_rows;
}

Row* BuildOpponentRecoveryRows(int& count) {
    static Row s_rows[8];
    int n = 0;
    auto& d = ImGuiGui::guiState.localData;
    g_mirrorAirtechMode = d.autoAirtech ? (ClampIndex(d.airtechDirection, 2) + 1) : 0;

    s_rows[n++] = WithHelp(ChoicesRow("AUTO-AIRTECH", &g_mirrorAirtechMode,
                                      kAirtechDirChoices, 3, OnAirtechMode),
                           "Makes the dummy air-recover forward or back once it can tech. Neutral turns Auto-Airtech off.");
    s_rows[n++] = WithHelp(IntNum("  AIRTECH DELAY", &d.airtechDelay, 0, 60, 1, 5, OnAutoApply,
                                  nullptr, AirtechDelayHidden),
                           "Waits this many frames after airtech is available before recovering.");
    count = n;
    return s_rows;
}

Row* BuildOpponentMovementRows(int& count) {
    static Row s_rows[8];
    int n = 0;
    auto& d = ImGuiGui::guiState.localData;

    s_rows[n++] = WithHelp(Toggle("AUTO-JUMP", &d.autoJump, OnAutoApply),
                           "Makes the selected side jump on its own and again on every landing.");
    s_rows[n++] = WithHelp(ChoicesRow("  JUMP DIRECTION", &d.jumpDirection,
                                      kJumpDirChoices, 3, OnAutoApply,
                                      nullptr, MovementJumpDirHidden),
                           "Sets neutral, forward, or back jump direction for Auto-Jump.");
    s_rows[n++] = WithHelp(ChoicesRow("  JUMP TARGET", &g_mirrorAutoJumpTargetIdx,
                                      kJumpTargetChoices, 3, OnAutoJumpTarget,
                                      nullptr, MovementJumpTargetHidden),
                           "Chooses whether Auto-Jump applies to P1, P2, or both sides.");
    count = n;
    return s_rows;
}

bool MovementJumpDirHidden() {
    return !ImGuiGui::guiState.localData.autoJump;
}
bool MovementJumpTargetHidden() {
    return !ImGuiGui::guiState.localData.autoJump;
}
bool AirtechDelayHidden() {
    return g_mirrorAirtechMode == 0;
}

Row* BuildOpponentRows(int& count) {
    static Row s_rows[40];
    int n = 0;
    auto& d = ImGuiGui::guiState.localData;
    g_mirrorAirtechMode = d.autoAirtech ? (ClampIndex(d.airtechDirection, 2) + 1) : 0;

    s_rows[n++] = Header("OPPONENT");
    s_rows[n++] = WithHelp(Toggle("ENABLE P2 CONTROL", &d.p2ControlEnabled, OnAutoApply),
                           "Lets you play P2 in Practice. The game's F6 stance and F7 auto-block keys stop working while this is on.");
    s_rows[n++] = WithHelp(ChoicesRow("DUMMY STANCE", &g_mirrorPracticeStance,
                                      kDummyStanceChoices, 3, OnPracticeStance,
                                      AdaptiveDisablesStance),
                           "Sets the dummy's F6 stance. Adaptive Stance takes it over while Dummy Auto-Block is on.");
    s_rows[n++] = WithHelp(ChoicesRow("AUTO-AIRTECH", &g_mirrorAirtechMode,
                                      kAirtechDirChoices, 3, OnAirtechMode),
                           "Makes the dummy air-recover forward or back once it can tech. Neutral turns Auto-Airtech off.");
    s_rows[n++] = WithHelp(IntNum("  AIRTECH DELAY", &d.airtechDelay, 0, 60, 1, 5, OnAutoApply,
                                  nullptr, AirtechDelayHidden),
                           "Waits this many frames after airtech is available before recovering.");
    s_rows[n++] = WithHelp(Toggle("AUTO-JUMP", &d.autoJump, OnAutoApply),
                           "Makes the selected side jump on its own and again on every landing.");
    s_rows[n++] = WithHelp(ChoicesRow("  JUMP DIRECTION", &d.jumpDirection,
                                      kJumpDirChoices, 3, OnAutoApply,
                                      nullptr, MovementJumpDirHidden),
                           "Sets neutral, forward, or back jump direction for Auto-Jump.");
    s_rows[n++] = WithHelp(ChoicesRow("  JUMP TARGET", &g_mirrorAutoJumpTargetIdx,
                                      kJumpTargetChoices, 3, OnAutoJumpTarget,
                                      nullptr, MovementJumpTargetHidden),
                           "Chooses whether Auto-Jump applies to P1, P2, or both sides.");
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("DEFENSE");
    {
        int subN = 0;
        Row* defense = BuildOpponentDefenseRows(subN);
        for (int i = 0; i < subN; ++i) {
            s_rows[n++] = defense[i];
        }
    }

    count = n;
    return s_rows;
}

// MENU screen
void RunExitToCharacterSelect() {
    RequestFrontendExit(FrontendExitTarget::CharacterSelect);
}

void RunExitToTitle() {
    RequestFrontendExit(FrontendExitTarget::Title);
}

const char* const kHotswapCharacterChoices[] = {
    "Rumi", "Ayu", "Mai", "Makoto", "Akane", "Mayu",
    "Mizuka", "Misaki", "Shiori", "Sayuri", "Neyuki", "Mio",
    "Doppel", "Kaori", "Ikumi", "Mishio", "Akiko", "Nayuki",
    "Unknown", "Kanna", "Kano", "Minagi", "Misuzu",
};

const int kHotswapCharacterSelectIds[] = {
    0, 1, 2, 3, 4, 5,
    6, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 23,
};

constexpr int kHotswapCharacterChoiceCount = static_cast<int>(sizeof(kHotswapCharacterChoices) / sizeof(kHotswapCharacterChoices[0]));

constexpr uintptr_t kHotswapScreenTableRva = 0x00390110;
constexpr uint8_t kHotswapCharacterSelectScreen = 1;
constexpr uintptr_t kHotswapCsP1SelectionOffset = 1340;
constexpr uintptr_t kHotswapCsP2SelectionOffset = 1341;

const char* const kHotswapPaletteChoices[] = {
    "1", "2", "3", "4", "5", "6",
};

constexpr int kHotswapPaletteChoiceCount = static_cast<int>(sizeof(kHotswapPaletteChoices) / sizeof(kHotswapPaletteChoices[0]));

int g_hotswapMenuP1Character = 4;
int g_hotswapMenuP2Character = 16;
int g_hotswapMenuP1Palette = 0;
int g_hotswapMenuP2Palette = 0;
int g_hotswapMenuP1PalettePrev = 0;
int g_hotswapMenuP2PalettePrev = 0;
bool g_hotswapMenuP1CustomPalette = false;
bool g_hotswapMenuP2CustomPalette = false;
int g_hotswapMenuStage = 0;
int g_hotswapMenuOstChoice = 0;
bool g_hotswapMenuSeeded = false;

struct HotswapCustomAvailabilityLogState {
    int selectId = -1;
    int palette = -1;
    int available = -1;
    int disabled = -1;
};

HotswapCustomAvailabilityLogState g_hotswapP1CustomAvailabilityLog;
HotswapCustomAvailabilityLogState g_hotswapP2CustomAvailabilityLog;

struct HotswapCurrentState {
    bool charsValid = false;
    int p1SelectId = 4;
    int p2SelectId = 16;
    bool paletteValid = false;
    CharacterHotswap::PaletteSelection paletteSelection;
    bool stageValid = false;
    int stageId = 0;
    bool bgmValid = false;
    int bgmTrack = 0;
};

int CharacterSelectIdForHotswapChoice(int choiceIdx);

bool HotswapCurrentStatesEqual(const HotswapCurrentState& lhs, const HotswapCurrentState& rhs) {
    return lhs.charsValid == rhs.charsValid
        && lhs.p1SelectId == rhs.p1SelectId
        && lhs.p2SelectId == rhs.p2SelectId
        && lhs.paletteValid == rhs.paletteValid
        && lhs.paletteSelection.p1Color == rhs.paletteSelection.p1Color
        && lhs.paletteSelection.p2Color == rhs.paletteSelection.p2Color
        && lhs.paletteSelection.p1UseCustomPalette == rhs.paletteSelection.p1UseCustomPalette
        && lhs.paletteSelection.p2UseCustomPalette == rhs.paletteSelection.p2UseCustomPalette
        && lhs.stageValid == rhs.stageValid
        && lhs.stageId == rhs.stageId
        && lhs.bgmValid == rhs.bgmValid
        && lhs.bgmTrack == rhs.bgmTrack;
}

void LogSavestateHotswapPromptStateIfChanged(const char* reason,
                                             bool promptVisible,
                                             const CustomSavestate::Summary* summary,
                                             const HotswapCurrentState* current,
                                             int savedP1SelectId,
                                             int savedP2SelectId) {
    static std::string s_lastLogLine;

    std::ostringstream oss;
    oss << "[SAVESTATE][MENU][HOTSWAP]"
        << " reason=" << (reason ? reason : "unknown")
        << " prompt=" << (promptVisible ? 1 : 0);

    if (summary) {
        oss << " rawChars=" << static_cast<unsigned int>(summary->savedP1CharId)
            << "/" << static_cast<unsigned int>(summary->savedP2CharId)
            << " selectChars=" << savedP1SelectId
            << "/" << savedP2SelectId
            << " savedStage=" << static_cast<unsigned int>(summary->savedStageId)
            << " compat=" << (summary->currentPairCompatible ? 1 : 0)
            << "/" << (summary->currentStageCompatible ? 1 : 0)
            << "/" << (summary->currentVersionCompatible ? 1 : 0);
    }

    if (current) {
        oss << " currentCharsValid=" << (current->charsValid ? 1 : 0)
            << " currentChars=" << current->p1SelectId
            << "/" << current->p2SelectId
            << " currentStageValid=" << (current->stageValid ? 1 : 0)
            << " currentStage=" << current->stageId
            << " currentBgmValid=" << (current->bgmValid ? 1 : 0)
            << " currentBgm=" << current->bgmTrack;
    }

    const std::string line = oss.str();
    if (line != s_lastLogLine) {
        LogOut(line, true);
        s_lastLogLine = line;
    }
}

void LogHotswapMenuSelection(const char* reason) {
    std::ostringstream oss;
    oss << "[HOTSWAP][MENU] " << (reason ? reason : "state")
        << " p1Char=" << CharacterSelectIdForHotswapChoice(g_hotswapMenuP1Character)
        << " p2Char=" << CharacterSelectIdForHotswapChoice(g_hotswapMenuP2Character)
        << " palette=" << (g_hotswapMenuP1Palette + 1)
        << "/" << (g_hotswapMenuP2Palette + 1)
        << " custom=" << (g_hotswapMenuP1CustomPalette ? 1 : 0)
        << "/" << (g_hotswapMenuP2CustomPalette ? 1 : 0)
        << " stage=" << g_hotswapMenuStage
        << " ostChoice=" << g_hotswapMenuOstChoice
        << " seeded=" << (g_hotswapMenuSeeded ? 1 : 0);
    LogOut(oss.str(), true);
}

void LogHotswapRuntimeStateIfChanged(const HotswapCurrentState& state) {
    static bool s_hasLogged = false;
    static HotswapCurrentState s_lastState{};
    if (s_hasLogged && HotswapCurrentStatesEqual(state, s_lastState)) {
        return;
    }

    std::ostringstream oss;
    oss << "[HOTSWAP][MENU] runtime charsValid=" << (state.charsValid ? 1 : 0)
        << " p1Char=" << state.p1SelectId
        << " p2Char=" << state.p2SelectId
        << " paletteValid=" << (state.paletteValid ? 1 : 0)
        << " palette=" << (state.paletteSelection.p1Color + 1)
        << "/" << (state.paletteSelection.p2Color + 1)
        << " custom=" << (state.paletteSelection.p1UseCustomPalette ? 1 : 0)
        << "/" << (state.paletteSelection.p2UseCustomPalette ? 1 : 0)
        << " stageValid=" << (state.stageValid ? 1 : 0)
        << " stage=" << state.stageId
        << " bgmValid=" << (state.bgmValid ? 1 : 0)
        << " bgm=" << state.bgmTrack;
    LogOut(oss.str(), true);

    s_lastState = state;
    s_hasLogged = true;
}

void LogCustomPaletteAvailabilityIfChanged(const char* playerLabel,
                                          HotswapCustomAvailabilityLogState& state,
                                          int selectId,
                                          int palette,
                                          bool available,
                                          bool disabled,
                                          const char* reason) {
    const int availableInt = available ? 1 : 0;
    const int disabledInt = disabled ? 1 : 0;
    if (state.selectId == selectId
        && state.palette == palette
        && state.available == availableInt
        && state.disabled == disabledInt) {
        return;
    }

    std::ostringstream oss;
    oss << "[HOTSWAP][MENU] custom availability " << playerLabel
        << " char=" << selectId
        << " palette=" << (palette + 1)
        << " available=" << availableInt
        << " disabled=" << disabledInt;
    if (reason && reason[0] != '\0') {
        oss << " reason=" << reason;
    }
    LogOut(oss.str(), true);

    state.selectId = selectId;
    state.palette = palette;
    state.available = availableInt;
    state.disabled = disabledInt;
}

void ResetHotswapMenuSeedState() {
    const bool wasSeeded = g_hotswapMenuSeeded;
    g_hotswapMenuSeeded = false;
    g_hotswapP1CustomAvailabilityLog = HotswapCustomAvailabilityLogState{};
    g_hotswapP2CustomAvailabilityLog = HotswapCustomAvailabilityLogState{};
    if (wasSeeded) {
        LogOut("[HOTSWAP][MENU] reset seed for next open", true);
    }
}

int NormalizePaletteChoiceIndex(int paletteIndex) {
    if (paletteIndex < 0) {
        paletteIndex %= kHotswapPaletteChoiceCount;
        paletteIndex += kHotswapPaletteChoiceCount;
    }
    if (paletteIndex >= kHotswapPaletteChoiceCount) {
        paletteIndex %= kHotswapPaletteChoiceCount;
    }
    return paletteIndex;
}

int InferPaletteCycleDirection(int previousPalette, int currentPalette) {
    previousPalette = NormalizePaletteChoiceIndex(previousPalette);
    currentPalette = NormalizePaletteChoiceIndex(currentPalette);
    if (currentPalette == previousPalette) {
        return 1;
    }
    if (currentPalette == (previousPalette + 1) % kHotswapPaletteChoiceCount) {
        return 1;
    }
    if (currentPalette == (previousPalette + kHotswapPaletteChoiceCount - 1) % kHotswapPaletteChoiceCount) {
        return -1;
    }
    return (currentPalette > previousPalette) ? 1 : -1;
}

bool FindCustomPaletteSlotForCharacter(int selectId, int startPalette, int direction, int& outPalette) {
    if (direction < 0) {
        direction = -1;
    } else {
        direction = 1;
    }

    int palette = NormalizePaletteChoiceIndex(startPalette);
    for (int i = 0; i < kHotswapPaletteChoiceCount; ++i) {
        if (CharacterHotswap::HasCustomPaletteFile(selectId, palette)) {
            outPalette = palette;
            return true;
        }
        palette = NormalizePaletteChoiceIndex(palette + direction);
    }
    return false;
}

bool CharacterHasAnyCustomPaletteSlot(int selectId) {
    int palette = 0;
    return FindCustomPaletteSlotForCharacter(selectId, 0, 1, palette);
}

void NormalizeMenuPaletteSelection(const char* playerLabel,
                                   int selectId,
                                   int& palette,
                                   bool& useCustomPalette,
                                   int& previousPalette) {
    const int originalPalette = palette;
    const bool originalUseCustomPalette = useCustomPalette;
    const int originalPreviousPalette = previousPalette;
    palette = NormalizePaletteChoiceIndex(palette);
    previousPalette = NormalizePaletteChoiceIndex(previousPalette);

    if (useCustomPalette && !CharacterHotswap::HasCustomPaletteFile(selectId, palette)) {
        const int direction = InferPaletteCycleDirection(previousPalette, palette);
        int resolvedPalette = palette;
        if (!FindCustomPaletteSlotForCharacter(selectId, palette, direction, resolvedPalette)) {
            useCustomPalette = false;
        } else {
            palette = resolvedPalette;
        }
    }

    previousPalette = palette;

    if (originalPalette != palette
        || originalUseCustomPalette != useCustomPalette
        || originalPreviousPalette != previousPalette) {
        std::ostringstream oss;
        oss << "[HOTSWAP][MENU] normalize " << playerLabel
            << " char=" << selectId
            << " beforePalette=" << (NormalizePaletteChoiceIndex(originalPalette) + 1)
            << " beforeCustom=" << (originalUseCustomPalette ? 1 : 0)
            << " afterPalette=" << (palette + 1)
            << " afterCustom=" << (useCustomPalette ? 1 : 0)
            << " prev=" << (NormalizePaletteChoiceIndex(originalPreviousPalette) + 1)
            << " nextPrev=" << (previousPalette + 1);
        LogOut(oss.str(), true);
    }
}

void NormalizeHotswapMenuPaletteSelections() {
    const int p1SelectId = CharacterSelectIdForHotswapChoice(g_hotswapMenuP1Character);
    const int p2SelectId = CharacterSelectIdForHotswapChoice(g_hotswapMenuP2Character);
    NormalizeMenuPaletteSelection("P1",
                                  p1SelectId,
                                  g_hotswapMenuP1Palette,
                                  g_hotswapMenuP1CustomPalette,
                                  g_hotswapMenuP1PalettePrev);
    NormalizeMenuPaletteSelection("P2",
                                  p2SelectId,
                                  g_hotswapMenuP2Palette,
                                  g_hotswapMenuP2CustomPalette,
                                  g_hotswapMenuP2PalettePrev);
}

void OnHotswapP1CharacterChanged() {
    NormalizeHotswapMenuPaletteSelections();
    LogHotswapMenuSelection("P1 character changed");
}

void OnHotswapP2CharacterChanged() {
    NormalizeHotswapMenuPaletteSelections();
    LogHotswapMenuSelection("P2 character changed");
}

void OnHotswapP1PaletteChanged() {
    NormalizeHotswapMenuPaletteSelections();
    LogHotswapMenuSelection("P1 palette changed");
}

void OnHotswapP2PaletteChanged() {
    NormalizeHotswapMenuPaletteSelections();
    LogHotswapMenuSelection("P2 palette changed");
}

void OnHotswapP1CustomPaletteChanged() {
    NormalizeHotswapMenuPaletteSelections();
    LogHotswapMenuSelection("P1 custom toggled");
}

void OnHotswapP2CustomPaletteChanged() {
    NormalizeHotswapMenuPaletteSelections();
    LogHotswapMenuSelection("P2 custom toggled");
}

int CharacterSelectIdForHotswapChoice(int choiceIdx) {
    if (choiceIdx < 0 || choiceIdx >= kHotswapCharacterChoiceCount) {
        return 4;
    }
    return kHotswapCharacterSelectIds[choiceIdx];
}

int HotswapChoiceIndexFromCharacterSelectId(int selectId) {
    if (selectId == 22) {
        selectId = 10;
    }
    for (int i = 0; i < kHotswapCharacterChoiceCount; ++i) {
        if (kHotswapCharacterSelectIds[i] == selectId) {
            return i;
        }
    }
    return 4;
}

bool ReadCurrentCharacterSelectIds(HotswapCurrentState& state) {
    const uintptr_t base = GetEFZBase();
    if (!base) {
        return false;
    }

    uintptr_t screenContext = 0;
    const uintptr_t slot = base + kHotswapScreenTableRva + 4u * static_cast<uintptr_t>(kHotswapCharacterSelectScreen);
    if (!SafeReadMemory(slot, &screenContext, sizeof(screenContext)) || !screenContext) {
        return false;
    }

    uint8_t p1SelectId = 0;
    uint8_t p2SelectId = 0;
    if (!SafeReadMemory(screenContext + kHotswapCsP1SelectionOffset, &p1SelectId, sizeof(p1SelectId))) {
        return false;
    }
    if (!SafeReadMemory(screenContext + kHotswapCsP2SelectionOffset, &p2SelectId, sizeof(p2SelectId))) {
        return false;
    }

    state.p1SelectId = static_cast<int>(p1SelectId);
    state.p2SelectId = static_cast<int>(p2SelectId);
    state.charsValid = true;
    return true;
}

bool ReadCurrentHotswapState(HotswapCurrentState& state) {
    state = HotswapCurrentState{};

    const GamePhase phase = GetCurrentGamePhase();
    if (phase == GamePhase::Match) {
        const auto& d = ImGuiGui::guiState.localData;
        state.p1SelectId = HotswapChoiceIndexFromCharacterSelectId(CharacterSelectIdFromInternalCharacterId(d.p1CharID));
        state.p2SelectId = HotswapChoiceIndexFromCharacterSelectId(CharacterSelectIdFromInternalCharacterId(d.p2CharID));
        state.p1SelectId = CharacterSelectIdForHotswapChoice(state.p1SelectId);
        state.p2SelectId = CharacterSelectIdForHotswapChoice(state.p2SelectId);
        state.charsValid = true;
    } else if (phase == GamePhase::CharacterSelect) {
        ReadCurrentCharacterSelectIds(state);
    }

    CharacterHotswap::PaletteSelection paletteSelection{};
    if (CharacterHotswap::ReadCurrentPaletteSelection(paletteSelection)) {
        state.paletteSelection = paletteSelection;
        state.paletteValid = true;
    }

    const uintptr_t gameStatePtr = GetGameStatePtr();
    if (gameStatePtr) {
        uint8_t currentStage = 0;
        if (SafeReadMemory(gameStatePtr + 3890, &currentStage, sizeof(currentStage))) {
            state.stageId = static_cast<int>(currentStage);
            state.stageValid = state.stageId >= 0 && state.stageId < kNamedStageChoiceCount;
        }
    }
    const unsigned short observedTrack = GetLastBgmTrack();
    state.bgmValid = observedTrack != 0xFFFFu;
    if (state.bgmValid) state.bgmTrack = static_cast<int>(observedTrack);

    LogHotswapRuntimeStateIfChanged(state);

    return state.charsValid || state.stageValid || state.bgmValid;
}

bool ReadCurrentHotswapStateCached(HotswapCurrentState& state, DWORD maxAgeMs = 100) {
    static bool s_haveCached = false;
    static bool s_cachedOk = false;
    static DWORD s_cachedTick = 0;
    static HotswapCurrentState s_cachedState{};

    const DWORD now = GetTickCount();
    if (s_haveCached && (now - s_cachedTick) < maxAgeMs) {
        state = s_cachedState;
        return s_cachedOk;
    }

    const DWORD start = now;
    HotswapCurrentState fresh{};
    const bool ok = ReadCurrentHotswapState(fresh);
    const DWORD elapsed = GetTickCount() - start;

    s_cachedState = fresh;
    s_cachedOk = ok;
    s_cachedTick = GetTickCount();
    s_haveCached = true;
    state = fresh;

    static DWORD s_lastSlowLog = 0;
    if (elapsed >= 25 && (s_lastSlowLog == 0 || (now - s_lastSlowLog) >= 1000)) {
        s_lastSlowLog = now;
        char buf[192];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "[CUSTOM_MENU][TIMING] ReadCurrentHotswapState took %lums ok=%d",
            static_cast<unsigned long>(elapsed),
            ok ? 1 : 0);
        LogOut(buf, true);
    }

    return ok;
}

void UpdateCustomSavestateHotswapPromptFromSummary(const CustomSavestate::Summary& summary) {
    g_customSavestateHotswapPrompt = false;

    if (g_customSavestateHotswapDismissed) {
        LogSavestateHotswapPromptStateIfChanged("dismissed", false, nullptr, nullptr, -1, -1);
        return;
    }
    if (!summary.hasWorkingSnapshot) {
        LogSavestateHotswapPromptStateIfChanged("no-working-snapshot", false, &summary, nullptr, -1, -1);
        return;
    }
    if (summary.savedStageId == 0xFF) {
        LogSavestateHotswapPromptStateIfChanged("saved-stage-unset", false, &summary, nullptr, -1, -1);
        return;
    }

    const int savedP1SelectId = SavedSavestateSelectId(summary.savedP1CharId);
    const int savedP2SelectId = SavedSavestateSelectId(summary.savedP2CharId);
    if (savedP1SelectId < 0 || savedP2SelectId < 0) {
        LogSavestateHotswapPromptStateIfChanged("invalid-char-map", false, &summary, nullptr, savedP1SelectId, savedP2SelectId);
        return;
    }

    HotswapCurrentState current{};
    if (!ReadCurrentHotswapStateCached(current) || !current.charsValid || !current.stageValid) {
        LogSavestateHotswapPromptStateIfChanged("current-unavailable", false, &summary, &current, savedP1SelectId, savedP2SelectId);
        return;
    }

    const bool mismatch = current.p1SelectId != savedP1SelectId
        || current.p2SelectId != savedP2SelectId
        || current.stageId != summary.savedStageId;
    if (!mismatch) {
        LogSavestateHotswapPromptStateIfChanged("already-matching", false, &summary, &current, savedP1SelectId, savedP2SelectId);
        return;
    }

    const std::string p1Name = SavedSavestateDisplayName(summary.savedP1CharId);
    const std::string p2Name = SavedSavestateDisplayName(summary.savedP2CharId);
    const char* stageName = GetNamedStageLabel(summary.savedStageId);
    _snprintf_s(g_customSavestateHotswapInfo,
                sizeof(g_customSavestateHotswapInfo),
                _TRUNCATE,
                "Loaded slot differs from current match. Hotswap to %s / %s on %s?",
                p1Name.c_str(),
                p2Name.c_str(),
                stageName);
    g_customSavestateHotswapPrompt = true;
    LogSavestateHotswapPromptStateIfChanged("prompt-visible", true, &summary, &current, savedP1SelectId, savedP2SelectId);
}

void UpdateCustomSavestateHotswapPromptFromWorking() {
    if (g_customSavestateHotswapDismissed) {
        g_customSavestateHotswapPrompt = false;
        LogSavestateHotswapPromptStateIfChanged("dismissed", false, nullptr, nullptr, -1, -1);
        return;
    }

    CustomSavestate::Summary summary{};
    if (!CustomSavestate::GetSummary(summary)) {
        g_customSavestateHotswapPrompt = false;
        LogSavestateHotswapPromptStateIfChanged("summary-unavailable", false, nullptr, nullptr, -1, -1);
        return;
    }
    UpdateCustomSavestateHotswapPromptFromSummary(summary);
}

bool RevivalBgmMuted() {
    constexpr DWORD kRefreshMs = 2000;
    static bool s_haveCached = false;
    static bool s_cachedMuted = false;
    static bool s_cachedModulePresent = false;
    static bool s_cachedPathOk = false;
    static DWORD s_cachedTick = 0;

    const DWORD now = GetTickCount();
    if (s_haveCached && (now - s_cachedTick) < kRefreshMs) {
        return s_cachedMuted;
    }

    const DWORD start = now;
    bool modulePresent = false;
    bool pathOk = false;
    bool muted = false;

    HMODULE revivalModule = GetModuleHandleA("EfzRevival.dll");
    if (!revivalModule) {
        modulePresent = false;
    } else {
        modulePresent = true;
        char modulePath[MAX_PATH] = {0};
        if (GetModuleFileNameA(revivalModule, modulePath, MAX_PATH)) {
            std::string iniPath(modulePath);
            const size_t slash = iniPath.find_last_of("\\/");
            if (slash != std::string::npos) {
                iniPath.resize(slash + 1);
                iniPath += "EfzRevival.ini";
                muted = GetPrivateProfileIntA("Global Settings", "MuteBGM", 0, iniPath.c_str()) != 0;
                pathOk = true;
            }
        }
    }

    const DWORD elapsed = GetTickCount() - start;
    const bool changed = !s_haveCached ||
        s_cachedMuted != muted ||
        s_cachedModulePresent != modulePresent ||
        s_cachedPathOk != pathOk;

    s_haveCached = true;
    s_cachedMuted = muted;
    s_cachedModulePresent = modulePresent;
    s_cachedPathOk = pathOk;
    s_cachedTick = GetTickCount();

    if (changed) {
        char buf[192];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "[HOTSWAP][MENU] Revival BGM mute cache module=%d path=%d muted=%d",
            modulePresent ? 1 : 0,
            pathOk ? 1 : 0,
            muted ? 1 : 0);
        LogOut(buf, true);
    }

    static DWORD s_lastSlowLog = 0;
    if (elapsed >= 25 && (s_lastSlowLog == 0 || (now - s_lastSlowLog) >= 1000)) {
        s_lastSlowLog = now;
        char buf[192];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "[CUSTOM_MENU][TIMING] RevivalBgmMuted poll took %lums module=%d path=%d",
            static_cast<unsigned long>(elapsed),
            modulePresent ? 1 : 0,
            pathOk ? 1 : 0);
        LogOut(buf, true);
    }

    return muted;
}

bool HotswapOstValueDisabled() {
    return CharacterHotswap::IsBusy();
}

bool HotswapHasReloadChanges(const HotswapCurrentState& current) {
    if (!current.charsValid || !current.stageValid) {
        return false;
    }

    return CharacterSelectIdForHotswapChoice(g_hotswapMenuP1Character) != current.p1SelectId
        || CharacterSelectIdForHotswapChoice(g_hotswapMenuP2Character) != current.p2SelectId
        || (current.paletteValid && g_hotswapMenuP1Palette != current.paletteSelection.p1Color)
        || (current.paletteValid && g_hotswapMenuP2Palette != current.paletteSelection.p2Color)
        || (current.paletteValid && g_hotswapMenuP1CustomPalette != current.paletteSelection.p1UseCustomPalette)
        || (current.paletteValid && g_hotswapMenuP2CustomPalette != current.paletteSelection.p2UseCustomPalette)
        || g_hotswapMenuStage != current.stageId;
}

bool HotswapHasOstSelectionChange(const HotswapCurrentState& current) {
    if (!current.bgmValid) {
        return false;
    }
    return static_cast<int>(TrackForNamedOstChoice(g_hotswapMenuOstChoice)) != current.bgmTrack;
}

bool HotswapHasOstChangeForAction(const HotswapCurrentState& current, bool revivalBgmMuted) {
    if (revivalBgmMuted) {
        return false;
    }
    return HotswapHasOstSelectionChange(current);
}

const char* ValHotswapApply() {
    if (CharacterHotswap::IsBusy()) {
        return CharacterHotswap::GetActionValueText();
    }

    HotswapCurrentState current{};
    ReadCurrentHotswapStateCached(current);
    const bool reloadChanged = HotswapHasReloadChanges(current);
    const bool ostChanged = HotswapHasOstSelectionChange(current);

    if (!reloadChanged && !ostChanged) {
        return "NO CHANGES";
    }
    if (reloadChanged) {
        return CharacterHotswap::CanQueueReload() ? "READY" : "MATCH/CS ONLY";
    }
    return current.bgmValid ? "OST ONLY" : "UNAVAILABLE";
}

void LogReadCurrentHotswapStateSeh(unsigned code) {
    char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "[CUSTOM_MENU][TRACE] SEH 0x%08X in ReadCurrentHotswapState",
        code);
    LogOut(buf, true);
}

static bool SehReadCurrentHotswapState(HotswapCurrentState* outState) {
    __try {
        return ReadCurrentHotswapState(*outState);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogReadCurrentHotswapStateSeh((unsigned)GetExceptionCode());
        if (outState) {
            memset(outState, 0, sizeof(*outState));
        }
        return false;
    }
}

void SeedHotswapMenuSelectionsIfNeeded() {
    RefreshNamedOstChoices();
    if (g_hotswapMenuSeeded) {
        return;
    }

    LogOut("[CUSTOM_MENU][TRACE] SeedHotswapMenuSelectionsIfNeeded: begin", true);
    HotswapCurrentState current{};
    const bool readOk = SehReadCurrentHotswapState(&current);

    if (readOk) {
        if (current.charsValid) {
            g_hotswapMenuP1Character = HotswapChoiceIndexFromCharacterSelectId(current.p1SelectId);
            g_hotswapMenuP2Character = HotswapChoiceIndexFromCharacterSelectId(current.p2SelectId);
        }
        if (current.paletteValid) {
            g_hotswapMenuP1Palette = NormalizePaletteChoiceIndex(current.paletteSelection.p1Color);
            g_hotswapMenuP2Palette = NormalizePaletteChoiceIndex(current.paletteSelection.p2Color);
            g_hotswapMenuP1CustomPalette = current.paletteSelection.p1UseCustomPalette;
            g_hotswapMenuP2CustomPalette = current.paletteSelection.p2UseCustomPalette;
        }
        if (current.stageValid) {
            g_hotswapMenuStage = current.stageId;
        }
        if (current.bgmValid) {
            const int ostChoice = FindNamedOstChoiceIndexByTrack(current.bgmTrack);
            if (ostChoice >= 0) {
                g_hotswapMenuOstChoice = ostChoice;
            } else {
                const int stageTrackChoice = FindNamedOstChoiceIndexByTrack(10 + g_hotswapMenuStage);
                g_hotswapMenuOstChoice = (stageTrackChoice >= 0) ? stageTrackChoice : 0;
            }
        }
    } else {
        const auto& d = ImGuiGui::guiState.localData;
        if (d.p1CharID >= CHAR_ID_AKANE && d.p1CharID <= CHAR_ID_KANO) {
            g_hotswapMenuP1Character = HotswapChoiceIndexFromCharacterSelectId(CharacterSelectIdFromInternalCharacterId(d.p1CharID));
        }
        if (d.p2CharID >= CHAR_ID_AKANE && d.p2CharID <= CHAR_ID_KANO) {
            g_hotswapMenuP2Character = HotswapChoiceIndexFromCharacterSelectId(CharacterSelectIdFromInternalCharacterId(d.p2CharID));
        }
    }

    g_hotswapMenuP1PalettePrev = NormalizePaletteChoiceIndex(g_hotswapMenuP1Palette);
    g_hotswapMenuP2PalettePrev = NormalizePaletteChoiceIndex(g_hotswapMenuP2Palette);
    NormalizeHotswapMenuPaletteSelections();

    g_hotswapMenuSeeded = true;
    LogHotswapMenuSelection(current.charsValid || current.paletteValid || current.stageValid || current.bgmValid
        ? "seeded from runtime"
        : "seeded from fallback local data");
}

void RunMenuHotswapApply() {
    if (CharacterHotswap::IsBusy()) {
        return;
    }

    HotswapCurrentState current{};
    ReadCurrentHotswapState(current);

    const int p1SelectId = CharacterSelectIdForHotswapChoice(g_hotswapMenuP1Character);
    const int p2SelectId = CharacterSelectIdForHotswapChoice(g_hotswapMenuP2Character);
    const bool reloadChanged = HotswapHasReloadChanges(current);
    unsigned short targetTrack = TrackForNamedOstChoice(g_hotswapMenuOstChoice);
    const bool revivalBgmMuted = RevivalBgmMuted();
    bool ostChanged = HotswapHasOstChangeForAction(current, revivalBgmMuted);

    if (revivalBgmMuted) {
        if (current.bgmValid) {
            targetTrack = static_cast<unsigned short>(current.bgmTrack);
        }
        if (HotswapHasOstSelectionChange(current)) {
            LogOut("[HOTSWAP] OST request ignored because Revival MuteBGM is enabled", true);
        }
    }

    if (!reloadChanged && !ostChanged) {
        LogOut("[HOTSWAP] apply ignored because menu selections match current runtime state", true);
        return;
    }

    if (!reloadChanged) {
        const uintptr_t gameStatePtr = GetGameStatePtr();
        if (!gameStatePtr) {
            LogOut("[HOTSWAP] OST-only apply failed because game state pointer was unavailable", true);
            return;
        }
        if (PlayBGM(gameStatePtr, targetTrack)) {
            LogOut("[HOTSWAP] applied OST without reload track=" + std::to_string(targetTrack), true);
        } else {
            LogOut("[HOTSWAP] failed to apply OST without reload track=" + std::to_string(targetTrack), true);
        }
        return;
    }

    CharacterHotswap::PaletteSelection paletteSelection{};
    paletteSelection.p1Color = g_hotswapMenuP1Palette;
    paletteSelection.p2Color = g_hotswapMenuP2Palette;
    paletteSelection.p1UseCustomPalette = g_hotswapMenuP1CustomPalette;
    paletteSelection.p2UseCustomPalette = g_hotswapMenuP2CustomPalette;
    {
        std::ostringstream oss;
        oss << "[HOTSWAP][MENU] apply request"
            << " runtimePaletteValid=" << (current.paletteValid ? 1 : 0)
            << " runtimePalette=" << (current.paletteSelection.p1Color + 1)
            << "/" << (current.paletteSelection.p2Color + 1)
            << " runtimeCustom=" << (current.paletteSelection.p1UseCustomPalette ? 1 : 0)
            << "/" << (current.paletteSelection.p2UseCustomPalette ? 1 : 0)
            << " requestedPalette=" << (paletteSelection.p1Color + 1)
            << "/" << (paletteSelection.p2Color + 1)
            << " requestedCustom=" << (paletteSelection.p1UseCustomPalette ? 1 : 0)
            << "/" << (paletteSelection.p2UseCustomPalette ? 1 : 0);
        LogOut(oss.str(), true);
    }
    CharacterHotswap::SanitizePaletteSelection(p1SelectId, p2SelectId, paletteSelection);
    {
        std::ostringstream oss;
        oss << "[HOTSWAP][MENU] apply sanitized"
            << " palette=" << (paletteSelection.p1Color + 1)
            << "/" << (paletteSelection.p2Color + 1)
            << " custom=" << (paletteSelection.p1UseCustomPalette ? 1 : 0)
            << "/" << (paletteSelection.p2UseCustomPalette ? 1 : 0);
        LogOut(oss.str(), true);
    }

    CharacterHotswap::QueueReload(p1SelectId,
                                  p2SelectId,
                                  g_hotswapMenuStage,
                                  paletteSelection,
                                  targetTrack);
}

bool ExitToCharacterSelectDisabled() {
    return !CanRequestFrontendExit(FrontendExitTarget::CharacterSelect);
}

bool ExitToTitleDisabled() {
    return !CanRequestFrontendExit(FrontendExitTarget::Title);
}

bool HotswapSelectionValueDisabled() {
    return CharacterHotswap::IsBusy();
}

bool HotswapP1CustomPaletteDisabled() {
    if (CharacterHotswap::IsBusy()) {
        LogCustomPaletteAvailabilityIfChanged("P1",
                                             g_hotswapP1CustomAvailabilityLog,
                                             CharacterSelectIdForHotswapChoice(g_hotswapMenuP1Character),
                                             g_hotswapMenuP1Palette,
                                             false,
                                             true,
                                             "busy");
        return true;
    }
    const int selectId = CharacterSelectIdForHotswapChoice(g_hotswapMenuP1Character);
    const bool available = CharacterHotswap::HasCustomPaletteFile(selectId, g_hotswapMenuP1Palette);
    const bool disabled = !available;
    LogCustomPaletteAvailabilityIfChanged("P1",
                                         g_hotswapP1CustomAvailabilityLog,
                                         selectId,
                                         g_hotswapMenuP1Palette,
                                         available,
                                         disabled,
                                         disabled ? "missing custom .pal" : "available");
    return disabled;
}

bool HotswapP2CustomPaletteDisabled() {
    if (CharacterHotswap::IsBusy()) {
        LogCustomPaletteAvailabilityIfChanged("P2",
                                             g_hotswapP2CustomAvailabilityLog,
                                             CharacterSelectIdForHotswapChoice(g_hotswapMenuP2Character),
                                             g_hotswapMenuP2Palette,
                                             false,
                                             true,
                                             "busy");
        return true;
    }
    const int selectId = CharacterSelectIdForHotswapChoice(g_hotswapMenuP2Character);
    const bool available = CharacterHotswap::HasCustomPaletteFile(selectId, g_hotswapMenuP2Palette);
    const bool disabled = !available;
    LogCustomPaletteAvailabilityIfChanged("P2",
                                         g_hotswapP2CustomAvailabilityLog,
                                         selectId,
                                         g_hotswapMenuP2Palette,
                                         available,
                                         disabled,
                                         disabled ? "missing custom .pal" : "available");
    return disabled;
}

bool HotswapReloadDisabled() {
    if (CharacterHotswap::IsBusy()) {
        return false;
    }

    HotswapCurrentState current{};
    ReadCurrentHotswapStateCached(current);
    const bool reloadChanged = HotswapHasReloadChanges(current);
    const bool ostChanged = HotswapHasOstSelectionChange(current);

    if (!reloadChanged && !ostChanged) {
        return true;
    }
    if (reloadChanged) {
        return !CharacterHotswap::CanQueueReload();
    }
    return !current.bgmValid;
}

const char* ValExitToCharacterSelect() {
    return CanRequestFrontendExit(FrontendExitTarget::CharacterSelect) ? "READY" : "MATCH ONLY";
}

const char* ValExitToTitle() {
    return CanRequestFrontendExit(FrontendExitTarget::Title) ? "READY" : "UNAVAILABLE";
}

Row* BuildHotswapOptionsRows(int& count);
Row* BuildSettingsAudioRows(int& count);
Row* BuildHelpMacroRows(int& count);

void QueueMenuNav(int pane, int focusRow, RowListBuilder sub, const char* subTitle, int subFocus) {
    Screens::MenuNavigationRequest req{};
    req.pane = pane;
    req.focusRow = focusRow;
    req.submenuBuilder = sub;
    req.submenuTitle = subTitle;
    req.submenuFocusRow = subFocus;
    Screens::RequestMenuNavigation(req);
}

void NavToHotswap() {
    QueueMenuNav(Screens::MenuPane::Options, 0, BuildHotswapOptionsRows, "MATCH HOTSWAP", 2);
}
void NavToChars() {
    QueueMenuNav(Screens::MenuPane::Chars, 0, nullptr, nullptr, 0);
}
void NavToHelp() {
    QueueMenuNav(Screens::MenuPane::HelpStart, 0, nullptr, nullptr, 0);
}
void NavToAbout() {
    QueueMenuNav(Screens::MenuPane::HelpAbout, 0, nullptr, nullptr, 0);
}
void NavToMacroHelp() {
    QueueMenuNav(Screens::MenuPane::HelpGuide, 0, BuildHelpMacroRows, "MACROS", 1);
}
void NavToSound() {
    QueueMenuNav(Screens::MenuPane::SettingsGeneral, 0, BuildSettingsAudioRows, "AUDIO", 1);
}

Row* BuildMenuRows(int& count) {
    static Row s_rows[16];
    int n = 0;

    s_rows[n++] = Header("SHORTCUTS");
    s_rows[n++] = Action("CHANGE CHARACTERS / STAGE", NavToHotswap, ValHotswapApply);
    s_rows[n++] = Action("CHARACTER SETTINGS", NavToChars);
    s_rows[n++] = WithHelp(Action("HELP", NavToHelp),
                           "Opens help pages with setup notes and troubleshooting.");
    s_rows[n++] = Action("ABOUT", NavToAbout, ValUpdateBadge);
    s_rows[n++] = Action("SOUND SETTINGS", NavToSound, ValAudioSettings);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("EXIT");
    s_rows[n++] = Action("EXIT TO CHARACTER SELECT", RunExitToCharacterSelect, ValExitToCharacterSelect, ExitToCharacterSelectDisabled);
    s_rows[n++] = Action("EXIT TO TITLE SCREEN",     RunExitToTitle,           ValExitToTitle,           ExitToTitleDisabled);

    count = n;
    return s_rows;
}

Row* BuildHotswapOptionsRows(int& count) {
    static Row s_rows[20];
    int n = 0;

    SeedHotswapMenuSelectionsIfNeeded();

    s_rows[n++] = Header("MATCH HOTSWAP");
    s_rows[n++] = Info("Change characters, palettes, stage, or music without leaving Practice.");
    s_rows[n++] = DropdownRow("PLAYER 1 CHARACTER", &g_hotswapMenuP1Character,
                              kHotswapCharacterChoices,
                              kHotswapCharacterChoiceCount,
                              OnHotswapP1CharacterChanged, HotswapSelectionValueDisabled);
    s_rows[n++] = DropdownRow("PLAYER 2 CHARACTER", &g_hotswapMenuP2Character,
                              kHotswapCharacterChoices,
                              kHotswapCharacterChoiceCount,
                              OnHotswapP2CharacterChanged, HotswapSelectionValueDisabled);
    s_rows[n++] = ChoicesRow("PLAYER 1 PALETTE", &g_hotswapMenuP1Palette,
                             kHotswapPaletteChoices,
                             kHotswapPaletteChoiceCount,
                             OnHotswapP1PaletteChanged,
                             HotswapSelectionValueDisabled);
    s_rows[n++] = Toggle("PLAYER 1 CUSTOM PALETTE", &g_hotswapMenuP1CustomPalette,
                         OnHotswapP1CustomPaletteChanged,
                         HotswapP1CustomPaletteDisabled,
                         nullptr,
                         true);
    s_rows[n++] = ChoicesRow("PLAYER 2 PALETTE", &g_hotswapMenuP2Palette,
                             kHotswapPaletteChoices,
                             kHotswapPaletteChoiceCount,
                             OnHotswapP2PaletteChanged,
                             HotswapSelectionValueDisabled);
    s_rows[n++] = Toggle("PLAYER 2 CUSTOM PALETTE", &g_hotswapMenuP2CustomPalette,
                         OnHotswapP2CustomPaletteChanged,
                         HotswapP2CustomPaletteDisabled,
                         nullptr,
                         true);
    s_rows[n++] = DropdownRow("STAGE", &g_hotswapMenuStage,
                              kNamedStageChoices,
                              kNamedStageChoiceCount,
                              nullptr, HotswapSelectionValueDisabled);
    s_rows[n++] = DropdownRow("OST", &g_hotswapMenuOstChoice,
                              kNamedOstChoices,
                              kNamedOstChoiceCount,
                              nullptr, HotswapOstValueDisabled);
    s_rows[n++] = Action("APPLY SELECTIONS", RunMenuHotswapApply,
                         ValHotswapApply,
                         HotswapReloadDisabled);

    count = n;
    return s_rows;
}

// OPTIONS screen
void OnFaOverlayPersist()  { g_showFrameAdvantageOverlay.store(g_mirrorFaOverlay); }
void OnFrameBarPersist() {
    PersistBool("General", "showFrameBar", MutableSettings().showFrameBar);
    FrameBar::g_enabled.store(MutableSettings().showFrameBar);
    FrameBar::Reset();
}
void OnFrameBarTiming() {
    PersistInt("General", "frameBarTimingMode", MutableSettings().frameBarTimingMode);
    FrameBar::Reset();
}
void OnFrameBarDetail() {
    PersistInt("General", "frameBarDetailMode", MutableSettings().frameBarDetailMode);
}
bool FrameBarOptionsHidden() { return !MutableSettings().showFrameBar; }

// Continuous Recovery mirrors
const char* const kCrHpModeChoices[]    = { "OFF", "MAX", "FM (3332)", "CUSTOM" };
const char* const kCrMeterModeChoices[] = { "OFF", "0", "1000", "2000", "3000", "CUSTOM" };
const char* const kCrRfModeChoices[]    = { "OFF", "ZERO", "FULL (1000)", "RED (500)", "RED MAX (999)", "CUSTOM" };

bool   g_crEnabledP1, g_crEnabledP2;
int    g_crHpModeP1, g_crHpModeP2;
int    g_crHpCustomP1, g_crHpCustomP2;
int    g_crMeterModeP1, g_crMeterModeP2;
int    g_crMeterCustomP1, g_crMeterCustomP2;
int    g_crRfModeP1, g_crRfModeP2;
float  g_crRfCustomP1, g_crRfCustomP2;
bool   g_crForceBlueICP1, g_crForceBlueICP2;

// localData is the CR source of truth: ApplyImGuiSettings copies it into displayData and
// the runtime atomics, so values written anywhere else are reverted by the next apply.
void RefreshCrMirrors() {
    const DisplayData& d = ImGuiGui::guiState.localData;
    g_crEnabledP1     = d.p1ContinuousRecoveryEnabled;
    g_crHpModeP1      = d.p1RecoveryHpMode;
    g_crHpCustomP1    = d.p1RecoveryHpCustom;
    g_crMeterModeP1   = d.p1RecoveryMeterMode;
    g_crMeterCustomP1 = d.p1RecoveryMeterCustom;
    g_crRfModeP1      = d.p1RecoveryRfMode;
    g_crRfCustomP1    = (float)d.p1RecoveryRfCustom;
    g_crForceBlueICP1 = d.p1RecoveryRfForceBlueIC;

    g_crEnabledP2     = d.p2ContinuousRecoveryEnabled;
    g_crHpModeP2      = d.p2RecoveryHpMode;
    g_crHpCustomP2    = d.p2RecoveryHpCustom;
    g_crMeterModeP2   = d.p2RecoveryMeterMode;
    g_crMeterCustomP2 = d.p2RecoveryMeterCustom;
    g_crRfModeP2      = d.p2RecoveryRfMode;
    g_crRfCustomP2    = (float)d.p2RecoveryRfCustom;
    g_crForceBlueICP2 = d.p2RecoveryRfForceBlueIC;
}

DisplayData& CrLocal() { return ImGuiGui::guiState.localData; }

void OnCrEnabledP1() {
    CrLocal().p1ContinuousRecoveryEnabled = g_crEnabledP1;
    if (g_crEnabledP1) WriteEngineRegenParams(0, 0);
    OnAutoApply();
}
void OnCrEnabledP2() {
    CrLocal().p2ContinuousRecoveryEnabled = g_crEnabledP2;
    if (g_crEnabledP2) WriteEngineRegenParams(0, 0);
    OnAutoApply();
}
void OnCrHpModeP1()      { CrLocal().p1RecoveryHpMode = g_crHpModeP1;                   OnAutoApply(); }
void OnCrHpModeP2()      { CrLocal().p2RecoveryHpMode = g_crHpModeP2;                   OnAutoApply(); }
void OnCrHpCustomP1()    { CrLocal().p1RecoveryHpCustom = g_crHpCustomP1;               OnAutoApply(); }
void OnCrHpCustomP2()    { CrLocal().p2RecoveryHpCustom = g_crHpCustomP2;               OnAutoApply(); }
void OnCrMeterModeP1()   { CrLocal().p1RecoveryMeterMode = g_crMeterModeP1;             OnAutoApply(); }
void OnCrMeterModeP2()   { CrLocal().p2RecoveryMeterMode = g_crMeterModeP2;             OnAutoApply(); }
void OnCrMeterCustomP1() { CrLocal().p1RecoveryMeterCustom = g_crMeterCustomP1;         OnAutoApply(); }
void OnCrMeterCustomP2() { CrLocal().p2RecoveryMeterCustom = g_crMeterCustomP2;         OnAutoApply(); }
void OnCrRfCustomP1()    { CrLocal().p1RecoveryRfCustom = (double)g_crRfCustomP1;       OnAutoApply(); }
void OnCrRfCustomP2()    { CrLocal().p2RecoveryRfCustom = (double)g_crRfCustomP2;       OnAutoApply(); }
void OnCrForceBlueICP1() { CrLocal().p1RecoveryRfForceBlueIC = g_crForceBlueICP1;       OnAutoApply(); }
void OnCrForceBlueICP2() { CrLocal().p2RecoveryRfForceBlueIC = g_crForceBlueICP2;       OnAutoApply(); }
void OnCrRfModeP1() {
    CrLocal().p1RecoveryRfMode = g_crRfModeP1;
    if (g_crRfModeP1 == 3 || g_crRfModeP1 == 4) {
        g_crForceBlueICP1 = false;
        CrLocal().p1RecoveryRfForceBlueIC = false;
    }
    OnAutoApply();
}
void OnCrRfModeP2() {
    CrLocal().p2RecoveryRfMode = g_crRfModeP2;
    if (g_crRfModeP2 == 3 || g_crRfModeP2 == 4) {
        g_crForceBlueICP2 = false;
        CrLocal().p2RecoveryRfForceBlueIC = false;
    }
    OnAutoApply();
}

bool CrP1HpCustomHidden()    { return g_crHpModeP1    != 3; }
bool CrP1MeterCustomHidden() { return g_crMeterModeP1 != 5; }
bool CrP1RfCustomHidden()    { return g_crRfModeP1    != 5; }
bool CrP1RfBicHidden()       { return g_crRfModeP1    != 5; }
bool CrP2HpCustomHidden()    { return g_crHpModeP2    != 3; }
bool CrP2MeterCustomHidden() { return g_crMeterModeP2 != 5; }
bool CrP2RfCustomHidden()    { return g_crRfModeP2    != 5; }
bool CrP2RfBicHidden()       { return g_crRfModeP2    != 5; }

// F4 / F5 engine recovery mirrors
const char* const kF5ModeChoices[3] = { "DISABLED", "FULL VALUES", "FM (3332)" };
const char* const kF4ModeChoices[3] = { "DISABLED", "FULL BLUE (1000)", "CUSTOM" };
const char* const kF4ColorChoices[2] = { "RED", "BLUE" };

int  g_f5Mode = 0;   // 0=Disabled, 1=Full, 2=FM
int  g_f4Mode = 0;   // 0=Disabled, 1=Full Blue, 2=Custom
int  g_f4Color = 0;  // 0=Red, 1=Blue (only for custom)
int  g_f4RfAmount = 1000;

void RefreshEngineRegenMirrors() {
    uint16_t a = 0, b = 0;
    EngineRegenMode regenMode = EngineRegenMode::Unknown;
    if (!GetEngineRegenStatus(regenMode, a, b)) return;

    if (regenMode == EngineRegenMode::F5_FullOrPreset) {
        if (b == 3332) {
            g_f5Mode = 2;
            g_f4Mode = 0;
        } else if (a == 1000 && b == 9999 && g_f4Mode == 1) {
            // F4 "Full Blue" uses the same Param A/B as F5 Full - keep UI on F4.
            g_f5Mode = 0;
        } else {
            g_f5Mode = 1;
            g_f4Mode = 0;
        }
        return;
    }

    g_f5Mode = 0;
    if (regenMode == EngineRegenMode::F4_FineTuneActive && b == 9999 && a > 0) {
        if (a == 1000 && g_f4Mode == 1) {
            g_f4Mode = 1;
        } else {
            g_f4Mode = 2;
            float rf = 0.0f; bool blue = false;
            if (DeriveRfFromParamA(a, rf, blue)) {
                g_f4Color = blue ? 1 : 0;
                g_f4RfAmount = (int)rf;
            }
        }
        return;
    }

    if (a == 0 && b == 0) {
        g_f4Mode = 0;
    }
}

void ApplyF5() {
    if (g_f5Mode != 0) {
        g_f4Mode = 0;
    }
    switch (g_f5Mode) {
        case 0: WriteEngineRegenParams(0, 0); break;
        case 1: ForceEngineF5Full(); break;
        case 2: WriteEngineRegenParams(1000, 3332); break;
    }
}
void ApplyF4Custom() {
    if (g_f4Mode != 2) return;
    uint16_t a = (uint16_t)((g_f4Color == 1) ? (2000 - g_f4RfAmount) : g_f4RfAmount);
    WriteEngineRegenParams(a, 9999);
}
void OnF5Mode()    { ApplyF5(); }
void OnF4Mode() {
    if (g_f4Mode != 0) {
        g_f5Mode = 0;
    }
    if (g_f4Mode == 0) WriteEngineRegenParams(0, 0);
    else if (g_f4Mode == 1) WriteEngineRegenParams(1000, 9999);
    else ApplyF4Custom();
}
void OnF4Custom() { if (g_f4Mode == 2) ApplyF4Custom(); }
bool F4DisabledByF5() { return g_f5Mode != 0; }
bool F4CustomHidden() { return g_f4Mode != 2 || F4DisabledByF5(); }

// Framestep
int  g_framestepMode = 0;     // 0=FullFrame, 1=Subframe
void OnFramestepEnabled() {
    PersistBool("General", "framestepEnabled", MutableSettings().framestepEnabled);
}
void OnSuppressRevivalFramestep() {
    PersistBool("General", "suppressRevivalFramestep", MutableSettings().suppressRevivalFramestep);
}
bool FramestepModeHidden() { return !Framestep::IsEnabled(); }
void RefreshFramestepMirror() {
    g_framestepMode = (Framestep::GetStepMode() == Framestep::StepMode::Subframe) ? 1 : 0;
}
void OnFramestepMode() {
    Framestep::SetStepMode(g_framestepMode == 1 ? Framestep::StepMode::Subframe
                                                : Framestep::StepMode::FullFrame);
}
const char* const kFramestepChoices[2] = { "FULL FRAMES", "SUBFRAMES" };
const char* const kComboDetailChoices[2] = { "COMBO STATE", "LAST HIT" };

const char* ValVanillaRegen() {
    if (g_f5Mode != 0) {
        return (g_f5Mode == 2) ? "F5 FM" : "F5 ON";
    }
    if (g_f4Mode == 1) return "F4 FULL BLUE";
    if (g_f4Mode == 2) return "F4 CUSTOM";
    return "OFF";
}

const char* ValContinuousRecovery() {
    const bool p1 = g_crEnabledP1;
    const bool p2 = g_crEnabledP2;
    if (p1 && p2) return "P1 + P2";
    if (p1) return "P1 ON";
    if (p2) return "P2 ON";
    return "OFF";
}

const char* ValPlayerValuesSummary() {
    return "HP / METER / RF / POS";
}

bool ComboStatsHidden() { return !MutableSettings().showComboStatisticsOverlay; }
bool ComboDetailSourceHidden() { return ComboStatsHidden() || !MutableSettings().comboOverlayShowDetailRow; }
bool ComboFinalDurationHidden() { return ComboStatsHidden() || !MutableSettings().comboOverlayShowFinalSummary; }

Row* BuildVanillaRegenRows(int& count) {
    static Row s_rows[12];
    int n = 0;

    s_rows[n++] = Header("VANILLA REGENERATION");
    s_rows[n++] = Info("These options mirror the game's F4/F5 recovery. F4 and F5 cannot run at the same time.");
    s_rows[n++] = ChoicesRow("AUTOMATIC HEALTH AND METER RECOVERY (F4)",
                             &g_f4Mode, kF4ModeChoices, 3, OnF4Mode, F4DisabledByF5);
    s_rows[n++] = ChoicesRow("  COLOR", &g_f4Color, kF4ColorChoices, 2, OnF4Custom,
                             F4DisabledByF5, F4CustomHidden);
    s_rows[n++] = IntNum("  RF AMOUNT", &g_f4RfAmount, 0, 1000, 50, 100, OnF4Custom,
                         F4DisabledByF5, F4CustomHidden);
    s_rows[n++] = ChoicesRow("PRESETS FOR RECOVERY (F5)",
                             &g_f5Mode, kF5ModeChoices, 3, OnF5Mode);

    count = n;
    return s_rows;
}

Row* BuildPlayerValuesPlaceholderRows(int& count) {
    static Row s_rows[1];
    count = 1;
    s_rows[0] = Info("Edit player values in the columns below.");
    return s_rows;
}

Row* BuildContinuousRecoveryPlaceholderRows(int& count) {
    static Row s_rows[1];
    count = 1;
    s_rows[0] = Info("Configure per-player recovery in the columns below.");
    return s_rows;
}

Row* BuildValuesRootRows(int& count) {
    static Row s_rows[24];
    int n = 0;

    s_rows[n++] = Header("VALUES");
    s_rows[n++] = Submenu("PLAYER VALUES", "PLAYER VALUES",
                          BuildPlayerValuesPlaceholderRows, ValPlayerValuesSummary);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("VANILLA REGENERATION");
    s_rows[n++] = ChoicesRow("AUTOMATIC HEALTH AND METER RECOVERY (F4)",
                             &g_f4Mode, kF4ModeChoices, 3, OnF4Mode, F4DisabledByF5);
    s_rows[n++] = ChoicesRow("  COLOR", &g_f4Color, kF4ColorChoices, 2, OnF4Custom,
                             F4DisabledByF5, F4CustomHidden);
    s_rows[n++] = IntNum("  RF AMOUNT", &g_f4RfAmount, 0, 1000, 50, 100, OnF4Custom,
                         F4DisabledByF5, F4CustomHidden);
    s_rows[n++] = ChoicesRow("PRESETS FOR RECOVERY (F5)",
                             &g_f5Mode, kF5ModeChoices, 3, OnF5Mode);

    s_rows[n++] = Spacer();
    s_rows[n++] = Submenu("CONTINUOUS RECOVERY", "CONTINUOUS RECOVERY",
                          BuildContinuousRecoveryPlaceholderRows, ValContinuousRecovery);

    count = n;
    return s_rows;
}

Row* BuildComboStatisticsRows(int& count) {
    static Row s_rows[16];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("COMBO STATISTICS");
    s_rows[n++] = Toggle("ENABLE OVERLAY", &s.showComboStatisticsOverlay, OnShowCombo);
    s_rows[n++] = Toggle("  SHOW DETAIL ROW", &s.comboOverlayShowDetailRow, OnComboDetailRow, nullptr, ComboStatsHidden);
    s_rows[n++] = ChoicesRow("  DETAIL SOURCE", &s.comboOverlayDetailRowSource, kComboDetailChoices, 2,
                             OnComboDetailSrc, nullptr, ComboDetailSourceHidden);
    s_rows[n++] = Toggle("  KEEP FINAL SUMMARY", &s.comboOverlayShowFinalSummary, OnComboFinal, nullptr, ComboStatsHidden);
    s_rows[n++] = FloatNum("  SUMMARY TIME", &s.comboOverlayDisplayDuration, 0.5f, 30.0f, 0.1f, 1.0f, "%.1f",
                           OnComboDuration, nullptr, ComboFinalDurationHidden);
    s_rows[n++] = Toggle("  HIDE WITH MENU", &s.comboOverlayHideWhenImGuiVisible, OnComboHideMenu, nullptr, ComboStatsHidden);
    s_rows[n++] = Toggle("  RESUME AFTER MENU", &s.comboOverlayResumeAfterImGui, OnComboResume, nullptr, ComboStatsHidden);
    s_rows[n++] = Toggle("  SHOW RF MULTIPLIER", &s.comboOverlayShowRfMultiplier, OnComboRfMult, nullptr, ComboStatsHidden);
    s_rows[n++] = Toggle("  SHOW RAW SCALE", &s.comboOverlayShowRawScale, OnComboRawScale, nullptr, ComboStatsHidden);

    count = n;
    return s_rows;
}

const char* ValComboStatistics() {
    return MutableSettings().showComboStatisticsOverlay ? "ON" : "OFF";
}

Row* BuildOptionsRows(int& count) {
    static Row s_rows[40];
    int n = 0;
    auto& s = MutableSettings();

    s_rows[n++] = Header("GAMEPLAY");
    s_rows[n++] = Toggle("FINAL MEMORY AT ANY HP",  &g_mirrorFmBypass,  OnFmBypass);
    s_rows[n++] = Toggle("FRAME ADVANTAGE OVERLAY", &g_mirrorFaOverlay, OnFaOverlayPersist);
    s_rows[n++] = FloatNum("FA DURATION (SEC)", &s.frameAdvantageDisplayDuration,
                            0.5f, 30.0f, 0.1f, 1.0f, "%.1f", OnFADuration);
    s_rows[n++] = Submenu("DISPLAY OVERLAYS", "DISPLAY OVERLAYS", BuildDisplayOverlayRows, ValDisplaySettings);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("FRAME BAR");
    s_rows[n++] = Toggle("SHOW FRAME BAR", &s.showFrameBar, OnFrameBarPersist);
    s_rows[n++] = ChoicesRow("  CELL STEP", &s.frameBarTimingMode, kFrameBarTimingChoices, 2,
                             OnFrameBarTiming, nullptr, FrameBarOptionsHidden);
    s_rows[n++] = ChoicesRow("  DETAIL", &s.frameBarDetailMode, kFrameBarDetailChoices, 3,
                             OnFrameBarDetail, nullptr, FrameBarOptionsHidden);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("FRAMESTEP");
    s_rows[n++] = Toggle("ENABLE FRAMESTEP", &s.framestepEnabled, OnFramestepEnabled);
    s_rows[n++] = Toggle("SUPPRESS REVIVAL STEP", &s.suppressRevivalFramestep, OnSuppressRevivalFramestep);
    s_rows[n++] = ChoicesRow("STEP MODE", &g_framestepMode, kFramestepChoices, 2,
                             OnFramestepMode, nullptr, FramestepModeHidden);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("COMBO STATISTICS");
    s_rows[n++] = Toggle("ENABLE OVERLAY", &s.showComboStatisticsOverlay, OnShowCombo);
    s_rows[n++] = Toggle("  SHOW DETAIL ROW", &s.comboOverlayShowDetailRow, OnComboDetailRow, nullptr, ComboStatsHidden);
    s_rows[n++] = ChoicesRow("  DETAIL SOURCE", &s.comboOverlayDetailRowSource, kComboDetailChoices, 2,
                             OnComboDetailSrc, nullptr, ComboDetailSourceHidden);
    s_rows[n++] = Toggle("  KEEP FINAL SUMMARY", &s.comboOverlayShowFinalSummary, OnComboFinal, nullptr, ComboStatsHidden);
    s_rows[n++] = FloatNum("  SUMMARY TIME", &s.comboOverlayDisplayDuration, 0.5f, 30.0f, 0.1f, 1.0f, "%.1f",
                           OnComboDuration, nullptr, ComboFinalDurationHidden);
    s_rows[n++] = Toggle("  HIDE WITH MENU", &s.comboOverlayHideWhenImGuiVisible, OnComboHideMenu, nullptr, ComboStatsHidden);
    s_rows[n++] = Toggle("  RESUME AFTER MENU", &s.comboOverlayResumeAfterImGui, OnComboResume, nullptr, ComboStatsHidden);
    s_rows[n++] = Toggle("  SHOW RF MULTIPLIER", &s.comboOverlayShowRfMultiplier, OnComboRfMult, nullptr, ComboStatsHidden);
    s_rows[n++] = Toggle("  SHOW RAW SCALE", &s.comboOverlayShowRawScale, OnComboRawScale, nullptr, ComboStatsHidden);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("OPTION MENUS");
    s_rows[n++] = Submenu("HOTSWAP", "MATCH HOTSWAP", BuildHotswapOptionsRows, ValHotswapApply);

    count = n;
    return s_rows;
}

bool CrRowHiddenImpl(int player, int row) {
    if (player == 0) {
        if (row == 2) return CrP1HpCustomHidden();
        if (row == 4) return CrP1MeterCustomHidden();
        if (row == 6) return CrP1RfCustomHidden();
        if (row == 7) return CrP1RfBicHidden();
    } else {
        if (row == 2) return CrP2HpCustomHidden();
        if (row == 4) return CrP2MeterCustomHidden();
        if (row == 6) return CrP2RfCustomHidden();
        if (row == 7) return CrP2RfBicHidden();
    }
    return false;
}

const char* CrRowLabelImpl(int row) {
    static const char* const kLabels[CrEditorRowCount] = {
        "ENABLE", "HP MODE", "HP CUSTOM", "METER MODE", "METER CUSTOM",
        "RF MODE", "RF CUSTOM", "RF FORCE BLUE IC",
    };
    if (row < 0 || row >= CrEditorRowCount) return "";
    return kLabels[row];
}

void CrFormatCellImpl(int player, int row, char* buf, size_t bufSz) {
    if (!buf || bufSz == 0) return;
    buf[0] = '\0';
    if (row < 0 || row >= CrEditorRowCount) return;

    if (player == 0) {
        switch (row) {
            case 0: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", g_crEnabledP1 ? "ON" : "OFF"); break;
            case 1: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", kCrHpModeChoices[g_crHpModeP1]); break;
            case 2: _snprintf_s(buf, bufSz, _TRUNCATE, "%d", g_crHpCustomP1); break;
            case 3: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", kCrMeterModeChoices[g_crMeterModeP1]); break;
            case 4: _snprintf_s(buf, bufSz, _TRUNCATE, "%d", g_crMeterCustomP1); break;
            case 5: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", kCrRfModeChoices[g_crRfModeP1]); break;
            case 6: _snprintf_s(buf, bufSz, _TRUNCATE, "%.0f", g_crRfCustomP1); break;
            case 7: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", g_crForceBlueICP1 ? "ON" : "OFF"); break;
        }
    } else {
        switch (row) {
            case 0: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", g_crEnabledP2 ? "ON" : "OFF"); break;
            case 1: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", kCrHpModeChoices[g_crHpModeP2]); break;
            case 2: _snprintf_s(buf, bufSz, _TRUNCATE, "%d", g_crHpCustomP2); break;
            case 3: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", kCrMeterModeChoices[g_crMeterModeP2]); break;
            case 4: _snprintf_s(buf, bufSz, _TRUNCATE, "%d", g_crMeterCustomP2); break;
            case 5: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", kCrRfModeChoices[g_crRfModeP2]); break;
            case 6: _snprintf_s(buf, bufSz, _TRUNCATE, "%.0f", g_crRfCustomP2); break;
            case 7: _snprintf_s(buf, bufSz, _TRUNCATE, "%s", g_crForceBlueICP2 ? "ON" : "OFF"); break;
        }
    }
}

void CrAdjustCellImpl(int player, int row, int direction, bool bigStep) {
    const int dir = (direction < 0) ? -1 : 1;
    if (player == 0) {
        switch (row) {
            case 0: g_crEnabledP1 = !g_crEnabledP1; OnCrEnabledP1(); break;
            case 1: g_crHpModeP1 = (g_crHpModeP1 + dir + 4) % 4; OnCrHpModeP1(); break;
            case 2: { int step = bigStep ? 1000 : 100; g_crHpCustomP1 += dir * step; if (g_crHpCustomP1 < 0) g_crHpCustomP1 = 0; if (g_crHpCustomP1 > 9999) g_crHpCustomP1 = 9999; OnCrHpCustomP1(); break; }
            case 3: g_crMeterModeP1 = (g_crMeterModeP1 + dir + 6) % 6; OnCrMeterModeP1(); break;
            case 4: { int step = bigStep ? 500 : 50; g_crMeterCustomP1 += dir * step; if (g_crMeterCustomP1 < 0) g_crMeterCustomP1 = 0; if (g_crMeterCustomP1 > 3000) g_crMeterCustomP1 = 3000; OnCrMeterCustomP1(); break; }
            case 5: g_crRfModeP1 = (g_crRfModeP1 + dir + 6) % 6; OnCrRfModeP1(); break;
            case 6: { float step = bigStep ? 100.0f : 10.0f; g_crRfCustomP1 += dir * step; if (g_crRfCustomP1 < 0.0f) g_crRfCustomP1 = 0.0f; if (g_crRfCustomP1 > 1000.0f) g_crRfCustomP1 = 1000.0f; OnCrRfCustomP1(); break; }
            case 7: g_crForceBlueICP1 = !g_crForceBlueICP1; OnCrForceBlueICP1(); break;
        }
    } else {
        switch (row) {
            case 0: g_crEnabledP2 = !g_crEnabledP2; OnCrEnabledP2(); break;
            case 1: g_crHpModeP2 = (g_crHpModeP2 + dir + 4) % 4; OnCrHpModeP2(); break;
            case 2: { int step = bigStep ? 1000 : 100; g_crHpCustomP2 += dir * step; if (g_crHpCustomP2 < 0) g_crHpCustomP2 = 0; if (g_crHpCustomP2 > 9999) g_crHpCustomP2 = 9999; OnCrHpCustomP2(); break; }
            case 3: g_crMeterModeP2 = (g_crMeterModeP2 + dir + 6) % 6; OnCrMeterModeP2(); break;
            case 4: { int step = bigStep ? 500 : 50; g_crMeterCustomP2 += dir * step; if (g_crMeterCustomP2 < 0) g_crMeterCustomP2 = 0; if (g_crMeterCustomP2 > 3000) g_crMeterCustomP2 = 3000; OnCrMeterCustomP2(); break; }
            case 5: g_crRfModeP2 = (g_crRfModeP2 + dir + 6) % 6; OnCrRfModeP2(); break;
            case 6: { float step = bigStep ? 100.0f : 10.0f; g_crRfCustomP2 += dir * step; if (g_crRfCustomP2 < 0.0f) g_crRfCustomP2 = 0.0f; if (g_crRfCustomP2 > 1000.0f) g_crRfCustomP2 = 1000.0f; OnCrRfCustomP2(); break; }
            case 7: g_crForceBlueICP2 = !g_crForceBlueICP2; OnCrForceBlueICP2(); break;
        }
    }
}

void CrActivateCellImpl(int player, int row) {
    CrAdjustCellImpl(player, row, +1, false);
}

void CorrectValueLocksForEngineRegenUiImpl(GuiValueLocks::State& locks) {
    if (g_f4Mode == 1 && g_f5Mode == 0 &&
        locks.globalReason == GuiValueLocks::GlobalReason::EngineF5) {
        locks.globalReason = GuiValueLocks::GlobalReason::EngineF4;
    }
}

} // namespace

const char* FormatTriggerButtonRow(const Row& row) {
    return FormatTriggerButtonRowImpl(row);
}

bool AdjustTriggerButtonRow(const Row& row, int direction) {
    return AdjustTriggerButtonRowImpl(row, direction);
}

// Public per-screen entry points

void OpenMissionBrowser() {
    g_authoringRescanRequested.store(true, std::memory_order_release);
    MenuNavigationRequest req;
    // This is a dedicated route; it deliberately does not depend on a hidden
    // row in ordinary Practice Debug.
    req.pane = MenuPane::SettingsDebug;
    req.submenuBuilder = BuildMissionBrowserRows;
    req.submenuTitle = "RECORD & AUTHOR";
    RequestMenuNavigation(req);
}

void OpenPracticeRoot() {
    ResetSubmenus();
    MenuNavigationRequest req{};
    req.pane = MenuPane::Values;
    req.focusRow = 0;
    RequestMenuNavigation(req);
}

void PrepareNewMissionAuthoringSession() {
    // The title-entry path calls this from the frame-monitor owner immediately
    // before opening the menu. Publish only a request; the menu/render owner
    // applies every string/vector mutation on its next builder pass.
    g_prepareNewMissionAuthoringRequested.store(true, std::memory_order_release);
}

void NotifyMissionLibraryChanged() {
    g_authoringRescanRequested.store(true, std::memory_order_release);
}

void ResetHotswapMenuSeed() {
    ResetHotswapMenuSeedState();
}

// Macros
bool g_macroIncludeBuffers = true;
int  g_macroSlotMirror = 1;
int  g_macroLastSlot = -1;
bool g_macroLastInclude = true;
bool g_macroForceReload = true;
std::string g_macroText;
std::string g_macroApplyError;
std::vector<std::string> g_macroUndoStack;
std::vector<std::string> g_macroRedoStack;

struct MacroTextEditorState {
    bool active = false;
    bool wantFocus = false;
    std::vector<char> buffer;
};
MacroTextEditorState g_macroEditor;

void MacroPushUndo(const std::string& previous) {
    if (!g_macroUndoStack.empty() && g_macroUndoStack.back() == previous) return;
    g_macroUndoStack.push_back(previous);
    if (g_macroUndoStack.size() > 64) {
        g_macroUndoStack.erase(g_macroUndoStack.begin());
    }
}

void MacroEnsureEditorBuffer(const std::string& text) {
    const size_t minCap = 8192;
    const size_t cap = (std::max)(minCap, text.size() + 1024);
    g_macroEditor.buffer.assign(cap, '\0');
    if (!text.empty()) {
        memcpy(g_macroEditor.buffer.data(), text.data(), (std::min)(text.size(), cap - 1));
    }
}

std::string MacroCleanText(const std::string& text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    bool prevWasSpace = false;
    for (char c : text) {
        const bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (isSpace) {
            if (!prevWasSpace && !cleaned.empty()) {
                cleaned.push_back(' ');
                prevWasSpace = true;
            }
        } else {
            cleaned.push_back(c);
            prevWasSpace = false;
        }
    }
    if (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
    return cleaned;
}

void MacroReloadFromSlot() {
    const int slot = MacroController::GetCurrentSlot();
    g_macroText = MacroController::SerializeSlot(slot, g_macroIncludeBuffers);
    g_macroApplyError.clear();
    g_macroUndoStack.clear();
    g_macroRedoStack.clear();
    g_macroLastSlot = slot;
    g_macroLastInclude = g_macroIncludeBuffers;
    g_macroForceReload = false;
    if (g_macroEditor.active) MacroEnsureEditorBuffer(g_macroText);
}

void MacroMaybeReloadText() {
    if (g_macroEditor.active && !g_macroForceReload) return;
    const int slot = MacroController::GetCurrentSlot();
    if (g_macroForceReload || g_macroLastSlot != slot || g_macroLastInclude != g_macroIncludeBuffers) {
        MacroReloadFromSlot();
    }
}

void MacroSetText(const std::string& text, bool pushUndo) {
    if (pushUndo && text != g_macroText) {
        MacroPushUndo(g_macroText);
        g_macroRedoStack.clear();
    }
    g_macroText = text;
    g_macroApplyError.clear();
    if (g_macroEditor.active) MacroEnsureEditorBuffer(g_macroText);
}

void MacroApplyTextToSlot() {
    const int slot = MacroController::GetCurrentSlot();
    std::string err;
    const std::string cleaned = MacroCleanText(g_macroText);
    if (!MacroController::DeserializeSlot(slot, cleaned, err)) {
        g_macroApplyError = err;
        DirectDrawHook::AddMessage(g_macroApplyError.c_str(), "MACRO", RGB(255, 120, 120), 1400, 0, 120);
        return;
    }

    g_macroApplyError.clear();
    g_macroText = MacroController::SerializeSlot(slot, g_macroIncludeBuffers);
    g_macroUndoStack.clear();
    g_macroRedoStack.clear();
    g_macroLastSlot = slot;
    g_macroLastInclude = g_macroIncludeBuffers;
    g_macroForceReload = false;
    if (g_macroEditor.active) MacroEnsureEditorBuffer(g_macroText);
    DirectDrawHook::AddMessage("Applied macro to slot", "MACRO", RGB(180, 255, 180), 1000, 0, 120);
}

void MacroRecord() {
    if (Mission::Engine::Recorder::IsSessionActive()) {
        const auto phase = Mission::Engine::Recorder::GetPhase();
        if (phase == Mission::Engine::Recorder::Phase::Review) {
            OpenMissionBrowser();
            return;
        }
        Mission::Engine::Recorder::Advance();
        if (phase == Mission::Engine::Recorder::Phase::PreRecord &&
            ImGuiImpl::IsVisible()) {
            ImGuiImpl::ToggleVisibility();
        }
        return;
    }
    const auto before = MacroController::GetState();
    MacroController::ToggleRecord();
    DirectDrawHook::AddMessage(MacroController::GetStatusLine().c_str(), "MACRO", RGB(200, 220, 255), 900, 0, 120);
    if (before == MacroController::State::PreRecord &&
        MacroController::GetState() == MacroController::State::Recording &&
        ImGuiImpl::IsVisible()) {
        ImGuiImpl::ToggleVisibility();
    }
}
void MacroPlay() {
    if (Mission::Engine::Recorder::IsSessionActive()) {
        DirectDrawHook::AddMessage("Macro playback is unavailable during mission authoring",
                                   "MISSION", RGB(255, 200, 120), 1200, 0, 120);
        return;
    }
    MacroController::Play();
    DirectDrawHook::AddMessage(MacroController::GetStatusLine().c_str(), "MACRO", RGB(180, 255, 180), 900, 0, 120);
}
void MacroStop() {
    if (Mission::Engine::Recorder::IsSessionActive()) {
        if (Mission::Engine::Recorder::GetPhase() ==
            Mission::Engine::Recorder::Phase::Recording) {
            Mission::Engine::Recorder::Advance();
        } else {
            DirectDrawHook::AddMessage("Use Record & Author to Retake or Discard this session",
                                       "MISSION", RGB(255, 200, 120), 1400, 0, 120);
        }
        return;
    }
    MacroController::Stop();
    DirectDrawHook::AddMessage(MacroController::GetStatusLine().c_str(), "MACRO", RGB(255, 220, 120), 900, 0, 120);
}
void MacroPrevSlot() {
    MacroController::PrevSlot();
    g_macroForceReload = true;
}
void MacroNextSlot() {
    MacroController::NextSlot();
    g_macroForceReload = true;
}

void OnMacroSlotChanged() {
    MacroController::SetCurrentSlot(g_macroSlotMirror);
    g_macroForceReload = true;
}
void OnMacroIncludeBuffers() {
    g_macroForceReload = true;
}

void MacroOpenEditor() {
    MacroMaybeReloadText();
    MacroEnsureEditorBuffer(g_macroText);
    g_macroEditor.active = true;
    g_macroEditor.wantFocus = true;
    Input::ResetEdges();
}

void MacroReloadAction() {
    MacroReloadFromSlot();
    DirectDrawHook::AddMessage("Reloaded macro text from slot", "MACRO", RGB(180, 255, 220), 700, 0, 120);
}

void MacroClearSlot() {
    std::string err;
    const int slot = MacroController::GetCurrentSlot();
    if (MacroController::DeserializeSlot(slot, std::string(), err)) {
        g_macroApplyError.clear();
        g_macroForceReload = true;
        MacroReloadFromSlot();
        DirectDrawHook::AddMessage("Cleared macro slot", "MACRO", RGB(255, 220, 120), 900, 0, 120);
    } else {
        g_macroApplyError = err;
    }
}

void MacroUndo() {
    if (g_macroUndoStack.empty()) return;
    g_macroRedoStack.push_back(g_macroText);
    g_macroText = g_macroUndoStack.back();
    g_macroUndoStack.pop_back();
    g_macroApplyError.clear();
    if (g_macroEditor.active) MacroEnsureEditorBuffer(g_macroText);
}
void MacroRedo() {
    if (g_macroRedoStack.empty()) return;
    g_macroUndoStack.push_back(g_macroText);
    g_macroText = g_macroRedoStack.back();
    g_macroRedoStack.pop_back();
    g_macroApplyError.clear();
    if (g_macroEditor.active) MacroEnsureEditorBuffer(g_macroText);
}
void MacroCopy() {
    MacroMaybeReloadText();
    ImGui::SetClipboardText(g_macroText.c_str());
    DirectDrawHook::AddMessage("Copied macro text", "MACRO", RGB(180, 255, 220), 700, 0, 120);
}
void MacroPaste() {
    const char* clip = ImGui::GetClipboardText();
    if (!clip || !*clip) {
        DirectDrawHook::AddMessage("Clipboard is empty", "MACRO", RGB(255, 220, 120), 700, 0, 120);
        return;
    }
    MacroSetText(std::string(clip), true);
    DirectDrawHook::AddMessage("Pasted macro text", "MACRO", RGB(180, 255, 220), 700, 0, 120);
}
void MacroInsertSample() {
    const char* sample =
        "EFZMACRO 1 "
        "5A 5x3 5B 5x3 5C "
        "6 {3: 6 6 6} 2 {3: 2 2 2} 3 {3: 3 3 3} 5B {3: 5B 5 5}";
    MacroSetText(sample, true);
}

bool MacroUndoDisabled() { return g_macroUndoStack.empty(); }
bool MacroRedoDisabled() { return g_macroRedoStack.empty(); }

const char* MacroStateStr() {
    static char buf[64];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, I18n::Tr("SLOT %d/%d"),
                MacroController::GetCurrentSlot(), MacroController::GetSlotCount());
    return buf;
}

const char* MacroPrimaryActionLabel() {
    if (Mission::Engine::Recorder::IsSessionActive()) {
        switch (Mission::Engine::Recorder::GetPhase()) {
            case Mission::Engine::Recorder::Phase::PreRecord: return "START MISSION COUNT-IN";
            case Mission::Engine::Recorder::Phase::Recording: return "STOP MISSION & REVIEW";
            case Mission::Engine::Recorder::Phase::Review: return "TAKE READY - OPEN MISSIONS";
            default: return "MISSION CAPTURE IN PROGRESS";
        }
    }
    switch (MacroController::GetState()) {
        case MacroController::State::Idle:      return "ARM RECORDING (PRE-RECORD)";
        case MacroController::State::PreRecord: return "START CAPTURE";
        case MacroController::State::Recording: return "STOP & KEEP CLIP";
        case MacroController::State::Replaying: return "STOP PLAYBACK";
    }
    return "RECORD";
}

const char* MacroSlotEmptyStr() {
    return MacroController::IsSlotEmpty(MacroController::GetCurrentSlot()) ? "EMPTY" : "HAS DATA";
}

const char* MacroSerializedStr() {
    return g_macroIncludeBuffers ? "BUFFERS" : "NO BUFFERS";
}

const char* MacroStatsStr() {
    static char buf[32];
    const int slot = MacroController::GetCurrentSlot();
    const auto stats = MacroController::GetSlotStats(slot);
    if (!stats.hasData) return "EMPTY";
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, I18n::Tr("%d TICKS"), stats.totalTicks);
    return buf;
}

int BuildTextPreviewRows(const std::string& text, char lines[][96], int maxLines) {
    if (maxLines <= 0) return 0;
    if (text.empty()) {
        strncpy_s(lines[0], 96, "(EMPTY)", _TRUNCATE);
        return 1;
    }

    const int maxChars = 84;
    int line = 0;
    int pos = 0;
    lines[line][0] = '\0';

    auto finishLine = [&]() {
        if (line + 1 >= maxLines) return false;
        ++line;
        pos = 0;
        lines[line][0] = '\0';
        return true;
    };

    std::string token;
    for (size_t i = 0; i <= text.size(); ++i) {
        const char c = (i < text.size()) ? text[i] : ' ';
        const bool split = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || i == text.size());
        if (!split) {
            token.push_back(c);
            continue;
        }
        if (token.empty()) continue;
        const int tokenLen = (int)token.size();
        if (pos > 0 && pos + 1 + tokenLen > maxChars) {
            if (!finishLine()) break;
        }
        if (pos > 0 && pos + 1 < 95) {
            strncat_s(lines[line], 96, " ", _TRUNCATE);
            ++pos;
        }
        strncat_s(lines[line], 96, token.c_str(), _TRUNCATE);
        pos += tokenLen;
        token.clear();
    }

    if (line == maxLines - 1 && text.size() > 0) {
        const size_t len = strlen(lines[line]);
        if (len < 92) {
            strncat_s(lines[line], 96, " ...", _TRUNCATE);
        }
    }
    return line + 1;
}

Row* BuildMacroSerializedRows(int& count) {
    static Row s_rows[48];
    static char preview[12][96];
    static char errorLine[128];
    int n = 0;
    MacroMaybeReloadText();

    s_rows[n++] = Header("SERIALIZED MACRO");
    s_rows[n++] = Toggle("INCLUDE BUFFERS", &g_macroIncludeBuffers, OnMacroIncludeBuffers);
    s_rows[n++] = Action("EDIT TEXT",       MacroOpenEditor, MacroSerializedStr);
    s_rows[n++] = Action("APPLY TO SLOT",   MacroApplyTextToSlot);
    s_rows[n++] = Action("RELOAD FROM SLOT", MacroReloadAction);
    s_rows[n++] = Action("CLEAR SLOT",      MacroClearSlot);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("CLIPBOARD / HISTORY");
    s_rows[n++] = Action("COPY",            MacroCopy);
    s_rows[n++] = Action("PASTE",           MacroPaste);
    s_rows[n++] = Action("UNDO",            MacroUndo, nullptr, MacroUndoDisabled);
    s_rows[n++] = Action("REDO",            MacroRedo, nullptr, MacroRedoDisabled);
    s_rows[n++] = Action("INSERT SAMPLE",   MacroInsertSample);

    if (!g_macroApplyError.empty()) {
        s_rows[n++] = Spacer();
        s_rows[n++] = Header("ERROR");
        _snprintf_s(errorLine, sizeof(errorLine), _TRUNCATE, "%s", g_macroApplyError.c_str());
        s_rows[n++] = Info(errorLine);
    }

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("PREVIEW");
    const int previewCount = BuildTextPreviewRows(g_macroText, preview, 12);
    for (int i = 0; i < previewCount && n < 47; ++i) {
        s_rows[n++] = Info(preview[i]);
    }

    count = n;
    return s_rows;
}

Row* BuildMacroStatsRows(int& count) {
    static Row s_rows[24];
    static char status[128];
    static char slot[96];
    static char spans[64];
    static char ticks[96];
    static char effective[64];
    static char firstButton[64];
    static char buffers[96];
    static char indexes[96];
    int n = 0;
    const int curSlot = MacroController::GetCurrentSlot();
    const auto stats = MacroController::GetSlotStats(curSlot);
    const int effectiveTicks = MacroController::GetEffectiveTicks(curSlot);
    const int firstButtonTick = MacroController::GetFirstButtonTick(curSlot);

    _snprintf_s(status, sizeof(status), _TRUNCATE, I18n::Tr("State: %s"), MacroController::GetStatusLine().c_str());
    _snprintf_s(slot, sizeof(slot), _TRUNCATE, I18n::Tr("Current slot: %d / %d (%s)"),
                curSlot, MacroController::GetSlotCount(), I18n::Tr(stats.hasData ? "has data" : "empty"));
    _snprintf_s(spans, sizeof(spans), _TRUNCATE, I18n::Tr("Spans: %d"), stats.spanCount);
    _snprintf_s(ticks, sizeof(ticks), _TRUNCATE, I18n::Tr("Total ticks: %d (~%.2f sec)"),
                stats.totalTicks, stats.totalTicks / 64.0f);
    _snprintf_s(effective, sizeof(effective), _TRUNCATE, I18n::Tr("Effective ticks: %d"), effectiveTicks);
    _snprintf_s(firstButton, sizeof(firstButton), _TRUNCATE,
                I18n::Tr(firstButtonTick >= 0 ? "First button tick: %d" : "First button tick: none"),
                firstButtonTick);
    _snprintf_s(buffers, sizeof(buffers), _TRUNCATE, I18n::Tr("Buffer entries: %d  Buffer ticks: %d"),
                stats.bufEntries, stats.bufTicks);
    _snprintf_s(indexes, sizeof(indexes), _TRUNCATE, I18n::Tr("Buffer idx: %u -> %u  Samples: %d"),
                (unsigned)stats.bufStartIdx, (unsigned)stats.bufEndIdx, stats.bufIndexTicks);

    s_rows[n++] = Header("SLOT STATUS");
    s_rows[n++] = Info(status);
    s_rows[n++] = Info(slot);
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("TIMING");
    s_rows[n++] = Info(spans);
    s_rows[n++] = Info(ticks);
    s_rows[n++] = Info(effective);
    s_rows[n++] = Info(firstButton);
    s_rows[n++] = Spacer();
    s_rows[n++] = Header("BUFFER CAPTURE");
    s_rows[n++] = Info(buffers);
    s_rows[n++] = Info(indexes);
    count = n;
    return s_rows;
}

Row* BuildMacrosRows(int& count) {
    static Row s_rows[24];
    int n = 0;
    MacroMaybeReloadText();
    g_macroSlotMirror = MacroController::GetCurrentSlot();

    static char status[128];
    _snprintf_s(status, sizeof(status), _TRUNCATE, "State: %s", MacroController::GetStatusLine().c_str());

    s_rows[n++] = Header("MACRO CONTROLLER");
    s_rows[n++] = Info(status);
    const auto phase = MacroController::GetState();
    if (phase == MacroController::State::Idle) {
        s_rows[n++] = Info("Arm first, arrange the dummy, then start capture. Recording never begins on the arm press.");
    } else if (phase == MacroController::State::PreRecord) {
        s_rows[n++] = Info("PRE-RECORD: P2 is under your control; start when the setup and your hands are ready.");
    } else if (phase == MacroController::State::Recording) {
        s_rows[n++] = Info("RECORDING: press Macro Record again, or use Stop & Keep Clip here, to save the clip.");
    } else {
        s_rows[n++] = Info("PLAYBACK: the clip drives P2 until it ends or you pick Stop.");
    }
    s_rows[n++] = IntNum("CURRENT SLOT", &g_macroSlotMirror, 1, MacroController::GetSlotCount(),
                         1, 1, OnMacroSlotChanged);
    s_rows[n++] = Action(MacroPrimaryActionLabel(), MacroRecord, MacroStateStr);
    s_rows[n++] = Action("PLAY",            MacroPlay,   MacroSlotEmptyStr);
    s_rows[n++] = Action("STOP",            MacroStop);
    s_rows[n++] = Action("PREV SLOT",       MacroPrevSlot);
    s_rows[n++] = Action("NEXT SLOT",       MacroNextSlot);

    s_rows[n++] = Spacer();
    s_rows[n++] = Header("MACRO TOOLS");
    s_rows[n++] = Submenu("SERIALIZED MACRO", "SERIALIZED MACRO", BuildMacroSerializedRows, MacroSerializedStr);
    s_rows[n++] = Submenu("SLOT STATS",       "SLOT STATS",       BuildMacroStatsRows,      MacroStatsStr);
    s_rows[n++] = Action("GUIDE",            NavToMacroHelp);
    count = n;
    return s_rows;
}

static bool InlineTextEditorActive() {
    return g_macroEditor.active || g_missionTextEditor.active;
}

bool IsTextEditorActive() {
    return InlineTextEditorActive() || IsManualKeybindEditorActive();
}

bool IsRawVkEditorActive() {
    return IsManualKeybindEditorActive();
}

void ResetTextEditor() {
    g_macroEditor.active = false;
    g_macroEditor.wantFocus = false;
    g_missionTextEditor = MissionTextEditorState{};
    CloseManualKeybindEditor();
}

bool TickMacroTextEditorIfActive(ImDrawList*, const ScreenLayout& layout) {
    if (g_missionTextEditor.active) {
        const CustomMenu::Scale::Metrics& metrics = CustomMenu::Scale::Get();
        const float marginX = CustomMenu::Scale::Snap(38.0f * metrics.layoutScale);
        const float bottomPad = CustomMenu::Scale::Snap(8.0f * metrics.layoutScale);
        const float x = CustomMenu::Scale::Snap(layout.panelX + marginX);
        const float y = CustomMenu::Scale::Snap(layout.contentTopY + bottomPad);
        const float w = CustomMenu::Scale::Snap(Theme::kPanelW - marginX * 2.0f);
        const float h = CustomMenu::Scale::Snap(layout.contentBottomY - y - bottomPad);

        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.94f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.86f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.04f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin(g_missionTextEditor.title, nullptr, flags)) {
            ImGui::TextDisabled("%s", g_missionTextEditor.help);
            ImGui::TextDisabled(I18n::Tr("%zu / %zu bytes"),
                strnlen_s(g_missionTextEditor.buffer.data(),
                          g_missionTextEditor.buffer.size()),
                g_missionTextEditor.maxBytes);
            if (g_missionTextEditor.wantFocus) {
                ImGui::SetKeyboardFocusHere();
                g_missionTextEditor.wantFocus = false;
            }
            if (g_missionTextEditor.multiline) {
                const float editorH = (std::max)(100.0f,
                    ImGui::GetContentRegionAvail().y - 38.0f);
                ImGui::InputTextMultiline("##mission_text_editor",
                                          g_missionTextEditor.buffer.data(),
                                          g_missionTextEditor.buffer.size(),
                                          ImVec2(-1.0f, editorH));
            } else {
                ImGui::InputText("##mission_text_editor",
                                 g_missionTextEditor.buffer.data(),
                                 g_missionTextEditor.buffer.size());
            }

            if (ImGui::Button("Done")) CloseMissionTextEditor(true);
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) CloseMissionTextEditor(false);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                CloseMissionTextEditor(false);
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(2);
        return true;
    }
    if (!g_macroEditor.active) return false;

    const CustomMenu::Scale::Metrics& metrics = CustomMenu::Scale::Get();
    const float marginX = CustomMenu::Scale::Snap(38.0f * metrics.layoutScale);
    const float bottomPad = CustomMenu::Scale::Snap(8.0f * metrics.layoutScale);
    const float x = CustomMenu::Scale::Snap(layout.panelX + marginX);
    const float y = CustomMenu::Scale::Snap(layout.contentTopY + bottomPad);
    const float w = CustomMenu::Scale::Snap(Theme::kPanelW - marginX * 2.0f);
    const float h = CustomMenu::Scale::Snap(layout.contentBottomY - y - bottomPad);

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.86f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.04f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin(I18n::Tr("MACRO TEXT EDITOR"), nullptr, flags)) {
        ImGui::Text(I18n::Tr("Slot %d / %d"), MacroController::GetCurrentSlot(), MacroController::GetSlotCount());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", I18n::Tr(g_macroIncludeBuffers ? "with buffers" : "without buffers"));
        if (!g_macroApplyError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), I18n::Tr("Error: %s"), g_macroApplyError.c_str());
        } else {
            ImGui::TextDisabled("%s", I18n::Tr("Use EFZMACRO text notation. Apply normalizes whitespace."));
        }

        const float buttonH = 28.0f;
        const float editorH = (std::max)(100.0f, ImGui::GetContentRegionAvail().y - buttonH - 10.0f);
        if (g_macroEditor.wantFocus) {
            ImGui::SetKeyboardFocusHere();
            g_macroEditor.wantFocus = false;
        }
        if (ImGui::InputTextMultiline("##macro_text_editor",
                                      g_macroEditor.buffer.data(),
                                      g_macroEditor.buffer.size(),
                                      ImVec2(-1.0f, editorH),
                                      ImGuiInputTextFlags_AllowTabInput)) {
            const std::string newText = g_macroEditor.buffer.data();
            if (newText != g_macroText) {
                MacroPushUndo(g_macroText);
                g_macroRedoStack.clear();
                g_macroText = newText;
                g_macroApplyError.clear();
            }
        }

        if (ImGui::Button(I18n::Tr("Apply"))) {
            g_macroText = g_macroEditor.buffer.data();
            MacroApplyTextToSlot();
        }
        ImGui::SameLine();
        if (ImGui::Button(I18n::Tr("Done"))) {
            g_macroText = g_macroEditor.buffer.data();
            g_macroEditor.active = false;
            Input::ResetEdges();
        }
        ImGui::SameLine();
        if (ImGui::Button(I18n::Tr("Cancel"))) {
            MacroReloadFromSlot();
            g_macroEditor.active = false;
            Input::ResetEdges();
        }
        ImGui::SameLine();
        if (ImGui::Button(I18n::Tr("Reload"))) {
            MacroReloadFromSlot();
            g_macroEditor.wantFocus = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(I18n::Tr("Sample"))) {
            MacroInsertSample();
            g_macroEditor.wantFocus = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g_macroText = g_macroEditor.buffer.data();
            g_macroEditor.active = false;
            Input::ResetEdges();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(2);
    return true;
}

void LogMirrorStepSeh(unsigned code, const char* name) {
    char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "[CUSTOM_MENU][TRACE] SEH 0x%08X in %s",
        code, name ? name : "(unknown mirror)");
    LogOut(buf, true);
}

void LogSlowSecondaryMirrorRefresh(DWORD elapsed) {
    char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "[CUSTOM_MENU][TIMING] Slow secondary mirror refresh: %lums",
        static_cast<unsigned long>(elapsed));
    LogOut(buf, true);
}

static bool SehRefreshMirrorStep(const char* name, void (*fn)()) {
    __try {
        fn();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogMirrorStepSeh((unsigned)GetExceptionCode(), name);
        return false;
    }
}

void RefreshSecondaryScreenMirrors() {
    // Wrap each step in SEH so one bad refresher (e.g. one walking game memory while
    // characters are mid-init) cannot kill the whole menu render path.
    struct Step { const char* name; void (*fn)(); };
    static const Step kFastSteps[] = {
        { "RefreshAutoMirrors",        &RefreshAutoMirrors        },
        { "RefreshCharMirrors",        &RefreshCharMirrors        },
        { "RefreshOpponentMirrors",    &RefreshOpponentMirrors    },
        { "RefreshOptionsMirrors",     &RefreshOptionsMirrors     },
    };
    static const Step kSlowSteps[] = {
        { "RefreshHelpStrings",        &RefreshHelpStrings        },
        { "RefreshHotkeyStrings",      &RefreshHotkeyStrings      },
        { "RefreshMacroSlotChoices",   &RefreshMacroSlotChoices   },
        { "RefreshSayuriMoveChoices",  &SayuriCounter::RefreshMoveChoices },
        { "RefreshDebugMirrors",       &RefreshDebugMirrors       },
        { "RefreshCrMirrors",          &RefreshCrMirrors          },
        { "RefreshEngineRegenMirrors", &RefreshEngineRegenMirrors },
        { "RefreshFramestepMirror",    &RefreshFramestepMirror    },
    };

    for (const Step& s : kFastSteps) {
        SehRefreshMirrorStep(s.name, s.fn);
    }

    constexpr DWORD kSlowMirrorRefreshMs = 250;
    static DWORD s_lastSlowMirrorRefresh = 0;

    const DWORD now = GetTickCount();
    if (s_lastSlowMirrorRefresh != 0 && (now - s_lastSlowMirrorRefresh) < kSlowMirrorRefreshMs) {
        return;
    }
    s_lastSlowMirrorRefresh = now;

    const DWORD slowStart = GetTickCount();
    for (const Step& s : kSlowSteps) {
        const DWORD stepStart = GetTickCount();
        SehRefreshMirrorStep(s.name, s.fn);
        const DWORD stepElapsed = GetTickCount() - stepStart;
        // Log any slow individual step so we can pinpoint future hangs in one repro.
        // Threshold deliberately low (>=20ms) since the whole loop runs throttled to 250ms.
        if (stepElapsed >= 20) {
            char stepBuf[160];
            _snprintf_s(stepBuf, sizeof(stepBuf), _TRUNCATE,
                "[CUSTOM_MENU][TIMING] step=%s took=%lums",
                s.name ? s.name : "(unknown)",
                static_cast<unsigned long>(stepElapsed));
            LogOut(stepBuf, true);
        }
    }

    const DWORD elapsed = GetTickCount() - slowStart;
    static DWORD s_lastSlowMirrorLog = 0;
    if (elapsed >= 50 && (s_lastSlowMirrorLog == 0 || (now - s_lastSlowMirrorLog) >= 1000)) {
        s_lastSlowMirrorLog = now;
        LogSlowSecondaryMirrorRefresh(elapsed);
    }
}

static void TickListScreen(ImDrawList* dl, const ScreenLayout& layout,
                           const char* title, Row* rows, int n,
                           int& focus, ScrollState& scroll, bool& backEdge) {
    ClampFocus(rows, n, focus);
    backEdge = HandleListInput(layout, rows, n, focus, scroll);
    RenderList(dl, layout, title, rows, n, focus, scroll);
}

// MAIN sub-panes
void TickOpponent(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildOpponentRows(n);
    TickListScreen(dl, layout, "OPPONENT", rows, n, focus, scroll, backEdge);
}
void TickOptions(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildOptionsRows(n);
    TickListScreen(dl, layout, "OPTIONS", rows, n, focus, scroll, backEdge);
}
void TickMenu(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildMenuRows(n);
    TickListScreen(dl, layout, "MENU", rows, n, focus, scroll, backEdge);
}
void TickValues(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildValuesRootRows(n);
    TickListScreen(dl, layout, "VALUES", rows, n, focus, scroll, backEdge);
}

bool CrRowHidden(int player, int row) { return CrRowHiddenImpl(player, row); }
const char* CrRowLabel(int row) { return CrRowLabelImpl(row); }
void CrFormatCell(int player, int row, char* buf, size_t bufSz) { CrFormatCellImpl(player, row, buf, bufSz); }
void CrAdjustCell(int player, int row, int direction, bool bigStep) { CrAdjustCellImpl(player, row, direction, bigStep); }
void CrActivateCell(int player, int row) { CrActivateCellImpl(player, row); }
void ResetContinuousRecoveryEditorState() {}
void CorrectValueLocksForEngineRegenUi(GuiValueLocks::State& locks) { CorrectValueLocksForEngineRegenUiImpl(locks); }

// AUTO sub-panes
void TickTriggers(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildTriggersRows(n);
    TickListScreen(dl, layout, "TRIGGERS", rows, n, focus, scroll, backEdge);
}
void TickMacros(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildMacrosRows(n);
    if (InlineTextEditorActive()) {
        ClampFocus(rows, n, focus);
        backEdge = false;
        RenderList(dl, layout, "MACROS", rows, n, focus, scroll);
        TickMacroTextEditorIfActive(dl, layout);
        return;
    }
    TickListScreen(dl, layout, "MACROS", rows, n, focus, scroll, backEdge);
}

// CHARS
void TickChars(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildCharsRows(n);
    TickListScreen(dl, layout, "CHARACTERS", rows, n, focus, scroll, backEdge);
}

// SETTINGS sub-panes
void TickSettingsGeneral(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildSettingsGeneralRows(n);
    TickListScreen(dl, layout, "GENERAL", rows, n, focus, scroll, backEdge);
}
void TickSettingsHotkeys(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildSettingsHotkeysRows(n);
    if (IsManualKeybindEditorActive()) {
        ClampFocus(rows, n, focus);
        backEdge = false;
        RenderList(dl, layout, "HOTKEYS", rows, n, focus, scroll);
        TickManualKeybindEditorIfActive(dl, layout);
        return;
    }
    TickListScreen(dl, layout, "HOTKEYS", rows, n, focus, scroll, backEdge);
}
void TickSettingsDebug(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    // Authoring callbacks only set request flags. Apply them before any active
    // submenu builds Rows so raw choice/value pointers stay valid through the
    // complete input-and-render pass, including nested pack/category screens.
    ProcessPendingMissionAuthoringActions();

    // Throttle the runtime poll: SafeReadMemory, GetModuleHandleA, RF-freeze queries and
    // string formatting all run on the render thread; 100 ms is far below human
    // perception and dramatically cheaper than per-frame.
    constexpr DWORD kDebugRuntimeMirrorRefreshMs = 100;
    static DWORD s_lastDebugRuntimeRefresh = 0;
    const DWORD now = GetTickCount();
    if (s_lastDebugRuntimeRefresh == 0 || (now - s_lastDebugRuntimeRefresh) >= kDebugRuntimeMirrorRefreshMs) {
        s_lastDebugRuntimeRefresh = now;
        RefreshDebugRuntimeMirrors();
    }
    int n = 0; Row* rows = BuildSettingsDebugRows(n);
    if (InlineTextEditorActive()) {
        ClampFocus(rows, n, focus);
        backEdge = false;
        // RenderList substitutes the active Missions submenu, so the authoring
        // form remains visible beneath its modal without accepting navigation.
        RenderList(dl, layout, "DEBUG", rows, n, focus, scroll);
        TickMacroTextEditorIfActive(dl, layout);
        return;
    }
    TickListScreen(dl, layout, "DEBUG", rows, n, focus, scroll, backEdge);
}

// HELP sub-panes
void TickHelpStart(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildHelpStartRows(n);
    TickListScreen(dl, layout, "START", rows, n, focus, scroll, backEdge);
}
void TickHelpGuide(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildHelpGuideRows(n);
    TickListScreen(dl, layout, "GUIDE", rows, n, focus, scroll, backEdge);
}
void TickHelpResources(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildHelpResourcesRows(n);
    TickListScreen(dl, layout, "RESOURCES", rows, n, focus, scroll, backEdge);
}
void TickHelpAbout(ImDrawList* dl, const ScreenLayout& layout, int& focus, ScrollState& scroll, bool& backEdge) {
    int n = 0; Row* rows = BuildHelpAboutRows(n);
    // Opening ABOUT is the acknowledgement. Internally idempotent, so calling
    // it every frame the pane is visible is free.
    UpdateCheck::AcknowledgeLatest();
    TickListScreen(dl, layout, "ABOUT", rows, n, focus, scroll, backEdge);
}

} // namespace CustomMenu::Screens
