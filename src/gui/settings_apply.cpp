#include "../include/gui/settings_apply.h"

#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/utils/utilities.h"
#include "../include/core/logger.h"
#include "../include/game/character_settings.h"

void ApplyRuntimeSettings(DisplayData* data) {
    if (!data) {
        LogOut("[SETTINGS] ApplyRuntimeSettings called with null data", true);
        return;
    }

    try {
        autoAirtechEnabled = data->autoAirtech;
        autoAirtechDirection = data->airtechDirection;
        autoAirtechDelay.store(data->airtechDelay);

        autoJumpEnabled.store(data->autoJump);
        jumpDirection.store(data->jumpDirection);
        jumpTarget.store(data->jumpTarget);

        data->autoAction = HasAnyAutoActionTriggerEnabled(*data);
        data->autoActionPlayer = ResolveAutoActionTargetPlayer();
        autoActionEnabled.store(data->autoAction);
        autoActionPlayer.store(data->autoActionPlayer);

        triggerAfterBlockEnabled.store(data->triggerAfterBlock);
        triggerOnWakeupEnabled.store(data->triggerOnWakeup);
        triggerAfterHitstunEnabled.store(data->triggerAfterHitstun);
        triggerAfterAirtechEnabled.store(data->triggerAfterAirtech);
        triggerOnRGEnabled.store(data->triggerOnRG);

        triggerAfterBlockAction.store(data->actionAfterBlock);
        triggerOnWakeupAction.store(data->actionOnWakeup);
        triggerAfterHitstunAction.store(data->actionAfterHitstun);
        triggerAfterAirtechAction.store(data->actionAfterAirtech);
        triggerOnRGAction.store(data->actionOnRG);

        triggerAfterBlockCharge.store(data->chargeAfterBlock);
        triggerOnWakeupCharge.store(data->chargeOnWakeup);
        triggerAfterHitstunCharge.store(data->chargeAfterHitstun);
        triggerAfterAirtechCharge.store(data->chargeAfterAirtech);
        triggerOnRGCharge.store(data->chargeOnRG);

        triggerAfterBlockDelay.store(data->delayAfterBlock);
        triggerOnWakeupDelay.store(data->delayOnWakeup);
        triggerAfterHitstunDelay.store(data->delayAfterHitstun);
        triggerAfterAirtechDelay.store(data->delayAfterAirtech);
        triggerOnRGDelay.store(data->delayOnRG);

        triggerAfterBlockActionPoolMask.store(data->afterBlockActionPoolMask);
        triggerOnWakeupActionPoolMask.store(data->onWakeupActionPoolMask);
        triggerAfterHitstunActionPoolMask.store(data->afterHitstunActionPoolMask);
        triggerAfterAirtechActionPoolMask.store(data->afterAirtechActionPoolMask);
        triggerOnRGActionPoolMask.store(data->onRGActionPoolMask);
        triggerAfterBlockActionPoolMaskLo.store(data->afterBlockActionPoolMaskLo);
        triggerAfterBlockActionPoolMaskHi.store(data->afterBlockActionPoolMaskHi);
        triggerOnWakeupActionPoolMaskLo.store(data->onWakeupActionPoolMaskLo);
        triggerOnWakeupActionPoolMaskHi.store(data->onWakeupActionPoolMaskHi);
        triggerAfterHitstunActionPoolMaskLo.store(data->afterHitstunActionPoolMaskLo);
        triggerAfterHitstunActionPoolMaskHi.store(data->afterHitstunActionPoolMaskHi);
        triggerAfterAirtechActionPoolMaskLo.store(data->afterAirtechActionPoolMaskLo);
        triggerAfterAirtechActionPoolMaskHi.store(data->afterAirtechActionPoolMaskHi);
        triggerOnRGActionPoolMaskLo.store(data->onRGActionPoolMaskLo);
        triggerOnRGActionPoolMaskHi.store(data->onRGActionPoolMaskHi);
        triggerAfterBlockUsePool.store(data->afterBlockUseActionPool);
        triggerOnWakeupUsePool.store(data->onWakeupUseActionPool);
        triggerAfterHitstunUsePool.store(data->afterHitstunUseActionPool);
        triggerAfterAirtechUsePool.store(data->afterAirtechUseActionPool);
        triggerOnRGUsePool.store(data->onRGUseActionPool);
        for (int i = 0; i < MAX_ACTION_POOL_OPTIONS; ++i) {
            g_afterBlockActionPoolDelays[i]   = data->afterBlockActionPoolDelays[i];
            g_onWakeupActionPoolDelays[i]     = data->onWakeupActionPoolDelays[i];
            g_afterHitstunActionPoolDelays[i] = data->afterHitstunActionPoolDelays[i];
            g_afterAirtechActionPoolDelays[i] = data->afterAirtechActionPoolDelays[i];
            g_onRGActionPoolDelays[i]         = data->onRGActionPoolDelays[i];
            g_afterBlockActionPoolCharges[i]   = data->afterBlockActionPoolCharges[i];
            g_onWakeupActionPoolCharges[i]     = data->onWakeupActionPoolCharges[i];
            g_afterHitstunActionPoolCharges[i] = data->afterHitstunActionPoolCharges[i];
            g_afterAirtechActionPoolCharges[i] = data->afterAirtechActionPoolCharges[i];
            g_onRGActionPoolCharges[i]         = data->onRGActionPoolCharges[i];
        }

        triggerAfterBlockMacroSlot.store(data->macroSlotAfterBlock);
        triggerOnWakeupMacroSlot.store(data->macroSlotOnWakeup);
        triggerAfterHitstunMacroSlot.store(data->macroSlotAfterHitstun);
        triggerAfterAirtechMacroSlot.store(data->macroSlotAfterAirtech);
        triggerOnRGMacroSlot.store(data->macroSlotOnRG);

        triggerAfterBlockCustomID.store(data->customAfterBlock);
        triggerOnWakeupCustomID.store(data->customOnWakeup);
        triggerAfterHitstunCustomID.store(data->customAfterHitstun);
        triggerAfterAirtechCustomID.store(data->customAfterAirtech);
        triggerOnRGCustomID.store(data->customOnRG);

        triggerAfterBlockStrength.store(data->strengthAfterBlock);
        triggerOnWakeupStrength.store(data->strengthOnWakeup);
        triggerAfterHitstunStrength.store(data->strengthAfterHitstun);
        triggerAfterAirtechStrength.store(data->strengthAfterAirtech);
        triggerOnRGStrength.store(data->strengthOnRG);

        uintptr_t base = GetEFZBase();
        if (base) {
            uint16_t pA = 0;
            uint16_t pB = 0;
            EngineRegenMode regenMode = EngineRegenMode::Unknown;
            bool got = GetEngineRegenStatus(regenMode, pA, pB);
            const bool f4ActiveNow = got && (regenMode == EngineRegenMode::F4_FineTuneActive);
            if (detailedLogging.load()) {
                const char* modeName = "Unknown";
                switch (regenMode) {
                    case EngineRegenMode::Unknown: modeName = "Unknown"; break;
                    case EngineRegenMode::Normal: modeName = "Normal"; break;
                    case EngineRegenMode::F5_FullOrPreset: modeName = "F5_FullOrPreset"; break;
                    case EngineRegenMode::F4_FineTuneActive: modeName = "F4_FineTuneActive"; break;
                }
                LogOut("[SETTINGS] ApplyRuntimeSettings regenMode=" + std::string(modeName) +
                       " A=" + std::to_string((unsigned)pA) +
                       " B=" + std::to_string((unsigned)pB) +
                       " skipManualValues=" + std::to_string((int)f4ActiveNow), true);
            }
            if (!f4ActiveNow) {
                UpdatePlayerValuesExceptRF(base, EFZ_BASE_OFFSET_P1, EFZ_BASE_OFFSET_P2);
            } else {
                SetPlayerPosition(base, EFZ_BASE_OFFSET_P1, (double)displayData.x1, (double)displayData.y1, false);
                SetPlayerPosition(base, EFZ_BASE_OFFSET_P2, (double)displayData.x2, (double)displayData.y2, false);
            }

            CharacterSettings::ApplyCharacterValues(base, *data);

            if (!f4ActiveNow) {
                if (!SetRFValuesDirect(data->rf1, data->rf2)) {
                    LogOut("[SETTINGS] Failed to set RF values directly, starting freeze thread", true);
                    StartRFFreeze(data->rf1, data->rf2);
                }
            }
        }

        g_contRecoveryEnabled.store(data->continuousRecoveryEnabled);
        g_contRecoveryApplyTo.store(data->continuousRecoveryApplyTo);
        g_contRecHpMode.store(data->recoveryHpMode);
        g_contRecHpCustom.store(data->recoveryHpCustom);
        g_contRecMeterMode.store(data->recoveryMeterMode);
        g_contRecMeterCustom.store(data->recoveryMeterCustom);
        g_contRecRfMode.store(data->recoveryRfMode);
        g_contRecRfCustom.store(data->recoveryRfCustom);
        g_contRecRfForceBlueIC.store(data->recoveryRfForceBlueIC);

        g_contRecEnabledP1.store(data->p1ContinuousRecoveryEnabled);
        g_contRecHpModeP1.store(data->p1RecoveryHpMode);
        g_contRecHpCustomP1.store(data->p1RecoveryHpCustom);
        g_contRecMeterModeP1.store(data->p1RecoveryMeterMode);
        g_contRecMeterCustomP1.store(data->p1RecoveryMeterCustom);
        g_contRecRfModeP1.store(data->p1RecoveryRfMode);
        g_contRecRfCustomP1.store(data->p1RecoveryRfCustom);
        g_contRecRfForceBlueICP1.store(data->p1RecoveryRfForceBlueIC);

        g_contRecEnabledP2.store(data->p2ContinuousRecoveryEnabled);
        g_contRecHpModeP2.store(data->p2RecoveryHpMode);
        g_contRecHpCustomP2.store(data->p2RecoveryHpCustom);
        g_contRecMeterModeP2.store(data->p2RecoveryMeterMode);
        g_contRecMeterCustomP2.store(data->p2RecoveryMeterCustom);
        g_contRecRfModeP2.store(data->p2RecoveryRfMode);
        g_contRecRfCustomP2.store(data->p2RecoveryRfCustom);
        g_contRecRfForceBlueICP2.store(data->p2RecoveryRfForceBlueIC);

        LogOut("[SETTINGS] Runtime settings applied successfully", detailedLogging.load());
    } catch (const std::exception& e) {
        LogOut("[SETTINGS] Exception in ApplyRuntimeSettings: " + std::string(e.what()), true);
    } catch (...) {
        LogOut("[SETTINGS] Unknown exception in ApplyRuntimeSettings", true);
    }
}
