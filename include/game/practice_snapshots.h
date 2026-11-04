#pragma once
#include "game/frame_monitor.h"
#include "game/per_frame_sample.h"
#include "utils/utilities.h"
#include <array>

struct TriggerOptionList {
    int count = 0;
    std::array<TriggerOption, MAX_TRIGGER_OPTIONS> rows{};
};

void PublishTriggerOptions(const DisplayData& data);
TriggerOptionList GetTriggerOptions(int triggerType);
void PublishPracticeFrameSample(const PerFrameSample& sample);
void PublishFrameSnapshot(const FrameSnapshot& snapshot);
