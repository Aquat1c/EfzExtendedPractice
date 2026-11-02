#pragma once

#include <cstdint>

namespace CharacterHotswap::Transition {

enum class WaitState : uint8_t { AwaitingLoading, AwaitingMatch };
enum class ObservedPhase : uint8_t { Other, CharacterSelect, Loading, Match };
enum class Decision : uint8_t { Wait, DirectFallback, LoadingObserved, Complete };

enum class DirectBootstrapState : uint8_t {
    Idle = 0,
    Armed,
    Entered,
    BattleHandoff,
};

enum class DirectLoadingAction : uint8_t {
    NativeUpdate = 0,
    Bootstrap,
    HoldLoading,
    ReturnBattle,
};

constexpr DirectLoadingAction DecideDirectLoadingAction(
        DirectBootstrapState state) {
    switch (state) {
        case DirectBootstrapState::Armed:
            return DirectLoadingAction::Bootstrap;
        case DirectBootstrapState::Entered:
            return DirectLoadingAction::HoldLoading;
        case DirectBootstrapState::BattleHandoff:
            return DirectLoadingAction::ReturnBattle;
        case DirectBootstrapState::Idle:
        default:
            return DirectLoadingAction::NativeUpdate;
    }
}

constexpr bool OwnsDirectLoadingTransaction(DirectBootstrapState state) {
    return state != DirectBootstrapState::Idle;
}

constexpr bool HasDirectLoadingBattleHandoff(DirectBootstrapState state) {
    return state == DirectBootstrapState::BattleHandoff;
}

constexpr bool DirectCompletionReceiptAllowed(DirectBootstrapState state,
                                              bool abortAfterHandoff) {
    return state == DirectBootstrapState::BattleHandoff && !abortAfterHandoff;
}

constexpr Decision Decide(WaitState state,
                          ObservedPhase phase,
                          bool directBootstrapPending,
                          bool loadingHandoffComplete) {
    if (state == WaitState::AwaitingLoading) {
        if (directBootstrapPending && phase == ObservedPhase::CharacterSelect) {
            return Decision::DirectFallback;
        }
        if (phase == ObservedPhase::Loading) {
            return Decision::LoadingObserved;
        }
        if (phase == ObservedPhase::Match && loadingHandoffComplete) {
            return Decision::Complete;
        }
        return Decision::Wait;
    }
    return phase == ObservedPhase::Match ? Decision::Complete : Decision::Wait;
}

// A completed-load receipt describes one destination session; the same tuple can appear again,
// so acceptance binds to the lifecycle generation current when the reload completed.
constexpr bool ReceiptGenerationMatches(uint32_t receiptGeneration,
                                        uint32_t currentGeneration) {
    return receiptGeneration != 0 && receiptGeneration == currentGeneration;
}

}
