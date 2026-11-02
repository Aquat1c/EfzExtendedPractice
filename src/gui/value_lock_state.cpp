#include "../include/gui/value_lock_state.h"

namespace {

bool IsContinuousRecoveryAny(const DisplayData& data) {
    return (data.p1ContinuousRecoveryEnabled &&
            (data.p1RecoveryHpMode > 0 || data.p1RecoveryMeterMode > 0 || data.p1RecoveryRfMode > 0)) ||
           (data.p2ContinuousRecoveryEnabled &&
            (data.p2RecoveryHpMode > 0 || data.p2RecoveryMeterMode > 0 || data.p2RecoveryRfMode > 0));
}

bool IsContinuousRecoveryColorManaged(const DisplayData& data, int player) {
    if (player == 1) {
        if (!data.p1ContinuousRecoveryEnabled) return false;
        if (data.p1RecoveryRfMode == 3 || data.p1RecoveryRfMode == 4) return true;
        if (data.p1RecoveryRfMode == 5 && data.p1RecoveryRfForceBlueIC) return true;
        return false;
    }

    if (player == 2) {
        if (!data.p2ContinuousRecoveryEnabled) return false;
        if (data.p2RecoveryRfMode == 3 || data.p2RecoveryRfMode == 4) return true;
        if (data.p2RecoveryRfMode == 5 && data.p2RecoveryRfForceBlueIC) return true;
        return false;
    }

    return false;
}

}

namespace GuiValueLocks {

State Compute(const DisplayData& data, bool pendingF4UiLock) {
    State state;
    state.pendingF4UiLocksValues = pendingF4UiLock;
    state.hasRegenStatus = GetEngineRegenStatus(state.regenMode, state.paramA, state.paramB);
    state.engineLocksValues =
        state.hasRegenStatus &&
        (state.regenMode == EngineRegenMode::F4_FineTuneActive ||
         state.regenMode == EngineRegenMode::F5_FullOrPreset);
    state.continuousRecoveryLocksValues = IsContinuousRecoveryAny(data);

    state.p1IcManagedByFreeze = IsRFFreezeColorManaging(1);
    state.p2IcManagedByFreeze = IsRFFreezeColorManaging(2);
    state.p1IcManagedByContinuousRecovery = IsContinuousRecoveryColorManaged(data, 1);
    state.p2IcManagedByContinuousRecovery = IsContinuousRecoveryColorManaged(data, 2);
    state.p1IcManaged = state.p1IcManagedByFreeze || state.p1IcManagedByContinuousRecovery;
    state.p2IcManaged = state.p2IcManagedByFreeze || state.p2IcManagedByContinuousRecovery;

    state.globalValuesLocked =
        state.engineLocksValues ||
        state.continuousRecoveryLocksValues ||
        state.pendingF4UiLocksValues;

    if (state.pendingF4UiLocksValues) {
        state.globalReason = GlobalReason::PendingF4Ui;
    } else if (state.regenMode == EngineRegenMode::F4_FineTuneActive) {
        state.globalReason = GlobalReason::EngineF4;
    } else if (state.regenMode == EngineRegenMode::F5_FullOrPreset) {
        state.globalReason = GlobalReason::EngineF5;
    } else if (state.continuousRecoveryLocksValues) {
        state.globalReason = GlobalReason::ContinuousRecovery;
    }

    return state;
}

const char* DescribeGlobalReason(GlobalReason reason) {
    switch (reason) {
        case GlobalReason::None: return "Unlocked";
        case GlobalReason::EngineF4: return "RF Recovery (F4)";
        case GlobalReason::EngineF5: return "Automatic Recovery (F5)";
        case GlobalReason::ContinuousRecovery: return "Continuous Recovery";
        case GlobalReason::PendingF4Ui: return "Pending RF Recovery (F4)";
    }
    return "Unknown";
}

const char* DescribeIcManagedReason(const State& state, int player) {
    if (player == 1) {
        if (state.p1IcManagedByFreeze) return "RF Freeze";
        if (state.p1IcManagedByContinuousRecovery) return "Continuous Recovery";
        return "Unlocked";
    }

    if (player == 2) {
        if (state.p2IcManagedByFreeze) return "RF Freeze";
        if (state.p2IcManagedByContinuousRecovery) return "Continuous Recovery";
        return "Unlocked";
    }

    return "Unknown";
}

}
