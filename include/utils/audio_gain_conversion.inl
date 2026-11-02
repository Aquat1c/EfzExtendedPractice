int PercentToDirectSoundVolume(int percent, int baseDirectSoundVolume) {
    if (percent <= 0) {
        return kMinDirectSoundVolume;
    }

    if (percent <= 100 && baseDirectSoundVolume >= -10000 && baseDirectSoundVolume <= 0) {
        return EfzAudioApplyGain(baseDirectSoundVolume, EfzAudioGainForPercent(percent));
    }

    // Out-of-domain input deliberately keeps this floating-point conversion instead of the gain table.
    const double baseAmplitude = std::pow(10.0, static_cast<double>(baseDirectSoundVolume) / 2000.0);
    const double scaledAmplitude = baseAmplitude * (static_cast<double>(percent) / 100.0);
    if (scaledAmplitude <= 0.0) {
        return kMinDirectSoundVolume;
    }

    const double directSoundVolume = 2000.0 * std::log10(scaledAmplitude);
    return std::clamp(static_cast<int>(std::lround(directSoundVolume)),
                      kMinDirectSoundVolume,
                      kMaxDirectSoundVolume);
}
