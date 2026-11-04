#include "game/practice_snapshots.h"
#include "utils/xp_compat.h"
#include <algorithm>
#include <mutex>

namespace {
std::mutex snapshotMutex;
std::array<TriggerOptionList, 5> optionGroups{};
PerFrameSample frameSample{};
FrameSnapshot frameSnapshot{};

int OptionIndex(int triggerType) {
    switch (triggerType) {
    case TRIGGER_AFTER_BLOCK: return 0;
    case TRIGGER_ON_WAKEUP: return 1;
    case TRIGGER_AFTER_HITSTUN: return 2;
    case TRIGGER_AFTER_AIRTECH: return 3;
    case TRIGGER_ON_RG: return 4;
    default: return -1;
    }
}
}

void PublishTriggerOptions(const DisplayData& data) {
    std::array<TriggerOptionList, 5> next{};
    auto copy = [&](size_t index, int count, const TriggerOption* rows) {
        next[index].count = (std::max)(0, (std::min)(count, MAX_TRIGGER_OPTIONS));
        std::copy_n(rows, next[index].count, next[index].rows.begin());
    };
    copy(0, data.afterBlockOptionCount, data.afterBlockOptions);
    copy(1, data.onWakeupOptionCount, data.onWakeupOptions);
    copy(2, data.afterHitstunOptionCount, data.afterHitstunOptions);
    copy(3, data.afterAirtechOptionCount, data.afterAirtechOptions);
    copy(4, data.onRGOptionCount, data.onRGOptions);
    std::lock_guard<std::mutex> lock(snapshotMutex);
    optionGroups = next;
}

TriggerOptionList GetTriggerOptions(int triggerType) {
    const int index = OptionIndex(triggerType);
    if (index < 0) return {};
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return optionGroups[index];
}

void PublishPracticeFrameSample(const PerFrameSample& sample) {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    frameSample = sample;
}

PerFrameSample GetCurrentPerFrameSample() {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return frameSample;
}

void PublishFrameSnapshot(const FrameSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    frameSnapshot = snapshot;
}

bool TryGetLatestSnapshot(FrameSnapshot& out, unsigned int maxAgeMs) {
    FrameSnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        snapshot = frameSnapshot;
    }
    const auto now = XPCompat::GetTickCount64Compat();
    if (!snapshot.tickMs || now - snapshot.tickMs > maxAgeMs) return false;
    out = snapshot;
    return true;
}
