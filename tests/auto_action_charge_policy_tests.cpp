#include "game/auto_action_charge_policy.h"

#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* label) {
    if (condition) return;
    ++failures;
    std::cerr << "FAILED: " << label << '\n';
}

AutoActionChargePolicy::NativeGateSnapshot OpenGate() {
    AutoActionChargePolicy::NativeGateSnapshot gate;
    gate.readable = true;
    gate.rf = 500.0;
    gate.actionPhase = 3;
    gate.frameFlags = AutoActionChargePolicy::kInstantChargeFrameFlag;
    gate.currentRank = 10;
    gate.instantChargeRank = 10;
    gate.y = 0.0;
    return gate;
}

}

int main() {
    using namespace AutoActionChargePolicy;

    auto gate = OpenGate();
    Check(NativeGateOpen(gate), "baseline native gate");

    gate.rf = 499.0;
    Check(!NativeGateOpen(gate), "red RF below 500 is closed");
    gate.blueCharge = 1;
    Check(NativeGateOpen(gate), "blue charge opens independently of RF");
    gate.blueCharge = -1;
    Check(!NativeGateOpen(gate), "blue charge is a signed positive flag");

    gate = OpenGate();
    for (int phase : {2, 3, 5}) {
        gate.actionPhase = phase;
        Check(NativeGateOpen(gate), "allowed action phase");
    }
    gate.actionPhase = 1;
    Check(!NativeGateOpen(gate), "phase 1 rejected");
    gate.actionPhase = 6;
    Check(NativeGateOpen(gate), "red RF accepts native counter phase 6");
    gate.rf = 499.0;
    gate.blueCharge = 1;
    Check(!NativeGateOpen(gate),
          "light-blue-only resource rejects counter phase 6");

    gate = OpenGate();
    gate.frameFlags = 0;
    Check(!NativeGateOpen(gate), "PAT IC flag required");
    gate = OpenGate();
    gate.currentRank = 11;
    Check(!NativeGateOpen(gate), "cancel rank must reach IC anchor");

    gate = OpenGate();
    Check(ExpectedDestination(gate) == 167, "ground destination");
    gate.y = -0.01;
    Check(ExpectedDestination(gate) == 171, "air destination");
    Check(ExactDestinationAccepted(167) && ExactDestinationAccepted(171),
          "only native IC destinations accepted");
    Check(!ExactDestinationAccepted(200) && !ExactDestinationAccepted(300),
          "unrelated attacks do not fake IC acceptance");

    gate = OpenGate();
    Check(!ReadyToSubmit(Mode::InstantCharge, false, gate),
          "synthetic FIC phase cannot satisfy ordinary IC");
    Check(ReadyToSubmit(Mode::InstantCharge, true, gate),
          "IC submits after committed contact");
    Check(ReadyToSubmit(Mode::FlickerInstantCharge, false, gate),
          "FIC submits in pre-contact native window");
    Check(!ReadyToSubmit(Mode::FlickerInstantCharge, true, gate) &&
              ContactCancels(Mode::FlickerInstantCharge, true),
          "contact cancels uncatalogued FIC instead of converting it to IC");
    Check(ReadyToSubmit(Mode::FlickerInstantCharge, true, gate, true) &&
              !ContactCancels(Mode::FlickerInstantCharge, true, true),
          "verified fixed FIC window remains valid after prior contact");
    Check(ContactEvidenceSufficient(
              Mode::InstantCharge, true, false),
          "ordinary IC remains a positive direct-contact proof");
    Check(!ContactEvidenceSufficient(
              Mode::InstantCharge, false, true),
          "ordinary IC still requires the direct resolver");
    Check(ContactEvidenceSufficient(
              Mode::FlickerInstantCharge, true, true) &&
              !ContactEvidenceSufficient(
                  Mode::FlickerInstantCharge, true, false) &&
              !ContactEvidenceSufficient(Mode::Off, true, true),
          "FIC absence proof requires both resolver streams");
    Check(EntityContactCountsForMode(
              Mode::InstantCharge, true) &&
              !EntityContactCountsForMode(
                  Mode::InstantCharge, false),
          "ordinary IC requires attributed projectile lineage");
    Check(EntityContactCountsForMode(
              Mode::FlickerInstantCharge, false),
          "any owner projectile contact disqualifies FIC conservatively");
    Check(ResolvePendingObservation(
              Mode::FlickerInstantCharge, true, true) ==
              PendingResolution::CompleteAccepted,
          "accepted FIC wins over a later same-batch projectile contact");
    Check(ResolvePendingObservation(
              Mode::FlickerInstantCharge, false, true) ==
              PendingResolution::CancelForContact,
          "contact still cancels an uncatalogued pending FIC transaction");
    Check(ResolvePendingObservation(
              Mode::FlickerInstantCharge, false, true, true) ==
              PendingResolution::Continue,
          "verified fixed FIC transaction survives earlier contact");
    Check(ResolvePendingObservation(
              Mode::InstantCharge, false, true) ==
              PendingResolution::Continue,
          "ordinary IC contact does not cancel its pending transaction");

    Check(EntitySpawnEstablishesTemporalLineage(
              true, false, true, true),
          "observed dead-to-alive entity spawn establishes lineage");
    Check(!EntitySpawnEstablishesTemporalLineage(
              true, true, true, true),
          "pre-existing alive entity cannot be adopted by a morph");
    Check(!EntitySpawnEstablishesTemporalLineage(
              false, false, true, true) &&
              !EntitySpawnEstablishesTemporalLineage(
                  true, false, false, true),
          "unreadable entity endpoint fails closed");
    Check(ReadableEntityDespawnClearsLineage(true, false) &&
              !ReadableEntityDespawnClearsLineage(false, false),
          "only a readable despawn clears entity lineage");

    Check(SameSourceInstance(250, 4, 250, 5),
          "same advancing action instance");
    Check(!SameSourceInstance(250, 4, 251, 5), "source action exit cancels");
    Check(!SameSourceInstance(250, 4, 250, 3), "source frame rewind cancels");

    for (int move : {306, 307, 308}) {
        Check(IsVerifiedKanoRandomMagicFixedFicSource(move),
              "Kano random-magic fixed FIC source is recognized");
        Check(IsVerifiedKanoRandomMagicLoop(move, 18, 16, 0),
              "Kano random-magic authored loop is accepted");
        Check(IsVerifiedKanoRandomMagicLoop(move, 18, 16, 5),
              "Kano random-magic sixth authored loop is accepted");
    }
    Check(!IsVerifiedKanoRandomMagicLoop(307, 18, 16, 6),
          "Kano random-magic loop count is bounded");
    Check(!IsVerifiedKanoRandomMagicLoop(305, 18, 16, 0) &&
              !IsVerifiedKanoRandomMagicLoop(307, 18, 15, 0) &&
              !IsVerifiedKanoRandomMagicLoop(307, 17, 16, 0),
          "unrelated source rewinds remain rejected");
    Check(!IsVerifiedKanoRandomMagicFixedFicSource(305) &&
              !IsVerifiedKanoRandomMagicFixedFicSource(309),
          "fixed FIC source range remains narrow");

    if (failures != 0) {
        std::cerr << failures << " auto-action charge policy test(s) failed\n";
        return 1;
    }
    std::cout << "auto-action charge policy tests passed\n";
    return 0;
}
