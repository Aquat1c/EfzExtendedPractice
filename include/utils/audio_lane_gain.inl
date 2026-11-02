// Whether training owns a lane comes only from the acknowledged tuple in the view.
inline int ApplyAudioLaneGain(const AudioSettingsView& view, bool bgm, int raw) noexcept {
    const int percent = bgm ? view.bgmPercent : view.sePercent;
    const bool owns = bgm ? view.trainingOwnsBgmGain : view.trainingOwnsSeGain;
    if (!owns) return raw;
    return EfzAudioApplyGain(raw, EfzAudioGainForPercent(percent));
}
