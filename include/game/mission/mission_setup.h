#pragma once
#include "mission_data.h"

namespace Mission::Setup {

namespace ResourcePolicy {
enum class Family {
    None,
    Mishio,
    Mio,
    Kano,
    Mai,
    Ikumi,
    Misuzu,
    Rumi,
    Akiko,
    Neyuki,
};

enum class Verification {
    Unsupported,
    Immediate,
    RequiresActionable,
};

// Unknown keys must fail setup, not be ignored. Rumi's "barehanded" needs an actionable state;
// every other value is a direct memory write.
inline Verification Classify(Family family, const std::string& key) {
    switch (family) {
        case Family::Mishio:
            return key == "element" ? Verification::Immediate : Verification::Unsupported;
        case Family::Mio:
            return key == "stance" ? Verification::Immediate : Verification::Unsupported;
        case Family::Kano:
            return key == "magic" ? Verification::Immediate : Verification::Unsupported;
        case Family::Mai:
            return key == "status" || key == "ghostTime" || key == "ghostCharge"
                ? Verification::Immediate : Verification::Unsupported;
        case Family::Ikumi:
            return key == "blood" || key == "genocide" || key == "levelGauge"
                ? Verification::Immediate : Verification::Unsupported;
        case Family::Misuzu:
            return key == "feathers" || key == "poisonTimer" || key == "poisonLevel"
                ? Verification::Immediate : Verification::Unsupported;
        case Family::Rumi:
            if (key == "barehanded") return Verification::RequiresActionable;
            return key == "kimchiActive" || key == "kimchiTimer"
                ? Verification::Immediate : Verification::Unsupported;
        case Family::Akiko:
            return key == "bulletCycle" ? Verification::Immediate : Verification::Unsupported;
        case Family::Neyuki:
            return key == "jam" ? Verification::Immediate : Verification::Unsupported;
        case Family::None:
            return Verification::Unsupported;
    }
    return Verification::Unsupported;
}
}

namespace DeferredPolicy {
enum class Phase { Idle, AwaitingValues, AwaitingStateRestore };
enum class SettleEffect {
    Wait,
    Fail,
    ApplyValues,
    ReleaseForStateRestore,
};

// A reload becoming "not busy" is not proof the requested match was created;
// only the matching completion receipt may release values or a state restore.
constexpr SettleEffect DecideSettle(bool timedOut, bool reloadBusy,
                                    bool inMatch, bool matchingReceipt,
                                    Phase phase) {
    if (phase == Phase::Idle) return SettleEffect::Wait;
    if (timedOut) return SettleEffect::Fail;
    if (reloadBusy || !inMatch) return SettleEffect::Wait;
    if (!matchingReceipt) return SettleEffect::Fail;
    return phase == Phase::AwaitingValues ? SettleEffect::ApplyValues
                       : SettleEffect::ReleaseForStateRestore;
}
}

void Capture(::Mission::Mission& m);
// applyValues=false: hotswap to the mission's chars/stage/bgm only - value
// writes are skipped entirely (an embedded savestate restore supplies them).
bool Apply(const ::Mission::Mission& m,
           bool applyValues = true,
           bool forceFreshMatch = false,
           bool allowReload = true,
           bool acceptPreparedSession = false);
void Tick();
bool IsPending();
void Cancel(const char* reason = nullptr);
bool TakeFailure(std::string& errorOut);

}
