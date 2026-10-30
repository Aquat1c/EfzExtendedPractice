// Mirror updates never publish.
void SetAudioSettingsMirror(int bgm, int se) {
    settings.bgmVolumePercent = (std::clamp)(bgm, 0, 100);
    settings.seVolumePercent = (std::clamp)(se, 0, 100);
    SetIniValue("general", "bgmvolumepercent", std::to_string(settings.bgmVolumePercent));
    SetIniValue("general", "sevolumepercent", std::to_string(settings.seVolumePercent));
}
void PublishLoadedAudioSettings() {
    EfzAudioFileTransaction transaction;
    if (transaction) AudioControl::PublishAudioPercents(settings.bgmVolumePercent, settings.seVolumePercent);
}
bool TrySetAudioSetting(const std::string& section, const std::string& key, const std::string& value) {
    if (_stricmp(section.c_str(), "General") != 0) return false;
    const bool bgm = _stricmp(key.c_str(), "bgmVolumePercent") == 0;
    if (!bgm && _stricmp(key.c_str(), "seVolumePercent") != 0) return false;
    EfzAudioFileTransaction transaction;
    if (!transaction) return true;
    int percent = 100;
    try { percent = std::stoi(value); } catch (...) {}
    const auto view = AudioControl::ReadAudioSettings();
    SetAudioSettingsMirror(bgm ? percent : view.bgmPercent, bgm ? view.sePercent : percent);
    AudioControl::PublishAudioPercents(settings.bgmVolumePercent, settings.seVolumePercent);
    return true;
}
