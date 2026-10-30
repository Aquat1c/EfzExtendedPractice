#include "../include/game/move_timing_policy.h"
#include "../include/game/collision_box_policy.h"

#include <cstdio>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

using MoveTiming::Tick;
using MoveTiming::Tracker;

Tick Make(short id, bool active, bool isCommand = true) {
    Tick t;
    t.inAttack = true;
    t.moveId = id;
    t.isCommand = isCommand;
    t.hitActive = active;
    t.boxesPresent = active;
    return t;
}

void Run(Tracker& t, short id, bool active, int ticks, bool isCommand = true) {
    for (int i = 0; i < ticks; ++i) t.Feed(Make(id, active, isCommand));
}

void End(Tracker& t, bool interrupted = false) {
    Tick idle;
    idle.interrupted = interrupted;
    t.Feed(idle);
}

void TestSimpleMove() {
    Tracker t;
    Run(t, 200, false, 6);
    CHECK(!t.Latest().valid);
    Run(t, 200, true, 4);
    CHECK(t.Latest().valid);
    CHECK(t.Latest().startup == 7);
    CHECK(t.Latest().active == 4);
    Run(t, 200, false, 18);
    End(t);
    CHECK(t.Latest().moveId == 200);
    CHECK(t.Latest().startup == 7);
    CHECK(t.Latest().active == 4);
    CHECK(t.Latest().recovery == 18);
    CHECK(!t.Latest().interrupted);
}

void TestChainStartsNewRecord() {
    Tracker t;
    Run(t, 200, false, 2);
    Run(t, 200, true, 2);
    Run(t, 200, false, 3);
    Run(t, 201, false, 5);
    Run(t, 201, true, 1);
    CHECK(t.Latest().moveId == 201);
    CHECK(t.Latest().startup == 6);
    CHECK(t.Latest().active == 1);
    CHECK(t.Latest().recovery == 0);
}

void TestPhaseStaysWithAttack() {
    Tracker t;
    Run(t, 256, false, 3);
    Run(t, 256, true, 2);
    Run(t, 257, false, 9, false);
    End(t);
    CHECK(t.Latest().moveId == 256);
    CHECK(t.Latest().recovery == 9);

    // A phase that goes active again extends the same move.
    Tracker m;
    Run(m, 256, false, 3);
    Run(m, 256, true, 2);
    Run(m, 257, false, 4, false);
    Run(m, 257, true, 2, false);
    CHECK(m.Latest().moveId == 256);
    CHECK(m.Latest().startup == 4);
    CHECK(m.Latest().active == 4);
    CHECK(m.Latest().recovery == 0);
}

void TestStartupPhaseCountsTowardStartup() {
    Tracker t;
    Run(t, 256, false, 3);
    Run(t, 257, false, 4, false);
    Run(t, 257, true, 1, false);
    CHECK(t.Latest().startup == 8);
    CHECK(t.Latest().moveId == 257);
}

void TestCancelIntoBoxlessMoveEndsRecovery() {
    Tracker t;
    Run(t, 200, false, 2);
    Run(t, 200, true, 2);
    Run(t, 200, false, 3);
    Run(t, 240, false, 40);
    CHECK(t.Latest().moveId == 200);
    CHECK(t.Latest().recovery == 3);
    End(t);
    CHECK(t.Latest().moveId == 240);
    CHECK(!t.Latest().hasActive);
    CHECK(t.Latest().recovery == 40);
}

void TestCancelledFeintDoesNotInflateStartup() {
    Tracker t;
    Run(t, 240, false, 20);
    Run(t, 200, false, 4);
    Run(t, 200, true, 1);
    CHECK(t.Latest().moveId == 200);
    CHECK(t.Latest().startup == 5);
}

void FeedContact(Tracker& t, short id) {
    Tick hit = Make(id, false);
    hit.boxesPresent = true;
    hit.contact = true;
    t.Feed(hit);
}

void FeedBoxesGateClosed(Tracker& t, short id, int ticks) {
    Tick closed = Make(id, false);
    closed.boxesPresent = true;
    for (int i = 0; i < ticks; ++i) t.Feed(closed);
}

void TestContactKeepsActiveWindow() {
    Tracker t;
    Run(t, 200, false, 2);
    Run(t, 200, true, 1);
    FeedContact(t, 200);
    FeedBoxesGateClosed(t, 200, 2);
    CHECK(t.Latest().active == 4);
    CHECK(t.Latest().recovery == 0);
    Run(t, 200, false, 5);
    End(t);
    CHECK(t.Latest().active == 4);
    CHECK(t.Latest().recovery == 5);

    // Whiffed boxes past the gate are not active.
    Tracker w;
    Run(w, 200, true, 2);
    FeedBoxesGateClosed(w, 200, 1);
    CHECK(w.Latest().active == 2);
    CHECK(w.Latest().recovery == 1);
}

void TestContactOnFirstActiveTick() {
    // Whiff: 4 ticks of startup, 3 of boxes, 5 of recovery.
    Tracker whiff;
    Run(whiff, 200, false, 4);
    Run(whiff, 200, true, 3);
    Run(whiff, 200, false, 5);
    End(whiff);

    // Point blank: the tick that opens the window connects and closes the gate.
    Tracker hit;
    Run(hit, 200, false, 4);
    FeedContact(hit, 200);
    FeedBoxesGateClosed(hit, 200, 2);
    Run(hit, 200, false, 5);
    End(hit);

    CHECK(hit.Latest().hasActive);
    CHECK(hit.Latest().startup == whiff.Latest().startup);
    CHECK(hit.Latest().startup == 5);
    CHECK(hit.Latest().active == whiff.Latest().active);
    CHECK(hit.Latest().recovery == whiff.Latest().recovery);
}

void TestContactOnBoxlessSample() {
    // The frame advanced in the contact tick: the sample already has no boxes.
    Tracker t;
    Run(t, 200, false, 3);
    Tick hit = Make(200, false);
    hit.contact = true;
    t.Feed(hit);
    Run(t, 200, false, 4);
    End(t);
    CHECK(t.Latest().startup == 4);
    CHECK(t.Latest().active == 1);
    CHECK(t.Latest().recovery == 4);
}

void TestContactLatchDoesNotCrossRecords() {
    Tracker t;
    Run(t, 200, false, 1);
    FeedContact(t, 200);
    Run(t, 201, false, 2);
    FeedBoxesGateClosed(t, 201, 2);
    End(t);
    CHECK(t.Latest().moveId == 201);
    CHECK(!t.Latest().hasActive);
}

void TestForcedGateWithoutContactIsNotActive() {
    // Scripts can force the latch with no contact: boxes that can never connect.
    Tracker t;
    Run(t, 250, false, 2);
    FeedBoxesGateClosed(t, 250, 4);
    Run(t, 250, false, 2);
    End(t);
    CHECK(!t.Latest().hasActive);
    CHECK(t.Latest().recovery == 8);
}

void TestWhiffedPhaseChangeKeepsActive() {
    // Nagamori 236236A: 300 then 303. Recovery in the phase stays recovery.
    Tracker t;
    Run(t, 300, false, 6);
    Run(t, 300, true, 4);
    Run(t, 303, false, 12, false);
    End(t);
    CHECK(t.Latest().moveId == 300);
    CHECK(t.Latest().startup == 7);
    CHECK(t.Latest().active == 4);
    CHECK(t.Latest().recovery == 12);
}

void TestFollowupCommandSplits() {
    using MoveTiming::StartsNewRecord;
    CHECK(StartsNewRecord(true, true, true));
    CHECK(!StartsNewRecord(true, false, true));
    CHECK(StartsNewRecord(false, false, true));   // Misuzu 211, Nagamori 216
    CHECK(!StartsNewRecord(false, false, false));

    // Akiko 623A~623A: the follow-up id is a command and gets its own record.
    Tracker t;
    Run(t, 253, false, 3);
    Run(t, 253, true, 2);
    Run(t, 253, false, 4);
    Run(t, 255, false, 5);
    Run(t, 255, true, 2);
    Run(t, 255, false, 6);
    End(t);
    CHECK(t.Latest().moveId == 255);
    CHECK(t.Latest().startup == 6);
    CHECK(t.Latest().active == 2);
    CHECK(t.Latest().recovery == 6);
}

void TestPerSideFreeze() {
    using MoveTiming::SideFrozen;
    CHECK(!SideFrozen(0, 0));
    CHECK(SideFrozen(3, 0));   // own hitstop
    CHECK(SideFrozen(0, 3));   // opponent's flash
    CHECK(!SideFrozen(-1, 0));
}

void TestAttackerThrowIsNotThrown() {
    using MoveTiming::ReadsAsThrown;
    CHECK(!ReadsAsThrown(6, false, true));
    CHECK(ReadsAsThrown(6, false, false));
    CHECK(ReadsAsThrown(0, true, false));
    CHECK(!ReadsAsThrown(0, false, false));

    // The throw record keeps its contact and finishes uninterrupted.
    Tracker t;
    Run(t, 220, false, 2);
    FeedContact(t, 220);
    Run(t, 221, false, 30, false);
    End(t);
    CHECK(t.Latest().moveId == 220);
    CHECK(t.Latest().startup == 3);
    CHECK(t.Latest().active == 1);
    CHECK(t.Latest().recovery == 30);
    CHECK(!t.Latest().interrupted);
}

void TestMultiHitKeepsFirstStartup() {
    Tracker t;
    Run(t, 210, false, 4);
    Run(t, 210, true, 2);
    Run(t, 210, false, 3);
    Run(t, 210, true, 2);
    Run(t, 210, false, 5);
    End(t);
    CHECK(t.Latest().startup == 5);
    CHECK(t.Latest().active == 4);
    CHECK(t.Latest().recovery == 5);
}

void TestInterrupted() {
    Tracker t;
    Run(t, 200, false, 3);
    End(t, true);
    CHECK(!t.Latest().valid);

    Run(t, 200, false, 3);
    Run(t, 200, true, 1);
    Run(t, 200, false, 2);
    End(t, true);
    CHECK(t.Latest().hasActive);
    CHECK(t.Latest().interrupted);
}

void TestLabelFollowsRecord() {
    const char* const first = "5A";
    const char* const second = "5B";
    Tracker t;
    Tick a = Make(200, true);
    a.label = first;
    t.Feed(a);
    CHECK(t.Latest().label == first);
    Tick b = Make(201, true);
    b.label = second;
    t.Feed(b);
    CHECK(t.Latest().label == second);
}

void TestEngineConnectGate() {
    using CollisionBoxPolicy::CharacterHitCanConnect;
    CHECK(CharacterHitCanConnect(1, 0, 1));
    CHECK(!CharacterHitCanConnect(0, 0, 1));
    CHECK(!CharacterHitCanConnect(1, 1, 1));
    CHECK(!CharacterHitCanConnect(1, 0, 0));
    CHECK(!CharacterHitCanConnect(1, 0, -1));
}

} // namespace

int main() {
    TestSimpleMove();
    TestChainStartsNewRecord();
    TestPhaseStaysWithAttack();
    TestStartupPhaseCountsTowardStartup();
    TestCancelIntoBoxlessMoveEndsRecovery();
    TestCancelledFeintDoesNotInflateStartup();
    TestContactKeepsActiveWindow();
    TestContactOnFirstActiveTick();
    TestContactOnBoxlessSample();
    TestContactLatchDoesNotCrossRecords();
    TestForcedGateWithoutContactIsNotActive();
    TestWhiffedPhaseChangeKeepsActive();
    TestFollowupCommandSplits();
    TestPerSideFreeze();
    TestAttackerThrowIsNotThrown();
    TestMultiHitKeepsFirstStartup();
    TestInterrupted();
    TestLabelFollowsRecord();
    TestEngineConnectGate();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("move_timing_policy_tests: all checks passed\n");
    return 0;
}
