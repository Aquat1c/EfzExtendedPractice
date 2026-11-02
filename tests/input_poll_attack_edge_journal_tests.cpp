#include "input/input_poll_attack_edge_journal.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using InputPollAttackEdgeJournalPolicy::Event;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "input_poll_attack_edge_journal_tests: " << message << '\n';
        std::exit(1);
    }
}

void TestFifoAndPartialDrain() {
    InputPollAttackEdgeJournalPolicy::Journal<4> journal;
    Require(journal.Push(Event{10, 0x10}), "first event was rejected");
    Require(journal.Push(Event{12, 0x20}), "second event was rejected");
    Require(journal.Push(Event{17, 0xC0}), "third event was rejected");

    Event first[2]{};
    const auto firstDrain = journal.Drain(first, 2);
    Require(firstDrain.count == 2, "partial drain returned the wrong count");
    Require(!firstDrain.overflowed && firstDrain.droppedEvents == 0,
            "non-overflow drain reported loss");
    Require(first[0].serial == 10 && first[0].mask == 0x10,
            "first event lost its poll identity");
    Require(first[1].serial == 12 && first[1].mask == 0x20,
            "second event lost FIFO order");

    Require(journal.Push(Event{18, 0x40}), "push after partial drain failed");
    Require(journal.Push(Event{25, 0x80}), "wrapped push failed");

    Event remainder[4]{};
    const auto secondDrain = journal.Drain(remainder, 4);
    Require(secondDrain.count == 3, "remainder drain returned the wrong count");
    Require(remainder[0].serial == 17 && remainder[0].mask == 0xC0,
            "partial drain skipped the oldest retained event");
    Require(remainder[1].serial == 18 && remainder[1].mask == 0x40,
            "wrapped event order drifted");
    Require(remainder[2].serial == 25 && remainder[2].mask == 0x80,
            "newest event order drifted");
}

void TestOverflowIsExplicitAndPreservesPublishedEvents() {
    InputPollAttackEdgeJournalPolicy::Journal<3> journal;
    Require(journal.Push(Event{1, 0x10}), "event 1 was rejected");
    Require(journal.Push(Event{2, 0x20}), "event 2 was rejected");
    Require(journal.Push(Event{3, 0x40}), "event 3 was rejected");
    Require(!journal.Push(Event{4, 0x80}), "full journal accepted an event");
    Require(!journal.Push(Event{5, 0x10}), "second overflow was not rejected");

    Event output[3]{};
    const auto drain = journal.Drain(output, 3);
    Require(drain.count == 3, "overflow changed the retained event count");
    Require(drain.overflowed && drain.droppedEvents == 2,
            "overflow loss was not reported exactly");
    Require(output[0].serial == 1 && output[1].serial == 2 &&
                output[2].serial == 3,
            "overflow replaced or reordered published events");

    const auto emptyDrain = journal.Drain(output, 3);
    Require(emptyDrain.count == 0 && !emptyDrain.overflowed &&
                emptyDrain.droppedEvents == 0,
            "overflow signal was not one-shot");
}

void TestResetDiscardsOnlyPreBoundaryState() {
    InputPollAttackEdgeJournalPolicy::Journal<2> journal;
    Require(journal.Push(Event{100, 0x10}), "pre-reset event was rejected");
    Require(journal.Push(Event{101, 0x20}), "second pre-reset event was rejected");
    Require(!journal.Push(Event{102, 0x40}), "overflow setup failed");

    journal.Reset();
    Event output[2]{};
    const auto afterReset = journal.Drain(output, 2);
    Require(afterReset.count == 0 && !afterReset.overflowed,
            "reset retained pre-boundary events or overflow");

    Require(journal.Push(Event{200, 0x80}), "post-reset event was rejected");
    const auto finalDrain = journal.Drain(output, 2);
    Require(finalDrain.count == 1 && output[0].serial == 200 &&
                output[0].mask == 0x80,
            "journal did not resume cleanly after reset");
}

void TestRepeatedCursorWrap() {
    InputPollAttackEdgeJournalPolicy::Journal<4> journal;
    Event output[1]{};
    for (uint32_t serial = 1; serial <= 1000; ++serial) {
        const uint8_t mask = static_cast<uint8_t>(0x10u << (serial % 4));
        Require(journal.Push(Event{serial, mask}),
                "single-event producer/consumer loop overflowed");
        const auto drain = journal.Drain(output, 1);
        Require(drain.count == 1 && output[0].serial == serial &&
                    output[0].mask == mask && !drain.overflowed,
                "cursor wrapping corrupted an event");
    }
}

}

int main() {
    TestFifoAndPartialDrain();
    TestOverflowIsExplicitAndPreservesPublishedEvents();
    TestResetDiscardsOnlyPreBoundaryState();
    TestRepeatedCursorWrap();
    std::cout << "input_poll_attack_edge_journal_tests passed\n";
    return 0;
}
