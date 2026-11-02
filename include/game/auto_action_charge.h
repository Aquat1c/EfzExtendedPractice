#pragma once

#include "auto_action_charge_policy.h"

bool ArmAutoActionChargeFollowup(
    int playerNum, AutoActionChargePolicy::Mode mode,
    int triggerType, int actionType,
    uint64_t* reservationTokenOut = nullptr,
    uint64_t primaryMotionGeneration = 0);

// Bounded and never sleeps; called once per internal battle update from the
// tick-integrated auto-action path.
void TickAutoActionChargeFollowups();

bool IsAutoActionChargeFollowupActive(int playerNum);

void CancelAutoActionChargeFollowup(int playerNum, const char* reason);
// Generation-safe: a stale wake/RG cleanup must not retire a newer follow-up
// which replaced its reservation.
bool CancelAutoActionChargeFollowupIfReservation(
    int playerNum, uint64_t reservationToken, const char* reason);
void CancelAllAutoActionChargeFollowups(const char* reason);
