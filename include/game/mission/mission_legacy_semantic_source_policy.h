#pragma once

namespace Mission::LegacySemanticSourcePolicy {

struct MoveSelection {
    int exactMove = -1;
    int exactCount = 0;       // saturated: 0, 1, or 2 (ambiguous)
    int positiveMove = -1;
    int positiveCount = 0;    // saturated: 0, 1, or 2 (ambiguous)
};

constexpr MoveSelection AccumulateMove(MoveSelection selected, int move,
                                        bool exactProducerContact) {
    if (move <= 0) return selected;

    if (selected.positiveCount == 0) {
        selected.positiveMove = move;
        selected.positiveCount = 1;
    } else if (selected.positiveCount == 1 &&
               selected.positiveMove != move) {
        selected.positiveCount = 2;
    }

    if (!exactProducerContact) return selected;
    if (selected.exactCount == 0) {
        selected.exactMove = move;
        selected.exactCount = 1;
    } else if (selected.exactCount == 1 && selected.exactMove != move) {
        selected.exactCount = 2;
    }
    return selected;
}

constexpr int SelectMove(const MoveSelection& selected,
                         bool allowOrdinaryWildcard) {
    if (selected.exactCount == 1) return selected.exactMove;
    if (selected.exactCount != 0) return -1;
    return allowOrdinaryWildcard && selected.positiveCount == 1
        ? selected.positiveMove : -1;
}

static_assert(SelectMove(AccumulateMove({}, 253, true), false) == 253 &&
                  SelectMove(AccumulateMove(
                      AccumulateMove({}, 253, true), 253, true), false) ==
                      253 &&
                  SelectMove(AccumulateMove(
                      AccumulateMove({}, 253, true), 254, true), false) < 0,
              "legacy producer migration must reject two exact setters");
static_assert(SelectMove(AccumulateMove({}, 253, false), true) == 253 &&
                  SelectMove(AccumulateMove(
                      AccumulateMove({}, 253, false), 254, false), true) < 0 &&
                  SelectMove(AccumulateMove({}, 253, false), false) < 0,
              "wildcard ordinary migration requires one positive move");

}
