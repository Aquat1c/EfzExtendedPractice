#pragma once
#include <atomic>
#include <cstdint>
#include "../core/logger.h"
#include <sstream>

struct ValidationMetrics {
    std::atomic<int> dashQueued{0};
    std::atomic<int> dashStartDetected{0};
    std::atomic<int> dashPreStartInterrupts{0};
    std::atomic<int> dashRestoreEvents{0};
    std::atomic<int> dashFollowupCancelled{0};

    std::atomic<int> p1AirtechableEdges{0};
    std::atomic<int> p2AirtechableEdges{0};
    std::atomic<int> p1AirtechInjectionAttempts{0};
    std::atomic<int> p2AirtechInjectionAttempts{0};
    std::atomic<int> p1AirtechSuccess{0};
    std::atomic<int> p2AirtechSuccess{0};

    std::atomic<int> p1LandingEdges{0};
    std::atomic<int> p2LandingEdges{0};
    std::atomic<int> p1ForcedNeutralFrames{0};
    std::atomic<int> p2ForcedNeutralFrames{0};
    std::atomic<int> p1JumpHoldApplied{0};
    std::atomic<int> p2JumpHoldApplied{0};

    std::atomic<int> p1TriggerStarts{0};
    std::atomic<int> p2TriggerStarts{0};
    std::atomic<int> p1ActionsApplied{0};
    std::atomic<int> p2ActionsApplied{0};
    std::atomic<int> p1SuppressedByAutoJump{0};
    std::atomic<int> p2SuppressedByAutoJump{0};
    std::atomic<int> p1SuppressedByMacro{0};
    std::atomic<int> p2SuppressedByMacro{0};
};

inline ValidationMetrics &GetValidationMetrics() {
    static ValidationMetrics m; return m;
}

inline void ResetValidationMetrics() {
    ValidationMetrics &m = GetValidationMetrics();
    m.dashQueued = 0; m.dashStartDetected = 0; m.dashPreStartInterrupts = 0; m.dashRestoreEvents = 0; m.dashFollowupCancelled = 0;
    m.p1AirtechableEdges = 0; m.p2AirtechableEdges = 0; m.p1AirtechInjectionAttempts = 0; m.p2AirtechInjectionAttempts = 0; m.p1AirtechSuccess = 0; m.p2AirtechSuccess = 0;
    m.p1LandingEdges = 0; m.p2LandingEdges = 0; m.p1ForcedNeutralFrames = 0; m.p2ForcedNeutralFrames = 0; m.p1JumpHoldApplied = 0; m.p2JumpHoldApplied = 0;
    m.p1TriggerStarts = 0; m.p2TriggerStarts = 0; m.p1ActionsApplied = 0; m.p2ActionsApplied = 0; m.p1SuppressedByAutoJump = 0; m.p2SuppressedByAutoJump = 0; m.p1SuppressedByMacro = 0; m.p2SuppressedByMacro = 0;
}

inline void LogValidationMetrics(bool detailed) {
    ValidationMetrics &m = GetValidationMetrics();
    std::ostringstream os; os << "[VALIDATION_METRICS]\n"
      << "Dash: queued=" << m.dashQueued.load() << " start=" << m.dashStartDetected.load() << " preInt=" << m.dashPreStartInterrupts.load()
      << " restore=" << m.dashRestoreEvents.load() << " followupCancelled=" << m.dashFollowupCancelled.load() << "\n"
      << "Airtech: p1AbleEdges=" << m.p1AirtechableEdges.load() << " p2AbleEdges=" << m.p2AirtechableEdges.load()
      << " p1InjectAttempts=" << m.p1AirtechInjectionAttempts.load() << " p2InjectAttempts=" << m.p2AirtechInjectionAttempts.load()
      << " p1Success=" << m.p1AirtechSuccess.load() << " p2Success=" << m.p2AirtechSuccess.load() << "\n"
      << "AutoJump: p1LandEdges=" << m.p1LandingEdges.load() << " p2LandEdges=" << m.p2LandingEdges.load()
      << " p1ForcedNeutralFrames=" << m.p1ForcedNeutralFrames.load() << " p2ForcedNeutralFrames=" << m.p2ForcedNeutralFrames.load()
      << " p1HoldApplied=" << m.p1JumpHoldApplied.load() << " p2HoldApplied=" << m.p2JumpHoldApplied.load() << "\n"
      << "AutoAction: p1TriggerStarts=" << m.p1TriggerStarts.load() << " p2TriggerStarts=" << m.p2TriggerStarts.load()
      << " p1Actions=" << m.p1ActionsApplied.load() << " p2Actions=" << m.p2ActionsApplied.load()
      << " p1SuppressedByJump=" << m.p1SuppressedByAutoJump.load() << " p2SuppressedByJump=" << m.p2SuppressedByAutoJump.load()
      << " p1SuppressedByMacro=" << m.p1SuppressedByMacro.load() << " p2SuppressedByMacro=" << m.p2SuppressedByMacro.load();
    LogOut(os.str(), detailed);
}

inline bool &ValidationMetricsEnabled() { static bool enabled = true; return enabled; }
