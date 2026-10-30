#include "audio_transaction_test_support.h"
#include "utils/audio_runtime_state.h"
#define GetPrivateProfileIntA InterceptIniRead
#define WritePrivateProfileStringA InterceptIniWrite
#include "../../src/utils/extended_config_bridge.cpp"
#undef GetPrivateProfileIntA
#undef WritePrivateProfileStringA
namespace Config {
Settings settings{}; unsigned calls=0;
std::unordered_map<std::string,std::unordered_map<std::string,std::string>> iniData;
void SetIniValue(const std::string& section,const std::string& key,const std::string& value) { iniData[section][key]=value; }
#include "utils/audio_config_publication.inl"
const Settings& GetSettings() { return settings; }
void SetSetting(const std::string& section,const std::string& key,const std::string& value) {
 ++calls; TrySetAudioSetting(section,key,value);
}
}
void LogOut(const std::string&,bool) {}
// The bridge pushes each published tuple onto the live buffers; the test links no audio owner.
namespace AudioControl { void ApplyConfiguredVolumesNow() {} }
void SetupTrainingFile(const std::string& path) {
 ExtendedConfigBridge::g_admitted=true;ExtendedConfigBridge::g_status={};
 ExtendedConfigBridge::g_status.sharedConfigPath=path;ExtendedConfigBridge::g_status.sharedConfigFound=true;
 Config::calls=0;
}
bool TrainingRefresh() {return ExtendedConfigBridge::Refresh(true);}
bool TrainingImport() {return ExtendedConfigBridge::ImportAudioSettingsIfAvailable(true);}
bool TrainingWrite(int b,int s) {return ExtendedConfigBridge::PublishAudioSettings(b,s);}
AudioTuple TrainingTuple() {auto v=AudioControl::ReadAudioSettings();return {v.bgmPercent,v.sePercent,0};}
unsigned TrainingMirrorCalls() {return Config::calls;}

bool TrainingLaneWrite(bool bgm,int p) {return ExtendedConfigBridge::PublishAudioLaneSetting(bgm,p);}
void TrainingConfigLane(bool bgm,int p) {Config::TrySetAudioSetting("General",bgm?"bgmVolumePercent":"seVolumePercent",std::to_string(p));}
void TrainingConfigLoad(int b,int s) {Config::SetAudioSettingsMirror(b,s);Config::PublishLoadedAudioSettings();}
AudioTuple TrainingMirror() {return {Config::settings.bgmVolumePercent,Config::settings.seVolumePercent,0};}

bool TrainingMapNormalized() {return Config::iniData["general"]["bgmvolumepercent"]==std::to_string(Config::settings.bgmVolumePercent)&&Config::iniData["general"]["sevolumepercent"]==std::to_string(Config::settings.seVolumePercent)&&Config::iniData.count("General")==0;}
