#include "game/character_hotswap_transition.h"

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

}

int main() {
    using namespace CharacterHotswap::Transition;

    Check(Decide(WaitState::AwaitingLoading, ObservedPhase::Match, true, false) == Decision::Wait,
          "old Match cannot complete a match-origin direct reload");
    Check(Decide(WaitState::AwaitingLoading, ObservedPhase::Loading, false, true) ==
              Decision::LoadingObserved,
          "observed Loading advances the transaction");
    Check(Decide(WaitState::AwaitingLoading, ObservedPhase::Match, false, true) ==
              Decision::Complete,
          "Loading-hook receipt permits a skipped monitor phase");
    Check(Decide(WaitState::AwaitingMatch, ObservedPhase::Loading, false, true) == Decision::Wait,
          "Loading cannot complete before the destination Match");
    Check(Decide(WaitState::AwaitingMatch, ObservedPhase::Match, false, true) == Decision::Complete,
          "new Match completes only after Loading was observed");
    Check(Decide(WaitState::AwaitingLoading, ObservedPhase::CharacterSelect, true, false) ==
              Decision::DirectFallback,
          "direct bootstrap failure retains selector fallback");
    Check(ReceiptGenerationMatches(7, 7),
          "prepared-session receipt is accepted in its destination generation");
    Check(!ReceiptGenerationMatches(7, 8),
          "prepared-session receipt cannot authorize a later session");
    Check(!ReceiptGenerationMatches(0, 0),
          "generation zero is never a valid receipt");

    Check(DecideDirectLoadingAction(DirectBootstrapState::Idle) ==
              DirectLoadingAction::NativeUpdate,
          "unowned Loading updates remain fully native");
    Check(DecideDirectLoadingAction(DirectBootstrapState::Armed) ==
              DirectLoadingAction::Bootstrap,
          "only an armed transaction may construct direct fighters");
    Check(DecideDirectLoadingAction(DirectBootstrapState::Entered) ==
              DirectLoadingAction::HoldLoading,
          "a reentrant poll cannot invoke the native loader twice");
    Check(DecideDirectLoadingAction(DirectBootstrapState::BattleHandoff) ==
              DirectLoadingAction::ReturnBattle,
          "post-loader polls preserve the proven Battle destination");
    Check(OwnsDirectLoadingTransaction(DirectBootstrapState::Armed) &&
              OwnsDirectLoadingTransaction(DirectBootstrapState::Entered) &&
              OwnsDirectLoadingTransaction(DirectBootstrapState::BattleHandoff),
          "direct ownership survives entry until Battle confirmation");
    Check(!OwnsDirectLoadingTransaction(DirectBootstrapState::Idle),
          "idle state owns no direct transaction");
    Check(DirectCompletionReceiptAllowed(DirectBootstrapState::BattleHandoff, false),
          "a normal Battle handoff may publish its completion receipt");
    Check(!DirectCompletionReceiptAllowed(DirectBootstrapState::Entered, false) &&
              !DirectCompletionReceiptAllowed(DirectBootstrapState::BattleHandoff, true),
          "entered or logically canceled handoffs cannot publish completion receipts");

    std::cout << "character_hotswap_transition_tests passed\n";
    return 0;
}
