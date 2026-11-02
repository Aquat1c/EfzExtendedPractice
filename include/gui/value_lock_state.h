#pragma once

#include "../core/memory.h"
#include "../utils/utilities.h"

namespace GuiValueLocks {

enum class GlobalReason {
    None = 0,
    EngineF4,
    EngineF5,
    ContinuousRecovery,
    PendingF4Ui
};

struct State {
    bool hasRegenStatus = false;
    EngineRegenMode regenMode = EngineRegenMode::Unknown;
    uint16_t paramA = 0;
    uint16_t paramB = 0;

    bool engineLocksValues = false;
    bool continuousRecoveryLocksValues = false;
    bool pendingF4UiLocksValues = false;
    bool globalValuesLocked = false;
    GlobalReason globalReason = GlobalReason::None;

    bool p1IcManaged = false;
    bool p2IcManaged = false;
    bool p1IcManagedByFreeze = false;
    bool p2IcManagedByFreeze = false;
    bool p1IcManagedByContinuousRecovery = false;
    bool p2IcManagedByContinuousRecovery = false;
};

State Compute(const DisplayData& data, bool pendingF4UiLock = false);

const char* DescribeGlobalReason(GlobalReason reason);
const char* DescribeIcManagedReason(const State& state, int player);

}
