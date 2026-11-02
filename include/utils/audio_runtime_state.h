#pragma once
#include <cstdint>
#include "utils/audio_owner_abi.h"
#include "utils/audio_gain_core.h"
namespace AudioControl {
struct AudioSettingsView {
    uint8_t bgmPercent, sePercent;
    bool trainingOwnsBgmGain, trainingOwnsSeGain;
    uint16_t publication;
};
inline uint32_t PackAudioSettings(AudioSettingsView v) noexcept {
    return uint32_t(v.bgmPercent) | (uint32_t(v.sePercent) << 7) |
        (uint32_t(v.trainingOwnsBgmGain) << 14) | (uint32_t(v.trainingOwnsSeGain) << 15) |
        (uint32_t(v.publication) << 16);
}
inline AudioSettingsView UnpackAudioSettings(uint32_t word) noexcept {
    return {uint8_t(word & 127), uint8_t((word >> 7) & 127),
        ((word >> 14) & 1) != 0, ((word >> 15) & 1) != 0, uint16_t(word >> 16)};
}
AudioSettingsView ReadAudioSettings() noexcept;
// Owner flags are derived from the acknowledged control state, never the caller.
void PublishAudioSettingsView(AudioSettingsView view) noexcept;
void PublishAudioPercents(int bgm, int se) noexcept;
// Setup only, before hooks or worker start. The provider must remain retained.
void BindAudioBoundary(const EfzAudioOwnerApiV1* provider) noexcept;
void EnterAudioBoundary() noexcept;
void LeaveAudioBoundary() noexcept;
// Called on the serialized boundary with the exact queried/returned generation.
bool AcknowledgeAudioOwner(const EfzAudioOwnerV1& record) noexcept;
void MarkIncompatibleAudioOwner(uint32_t lanes) noexcept;
bool ConsumeAudioApplyRequest() noexcept;
#include "utils/audio_lane_gain.inl"
}
