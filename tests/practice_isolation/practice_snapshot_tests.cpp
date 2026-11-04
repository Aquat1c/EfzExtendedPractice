#include "game/practice_snapshots.h"
#include "utils/xp_compat.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>

namespace {
int failures = 0;
std::atomic<unsigned long long> now{1000};
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); ++failures; } } while (false)

void Publish(unsigned generation) {
    DisplayData options{};
    options.afterBlockOptionCount = MAX_TRIGGER_OPTIONS;
    for (auto& row : options.afterBlockOptions) {
        row = {true, static_cast<int>(generation), static_cast<int>(generation + 1), 0, 0, 0, 0};
    }
    PublishTriggerOptions(options);
    PerFrameSample sample{};
    sample.frame = generation;
    sample.moveID1 = static_cast<short>(generation % 10000);
    sample.actionable1 = (generation % 2) != 0;
    PublishPracticeFrameSample(sample);
    FrameSnapshot frame{};
    frame.tickMs = 1000;
    frame.p1Hp = static_cast<int>(generation);
    frame.p2Hp = -static_cast<int>(generation);
    PublishFrameSnapshot(frame);
}
}

namespace XPCompat {
unsigned long long GetTickCount64Compat() { return now.load(); }
}

int main() {
    CHECK(!std::is_reference<decltype(GetCurrentPerFrameSample())>::value);
    CHECK(GetCurrentPerFrameSample().frame == 0);
    FrameSnapshot frame{};
    CHECK(!TryGetLatestSnapshot(frame, 100));

    Publish(1);
    const auto saved = GetCurrentPerFrameSample();
    const auto savedOptions = GetTriggerOptions(TRIGGER_AFTER_BLOCK);
    Publish(2);
    CHECK(saved.frame == 1 && saved.moveID1 == 1 && saved.actionable1);
    CHECK(savedOptions.count == MAX_TRIGGER_OPTIONS && savedOptions.rows[0].action == 1);
    CHECK(GetTriggerOptions(-1).count == 0);
    CHECK(TryGetLatestSnapshot(frame, 100));
    now.store(1101);
    CHECK(!TryGetLatestSnapshot(frame, 100));
    now.store(1000);

    DisplayData bounds{};
    bounds.afterBlockOptionCount = MAX_TRIGGER_OPTIONS + 20;
    bounds.onWakeupOptionCount = -1;
    bounds.afterHitstunOptionCount = 1;
    bounds.afterHitstunOptions[0].action = 37;
    bounds.afterAirtechOptionCount = 1;
    bounds.afterAirtechOptions[0].action = 38;
    bounds.onRGOptionCount = 1;
    bounds.onRGOptions[0].action = 39;
    PublishTriggerOptions(bounds);
    CHECK(GetTriggerOptions(TRIGGER_AFTER_BLOCK).count == MAX_TRIGGER_OPTIONS);
    CHECK(GetTriggerOptions(TRIGGER_ON_WAKEUP).count == 0);
    CHECK(GetTriggerOptions(TRIGGER_AFTER_HITSTUN).rows[0].action == 37);
    CHECK(GetTriggerOptions(TRIGGER_AFTER_AIRTECH).rows[0].action == 38);
    CHECK(GetTriggerOptions(TRIGGER_ON_RG).rows[0].action == 39);

    Publish(1);
    std::atomic<bool> start{false};
    std::thread writer([&] {
        while (!start.load()) std::this_thread::yield();
        for (unsigned generation = 2; generation < 20000; ++generation) Publish(generation);
    });
    start.store(true);
    for (int i = 0; i < 20000; ++i) {
        const auto sample = GetCurrentPerFrameSample();
        CHECK(sample.moveID1 == static_cast<short>(sample.frame % 10000));
        CHECK(sample.actionable1 == ((sample.frame % 2) != 0));
        const auto options = GetTriggerOptions(TRIGGER_AFTER_BLOCK);
        CHECK(options.count == MAX_TRIGGER_OPTIONS);
        for (const auto& row : options.rows) {
            CHECK(row.enabled && row.action == options.rows[0].action);
            CHECK(row.strength == row.action + 1);
        }
        CHECK(TryGetLatestSnapshot(frame, 100));
        CHECK(frame.p1Hp == -frame.p2Hp);
    }
    writer.join();
    return failures ? 1 : 0;
}
